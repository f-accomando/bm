#!/usr/bin/env python3
"""Generator of the golden `bmmesh_lit.bm`: written by the REAL Python writers
scripts/bmmesh.py (encode, encode_anim) and scripts/mkbm.py (pack), never by
bm_core. It covers what the tool goldens do not: a lit model (flags bit 0)
with corner lights on untextured faces and a 5-6-5 light on textured faces,
material bits 24..30 in the colour, a step clip without loop, a 3-bone rig.

  python3 gen_bmmesh_lit.py OUT.bm        (run from the repo root or anywhere)
"""
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmmesh  # noqa: E402
import mkbm  # noqa: E402

T = bmmesh.TEXTURED


def models():
    quad = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (0, 2, 0), (1, 2, 0)]
    lit = {"name": "litbox", "lit": True, "verts": quad, "faces": [
        # untextured, emissive + glossy material bits, corner lights
        (0, 2, 1, 0x60FF8000, None, (128, 128, 128, 255, 64, 0, 10, 20, 30)),
        # textured, screen-door bit, one 5-6-5 light
        (0, 3, 2, T | 0x10000000, (0, 0, 8, 0, 8, 8), (200, 100, 50) * 3),
        # untextured with LOD bits (k = 2, stand-in bit 26)
        (3, 4, 5, 0x060000FF, None, (128,) * 9),
    ]}
    plain = {"name": "flat", "verts": [(0, 0, 0), (0, 0, 1), (1, 0, 0)],
             "faces": [(0, 1, 2, 0x08123456, (0.125, 0.25, 1.5, 2.0, 3.875, 4.0))]}   # FLAT bit 27
    return [lit, plain]


def rig():
    bones = [{"name": "root", "parent": -1, "head": (0, 0, 0), "tail": (0, 1, 0)},
             {"name": "mid", "parent": 0, "head": (0, 1, 0), "tail": (0, 2, 0)},
             {"name": "tip", "parent": 1, "head": (0, 2, 0), "tail": (0, 3, 0)}]
    vb = [0, 0, 1, 1, 2, 2]
    ident = {"q": (0, 0, 0, 1), "t": (0, 0, 0)}
    bend = {"q": (0, 0, 0.38268343, 0.9238795), "t": (0, 0.5, 0)}
    clips = [{"name": "bend", "mode": 2, "loop": False, "length": 0.5,
              "keys": [{"t": 0, "pose": [ident, ident, ident]}, {"t": 0.5, "pose": [ident, bend, bend]}]},
             {"name": "sway", "mode": 0, "loop": True, "length": 1.0,
              "keys": [{"t": 0.25, "pose": [bend, ident, ident]}]}]
    return [("litbox", bones, vb, clips)]


def main():
    out = sys.argv[1]
    mesh = bmmesh.encode(models(), inset=0.5)
    anim = bmmesh.encode_anim(rig())
    data = mkbm.pack(b"function _draw() cls(0) end", title="bmmesh lit", author="bm-forge", res=(320, 180),
                     mesh=mesh, extra=[(bmmesh.SEC_ANIM, anim)])
    with open(out, "wb") as f:
        f.write(data)
    print(f"{out}: {len(data)} bytes")


if __name__ == "__main__":
    main()
