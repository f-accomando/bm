#!/usr/bin/env python3
"""Assets of carts/bench3d (the Lua edition of the 3D Bench), made at build time.

  mkassets.py OUT_DIR   writes OUT_DIR/models.bm (the models, their skeletons and the sprite sheet) and
                        OUT_DIR/map_l1.csv .. map_l8.csv (the 8 layers of the test `map8`)

It makes what mesh() cannot: models with bones (the hero of src/bm/b3d.c: 16 parts of 96 faces, one bone
a part, a clip "walk" that turns each part about its middle as b3d.c's pose()) and with light baked at
the corners (the pieces of map). The scenes of src/bm/b3d.c (make_hero, make_tile, make_slab, make_sheet)
are the model: same vertices, faces, colours and texture corners. Standard library only.

The sprite sheet (384 x 160) holds the three 128 x 128 checker sheets of b3d.c side by side (sheet 0 at
x 0, sheet 1 with the holes at x 128, sheet 2 at x 256) and below them the three 64 x 32 sheets of
herotris (at x 0, 64 and 128, y 128).
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts"))
import bmmesh  # noqa: E402
import mkbm    # noqa: E402

TEXTURED = bmmesh.TEXTURED
GLOSSY, EMISSIVE = 0x20000000, 0x40000000
SW, SH = 384, 160
# (x, y) of the six sheets: b3d.c's sheet[0..2] (128 x 128) and hsheet[0..2] (64 x 32)
SHEET_AT = [(0, 0), (128, 0), (256, 0), (0, 128), (64, 128), (128, 128)]

PART = [
    (0, 1.0, 0, 0.32), (0, 1.45, 0, 0.30), (0, 1.85, 0, 0.18), (0, 0.62, 0, 0.24),
    (-0.35, 1.5, 0, 0.12), (-0.42, 1.2, 0, 0.11), (-0.45, 0.95, 0, 0.09),
    (0.35, 1.5, 0, 0.12), (0.42, 1.2, 0, 0.11), (0.45, 0.95, 0, 0.09),
    (-0.15, 0.5, 0, 0.13), (-0.15, 0.25, 0, 0.12), (-0.15, 0.05, 0.05, 0.08),
    (0.15, 0.5, 0, 0.13), (0.15, 0.25, 0, 0.12), (0.15, 0.05, 0.05, 0.08),
]


def make_sheet():
    """the six sheets in one RGBA sheet; (cell colours of b3d.c make_sheet_wh)"""
    pairs = [(0x2060D0, 0xF0D040), (0xD04030, 0xF0F0F0), (0x30A050, 0x202020)]
    px = bytearray(SW * SH * 4)
    for idx, (ox, oy) in enumerate(SHEET_AT):
        w, h = (128, 128) if idx < 3 else (64, 32)
        k = idx % 3
        holes = idx == 1
        for y in range(h):
            for x in range(w):
                c = pairs[k][((x >> 3) ^ (y >> 3)) & 1]
                hole = holes and ((x >> 2) & 3) == 0 and ((y >> 2) & 3) == 0
                o = ((oy + y) * SW + ox + x) * 4
                px[o:o + 4] = bytes((0, 0, 0, 0)) if hole else bytes(((c >> 16) & 255, (c >> 8) & 255, c & 255, 255))
    return SW, SH, bytes(px)


def sphere(rings, segs):
    """r3d_mesh_sphere: unit vertices and faces (a, b, c) in b3d.c's order, with the ring and segment of each"""
    verts = []
    for r in range(rings + 1):
        th = math.pi * r / rings
        for s in range(segs):
            ph = 2 * math.pi * s / segs
            verts.append((math.sin(th) * math.cos(ph), math.cos(th), math.sin(th) * math.sin(ph)))
    faces = []
    for r in range(rings):
        for s in range(segs):
            a, b = r * segs + s, r * segs + (s + 1) % segs
            c, d = a + segs, b + segs
            faces.append((a, b, c, r, s))
            faces.append((b, d, c, r, s))
    return verts, faces


