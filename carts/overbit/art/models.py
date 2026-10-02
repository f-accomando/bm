#!/usr/bin/env python3
"""
The 3D models of Overbit (every hero's meshes, skeletons and animations)
as a .bm holding only the MESH and ANIM sections: the game's build packs it
with mkbm.py --models, and bm Studio, bm Animator and bm Mesh can open it.

  models.py OUT.bm [--lua viewer.lua] [--stats]
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "heroes"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "scripts"))

import bmmesh  # noqa: E402
import mkbm  # noqa: E402
import rig  # noqa: E402

HEROES = ["rally", "kaiju", "sarge", "frost", "fuse", "rail", "orbit", "akari"]


def collect():
    models = []
    for name in HEROES:
        mod = __import__(name)
        models += mod.build()
    return models


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--lua", help="code of the cartridge (default: none, a models-only file)")
    ap.add_argument("--title", default="Overbit models")
    ap.add_argument("--res", default="320x180")
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--define", action="append", default=[], help="NAME=lua value, set before the code")
    a = ap.parse_args()
    built = collect()
    mesh = bmmesh.encode([m.model() for m, _, _ in built], inset=0)
    anim = rig.encode([(m.name, sk, m.vbone, clips) for m, sk, clips in built])
    lua = open(a.lua, "rb").read() if a.lua else b"-- models only\n"
    lua = "".join(f"{d.split('=', 1)[0]} = {d.split('=', 1)[1]}\n" for d in a.define).encode() + lua
    data = mkbm.pack(lua, title=a.title, author="bm", res=tuple(int(v) for v in a.res.split("x")), mesh=mesh, extra=[(bmmesh.SEC_ANIM, anim)])
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "wb") as f:
        f.write(data)
    if a.stats:
        for m, sk, clips in built:
            print(f"{m.name:14s} {len(m.verts):5d} verts  faces by detail {[m.stats(d)[1] for d in range(4)]}"
                  f"  {len(sk.bones)} bones  {len(clips)} clips")
        print(f"{a.out}: {len(data)} bytes (MESH {len(mesh)}, ANIM {len(anim)})")


if __name__ == "__main__":
    main()
