"""The board on USB: find it, keep the port, send snapshots, read what it says back (docs/protocol.md)."""
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "vendor"))
import serial  # noqa: E402  (vendored pyserial)
from serial.tools import list_ports  # noqa: E402

BAUD = 921600
HELLO_WAIT_S = 5  # a device that doesn't answer is not our board (a printer, another board): let it go
USB_IDS = {(0x1A86, 0x7523), (0x1A86, 0x55D4), (0x10C4, 0xEA60)}  # CH340, CH9102, CP2102: USB bridges on CYD boards


class Board:
    def __init__(self, log):
        self.log, self.port, self.hello, self._next_try, self._rx = log, None, None, 0, b""
        self.opened_at, self.skip, self.paused_until = 0, {}, 0  # skip: device -> time until which it's left alone

    def find(self):
        wanted = os.environ.get("AGENT_DISPLAY_PORT")
        for p in list_ports.comports():
            if self.skip.get(p.device, 0) > time.time():
                continue
            if p.device == wanted or (not wanted and (p.vid, p.pid) in USB_IDS):
                return p.device
        return None

    def send(self, snap):
        """Write one snapshot line; (re)connects when needed, never raises."""
        if not self.port and not self._connect():
            return False
        try:
            self.port.write((json.dumps(snap, ensure_ascii=False, separators=(",", ":")) + "\n").encode())
            self._read()
            if not self.hello and time.time() - self.opened_at > HELLO_WAIT_S:
                self.log.info("%s never said hello, not our board", self.port.port)
                self.skip[self.port.port] = time.time() + 60
                self.close()
                return False
            return True
        except (serial.SerialException, OSError) as e:
            self.log.warning("board lost: %s", e)
            self.close()
            return False

    def pause(self, seconds):
        """Let go of the port for a while (a firmware update needs it); 0 resumes right away."""
        self.close()
        self.paused_until = time.time() + seconds

    def _connect(self):
        if time.time() < max(self._next_try, self.paused_until):
            return False
        self._next_try = time.time() + 2
        device = self.find()
        if not device:
            return False
        p = serial.Serial()
        p.port, p.baudrate, p.timeout, p.write_timeout = device, BAUD, 0, 2
        p.dtr = p.rts = False  # opening the port must not reset the board
        try:
            p.open()
        except (serial.SerialException, OSError) as e:
            self.log.warning("can't open %s: %s", device, e)
            return False
        self.port, self.hello, self._rx, self.opened_at = p, None, b"", time.time()
        p.write(b'{"t":"ping"}\n')  # ask for a hello right away
        self.log.info("board on %s", device)
        return True

    def _read(self):
        if self.port.in_waiting:
            self._rx = (self._rx + self.port.read(self.port.in_waiting))[-8192:]
        while b"\n" in self._rx:
            raw, self._rx = self._rx.split(b"\n", 1)
            line = raw.decode("utf-8", "replace").strip()
            if not line.startswith("{"):
                continue  # boot messages and other noise
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            if msg.get("t") == "hello":
                self.hello = msg
                self.log.info("board says hello: %s", msg)
            elif msg.get("t") == "log":
                self.log.info("board: %s", msg.get("msg"))

    def close(self):
        if self.port:
            try:
                self.port.close()
            except (serial.SerialException, OSError):
                pass
        self.port = None
