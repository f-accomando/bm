"""Crash Bandicoot as an organic low-poly model (flat-shaded facets, no boxes), ~1000 triangles.

Uses the modelling kit of Overbit's heroes (carts/overbit/art/geo.py: hulls, tubes, ellipsoids, all
stdlib). Conventions of bm Studio: y up, the character faces +z, its right side is +x. Units: metres.
Writes a models cart: python crash_organic.py OUT.bm   (absolute path)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "carts", "overbit", "art"))
sys.path.insert(0, os.path.join(HERE, "..", ".."))

from geo import Mat, Mesh, ellipsoid, hull, tube  # noqa: E402
from bm_core import export, mesh as M  # noqa: E402

# colours read from the original colour chart (Naughty Dog 1995, values approximate, read off the picture)
ORANGE, ORANGE2, TAN, PALE = Mat(0xFF3C00), Mat(0xD42E00), Mat(0xFF7600), Mat(0xE8A8A0)
BLUE, BLUE2, RED, WHITE = Mat(0x121261), Mat(0x272791), Mat(0x6B1515), Mat(0xF0F0F0)
GLOVE, HAIR, NAVY, BROW = Mat(0x4A0E0E), Mat(0x9D0000), Mat(0x0D0D29), Mat(0x291508)
GREEN, BLACK, MOUTH, PINK = Mat(0x101010), Mat(0x101010), Mat(0x601818), Mat(0xAB8E91)

SEG = int(os.environ.get("CRASH_SEG", "6"))   # sides of the round parts: the triangle knob

m = Mesh("crash")


HEAD_K, HEAD_Y = 1.25, 1.54     # the head is drawn around y 1.46 and enlarged: Crash has a big head
in_head = False


def put(part, mat):
    if in_head:
        part.map(lambda p: (p[0] * HEAD_K, HEAD_Y + (p[1] - 1.46) * HEAD_K, p[2] * HEAD_K))
    m.add(part, mat)


def both(part, mat):
    """a part and its mirror on the other side (x -> -x)"""
    put(part.copy(), mat)
    put(part.copy().mirror_x(), mat)


def sym(pts):
    """points of the right half -> hull points of both halves"""
    return pts + [(-x, y, z) for x, y, z in pts]


def spike(base_y, x, z, h, w, lean=0.0):
    """a pointed tuft: a 4 sided pyramid"""
    return hull([(x - w, base_y, z - w), (x + w, base_y, z - w), (x + w, base_y, z + w), (x - w, base_y, z + w),
                 (x + lean, base_y + h, z)])


# --- head: a big faceted skull, a tapered muzzle, pointed ears, a mohawk
in_head = True
put(ellipsoid(0.23, 0.25, 0.23, SEG + 4, 6, smooth=False).move(0, 1.46, -0.02), ORANGE)
put(hull(sym([(0.12, 1.40, 0.14), (0.16, 1.55, 0.16), (0.07, 1.58, 0.42), (0.09, 1.40, 0.38), (0.13, 1.33, 0.2)])), PALE)
put(hull(sym([(0.07, 1.50, 0.36), (0.07, 1.62, 0.38), (0.04, 1.62, 0.47), (0.04, 1.47, 0.44)])), NAVY)
put(hull(sym([(0.17, 1.36, 0.22), (0.16, 1.44, 0.36), (0.12, 1.44, 0.385), (0.1, 1.39, 0.37)])), WHITE)   # grin
put(hull(sym([(0.12, 1.36, 0.22), (0.12, 1.38, 0.37), (0.06, 1.33, 0.33)])), MOUTH)
for sx in (1, -1):
    eye = ellipsoid(0.11, 0.11, 0.055, SEG + 2, 4, smooth=False).move(sx * 0.115, 1.62, 0.19)
    put(eye, WHITE)
    put(ellipsoid(0.05, 0.055, 0.03, 6, 3, smooth=False).move(sx * 0.105, 1.62, 0.235), GREEN)
    put(hull([(sx * 0.02, 1.715, 0.17), (sx * 0.2, 1.74, 0.15), (sx * 0.2, 1.78, 0.17), (sx * 0.02, 1.76, 0.2),
              (sx * 0.1, 1.74, 0.23)]), BROW)
    put(hull([(sx * 0.2, 1.58, -0.02), (sx * 0.22, 1.50, 0.06), (sx * 0.24, 1.76, 0.0), (sx * 0.3, 1.98, -0.03),
              (sx * 0.24, 1.62, 0.12)]), ORANGE)                                   # ear
    put(hull([(sx * 0.21, 1.62, 0.1), (sx * 0.215, 1.7, 0.1), (sx * 0.27, 1.84, 0.02), (sx * 0.2, 1.64, 0.02),
              (sx * 0.25, 1.68, 0.06)]), PINK)
    put(hull([(sx * 0.2, 1.4, 0.08), (sx * 0.24, 1.5, 0.12), (sx * 0.3, 1.38, 0.12), (sx * 0.2, 1.36, 0.0)]), ORANGE2)  # cheek fur
for i, (z, h) in enumerate([(0.12, 0.2), (0.06, 0.3), (0.0, 0.38), (-0.07, 0.32), (-0.13, 0.24)]):
    put(spike(1.68 + 0.02 * i, 0, z, h, 0.07, lean=-0.03), HAIR)

in_head = False

# --- torso: narrow waist, wide chest, tan belly, back darker
put(hull(sym([(0.12, 0.76, -0.10), (0.12, 0.76, 0.10), (0.30, 1.20, -0.13), (0.30, 1.20, 0.13), (0.13, 1.32, 0.0)])), ORANGE)
put(hull(sym([(0.06, 0.80, 0.105), (0.13, 1.15, 0.15), (0.06, 1.30, 0.12)])), TAN)
put(hull(sym([(0.10, 0.78, -0.11), (0.2, 1.14, -0.15), (0.09, 1.28, -0.12)])), ORANGE2)
put(hull(sym([(0.08, 1.26, -0.06), (0.09, 1.34, 0.06), (0.09, 1.34, -0.06), (0.08, 1.26, 0.06)])), ORANGE)  # neck
# shorts: waist, legs, cuffs
put(hull(sym([(0.2, 0.62, -0.14), (0.2, 0.62, 0.14), (0.15, 0.82, -0.12), (0.15, 0.82, 0.12)])), BLUE)
put(hull(sym([(0.02, 0.38, -0.13), (0.02, 0.38, 0.13), (0.25, 0.38, 0.15), (0.25, 0.38, -0.15),
              (0.02, 0.64, -0.12), (0.02, 0.64, 0.12), (0.2, 0.64, 0.13), (0.2, 0.64, -0.13)])), BLUE2)
for sx in (1, -1):
    put(hull([(sx * 0.03, 0.34, -0.12), (sx * 0.22, 0.34, -0.14), (sx * 0.22, 0.34, 0.14), (sx * 0.03, 0.34, 0.12),
              (sx * 0.03, 0.42, -0.12), (sx * 0.22, 0.42, -0.14), (sx * 0.22, 0.42, 0.14), (sx * 0.03, 0.42, 0.12)]), Mat(0x3A3AB0))
    put(tube((sx * 0.13, 0.34, 0.0), (sx * 0.13, 0.16, 0.0), 0.075, 0.06, SEG, smooth=False), ORANGE)
    put(hull([(sx * 0.07, 0.14, -0.09), (sx * 0.2, 0.14, -0.09), (sx * 0.2, 0.2, 0.09), (sx * 0.07, 0.2, 0.09),
              (sx * 0.06, 0.05, -0.12), (sx * 0.22, 0.05, -0.12), (sx * 0.22, 0.05, 0.14), (sx * 0.06, 0.05, 0.14)]), GLOVE)
    # shoe: rounded toe, big, red, white sole
    put(hull([(sx * 0.0, 0.04, -0.14), (sx * 0.26, 0.04, -0.14), (sx * 0.28, 0.04, 0.22), (sx * -0.02, 0.04, 0.22),
              (sx * 0.02, 0.17, -0.12), (sx * 0.24, 0.17, -0.12), (sx * 0.2, 0.15, 0.32), (sx * 0.06, 0.15, 0.32),
              (sx * 0.13, 0.1, 0.38)]), RED)
    put(hull([(sx * -0.01, 0.0, -0.15), (sx * 0.27, 0.0, -0.15), (sx * 0.29, 0.0, 0.24), (sx * -0.03, 0.0, 0.24),
              (sx * 0.0, 0.04, -0.14), (sx * 0.26, 0.04, -0.14), (sx * 0.28, 0.04, 0.22), (sx * -0.02, 0.04, 0.22)]), WHITE)
    put(hull([(sx * 0.09, 0.17, 0.1), (sx * 0.17, 0.17, 0.1), (sx * 0.16, 0.16, 0.26), (sx * 0.1, 0.16, 0.26),
              (sx * 0.13, 0.2, 0.18)]), WHITE)  # laces

# --- arms, long and thin, big gloves
for sx in (1, -1):
    put(tube((sx * 0.33, 1.2, 0.0), (sx * 0.43, 1.0, 0.0), 0.06, 0.05, SEG, smooth=False), ORANGE)
    put(tube((sx * 0.43, 1.0, 0.0), (sx * 0.5, 0.82, 0.04), 0.05, 0.045, SEG, smooth=False), ORANGE)
    put(tube((sx * 0.5, 0.82, 0.04), (sx * 0.52, 0.76, 0.05), 0.075, 0.08, SEG, smooth=False), GLOVE)    # cuff
    put(ellipsoid(0.11, 0.12, 0.09, SEG, 3, smooth=False).move(sx * 0.54, 0.68, 0.05), GLOVE)
    for k, (dx, dz) in enumerate([(-0.04, 0.05), (0.0, 0.07), (0.04, 0.05)]):
        put(hull([(sx * (0.51 + dx), 0.62, 0.02 + dz), (sx * (0.57 + dx), 0.62, 0.02 + dz),
                  (sx * (0.55 + dx), 0.46, 0.05 + dz), (sx * (0.53 + dx), 0.46, 0.05 + dz),
                  (sx * (0.54 + dx), 0.64, 0.09 + dz)]), GLOVE)

m.weld()
out = sys.argv[1] if len(sys.argv) > 1 else None
verts, faces = m.verts, m.faces
print(f"{len(verts)} verts, {len(faces)} tris", file=sys.stderr)
if out:
    mm = M.Model("crash", [list(v) for v in verts], [M.Face(a, b, c, col) for a, b, c, col in faces], 0)
    export.export_models(out, M.Mesh([mm]), title="Crash organic")
    print("wrote", out)
