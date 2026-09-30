#!/usr/bin/env python3
"""Texture Room (M14): draws the textures (sheet.png, 128x128: 4x4 textures
of 32x32) and the cover (cover.png, 128x80) in code, reproducibly.

    python3 carts/texroom/mkassets.py

Textures, numbered by rows (the numbers main.lua uses):
  0 stone floor   1 brick wall     2 wooden crate   3 metal panel
  4 moss floor    5 checker        6 dark planks    7 bm33 tile
"""
import math
import os
import random
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SW = SH = 128
T = 32
sheet = [(0, 0, 0, 255)] * (SW * SH)
rnd = random.Random(33)


def rgb(c):
    return (c >> 16 & 255, c >> 8 & 255, c & 255, 255)


def shade(c, k):
    r, g, b = c >> 16 & 255, c >> 8 & 255, c & 255
    f = lambda v: max(0, min(255, int(v * k)))
    return f(r) << 16 | f(g) << 8 | f(b)


def tex(n, fn):
    """fills texture n with fn(x, y) -> 0xRRGGBB"""
    ox, oy = (n % 4) * T, (n // 4) * T
    for y in range(T):
        for x in range(T):
            sheet[(oy + y) * SW + ox + x] = rgb(fn(x, y))


def noise(k=0.12):
    return 1 + (rnd.random() - 0.5) * 2 * k


# 0: stone floor, 4 slabs with dark joints
def stone(x, y):
    if x % 16 == 0 or y % 16 == 0:
        return 0x3A3A40
    base = [0x8A8A90, 0x7E7F88, 0x92908A, 0x84868C][(x // 16) + 2 * (y // 16)]
    return shade(base, noise(0.10))


# 1: brick wall, offset rows of 16x8 bricks
def brick(x, y):
    row = y // 8
    xs = (x + (8 if row % 2 else 0)) % 16
    if y % 8 == 7 or xs == 15:
        return 0xB8B0A0
    b = [0xA0402C, 0x943A28, 0xA8482E, 0x8C3624][(row + (x + (8 if row % 2 else 0)) // 16) % 4]
    return shade(b, noise(0.12))


# 2: wooden crate: frame, diagonal brace, planks
def crate(x, y):
    if x < 3 or y < 3 or x > 28 or y > 28:
        return shade(0x6A4424, noise(0.08))
    if abs(x - y) < 3:
        return shade(0x7A5230, noise(0.08))
    plank = 0xB07C44 if (y // 6) % 2 else 0xA07038
    if y % 6 == 0:
        return 0x5A3A1E
    return shade(plank, noise(0.06))


# 3: metal panel with rivets
def metal(x, y):
    d = min(x, y, 31 - x, 31 - y)
    if d == 0:
        return 0x40484F
    if (x in (3, 28)) and (y in (3, 28)):
        return 0xE0E8F0
    k = 0.85 + 0.3 * (y / 31)
    return shade(0x8898A4, k * noise(0.03))


# 4: moss floor
def moss(x, y):
    return shade(0x4E7A34 if rnd.random() > 0.2 else 0x3C5E28, noise(0.15))


# 5: checker
def checker(x, y):
    return 0xE8E8E8 if ((x // 8) + (y // 8)) % 2 else 0x303038


# 6: dark planks
def planks(x, y):
    if x % 8 == 0:
        return 0x2A1A10
    return shade(0x5C3C24, noise(0.1) * (0.9 + 0.1 * math.sin(y * 0.7 + x)))


# 7: a bm33 tile: blue with a light border and "33"
DIGITS = ["111", "001", "011", "001", "111"]


def logo(x, y):
    if min(x, y, 31 - x, 31 - y) < 2:
        return 0xA0C8FF
    for i, ox in enumerate((8, 18)):
        cx, cy = (x - ox) // 2, (y - 11) // 2
        if 0 <= cx < 3 and 0 <= cy < 5 and DIGITS[cy][cx] == "1":
            return 0xFFFFFF
    return shade(0x2050B0, 0.9 + 0.2 * (y / 31))


for n, fn in enumerate([stone, brick, crate, metal, moss, checker, planks, logo]):
    tex(n, fn)


def write_png(path, w, h, px):
    raw = b"".join(b"\0" + bytes(v for p in px[y * w:(y + 1) * w] for v in p) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


write_png(os.path.join(HERE, "sheet.png"), SW, SH, sheet)

# cover: a floor in perspective with a crate, from the textures
CW, CH = 128, 80
cover = [(0, 0, 0, 255)] * (CW * CH)
for y in range(CH):
    for x in range(CW):
        if y < 34:                                  # brick wall, far away
            c = sheet[(y % T + T * 0) * SW + (x % T) + T][:3]
            c = tuple(int(v * 0.6) for v in c)
        else:                                       # floor in perspective
            d = (y - 30) / 50
            u = int((x - 64) / d / 4) % T
            v = int(1 / d * 16) % T
            c = tuple(int(v2 * min(1, d * 1.6)) for v2 in sheet[v * SW + u][:3])
        cover[y * CW + x] = c + (255,)
for y in range(22, 62):                             # the crate
    for x in range(44, 84):
        tx, ty = (x - 44) * T // 40, (y - 22) * T // 40
        cover[y * CW + x] = sheet[(ty) * SW + 2 * T + tx]
write_png(os.path.join(HERE, "cover.png"), CW, CH, cover)
print("sheet.png, cover.png")
