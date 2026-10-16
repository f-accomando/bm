import os, sys, shutil, subprocess
HERE=os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0,os.path.join(HERE,".."))
import lib3d
from lib3d import ROOT
from bmvideo import Script
OUT=os.path.join(HERE,"out"); SD=os.path.join(OUT,"sd")
src=os.path.join(lib3d.CHAIN,"03-animator.bme")
BIN=os.path.join(OUT,"bin"); os.makedirs(BIN,exist_ok=True)
subprocess.check_call([sys.executable,os.path.join(ROOT,"scripts","mkbm.py"),"-o",os.path.join(BIN,"editor.bm"),"--lua","carts/editor/main.lua","--cover","carts/editor/icon.png","--info","carts/editor/bm.info"],cwd=ROOT)
lib3d.fresh_sd(SD,[(src,"TROPIC.BME")])
s=Script()
s.wait(120); s.shot("a_menu")
for step in sys.argv[1:]:
    if step.startswith("shot:"): s.shot(step[5:]); continue
    if step.startswith("wait:"): s.wait(int(step[5:])); continue
    if step.startswith('ctrl:'): s.chord(step[5:],shift=False); s.wait(20); continue
    if step.startswith('x:'):
        k,n=step[2:].split('*'); [ (s.key(k,hold=8), s.wait(4)) for _ in range(int(n)) ]; continue
    s.key(step.lower() if step.islower() or len(step)>1 else step, hold=8)
    s.wait(20)
s.wait(60); s.end() if hasattr(s,"end") else None
inp=os.path.join(OUT,"input.txt"); s.write_input(inp)
r=lib3d.run_headless(os.path.join(BIN,"editor.bm"),inp,int(s.t/60)+3,SD,shots=os.path.join(OUT,"shots"))
print(r.stdout[-400:], r.stderr[-300:])
