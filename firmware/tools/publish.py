"""Build the firmware and put the ready images into the plugin, so /agent-display:update-firmware can flash them.

Usage: py -3 firmware/tools/publish.py   (needs PlatformIO)
Output: plugin/firmware/{bootloader,partitions,firmware}.bin and firmware.json (version + flash offsets)
Bump FW_VERSION in firmware/src/app/model.h before publishing a new firmware, and the plugin version too.
"""
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

FW = Path(__file__).resolve().parents[1]
OUT = FW.parent / "plugin" / "firmware"
PIO = Path.home() / ".platformio" / "penv" / ("Scripts/pio.exe" if os.name == "nt" else "bin/pio")
FILES = {"0x1000": "bootloader.bin", "0x8000": "partitions.bin", "0x10000": "firmware.bin"}  # partitions.csv layout


def main():
    subprocess.run([str(PIO), "run", "-e", "main"], cwd=FW, check=True)
    model = (FW / "src" / "app" / "model.h").read_text(encoding="utf-8")
    version = re.search(r'FW_VERSION = "([^"]+)"', model).group(1)
    proto = int(re.search(r"PROTO = (\d+)", model).group(1))
    build = FW / ".pio" / "build" / "main"
    OUT.mkdir(parents=True, exist_ok=True)
    for name in FILES.values():
        shutil.copyfile(build / name, OUT / name)
    manifest = {"version": version, "proto": proto, "chip": "esp32", "flash_size": "4MB", "files": FILES,
                "app": "0x10000"}  # an update writes only the app: NVS (settings, touch calibration) stays
    (OUT / "firmware.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"firmware {version} (protocol {proto}) -> {OUT}")


if __name__ == "__main__":
    main()
