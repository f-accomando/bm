"""Writes the phase-2 samples of this folder from ../../../examples/*.json (made by
bm_core, NOT by the repo's tools: they are not goldens).

    python3 tests/golden/preview_samples/make_forge_samples.py     (from .claude/skills/bm-forge)

forge_walk.gif        the generated "walk" on the generated box humanoid, 12 fps, 160x160, bones drawn
forge_walk_sheet.png  the same 12 frames side by side
forge_jump_sheet.png  the generated "jump" (18 frames, baked), seen from the side
forge_humanoid.bm     a 320x180 cartridge: LUA stub + MESH (humanoid) + ANIM (idle, walk, jump);
                      passes bm_core checks; never run on the kernel or in bm Animator
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SKILL = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, SKILL)

import bm_core as B  # noqa: E402
from bm_core import animgen as AG, preview as P, shapes, skelgen as SG  # noqa: E402

EX = os.path.join(SKILL, "examples")


def build():
    """-> (cart, model, rig) of the examples"""
    rig = SG.skeleton(os.path.join(EX, "humanoid.json"))
    model, _ = shapes.load_boxes(os.path.join(EX, "humanoid_boxes.json"))
    rig = SG.bind(model, rig, strategy="nearest_bone_part")
    rig = AG.add_clips(rig, [os.path.join(EX, n + ".json") for n in ("idle", "walk", "jump")])
    cart = B.new_cart(b"-- bm-forge sample: a generated humanoid (MESH + ANIM), no game code\n"
                      b"function _draw() cls(0) end\n", 320, 180, "bm-forge humanoid", "bm-forge")
    AG.write_anim(cart, [rig], B.Mesh([model]))
    return cart, model, rig


if __name__ == "__main__":
    cart, model, rig = build()
    B.write_file(os.path.join(HERE, "forge_humanoid.bm"), cart)
    imgs, _ = P.render_anim(model, rig, "walk", fps=12, size=(160, 160), yaw=0.9, pitch=0.2, show_bones=True)
    P.save_gif(os.path.join(HERE, "forge_walk.gif"), imgs, delay_cs=8)
    P.contact_sheet(imgs, cols=6).save_png(os.path.join(HERE, "forge_walk_sheet.png"))
    imgs, _ = P.render_anim(model, rig, "jump", fps=12, size=(120, 160), yaw=1.5708, pitch=0.1, include_end=False)
    P.contact_sheet(imgs, cols=9).save_png(os.path.join(HERE, "forge_jump_sheet.png"))
    print("forge samples written")
