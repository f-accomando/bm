#!/usr/bin/env python3
"""
pack.py - the Meshy models of Overbit's heroes into this folder, from the
files the workflow meshy-overbit put on the branch meshy-out: for each
model NAME, NAME_1200.bm (1200 triangles) and NAME_450.bm (the same reduced
to 450 with tools/bmreduce.py of bm-core, for the low levels
of detail) become

  NAME.mesh   a MESH section body (scripts/bmmesh.py) with the two models,
              NAME and NAME_lo, their texture corners in the 256x256 picture
  NAME.png    the texture

which art/meshyrig.py puts on the heroes' skeletons; with --rig, NAME_rig.glb
(Meshy's auto-rigging, tools/meshy_rig.py) becomes

  NAME.rig    JSON: the rig's joints (name, position) and, for each vertex
              of the rigged mesh, its position and its heaviest joint
              (positions in tenths of a millimetre, glTF's axes)

  pack.py DIR [NAME...]
  pack.py --rig DIR [NAME...]
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "..", "scripts"))
import bmmesh  # noqa: E402
import mkbm  # noqa: E402

NAMES = ["sarge", "frost", "fuse", "rail", "orbit", "akari", "rally_mech", "rally_pilot", "kaiju_mech", "kaiju_pilot"]


def read(path):
    data = open(path, "rb").read()
    secs = dict(bmmesh.cart_sections(data))
    models, _ = bmmesh.decode(secs[bmmesh.SEC_MESH])
    if mkbm.SEC_SHEET8 in secs:
        sheet = mkbm.sheet8_decode(secs[mkbm.SEC_SHEET8])
    else:
        body = secs[mkbm.SEC_SHEET]
        w, h = body[0] | body[1] << 8, body[2] | body[3] << 8
        sheet = (w, h, body[4:4 + w * h * 4])
    return models[0], sheet


def rig(src, names):
    sys.path.insert(0, os.path.join(HERE, ".."))
    import meshyrig
    for name in names:
        path = os.path.join(src, f"{name}_rig.glb")
        pos, owner, joints = meshyrig.read_rig_glb(path)
        q = lambda p: [round(c * 10000) for c in p]      # noqa: E731
        out = {"joints": [[n, q(p)] for n, p in joints], "verts": [q(p) + [o] for p, o in zip(pos, owner)]}
        with open(os.path.join(HERE, f"{name}.rig"), "w") as f:
            json.dump(out, f, separators=(",", ":"))
        print(f"{name}: {len(joints)} joints, {len(pos)} vertices")


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    if sys.argv[1] == "--rig":
        return rig(sys.argv[2], sys.argv[3:] or NAMES)
    src = sys.argv[1]
    from PIL import Image
    for name in sys.argv[2:] or NAMES:
        hi, sheet = read(os.path.join(src, f"{name}_1200.bm"))
        lo, _ = read(os.path.join(src, f"{name}_450.bm"))
        hi["name"], lo["name"] = name, name + "_lo"
        with open(os.path.join(HERE, f"{name}.mesh"), "wb") as f:
            f.write(bmmesh.encode([hi, lo], 0))
        w, h, rgba = sheet
        Image.frombytes("RGBA", (w, h), bytes(rgba)).convert("RGB").save(os.path.join(HERE, f"{name}.png"))
        print(f"{name}: {len(hi['faces'])} + {len(lo['faces'])} triangles, texture {w}x{h}")


if __name__ == "__main__":
    main()
