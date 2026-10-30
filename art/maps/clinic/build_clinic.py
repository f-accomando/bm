#!/usr/bin/env python3
"""build_clinic.py: the map kit `clinic.bmk` for Dark Night (a Victorian, dystopian, cramped hospital and its courtyard).

  python3 build_clinic.py OUT.bmk [--png TOPVIEW.png]

One BMRES kit (kind 6, like island.bmk): MESH (static models, each in a textured and a flat look), SHEET8 (one sheet),
MAP and LAYERS (floor, height, walls, objects, as map3d_build reads them), FLAGS, INFO (the object table), COVER.
Built from scripts/bmkit.py (Sheet, Solid, box, cylinder). The layout is a rotated, simplified reading of the reference
map: the Clinic on the west, a long hall with a stair down to a grand atrium, a courtyard, and a stair up to the
Courtyard Gate on the east. 1 tile = 1 unit; a person is about 1.9 units tall.

How map3d reads a wall: the value in the `walls` layer is BOTH the height (value x 0.25 units) and the texture, taken from
the sheet cell with that number. So a wall is always one of the textures of the sheet's row 0, tile k = cell 2k: height
k x 0.5 units. The row-0 tiles of this kit are therefore wall styles and floors at the same time (WALL_OF below).
"""
import argparse
import math
import os
import random
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmkit as K  # noqa: E402
import bmmesh  # noqa: E402
import bmres  # noqa: E402

SW = SH = 256
NAME = "clinic"
TW, TH = 80, 48                       # the map in tiles
CW, CH = TW * 2, TH * 2               # in cells (a tile is 2 x 2 cells)

# ---------------------------------------------------------------- the sheet

# row 0 (y = 0): tile k at x = 16 k. Floors and walls share them: the wall id of tile k is 2k (height k x 0.5 units).
TILE = {"ward": 1, "wood": 2, "cobble": 3, "fence": 4, "plaster": 5, "flags": 6, "clinicwall": 7, "dirt": 8,
        "stucco": 9, "facade": 10, "road": 11, "tall": 12, "carpet": 13, "gutter": 14, "stairs": 15}
WALL_OF = {n: 2 * k for n, k in TILE.items()}          # the walls-layer value of a tile used as a wall
# textures of the models: row y = 16 and y = 32, 16 x 16 each
TEXS = {"wood": (0, 16, 16, 16), "linen": (16, 16, 16, 16), "iron": (32, 16, 16, 16), "brass": (48, 16, 16, 16),
        "stone": (64, 16, 16, 16), "blood": (80, 16, 16, 16), "glass": (96, 16, 16, 16), "plank": (112, 16, 16, 16)}


def rgb(c):
    return (c >> 16 & 255, c >> 8 & 255, c & 255)


def mix(a, b, t):
    ra, rb = rgb(a), rgb(b)
    return tuple(int(ra[i] + (rb[i] - ra[i]) * t) for i in range(3))


def tint(c, k):
    r, g, b = rgb(c)
    return (max(0, min(255, int(r * k))) << 16) | (max(0, min(255, int(g * k))) << 8) | max(0, min(255, int(b * k)))


class Paint:
    """a 16 x 16 tile of the sheet: put(i, j, colour)"""

    def __init__(self, sh, x, y, seed):
        self.sh, self.x, self.y, self.rnd = sh, x, y, random.Random(seed)

    def put(self, i, j, c):
        self.sh.put(self.x + i, self.y + j, c)

    def fill(self, c):
        for j in range(16):
            for i in range(16):
                self.put(i, j, c)

    def rect(self, i0, j0, i1, j1, c):
        for j in range(j0, j1):
            for i in range(i0, i1):
                self.put(i, j, c)

    def speckle(self, ramp, amount=1.0):
        for j in range(16):
            for i in range(16):
                if self.rnd.random() < amount:
                    self.put(i, j, ramp[self.rnd.randrange(len(ramp))])


