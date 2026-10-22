#!/usr/bin/env python3
"""
mkplaza.py: makes the plaza of carts/herocart: assets/sheet.png (128 x 128, 8 pixel
tiles) and the three layers of the 3D map, assets/map_floor.csv, map_height.csv and
map_walls.csv (docs/api/light3d.md, "3D Map terrain").

A cell of map3d is 2 x 2 tiles and only the top left tile of a cell is read, so the
map is 64 x 64 tiles for a plaza of 32 x 32 cells (1 unit each). The id of a floor
or of a wall is also the place of its picture in the sheet: x = id % 16 * 8,
y = id // 16 * 8, 16 x 16 pixels, and the id of a wall is its height in steps of
0.25. So the pictures sit on even ids:

  floor  6 grass   10 grass, dark   14 path
  wall   2 stone (0.5 high, the fountain)   4 brick (1.0, the ring wall)
         8 sandstone (2.0, the spout)   12 pillar (3.0)

Original art, drawn here from a hash of the pixel place; no outside picture.

  mkplaza.py          writes the four files
"""
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "..", "assets")
CELLS = 32
CENTRE = (CELLS - 1) / 2.0      # 15.5


def noise(x, y, s):
    h = (x * 374761393 + y * 668265263 + s * 2147483647) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return (h ^ (h >> 16)) & 255


def put(px, ox, oy, fn):
    for y in range(16):
        for x in range(16):
            px[(oy + y) * 128 + ox + x] = fn(x, y)


def clampc(v):
    return max(0, min(255, int(v)))


def tint(base, k):
    return tuple(clampc(c * k) for c in base) + (255,)


def grass(base, seed):
    def f(x, y):
        n = noise(x, y, seed)
        return tint(base, 0.85 + 0.3 * n / 255)
    return f


def path(x, y):
    n = noise(x // 4, y // 4, 3)             # flagstones of 4 x 4
    edge = x % 4 == 0 or y % 4 == 0
    return tint((196, 176, 140), (0.8 if edge else 0.95 + 0.1 * n / 255))


def brick(base, rows, seed):
    def f(x, y):
        r = y // rows
        off = (r % 2) * 4
        mortar = y % rows == 0 or (x + off) % 8 == 0
        n = noise((x + off) // 8, r, seed)
        return tint((200, 200, 200), 0.6) if mortar else tint(base, 0.85 + 0.25 * n / 255)
    return f


def pillar(x, y):
    shade = 0.75 + 0.35 * abs(x - 7.5) / 7.5 * -1 + 0.35      # lit in the middle
    return tint((225, 220, 205), max(0.55, shade) + 0.05 * noise(x, y, 9) / 255)


def sheet():
    px = [(255, 0, 255, 255)] * (128 * 128)         # unused tiles: magenta, to show a wrong id
    put(px, 16, 0, brick((150, 150, 160), 4, 1))                            # 2 stone
    put(px, 32, 0, brick((176, 84, 60), 4, 2))                              # 4 brick
    put(px, 48, 0, grass((70, 150, 60), 4))                                 # 6 grass
    put(px, 64, 0, brick((214, 190, 130), 8, 5))                            # 8 sandstone
    put(px, 80, 0, grass((54, 120, 48), 6))                                 # 10 grass, dark
    put(px, 96, 0, pillar)                                                  # 12 pillar
    put(px, 112, 0, path)                                                   # 14 path
    return px


def png(w, h, px):
    raw = b"".join(b"\0" + b"".join(bytes(c) for c in px[y * w:(y + 1) * w]) for y in range(h))

    def chunk(t, body):
        return struct.pack(">I", len(body)) + t + body + struct.pack(">I", zlib.crc32(t + body) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def layers():
    floor = [[0] * CELLS for _ in range(CELLS)]
    height = [[0] * CELLS for _ in range(CELLS)]
    walls = [[0] * CELLS for _ in range(CELLS)]
    for z in range(CELLS):
        for x in range(CELLS):
            dx, dz = abs(x - CENTRE), abs(z - CENTRE)
            on_path = (dx < 1.1 and dz < 12) or (dz < 1.1 and dx < 12) or abs(max(dx, dz) - 9) < 1.1 and max(dx, dz) <= 10
            floor[z][x] = 14 if (on_path or max(dx, dz) < 4) else (6 if (x // 2 + z // 2) % 2 == 0 else 10)
            if max(dx, dz) < 4:
                height[z][x] = 1                              # the plateau of the fountain: 0.25
            if x in (0, CELLS - 1) or z in (0, CELLS - 1):
                walls[z][x] = 4                               # the ring wall
            elif 1.5 <= max(dx, dz) <= 2.5:
                walls[z][x] = 2                               # the fountain's rim
            elif max(dx, dz) < 1.5:
                walls[z][x] = 8                               # the spout
    for k in range(8):                                        # 8 pillars on a ring of radius 12
        import math
        a = k * math.pi / 4
        walls[int(round(CENTRE + 12 * math.sin(a)))][int(round(CENTRE + 12 * math.cos(a)))] = 12
    return floor, height, walls


def csv(path_, cells):
    # 2 x 2 tiles a cell, the same value in all four (the editor shows a full map)
    rows = []
    for row in cells:
        line = ",".join(str(v) for v in row for _ in range(2))
        rows += [line, line]
    with open(path_, "w", newline="\n") as f:
        f.write("\n".join(rows) + "\n")


def main():
    os.makedirs(ASSETS, exist_ok=True)
    with open(os.path.join(ASSETS, "sheet.png"), "wb") as f:
        f.write(png(128, 128, sheet()))
    floor, height, walls = layers()
    for name, cells in (("floor", floor), ("height", height), ("walls", walls)):
        csv(os.path.join(ASSETS, f"map_{name}.csv"), cells)
    print("wrote", ASSETS)


if __name__ == "__main__":
    main()
