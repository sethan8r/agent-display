"""Render README images from the mockup: animated demo + a strip of screens, for each language.

Usage: py -3 design/capture.py   (needs Edge or Chrome and Pillow)
Output: docs/img/demo-{ru,en}.gif, docs/img/screens-{ru,en}.png
"""
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "docs" / "img"
PAGE = (HERE / "index.html").as_uri()
BROWSERS = [
    Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft/Edge/Application/msedge.exe",
    Path(os.environ.get("ProgramFiles", "")) / "Microsoft/Edge/Application/msedge.exe",
    Path(os.environ.get("ProgramFiles", "")) / "Google/Chrome/Application/chrome.exe",
]
BROWSER = next(str(p) for p in BROWSERS if p.is_file())
PROFILE = tempfile.mkdtemp(prefix="capture-")
BASE = [BROWSER, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
        f"--user-data-dir={PROFILE}", "--virtual-time-budget=2500"]


def shot(query, size, out, transparent=False):
    extra = ["--default-background-color=00000000"] if transparent else []
    subprocess.run(BASE + extra + [f"--window-size={size[0]},{size[1]}", f"--screenshot={out}", f"{PAGE}?{query}"],
                   capture_output=True, check=True)


def story_durations():
    dom = subprocess.run(BASE + ["--dump-dom", f"{PAGE}?cap=meta"], capture_output=True, check=True).stdout.decode("utf-8")
    return json.loads(re.search(r'<pre id="meta">(.*?)</pre>', dom, re.S).group(1))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    durations = story_durations()
    with tempfile.TemporaryDirectory() as tmp:
        for lang in ("ru", "en"):
            frames = []
            for i in range(len(durations)):
                png = Path(tmp) / f"{lang}-{i}.png"
                shot(f"cap=story&lang={lang}&f={i}", (360, 480), png)
                frames.append(Image.open(png).convert("RGB"))
            gif = OUT / f"demo-{lang}.gif"
            frames[0].save(gif, save_all=True, append_images=frames[1:], duration=durations, loop=0, optimize=True)
            # 5 screens × (240 px × 1.25) + 4 gaps × 24 px
            shot(f"cap=strip&lang={lang}", (5 * 300 + 4 * 24, 400), OUT / f"screens-{lang}.png", transparent=True)
            print(f"{lang}: {len(frames)} frames, {gif.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
