import os, subprocess, sys, time, socket
S = os.path.dirname(os.path.abspath(__file__))
os.environ["DISPLAY"] = ":99"
dur = sys.argv[1]
xv = subprocess.Popen(["Xvfb", ":99", "-screen", "0", "640x360x24"], stderr=subprocess.DEVNULL)
time.sleep(1)
ff = subprocess.Popen(["ffmpeg", "-y", "-f", "x11grab", "-draw_mouse", "0", "-framerate", "30", "-video_size", "640x360",
    "-i", ":99.0+0,0", "-t", dur, "-c:v", "libx264", "-preset", "ultrafast", "-crf", "12", f"{S}/raw.mp4"],
    stdin=subprocess.DEVNULL, stderr=open(f"{S}/ff.log", "w"))
time.sleep(3)
t0 = time.time()
qemu = subprocess.Popen(["qemu-system-arm", "-M", "raspi0", "-bios", "/home/user/bm/build/kernel.img",
    "-drive", f"if=sd,format=raw,file={S}/sd.img", "-display", "gtk,show-menubar=off,zoom-to-fit=off",
    "-serial", "tcp:127.0.0.1:5555,server=on,wait=off", "-serial", "null"], stderr=subprocess.DEVNULL)
def at(t):
    d = t0 + t - time.time()
    if d > 0: time.sleep(d)
def keys(s):
    with socket.create_connection(("127.0.0.1", 5555)) as c:
        c.sendall(s.encode()); time.sleep(0.1)
# timeline (s since start); log it
tl = [(float(sys.argv[2]), "\r")]
for t, k in tl:
    at(t); keys(k); print(t, repr(k), flush=True)
ff.wait()
qemu.kill(); xv.kill()
