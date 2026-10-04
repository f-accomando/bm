#!/usr/bin/env python3
"""The bm logo of the boot splash (src/kernel/splash.c), cut from the user's
bm Suite sheet (art/brand/bm-suite.png, 2026-10-04): the big "bm" at the top
left, on its dark background, scaled to LOGO_H rows and written as RGB565 in
src/kernel/logo_data.c; and the same "bm" as pixel art, SMALL_W x SMALL_H in a
few colours with the background see-through, for the games' loading screen
(src/bm/loading.c) in src/bm/loading_logo.c. Both committed: the builds need
no Pillow.

    python3 scripts/mklogo.py
"""
import os

from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SHEET = os.path.join(ROOT, "art", "brand", "bm-suite.png")
OUT = os.path.join(ROOT, "src", "kernel", "logo_data.c")
OUT_SMALL = os.path.join(ROOT, "src", "bm", "loading_logo.c")
SMALL_W, SMALL_H = 44, 26
KEY = 0xF81F                        # see-through in the small one
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
    small()


def rgb565(c):
    v = (c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3
    return v if v != KEY else KEY ^ 1


def band_at(x):
    c0, c1 = (30, 150, 255), (165, 95, 255)
    t = min(int(x * 5 / SMALL_W), 4) / 4
    return tuple(round(c0[i] + (c1[i] - c0[i]) * t) for i in range(3))


def leg(px):
    """the m's left leg: in the brand art it goes behind the b's belly and
    stops short; here it goes down as far as the other two (the user's
    request, 2026-10-04): the middle leg's shape one leg to the left, only
    where nothing is drawn (the b stays over the m), in the shaded colour of
    the part of it already seen"""
    at = lambda x, y: px[y * SMALL_W + x]
    y = SMALL_H - 6                             # a row where the legs stand apart
    runs, x = [], 0
    while x < SMALL_W:
        if at(x, y) is not None:
            x0 = x
            while x < SMALL_W and at(x, y) is not None:
                x += 1
            runs.append((x0, x))
        x += 1
    if len(runs) < 3:
        return
    (m0, m1), (r0, _) = runs[-2], runs[-1]      # the middle leg, the right one
    step = r0 - m0
    for yy in range(SMALL_H):
        # the rows where the middle leg stands alone, background on both sides
        if not (at(m0 - 1, yy) is None and (m1 >= SMALL_W or at(m1, yy) is None)):
            continue
        for xx in range(m0, m1):
            lx = xx - step
            if at(xx, yy) is None or lx < 0 or at(lx, yy) is not None:
                continue
            px[yy * SMALL_W + lx] = tuple(round(v * 0.62) for v in band_at(lx))


def small():
    """the "bm" as pixel art: how much of each pixel is logo (its distance
    from the background), scaled down, cut at half, then a few colours"""
    src = Image.open(SHEET).convert("RGB").crop(BOX)
    px = src.load()
    bg = min([px[0, 0], px[src.width - 1, 0], px[0, src.height - 1], px[src.width - 1, src.height - 1]], key=sum)
    alpha = Image.new("L", src.size)
    pa = alpha.load()
    for y in range(src.height):
        for x in range(src.width):
            d = max(abs(px[x, y][i] - bg[i]) for i in range(3))
            pa[x, y] = int(min(max((d - 14) / 30, 0.0), 1.0) * 255)
    # the box of the logo itself
    bbox = alpha.getbbox()
    src, alpha = src.crop(bbox), alpha.crop(bbox)
    # pixel art: five bands of colour from the logo's blue to its purple,
    # darker where the letters are shaded (the "b" over the "m"), a light
    # edge on top; the background see-through
    col = src.resize((SMALL_W, SMALL_H), Image.LANCZOS).load()
    a = alpha.resize((SMALL_W, SMALL_H), Image.LANCZOS).load()
    c0, c1 = (30, 150, 255), (165, 95, 255)

    def lum(c):
        return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]

    def solid(x, y):
        return 0 <= x < SMALL_W and 0 <= y < SMALL_H and a[x, y] >= 128

    words = []
    for y in range(SMALL_H):
        for x in range(SMALL_W):
            if not solid(x, y):
                words.append(None)
                continue
            t = min(int(x * 5 / SMALL_W), 4) / 4
            band = tuple(round(c0[i] + (c1[i] - c0[i]) * t) for i in range(3))
            if lum(col[x, y]) < 0.72 * lum(band):
                band = tuple(round(v * 0.62) for v in band)
            elif not solid(x, y - 1):
                band = tuple(min(255, round(v + (255 - v) * 0.35)) for v in band)
            words.append(band)
    leg(words)
    words = [KEY if c is None else rgb565(c) for c in words]
    with open(OUT_SMALL, "w") as f:
        f.write("/* The bm logo as pixel art, for the games' loading screen: scripts/mklogo.py\n"
                " * from art/brand/bm-suite.png. Do not edit. */\n")
        f.write('#include "loading.h"\n\n')
        f.write(f"const int loading_logo_w = {SMALL_W}, loading_logo_h = {SMALL_H};\n")
        f.write(f"const uint16_t loading_logo[{SMALL_W * SMALL_H}] = {{\n")
        for i in range(0, len(words), 11):
            f.write("    " + ", ".join(f"0x{v:04X}" for v in words[i:i + 11]) + ",\n")
        f.write("};\n")
    print(f"logo: {os.path.relpath(OUT_SMALL, ROOT)} ({SMALL_W}x{SMALL_H}, pixel art)")


if __name__ == "__main__":
    main()
