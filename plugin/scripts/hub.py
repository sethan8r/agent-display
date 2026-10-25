"""agent-display hub: the one process that owns the board's COM port.

Claude Code hooks POST their events here (127.0.0.1:47800), the status line wrapper POSTs its JSON, and every
second (or right after a change) the hub sends the board a full snapshot. Started by session_start.py or the status
line wrapper, exits by itself when no Claude Code session is left. GET /snapshot serves the same JSON to the browser
mockup (design/index.html opened from disk).
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
    """Hub log and the status line wrapper. Not the plugin data dir: Claude Code deletes that on uninstall, and the
    user's status line must survive an uninstall."""
    d = os.environ.get("AGENT_DISPLAY_HOME") or os.path.join(os.path.expanduser("~"), ".claude", "agent-display")
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
    if not ping():
        spawn()


def spawn():
    """Start a hub; if one is already running, the new one finds the port taken and exits."""
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
                if "pid" in body:  # from session_start.py / statusline.py: their Claude ancestor, or null if not found
                    pid = body["pid"]
                    if isinstance(pid, int) and 0 < pid < 1 << 32:
                        s.pid, s.pid_created = pid, body.get("pid_created")
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
            try:  # one bad event or a flaky port must not take the hub down
                with self.lock:
                    self.drop_closed(now)
                    self.state.check_interrupts(now)
                    snap = self.state.snapshot(now)
                    if self.state.sessions:
                        self.empty_since = now
                self.last_snapshot = snap
                self.board.send(snap)
            except Exception:  # noqa: BLE001
                self.log.exception("loop")
            last_sent = now
            if now - self.empty_since > IDLE_EXIT_S:
                self.log.info("no sessions for %d s, exiting", IDLE_EXIT_S)
                self.board.close()
                return


def serve(hub):
    class Handler(BaseHTTPRequestHandler):
        def local(self):
            # web pages: browsers send Origin; DNS rebinding: a foreign Host. Claude Code and our scripts send neither.
            host_ok = self.headers.get("Host", "").split(":")[0] in ("127.0.0.1", "localhost")
            return host_ok and self.headers.get("Origin") in (None, "null")

        def do_POST(self):
            size = int(self.headers.get("Content-Length") or 0)
            try:
                body = json.loads(self.rfile.read(size) or b"{}") if size < 1 << 20 else None
            except ValueError:
                body = None
            self.send_response(200 if self.local() else 403)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b"{}")
            # the hook's request completes when this returns (HTTP/1.0, no Content-Length), so the work under the
            # lock stays tiny; it also keeps the socket open for the PID lookup and one session's hooks in order
            if self.local() and isinstance(body, dict) and self.path in ("/hook", "/status"):
                hub.handle(self.path, body, self.client_address[1])

        def do_GET(self):
            out = b"agent-display" if self.path == "/ping" else json.dumps(hub.last_snapshot).encode()
            self.send_response(200 if self.local() and self.path in ("/ping", "/snapshot") else 404)
            self.send_header("Access-Control-Allow-Origin", "null")  # only pages opened from disk: the mockup
            self.end_headers()
            if self.local():
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
