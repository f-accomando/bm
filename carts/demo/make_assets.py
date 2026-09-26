#!/usr/bin/env python3
"""Generates sheet.png (128x128, 8x8 cells) and map.csv (160x90) for the demo cart."""
import math
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
W = H = 128
px = [[(0, 0, 0, 0)] * W for _ in range(H)]


def rnd(seed):
    x = seed & 0xFFFFFFFF
    while True:
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        yield x


R = rnd(12345)


def cell(n, fn):
    """Fills 8x8 cell n with fn(x, y) -> (r, g, b, a)."""
    cx, cy = n % 16 * 8, n // 16 * 8
    for y in range(8):
        for x in range(8):
            px[cy + y][cx + x] = fn(x, y)


def big(n, fn):
    """16x16 sprite whose top-left cell is n (uses n, n+1, n+16, n+17)."""
    cx, cy = n % 16 * 8, n // 16 * 8
    for y in range(16):
        for x in range(16):
            px[cy + y][cx + x] = fn(x, y)


def noise(base, amp):
    def f(x, y):
        v = next(R) % (2 * amp + 1) - amp
        return tuple(max(0, min(255, c + v)) for c in base) + (255,)
    return f


# map tiles (opaque)
cell(1, noise((60, 150, 60), 18))                                           # grass
cell(2, lambda x, y: (30, 80 + 30 * ((x + 2 * y) % 4 == 0), 180, 255))      # water
cell(3, lambda x, y: (150, 60, 40, 255) if (y % 4 and (x + (y // 4) * 4) % 8) else (200, 190, 170, 255))  # brick
cell(4, noise((210, 190, 120), 12))                                         # sand
cell(5, noise((110, 110, 120), 20))                                         # stone
cell(6, lambda x, y: (240, 80, 150, 255) if (x - 3.5) ** 2 + (y - 3.5) ** 2 < 5 else (60, 150, 60, 255))  # flower
cell(7, lambda x, y: (40, 110, 40, 255) if (x + y) % 3 else (30, 90, 30, 255))  # dark grass

# 16x16 sprites (transparent background) at cells 32, 34, 36, 38


def ball(x, y):
    dx, dy = x - 7.5, y - 7.5
    d = math.hypot(dx, dy)
    if d > 7.5:
        return (0, 0, 0, 0)
    light = max(0.0, 1 - math.hypot(dx + 3, dy + 3) / 11)
    return (int(80 + 175 * light), int(40 + 60 * light), int(200), 255)


def star(x, y):
    dx, dy = x - 7.5, y - 7.5
    a = math.atan2(dy, dx)
    r = 3.5 + 4 * abs(math.cos(2.5 * a))
    return (255, 220, 60, 255) if math.hypot(dx, dy) <= r else (0, 0, 0, 0)


def ship(x, y):     # points right: asymmetric, shows horizontal flips
    if abs(y - 7.5) <= x * 0.5 and x < 15:
        return (230, 230, 240, 255) if x > 3 else (255, 120, 40, 255)
    return (0, 0, 0, 0)


def heart(x, y):
    u, v = (x - 7.5) / 7, -(y - 8) / 7
    return (240, 40, 60, 255) if (u * u + v * v - 0.6) ** 3 - u * u * v ** 3 <= 0 else (0, 0, 0, 0)


big(32, ball)
big(34, star)
big(36, ship)
big(38, heart)


def write_png(path):
    raw = b"".join(b"\0" + bytes(c for p in row for c in p) for row in px)

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def write_map(path, mw=160, mh=90):
    rows = []
    for y in range(mh):
        row = []
        for x in range(mw):
            h = (math.sin(x * 0.09) + math.cos(y * 0.13) + math.sin((x + y) * 0.05)) / 3
            if h < -0.35:
                t = 2                                   # water
            elif h < -0.25:
                t = 4                                   # sand
            elif (x % 20 == 0 or y % 15 == 0) and h > 0:
                t = 5                                   # stone paths
            elif x % 40 in (10, 11) and 20 < y < 60:
                t = 3                                   # brick walls
            else:
                t = 6 if next(R) % 23 == 0 else (7 if next(R) % 3 == 0 else 1)
            row.append(t)
        rows.append(",".join(map(str, row)))
    with open(path, "w") as f:
        f.write("\n".join(rows) + "\n")


write_png(os.path.join(HERE, "sheet.png"))
write_map(os.path.join(HERE, "map.csv"))
print("wrote sheet.png and map.csv")
