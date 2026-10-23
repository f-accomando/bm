#!/usr/bin/env python3
"""
A cartridge for the test of the clip modes (make test-anim-ease): the figure
of tests/studio/anim_figure.py with one clip a mode (0 linear, 1 smooth,
2 step, 3 ease-in, 4 ease-out). Each clip moves the bone arm.R up by 1 unit
between t = 0 and t = 1 (no loop), so the head's y minus 1 is the eased u.
The cart's Lua (tests/anim_ease.lua) reads it with animate() and bone3d().

  python3 tests/anim_ease_cart.py OUT.bm [MODE_TO_BREAK_NOTHING]
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "studio"))
sys.path.insert(0, os.path.join(HERE, "..", "scripts"))
import anim_figure  # noqa: E402
import bmmesh  # noqa: E402
import mkbm  # noqa: E402

NAMES = ("linear", "smooth", "step", "easein", "easeout")


def cart(lua):
    faces = anim_figure.cube((0, 0, 0), 0x3366CC, 0) + anim_figure.cube((1, 0.5, 0), 0xCC6633, 1)
    model, vb = bmmesh.encode_faces("figure", faces, bones=True)
    rest = [{"q": (0, 0, 0, 1), "t": (0, 0, 0)}] * 2
    moved = [rest[0], {"q": (0, 0, 0, 1), "t": (0, 1, 0)}]
    bones = [{"name": "body", "parent": -1, "head": (0.5, 0, 0.5), "tail": (0.5, 1, 0.5)},
             {"name": "arm.R", "parent": 0, "head": (1, 1, 0.5), "tail": (2, 1, 0.5)}]
    clips = [{"name": n, "mode": m, "loop": False, "length": 1.0,
              "keys": [{"t": 0, "pose": rest}, {"t": 1.0, "pose": moved}]} for m, n in enumerate(NAMES)]
    mesh = bmmesh.encode([model], inset=0.5)
    anim = bmmesh.encode_anim([("figure", bones, vb, clips)])
    return mkbm.pack(lua, title="anim ease", mesh=mesh, extra=[(bmmesh.SEC_ANIM, anim)])


if __name__ == "__main__":
    with open(sys.argv[1], "wb") as f:
        f.write(cart(open(os.path.join(HERE, "anim_ease.lua"), "rb").read()))
