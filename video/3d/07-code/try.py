#!/usr/bin/env python3
"""Runs the game code of video 7 (game_flat.lua) on the map of video 6 in bmhost with a scripted pad and saves shots: python3 try.py [seconds]"""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmres
OUT = os.path.join(HERE, "out"); os.makedirs(OUT, exist_ok=True)
f = bmres.read(os.path.join(ROOT, "video", "3d", "06-sdk", "out", "start.bm"))
f.put(bmres.SEC_LUA, open(os.path.join(HERE, sys.argv[2] if len(sys.argv) > 2 else "game_flat.lua"), "rb").read())
bmres.write(os.path.join(OUT, "game.bm"), f)
L = ["60 pad 1 a"]
for fr in range(120, 2400, 3):
    L.append("%d pad 1 up" % fr)
for fr in (400, 700, 1000, 1500):
    L.append("%d pad 1 a" % fr)
for fr in (550, 1100, 1700):
    L.append("%d pad 1 b" % fr)
L += ["%d shot g%04d" % (fr, fr) for fr in range(90, 2400, 200)]
L.sort(key=lambda l: (int(l.split()[0])))
open(os.path.join(OUT, "pad.txt"), "w", newline="\n").write("\n".join(L) + "\n")
sd = os.path.join(OUT, "sd"); os.makedirs(os.path.join(sd, "carts"), exist_ok=True)
cmd = [os.path.join(ROOT, "build", "host", "bmhost-bin"), os.path.join(OUT, "game.bm"), "--sd", sd, "--seconds", sys.argv[1] if len(sys.argv) > 1 else "40",
       "--input", os.path.join(OUT, "pad.txt"), "--shots", os.path.join(OUT, "shots")]
os.makedirs(os.path.join(OUT, "shots"), exist_ok=True)
r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
print((r.stdout + r.stderr)[-700:])