def hero(textured, skin=False, small=False):
    """b3d.c make_hero: 16 spheres of 96 faces, one bone a part. textured: every face from sheet 0 (the Meshy
    heroes of Overbit); small: the herotris hero, texture corners in a 64 x 32 sheet (uv_small) at (0, 128)"""
    sv, sf = sphere(6, 8)
    verts, faces, vb = [], [], []
    for b, (px, py, pz, ps) in enumerate(PART):
        base = len(verts)
        for (x, y, z) in sv:
            verts.append((px + x * ps, py + y * ps, pz + z * ps))
            vb.append(b)
        for t, (a, bb, c, r, s) in enumerate(sf):
            if textured:
                colour = TEXTURED | (EMISSIVE if b == 2 and t < 8 else 0)
                uv = []
                for v in (a, bb, c):
                    ux = (b % 4) * 32.0 + 16.0 + 15.0 * sv[v][0]
                    uy = (b // 4) * 32.0 + 16.0 - 15.0 * sv[v][1]
                    if small:
                        ux, uy = ux * 0.5, uy * 0.25 + 128
                    uv += [ux, uy]
                faces.append((base + a, base + bb, base + c, colour, tuple(uv)))
            else:
                colour = (0xC05040 if (r + s) & 1 else 0x4070C0)
                if b < 3 and t % 2:
                    colour |= GLOSSY
                if b == 2 and t < 8:
                    colour |= EMISSIVE
                faces.append((base + a, base + bb, base + c, colour, None))
    if skin:   # the upper half of each part on the next bone (a face round the middle of a part on two bones)
        for i, v in enumerate(verts):
            b = vb[i]
            vb[i] = (b + 1) % 16 if v[1] > PART[b][1] + 0.02 else b
    return verts, faces, vb


def tile(kx, kz, size, sheet, light, uv_small_to=None):
    """b3d.c make_tile / make_slab: a flat grid of kx by kz squares, textured (corners from the vertex number as
    textured()), with the light baked at the corners (bake(): base + (i * 37) % 90 over the face's 9 bytes).
    size: the side of a square; make_tile(k) has squares of 1 / k and a side of 1"""
    verts = []
    for z in range(kz + 1):
        for x in range(kx + 1):
            verts.append((x * size - kx * size / 2, 0.0, z * size - kz * size / 2))
    faces = []
    ox, oy = SHEET_AT[sheet]
    small = sheet >= 3

    def uv_of(v):
        u, w = (v * 13 % 8) * 16.0, (v * 7 % 8) * 16.0
        if small:
            u, w = u * 0.5, w * 0.25
        return (u + ox, w + oy)

    t = 0
    for z in range(kz):
        for x in range(kx):
            a = z * (kx + 1) + x
            b, c = a + 1, a + kx + 1
            d = c + 1
            for tri in ((a, c, b), (b, c, d)):
                uv = tuple(q for v in tri for q in uv_of(v))
                lt = tuple(light + ((t * 9 + i) * 37) % 90 for i in range(9))
                faces.append((*tri, TEXTURED, uv, lt))
                t += 1
    return verts, faces


def clip(nb):
    """the clip "walk": bone b turns about x by (0.35 or 0.1 for the first) * sin(3 t + b); b3d.c pose(t)"""
    length = 2 * math.pi / 3
    keys = []
    nk = 24
    for k in range(nk):
        t = length * k / nk
        pose = []
        for b in range(nb):
            a = (0.35 if b else 0.1) * math.sin(t * 3 + b)
            pose.append({"q": (math.sin(a / 2), 0.0, 0.0, math.cos(a / 2)), "t": (0.0, 0.0, 0.0)})
        keys.append({"t": t, "pose": pose})
    return {"name": "walk", "loop": True, "length": length, "mode": 0, "keys": keys}


def bones():
    return [{"name": "b%d" % b, "parent": -1, "head": (p[0], p[1], p[2]), "tail": (p[0], p[1] + 0.05, p[2])}
            for b, p in enumerate(PART)]


def hash2d(i):
    i &= 0xFFFFFFFF
    i ^= i >> 16
    i = (i * 0x7FEB352D) & 0xFFFFFFFF
    i ^= i >> 15
    i = (i * 0x846CA68B) & 0xFFFFFFFF
    i ^= i >> 16
    return i


MAP_W, MAP_H = 96, 56


def map_layers(out_dir):
    """b3d.c map8_setup: 8 layers of 96 x 56 cells; 0-2 every cell opaque tiles (sheet 0), 3-7 half the cells,
    tiles with holes (sheet 1). A tile is 1 + h % 255 as in b3d.c, a cell of its 16 x 16 sheet, in this sheet's
    48 cells a row"""
    for layer in range(8):
        rows = []
        for y in range(MAP_H):
            row = []
            for x in range(MAP_W):
                h = hash2d(layer * 100003 + y * MAP_W + x)
                if layer < 3 or (h >> 8) & 1:
                    k = 1 + h % 255
                    row.append((k // 16) * 48 + k % 16 + (0 if layer < 3 else 16))
                else:
                    row.append(0)
            rows.append(",".join(map(str, row)))
        with open(os.path.join(out_dir, "map_l%d.csv" % (layer + 1)), "w", newline="\n") as f:
            f.write("\n".join(rows) + "\n")


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    models, rigs = [], []
    for name, args in (("hero", dict(textured=False)), ("hero_tex", dict(textured=True)),
                       ("hero_skin", dict(textured=True, skin=True)),
                       ("hero_h", dict(textured=True, small=True))):
        v, f, vb = hero(**args)
        models.append({"name": name, "verts": v, "faces": f})
        rigs.append((name, bones(), vb, [clip(16)]))
    for name, (kx, kz, size, sheet, light) in (
            ("tile", (4, 4, 0.25, 0, 120)),            # make_tile(4): 32 faces, sheet 0
            ("tile_a", (5, 5, 0.2, 4, 120)),           # herotris map piece, sheet 1 of the 64 x 32
            ("tile_b", (5, 5, 0.2, 5, 100)),
            ("slab10a", (50, 5, 0.2, 4, 120)),         # make_slab(50, 5): a strip of 10 pieces
            ("slab10b", (50, 5, 0.2, 5, 100))):
        v, f = tile(kx, kz, size, sheet, light)
        models.append({"name": name, "verts": v, "faces": f, "lit": True})
    mesh = bmmesh.encode(models, 0.0)
    anim = bmmesh.encode_anim(rigs)
    data = mkbm.pack(b"-- models only\n", make_sheet(), title="bench3d models", author="bm", sheet_packed=True,
                     mesh=mesh, extra=[(bmmesh.SEC_ANIM, anim)])
    with open(os.path.join(out_dir, "models.bm"), "wb") as f:
        f.write(data)
    map_layers(out_dir)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
