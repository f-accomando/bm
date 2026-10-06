#!/usr/bin/env python3
"""
A figure with a skeleton, for the tests of the kernel's ANIM (tests/bm/
test_bm.c, QEMU test_animation): a blue body block and an orange arm block
beside its top, the bones body and arm.R; "wave" (smooth, looping, 1 s)
turns the arm up around the shoulder at 0.5 s, "still" (step, 0.5 s) holds
the rest pose.

  python3 tests/studio/anim_figure.py OUT.bm      a cartridge with it
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import bmmesh  # noqa: E402
import mkbm  # noqa: E402


def cube(o, colour, bone):
    """the six faces of a block of side 1 at o, each turned outwards (the
    side it is seen from is the one its corners go clockwise from), all its
    corners on one bone"""
    faces = []
    for axis in range(3):
        for side in (0, 1):
            u, w = (axis + 1) % 3, (axis + 2) % 3
            ps = []
            for a, c in ((0, 0), (1, 0), (1, 1), (0, 1)):
                p = list(o)
                p[axis] += side
                p[u] += a
                p[w] += c
                ps.append(p)
            e1 = [ps[1][k] - ps[0][k] for k in range(3)]
            e2 = [ps[2][k] - ps[0][k] for k in range(3)]
            n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
            if (n[axis] > 0) != (side == 1):
                ps.reverse()
            faces.append({"p": ps, "c": colour, "b": [bone] * 4})
    return faces


def sections():
    """the MESH and ANIM section bodies"""
    faces = cube((0, 0, 0), 0x3366CC, 0) + cube((1, 0.5, 0), 0xCC6633, 1)
    model, vb = bmmesh.encode_faces("figure", faces, bones=True)
    rest = [{"q": (0, 0, 0, 1), "t": (0, 0, 0)}] * 2
    up = {"q": (0, 0, math.sqrt(0.5), math.sqrt(0.5)), "t": (0, 0.25, 0)}
    bones = [{"name": "body", "parent": -1, "head": (0.5, 0, 0.5), "tail": (0.5, 1, 0.5)},
             {"name": "arm.R", "parent": 0, "head": (1, 1, 0.5), "tail": (2, 1, 0.5)}]
    clips = [{"name": "wave", "mode": 1, "loop": True, "length": 1.0,
              "keys": [{"t": 0, "pose": rest}, {"t": 0.5, "pose": [rest[0], up]}]},
             {"name": "still", "mode": 2, "loop": False, "length": 0.5, "keys": [{"t": 0, "pose": rest}]}]
    return bmmesh.encode([model], inset=0.5), bmmesh.encode_anim([("figure", bones, vb, clips)])


def cart(lua=b"function _draw() cls(0) end", title="figure"):
    mesh, anim = sections()
    return mkbm.pack(lua, title=title, mesh=mesh, extra=[(bmmesh.SEC_ANIM, anim)])


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    with open(sys.argv[1], "wb") as f:
        f.write(cart())
