#!/usr/bin/env python3
"""
The 3D models of Overbit (every hero's meshes, skeletons and animations,
the map) as a .bm holding only the MESH and ANIM sections and the sheet
(the pictures of the map, and the textures of the heroes' Meshy bodies):
the game's build packs it with mkbm.py --models, and bm Studio, bm
Animator and bm Mesh can open it.

  models.py OUT.bm [--lua viewer.lua] [--stats] [--map MAP.lua] [--classic]

The heroes' third-person models are the Meshy figures of art/meshy on the
heroes' skeletons (meshyrig.py); --classic: the bodies made of primitives
(heroes/*.py) instead, the sheet with the map only (SHEET8).
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "heroes"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "scripts"))

import bmmesh  # noqa: E402
import meshyrig  # noqa: E402
import mkbm  # noqa: E402
import rig  # noqa: E402

HEROES = ["rally", "kaiju", "sarge", "frost", "fuse", "rail", "orbit", "akari"]


def collect(meshy=True):
    """[(Mesh, Skeleton, clips)], the names of the Meshy figures used"""
    models, used = [], []
    for name in HEROES:
        mod = __import__(name)
        built = mod.build()
        if meshy:
            built, u = meshyrig.apply(mod, built)
            used += u
        models += built
    return models, used


def build_map(lua_out, pvs_tool=None):
    """the map (partenope.py): its lit models, and its data for the Lua code
    (with what can be seen from where, if the tool is given)"""
    import mapbake
    import partenope
    import maptex
    mp = partenope.build()
    models, lua = mapbake.build(mp, partenope.LIGHT, chunk=8.0, max_edge=12.0, verbose=True, pvs_tool=pvs_tool)
    with open(lua_out, "w") as f:
        f.write(lua)
    return [m for m, _ in models], maptex.sheet(mp.atlas)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--lua", help="code of the cartridge (default: none, a models-only file)")
    ap.add_argument("--title", default="Overbit models")
    ap.add_argument("--res", default="320x180")
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--define", action="append", default=[], help="NAME=lua value, set before the code")
    ap.add_argument("--map", help="also the map: its data for the Lua code goes to this file")
    ap.add_argument("--pvs", help="the tool that finds what can be seen from where (build/host/mappvs)")
    ap.add_argument("--classic", action="store_true", help="the heroes' bodies made of primitives, not Meshy's")
    a = ap.parse_args()
    built, used = collect(meshy=not a.classic)
    extra, sheet = build_map(a.map, a.pvs) if a.map else ([], None)
    if used:
        # the Meshy textures next to the map's atlas: a sheet of 24 bits (too
        # many colours for SHEET8)
        sheet = meshyrig.sheet_with(sheet, used)
    mesh = bmmesh.encode([m.model() for m, _, _ in built] + extra, inset=0)
    anim = rig.encode([(m.name, sk, m.vbone, clips) for m, sk, clips in built])
    lua = open(a.lua, "rb").read() if a.lua else b"-- models only\n"
    lua = "".join(f"{d.split('=', 1)[0]} = {d.split('=', 1)[1]}\n" for d in a.define).encode() + lua
    # the map's pictures are the sprite sheet (SHEET8: few colours; 24 bits
    # with the Meshy textures)
    data = mkbm.pack(lua, sheet, title=a.title, author="bm", res=tuple(int(v) for v in a.res.split("x")),
                     sheet_packed=not used, mesh=mesh, extra=[(bmmesh.SEC_ANIM, anim)])
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
