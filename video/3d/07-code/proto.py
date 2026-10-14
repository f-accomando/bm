#!/usr/bin/env python3
"""Dev probe of the assistant answers in bm Code (run in WSL): python3 proto.py "question" [downs]"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
OUT = os.path.join(HERE, "out")
q = sys.argv[1] if len(sys.argv) > 1 else "third person camera"
downs = int(sys.argv[2]) if len(sys.argv) > 2 else 0
L = [(60, "\\x0f"), (300, "\\n"), (500, "\\x1b[17~")]
f = 600
for ch in q:
    L.append((f, "\\x20" if ch == " " else ch))
    f += 4
f += 120
shots = [(f, "q0")]
for i in range(downs):
    f += 60
    L.append((f, "\\xf1"))
    f += 90
    shots.append((f, "q%d" % (i + 1)))
rows = ["%d type %s" % x for x in L] + ["%d shot %s" % x for x in shots]
rows.sort(key=lambda r: int(r.split()[0]))
with open(os.path.join(OUT, "in.txt"), "w", newline="\n") as g:
    g.write("\n".join(rows) + "\n")
sd = os.path.join(OUT, "sd2", "carts")
os.makedirs(sd, exist_ok=True)
shutil.copy(os.path.join(OUT, "start.bme"), os.path.join(sd, "TROPIC.BME"))
os.makedirs(os.path.join(OUT, "shots"), exist_ok=True)
r = subprocess.run([os.path.join(ROOT, "build", "host", "bmhost-ai"), os.path.join(OUT, "bin", "code.bm"), "--tool", "--sd", os.path.join(OUT, "sd2"),
                    "--seconds", str(f // 60 + 3), "--input", os.path.join(OUT, "in.txt"), "--shots", os.path.join(OUT, "shots"), "--quiet"],
                   cwd=ROOT, capture_output=True, text=True)
print((r.stdout + r.stderr)[-200:])
