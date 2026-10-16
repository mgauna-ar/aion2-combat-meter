#!/usr/bin/env python3
"""Draws src/app/resources/app.ico: three meter bars on a dark rounded square.

Every size is drawn at 8x and box-filtered down, so edges stay clean at tray size.

Usage: python3 tools/gen_app_icon.py [--out src/app/resources/app.ico]
"""
import argparse
import os

from PIL import Image, ImageDraw

SS = 8  # supersample factor
SIZES = [16, 20, 24, 32, 48, 64, 128, 256]

BG = (13, 16, 24, 255)
RIM = (70, 84, 110, 255)
# Bars, shortest first: the overlay's accent, DPS orange and live red.
BARS = [
    ((0.20, 0.58), (77, 148, 255, 255)),
    ((0.42, 0.40), (245, 140, 51, 255)),
    ((0.64, 0.22), (242, 77, 89, 255)),
]
BAR_WIDTH = 0.17
BAR_BOTTOM = 0.80


def draw(size):
    big = size * SS
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    radius = int(big * 0.22)
    rim = max(SS, int(big * 0.03))
    d.rounded_rectangle([0, 0, big - 1, big - 1], radius=radius, fill=RIM)
    d.rounded_rectangle([rim, rim, big - 1 - rim, big - 1 - rim], radius=max(1, radius - rim), fill=BG)
    for (x, top), color in BARS:
        x0 = int(big * x)
        x1 = int(big * (x + BAR_WIDTH))
        y0 = int(big * top)
        y1 = int(big * BAR_BOTTOM)
        d.rounded_rectangle([x0, y0, x1, y1], radius=max(1, int(big * 0.03)), fill=color)
    return img.resize((size, size), Image.Resampling.BOX)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=os.path.join("src", "app", "resources", "app.ico"))
    args = parser.parse_args()
    images = [draw(s) for s in SIZES]
    images[-1].save(args.out, format="ICO", sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
