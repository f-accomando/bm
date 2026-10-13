#!/usr/bin/env python3
"""Calibration helper: runs bm Animator with a debug tail (dbg.lua) that logs every state change.
  python3 video/3d/03-animator/probe.py INPUT.txt [SECONDS] [SHOTS_DIR]   (the SD is a clean copy of the stand-in start)
The log goes to stdout; the cart is out/probe/animator_dbg.bm."""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import lib3d as L  # noqa: E402
out = os.path.join(HERE, "out", "probe")
os.makedirs(out, exist_ok=True)
src = open(os.path.join(L.ROOT, "carts", "animator", "main.lua"), encoding="utf-8").read()
src = src.replace("local rg = {", "rg = {", 1).replace("local an = {", "an = {", 1)
lua = os.path.join(out, "main_dbg.lua")
open(lua, "w", encoding="utf-8").write(src + open(os.path.join(HERE, "dbg.lua"), encoding="utf-8").read())
cart = os.path.join(out, "animator_dbg.bm")
subprocess.run([sys.executable, os.path.join(L.ROOT, "scripts", "mkbm.py"), "-o", cart, "--lua", lua, "--cover",
                os.path.join(L.ROOT, "carts", "animator", "icon.png"), "--info", os.path.join(L.ROOT, "carts", "animator", "bm.info")],
               check=True, cwd=L.ROOT, stdout=subprocess.DEVNULL)
start = os.path.join(L.ROOT, "video", "3d", "out", "assets", "models.bm")
sd = L.fresh_sd(os.path.join(out, "sd"), [(start, "TROPIC.BM")])
cmd = [L.BMHOST, cart, "--tool", "--sd", sd, "--input", sys.argv[1], "--seconds", sys.argv[2] if len(sys.argv) > 2 else "20"]
if len(sys.argv) > 3:
    os.makedirs(sys.argv[3], exist_ok=True); cmd += ["--shots", sys.argv[3]]
subprocess.run(cmd, cwd=L.ROOT)
