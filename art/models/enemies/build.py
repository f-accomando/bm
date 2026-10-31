"""build.py: the enemies of Dark Night -> two BMRES files.

  python3 build.py OUT_DIR
    OUT_DIR/enemies.bmm   the editable file: villager, huntsman, dog (each with its clips) and the weapons cleaver, torch, haxe
                          as separate models (bm Mesh and bm Animator open it)
    OUT_DIR/dnenemy.bmm   the game's file: villager (the cleaver in his right hand), huntsman (the torch in the left, the axe in the
                          right) and dog, the weapons baked on the hands, each with its clips
"""
import dataclasses
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import enemy_build as EB  # noqa: E402
import enemy_clips as EC  # noqa: E402
from bm_core import anim as A, animgen, export, gpuskin, mesh as M  # noqa: E402

PALM_Y = 1.48
GRIP = {"cleaver": ("hand.R", -1), "haxe": ("hand.R", -1), "torch": ("hand.L", 1)}      # hand bone, side (+1 = .L on +x)


def place(model, side):
    """a weapon (tip -z, up +y) -> the hand in the T pose: turned about y by +-90 degrees, at the palm"""
    a = math.radians(-90 * side)
    ca, sa = math.cos(a), math.sin(a)
    return [[x * ca + z * sa + side * 0.80, y + PALM_Y, -x * sa + z * ca] for x, y, z in model.verts]


def armed(body, rig, weapons):
    byname = {m.name: m for m in weapons}
    bidx = {b.name: i for i, b in enumerate(rig.bones)}
    verts = [list(v) for v in body.verts]
    faces = list(body.faces)
    vb = list(rig.vbones)
    for wname in GRIP_OF[body.name]:
        m = byname[wname]
        hand, side = GRIP[wname]
        off = len(verts)
        verts += place(m, side)
        vb += [bidx[hand]] * len(m.verts)
        for f in m.faces:
            faces.append(dataclasses.replace(f, a=f.a + off, b=f.b + off, c=f.c + off))
    return dataclasses.replace(body, verts=verts, faces=faces), A.Rig(rig.model, list(rig.bones), vb, [])


GRIP_OF = {"villager": ["cleaver"], "huntsman": ["torch", "haxe"]}


def main(out_dir):
    models, rigs = EB.build_all()
    by = {m.name: (m, r) for m, r in zip(models, rigs)}
    clips = {"villager": EC.man_clips("villager"), "huntsman": EC.man_clips("huntsman"), "dog": EC.dog_clips()}
    weapons = [by[n][0] for n in ("cleaver", "torch", "haxe")]
    # the editable file: bodies with clips, weapons separate
    ed_models, ed_rigs = [], []
    for name in ("villager", "huntsman", "dog", "cleaver", "torch", "haxe"):
        m, r = by[name]
        if name in clips:
            r = animgen.add_clips(r, clips[name])
        ed_models.append(m)
        ed_rigs.append(r)
    os.makedirs(out_dir, exist_ok=True)
    p1 = os.path.join(out_dir, "enemies.bmm")
    export.export_bmm(p1, M.Mesh(ed_models), anim=A.Anim(ed_rigs), title="Enemies")
    # the game's file: the weapons baked on the hands
    g_models, g_rigs = [], []
    for name in ("villager", "huntsman", "dog"):
        m, r = by[name]
        if name in GRIP_OF:
            m, r = armed(m, r, weapons)
        m, r = gpuskin.two_bones_max(m, r)                             # no face on three bones: the GPU draws the skin
        g_models.append(m)
        g_rigs.append(animgen.add_clips(r, clips[name]))
    p2 = os.path.join(out_dir, "dnenemy.bmm")
    export.export_bmm(p2, M.Mesh(g_models), anim=A.Anim(g_rigs), title="Enemies armed")
    for p, ms in ((p1, ed_models), (p2, g_models)):
        print("wrote", p, ", ".join("%s %d/%d" % (m.name, len(m.verts), len(m.faces)) for m in ms))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    main(sys.argv[1])
