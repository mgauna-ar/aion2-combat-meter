#!/usr/bin/env python3
"""Draws src/app/resources/app.ico: a crystal between a pair of wings, on a dark squircle.

AION's Daevas fly on wings, and the Tower of Eternity stands at the middle of its
world: the mark nods at both and copies none of the game's art (AGENTS.md,
Third-party code). Each wing is five feathers that rise and then sweep out at the
tip, shaded from the root outwards and set off from the one below by a hairline of
the plate's colour. Sizes of 48 px and up also get a glow behind the mark and a light
rim on the plate. Tray sizes (24 px and down) draw three broader feathers a wing, with
no hairlines and no glow, which turn to mush there.

Every size is drawn at 8x and box-filtered down, so edges stay clean.

It also writes the mark at MARK_PX, raw DEFLATE, into src/app/src/app_mark.inc: the
app window's sidebar shows it, painted into the font atlas as the game icons are
(app/game_icons.hpp), but apart from their sheet, since that one is NCSOFT's art and
this is ours.

Usage: python3 tools/gen_app_icon.py [--out src/app/resources/app.ico]
           [--mark src/app/src/app_mark.inc] [--wings gold] [--png DIR]
"""
import argparse
import math
import os
import zlib

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageOps

SS = 8  # supersample factor
SIZES = [16, 20, 24, 32, 48, 64, 128, 256]
# The sidebar's mark is 36 px at 100% scale; this covers 250% without blowing it up.
MARK_PX = 96

PLATE_TOP = (28, 36, 64)
PLATE_BOTTOM = (9, 12, 20)
PLATE_GLOW = (59, 130, 246)  # theme Accent
CRYSTAL_GLOW = (125, 211, 252)
# The crystal's two facets, each a vertical ramp: the lit left one and the shaded right.
CRYSTAL_LEFT = ((255, 255, 255), (165, 243, 252))
CRYSTAL_RIGHT = ((186, 230, 253), (37, 99, 235))
# Each wing's ramp, root to tip.
WINGS = {
    "gold": ((161, 72, 8), (245, 158, 11), (254, 243, 199)),
    "blue": ((67, 56, 202), (59, 130, 246), (191, 219, 254)),
    "violet": ((59, 7, 100), (147, 51, 234), (233, 213, 255)),
}

# The right wing in unit coordinates, the left its mirror: each feather is a curve
# (root, control, tip) and its widest width. Bottom first, so each one drawn overlaps
# the one below it at the root.
FEATHERS = [
    ((0.56, 0.68), (0.66, 0.74), (0.78, 0.80), 0.09),
    ((0.56, 0.65), (0.72, 0.66), (0.88, 0.71), 0.11),
    ((0.56, 0.61), (0.76, 0.55), (0.94, 0.56), 0.12),
    ((0.56, 0.57), (0.73, 0.39), (0.95, 0.36), 0.13),
    ((0.55, 0.53), (0.64, 0.22), (0.90, 0.15), 0.14),
]
FEATHERS_TRAY = [
    ((0.56, 0.66), (0.72, 0.68), (0.88, 0.75), 0.16),
    ((0.56, 0.60), (0.76, 0.50), (0.96, 0.46), 0.17),
    ((0.55, 0.53), (0.64, 0.22), (0.92, 0.15), 0.18),
]
CRYSTAL_TOP, CRYSTAL_WAIST, CRYSTAL_BOTTOM = 0.10, 0.56, 0.92


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def vertical(w, top, bottom):
    strip = Image.new("RGB", (1, 256))
    px = strip.load()
    for y in range(256):
        px[0, y] = lerp(top, bottom, y / 255)
    return strip.resize((w, w), Image.Resampling.BILINEAR)


def radial(w, centre, radius, ramp):
    """A three-stop ramp outwards from centre (unit coordinates), its last colour beyond radius."""
    disc = round(radius * w * 2)
    grad = Image.radial_gradient("L").resize((disc, disc), Image.Resampling.BILINEAR)
    full = Image.new("L", (w, w), 255)
    full.paste(grad, (round(centre[0] * w - disc / 2), round(centre[1] * w - disc / 2)))
    inner, mid, outer = ramp
    return ImageOps.colorize(full, inner, outer, mid)


def squircle(w, margin, n=5.0, steps=720):
    """A superellipse's outline: the rounded square of current app icons."""
    r = (1 - 2 * margin) * w / 2
    c = w / 2
    pts = []
    for i in range(steps):
        th = 2 * math.pi * i / steps
        ct, st = math.cos(th), math.sin(th)
        pts.append((c + math.copysign(abs(ct) ** (2 / n), ct) * r, c + math.copysign(abs(st) ** (2 / n), st) * r))
    return pts


def feather(root, ctrl, tip, width, steps=64):
    """A feather's outline along a quadratic curve: broad near the root, to a point at the tip."""
    left, right = [], []
    for i in range(steps + 1):
        t = i / steps
        a, b, c = (1 - t) ** 2, 2 * (1 - t) * t, t * t
        x = a * root[0] + b * ctrl[0] + c * tip[0]
        y = a * root[1] + b * ctrl[1] + c * tip[1]
        dx = 2 * (1 - t) * (ctrl[0] - root[0]) + 2 * t * (tip[0] - ctrl[0])
        dy = 2 * (1 - t) * (ctrl[1] - root[1]) + 2 * t * (tip[1] - ctrl[1])
        length = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / length, dx / length
        half = width / 2 * (1 - t ** 1.6) ** 0.9 * (0.6 + 0.4 * min(1.0, t * 4))
        left.append((x + nx * half, y + ny * half))
        right.append((x - nx * half, y - ny * half))
    return left + right[::-1]