def t_ward(p):                       # the infirmary floor: pale green-grey squares with a dark grout line
    for j in range(16):
        for i in range(16):
            sq = ((i // 8) + (j // 8)) % 2
            c = (0xA2ACA5, 0x8B958F)[sq]
            if i % 8 == 0 or j % 8 == 0:
                c = 0x5A635F
            p.put(i, j, c)


def t_wood(p):                       # planks: warm brown with dark gaps
    for j in range(16):
        base = (0xA8804F, 0x94703F, 0xB08A57, 0x9A7646)[(j // 4) % 4]
        for i in range(16):
            c = base
            if j % 4 == 0:
                c = 0x4E3822
            elif (i + 5 * (j // 4)) % 16 == 0:
                c = 0x5F462B
            p.put(i, j, c)
    p.speckle([0x8A6838, 0xB89260], 0.06)


def t_cobble(p):                     # small round stones
    for cy in range(4):
        for cx in range(4):
            base = (0x948E80, 0x857F72, 0xA09A8B, 0x7C776A)[p.rnd.randrange(4)]
            off = 2 if cy % 2 else 0
            for j in range(4):
                for i in range(4):
                    x, y = (cx * 4 + i + off) % 16, cy * 4 + j
                    c = base
                    if i == 0 or j == 0:
                        c = 0x4B4740
                    elif i == 1 and j == 1:
                        c = tint(base, 1.12)
                    p.put(x, y, c)


def t_fence(p):                      # iron bars in front of a lighter back, so the bars read
    p.fill(0x7D8794)
    for i in range(16):
        if i % 4 in (1, 2):
            for j in range(16):
                p.put(i, j, 0x20242B if i % 4 == 1 else 0x3A414C)
    p.rect(0, 0, 16, 2, 0x20242B)
    p.rect(0, 7, 16, 9, 0x20242B)
    p.rect(0, 14, 16, 16, 0x20242B)


def t_plaster(p):                    # cream plaster, a dark rail on top, a stained foot
    for j in range(16):
        for i in range(16):
            c = 0xD5C9AD if j < 11 else 0xB9AB8C
            if j < 2:
                c = 0x4A3A2C
            elif j == 2:
                c = 0xEFE6CE
            p.put(i, j, c)
    p.speckle([0xC9BD9F, 0xDDD2B8], 0.10)


def t_flags(p):                      # courtyard flagstones, lighter than the walls
    for j in range(16):
        for i in range(16):
            row = j // 8
            x = (i + 4 * row) % 16
            c = (0xA29C8C, 0x938D7F)[((x // 8) + row) % 2]
            if j % 8 == 0 or x % 8 == 0:
                c = 0x5A564D
            p.put(i, j, c)
    p.speckle([0x8C8678, 0xAFA999], 0.08)


def t_clinicwall(p):                 # infirmary wall: cream plaster over a muted green dado (the dado is the bottom third)
    for j in range(16):
        for i in range(16):
            if j < 10:
                c = 0xC2B79C
                if i % 8 == 0:
                    c = 0xA79C82
                elif (i * 7 + j * 3) % 11 == 0:
                    c = 0xB3A88D                                # a stain
            elif j == 10:
                c = 0xDDD4BA
            else:
                c = 0x41614F if (i % 8) else 0x34503F
            p.put(i, j, c)
    p.rect(0, 0, 16, 1, 0x8E8369)


def t_dirt(p):
    p.fill(0x7C6B50)
    p.speckle([0x6E5E45, 0x8A795B, 0x5F5039, 0x95845F], 0.7)


def t_stucco(p):                     # half-timber: cream infill in dark beams
    p.fill(0xCBBB99)
    for j in range(16):
        for i in range(16):
            if i in (0, 1, 14, 15) or j in (0, 1, 7, 8, 14, 15) or abs(i - j) < 1:
                p.put(i, j, 0x44301F)
    p.speckle([0xBBAA88, 0xD8C8A6], 0.08)


def t_facade(p):                     # gothic stone front: courses, a pale-framed window with one lit pane
    for j in range(16):
        for i in range(16):
            c = 0x7B8190
            if j % 4 == 0:
                c = 0x535866
            elif (i + 8 * ((j // 4) % 2)) % 16 == 0:
                c = 0x5E6370
            p.put(i, j, c)
    p.rect(4, 3, 12, 11, 0xB5BAC8)
    p.rect(5, 4, 11, 10, 0x1F2636)
    p.rect(5, 4, 7, 6, 0xE3A83E)
    p.rect(8, 4, 9, 10, 0xB5BAC8)
    p.rect(5, 7, 11, 8, 0xB5BAC8)


def t_road(p):                       # alley cobbles: darker than the courtyard flags
    for cy in range(4):
        for cx in range(4):
            base = (0x77726A, 0x6A665F, 0x817C73)[(cx + cy * 2) % 3]
            for j in range(4):
                for i in range(4):
                    c = base if (i and j) else 0x3A3732
                    p.put((cx * 4 + i + (2 if cy % 2 else 0)) % 16, cy * 4 + j, c)


def t_tall(p):                       # tall boundary wall: big dark blocks, pale mortar
    for j in range(16):
        for i in range(16):
            c = 0x6B7287
            if j % 4 == 0:
                c = 0x9AA2B8
            elif (i + 8 * ((j // 4) % 2)) % 16 == 0:
                c = 0x8890A6
            p.put(i, j, c)
    p.speckle([0x636A7E, 0x757C91], 0.10)


def t_carpet(p):                     # the red runner of the hall, a gold border
    p.fill(0x883940)
    for k in range(16):
        for c in (0, 15):
            p.put(k, c, 0xC29A45)
            p.put(c, k, 0xC29A45)
    for k in range(2, 14):
        p.put(k, 2, 0x5E2429)
        p.put(k, 13, 0x5E2429)
        p.put(2, k, 0x5E2429)
        p.put(13, k, 0x5E2429)
    for d in range(6):
        p.put(8 + d % 3 - 1, 4 + d, 0xA94A52)
        p.put(7 - d % 3 + 1, 4 + d, 0xA94A52)


def t_gutter(p):                     # a drain of dark water between stone lips
    p.fill(0x3B4C59)
    for i in range(16):
        p.put(i, 0, 0x7A756B)
        p.put(i, 15, 0x7A756B)
        p.put(i, 5, 0x587386)
        p.put(i, 10, 0x4A6275)


def t_stairs(p):                     # a stone tread: pale nosing, a dark riser line
    p.fill(0xA29C8F)
    for i in range(16):
        p.put(i, 0, 0xCDC7B9)
        p.put(i, 1, 0xB8B2A4)
        p.put(i, 15, 0x6E695F)
    p.speckle([0x958F82, 0xADA79A], 0.1)


TILE_PAINT = {"ward": t_ward, "wood": t_wood, "cobble": t_cobble, "fence": t_fence, "plaster": t_plaster,
              "flags": t_flags, "clinicwall": t_clinicwall, "dirt": t_dirt, "stucco": t_stucco, "facade": t_facade,
              "road": t_road, "tall": t_tall, "carpet": t_carpet, "gutter": t_gutter, "stairs": t_stairs}


def paint_model_textures(sh):
    def tex(name, fn):
        x, y, _, _ = TEXS[name]
        fn(Paint(sh, x, y, hash(name) & 0xFFFF))

    def wood(p):
        for j in range(16):
            for i in range(16):
                c = (0x73502F, 0x62432A)[(i // 5) % 2]
                if i % 5 == 0:
                    c = 0x3A2616
                p.put(i, j, c)
        p.speckle([0x7F5A36, 0x56391F], 0.08)

    def linen(p):
        p.fill(0xE9E3D4)
        for k in range(16):
            p.put(k, 7, 0xCFC8B6)
            p.put(k, 8, 0xD8D2C1)
        p.rect(0, 0, 16, 1, 0xC4BDAB)

    def iron(p):
        p.fill(0x424852)
        for i in (2, 13):
            for j in (2, 13):
                p.put(i, j, 0x7C8594)
        p.rect(0, 0, 16, 1, 0x5B626E)

    def brass(p):
        p.fill(0xC9A24A)
        p.rect(0, 0, 16, 2, 0xE5C46A)
        p.rect(0, 14, 16, 16, 0x8F6F2C)

    def stone(p):
        for j in range(16):
            for i in range(16):
                c = 0x8F8A7D
                if j % 8 == 0 or (i + 8 * (j // 8)) % 16 == 0:
                    c = 0x5C584F
                p.put(i, j, c)

    def blood(p):
        linen(p)
        for (cx, cy, r) in ((5, 6, 4), (11, 10, 3), (9, 3, 2)):
            for j in range(16):
                for i in range(16):
                    if (i - cx) ** 2 + (j - cy) ** 2 <= r * r:
                        p.put(i, j, 0x8A2B2B if (i + j) % 3 else 0x6E2020)

    def glass(p):
        p.fill(0xF3B342)
        p.rect(0, 0, 16, 1, 0x5A3A1A)
        p.rect(0, 15, 16, 16, 0x5A3A1A)
        p.rect(0, 0, 1, 16, 0x5A3A1A)
        p.rect(15, 0, 16, 16, 0x5A3A1A)
        p.rect(5, 3, 11, 13, 0xFFD97A)

    def plank(p):
        for j in range(16):
            c = (0x8A6A43, 0x7A5C38)[(j // 4) % 2]
            for i in range(16):
                p.put(i, j, 0x3C2A18 if j % 4 == 0 else c)

    for n, f in (("wood", wood), ("linen", linen), ("iron", iron), ("brass", brass), ("stone", stone),
                 ("blood", blood), ("glass", glass), ("plank", plank)):
        tex(n, f)


def paint_sheet():
    sh = K.Sheet()
    for name, k in TILE.items():
        TILE_PAINT[name](Paint(sh, 16 * k, 0, k * 977))
    paint_model_textures(sh)
    return sh


def tile_average(sh, k):
    r = g = b = 0
    for j in range(16):
        for i in range(16):
            o = ((j * SW) + 16 * k + i) * 4
            r, g, b = r + sh.px[o], g + sh.px[o + 1], b + sh.px[o + 2]
    return (r // 256) << 16 | (g // 256) << 8 | b // 256


# ---------------------------------------------------------------- the models (static elements of the map)

SOLID, DECOR = K.SOLID, K.DECOR
box = K.box
iron_c, wood_c, brass_c = 0x424852, 0x73502F, 0xC9A24A


def turn(s, quarter=1):
    """a model turned about y by `quarter` x 90 degrees: (x, z) -> (z, -x)"""
    for _ in range(quarter):
        s.faces = [([(z, y, -x) for x, y, z in pts], c, t) for pts, c, t in s.faces]
    return s


def prism_x(s, x0, x1, cy, cz, r, n, colour, tex=None):
    """a prism lying along x (a wheel, a barrel on its side) with n sides"""
    ring = [(cy + r * math.cos(2 * math.pi * i / n), cz + r * math.sin(2 * math.pi * i / n)) for i in range(n)]
    mid = ((x0 + x1) / 2, cy, cz)
    for i in range(n):
        a, b = ring[i], ring[(i + 1) % n]
        s.add(mid, [(x0, a[0], a[1]), (x1, a[0], a[1]), (x1, b[0], b[1]), (x0, b[0], b[1])], colour, tex)
    for x in (x0, x1):
        for i in range(n):
            a, b = ring[i], ring[(i + 1) % n]
            s.add(mid, [(x, cy, cz), (x, a[0], a[1]), (x, b[0], b[1])], colour, tex)


def m_bed():
    s = K.Solid("bed", SOLID)
    for sx in (-0.88, 0.88):                                     # iron frame: four legs and the two ends
        for sz in (-0.38, 0.38):
            box(s, sx - 0.03, 0, sz - 0.03, sx + 0.03, 0.4, sz + 0.03, iron_c, TEXS["iron"])
    box(s, -0.95, 0.36, -0.42, 0.95, 0.42, 0.42, iron_c, TEXS["iron"])
    box(s, -0.92, 0.42, -0.38, 0.92, 0.56, 0.38, 0xE9E3D4, TEXS["linen"])             # the mattress
    box(s, -0.90, 0.56, -0.30, -0.52, 0.64, 0.30, 0xF4EFE2, TEXS["linen"])             # the pillow
    box(s, -0.40, 0.56, -0.36, 0.90, 0.60, 0.36, 0xB9B2A0, TEXS["linen"])              # the blanket
    box(s, -1.0, 0.3, -0.42, -0.94, 0.95, 0.42, iron_c, TEXS["iron"])                  # the head
    box(s, 0.94, 0.3, -0.42, 1.0, 0.7, 0.42, iron_c, TEXS["iron"])                     # the foot
    return s


def m_table():
    s = K.Solid("table", SOLID)
    for sx in (-0.58, 0.58):
        for sz in (-0.26, 0.26):
            box(s, sx - 0.04, 0, sz - 0.04, sx + 0.04, 0.8, sz + 0.04, iron_c, TEXS["iron"])
    box(s, -0.75, 0.8, -0.38, 0.75, 0.9, 0.38, 0xC9BFAE, TEXS["blood"])               # the slab with a stained sheet
    box(s, -0.6, 0.35, -0.3, 0.6, 0.39, 0.3, iron_c, TEXS["iron"])
    return s


def m_cabinet():
    s = K.Solid("cabinet", SOLID)
    box(s, -0.45, 0, -0.2, 0.45, 1.9, 0.2, wood_c, TEXS["wood"])
    box(s, -0.5, 1.9, -0.24, 0.5, 1.98, 0.24, 0x4E3822, TEXS["wood"])
    for j, y in enumerate((0.45, 0.95, 1.45)):                                          # shelves of bottles (green, red, amber)
        for i, c in enumerate((0x4F8F5E, 0xB04040, 0xD9A03A, 0x4F8F5E)):
            box(s, -0.36 + i * 0.2, y, 0.2, -0.26 + i * 0.2, y + 0.22 + 0.04 * ((i + j) % 2), 0.26, c)
        box(s, -0.42, y - 0.04, 0.19, 0.42, y, 0.28, 0x4E3822, TEXS["wood"])
    return s


def m_lamp():
    """a gas street lamp: iron post, an arm, a lit amber lantern (the bright colours read in a dark street)"""
    s = K.Solid("lamp", SOLID)
    K.cylinder(s, 0, 0, 0, 0.14, 0.1, 0.25, 8, 0x353A42, TEXS["iron"])
    K.cylinder(s, 0, 0.25, 0, 0.055, 0.045, 2.35, 8, 0x353A42, TEXS["iron"])
    box(s, -0.02, 2.5, -0.02, 0.28, 2.56, 0.02, 0x353A42, TEXS["iron"])
    box(s, 0.12, 2.56, -0.14, 0.44, 2.6, 0.14, brass_c, TEXS["brass"])
    box(s, 0.14, 2.6, -0.12, 0.42, 3.0, 0.12, 0xFFC24A, TEXS["glass"])
    box(s, 0.1, 3.0, -0.16, 0.46, 3.07, 0.16, 0x353A42, TEXS["iron"])
    return s


def m_lantern():
    """the checkpoint lantern: a stone foot, a short iron post, a large glowing orange lantern"""
    s = K.Solid("lantern", SOLID)
    box(s, -0.28, 0, -0.28, 0.28, 0.22, 0.28, 0x8F8A7D, TEXS["stone"])
    K.cylinder(s, 0, 0.22, 0, 0.06, 0.05, 1.05, 8, 0x353A42, TEXS["iron"])
    box(s, -0.2, 1.27, -0.2, 0.2, 1.33, 0.2, brass_c, TEXS["brass"])
    box(s, -0.17, 1.33, -0.17, 0.17, 1.72, 0.17, 0xFF8A2A, TEXS["glass"])
    box(s, -0.21, 1.72, -0.21, 0.21, 1.79, 0.21, 0x353A42, TEXS["iron"])
    s.add((0, 1.8, 0), [(-0.21, 1.79, -0.21), (0.21, 1.79, -0.21), (0.0, 1.95, 0.0)], 0x353A42, TEXS["iron"])
    s.add((0, 1.8, 0), [(0.21, 1.79, -0.21), (0.21, 1.79, 0.21), (0.0, 1.95, 0.0)], 0x353A42, TEXS["iron"])
    s.add((0, 1.8, 0), [(0.21, 1.79, 0.21), (-0.21, 1.79, 0.21), (0.0, 1.95, 0.0)], 0x353A42, TEXS["iron"])
    s.add((0, 1.8, 0), [(-0.21, 1.79, 0.21), (-0.21, 1.79, -0.21), (0.0, 1.95, 0.0)], 0x353A42, TEXS["iron"])
    return s


def m_door(locked):
    """a door in a wall that runs along z... built for a wall along x (the panel plane is along x); `turn` makes the other"""
    s = K.Solid("door", SOLID)
    box(s, -0.5, 0, -0.1, -0.42, 2.45, 0.1, 0x4E3822, TEXS["wood"])                    # the posts
    box(s, 0.42, 0, -0.1, 0.5, 2.45, 0.1, 0x4E3822, TEXS["wood"])
    box(s, -0.5, 2.45, -0.1, 0.5, 2.6, 0.1, 0x4E3822, TEXS["wood"])                    # the lintel
    box(s, -0.42, 0, -0.05, 0.42, 2.45, 0.05, 0x8A6238, TEXS["plank"])                 # the panel
    box(s, 0.28, 1.0, 0.05, 0.34, 1.08, 0.1, brass_c, TEXS["brass"])                   # the handle
    box(s, -0.5, 2.6, -0.1, 0.5, 3.5, 0.1, 0xDAD0B8)                                   # the plaster over the door, up to the wall
    if locked:
        box(s, -0.42, 0.7, 0.05, 0.42, 0.8, 0.09, iron_c, TEXS["iron"])                # an iron bar across
        box(s, -0.42, 1.5, 0.05, 0.42, 1.6, 0.09, iron_c, TEXS["iron"])
        box(s, -0.08, 1.05, 0.09, 0.08, 1.25, 0.14, brass_c, TEXS["brass"])            # the lock
    return s


def m_gate():
    """the Courtyard Gate: two stone posts, a double iron gate 3 wide (along z), a plain arch of bars"""
    s = K.Solid("gate", SOLID)
    for z in (-1.5, 1.35):
        box(s, -0.2, 0, z - 0.0, 0.2, 3.4, z + 0.15, 0x8F8A7D, TEXS["stone"])
        box(s, -0.26, 3.4, z - 0.06, 0.26, 3.55, z + 0.21, 0x8F8A7D, TEXS["stone"])
    box(s, -0.06, 3.0, -1.35, 0.06, 3.15, 1.35, iron_c, TEXS["iron"])
    box(s, -0.06, 0.25, -1.35, 0.06, 0.4, 1.35, iron_c, TEXS["iron"])
    for i in range(13):
        z = -1.3 + i * (2.6 / 12)
        box(s, -0.04, 0.0, z - 0.03, 0.04, 3.3, z + 0.03, iron_c, TEXS["iron"])
    box(s, -0.05, 0.0, -0.04, 0.05, 3.15, 0.04, 0x6E7482)                              # the middle seam
    return s


def m_crate():
    s = K.Solid("crate", SOLID)
    box(s, -0.4, 0, -0.4, 0.4, 0.8, 0.4, 0x8A6238, TEXS["plank"])
    for y in (0.0, 0.7):
        box(s, -0.42, y, -0.42, 0.42, y + 0.1, 0.42, 0x4E3822, TEXS["wood"])
    return s


def m_barrel():
    s = K.Solid("barrel", SOLID)
    K.cylinder(s, 0, 0, 0, 0.3, 0.36, 0.45, 10, 0x7A5632, TEXS["plank"], caps=False)
    K.cylinder(s, 0, 0.45, 0, 0.36, 0.3, 0.45, 10, 0x7A5632, TEXS["plank"], caps=False)
    K.cylinder(s, 0, 0.88, 0, 0.3, 0.28, 0.04, 10, 0x5F4326, TEXS["wood"], caps=True)
    for y in (0.08, 0.45, 0.8):
        K.cylinder(s, 0, y, 0, 0.375, 0.375, 0.05, 10, iron_c, TEXS["iron"], caps=False)
    return s


def m_pillar():
    s = K.Solid("pillar", SOLID)
    box(s, -0.42, 0, -0.42, 0.42, 0.3, 0.42, 0x8F8A7D, TEXS["stone"])
    K.cylinder(s, 0, 0.3, 0, 0.3, 0.26, 3.0, 10, 0x9B9689, TEXS["stone"])
    box(s, -0.44, 3.3, -0.44, 0.44, 3.55, 0.44, 0x8F8A7D, TEXS["stone"])
    return s


def m_bench():
    s = K.Solid("bench", SOLID)
    for sx in (-0.65, 0.65):
        box(s, sx - 0.04, 0, -0.18, sx + 0.04, 0.45, 0.18, iron_c, TEXS["iron"])
    box(s, -0.8, 0.45, -0.2, 0.8, 0.52, 0.2, 0x8A6238, TEXS["plank"])
    box(s, -0.8, 0.7, -0.22, 0.8, 0.98, -0.16, 0x8A6238, TEXS["plank"])
    box(s, -0.7, 0.52, -0.2, -0.64, 0.98, -0.16, iron_c, TEXS["iron"])
    box(s, 0.64, 0.52, -0.2, 0.7, 0.98, -0.16, iron_c, TEXS["iron"])
    return s


def m_cart():
    s = K.Solid("cart", SOLID)
    box(s, -0.7, 0.45, -0.4, 0.7, 0.55, 0.4, 0x8A6238, TEXS["plank"])                 # the bed
    for sz in (-0.4, 0.36):
        box(s, -0.7, 0.55, sz, 0.7, 0.95, sz + 0.04, 0x7A5C38, TEXS["plank"])         # the sides
    for sx in (-0.7, 0.66):
        box(s, sx, 0.55, -0.4, sx + 0.04, 0.95, 0.4, 0x7A5C38, TEXS["plank"])
    wheel = K.Solid("wheel")                                                           # a prism along x, turned to lie along z
    prism_x(wheel, -0.04, 0.04, 0.0, 0.0, 0.38, 10, 0x4E3822, TEXS["wood"])
    for sx in (-0.4, 0.4):
        for sz in (0.46, -0.5):
            for pts, c, t in wheel.faces:
                s.faces.append(([(sx + y, 0.38 + z, sz + x) for x, y, z in pts], c, t))
    box(s, -1.3, 0.5, -0.04, -0.7, 0.56, 0.04, 0x4E3822, TEXS["wood"])                 # the shaft
    for i, (x, z) in enumerate(((-0.3, -0.1), (0.2, 0.15), (0.35, -0.2))):             # sacks and a box on it
        box(s, x - 0.18, 0.55, z - 0.18, x + 0.18, 0.85 + 0.1 * (i % 2), z + 0.18, (0xB59E6B, 0x8A6238, 0xA38458)[i],
            TEXS["plank"] if i == 1 else TEXS["linen"])
    return s


def m_barricade():
    """a barricade across a 3-wide alley: two posts, crossed planks, sharpened stakes (built along x)"""
    s = K.Solid("barricade", SOLID)
    for x in (-1.45, 1.4):
        box(s, x, 0, -0.12, x + 0.1, 2.0, 0.12, 0x4E3822, TEXS["wood"])
    for y, h in ((0.25, 0.2), (0.75, 0.2), (1.25, 0.2)):
        box(s, -1.45, y, -0.06, 1.5, y + h, 0.06, 0x8A6238, TEXS["plank"])
    for i in range(7):                                                                  # stakes
        x = -1.2 + i * 0.4
        s.add((x, 0.4, 0), [(x - 0.07, 1.45, -0.06), (x + 0.07, 1.45, -0.06), (x, 2.0, 0.0)], 0x5F4326, TEXS["wood"])
        s.add((x, 0.4, 0), [(x - 0.07, 1.45, 0.06), (x + 0.07, 1.45, 0.06), (x, 2.0, 0.0)], 0x5F4326, TEXS["wood"])
    return s


# name -> (builder, turns, class, radius): the object table of the kit
OBJECTS = (
    ("bed",         lambda: m_bed(),                 SOLID, 0.7),
    ("bed_ns",      lambda: turn(m_bed()),           SOLID, 0.7),
    ("table",       lambda: m_table(),               SOLID, 0.6),
    ("cabinet",     lambda: m_cabinet(),             SOLID, 0.5),
    ("lamp",        lambda: m_lamp(),                SOLID, 0.15),
    ("lantern",     lambda: m_lantern(),             SOLID, 0.3),
    ("door_ew",     lambda: m_door(False),           SOLID, 0.5),
    ("door_ns",     lambda: turn(m_door(False)),     SOLID, 0.5),
    ("door_ew_l",   lambda: m_door(True),            SOLID, 0.5),
    ("door_ns_l",   lambda: turn(m_door(True)),      SOLID, 0.5),
    ("gate_ns",     lambda: m_gate(),                SOLID, 1.5),
    ("crate",       lambda: m_crate(),               SOLID, 0.5),
    ("barrel",      lambda: m_barrel(),              SOLID, 0.4),
    ("pillar",      lambda: m_pillar(),              SOLID, 0.4),
    ("bench",       lambda: m_bench(),               SOLID, 0.8),
    ("bench_ns",    lambda: turn(m_bench()),         SOLID, 0.8),
    ("cart",        lambda: m_cart(),                SOLID, 0.9),
    ("barricade",   lambda: m_barricade(),           SOLID, 1.5),
)
FIRST_ID = K.FIRST_KIT_ID                           # 1040: the table's ids go 1040, 1042, ...
IDS = {n: FIRST_ID + 2 * i for i, (n, *_r) in enumerate(OBJECTS)}
LOGIC = ("enemy", "player_start")                   # objects with no model: spawn points
ENEMY = FIRST_ID + 2 * len(OBJECTS)
PLAYER_START = 1024


def make_models(look):
    old, K.LOOK = K.LOOK, look
    try:
        out = []
        for name, fn, cls, _r in OBJECTS:
            s = fn()
            s.name, s.flags = name, cls
            out.append(s.model())
        return out
    finally:
        K.LOOK = old


# ---------------------------------------------------------------- the map

class Grid:
    def __init__(self):
        self.fl = [[0] * TW for _ in range(TH)]       # floor tile (0: none)
        self.ht = [[0] * TW for _ in range(TH)]       # height in steps of 0.25 units
        self.wl = [[0] * TW for _ in range(TH)]       # wall value (0: none)
        self.ob = [[0] * TW for _ in range(TH)]
        self.zone = [[None] * TW for _ in range(TH)]
        self.zstyle = {}

    def open(self, x0, z0, x1, z1, floor, h, zone):
        for z in range(z0, z1):
            for x in range(x0, x1):
                self.fl[z][x], self.ht[z][x], self.zone[z][x] = TILE[floor], h, zone

    def solid(self, x0, z0, x1, z1, style, zone="block"):
        """a block of wall tiles (a building seen from the street): floor none, the wall's base at the ground"""
        for z in range(z0, z1):
            for x in range(x0, x1):
                self.fl[z][x], self.wl[z][x], self.ht[z][x], self.zone[z][x] = 0, WALL_OF[style], 0, zone

    def is_open(self, x, z):
        return 0 <= x < TW and 0 <= z < TH and self.fl[z][x] != 0

    def shell(self):
        """a ring of wall tiles round every open tile: the wall style and the base come from the zone it hugs"""
        new = {}
        for z in range(TH):
            for x in range(TW):
                if self.fl[z][x] or self.wl[z][x]:
                    continue
                best = None
                for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, 1), (1, -1), (-1, -1)):
                    nx, nz = x + dx, z + dz
                    if self.is_open(nx, nz):
                        cand = (self.zstyle[self.zone[nz][nx]], self.ht[nz][nx], 0 if dx == 0 or dz == 0 else 1)
                        if best is None or cand[2] < best[2] or (cand[2] == best[2] and cand[1] < best[1]):
                            best = cand
                if best:
                    new[(x, z)] = best
        for (x, z), (style, h, _d) in new.items():
            self.wl[z][x], self.ht[z][x] = WALL_OF[style], h

    def partition(self, x0, z0, x1, z1, style):
        for z in range(z0, z1):
            for x in range(x0, x1):
                h = self.ht[z][x]
                self.fl[z][x], self.wl[z][x], self.ht[z][x] = 0, WALL_OF[style], h

    def put(self, name, x, z):
        oid = IDS.get(name, ENEMY if name == "enemy" else PLAYER_START)
        assert self.is_open(x, z), f"{name} at {x},{z} is not on a floor tile"
        assert not self.ob[z][x], f"{name} at {x},{z}: the tile has an object"
        self.ob[z][x] = oid


def build_map():
    g = Grid()
    g.zstyle = {"ward": "clinicwall", "hall": "clinicwall", "atrium": "facade", "yard": "facade", "alley": "tall",
                "east": "tall", "landing": "tall"}
    CL = 12                                            # the clinic floor: 3 units over the courtyard
    # --- the Clinic
    g.open(4, 4, 13, 14, "ward", CL, "ward")                           # Ward A: the start
    g.open(13, 8, 14, 9, "wood", CL, "ward")                           # its door
    g.open(14, 8, 22, 11, "carpet", CL, "hall")                        # the hall, with a red runner
    for i, h in enumerate((10, 8, 6, 4, 2)):                           # the stair down: five treads of two tiles, 0.5 units each
        g.open(22 + 2 * i, 8, 24 + 2 * i, 11, "stairs", h, "hall")
    g.open(32, 8, 33, 11, "stairs", 0, "hall")
    g.open(15, 3, 22, 7, "ward", CL, "ward")                           # Ward B (beds)
    g.open(19, 7, 20, 8, "wood", CL, "ward")                           # its door
    g.open(14, 12, 22, 22, "ward", CL, "ward")                         # Ward C (the operating room), a locked door
    g.open(20, 11, 21, 12, "wood", CL, "ward")
    g.open(33, 4, 42, 16, "ward", 0, "atrium")                         # the grand atrium at ground level
    g.open(42, 9, 43, 11, "stairs", 0, "atrium")                       # the double door to the courtyard
    # --- the courtyard
    g.open(43, 8, 59, 20, "flags", 0, "yard")
    g.open(47, 4, 56, 8, "flags", 0, "yard")
    g.open(47, 20, 56, 30, "flags", 0, "yard")
    g.open(46, 30, 49, 45, "road", 0, "alley")                         # the alley to the south (Main Street, later)
    for z in range(31, 44, 4):
        g.open(47, z, 48, z + 1, "gutter", 0, "alley")                 # a drain down the alley
    g.open(59, 9, 64, 12, "flags", 0, "east")                          # the way to the gate
    for i, h in enumerate((2, 4, 6, 8)):                               # a stair up to the Courtyard Gate: four treads
        g.open(64 + 2 * i, 9, 66 + 2 * i, 12, "stairs", h, "east")
    g.open(72, 8, 76, 13, "flags", 8, "landing")                       # beyond the gate: a landing, a dead end for now
    g.solid(51, 11, 54, 14, "stucco", "yard")                          # a house in the middle of the plaza
    g.solid(44, 13, 47, 16, "plaster", "yard")                         # a low ruin by the atrium door
    g.solid(52, 17, 55, 19, "facade", "yard")                          # a second shed
    g.solid(56, 15, 59, 19, "facade", "yard")                          # a shed
    g.shell()
    # --- objects: the player, beds, tables, cabinets
    g.put("player_start", 8, 11)
    for x in (5, 8, 11):
        g.put("bed", x, 4)
    g.put("bed_ns", 4, 8)
    g.put("cabinet", 11, 13)
    g.put("lantern", 5, 12)
    g.put("door_ns", 13, 8)
    g.put("lamp", 14, 9)
    for x in (16, 19):
        g.put("bed", x, 3)
    g.put("bed", 16, 6)
    g.put("door_ew", 19, 7)
    g.put("enemy", 20, 4)
    g.put("door_ew_l", 20, 11)
    for x, z in ((17, 15), (20, 15), (17, 19), (20, 19)):
        g.put("table", x, z)
    g.put("cabinet", 15, 13)
    g.put("cabinet", 17, 13)
    g.put("barrel", 21, 21)
    g.put("enemy", 19, 17)
    g.put("enemy", 16, 20)
    g.put("enemy", 17, 9)
    g.put("enemy", 27, 10)
    for x, z in ((36, 6), (36, 13), (39, 6), (39, 13)):
        g.put("pillar", x, z)
    g.put("lantern", 34, 5)
    g.put("bench_ns", 34, 9)
    g.put("bench_ns", 34, 10)
    g.put("enemy", 38, 9)
    g.put("enemy", 37, 12)
    g.put("door_ns", 42, 9)
    g.put("door_ns", 42, 10)
    for x, z in ((44, 9), (44, 18), (58, 9), (49, 22)):
        g.put("lamp", x, z)
    for x, z in ((45, 8), (46, 8), (44, 19), (55, 5), (48, 5), (48, 28), (54, 28)):
        g.put("crate" if (x + z) % 2 else "barrel", x, z)
    g.put("cart", 49, 17)
    g.put("bench", 48, 11)
    g.put("bench", 57, 19)
    for x, z in ((46, 10), (49, 15), (57, 10), (52, 22), (50, 26), (52, 6), (57, 17)):
        try:
            g.put("enemy", x, z)
        except AssertionError:
            pass
    g.put("enemy", 62, 10)
    g.put("enemy", 68, 10)
    g.put("lantern", 60, 11)
    g.put("gate_ns", 72, 10)
    g.put("enemy", 47, 36)
    g.put("enemy", 47, 40)
    g.put("barricade", 47, 44)
    return g


def layers_of(g):
    def cells(grid, replicate=True):
        out = [0] * (CW * CH)
        for z in range(TH):
            for x in range(TW):
                v = grid[z][x]
                if not v:
                    continue
                for dz in (0, 1):
                    for dx in (0, 1):
                        if replicate or (dx == 0 and dz == 0):
                            out[(z * 2 + dz) * CW + x * 2 + dx] = v
        return out
    floor = [[TILE_ID(v) if v else 0 for v in row] for row in g.fl]
    return {"floor": cells(floor), "height": cells(g.ht), "walls": cells(g.wl), "objects": cells(g.ob, replicate=False)}


def TILE_ID(k):
    return k * 2                                      # the cell of the tile's top left corner in row 0


def tile_flags():
    out = {}
    for name, flag in (("gutter", 8),):
        k = TILE[name]
        for dy in (0, 1):
            for dx in (0, 1):
                out[(k * 2 + dx, dy)] = flag
    return out


# ---------------------------------------------------------------- the file

def cover_rgba(sh, g, size=128):
    """a top view of the map: floors by their average colour, walls darker with a light top, objects as dots"""
    avg = {k: tile_average(sh, k) for k in TILE.values()}
    px = bytearray(size * size * 4)
    for i in range(size * size):
        px[i * 4:i * 4 + 4] = bytes((20, 22, 30, 255))
    sc = min((size - 4) // TW, (size - 4) // TH)
    ox, oz = (size - TW * sc) // 2, (size - TH * sc) // 2
    for z in range(TH):
        for x in range(TW):
            if g.wl[z][x]:
                col = tint(avg[g.wl[z][x] // 2], 1.25)
            elif g.fl[z][x]:
                col = tint(avg[g.fl[z][x]], 0.9 + g.ht[z][x] * 0.012)
            else:
                continue
            ob = g.ob[z][x]
            if ob == ENEMY:
                col = 0xE04040
            elif ob == IDS["lantern"]:
                col = 0xFFA030
            for j in range(sc):
                for i in range(sc):
                    o = ((oz + z * sc + j) * size + ox + x * sc + i) * 4
                    px[o:o + 4] = bytes((col >> 16 & 255, col >> 8 & 255, col & 255, 255))
    return bytes(px)


def build_kit():
    sh = paint_sheet()
    g = build_map()
    looks = ("texture", "flat")
    models = []
    for look in looks:
        models += make_models(look)
    f = bmres.File(bmres.KIT, "Clinic", "bm")
    f.sections.append([bmres.SEC_MESH, bmmesh.encode(models, inset=0.25)])
    bmres.sheet_set(f, SW, SH, bytes(sh.px), seed=())
    lay = layers_of(g)
    bmres.map_put(f, CW, CH, [(n, lay[n]) for n in K.LAYER_NAMES])
    bmres.flags_put(f, tile_flags(), SW)
    info = {"file": [("name", "Dark Night: Clinic"), ("author", "bm"), ("license", "LicenseRef-BM-Community-1.0"),
                     ("version", "1"), ("tags", "map, 3d, kit, clinic"), ("looks", "texture, flat"),
                     ("desc", "Dark Night map kit: the Clinic, a hall, an atrium, a courtyard, an alley and the gate")],
            "items": []}
    for name, _fn, cls, radius in OBJECTS:
        keys = [("id", str(IDS[name])), ("model", name), ("model_flat", name + K.FLAT_SUFFIX), ("mode", "3d"),
                ("scale", "1"), ("class", K.CLASS_NAMES[cls]), ("radius", f"{radius:g}")]
        bmres.info_item(info, "object", name, True)[2].extend(keys)
    bmres.info_item(info, "object", "enemy", True)[2].extend([("id", str(ENEMY)), ("mode", "none")])
    bmres.info_item(info, "object", "player_start", True)[2].extend([("id", str(PLAYER_START)), ("mode", "none")])
    flat = ", ".join(f"{n}={tile_average(sh, k):06X}" for n, k in TILE.items())
    walls = ", ".join(f"{n}={WALL_OF[n]}" for n in ("clinicwall", "plaster", "stucco", "facade", "tall", "fence", "wood", "cobble"))
    bmres.info_item(info, "map", NAME, True)[2].extend([
        ("layers", ",".join(K.LAYER_NAMES)), ("tile", "16x16 pixels = 2x2 cells"), ("size", f"{TW}x{TH} tiles"),
        ("floor tiles", ", ".join(f"{n}={2 * k}" for n, k in TILE.items())),
        ("wall values", walls + " (the value is the height x4 and the sheet cell of the texture)"),
        ("height step", "0.25 world units (map3d_build step)"), ("ground", "height 0; the clinic floor is height 12"),
        ("screen", "640x360 (shown at 1280x720 by 2x scaling)"), ("flat floor colours", flat)])
    bmres.info_store(f, info)
    f.sections.insert(0, [bmres.SEC_COVER, struct.pack("<HH", 128, 128) + cover_rgba(sh, g)])
    bmres.check(f, NAME + ".bmk")
    return f, sh, g


def top_png(sh, g, path, S=10):
    avg = {k: tile_average(sh, k) for k in TILE.values()}
    n_w, n_h = TW * S, TH * S
    rows = []
    for z in range(TH):
        line = [bytearray() for _ in range(S)]
        for x in range(TW):
            if g.wl[z][x]:
                col, wall = tint(avg[g.wl[z][x] // 2], 1.2), True
            elif g.fl[z][x]:
                col, wall = tint(avg[g.fl[z][x]], 0.85 + 0.015 * g.ht[z][x]), False
            else:
                col, wall = 0x14161E, False
            ob = g.ob[z][x]
            for j in range(S):
                for i in range(S):
                    c = col
                    if ob and (i - S / 2) ** 2 + (j - S / 2) ** 2 <= (S * 0.32) ** 2:
                        c = 0xE04040 if ob == ENEMY else 0xFFA030 if ob == IDS["lantern"] else 0x44D0F0 if ob == PLAYER_START else 0xF0F0F0
                    line[j] += bytes((c >> 16 & 255, c >> 8 & 255, c & 255))
        rows += [b"\0" + bytes(l) for l in line]
    raw = b"".join(rows)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", n_w, n_h, 8, 2, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--png", help="a top view of the layout (floors, walls, objects), only to look at it")
    a = ap.parse_args()
    f, sh, g = build_kit()
    bmres.write(a.out, f)
    print(f"{a.out}: {os.path.getsize(a.out)} bytes")
    if a.png:
        top_png(sh, g, a.png)
    lines, problems = K.describe(f)
    print("\n".join(lines))
    for p in problems:
        print("PROBLEM:", p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
