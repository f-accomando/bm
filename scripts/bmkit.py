#!/usr/bin/env python3
"""
bmkit.py - map kits (.bmk): a 3D map with every model and sprite that goes with it, in one BMRES file with a
single shared sheet (docs/RISORSE.md, docs/design/map-kit.md). Standard library only, like bmres.py.

  bmkit.py island OUT.bmk [--quality hi|med|low] [--png PREVIEW.png]   builds the example kit: an island in the ocean (a benchmark map)
  bmkit.py check FILE.bmk                       reads it, checks the models (closed, looking out), the map layers,
                                                the object table and prints what is inside
  bmkit.py list FILE.bmk                        the same without the checks of the models

What a kit holds (the sections of a .bmk, no new section):
  MESH     the models (3D). The u32 flags of each model carry its map class: 2 solid, 4 floor, 8 decor.
  SHEET8   the one sheet: the floor tiles (16x16, tile 0 is "no floor"), the textures of the models, the 2D sprites.
  SPRITES  the 2D sprites: the flat version of an object (a billboard), same name as in the object table.
  MAP LAYERS FLAGS   the map as bm SDK's 3D map page reads it: layers floor, height, walls, objects (names fixed by
           map3d_build); the tile flags (0 solid, 3 water, 4 hurts: docs/api/draw.md).
  INFO     the object table: one block [object NAME] for each id of the objects layer (see OBJECT_KEYS).
The objects layer holds an even number >= 1024 on the even cell of a tile (map3d.c): the ids below 1040 are the
fixed ones of the kernel (PLAYER_START 1024, APPLE 1026, CRATE 1028, GOAL 1030, LAMP 1032, CRATE_BOUNCE 1034);
a kit uses 1040 and up.
"""
import argparse
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bmmesh  # noqa: E402
import bmres  # noqa: E402

TEX = bmmesh.TEXTURED
SOLID, FLOOR, DECOR = 2, 4, 8                       # the map class, bits 1-3 of the model flags (bm.h)
CLASS_NAMES = {0: "none", SOLID: "solid", FLOOR: "floor", DECOR: "decor"}
OBJECT_KEYS = ("id", "model", "sprite", "mode", "scale", "class", "radius")
OBJECT_MODES = ("3d", "2d")                         # what the object is at first; the game may swap (a tree 3D near, 2D far)
FIRST_KIT_ID = 1040
LAYER_NAMES = ("floor", "height", "walls", "objects")


# ---------------------------------------------------------------- solids: faces with a hint behind them

def _sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def _cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def _dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


class Solid:
    """faces of a model: polygons of 3 or 4 corners with a colour, or a texture rectangle (x, y, w, h in the
    sheet). add(hint, corners): the face looks away from the point hint, which lies behind it."""

    def __init__(self, name, flags=0):
        self.name, self.flags, self.faces = name, flags, []

    def add(self, hint, pts, colour=0x808080, tex=None, both=False):
        a, b, c = pts[0], pts[1], pts[2]
        n = _cross(_sub(b, a), _sub(c, a))
        mid = tuple(sum(p[i] for p in pts) / len(pts) for i in range(3))
        if _dot(n, _sub(mid, hint)) < 0:
            pts = pts[::-1]
        self.faces.append((list(pts), colour, tex))
        if both:                                   # a thin blade: seen from both sides
            self.faces.append((list(pts[::-1]), colour, tex))
        return self

    def model(self):
        verts, index, tris = [], {}, []
        for pts, colour, tex in self.faces:
            ids = []
            for p in pts:
                k = tuple(round(x, 5) for x in p)
                if k not in index:
                    index[k] = len(verts)
                    verts.append(k)
                ids.append(index[k])
            for t in ([(0, 1, 2), (0, 2, 3)] if len(pts) == 4 else [(0, 1, 2)]):
                a, b, c = ids[t[0]], ids[t[1]], ids[t[2]]
                if len({a, b, c}) < 3:
                    continue
                if tex:
                    x, y, w, h = tex
                    corner = [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
                    uv = tuple(v for j in t for v in corner[j])
                    tris.append((a, b, c, TEX, uv))
                else:
                    tris.append((a, b, c, colour, None))
        return {"name": self.name, "verts": verts, "faces": tris, "flags": self.flags}


def ring(cx, y, cz, rx, rz, n, phase=0.0):
    return [(cx + rx * math.cos(phase + i / n * 2 * math.pi), y, cz + rz * math.sin(phase + i / n * 2 * math.pi))
            for i in range(n)]


def cylinder(s, cx, y0, cz, r0, r1, h, n, colour, tex=None, caps=True, hint=None):
    """a (tapered) cylinder, r0 at the bottom, r1 at the top; r1 = 0 makes a cone"""
    lo, hi = ring(cx, y0, cz, r0, r0, n), ring(cx, y0 + h, cz, r1, r1, n)
    mid = hint or (cx, y0 + h / 2, cz)
    for i in range(n):
        j = (i + 1) % n
        c = colour(i) if callable(colour) else colour
        if r1 > 1e-6:
            s.add(mid, [lo[i], lo[j], hi[j], hi[i]], c, tex)
            if caps:
                s.add(mid, [(cx, y0 + h, cz), hi[i], hi[j]], c)
        else:
            s.add(mid, [lo[i], lo[j], (cx, y0 + h, cz)], c, tex)
        if caps:
            s.add(mid, [(cx, y0, cz), lo[i], lo[j]], c)


def sphere(s, cx, cy, cz, rx, ry, rz, n, rings, colour, top_only=False, jitter=None, amp=0.18):
    last = rings
    span = math.pi / 2 if top_only else math.pi
    rows = []
    for r in range(last + 1):
        a = r / last * span
        if r == 0:
            rows.append([(cx, cy + ry, cz)])
        elif r == last and not top_only:
            rows.append([(cx, cy - ry, cz)])
        else:
            rows.append([(cx + rx * math.sin(a) * math.cos(i / n * 2 * math.pi + (r % 2) * math.pi / n),
                          cy + ry * math.cos(a),
                          cz + rz * math.sin(a) * math.sin(i / n * 2 * math.pi + (r % 2) * math.pi / n))
                         for i in range(n)])
    if jitter:                                     # a rock: every point pushed in or out, by a fixed pseudo-random
        rnd = random.Random(jitter)
        rows = [[(cx + (p[0] - cx) * k, cy + (p[1] - cy) * k, cz + (p[2] - cz) * k) for p in row
                 for k in [1 + rnd.uniform(-amp, amp)]] for row in rows]
    hint = (cx, cy, cz)
    for r in range(last):
        A, B = rows[r], rows[r + 1]
        for i in range(n):
            j = (i + 1) % n
            c = colour(r, i) if callable(colour) else colour
            if len(A) == 1:
                s.add(hint, [A[0], B[i], B[j]], c)
            elif len(B) == 1:
                s.add(hint, [A[i], A[j], B[0]], c)
            else:
                s.add(hint, [A[i], A[j], B[j], B[i]], c)
    if top_only:
        base = rows[last]
        for i in range(n):
            s.add((cx, cy + 1, cz), [(cx, cy, cz), base[i], base[(i + 1) % n]], colour(last, i) if callable(colour) else colour)


def box(s, x0, y0, z0, x1, y1, z1, colour, tex=None):
    mid = ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)
    P = lambda x, y, z: (x, y, z)
    for q in ([P(x0, y0, z0), P(x1, y0, z0), P(x1, y1, z0), P(x0, y1, z0)],
              [P(x0, y0, z1), P(x1, y0, z1), P(x1, y1, z1), P(x0, y1, z1)],
              [P(x0, y0, z0), P(x0, y0, z1), P(x0, y1, z1), P(x0, y1, z0)],
              [P(x1, y0, z0), P(x1, y0, z1), P(x1, y1, z1), P(x1, y1, z0)],
              [P(x0, y0, z0), P(x1, y0, z0), P(x1, y0, z1), P(x0, y0, z1)],
              [P(x0, y1, z0), P(x1, y1, z0), P(x1, y1, z1), P(x0, y1, z1)]):
        s.add(mid, q, colour, tex)


