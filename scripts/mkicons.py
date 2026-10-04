#!/usr/bin/env python3
"""The bm Suite's app icons (art/brand/bm-suite.png, the user's sheet of
2026-10-04) cut out as the covers of the editors in the menu:
carts/<app>/icon.png, 128x128, square. mkbm.py --cover makes them the
88x88 cover of the cartridge.

    python3 scripts/mkicons.py          # all of them

The icons on the sheet are rounded squares on a dark background: each is
cut at its box, scaled to a square, and the dark corners outside its round
edge take the colour of the edge (the menu rounds the card itself).
"""
import os
import sys

from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SHEET = os.path.join(ROOT, "art", "brand", "bm-suite.png")
SIZE = 128
RADIUS = 26                         # the icons' round corners, at SIZE

# the icons on the sheet: left edge (135 wide), top 322, 143 high
ICONS = {
    "studio": 70, "code": 283, "animator": 494, "editor": 706,      # editor: bm SDK
    "sound": 918, "mesh": 1130, "pixel": 1339,
}
TOP, W, H = 322, 135, 143


def inside(x, y):
    """the nearest point of the rounded square to (x, y)"""
    r = RADIUS
    cx = min(max(x, r), SIZE - 1 - r)
    cy = min(max(y, r), SIZE - 1 - r)
    dx, dy = x - cx, y - cy
    d = (dx * dx + dy * dy) ** 0.5
    if d <= r - 1.5:
        return x, y
    k = (r - 1.5) / d
    return int(round(cx + dx * k)), int(round(cy + dy * k))


def main(names):
    sheet = Image.open(SHEET).convert("RGB")
    for name, x0 in ICONS.items():
        if names and name not in names:
            continue
        icon = sheet.crop((x0, TOP, x0 + W, TOP + H)).resize((SIZE, SIZE), Image.LANCZOS)
        px = icon.load()
        out = icon.copy()
        po = out.load()
        for y in range(SIZE):
            for x in range(SIZE):
                ix, iy = inside(x, y)
                if (ix, iy) != (x, y):
                    po[x, y] = px[ix, iy]
        path = os.path.join(ROOT, "carts", name, "icon.png")
        out.save(path)
        print("icon:", os.path.relpath(path, ROOT))


if __name__ == "__main__":
    main(sys.argv[1:])
