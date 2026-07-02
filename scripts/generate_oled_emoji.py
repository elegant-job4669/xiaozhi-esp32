#!/usr/bin/env python3
"""Generate 1-bit friendly OLED emoji assets from Twemoji PNGs.

Strips the yellow face circle and keeps only high-contrast black features on
a white background, then converts to LVGL L8 C arrays for monochrome OLEDs.
"""

from __future__ import annotations

import argparse
import subprocess
import urllib.request
from pathlib import Path

from PIL import Image

EMOTIONS = {
    "neutral": "1f636",
    "happy": "1f642",
    "laughing": "1f606",
    "funny": "1f602",
    "sad": "1f614",
    "angry": "1f620",
    "crying": "1f62d",
    "loving": "1f60d",
    "embarrassed": "1f633",
    "surprised": "1f62f",
    "shocked": "1f631",
    "thinking": "1f914",
    "winking": "1f609",
    "cool": "1f60e",
    "relaxed": "1f60c",
    "delicious": "1f924",
    "kissy": "1f618",
    "confident": "1f60f",
    "sleepy": "1f634",
    "silly": "1f61c",
    "confused": "1f644",
}

TWEMOJI_URL = "https://cdn.jsdelivr.net/gh/twitter/twemoji@14.0.2/assets/72x72/{}.png"


def is_background(r: int, g: int, b: int, a: int) -> bool:
    if a < 40:
        return True
    if r > 160 and g > 120 and b < 130 and (r - b) > 50:
        return True
    if r > 200 and g > 180 and b < 160:
        return True
    return False


def to_mono(r: int, g: int, b: int, a: int) -> int:
    if is_background(r, g, b, a):
        return 255
    lum = int(0.299 * r + 0.587 * g + 0.114 * b)
    return 0 if lum < 150 else 255


def generate_pngs(png_dir: Path, size: int) -> None:
    png_dir.mkdir(parents=True, exist_ok=True)
    for name, code in EMOTIONS.items():
        data = urllib.request.urlopen(TWEMOJI_URL.format(code)).read()
        src = Image.open(__import__("io").BytesIO(data)).convert("RGBA")
        src = src.resize((size, size), Image.Resampling.LANCZOS)
        out = Image.new("L", (size, size), 255)
        spx = src.load()
        opx = out.load()
        for y in range(size):
            for x in range(size):
                opx[x, y] = to_mono(*spx[x, y])
        out.save(png_dir / f"{name}.png")
        print(f"png: {name}")


def generate_c_arrays(png_dir: Path, c_dir: Path, converter: Path) -> None:
    c_dir.mkdir(parents=True, exist_ok=True)
    for png in sorted(png_dir.glob("*.png")):
        subprocess.check_call([
            "python3",
            str(converter),
            "--ofmt",
            "C",
            "--cf",
            "L8",
            "--background",
            "0xffffff",
            "-o",
            str(c_dir),
            str(png),
        ])
        print(f"c: {png.stem}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "main" / "display" / "oled_emoji",
    )
    parser.add_argument("--size", type=int, default=64)
    args = parser.parse_args()

    png_dir = args.root / "png"
    c_dir = args.root / "c"
    converter = Path(__file__).resolve().parents[1] / "scripts" / "Image_Converter" / "LVGLImage.py"

    generate_pngs(png_dir, args.size)
    generate_c_arrays(png_dir, c_dir, converter)


if __name__ == "__main__":
    main()
