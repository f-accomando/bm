"""armed.py: a PREVIEW cart with the hunter holding the weapons (not a deliverable).

  HUNTER_LOD=lo python3 armed.py OUT.bm [saw_c|saw_o]
  HUNTER_LOD=lo python3 armed.py --game OUT.bmm       the game's models: hunter_c and hunter_o, with the clips

The weapons are separate models; to hold one, a game draws it with the matrix of a hand bone times a fixed grip.
Here the grip is baked: weapon local (tip -z, up +y) -> turn about y by -90 deg (.L, +x) or +90 deg (.R, -x) so the tip
follows the hand direction, then move to the palm centre of the T pose. Saw in the right hand, pistol in the left.
"""
import dataclasses
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import hunter_build as B  # noqa: E402
import hunter_clips as C  # noqa: E402
from bm_core import anim as A, animgen, export, gpuskin, mesh as M  # noqa: E402

GRIP = {"saw": ("hand.R", -1), "pistol": ("hand.L", 1)}    # hand bone, side (+1 = .L on +x)
PALM_Y = 1.48


def place(model, side):
    a = math.radians(-90 * side)                           # R: +90, L: -90
    ca, sa = math.cos(a), math.sin(a)
    out = []
    for x, y, z in model.verts:
        out.append([x * ca + z * sa + side * 0.80, y + PALM_Y, -x * sa + z * ca])
    return out


def armed(models, rigs, saw, name):
    """the hunter with a saw in the right hand and the pistol in the left, one skin: (model, rig with no clips yet)"""
    body = models[0]
    byname = {m.name: m for m in models}
    rig = rigs[0]
    bidx = {b.name: i for i, b in enumerate(rig.bones)}
    verts = [list(v) for v in body.verts]
    faces = list(body.faces)
    vb = list(rig.vbones)
    for wname, key in ((saw, "saw"), ("pistol", "pistol")):
        m = byname[wname]
        hand, side = GRIP[key]
        off = len(verts)
        verts += place(m, side)
        vb += [bidx[hand]] * len(m.verts)
        for f in m.faces:
            faces.append(dataclasses.replace(f, a=f.a + off, b=f.b + off, c=f.c + off))
    model = dataclasses.replace(body, name=name, verts=verts, faces=faces)
    return model, A.Rig(name, list(rig.bones), vb, [])


def main(argv):
    """armed.py OUT.bm [saw_c|saw_o]   a preview cart (model `armed`)
       armed.py --game OUT.bmm        the game's file: models hunter_c and hunter_o, each with every clip"""
    models, rigs = B.build_all()
    descs = C.clips()
    if argv[1] == "--game":
        out_models, out_rigs = [], []
        for saw, name in (("saw_c", "hunter_c"), ("saw_o", "hunter_o")):
            m, r = gpuskin.two_bones_max(*armed(models, rigs, saw, name))   # no face on three bones: the GPU draws the skin
            out_models.append(m)
            out_rigs.append(animgen.add_clips(r, descs))
        export.export_bmm(argv[2], M.Mesh(out_models), anim=A.Anim(out_rigs), title="Hunter armed")
        print("wrote", argv[2], ", ".join("%s %d verts %d tris" % (m.name, len(m.verts), len(m.faces)) for m in out_models))
        return
    saw = argv[2] if len(argv) > 2 else "saw_c"
    model, r = armed(models, rigs, saw, "armed")
    r = animgen.add_clips(r, descs)
    export.export_models(argv[1], M.Mesh([model]), anim=A.Anim([r]), title="armed")
    print("wrote", argv[1], len(model.verts), "verts", len(model.faces), "tris")


if __name__ == "__main__":
    main(sys.argv)
