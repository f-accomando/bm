#!/usr/bin/env python3
"""The test kit: the sample cartridge of the studio, resource and kernel tests.

  tests/fixtures/mktestkit.py OUT.bm [--plain]

Made at test time from a few lines of code (no binary in the repository), with
the repository's own tools (scripts/mkbm.py, scripts/bmmesh.py):

  - a 256x256 sheet (SHEET8) with 8 textured patches and a clear background;
  - MESH: 8 models in this order: ground (288 faces), house, tree, bush, well,
    fence, crates and walker; all faces are textured except the walker's (flat colours);
  - ANIM: the skeleton of "walker" (7 bones: hips, spine, head, arm.L, arm.R,
    leg.L, leg.R; one box for each bone) and 3 clips: idle, walk, wave;
  - a stub game (LUA), the INFO section and a cover.

--plain: a game of before the INFO section (no INFO, no cover; title "Test kit").
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import bmmesh  # noqa: E402
import mkbm  # noqa: E402

TEXTURED = bmmesh.TEXTURED
SHEET = 256
PATCH = 16                       # each model has a 16x16 patch of the sheet
NAMES = ["ground", "house", "tree", "bush", "well", "fence", "crates", "walker"]

LUA = b"""-- The test kit (tests/fixtures/mktestkit.py), not a game: 8 models, one skeleton and a sheet.
function _init()
  for _, name in ipairs(models()) do model(name) end
end

function _draw()
  cls(0)
  print("Test kit (test fixture)", 8, 8, 0xFFFFFF)
