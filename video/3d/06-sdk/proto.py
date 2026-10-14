import os, sys, shutil, subprocess
HERE=os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0,os.path.join(HERE,".."))
import lib3d
from lib3d import ROOT
from bmvideo import Script
OUT=os.path.join(HERE,"out"); SD=os.path.join(OUT,"sd")
BIN=os.path.join(OUT,"bin"); os.makedirs(BIN,exist_ok=True)
subprocess.check_call([sys.executable,os.path.join(ROOT,"scripts","mkbm.py"),"-o",os.path.join(BIN,"editor.bm"),"--lua","carts/editor/main.lua","--cover","carts/editor/icon.png","--info","carts/editor/bm.info"],cwd=ROOT)
lib3d.fresh_sd(SD,[(os.path.join(OUT,"start.bme"),"TROPIC.BME")])
s=Script()
s.wait(120)
for step in sys.argv[1:]:
    if step.startswith("shot:"): s.shot(step[5:]); continue
    if step.startswith("wait:"): s.wait(int(step[5:])); continue
    if step.startswith("ctrl:"): s.chord(step[5:],shift=False); s.wait(20); continue
    if step.startswith("cs:"): s.chord(step[3:],shift=True); s.wait(20); continue
    if step.startswith("x:"):
        k,n=step[2:].split("*"); [ (s.key(k,hold=8), s.wait(4)) for _ in range(int(n)) ]; continue
    if step.startswith("t:"): s.type_text(step[2:],per_key=4); s.wait(10); continue
    s.key(step.lower() if step.islower() or len(step)>1 else step, hold=8); s.wait(20)
s.wait(60)
inp=os.path.join(OUT,"input.txt"); s.write_input(inp)
r=lib3d.run_headless(os.path.join(BIN,"editor.bm"),inp,int(s.t/60)+3,SD,shots=os.path.join(OUT,"shots"))
print(r.stdout[-300:], r.stderr[-300:])
