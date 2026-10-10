#!/usr/bin/env python3
"""
pack.py - the Meshy models of Overbit's heroes into this folder, from the
files the workflows put on the branch meshy-out (out/): for each hero's
model NAME, its source SRC (SOURCES: the image-to-3D figures made from
pictures, or NAME itself: a text-to-3D figure of our descriptions),
SRC_1200.bm (1200 triangles) and SRC_450.bm (450, for the low levels of
detail; each reduced with the console's reducer, scripts/bmdecimate.py,
from the larger one when it is missing) become

  NAME.mesh   a MESH section body (scripts/bmmesh.py) with the two models,
              NAME and NAME_lo, their texture corners in the 256x256 picture
  NAME.png    the texture

which art/meshyrig.py puts on the heroes' skeletons. A NAME.rig left from
another mesh would label the wrong vertices: packing a model removes it.
With --rig, SRC_rig.glb (Meshy's auto-rigging, tools/meshy_rig.py) becomes

  NAME.rig    JSON: the rig's joints (name, position) and, for each vertex
              of the rigged mesh, its position and its heaviest joint
              (positions in tenths of a millimetre, glTF's axes)

  pack.py DIR [NAME[=SRC]...]
  pack.py --rig DIR [NAME[=SRC]...]
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "..", "scripts"))
import bmmesh  # noqa: E402
import mkbm  # noqa: E402

NAMES = ["sarge", "frost", "fuse", "rail", "orbit", "akari", "rally_mech", "rally_pilot", "kaiju_mech", "kaiju_pilot"]

# the figure of each hero: Meshy image-to-3D models made from pictures
# (.github/workflows/meshy.yml, on meshy-out); the names missing here are
# the models of the same name (the pilots: image-to-3D from ref/, through
# request.txt and .github/workflows/meshy-overbit.yml)
SOURCES = {"sarge": "soldier", "frost": "mei", "fuse": "junkrat", "rail": "sojourn", "orbit": "juno",
           "akari": "kiriko", "rally_mech": "dva", "kaiju_mech": "beast"}


def pairs(args):
    """[(NAME, SRC)] from NAME or NAME=SRC (default: every hero)"""
    out = []
    for a in args or NAMES:
        name, _, src = a.partition("=")
        out.append((name, src or SOURCES.get(name, name)))
    return out


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


def reduced(model, faces):
    """the model with `faces` triangles at most (the console's reducer)"""
    if len(model["faces"]) <= faces:
        return model
    import bmdecimate
    return bmdecimate.reduce_model(model, faces)[0]


def read_lods(src, name):
    """(1200 triangles, 450, sheet) of the source model `name` in `src`"""
    p1200, p450, pfull = (os.path.join(src, f"{name}{s}.bm") for s in ("_1200", "_450", ""))
    hi, sheet = read(p1200 if os.path.exists(p1200) else pfull)
    hi = reduced(hi, 1200)
    lo = read(p450)[0] if os.path.exists(p450) else hi
    return hi, reduced(lo, 450), sheet


def rig(src, todo):
    sys.path.insert(0, os.path.join(HERE, ".."))
    import meshyrig
    for name, s in todo:
        path = os.path.join(src, f"{s}_rig.glb")
        pos, owner, joints = meshyrig.read_rig_glb(path)
        q = lambda p: [round(c * 10000) for c in p]      # noqa: E731
        out = {"joints": [[n, q(p)] for n, p in joints], "verts": [q(p) + [o] for p, o in zip(pos, owner)]}
        with open(os.path.join(HERE, f"{name}.rig"), "w") as f:
            json.dump(out, f, separators=(",", ":"))
        print(f"{name} ({s}): {len(joints)} joints, {len(pos)} vertices")


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    if sys.argv[1] == "--rig":
        return rig(sys.argv[2], pairs(sys.argv[3:]))
    src = sys.argv[1]
    from PIL import Image
    for name, s in pairs(sys.argv[2:]):
        hi, lo, sheet = read_lods(src, s)
        hi, lo = dict(hi, name=name), dict(lo, name=name + "_lo")
        with open(os.path.join(HERE, f"{name}.mesh"), "wb") as f:
            f.write(bmmesh.encode([hi, lo], 0))
        w, h, rgba = sheet
        Image.frombytes("RGBA", (w, h), bytes(rgba)).convert("RGB").save(os.path.join(HERE, f"{name}.png"))
        stale = os.path.join(HERE, f"{name}.rig")
        if os.path.exists(stale):
            os.remove(stale)
            print(f"{name}: {name}.rig removed (another mesh; pack.py --rig if Meshy rigs this one)")
        print(f"{name} ({s}): {len(hi['faces'])} + {len(lo['faces'])} triangles, texture {w}x{h}")


if __name__ == "__main__":
    main()
