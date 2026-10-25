"""agent-display hub: the one process that owns the board's COM port.

Claude Code hooks POST their events here (127.0.0.1:47800), the status line wrapper POSTs its JSON, and every
second (or right after a change) the hub sends the board a full snapshot. Started by session_start.py,
exits by itself when no Claude Code session is left. GET /snapshot serves the same JSON to the browser mockup.
"""
import json
import logging
import os
import subprocess
import sys
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from logging.handlers import RotatingFileHandler

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import procs  # noqa: E402
import state  # noqa: E402

PORT = 47800
URL = f"http://127.0.0.1:{PORT}"
IDLE_EXIT_S = 300   # no sessions for this long: exit
MIN_GAP_S = 0.2     # at most 5 snapshots a second
NO_PID_TTL_S = 6 * 3600  # sessions whose process is unknown are dropped after this long without events


def data_dir():
    d = os.environ.get("CLAUDE_PLUGIN_DATA") or os.path.join(os.path.expanduser("~"), ".agent-display")
    os.makedirs(d, exist_ok=True)
    return d


# ---------- client side: used by the hook and status line scripts ----------

def ping():
    try:
        return urllib.request.urlopen(URL + "/ping", timeout=0.3).read() == b"agent-display"
    except OSError:
        return False


def post(path, payload, timeout=0.5):
    try:
        req = urllib.request.Request(URL + path, json.dumps(payload).encode(), {"Content-Type": "application/json"})
        urllib.request.urlopen(req, timeout=timeout).read()
        return True
    except OSError:
        return False


def ensure_running():
    """Start the hub in the background unless it already answers. Never raises."""
    if ping():
        return
    exe = sys.executable
    kw = dict(stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=data_dir())
    if procs.WIN:
        windowless = os.path.join(os.path.dirname(exe), "pythonw.exe")
        exe = windowless if os.path.exists(windowless) else exe
        flags = 0x00000008 | 0x00000200 | 0x08000000  # DETACHED_PROCESS | NEW_PROCESS_GROUP | NO_WINDOW
        try:  # leave the hook's job object, so the hub outlives the hook that started it
            subprocess.Popen([exe, os.path.join(HERE, "hub.py")], creationflags=flags | 0x01000000, **kw)
            return
        except OSError:
            kw["creationflags"] = flags
    else:
        kw["start_new_session"] = True
    try:
        subprocess.Popen([exe, os.path.join(HERE, "hub.py")], **kw)
    except OSError:
        pass


# ---------- the hub itself ----------

class Hub:
    def __init__(self, log):
        import board  # here, so the client functions above stay light
        self.log, self.lock, self.changed = log, threading.Lock(), threading.Event()
        self.state, self.board = state.State(), board.Board(log)
        self.last_snapshot, self.empty_since = None, time.time()

    def handle(self, path, body, client_port):
        with self.lock:
            s = self.state.status_line(body) if path == "/status" else self.state.event(body)
            if s and not s.pid:
                if body.get("pid"):  # sent by session_start.py / statusline.py, which know their Claude ancestor
                    s.pid, s.pid_created = body["pid"], body.get("pid_created")
                elif path == "/hook":  # an HTTP hook: Claude Code itself is on the other end of this socket
                    pid = procs.tcp_client_pid(client_port, PORT)
                    if pid:
                        s.pid, s.pid_created = pid, procs.created(pid)
                if s.pid:
                    self.log.info("session %s is pid %s", s.id[:8], s.pid)
        self.changed.set()

    def drop_closed(self, now):
        """Windows closed without /exit send no SessionEnd: forget sessions whose process is gone."""
        for sid, s in list(self.state.sessions.items()):
            gone = not procs.alive(s.pid, s.pid_created) if s.pid else now - s.last_event > NO_PID_TTL_S
            if gone:
                self.log.info("session %s closed", sid[:8])
                del self.state.sessions[sid]
                self.changed.set()

    def run(self):
        last_sent = 0
        while True:
            self.changed.wait(1)
            now = time.time()
            if now - last_sent < MIN_GAP_S:
                time.sleep(MIN_GAP_S - (now - last_sent))
                now = time.time()
            self.changed.clear()
            with self.lock:
                self.drop_closed(now)
                self.state.check_interrupts(now)
                snap = self.state.snapshot(now)
                if self.state.sessions:
                    self.empty_since = now
            self.last_snapshot = snap
            self.board.send(snap)
            last_sent = now
            if now - self.empty_since > IDLE_EXIT_S:
                self.log.info("no sessions for %d s, exiting", IDLE_EXIT_S)
                self.board.close()
                return


def serve(hub):
    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            try:
                body = json.loads(self.rfile.read(int(self.headers.get("Content-Length") or 0)) or b"{}")
            except ValueError:
                body = None
            self.send_response(200)  # answer first and fast: a hook must never wait on the hub
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b"{}")
            if isinstance(body, dict) and self.path in ("/hook", "/status"):
                hub.handle(self.path, body, self.client_address[1])

        def do_GET(self):
            out = b"agent-display" if self.path == "/ping" else json.dumps(hub.last_snapshot).encode()
            self.send_response(200 if self.path in ("/ping", "/snapshot") else 404)
            self.send_header("Access-Control-Allow-Origin", "*")  # the mockup opened from file:// reads /snapshot
            self.end_headers()
            self.wfile.write(out)

        def log_message(self, *a):
            pass

    class Server(ThreadingHTTPServer):
        allow_reuse_address = False  # on Windows reuse would let a second hub bind the same port
        daemon_threads = True

    return Server(("127.0.0.1", PORT), Handler)


def main():
    log = logging.getLogger("hub")
    handler = RotatingFileHandler(os.path.join(data_dir(), "hub.log"), maxBytes=200_000, backupCount=1, encoding="utf-8")
    handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s"))
    log.addHandler(handler)
    log.setLevel(logging.INFO)
    hub = Hub(log)
    try:
        server = serve(hub)
    except OSError:
        return  # another hub already has the port
    log.info("hub started, pid %s", os.getpid())
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        hub.run()
    except Exception:
        log.exception("hub crashed")
        raise


if __name__ == "__main__":
    main()