end
"""

INFO = b"""name: Test Kit
desc: Test fixture with 8 models, a walker skeleton and a sheet.
author: bm
license: LicenseRef-BM-Community-1.0
version: 1.0
tags: test, 3d
resolution: 320x180
"""

# the box of bm: corners, and 12 triangles clockwise seen from outside; two triangles
# (a b c) (a c d) are one side, whose corners a b c d take the corners of a patch
_CORNERS = ((0, 0, 0), (0, 1, 0), (1, 1, 0), (1, 0, 0), (1, 0, 1), (1, 1, 1), (0, 1, 1), (0, 0, 1))
_TRIS = ((0, 1, 2), (0, 2, 3), (4, 5, 6), (4, 6, 7), (7, 6, 1), (7, 1, 0),
         (3, 2, 5), (3, 5, 4), (1, 6, 5), (1, 5, 2), (7, 0, 3), (7, 3, 4))
_QUAD = ((0, 0), (0, 1), (1, 1), (1, 0))


def patch_origin(i):
    return (i % (SHEET // PATCH)) * PATCH, (i // (SHEET // PATCH)) * PATCH


def add_box(m, lo, hi, patch, bone=None, colour=None):
    """a box of the model (verts, faces, vb); the sides show the patch, or have a flat colour"""
    px, py = patch_origin(patch)
    base = len(m["verts"])
    for c in _CORNERS:
        m["verts"].append(tuple((lo, hi)[c[k]][k] for k in range(3)))
        m["vb"].append(bone or 0)
    for n, (a, b, c) in enumerate(_TRIS):
        t0 = n - n % 2                                      # the first triangle of this side
        quad = (*_TRIS[t0], _TRIS[t0 + 1][2])               # a b c d
        corner = dict(zip(quad, _QUAD))
        uv = []
        for v in (a, b, c):
            uv += [px + corner[v][0] * PATCH, py + corner[v][1] * PATCH]
        m["faces"].append((base + a, base + b, base + c, TEXTURED if colour is None else colour,
                           tuple(uv) if colour is None else (0,) * 6))


def new_model(name):
    return {"name": name, "verts": [], "faces": [], "vb": []}


def ground(patch):
    """a grid of 12 x 12 squares, 288 triangles"""
    m = new_model("ground")
    px, py = patch_origin(patch)
    n = 12
    for j in range(n + 1):
        for i in range(n + 1):
            m["verts"].append((i - n / 2, 0.0, j - n / 2))
            m["vb"].append(0)
    for j in range(n):
        for i in range(n):
            a, b = j * (n + 1) + i, (j + 1) * (n + 1) + i
            u0, u1 = px + (i % 2) * 8, px + (i % 2) * 8 + 8
            v0, v1 = py + (j % 2) * 8, py + (j % 2) * 8 + 8
            m["faces"].append((a, b, b + 1, TEXTURED, (u0, v0, u0, v1, u1, v1)))
            m["faces"].append((a, b + 1, a + 1, TEXTURED, (u0, v0, u1, v1, u1, v0)))
    return m


def house(patch):
    m = new_model("house")
    add_box(m, (-1.0, 0.0, -1.0), (1.0, 1.4, 1.0), patch)          # the walls
    add_box(m, (-1.2, 1.4, -1.2), (1.2, 1.9, 1.2), patch)          # the roof
    add_box(m, (-0.2, 0.0, 1.0), (0.2, 0.9, 1.1), patch)           # the door
    return m


def tree(patch):
    m = new_model("tree")
    add_box(m, (-0.1, 0.0, -0.1), (0.1, 0.8, 0.1), patch)
    add_box(m, (-0.5, 0.8, -0.5), (0.5, 1.8, 0.5), patch)
    return m


def bush(patch):
    m = new_model("bush")
    add_box(m, (-0.4, 0.0, -0.4), (0.4, 0.5, 0.4), patch)
    return m


def well(patch):
    m = new_model("well")
    add_box(m, (-0.5, 0.0, -0.5), (0.5, 0.5, 0.5), patch)
    for sx in (-0.45, 0.35):
        add_box(m, (sx, 0.5, -0.05), (sx + 0.1, 1.3, 0.05), patch)
    add_box(m, (-0.55, 1.3, -0.15), (0.55, 1.4, 0.15), patch)
    return m


def fence(patch):
    m = new_model("fence")
    add_box(m, (-0.6, 0.0, -0.05), (-0.5, 0.7, 0.05), patch)
    add_box(m, (0.5, 0.0, -0.05), (0.6, 0.7, 0.05), patch)
    return m


def crates(patch):
    m = new_model("crates")
    add_box(m, (-0.4, 0.0, -0.4), (0.4, 0.8, 0.4), patch)
    add_box(m, (-0.3, 0.8, -0.3), (0.3, 1.4, 0.3), patch)
    add_box(m, (0.5, 0.0, 0.0), (1.0, 0.5, 0.5), patch)
    return m


# the skeleton: name, parent, head, tail; one box for each bone (the box is the bone's skin)
BONES = [("hips", -1, (0, 0.9, 0), (0, 1.0, 0)),
         ("spine", 0, (0, 0.95, 0), (0, 1.5, 0)),
         ("head", 1, (0, 1.5, 0), (0, 2.0, 0)),
         ("arm.L", 1, (0.33, 1.42, 0), (0.33, 0.9, 0)),
         ("arm.R", 1, (-0.33, 1.42, 0), (-0.33, 0.9, 0)),
         ("leg.L", 0, (0.13, 0.9, 0), (0.13, 0.08, 0)),
         ("leg.R", 0, (-0.13, 0.9, 0), (-0.13, 0.08, 0))]
SKIN = [((-0.15, 0.9, -0.1), (0.15, 1.0, 0.1)),
        ((-0.25, 1.0, -0.12), (0.25, 1.5, 0.12)),
        ((-0.15, 1.5, -0.15), (0.15, 1.9, 0.15)),
        ((0.25, 0.9, -0.07), (0.4, 1.42, 0.07)),
        ((-0.4, 0.9, -0.07), (-0.25, 1.42, 0.07)),
        ((0.06, 0.08, -0.08), (0.2, 0.9, 0.08)),
        ((-0.2, 0.08, -0.08), (-0.06, 0.9, 0.08))]


SKIN_COLOURS = [0x3060A0, 0x4070B0, 0xE0B090, 0xE0B090, 0xE0B090, 0x504030, 0x504030]


def walker(patch):
    m = new_model("walker")
    for b in (1, 0, 2, 3, 4, 5, 6):          # the spine first: vertex 0 is not on the root bone
        lo, hi = SKIN[b]                     # flat colours: a model with a skeleton needs no sheet
        add_box(m, lo, hi, patch, b, SKIN_COLOURS[b])
    return m


def quat(ax, ay, az):
    """a rotation of three small angles (radians) -> (x, y, z, w)"""
    cx, sx, cy, sy, cz, sz = (f(a / 2) for a in (ax, ay, az) for f in (math.cos, math.sin))
    return (sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz, cx * cy * cz + sx * sy * sz)


def pose(**bones):
    """the 7 bones of a key: bone name (with . as _) -> (rx, ry, rz) or ((rx, ry, rz), (tx, ty, tz))"""
    out = []
    for name, *_ in BONES:
        v = bones.get(name.replace(".", "_"), (0, 0, 0))
        r, t = (v, (0, 0, 0)) if not isinstance(v[0], tuple) else v
        out.append({"q": quat(*r), "t": t})
    return out


def clips():
    d = math.radians
    idle = [{"t": 0.0, "pose": pose(arm_L=(0, 0, d(-4)), arm_R=(0, 0, d(4)))},
            {"t": 1.0, "pose": pose(hips=((0, 0, 0), (0, -0.02, 0)), spine=(d(-3), 0, 0), head=(d(4), 0, 0),
                                    arm_L=(0, 0, d(-8)), arm_R=(0, 0, d(8)))}]
    walk = []
    for k, t in enumerate((0.0, 0.2, 0.4, 0.6)):
        s = (1, 0, -1, 0)[k]
        walk.append({"t": t, "pose": pose(hips=((0, 0, 0), (0, 0.05 * (k % 2), 0)), spine=(d(4) * s, 0, 0),
                                          arm_L=(d(-24) * s, 0, 0), arm_R=(d(24) * s, 0, 0),
                                          leg_L=(d(28) * s, 0, 0), leg_R=(d(-28) * s, 0, 0))})
    wave = [{"t": t, "pose": pose(head=(0, d(-10), 0), arm_R=(d(-25) * k, 0, d(-150) + d(10) * k))}
            for t, k in ((0.0, 0), (0.3, 1), (0.6, 0), (0.9, 1))]
    return [{"name": "idle", "mode": 1, "loop": True, "length": 2.0, "keys": idle},
            {"name": "walk", "mode": 1, "loop": True, "length": 0.8, "keys": walk},
            {"name": "wave", "mode": 1, "loop": True, "length": 1.2, "keys": wave}]


def paint_sheet():
    """the sheet: for each model a 16x16 patch of 4 shades of its colour in bricks, edges darker;
    the rest is clear (alpha 0). Returns (w, h, rgba)"""
    base = [(90, 150, 60), (200, 160, 110), (40, 120, 50), (70, 160, 70), (140, 140, 150),
            (130, 90, 50), (180, 130, 70), (210, 90, 80)]
    rgba = bytearray(SHEET * SHEET * 4)
    for i, (r, g, b) in enumerate(base):
        px, py = patch_origin(i)
        for y in range(PATCH):
            for x in range(PATCH):
                shade = 0.75 + 0.1 * ((x // 4 + y // 4 * 2 + (y // 4 % 2) * 1) % 4)
                if x % 8 == 0 or y % 8 == 0:
                    shade -= 0.3
                o = ((py + y) * SHEET + px + x) * 4
                rgba[o:o + 4] = bytes((int(r * shade), int(g * shade), int(b * shade), 255))
    return SHEET, SHEET, bytes(rgba)


def cover():
    w, h = 320, 180
    rgba = bytearray()
    for y in range(h):
        for x in range(w):
            rgba += bytes((40 + x * 120 // w, 90 + y * 100 // h, 160 - x * 80 // w, 255))
    return mkbm.make_cover((w, h, bytes(rgba)))


def build(plain=False):
    patch = {n: i for i, n in enumerate(NAMES)}
    models = [ground(patch["ground"]), house(patch["house"]), tree(patch["tree"]), bush(patch["bush"]),
              well(patch["well"]), fence(patch["fence"]), crates(patch["crates"]), walker(patch["walker"])]
    mesh = bmmesh.encode(models, 0.25)
    bones = [{"name": n, "parent": p, "head": h, "tail": t} for n, p, h, t in BONES]
    anim = bmmesh.encode_anim([("walker", bones, models[-1]["vb"], clips())])
    extra = [(mkbm.SEC_ANIM, anim)]
    if not plain:
        extra.insert(0, (mkbm.SEC_INFO, INFO))
    return mkbm.pack(LUA, paint_sheet(), None, "Test kit" if plain else "Test Kit", "bm", (320, 180),
                     None if plain else cover(), True, None, mesh, extra)


def main():
    args = [a for a in sys.argv[1:] if a != "--plain"]
    if len(args) != 1:
        raise SystemExit(__doc__)
    data = build("--plain" in sys.argv[1:])
    with open(args[0], "wb") as f:
        f.write(data)
    print(f"{args[0]}: {len(data)} bytes")


if __name__ == "__main__":
    main()
