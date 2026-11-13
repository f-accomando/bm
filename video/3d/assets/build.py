#!/usr/bin/env python3
"""Reference assets of Tropic Trail with bm-forge: PICO, APPLE, CRATE (boxes), PICO's skeleton and 5 clips.
  python video/3d/assets/build.py [OUTDIR]     default: video/3d/out/assets
Writes OUTDIR/models.bm, rig.bm, tropic_assets.bm, PNG previews; checks budgets (PIANO.md section 2).
These are the *answers* for verify.py and a visual reference; the videos build the models by hand in the console."""
import os, subprocess, sys
H = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(H, "..", "..", ".."))
F = [sys.executable, os.path.join(ROOT, ".claude", "skills", "bm-forge", "scripts", "bm_forge.py")]
out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "video", "3d", "out", "assets"))
os.makedirs(out, exist_ok=True)
def run(*a):
    print("$ forge", *[os.path.basename(x) for x in a]); r = subprocess.run(F + list(a)); 
    if r.returncode: sys.exit(r.returncode)
j = lambda n: os.path.join(H, n)
c = lambda n: os.path.join(H, "clips", n + ".json")
o = lambda n: os.path.join(out, n)
run("mesh", j("pico_boxes.json"), j("apple_boxes.json"), j("crate_boxes.json"), "-o", o("models.bm"), "--title", "Tropic Trail assets", "--max-tris", "300")
run("skel", j("pico_skel.json"), "--in", o("models.bm"), "-o", o("rig.bm"))
run("anim", *[c(n) for n in ("idle", "run", "jump", "spin", "hit")], "--in", o("rig.bm"), "-o", o("tropic_assets.bm"))
run("check", o("tropic_assets.bm"))
