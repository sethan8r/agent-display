"""Download the Inter font (regular + semibold TTF) that the firmware embeds; LVGL's TinyTTF renders it on the board.

Usage: py -3 firmware/tools/gen_fonts.py
Output: firmware/fonts/Inter-400.ttf, Inter-600.ttf, OFL-Inter.txt (embedded via board_build.embed_files)
"""
import re
import urllib.request
from pathlib import Path

OUT = Path(__file__).resolve().parents[1] / "fonts"
WEIGHTS = (400, 600)
OFL = "https://raw.githubusercontent.com/google/fonts/main/ofl/inter/OFL.txt"


def fetch(url, headers=None):
    return urllib.request.urlopen(urllib.request.Request(url, headers=headers or {}), timeout=60).read()


def ttf(weight):
    # an old User-Agent makes Google Fonts serve one plain TTF instead of woff2 subsets
    css = fetch(f"https://fonts.googleapis.com/css2?family=Inter:wght@{weight}", {"User-Agent": "Mozilla/4.0"}).decode()
    urls = re.findall(r"url\((https://[^)]+)\)", css)
    assert len(urls) == 1, f"expected one TTF for weight {weight}, got {urls}"
    data = fetch(urls[0])
    assert data[:4] in (b"\x00\x01\x00\x00", b"true"), "not a TrueType file"
    return data


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for w in WEIGHTS:
        data = ttf(w)
        (OUT / f"Inter-{w}.ttf").write_bytes(data)
        print(f"Inter-{w}.ttf: {len(data) // 1024} KB")
    (OUT / "OFL-Inter.txt").write_bytes(fetch(OFL))


if __name__ == "__main__":
    main()
