#!/usr/bin/env python3
"""Stand-in for chain/01-studio.bme while episode 1 is not recorded yet (record.py --start forge).

It is what bm Studio's "New project" + the models of video 1 would leave: the reference models PICO, APPLE, CRATE
(flat colours, from video/3d/assets) on a 256x256 sheet that carries the starter tiles of bm Studio (rows 0-47,
copied from build/studio.bm, as Studio's new_project does). Built with scripts/bmres.py.

  python3 video/3d/02-pixel/standin.py [OUT.bme]       default: video/3d/out/standin/TROPIC.BME
Needs video/3d/out/assets/tropic_assets.bm (python3 video/3d/assets/build.py). The skeleton/clips of the assets are
dropped (video 1 does not make them); only the MESH is kept.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmres  # noqa: E402

ASSETS = os.path.join(ROOT, "video", "3d", "out", "assets", "tropic_assets.bm")
STUDIO = os.path.join(ROOT, "build", "studio.bm")


def build(out):
    if not os.path.exists(ASSETS):
        subprocess.check_call([sys.executable, os.path.join(ROOT, "video", "3d", "assets", "build.py")])
    f = bmres.read(ASSETS)
    f.sections = [s for s in f.sections if s[0] in (bmres.SEC_MESH, bmres.SEC_LUA, bmres.SEC_INFO)]
    f.name = "Tropic Trail"
    # the order video 1 builds them in (script.md): CRATE, APPLE, PICO ("model 3/3: PICO")
    inset, recs = bmres.mesh_parse(f.get(bmres.SEC_MESH))
    by = dict(recs)
    f.sections = [[t, bmres.mesh_build(inset, [(n, by[n]) for n in ("CRATE", "APPLE", "PICO")]) if t == bmres.SEC_MESH else b]
                  for t, b in f.sections]
    sw, sh, srgba = bmres.sheet_get(bmres.read(STUDIO))
    W = H = 256
    rgba = bmres.blank(W, H)
    bmres.blit(srgba, sw, 0, 0, rgba, W, 0, 0, min(sw, W), min(sh, 48))
    bmres.sheet_set(f, W, H, rgba, seed=())
    os.makedirs(os.path.dirname(out), exist_ok=True)
    bmres.write(out, f)
    return out


if __name__ == "__main__":
    o = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "video", "3d", "out", "standin", "TROPIC.BME")
    print("stand-in:", build(o))
