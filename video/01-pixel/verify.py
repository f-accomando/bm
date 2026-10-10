#!/usr/bin/env python3
"""Checks the cartridge bm Pixel saved in the recording (out/sd/carts/SKYVALE.BM)
against the art of art.py: every frame of the run, the jump and the coin, pixel
by pixel (transparent or the right colour of the palette, RGB565 rounding).
No pictures needed: the file is what the editor really wrote.

  python3 video/01-pixel/verify.py [SD_DIR]
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
sys.path.insert(0, HERE)
import bmres  # noqa: E402
import art    # noqa: E402

sd = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "out", "sd")
path = os.path.join(sd, "carts", "SKYVALE.BM")
if not os.path.exists(path):
    print("no", path, "- files:", os.listdir(os.path.join(sd, "carts")) if os.path.isdir(os.path.join(sd, "carts")) else "none")
    sys.exit(1)
w, h, rgba = bmres.sheet_get(bmres.read(path))


def px(x, y):
    i = 4 * (y * w + x)
    return None if rgba[i + 3] < 128 else (rgba[i], rgba[i + 1], rgba[i + 2])


sprites = art.FRAMES
bad = 0
for n, rows in enumerate(sprites):
    x0 = 16 * n
    wrong = []
    for y in range(16):
        for x in range(16):
            want, got = rows[y][x], px(x0 + x, y)
            if want == ".":
                ok = got is None
            else:
                rgb = art.COLOURS[want][2]
                r, g, b = rgb >> 16, rgb >> 8 & 255, rgb & 255
                ok = got is not None and all(abs(a - c) <= 8 for a, c in zip(got, (r, g, b)))
            if not ok:
                wrong.append((x, y, want, got))
    name = "run %d" % n if n < len(art.RUN) else "jump"
    print("%-5s sprite %2d: %s" % (name, 2 * n, "ok" if not wrong else "%d wrong, first %s" % (len(wrong), wrong[:4])))
    bad += len(wrong)
# the coin, sprite 14: drawn with the shape tools, so checked by what it holds
cx = 16 * len(sprites)
coin = [px(cx + x, y) for y in range(16) for x in range(16)]
solid = [c for c in coin if c]
cols = {c for c in solid}
print("coin  sprite %2d: %d solid pixels, %d colours" % (2 * len(sprites), len(solid), len(cols)))
if len(solid) < 80 or len(cols) < 4:
    bad += 1
extra = [(x, y) for y in range(h) for x in range(w) if x >= cx + 16 and px(x, y)]
print("pixels outside the sprites:", len(extra))
print("sheet %dx%d, files in /carts: %s" % (w, h, os.listdir(os.path.join(sd, "carts"))))
sys.exit(1 if bad or extra else 0)
