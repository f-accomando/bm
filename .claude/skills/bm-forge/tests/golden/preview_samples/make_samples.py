"""Writes the preview samples of this folder from ../village_models.bm.

    python3 tests/golden/preview_samples/make_samples.py     (from .claude/skills/bm-forge)

house.png       the house, textured, 256x256
models.png      the 8 village models, 128x128 each
walk.gif        the villager's "walk" clip at 12 fps, 128x128, bones drawn
walk_sheet.png  the same frames side by side
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "..", "..")))

import bm_core as B  # noqa: E402
from bm_core import preview as P  # noqa: E402

cart = B.read_file(os.path.join(HERE, "..", "village_models.bm"))
mesh, anim = cart.mesh(), cart.anim()
tex, inset = P.model_texture(cart)
t = time.perf_counter()
P.render_mesh(mesh.get("house"), size=(256, 256), texture=tex, inset=inset).save_png(os.path.join(HERE, "house.png"))
P.contact_sheet([P.render_mesh(m, size=(128, 128), texture=tex, inset=inset) for m in mesh.models],
                cols=4).save_png(os.path.join(HERE, "models.png"))
imgs, frames = P.render_anim(mesh.get("villager"), anim.get("villager"), "walk", fps=12, size=(128, 128),
                             show_bones=True, texture=tex, inset=inset)
P.save_gif(os.path.join(HERE, "walk.gif"), imgs, delay_cs=8)
P.contact_sheet(imgs, cols=5).save_png(os.path.join(HERE, "walk_sheet.png"))
print(f"{len(imgs)} walk frames; all samples in {time.perf_counter() - t:.2f} s")
