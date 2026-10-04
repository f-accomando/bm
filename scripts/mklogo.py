#!/usr/bin/env python3
"""The bm logo of the boot splash (src/kernel/splash.c), cut from the user's
bm Suite sheet (art/brand/bm-suite.png, 2026-10-04): the big "bm" at the top
left, on its dark background, scaled to LOGO_H rows and written as RGB565 in
src/kernel/logo_data.c (committed: the builds need no Pillow).

    python3 scripts/mklogo.py
"""
import os

from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SHEET = os.path.join(ROOT, "art", "brand", "bm-suite.png")
OUT = os.path.join(ROOT, "src", "kernel", "logo_data.c")
BOX = (316, 72, 566, 222)           # the logo and a margin of its background
LOGO_H = 100


def main():
    logo = Image.open(SHEET).convert("RGB").crop(BOX)
    w = round(logo.width * LOGO_H / logo.height)
    logo = logo.resize((w, LOGO_H), Image.LANCZOS)
    px = logo.load()
    # the background: the darkest corner (the splash fills the screen with it)
    corners = [px[0, 0], px[w - 1, 0], px[0, LOGO_H - 1], px[w - 1, LOGO_H - 1]]
    bg = min(corners, key=sum)
    # as RGB565 gives it back (splash.c): the screen and the logo's
    # background the very same colour
    bg = ((bg[0] >> 3) << 3 | bg[0] >> 5, (bg[1] >> 2) << 2 | bg[1] >> 6, (bg[2] >> 3) << 3 | bg[2] >> 5)
    # the sheet's background is not even (a little lighter in places): what
    # is near it becomes exactly the splash's colour, the logo's edges blend
    words = []
    for y in range(LOGO_H):
        for x in range(w):
            p = px[x, y]
            d = max(abs(p[i] - bg[i]) for i in range(3))
            a = min(max((d - 14) / 30, 0.0), 1.0)
            r, g, b = (round(bg[i] + (p[i] - bg[i]) * a) for i in range(3))
            words.append((r >> 3) << 11 | (g >> 2) << 5 | b >> 3)
    with open(OUT, "w") as f:
        f.write("/* The bm logo of the boot splash: scripts/mklogo.py from "
                "art/brand/bm-suite.png. Do not edit. */\n")
        f.write('#include "splash.h"\n\n')
        f.write(f"const int bm_logo_w = {w}, bm_logo_h = {LOGO_H};\n")
        f.write(f"const uint32_t bm_logo_bg = 0x{bg[0]:02X}{bg[1]:02X}{bg[2]:02X};\n")
        f.write(f"const uint16_t bm_logo[{w * LOGO_H}] = {{\n")
        for i in range(0, len(words), 12):
            f.write("    " + ", ".join(f"0x{v:04X}" for v in words[i:i + 12]) + ",\n")
        f.write("};\n")
    print(f"logo: {os.path.relpath(OUT, ROOT)} ({w}x{LOGO_H}, background #{bg[0]:02X}{bg[1]:02X}{bg[2]:02X})")


if __name__ == "__main__":
    main()