def volume(model):
    """signed volume of a closed model (positive: the faces look out); None when the model is not closed"""
    edges = {}
    for a, b, c, _, _ in model["faces"]:
        for e in ((a, b), (b, c), (c, a)):
            edges[e] = edges.get(e, 0) + 1
    for (a, b), n in edges.items():
        if n != 1 or edges.get((b, a)) != 1:
            return None
    v, total = model["verts"], 0.0
    for a, b, c, _, _ in model["faces"]:
        total += _dot(v[a], _cross(v[b], v[c])) / 6
    return total


# ---------------------------------------------------------------- the sheet: flat colours, little textures, sprites

SW = SH = 256
TILE = 16


class Sheet:
    def __init__(self):
        self.px = bytearray(SW * SH * 4)

    def put(self, x, y, c):
        if 0 <= x < SW and 0 <= y < SH:
            o = (y * SW + x) * 4
            self.px[o:o + 4] = bytes((c >> 16 & 255, c >> 8 & 255, c & 255, 255 if c >= 0 else 0))

    def clear_px(self, x, y):
        if 0 <= x < SW and 0 <= y < SH:
            o = (y * SW + x) * 4
            self.px[o:o + 4] = bytes(4)

    def rect(self, x, y, w, h, c):
        for j in range(h):
            for i in range(w):
                self.put(x + i, y + j, c)

    def tri(self, a, b, c, col):
        xs, ys = [a[0], b[0], c[0]], [a[1], b[1], c[1]]
        for y in range(int(min(ys)), int(max(ys)) + 1):
            for x in range(int(min(xs)), int(max(xs)) + 1):
                d1 = (x - b[0]) * (a[1] - b[1]) - (a[0] - b[0]) * (y - b[1])
                d2 = (x - c[0]) * (b[1] - c[1]) - (b[0] - c[0]) * (y - c[1])
                d3 = (x - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (y - a[1])
                if not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0)):
                    self.put(x, y, col)

    def disc(self, cx, cy, r, c):
        for y in range(int(cy - r), int(cy + r) + 1):
            for x in range(int(cx - r), int(cx + r) + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    self.put(x, y, c)

    def noise(self, x, y, w, h, ramp, seed):
        """a tile of speckle: each pixel one of the colours of the ramp"""
        rnd = random.Random(seed)
        for j in range(h):
            for i in range(w):
                self.put(x + i, y + j, ramp[rnd.randrange(len(ramp))])


# the floor tiles, in the sheet's row 0 (tile 0 is "no floor" for the map). x in tiles of 16 pixels.
TILES = {"water": 1, "sand": 2, "grass": 3, "dirt": 4, "rock": 5, "shore": 6, "stones": 7}
# little textures of the models, in row 1 (y = 16): bark, leaf, rock, wood
TEXTURES = {"bark": (0, 16, 16, 16), "leaf": (16, 16, 16, 16), "stone": (32, 16, 16, 16), "wood": (48, 16, 16, 16)}
SPRITE_Y = 64


def paint_sheet(sh):
    sh.noise(16, 0, 16, 16, [0x2D6FB5, 0x2D6FB5, 0x3A80C8, 0x2860A5, 0x4C92D8], 1)
    sh.noise(32, 0, 16, 16, [0xE8D190, 0xE2C985, 0xEED89C, 0xDCC27A], 2)
    sh.noise(48, 0, 16, 16, [0x5FAE3C, 0x56A235, 0x69B845, 0x4E962F], 3)
    sh.noise(64, 0, 16, 16, [0x8A5A33, 0x7E5030, 0x956338, 0x6F4529], 4)
    sh.noise(80, 0, 16, 16, [0x8D9096, 0x80838A, 0x9A9DA3, 0x73767C], 5)
    sh.noise(96, 0, 16, 16, [0xC9B27A, 0xBFA66C, 0xD3BD88, 0x7FA6B8], 6)
    sh.noise(112, 0, 16, 16, [0x6C6F75, 0x5E6167, 0x7C7F85, 0x8D8D86], 7)
    # bark: vertical streaks
    rnd = random.Random(8)
    for i in range(16):
        c = [0x7A4C28, 0x6A3F20, 0x8A5A33][rnd.randrange(3)]
        sh.rect(0 + i, 16, 1, 16, c)
    sh.noise(16, 16, 16, 16, [0x3E8F2A, 0x4CA336, 0x358024], 9)
    sh.noise(32, 16, 16, 16, [0x8D9096, 0x80838A, 0x9A9DA3], 10)
    for j in range(4):                                    # wood: planks
        sh.rect(48, 16 + j * 4, 16, 3, [0xA06A3A, 0x94602F][j % 2])
        sh.rect(48, 19 + j * 4, 16, 1, 0x6B4423)
    # the 2D sprites (billboards): the flat version of each object
    return paint_sprites(sh)


def paint_sprites(sh):
    zones = []

    def zone(name, x, y, w, h, frames=1, fps=0):
        zones.append({"name": name, "x": x, "y": y, "w": w, "h": h, "frames": frames, "fps": fps})

    # palm 32 x 48
    x, y = 0, SPRITE_Y
    for j in range(30):
        sh.rect(x + 14 + j // 12, y + 47 - j, 3, 1, 0x8A5A33)
    for a, b in (((16, 18), (1, 10)), ((16, 18), (31, 10)), ((16, 18), (4, 26)), ((16, 18), (28, 26)),
                 ((16, 18), (16, 1)), ((16, 18), (8, 4)), ((16, 18), (24, 4))):
        sh.tri((x + a[0], y + a[1]), (x + b[0], y + b[1]), (x + (a[0] + b[0]) // 2 + 2, y + (a[1] + b[1]) // 2 - 3), 0x4CA336)
    sh.disc(x + 15, y + 19, 2, 0x6B3A1E)
    sh.disc(x + 18, y + 20, 2, 0x6B3A1E)
    zone("palm2d", x, y, 32, 48)
    # bush 16 x 16
    x = 32
    sh.disc(x + 8, y + 10, 6, 0x3E8F2A)
    sh.disc(x + 5, y + 10, 4, 0x4CA336)
    sh.disc(x + 11, y + 10, 4, 0x56A235)
    zone("bush2d", x, y, 16, 16)
    # rock 16 x 16
    x = 48
    sh.disc(x + 8, y + 10, 5, 0x80838A)
    sh.disc(x + 6, y + 8, 3, 0x9A9DA3)
    sh.rect(x + 3, y + 13, 10, 2, 0x73767C)
    zone("rock2d", x, y, 16, 16)
    # chest 16 x 16
    x = 64
    sh.rect(x + 2, y + 6, 12, 9, 0x8A5A33)
    sh.rect(x + 2, y + 3, 12, 4, 0xA06A3A)
    sh.rect(x + 2, y + 8, 12, 1, 0x4A4F5A)
    sh.rect(x + 7, y + 7, 2, 3, 0xE8C14A)
    zone("chest2d", x, y, 16, 16)
    # boat 32 x 16
    x = 80
    sh.tri((x + 1, y + 6), (x + 31, y + 6), (x + 24, y + 14), 0x8A4B2A)
    sh.tri((x + 1, y + 6), (x + 24, y + 14), (x + 8, y + 14), 0x8A4B2A)
    sh.rect(x + 4, y + 5, 24, 1, 0xB26A3C)
    zone("boat2d", x, y, 32, 16)
    # campfire 16 x 16, 4 frames one beside the other (the flame moves)
    for f in range(4):
        x = 112 + f * 16
        sh.rect(x + 2, y + 13, 12, 2, 0x6B4423)
        sh.disc(x + 4, y + 13, 2, 0x6C6F75)
        sh.disc(x + 12, y + 13, 2, 0x6C6F75)
        h = 7 + (f % 2) * 2
        sh.tri((x + 4, y + 13), (x + 12, y + 13), (x + 8, y + 13 - h - 1), 0xE8541E)
        sh.tri((x + 6, y + 13), (x + 10, y + 13), (x + 8 + (f - 1) % 3 - 1, y + 13 - h + 2), 0xF6C23C)
    zone("campfire2d", 112, y, 16, 16, 4, 8)
    # grass tuft 16 x 16
    x = 176
    for i, hh in enumerate((6, 9, 7, 10, 5)):
        sh.rect(x + 3 + i * 2, y + 15 - hh, 1, hh, 0x56A235)
    zone("grass2d", x, y, 16, 16)
    # cloud 32 x 16, for the sky (flat)
    x = 192
    for cx, cy, r in ((10, 9, 5), (17, 7, 6), (24, 10, 4)):
        sh.disc(x + cx, y + cy, r, 0xF4F8FC)
    sh.rect(x + 6, y + 11, 20, 4, 0xF4F8FC)
    zone("cloud2d", x, y, 32, 16)
    # islet 48 x 24: the island seen from far
    x, y = 0, SPRITE_Y + 48
    for j in range(24):
        for i in range(48):
            dx, dy = (i - 24) / 24, (j - 14) / 9
            if dx * dx + dy * dy <= 1 and j >= 10:
                sh.put(x + i, y + j, 0xE8D190 if dx * dx + dy * dy > 0.55 else 0x56A235)
    sh.disc(x + 24, y + 8, 3, 0x4CA336)
    sh.rect(x + 23, y + 8, 2, 6, 0x8A5A33)
    zone("islet2d", x, y, 48, 24)
    return zones


# ---------------------------------------------------------------- the models
# Quality: the models are made hi poly first and cut down after (QUALITY, --quality). side: sides of a round
# part; ring: rings of a sphere; seg: pieces of a trunk; blades: palm leaves; bseg: pieces along a leaf;
# lumps: spheres of a bush or a cloud; stones: stones round the fire; tufts: blades of a grass tuft.
QUALITIES = {
    "hi": dict(side=12, ring=6, seg=8, blades=7, bseg=4, lumps=9, stones=12, tufts=9, arcs=40, detail=True),
    "med": dict(side=8, ring=4, seg=5, blades=6, bseg=3, lumps=5, stones=8, tufts=5, arcs=28, detail=True),
    "low": dict(side=6, ring=3, seg=4, blades=5, bseg=2, lumps=3, stones=6, tufts=3, arcs=16, detail=False),
}
QUALITY = "hi"


def Q():
    return QUALITIES[QUALITY]


def limb(s, a, b, r0, r1, n, colour, tex=None, caps=True):
    """a (tapered) round bar from the point a to the point b"""
    ax = _sub(b, a)
    ln = math.sqrt(_dot(ax, ax))
    ax = tuple(x / ln for x in ax)
    ref = (1, 0, 0) if abs(ax[0]) < 0.9 else (0, 1, 0)
    u = _cross(ax, ref)
    ul = math.sqrt(_dot(u, u))
    u = tuple(x / ul for x in u)
    v = _cross(ax, u)

    def rng(c, r, i):
        t = i / n * 2 * math.pi
        return tuple(c[k] + r * (math.cos(t) * u[k] + math.sin(t) * v[k]) for k in range(3))
    lo = [rng(a, r0, i) for i in range(n)]
    hi = [rng(b, r1, i) for i in range(n)]
    mid = tuple((a[k] + b[k]) / 2 for k in range(3))
    for i in range(n):
        j = (i + 1) % n
        c = colour(i) if callable(colour) else colour
        s.add(mid, [lo[i], lo[j], hi[j], hi[i]], c, tex)
        if caps:
            s.add(mid, [a, lo[i], lo[j]], c)
            s.add(mid, [b, hi[i], hi[j]], c)


def blade(s, base, direction, length, width, bend, colour, bseg, hint, ribbed=True, droop=None):
    """a leaf or a blade of grass: a strip along direction (a unit vector in the x-z plane), arching up by
    bend then drooping, tapering to a point; two-sided; the middle raised a little (a fold) when ribbed"""
    dx, dz = direction
    px, pz = -dz, dx
    st = []
    for i in range(bseg + 1):
        t = i / bseg
        up = bend * math.sin(t * math.pi * 0.85) - (bend * 0.5 if droop is None else droop) * t * t
        w = width * (math.sin(min(1.0, t * 1.15) * math.pi * 0.5) ** 0.8) * (1 - t) ** 0.45 if t < 1 else 0
        x, y, z = base[0] + dx * length * t, base[1] + up, base[2] + dz * length * t
        rib = 0.05 * w / max(width, 1e-6) if ribbed else 0
        st.append(((x + px * w, y, z + pz * w), (x, y + rib, z), (x - px * w, y, z - pz * w)))
    for i in range(bseg):
        L0, C0, R0 = st[i]
        L1, C1, R1 = st[i + 1]
        s.add(hint, [L0, L1, C1, C0], colour(i, 0) if callable(colour) else colour, both=True)
        s.add(hint, [C0, C1, R1, R0], colour(i, 1) if callable(colour) else colour, both=True)


PALM_VARIANTS = (                                  # name, lean, height, blades fewer by, leaf length, trunk radius
    ("palm", 0.45, 3.4, 0, 1.75, 0.12),
    ("palm2", 0.2, 2.7, 1, 1.5, 0.13),
    ("palm3", -0.5, 4.0, 0, 1.9, 0.10),
)


def m_palm(variant=0):
    q = Q()
    name, lean, h, fewer, leaf, rad = PALM_VARIANTS[variant]
    s = Solid(name, SOLID)
    n, segs = max(5, round(q["side"] * 0.67)), q["seg"]
    bark = TEXTURES["bark"]
    for k in range(segs):
        t0, t1 = k / segs, (k + 1) / segs
        x0, x1 = lean * t0 * t0, lean * t1 * t1
        r0 = rad * (1 - 0.4 * t0) * (1 + 0.8 * (1 - t0) ** 6)          # the foot flares
        r1 = rad * (1 - 0.4 * t1) * (1 + 0.8 * (1 - t1) ** 6)
        lo, hi = ring(x0, h * t0, 0, r0, r0, n), ring(x1, h * t1, 0, r1, r1, n)
        for i in range(n):
            j = (i + 1) % n
            s.add((x0, h * (t0 + t1) / 2, 0), [lo[i], lo[j], hi[j], hi[i]], 0x8A5A33, bark)
    top = (lean, h, 0)
    last = ring(lean, h, 0, rad * 0.6, rad * 0.6, n)
    foot = ring(0, 0, 0, rad * 1.8, rad * 1.8, n)
    for i in range(n):
        s.add((lean, h - 0.2, 0), [top, last[i], last[(i + 1) % n]], 0x8A5A33, bark)
        s.add((0, 0.5, 0), [(0, 0, 0), foot[i], foot[(i + 1) % n]], 0x8A5A33)
    nb = max(4, q["blades"] - fewer)
    for b in range(nb):
        a = b / nb * 2 * math.pi + 0.3 + variant
        blade(s, (lean, h + 0.02, 0), (math.cos(a), math.sin(a)), leaf + 0.15 * (b % 2), 0.55, 0.45 - 0.1 * (b % 2),
              lambda i, side, b=b: (0x4CA336, 0x3E8F2A, 0x5CB83F)[(b + side) % 3], q["bseg"], (lean, h - 1, 0), droop=0.95)
    for k in range(4 if q["detail"] else 2):                              # coconuts
        a = k / 4 * 2 * math.pi
        sphere(s, lean + 0.15 * math.cos(a), h - 0.12, 0.15 * math.sin(a), 0.1, 0.1, 0.1, max(5, n // 2), max(3, q["ring"] - 2), 0x6B3A1E)
    return s


def m_bush():
    q = Q()
    s = Solid("bush", DECOR)
    rnd = random.Random(5)
    for k in range(q["lumps"]):
        a = k / q["lumps"] * 2 * math.pi
        d = 0.0 if k == 0 else 0.28 + 0.06 * rnd.random()
        r = 0.4 if k == 0 else 0.22 + 0.08 * rnd.random()
        sphere(s, d * math.cos(a), 0.08 + r * 0.7 + (0.12 if k == 0 else 0), d * math.sin(a), r, r * 0.85, r, max(5, q["side"] // 2),
               max(3, q["ring"] - 1), lambda row, i, k=k: (0x3E8F2A, 0x4CA336, 0x56A235, 0x6BAE3A)[(i + row + k) % 4], jitter=40 + k)
    return s


def m_rock():
    q = Q()
    s = Solid("rock", SOLID)
    sphere(s, 0, 0.3, 0, 0.55, 0.38, 0.45, max(5, q["side"] // 2 + 1), max(3, q["ring"] - 2),
           lambda row, i: (0x80838A, 0x8D9096, 0x73767C, 0x9A9DA3)[(i * 2 + row) % 4], jitter=7, amp=0.3)
    return s


def m_chest():
    q = Q()
    s = Solid("chest", SOLID)
    wood = TEXTURES["wood"]
    steel = 0x4A5A78
    box(s, -0.35, 0, -0.22, 0.35, 0.3, 0.22, 0x8A5A33, wood)
    n = max(4, 2 * q["ring"])

    def arc(a, x, k=1.0):
        return (x, 0.3 + 0.22 * k * math.sin(a), 0.22 * k * math.cos(a))
    for i in range(n):
        a0, a1 = i / n * math.pi, (i + 1) / n * math.pi
        s.add((0, 0.2, 0), [arc(a0, -0.35), arc(a0, 0.35), arc(a1, 0.35), arc(a1, -0.35)], 0xA06A3A, wood)
    for sx in (-0.35, 0.35):
        for i in range(n):
            a0, a1 = i / n * math.pi, (i + 1) / n * math.pi
            s.add((-sx, 0.3, 0), [(sx, 0.3, 0), arc(a0, sx), arc(a1, sx)], 0xA06A3A, wood)
    if q["detail"]:
        for bx in (-0.26, 0.26):                                           # the steel bands round the chest
            box(s, bx - 0.035, -0.005, -0.23, bx + 0.035, 0.3, 0.23, steel)
            for i in range(n):
                a0, a1 = i / n * math.pi, (i + 1) / n * math.pi
                s.add((0, 0.2, 0), [arc(a0, bx - 0.035, 1.07), arc(a0, bx + 0.035, 1.07),
                                    arc(a1, bx + 0.035, 1.07), arc(a1, bx - 0.035, 1.07)], steel)
        box(s, -0.05, 0.14, 0.215, 0.05, 0.34, 0.26, 0xE8C14A)               # the lock plate
    else:
        box(s, -0.04, 0.04, 0.22, 0.04, 0.34, 0.25, 0xE8C14A)
    return s


def m_boat():
    q = Q()
    s = Solid("boat", SOLID)
    L, W, D = 1.2, 0.62, 0.5
    stations = 9 if q["detail"] else 5
    secs = []
    for i in range(stations):
        t = i / (stations - 1) * 2 - 1                                    # -1 .. 1 from stern to bow
        k = max(0.0, 1 - abs(t) ** 2.4)
        w = W * k
        rise = D * (1 + 0.35 * max(0, abs(t) - 0.5))
        secs.append([(L * t, rise, -w), (L * t, rise * 0.4, -w * 0.85), (L * t, 0.0, -w * 0.4),
                     (L * t, 0.0, w * 0.4), (L * t, rise * 0.4, w * 0.85), (L * t, rise, w)])
    hint = (0, D * 0.5, 0)
    wood = TEXTURES["wood"]
    for a in range(stations - 1):
        for i in range(5):
            s.add(hint, [secs[a][i], secs[a + 1][i], secs[a + 1][i + 1], secs[a][i + 1]], 0x8A4B2A, wood if i == 2 else None)
    for a in range(stations - 1):                                         # the inside, a little lower, seen from above
        for i in range(5):
            qd = [(p[0], p[1] - 0.04 if p[1] > 0 else p[1] + 0.03, p[2] * 0.93) for p in
                  (secs[a][i], secs[a + 1][i], secs[a + 1][i + 1], secs[a][i + 1])]
            s.add((0, 5, 0), qd, 0xB26A3C)
    box(s, -0.06, 0.12, -0.36, 0.06, 0.18, 0.36, 0xB26A3C)               # a thwart
    if q["detail"]:
        box(s, 0.45, 0.12, -0.3, 0.57, 0.18, 0.3, 0xB26A3C)
        for z in (-1, 1):                                                  # the two oars, across the boat
            limb(s, (-0.25, 0.28, 0.1 * z), (0.15, 0.2, 0.75 * z), 0.018, 0.018, 5, 0x7A4C28)
            blade(s, (0.15, 0.2, 0.75 * z), (0.0, 1.0 * z), 0.28, 0.07, 0.0, 0xB26A3C, 1, (0.15, 0.1, 0), ribbed=False)
    return s


def spike(s, cx, y0, cz, r, h, lean, n, colour, turn=0.0):
    """a shard: an n-sided base of radius r and a point h above it, leaning by lean (x, z)"""
    base = ring(cx, y0, cz, r, r, n, turn)
    tip = (cx + lean[0], y0 + h, cz + lean[1])
    mid = (cx, y0 + h * 0.3, cz)
    for i in range(n):
        j = (i + 1) % n
        c = colour(i) if callable(colour) else colour
        s.add(mid, [base[i], base[j], tip], c)
        s.add(mid, [(cx, y0, cz), base[i], base[j]], c)


def m_campfire():
    """logs in a star, the ends meeting at the middle; a flame of jagged shards; sparks over it (a fire reads better
    with a ragged flame than with a cone); a ring of stones"""
    q = Q()
    s = Solid("campfire", SOLID)
    n = q["stones"] * 2 // 3
    for i in range(n):
        a = (i + 0.5) / n * 2 * math.pi
        sphere(s, 0.72 * math.cos(a), 0.09, 0.72 * math.sin(a), 0.14, 0.09, 0.14, 5, 2,
               lambda r, j, i=i: (0x80838A, 0x8D9096)[(i + j) % 2], jitter=70 + i, amp=0.25)
    logs = 6 if q["detail"] else 4
    for i in range(logs):                                              # lying nearly flat, the high end at the middle
        a = i / logs * 2 * math.pi + 0.3
        ca, sa = math.cos(a), math.sin(a)
        limb(s, (0.62 * ca, 0.07, 0.62 * sa), (0.1 * ca, 0.24, 0.1 * sa), 0.075, 0.065, max(5, q["side"] // 2), 0x7A4C28)
    flame = (0xE8541E, 0xF08A24, 0xF6C23C)
    shards = ((0.0, 0.0, 0.0, 0.2, 1.15, 0.0, 0.1), (0.1, 0.06, 0.13, 0.15, 0.85, 0.1, 0.0), (-0.1, 0.1, -0.1, 0.15, 0.75, -0.1, 0.05),
              (0.05, -0.12, 0.1, 0.13, 0.6, 0.1, -0.1), (-0.12, -0.05, 0.1, 0.12, 0.55, -0.1, -0.05),
              (0.14, 0.12, -0.08, 0.1, 0.45, 0.05, 0.1), (0.0, -0.2, 0.0, 0.1, 0.4, 0.0, -0.1))
    count = {"hi": 7, "med": 5, "low": 3}[QUALITY]
    for k in range(count):
        dx, _, dz, r, h, lx, lz = shards[k]
        spike(s, dx, 0.2, dz, r, h, (lx, lz), 5 if q["detail"] else 4, lambda i, k=k: flame[(i + k) % 3], turn=k)
    if q["detail"]:
        for k in range(4 if QUALITY == "hi" else 2):                   # sparks
            a = k * 1.7
            sphere(s, 0.14 * math.cos(a), 1.45 + 0.22 * k, 0.14 * math.sin(a), 0.035, 0.05, 0.035, 4, 2, 0xE8541E)
    return s


def m_grass():
    q = Q()
    s = Solid("grass", DECOR)
    rnd = random.Random(11)
    for k in range(q["tufts"]):
        a = k / q["tufts"] * 2 * math.pi + rnd.uniform(-0.3, 0.3)
        d = rnd.uniform(0.02, 0.12)
        blade(s, (d * math.cos(a), 0.0, d * math.sin(a)), (math.cos(a), math.sin(a)), rnd.uniform(0.22, 0.42), 0.035, 0.1,
              (0x56A235, 0x4CA336, 0x6BAE3A)[k % 3], max(2, q["bseg"] // 2), (0, 0, 0), ribbed=False)
    return s


def m_cloud():
    q = Q()
    s = Solid("cloud", DECOR)
    rnd = random.Random(21)
    for k in range(q["lumps"]):
        a = k / q["lumps"] * 2 * math.pi
        d = 0.0 if k == 0 else rnd.uniform(0.55, 1.0)
        r = 0.8 if k == 0 else rnd.uniform(0.35, 0.6)
        sphere(s, d * math.cos(a), rnd.uniform(-0.1, 0.25), d * math.sin(a) * 0.7, r, r * 0.7, r * 0.8, max(5, q["side"] // 2),
               max(3, q["ring"] - 1), lambda row, i, k=k: (0xF4F8FC, 0xE2EAF2, 0xD4E6F4)[(row + i + k) % 3], jitter=90 + k)
    return s


def m_islet():
    """the whole island as one model of radius 1 (a far impostor, scaled by the object table): a sand skirt with an
    irregular rim, a ring of dark soil, a dome of grass. Flat colours, no texture."""
    q = Q()
    A = q["arcs"]
    s = Solid("islet", FLOOR)
    rnd = random.Random(33)
    phase = [rnd.uniform(0, 6.28) for _ in range(3)]

    def outline(a):                                        # the rim: low waves and a little noise per arc
        return 1 + 0.16 * math.sin(3 * a + phase[0]) + 0.08 * math.sin(5 * a + phase[1]) + 0.05 * math.sin(8 * a + phase[2])
    noise = [rnd.uniform(-0.04, 0.04) for _ in range(A)]
    # (radius, height, how much of the outline it follows, colours of the quads that start at this ring)
    prof = [(0.0, 0.34, 0.0, GRASS), (0.22, 0.31, 0.2, GRASS), (0.42, 0.24, 0.5, GRASS), (0.56, 0.14, 0.7, GRASS),
            (0.62, 0.05, 0.8, SOIL), (0.66, 0.0, 0.85, SAND), (0.82, -0.01, 0.95, SAND), (1.0, -0.04, 1.0, SAND),
            (0.88, -0.14, 1.0, UNDER)]
    pts = []
    for i, (r, y, f, _) in enumerate(prof):
        row = []
        for j in range(A):
            a = j / A * 2 * math.pi
            k = 1 + (outline(a) - 1) * f + (noise[j] * f if i > 0 else 0)
            row.append((r * k * math.cos(a), y + (rnd.uniform(-0.012, 0.012) if 0 < i < 7 else 0), r * k * math.sin(a)))
        pts.append(row if r > 0 else [row[0]])
    hint = (0, -0.2, 0)
    for i in range(len(prof) - 1):
        A0, B0 = pts[i], pts[i + 1]
        cols = prof[i][3]
        for j in range(A):
            k = (j + 1) % A
            c = cols[(j + i) % len(cols)]
            if len(A0) == 1:
                s.add(hint, [A0[0], B0[j], B0[k]], c)
            else:
                s.add(hint, [A0[j], A0[k], B0[k], B0[j]], c)
    under = pts[-1]
    for j in range(A):                                       # the bottom, closed
        s.add((0, 0.5, 0), [(0, -0.14, 0), under[j], under[(j + 1) % A]], UNDER[0])
    return s


GRASS = (0x5FAE3C, 0x56A235, 0x69B845, 0x4E962F)
SOIL = (0x6F4529, 0x7E5030)
SAND = (0xE8D190, 0xDFC684, 0xEED89C)
UNDER = (0xB89A5E,)

MODELS = (m_palm, m_bush, m_rock, m_chest, m_boat, m_campfire, m_grass, m_cloud,
          lambda: m_palm(1), lambda: m_palm(2), m_islet)

# the object table: id on the objects layer, the model, the 2D sprite, the first mode, the scale, the radius of its
# collision (world units; 0: none)
OBJECTS = (
    ("palm", FIRST_KIT_ID + 0, "palm", "palm2d", "3d", 1.0, 0.25),
    ("bush", FIRST_KIT_ID + 2, "bush", "bush2d", "3d", 1.0, 0.0),
    ("rock", FIRST_KIT_ID + 4, "rock", "rock2d", "3d", 1.0, 0.45),
    ("chest", FIRST_KIT_ID + 6, "chest", "chest2d", "3d", 1.0, 0.4),
    ("boat", FIRST_KIT_ID + 8, "boat", "boat2d", "3d", 1.0, 0.8),
    ("campfire", FIRST_KIT_ID + 10, "campfire", "campfire2d", "3d", 1.0, 0.6),
    ("grass", FIRST_KIT_ID + 12, "grass", "grass2d", "3d", 1.0, 0.0),
    ("cloud", FIRST_KIT_ID + 14, "cloud", "cloud2d", "3d", 1.0, 0.0),
    ("palm2", FIRST_KIT_ID + 16, "palm2", "palm2d", "3d", 1.0, 0.25),
    ("palm3", FIRST_KIT_ID + 18, "palm3", "palm2d", "3d", 1.0, 0.25),
    ("islet", FIRST_KIT_ID + 20, "islet", "islet2d", "3d", 30.0, 0.0),      # the whole island seen from far
)
PLAYER_START = 1024


# ---------------------------------------------------------------- the map

MAP_TILES = 48                                            # 48 x 48 tiles of 16 x 16 pixels = 96 x 96 cells


def tile_id(name):
    """the id of a floor tile in the floor layer: the cell of the sheet (8 x 8) of its top left corner"""
    return TILES[name] * 2                               # row 0 of the sheet: cell (tile x * 2, 0)


def build_map():
    W = MAP_TILES * 2
    layers = {n: [0] * (W * W) for n in LAYER_NAMES}
    rnd = random.Random(33)
    c = MAP_TILES // 2
    placed = {}

    def put(layer, tx, tz, v):
        for dz in (0, 1):
            for dx in (0, 1):
                layers[layer][(tz * 2 + dz) * W + tx * 2 + dx] = v if (dx == 0 and dz == 0) or layer != "objects" else 0

    def edge(th): return 14.5 + 2.2 * math.sin(3 * th + 0.5) + 1.3 * math.sin(5 * th + 1.0)
    objects = []
    for tz in range(MAP_TILES):
        for tx in range(MAP_TILES):
            dx, dz = tx + 0.5 - c, tz + 0.5 - c
            d, th = math.hypot(dx, dz), math.atan2(dz, dx)
            R = edge(th)
            if d < R - 0.5:
                h = max(1, min(6, int((R - d) / R * 9)))
                name = "sand" if d > R * 0.78 else "grass"
                if d < 2.4:
                    name = "dirt"
                if d < 1.2:
                    name = "stones"
            elif d < R + 1.2:
                h, name = 0, "shore"
            else:
                h, name = 0, "water"
            put("floor", tx, tz, tile_id(name))
            put("height", tx, tz, h)

    def free(tx, tz):
        return layers["floor"][(tz * 2) * W + tx * 2] not in (tile_id("water"), tile_id("stones")) and (tx, tz) not in placed

    def place(oid, tx, tz):
        placed[(tx, tz)] = oid
        layers["objects"][(tz * 2) * W + tx * 2] = oid

    place(OBJECTS[5][1], c, c)                                       # the campfire, in the middle
    place(OBJECTS[3][1], c + 3, c - 1)                               # the chest, next to it
    place(PLAYER_START, c, c + 11)                                   # the player starts on the south beach
    ids = {o[0]: o[1] for o in OBJECTS}
    palms = [ids["palm"], ids["palm2"], ids["palm3"]]
    for i in range(8):                                               # palms in a loose ring
        a = i / 8 * 2 * math.pi + rnd.uniform(-0.2, 0.2)
        r = rnd.uniform(6, 9)
        tx, tz = int(c + r * math.cos(a)), int(c + r * math.sin(a))
        if free(tx, tz):
            place(palms[i % 3], tx, tz)
    for _ in range(14):
        a, r = rnd.uniform(0, 2 * math.pi), rnd.uniform(3, 11)
        tx, tz = int(c + r * math.cos(a)), int(c + r * math.sin(a))
        if free(tx, tz):
            place(ids["bush"], tx, tz)
    for _ in range(10):
        a, r = rnd.uniform(0, 2 * math.pi), rnd.uniform(9, 14)
        tx, tz = int(c + r * math.cos(a)), int(c + r * math.sin(a))
        if free(tx, tz):
            place(ids["rock"], tx, tz)
    for _ in range(16):
        a, r = rnd.uniform(0, 2 * math.pi), rnd.uniform(3, 12)
        tx, tz = int(c + r * math.cos(a)), int(c + r * math.sin(a))
        if free(tx, tz):
            place(ids["grass"], tx, tz)
    for k in range(3):                                               # clouds, high over the water (a sky layer: y by the game)
        place(ids["cloud"], 4 + k * 17, 5 + (k * 7) % 11)
    for tz in range(c + 9, c + 20):                                  # the boat, on the shore of the south beach
        tx = c + 4
        if layers["floor"][(tz * 2) * W + tx * 2] == tile_id("shore"):
            place(ids["boat"], tx, tz)
            break
    return W, layers


def tile_flags():
    """FLAGS: 0 solid, 3 water (docs/api/draw.md); for each 8 x 8 cell of a 16 x 16 tile"""
    out = {}
    for name, flag in (("water", 8), ("rock", 1), ("stones", 32)):
        t = TILES[name]
        for dy in (0, 1):
            for dx in (0, 1):
                out[(t * 2 + dx, dy)] = flag
    return out


# ---------------------------------------------------------------- the kit

def scene_model(models):
    """the island with its things, merged into one model (for the picture inside the file only; it is not in the
    kit): the islet scaled up, the objects on it. Same sheet, so the textures hold."""
    by = {m["name"]: m for m in models}
    top = ((0.0, 0.34), (0.22, 0.31), (0.42, 0.24), (0.56, 0.14), (0.62, 0.05), (0.66, 0.0), (1.0, -0.04))

    def height(r):                                           # the surface of the islet at r (of its radius 1)
        for (r0, h0), (r1, h1) in zip(top, top[1:]):
            if r <= r1:
                return h0 + (h1 - h0) * (r - r0) / (r1 - r0)
        return -0.04
    R = 6.0
    verts, faces = [], []

    def put(name, x, y, z, k=1.0, turn=0.0):
        m = by[name]
        c, s = math.cos(turn), math.sin(turn)
        base = len(verts)
        for px, py, pz in m["verts"]:
            verts.append((x + k * (px * c + pz * s), y + k * py, z + k * (-px * s + pz * c)))
        for a, b, c2, col, uv in m["faces"]:
            faces.append((a + base, b + base, c2 + base, col, uv))

    put("islet", 0, 0, 0, R)
    def at(r, a): return (r * R * math.cos(a), height(r) * R, r * R * math.sin(a))
    put("campfire", 0, height(0) * R, 0, 0.8)
    for name, r, a, k in (("palm", 0.42, 0.5, 1.0), ("palm2", 0.46, 2.3, 1.0), ("palm3", 0.4, 4.1, 1.0),
                          ("chest", 0.2, 5.6, 0.9), ("bush", 0.3, 1.4, 1.0), ("bush", 0.36, 3.3, 1.0),
                          ("bush", 0.33, 5.0, 1.0), ("rock", 0.58, 0.0, 1.1), ("rock", 0.6, 3.0, 0.9),
                          ("rock", 0.57, 4.6, 1.0), ("grass", 0.3, 0.9, 1.4), ("grass", 0.34, 2.7, 1.4)):
        x, y, z = at(r, a)
        put(name, x, y, z, k, turn=a * 1.7)
    x, y, z = at(0.93, 2.0)
    put("boat", x, -0.15, z, 1.3, turn=-2.0 + 1.57)
    return {"name": "scene", "verts": verts, "faces": faces, "flags": 0}


def build_island():
    sh = Sheet()
    zones = paint_sheet(sh)
    models = [m().model() for m in MODELS]
    f = bmres.File(bmres.KIT, "Island", "bm")
    f.sections.append([bmres.SEC_MESH, bmmesh.encode(models, inset=0.25)])
    bmres.sheet_set(f, SW, SH, bytes(sh.px), seed=())
    f.sections.append([bmres.SEC_SPRITES, bmres.sprites_encode(zones)])
    W, layers = build_map()
    bmres.map_put(f, W, W, [(n, layers[n]) for n in LAYER_NAMES])
    bmres.flags_put(f, tile_flags(), SW)
    info = {"file": [("name", "Island"), ("author", "bm"), ("license", "LicenseRef-BM-Community-1.0"), ("version", "1"),
                     ("tags", "map, 3d, island, benchmark, kit"), ("quality", QUALITY),
                     ("desc", "An island in the ocean with a campfire in the middle: 3D map, models and 2D sprites that swap with them")],
            "items": []}
    for name, oid, model, sprite, mode, scale, radius in OBJECTS:
        cls = CLASS_NAMES[next(m for m in models if m["name"] == model)["flags"] & 14]
        bmres.info_item(info, "object", name, True)[2].extend([("id", str(oid)), ("model", model), ("sprite", sprite),
                                                              ("mode", mode), ("scale", f"{scale:g}"), ("class", cls),
                                                              ("radius", f"{radius:g}")])
    bmres.info_item(info, "object", "player_start", True)[2].extend([("id", str(PLAYER_START)), ("mode", "none")])
    bmres.info_item(info, "map", "island", True)[2].extend([
        ("layers", ",".join(LAYER_NAMES)), ("tile", "16x16 pixels = 2x2 cells"), ("size", f"{MAP_TILES}x{MAP_TILES} tiles"),
        ("floor tiles", ", ".join(f"{k}={v}" for k, v in TILES.items())),
        ("height step", "0.25 world units (map3d_build step)"), ("sea level", "height 0")])
    bmres.info_store(f, info)
    bmres.check(f, "island.bmk")
    return f


# ---------------------------------------------------------------- checks and listing

def describe(f, strict=True):
    out = []
    problems = []
    inset, recs = bmres.mesh_parse(f.get(bmres.SEC_MESH))
    infos = bmres.info_of(f)
    objs = {it[1]: dict(it[2]) for it in infos["items"] if it[0] == "object"}
    names = {n for n, _ in recs}
    zones = {z["name"] for z in bmres.sprites_decode(f.get(bmres.SEC_SPRITES))} if f.get(bmres.SEC_SPRITES) else set()
    out.append(f"{f.name!r} by {f.author!r}: {len(recs)} models, {len(zones)} sprites")
    for name, rec in recs:
        nv, nf, fl = struct.unpack_from("<HHI", rec, 16)
        cls = CLASS_NAMES.get(fl & 14, "mixed")
        vol = None
        if strict:
            verts = [struct.unpack_from("<3f", rec, 24 + 12 * i) for i in range(nv)]
            faces = [struct.unpack_from("<3H", rec, 24 + 12 * nv + 24 * i) for i in range(nf)]
            vol = volume({"verts": verts, "faces": [(a, b, c, 0, None) for a, b, c in faces]})
        thin = vol is not None and abs(vol) < 1e-6          # blades seen from both sides: closed, no volume
        out.append(f"  model {name:10} {nv:4} vertices {nf:4} triangles  class {cls}" +
                   ("" if vol is None else "  two-sided" if thin else f"  volume {vol:.3f}" + ("" if vol > 0 else "  FACES LOOK IN")))
        if vol is not None and vol < 0 and not thin:
            problems.append(f"model {name}: faces look in")
        if fl & 14 and (fl & 14) not in CLASS_NAMES:
            problems.append(f"model {name}: more than one map class")
    layers = dict(bmres.layers_get(f))
    w = bmres.map_get(f)[0]
    out.append(f"  map {w}x{w} cells, layers {', '.join(layers)}")
    for n in LAYER_NAMES:
        if n not in layers:
            problems.append(f"the map has no {n} layer (bm SDK's 3D map needs it)")
    seen = set()
    for v in layers.get("objects", []):
        if v >= 1024:
            seen.add(v)
    ids = {int(o["id"]): n for n, o in objs.items() if "id" in o}
    for v in sorted(seen):
        if v not in ids and v not in (1024, 1026, 1028, 1030, 1032, 1034):
            problems.append(f"objects layer: id {v} is not in the object table (INFO)")
    for n, o in objs.items():
        if o.get("model") and o["model"] not in names:
            problems.append(f"object {n}: no model {o['model']}")
        if o.get("sprite") and o["sprite"] not in zones:
            problems.append(f"object {n}: no sprite {o['sprite']}")
        if o.get("mode") not in OBJECT_MODES + ("none", None):
            problems.append(f"object {n}: mode {o.get('mode')}")
        for k in o:
            if k not in OBJECT_KEYS:
                problems.append(f"object {n}: unknown key {k}")
    out.append(f"  objects on the map: {sum(1 for v in layers.get('objects', []) if v >= 1024)}  table: {', '.join(objs)}")
    return out, problems


def render_png(f, path):
    """a flat top view of the map (floor tiles by colour, objects as dots), only to look at the layout"""
    import zlib
    sh = bmres.sheet_get(f)
    layers = dict(bmres.layers_get(f))
    W = bmres.map_get(f)[0]
    tiles = {tile_id(n): n for n in TILES}
    pal = {"water": (45, 111, 181), "sand": (232, 209, 144), "grass": (95, 174, 60), "dirt": (138, 90, 51), "rock": (141, 144, 150),
           "shore": (201, 178, 122), "stones": (108, 111, 117)}
    S = 8
    rows = []
    for ty in range(W // 2):
        line = [[] for _ in range(S)]
        for tx in range(W // 2):
            n = tiles.get(layers["floor"][(ty * 2) * W + tx * 2], "water")
            h = layers["height"][(ty * 2) * W + tx * 2]
            col = tuple(min(255, int(c * (0.8 + 0.07 * h))) for c in pal[n])
            ob = layers["objects"][(ty * 2) * W + tx * 2]
            for j in range(S):
                for i in range(S):
                    c = col
                    if ob >= 1024 and (i - 4) ** 2 + (j - 4) ** 2 <= 6:
                        c = (255, 80, 40) if ob == 1050 else (20, 60, 20) if ob in (1040, 1056, 1058) else (250, 250, 250) if ob == 1054 else (60, 40, 30)
                    line[j] += c
        rows += [b"\0" + bytes(l) for l in line]
    raw = b"".join(rows)
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    n = W // 2 * S
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", n, n, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("island")
    a.add_argument("out")
    a.add_argument("--png")
    a.add_argument("--no-preview", action="store_true", help="no COVER and PREVIEW (the picture inside the file)")
    a.add_argument("--quality", choices=sorted(QUALITIES), default="hi", help="hi poly first, cut down after (default hi)")
    for c in ("check", "list"):
        sub.add_parser(c).add_argument("file")
    args = ap.parse_args()
    try:
        if args.cmd == "island":
            global QUALITY
            QUALITY = args.quality
            f = build_island()
            if not args.no_preview:                         # the picture inside the file (scripts/bmpreview.py)
                import bmpreview
                sheet = bmres.File(bmres.KIT, "scene", "bm", [s for s in f.sections if s[0] in bmres.SHEETS])
                scene = bmres.File(bmres.KIT, "scene", "bm", sheet.sections +
                                   [[bmres.SEC_MESH, bmmesh.encode([m().model() for m in MODELS] + [scene_model([m().model() for m in MODELS])])]])
                bmpreview.embed(f, "scene", source=scene)
            bmres.write(args.out, f)
            print(f"{args.out}: {os.path.getsize(args.out)} bytes")
            if args.png:
                render_png(f, args.png)
            lines, problems = describe(f)
        else:
            f = bmres.read(args.file)
            if f.kind != bmres.KIT:
                raise bmres.ResError(f"{args.file}: not a kit (.bmk)")
            lines, problems = describe(f, strict=args.cmd == "check")
    except bmres.ResError as e:
        print(f"error: {e}")
        return 1
    print("\n".join(lines))
    for p in problems:
        print("PROBLEM:", p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