def mask(w, paint):
    m = Image.new("L", (w, w), 0)
    paint(ImageDraw.Draw(m))
    return m


def solid(w, colour):
    return Image.new("RGB", (w, w), colour)


def render(size, wing_ramp):
    tray = size <= 24
    w = size * SS
    img = Image.new("RGB", (w, w), PLATE_BOTTOM)
    # The mark fills more of a small icon.
    k = 0.96 if tray else 0.88

    def at(p):
        return (0.5 + (p[0] - 0.5) * k, 0.5 + (p[1] - 0.5) * k)

    def px(points):
        return [(x * w, y * w) for x, y in points]

    # The plate: full bleed up to 32 px, where every pixel counts.
    outline = squircle(w, 0.0 if size <= 32 else 0.035)
    shape = mask(w, lambda d: d.polygon(outline, fill=255))
    img.paste(vertical(w, PLATE_TOP, PLATE_BOTTOM), (0, 0), shape)
    if size >= 48:
        r = 0.32 * w
        glow = mask(w, lambda d: d.ellipse([0.5 * w - r, 0.5 * w - r, 0.5 * w + r, 0.5 * w + r], fill=120))
        glow = glow.filter(ImageFilter.GaussianBlur(w * 0.12))
        img.paste(solid(w, PLATE_GLOW), (0, 0), ImageChops.multiply(glow, shape))
        # A hairline that catches the light along the top and fades out downwards.
        edge = mask(w, lambda d: d.line(outline + [outline[0]], fill=255, width=max(2, round(w * 0.010))))
        fade = vertical(w, (90, 90, 90), (8, 8, 8)).convert("L")
        img.paste(solid(w, (255, 255, 255)), (0, 0), ImageChops.multiply(ImageChops.multiply(edge, fade), shape))

    gap = 0 if tray else max(3, round(w * 0.007))

    def set_off(points):
        """Clears a hairline of plate around a shape, so it reads apart from what is under it."""
        if gap:
            ring = mask(w, lambda d: (d.polygon(points, fill=255),
                                      d.line(points + [points[0]], fill=255, width=gap * 2, joint="curve")))
            img.paste(solid(w, PLATE_BOTTOM), (0, 0), ImageChops.multiply(ring, shape))

    # The wings.
    paint = radial(w, at((0.53, 0.60)), 0.42 * k, wing_ramp)
    mirrored = paint.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    for side, colours in ((1, paint), (-1, mirrored)):
        for root, ctrl, tip, width in FEATHERS_TRAY if tray else FEATHERS:
            outline_f = feather(at(root), at(ctrl), at(tip), width * k)
            points = px([(0.5 + (x - 0.5) * side, y) for x, y in outline_f])
            set_off(points)
            img.paste(colours, (0, 0), mask(w, lambda d: d.polygon(points, fill=255)))

    # The crystal, over the wings' roots.
    half = 0.11 if tray else 0.085
    top, right, bottom, left = px([at((0.5, CRYSTAL_TOP)), at((0.5 + half, CRYSTAL_WAIST)),
                                   at((0.5, CRYSTAL_BOTTOM)), at((0.5 - half, CRYSTAL_WAIST))])
    crystal = [top, right, bottom, left]
    if not tray:
        glow = mask(w, lambda d: d.polygon(crystal, fill=170)).filter(ImageFilter.GaussianBlur(w * 0.05))
        img.paste(solid(w, CRYSTAL_GLOW), (0, 0), ImageChops.multiply(glow, shape))
    set_off(crystal)
    img.paste(vertical(w, *CRYSTAL_LEFT), (0, 0), mask(w, lambda d: d.polygon([top, bottom, left], fill=255)))
    img.paste(vertical(w, *CRYSTAL_RIGHT), (0, 0), mask(w, lambda d: d.polygon([top, right, bottom], fill=255)))

    out = img.convert("RGBA")
    out.putalpha(shape)
    return out.resize((size, size), Image.Resampling.BOX)


def write_mark(path, image):
    """The mark's RGBA pixels (straight alpha), raw DEFLATE, as archive::inflate reads."""
    raw = image.tobytes()
    packer = zlib.compressobj(9, zlib.DEFLATED, -15)
    packed = packer.compress(raw) + packer.flush()
    lines = [
        "// Generated by tools/gen_app_icon.py: the app's mark, as app.ico draws it. Do not",
        "// edit: run the script. This project's own drawing, under its licence.",
        f"constexpr int kAppMarkPx = {MARK_PX};",
        f"constexpr size_t kAppMarkSize = {len(raw)};",
        "const uint8_t kAppMarkDeflated[] = {",
    ]
    for i in range(0, len(packed), 32):
        lines.append(",".join(str(b) for b in packed[i:i + 32]) + ",")
    lines.append("};")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {path}: {len(raw)} bytes, {len(packed)} deflated")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=os.path.join("src", "app", "resources", "app.ico"))
    parser.add_argument("--mark", default=os.path.join("src", "app", "src", "app_mark.inc"))
    parser.add_argument("--wings", choices=sorted(WINGS), default="gold")
    parser.add_argument("--png", help="also write each size as icon-<size>.png into this folder, to look at")
    args = parser.parse_args()
    images = [render(s, WINGS[args.wings]) for s in SIZES]
    images[-1].save(args.out, format="ICO", sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    print(f"wrote {args.out}")
    write_mark(args.mark, render(MARK_PX, WINGS[args.wings]))
    if args.png:
        os.makedirs(args.png, exist_ok=True)
        for s, im in zip(SIZES, images):
            im.save(os.path.join(args.png, f"icon-{s}.png"))
        print(f"wrote {len(SIZES)} PNGs to {args.png}")


if __name__ == "__main__":
    main()
