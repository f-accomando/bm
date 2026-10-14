"""Crash Bandicoot, classic PS1 look: ONE continuous flat-shaded skin, rigged, ~800 triangles.

No boxes and no loose pieces. The body, neck and head are a single loft of rings; ears, hair spikes,
shoulders, arms, hands, fingers, legs and shoes grow out of vertices they SHARE with the part they hang on,
so nothing is detached and a bent joint stretches the skin instead of opening a gap.

Skinning is the format's own: one bone per vertex, no weights (src/bm/format.c:266-269). A ring shared by two
segments belongs to the distal bone, so the bend happens in the faces of the proximal segment. The vertex
bones are written here (nearest_bone_part would give a whole connected skin to one bone).

Conventions of bm Studio: y up, the character faces +z, .L is on +x (as bm Animator's mirror). Metres.
Run: python crash_organic.py OUT.bm   (absolute path). Then e.g. bm_forge.py anim examples/walk.json --in OUT.bm -o ...
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))

from bm_core import anim as A, export, mesh as M, skelgen  # noqa: E402

FLAT = 0x08000000
# colours read off the original colour chart (Naughty Dog 1995; values approximate)
ORANGE, ORANGE2, TAN, PALE = 0xFF3C00, 0xD42E00, 0xFF7600, 0xE8A8A0
BLUE, BLUE2, BLUE3, RED = 0x121261, 0x272791, 0x3A3AB0, 0x6B1515
WHITE, GLOVE, HAIR, NAVY = 0xF0F0F0, 0x4A0E0E, 0x9D0000, 0x0D0D29
BROW, BLACK, MOUTH = 0x291508, 0x101010, 0x601818

SKELETON = {"model": "crash", "bones": [
    {"name": "hips", "parent": None, "head": [0, 0.62, 0], "tail": [0, 0.80, 0]},
    {"name": "spine", "parent": "hips", "head": [0, 0.80, 0], "tail": [0, 1.30, 0]},
    {"name": "head", "parent": "spine", "connect": True, "length": 0.62, "direction": [0, 1, 0]},
    {"name": "arm.L", "parent": "spine", "head": [0.30, 1.19, 0], "tail": [0.45, 0.98, 0.02]},
    {"name": "forearm.L", "parent": "arm.L", "connect": True, "tail": [0.51, 0.78, 0.04]},
    {"name": "hand.L", "parent": "forearm.L", "connect": True, "tail": [0.53, 0.58, 0.06]},
    {"name": "arm.R", "parent": "spine", "head": [-0.30, 1.19, 0], "tail": [-0.45, 0.98, 0.02]},
    {"name": "forearm.R", "parent": "arm.R", "connect": True, "tail": [-0.51, 0.78, 0.04]},
    {"name": "hand.R", "parent": "forearm.R", "connect": True, "tail": [-0.53, 0.58, 0.06]},
    {"name": "leg.L", "parent": "hips", "head": [0.11, 0.62, 0], "tail": [0.115, 0.34, 0]},
    {"name": "shin.L", "parent": "leg.L", "connect": True, "tail": [0.12, 0.13, 0]},
    {"name": "foot.L", "parent": "shin.L", "connect": True, "tail": [0.12, 0.02, 0.2]},
    {"name": "leg.R", "parent": "hips", "head": [-0.11, 0.62, 0], "tail": [-0.115, 0.34, 0]},
    {"name": "shin.R", "parent": "leg.R", "connect": True, "tail": [-0.12, 0.13, 0]},
    {"name": "foot.R", "parent": "shin.R", "connect": True, "tail": [-0.12, 0.02, 0.2]},
]}


def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def mul(a, k): return (a[0] * k, a[1] * k, a[2] * k)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def unit(a):
    n = math.sqrt(dot(a, a)) or 1.0
    return (a[0] / n, a[1] / n, a[2] / n)


class Skin:
    """one shared vertex table; every vertex has a bone name; faces are (a, b, c, colour)"""

    def __init__(self):
        self.v, self.bone, self.f = [], [], []

    def vert(self, p, bone):
        self.v.append(tuple(p))
        self.bone.append(bone)
        return len(self.v) - 1

    def ring(self, pts, bone):
        return [self.vert(p, bone) for p in pts]

    def centre(self, ids):
        return mul(tuple(map(sum, zip(*[self.v[i] for i in ids]))), 1.0 / len(ids))

    def tri(self, a, b, c, col, inside):
        """a face turned so its normal (b-a)x(c-a) points away from `inside` (a point inside the part)"""
        n = cross(sub(self.v[b], self.v[a]), sub(self.v[c], self.v[a]))
        mid = mul(add(add(self.v[a], self.v[b]), self.v[c]), 1 / 3)
        if dot(n, sub(mid, inside)) < 0:
            b, c = c, b
        self.f.append((a, b, c, col))

    def quad(self, a, b, c, d, col, inside):
        self.tri(a, b, c, col, inside)
        self.tri(a, c, d, col, inside)

    def bridge(self, r0, r1, col, closed=True):
        """faces between two rings of the same size; col is a colour or a list (one per sector)"""
        n = len(r0)
        inside = mul(add(self.centre(r0), self.centre(r1)), 0.5)
        for k in range(n if closed else n - 1):
            c = col[k] if isinstance(col, (list, tuple)) else col
            j = (k + 1) % n
            self.quad(r0[k], r0[j], r1[j], r1[k], c, inside)

    def cap(self, r, col, inside=None, apex=None, bone=None):
        """close a ring with a fan to its centre (or to `apex`, a new vertex)"""
        c = self.vert(apex, bone) if apex is not None else self.vert(self.centre(r), self.bone[r[0]])
        inside = inside if inside is not None else self.centre(r + [c])
        for k in range(len(r)):
            self.tri(r[k], r[(k + 1) % len(r)], c, col, inside)
        return c

    def tent(self, base, apex, col, bone):
        """a pyramid on `base` vertices (shared with the part it grows from)"""
        t = self.vert(apex, bone)
        inside = self.centre(base + [t])
        for k in range(len(base)):
            c = col(self.v[base[k]], self.v[t]) if callable(col) else col
            self.tri(base[k], base[(k + 1) % len(base)], t, c, inside)
        return t


def ellipse(cy, rx, rz, n=12, off=0.0, cx=0.0, cz=0.0):
    return [(cx + rx * math.cos(off + 2 * math.pi * k / n), cy, cz + rz * math.sin(off + 2 * math.pi * k / n))
            for k in range(n)]


def align(skin, given, new):
    """order `new` (points) so it follows the vertex ids `given` (cyclic shift and direction, nearest)"""
    n = len(given)
    best = None
    for d in (1, -1):
        for s in range(n):
            cost = sum(sum((x - y) ** 2 for x, y in zip(skin.v[given[i]], new[(d * i + s) % n])) for i in range(n))
            if best is None or cost < best[0]:
                best = (cost, d, s)
    _, d, s = best
    return [new[(d * i + s) % n] for i in range(n)]


s = Skin()
N = 12
FRONT = (2, 3)      # sectors (30 degree steps, 3 is straight ahead: +z)
BACK = (8, 9)


def sectors(front=None, back=None, base=None, sides=None):
    out = [base] * N
    for k in FRONT:
        out[k] = front if front is not None else base
    for k in BACK:
        out[k] = back if back is not None else base
    for k, c in (sides or {}).items():
        out[k] = c
    return out


# ---- the column: pelvis, waist, chest, shoulders, neck, head (one loft, shared rings)
# (y, rx, rz, bone, ring angle offset)
COLUMN = [(0.62, 0.20, 0.13, "hips", math.radians(15)), (0.76, 0.15, 0.10, "hips", 0),
          (0.98, 0.22, 0.12, "spine", 0), (1.14, 0.29, 0.14, "spine", 0), (1.24, 0.30, 0.13, "spine", 0),
          (1.30, 0.09, 0.08, "spine", 0), (1.35, 0.085, 0.075, "head", 0),
          (1.40, 0.15, 0.14, "head", 0), (1.50, 0.22, 0.20, "head", 0), (1.62, 0.25, 0.23, "head", 0),
          (1.78, 0.21, 0.20, "head", 0), (1.88, 0.09, 0.09, "head", 0)]
R = [s.ring(ellipse(y, rx, rz, N, off), b) for y, rx, rz, b, off in COLUMN]
# the snout: pull the front vertices (3 = ahead, 2 and 4 beside it) forward; eyes bulge a little
for ring, dz in ((R[7], {2: .10, 3: .14, 4: .10}), (R[8], {2: .12, 3: .20, 4: .12}),
                 (R[9], {1: .05, 2: .06, 3: .04, 4: .06, 5: .05})):
    for k, d in dz.items():
        x, y, z = s.v[ring[k]]
        s.v[ring[k]] = (x, y, z + d)

TORSO = [BLUE, ORANGE, ORANGE, ORANGE, ORANGE]
for i in range(5):
    col = sectors(front=TAN if i >= 1 else BLUE, back=ORANGE2 if i >= 1 else BLUE, base=TORSO[i])
    s.bridge(R[i], R[i + 1], col)
s.bridge(R[5], R[6], ORANGE)
s.bridge(R[6], R[7], sectors(front=WHITE, base=ORANGE, sides={1: MOUTH, 4: MOUTH}))           # grin: teeth in front
s.bridge(R[7], R[8], sectors(front=PALE, base=ORANGE, sides={1: PALE, 4: PALE}))
s.bridge(R[8], R[9], sectors(front=PALE, base=ORANGE, sides={1: WHITE, 4: WHITE, 0: ORANGE2, 5: ORANGE2}))
s.bridge(R[9], R[10], sectors(front=ORANGE, base=ORANGE, sides={1: BROW, 4: BROW}))
s.bridge(R[10], R[11], ORANGE)
HEADTOP = s.cap(R[11], HAIR, apex=(0.0, 2.08, -0.06), bone="head")           # the tall central spike

# nose: a small navy pyramid on the snout tip vertices, pointing forward and down
tip = R[8][3]
nose_apex = add(s.v[tip], (0.0, -0.02, 0.07))
s.tent([R[8][2], R[8][3], R[8][4], R[7][3]], nose_apex, NAVY, "head")

# eyes: a black pupil, a small hexagonal pyramid on the white face (a decal growing from its vertices)
for k in (1, 4):
    ids = [R[8][k], R[8][k + 1], R[9][k + 1], R[9][k]]
    c = s.centre(ids)
    n = unit(sub(c, (0, 1.55, 0)))
    u = unit(cross(n, (0, 1, 0)))
    v = cross(n, u)
    base = [add(add(c, mul(n, 0.012)), add(mul(u, 0.05 * math.cos(a)), mul(v, 0.065 * math.sin(a))))
            for a in [2 * math.pi * i / 6 for i in range(6)]]
    ids2 = s.ring(base, "head")
    s.cap(ids2, BLACK, apex=add(c, mul(n, 0.032)), bone="head")

# ears: wide tents on the side of the skull (rings 9/10, sectors 11 and 0 for +x, 5 and 6 for -x)
for q in ([(9, 11), (9, 0), (9, 1), (10, 1), (10, 0), (10, 11)], [(9, 7), (9, 6), (9, 5), (10, 5), (10, 6), (10, 7)]):
    base = [R[r][k] for r, k in q]
    sx = 1 if s.v[base[0]][0] > 0 else -1
    s.tent(base, (sx * 0.44, 2.04, -0.04), lambda p, t: GLOVE if (p[2] > 0.0 and t[2] < p[2]) else ORANGE, "head")
# mohawk: spikes standing on the crown rings (10/11), leaning back
for k, h in ((2, 0.20), (3, 0.24), (8, 0.20), (9, 0.18)):
    base = [R[10][k], R[10][k + 1], R[11][k + 1], R[11][k]]
    c = s.centre(base)
    s.tent(base, (c[0], c[1] + h, c[2] - 0.05), HAIR, "head")

# ---- arms: the first ring is a patch of 6 torso vertices (rings 3/4, sectors 11,0,1 or 5,6,7)
for sx, bones, ks in ((1, ("arm.L", "forearm.L", "hand.L"), (11, 0, 1)), (-1, ("arm.R", "forearm.R", "hand.R"), (7, 6, 5))):
    r0 = [R[3][ks[0]], R[3][ks[1]], R[3][ks[2]], R[4][ks[2]], R[4][ks[1]], R[4][ks[0]]]
    spec = [(1.17, 0.36, 0.060, bones[0], ORANGE2), (1.04, 0.42, 0.052, bones[0], ORANGE),
            (0.94, 0.465, 0.047, bones[1], ORANGE), (0.80, 0.505, 0.043, bones[1], ORANGE),
            (0.745, 0.52, 0.075, bones[2], GLOVE), (0.655, 0.535, 0.115, bones[2], GLOVE)]
    prev = r0
    rings = []
    for y, x, r, b, col in spec:
        pts = ellipse(y, r, r, 6, 0, cx=sx * x, cz=0.04 if y < 0.9 else 0.0)
        pts = align(s, prev, pts) if prev is r0 else align(s, prev, pts)
        ids = s.ring(pts, b)
        s.bridge(prev, ids, col)
        prev = ids
        rings.append(ids)
    # hand: three fingers on the last two rings, a flat palm closing the ring
    palm, knuckle = rings[-2], rings[-1]
    s.cap(knuckle, GLOVE)
    for j in (0, 2, 4):
        base = [palm[j], palm[(j + 1) % 6], knuckle[(j + 1) % 6], knuckle[j]]
        c = s.centre(base)
        s.tent(base, (c[0] + sx * 0.01, c[1] - 0.17, c[2] + 0.02), GLOVE, bones[2])

# ---- legs: the first ring is the half of the pelvis ring on that side (an open arc closed by a chord)
for sx, bones, arc in ((1, ("leg.L", "shin.L", "foot.L"), (9, 10, 11, 0, 1, 2)), (-1, ("leg.R", "shin.R", "foot.R"), (3, 4, 5, 6, 7, 8))):
    r0 = [R[0][k] for k in arc]
    cx = sx * 0.12
    spec = [(0.52, 0.12, bones[0], BLUE2), (0.40, 0.135, bones[0], BLUE), (0.35, 0.14, bones[1], BLUE3),
            (0.33, 0.055, bones[1], BLUE3), (0.22, 0.048, bones[1], ORANGE), (0.13, 0.052, bones[2], ORANGE)]
    prev = r0
    for y, r, b, col in spec:
        pts = align(s, prev, ellipse(y, r, r, 6, 0, cx=cx))
        ids = s.ring(pts, b)
        s.bridge(prev, ids, col)
        prev = ids
    # shoe: horizontal slices, a chunky toe, white sole (faces looking down), red upper
    for y, rx, rz, cz, col in ((0.095, 0.09, 0.11, 0.06, RED), (0.05, 0.125, 0.21, 0.13, RED), (0.015, 0.13, 0.22, 0.13, WHITE)):
        ids = s.ring(align(s, prev, ellipse(y, rx, rz, 6, 0, cx=cx, cz=cz)), bones[2])
        s.bridge(prev, ids, col)
        prev = ids
    s.cap(prev, WHITE, inside=(cx, 0.1, 0.1))
# fill the slit between the two legs at the crotch (arc ends of both sides)
s.quad(R[0][9], R[0][2], R[0][3], R[0][8], BLUE, (0, 0.7, 0))

# ---- bind: one bone per vertex
rig = skelgen.skeleton(SKELETON)
names = {b.name: i for i, b in enumerate(rig.bones)}
faces = [M.Face(a, b, c, col | FLAT) for a, b, c, col in s.f]
model = M.Model("crash", [list(v) for v in s.v], faces, 0)
rig = skelgen.bind(model, rig, vbones=[names[b] for b in s.bone])
print(f"{len(s.v)} verts, {len(faces)} tris, {len(rig.bones)} bones", file=sys.stderr)

if len(sys.argv) > 1:
    export.export_models(sys.argv[1], M.Mesh([model]), anim=A.Anim([rig]), title="Crash organic")
    print("wrote", sys.argv[1])
