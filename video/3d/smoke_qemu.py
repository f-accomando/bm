"""Smoke test of video/lib/qemurec.py: boots the kernel in QEMU (icount), opens bm SDK from the Dev tab,
records ~12 s of guest time and builds out/smoke/test.mp4 (1920x1080, 60 fps).

  python3 video/3d/smoke_qemu.py [shift]      shift 1 = exact Pi speed (slow host), 2 = ~8x real time
"""
import os
import sys
import time
HERE = os.path.dirname(os.path.abspath(__file__))
W = os.path.join(HERE, "out", "smoke")
sys.path.insert(0, os.path.join(HERE, "..", "lib"))
import bmvideo, qemurec
shift = int(sys.argv[1]) if len(sys.argv) > 1 else 2
s=bmvideo.Script()
s.scene("Home ▸ Dev", [("Tab","next tab"),("Enter","open")]); s.say("Open the Dev tab and start bm SDK.")
s.sec(1); s.key("tab","Dev tab"); s.sec(1.5); s.key("enter","open bm SDK"); s.sec(3)
s.scene("bm SDK ▸ project", [("3","bm Studio")]); s.say("The project page.")
s.key("3","open Studio"); s.sec(4); s.key("esc","menu"); s.sec(2); s.end()
frames=s.t
os.makedirs(W, exist_ok=True)
open(W + "/in.txt", "w").write("\n".join(s.lines)+"\n")
r=qemurec.Recorder(W, shift=shift)
r.boot(timeout=100)
# 3 virtual seconds of settle
t=r.vtime_us()+3e6
while r.vtime_us()<t: time.sleep(0.05)
w0=time.time()
r.record(qemurec.parse_input(s.lines), frames, r.work+"/console.mkv")
print("recorded", frames, "frames in", time.time()-w0, "s wall", r.stats, flush=True)
bmvideo.write_ass(s, r.work+"/o.ass", "Tropic Trail · test", "bm Studio", frames)
qemurec.assemble(r.work+"/console.mkv", r.work+"/o.ass", r.work+"/test.mp4")
r.close()
