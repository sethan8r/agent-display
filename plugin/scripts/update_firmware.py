"""Flash the firmware bundled with the plugin onto the board: the /agent-display:update-firmware command.

Usage: update_firmware.py status | flash [--full] [--port COMx]

flash lets the hub release the board's port, runs the vendored esptool and hands the port back. Without --full
only the app is written, so settings and touch calibration stay. --full also writes the bootloader and the
partition table: needed once for a new board. A board that never answered hello gets --full by itself.
"""
import json
import os
import subprocess
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import board  # noqa: E402
import hub  # noqa: E402

FIRMWARE = os.path.join(HERE, "..", "firmware")
VENDOR = os.path.join(HERE, "..", "vendor")


def manifest():
    with open(os.path.join(FIRMWARE, "firmware.json"), encoding="utf-8") as f:
        return json.load(f)


def hub_board():
    """What the running hub knows about the board, or None if no hub runs."""
    try:
        return json.loads(urllib.request.urlopen(hub.URL + "/board", timeout=0.5).read())
    except (OSError, ValueError):
        return None


def status():
    info = hub_board()
    print("plugin firmware:", manifest()["version"])
    if info and info.get("hello"):
        print(f"board firmware: {info['hello'].get('fw')} (port {info['port']})")
    else:
        print("board firmware: unknown (a new board, another firmware, or not plugged in)")
    port = (info or {}).get("port") or board.Board(None).find()
    print("port:", port or "no board found")


def flash(full, port):
    m = manifest()
    info = hub_board()
    if info:
        full = full or not info.get("hello")
        port = port or info.get("port")
        hub.post("/release", {"seconds": 180})
        for _ in range(40):  # until the hub has really closed the port
            now = hub_board()
            if not now or not now.get("port"):
                break
            time.sleep(0.1)
    port = port or board.Board(None).find()
    if not port:
        print("no board found: plug it in with a micro-USB data cable")
        return 1
    files = m["files"] if full else {m["app"]: m["files"][m["app"]]}
    images = [x for offset, name in files.items() for x in (offset, os.path.join(FIRMWARE, name))]
    env = dict(os.environ, PYTHONPATH=VENDOR + os.pathsep + os.environ.get("PYTHONPATH", ""))
    rc = 1
    for baud in ("921600", "115200"):  # some USB bridges and cables don't manage the fast rate
        print(f"{'full flash' if full else 'app update'}: firmware {m['version']} -> {port} at {baud} baud", flush=True)
        rc = subprocess.run([sys.executable, "-m", "esptool", "--chip", m["chip"], "--port", port, "--baud", baud,
                             "--before", "default_reset", "--after", "hard_reset", "write_flash", "-z",
                             "--flash_mode", "dio", "--flash_freq", "40m", "--flash_size", m["flash_size"], *images],
                            env=env).returncode
        if rc == 0:
            break
    if info:
        hub.post("/release", {"seconds": 0})  # take the port back
    print("OK: the board restarts with the new firmware" if rc == 0 else f"FAILED (esptool exit code {rc})")
    return rc


def main(argv):
    if len(argv) > 1 and argv[1] == "status":
        status()
        return 0
    if len(argv) > 1 and argv[1] == "flash":
        port = argv[argv.index("--port") + 1] if "--port" in argv else None
        return flash("--full" in argv, port)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
