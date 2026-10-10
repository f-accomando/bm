#!/usr/bin/env python3
"""Overbit: the cover (carts/overbit/cover.png), 88x88 as the menu's square
covers, filmed on the console's own 3D: the title screen's hero alone and
close (OVERBIT_COVER in 80_modes.lua), drawn by bmhost at 360x360 (the
RGB30's screen, --square) in its victory pose, then made small with the
title in the console's font, as the other covers have it.

    make build/host/bmhost-bin build/overbit/models.bm build/overbit/sounds.json
    python3 carts/overbit/tools/mkcover.py [--build build] [--hero kaiju]

(Linux or WSL: bmhost; needs Pillow.) Then commit cover.png: the Makefile
puts it in overbit.bm and overbit.b16.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
CART = os.path.join(HERE, "..")
ROOT = os.path.join(CART, "..", "..")
W = H = 88
SHOT_FRAME = 470        # the title's hero: idle, then its victory pose from 6 s (Menu.vt % 9 > 6)


def load_font():
    src = open(os.path.join(ROOT, "src", "gfx", "font8x16.c")).read()
    rows = re.findall(r"\{ (0x[0-9a-f]{2}(?:, 0x[0-9a-f]{2}){15}) \}", src)
    return [bytes(int(b, 16) for b in r.split(", ")) for r in rows]


def text(img, s, x, y, col, outline):
    """the console's 8x16 font, with a one-pixel outline all round"""
    font = load_font()
    px = img.load()

    def glyphs(dx, dy, c):
        for i, ch in enumerate(s):
            g = font[ord(ch)]
            for r in range(16):
                for b in range(8):
                    if g[r] & (0x80 >> b):
                        xx, yy = x + i * 8 + b + dx, y + r + dy
                        if 0 <= xx < W and 0 <= yy < H:
                            px[xx, yy] = c

    for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1), (0, 2)):
        glyphs(dx, dy, outline)
    glyphs(0, 0, col)


def film(build, hero, tmp, yaw=None):
    """the title screen in cover mode (yaw: the camera's, radians), one frame as a PNG"""
    lua = os.path.join(tmp, "cover.lua")
    bm = os.path.join(tmp, "cover.bm")
    subprocess.run([sys.executable, os.path.join(CART, "build.py"), lua, "--hero", hero,
                    "--extra", os.path.join(build, "overbit", "21_map.lua"),
                    "--define", f"OVERBIT_COVER={'true' if yaw is None else yaw}",
                    "--define", 'OVERBIT_RES="360x360"'], check=True)
    subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "mkbm.py"), "-o", bm, "--lua", lua,
                    "--title", "Overbit cover", "--author", "bm", "--res", "360x360",
                    "--models", os.path.join(build, "overbit", "models.bm"),
                    "--audio", os.path.join(build, "overbit", "sounds.json")], check=True, stdout=subprocess.DEVNULL)
    script = os.path.join(tmp, "input.txt")
    with open(script, "w") as f:
        f.write(f"{SHOT_FRAME} shot cover\n")
    subprocess.run([os.path.join(build, "host", "bmhost-bin"), bm, "--seconds", str(SHOT_FRAME / 60 + 0.2),
                    "--square", "--quiet", "--shots", tmp, "--input", script], check=True)
    return Image.open(os.path.join(tmp, "cover.png")).convert("RGBA")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=os.path.join(ROOT, "build"))
    ap.add_argument("--hero", default="kaiju", help="the hero on the cover (kaiju: the red mech)")
    ap.add_argument("--yaw", type=float, help="the camera's yaw (radians; 80_modes.lua has the default)")
    a = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        shot = film(a.build, a.hero, tmp, a.yaw)
    # the hero fills the middle of the frame: a square from the top, the title over the sky
    s = shot.width
    box = (s * 5 // 100, 0, s * 95 // 100, s * 90 // 100)
    img = shot.crop(box).resize((W, H), Image.LANCZOS)
    # a darker edge, so that the title stands out
    vig = Image.new("L", (W, H), 0)
    ImageDraw.Draw(vig).ellipse((-26, -18, W + 26, H + 30), fill=255)
    vig = vig.filter(ImageFilter.GaussianBlur(9))
    dark = Image.new("RGBA", (W, H), (6, 8, 14, 255))
    img = Image.composite(img, dark, vig).convert("RGBA")
    text(img, "OVERBIT", (W - 7 * 8) // 2, 3, (255, 224, 112, 255), (16, 20, 24, 255))
    out = os.path.join(CART, "cover.png")
    img.convert("RGB").save(out, optimize=True)
    print(os.path.normpath(out))


if __name__ == "__main__":
    main()
