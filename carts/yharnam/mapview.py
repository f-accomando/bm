#!/usr/bin/env python3
"""The map of Yharnam as a picture (needs Pillow), from the text of tests/yharnam/mapdump.lua:

    build/host/luahost tests/yharnam/mapdump.lua carts/yharnam/main.lua | python3 carts/yharnam/mapview.py docs/img/yharnam-map.png

Chunks coloured by district (the boss's arena in red), the hunter's lamps (yellow), the start; on the
borders between areas the barricade: sealed (grey) or a gate: breakable orange, one-way blue (an arrow
for the side it opens from), with a horde red dots, and the boss it waits for beside it."""
import sys
from PIL import Image, ImageDraw

C, M = 72, 28
COL = {"t": (96, 88, 80), "s": (160, 136, 92), "p": (70, 116, 70), "c": (76, 88, 112), "y": (176, 84, 52),
       "w": (38, 82, 50), "l": (88, 136, 70), "h": (136, 124, 156), "B": (196, 60, 60)}
NAME = {"t": "town", "s": "square", "p": "park", "c": "graves", "y": "pyre", "w": "woods", "l": "glade",
        "h": "chapel", "B": "BOSS"}
cells, lamps, bosses, gates, names, size = {}, [], {}, [], {}, 8
for line in sys.stdin:
    f = line.split()
    if not f:
        continue
    if f[0] == "size":
        size = int(f[1])
    elif f[0] == "area":
        names[(int(f[3]), int(f[4]))] = f[7].replace("_", " ")
    elif f[0] == "row":
        for k, ch in enumerate(f[4]):
            cells[(int(f[3]) + k, int(f[2]))] = ch
    elif f[0] == "lamp":
        lamps.append((int(f[1]), int(f[2])))
    elif f[0] == "boss":
        bosses[(int(f[1]), int(f[2]))] = f[3]
    elif f[0] == "gate":
        gates.append(f[1:])
W = size * C + 2 * M
im = Image.new("RGB", (W, W + 40), (16, 16, 22))
d = ImageDraw.Draw(im)


def xy(cx, cy):
    return M + cx * C, M + cy * C


for (cx, cy), ch in cells.items():
    x, y = xy(cx, cy)
    d.rectangle([x + 2, y + 2, x + C - 2, y + C - 2], fill=COL[ch])
    d.text((x + 6, y + 6), NAME[ch], fill=(235, 235, 235))
    if (cx, cy) in bosses:
        d.text((x + 6, y + 20), bosses[(cx, cy)], fill=(255, 235, 200))
    if (cx, cy) == (0, 0):
        d.text((x + 6, y + C - 18), "START", fill=(255, 255, 255))
for cx, cy in lamps:
    x, y = xy(cx, cy)
    d.ellipse([x + C - 20, y + C - 20, x + C - 8, y + C - 8], fill=(255, 220, 90))
for (cx, cy), n in names.items():
    x, y = xy(cx, cy)
    d.text((x, y - 14), n, fill=(200, 200, 210))
# the borders between the areas: sealed all along, a gate where it is
for (cx, cy), ch in cells.items():
    for dx, dy in ((1, 0), (0, 1)):
        n = (cx + dx, cy + dy)
        if n in cells and any(names.get((ax, ay)) for ax, ay in [(0, 0)]):
            pass
areas_of = {}
for (x0, y0), n in names.items():
    for cy in range(y0, y0 + 4):
        for cx in range(x0, x0 + 4):
            areas_of[(cx, cy)] = (x0, y0)
SEG = {}
for (cx, cy), a in areas_of.items():
    for dx, dy in ((1, 0), (0, 1)):
        n = (cx + dx, cy + dy)
        if n in areas_of and areas_of[n] != a:
            SEG[(cx, cy, dx, dy)] = None
for g in gates:
    gid, typ, req, frm, e = g[:5]
    for s in e.split(";"):
        cx, cy, dx, dy = map(int, s.split(","))
        key = (cx, cy, dx, dy) if dx > 0 or dy > 0 else (cx + dx, cy + dy, -dx, -dy)
        SEG[key] = (gid, typ, req, frm)
for (cx, cy, dx, dy), g in SEG.items():
    x, y = xy(cx + dx, cy + dy)
    if dx:
        a, b = (x, y), (x, y + C)
    else:
        a, b = (x, y), (x + C, y)
    if g is None:
        d.line([a, b], fill=(120, 120, 130), width=5)
        continue
    gid, typ, req, frm = g
    col = (80, 150, 255) if typ == "oneway" else (255, 150, 40)
    d.line([a, b], fill=col, width=7)
    mx, my = (a[0] + b[0]) // 2, (a[1] + b[1]) // 2
    if "horde" in typ:
        for k in (-18, 0, 18):
            px, py = (mx, my + k) if dx else (mx + k, my)
            d.ellipse([px - 4, py - 4, px + 4, py + 4], fill=(230, 40, 40))
    d.text((mx + 6, my - 6), req if req != "-" else "open", fill=(255, 255, 255))
ly = W + 6
for k, (txt, col) in enumerate((("sealed", (120, 120, 130)), ("gate: breakable", (255, 150, 40)),
                                ("gate: one-way", (80, 150, 255)), ("horde", (230, 40, 40)),
                                ("hunter's lamp", (255, 220, 90)))):
    d.rectangle([M + k * 130, ly, M + k * 130 + 12, ly + 12], fill=col)
    d.text((M + k * 130 + 18, ly), txt, fill=(220, 220, 220))
d.text((M, ly + 20), "written beside a gate: the boss it waits for (open: none). Rows north to south.",
       fill=(150, 150, 160))
im.save(sys.argv[1])
