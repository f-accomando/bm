#!/usr/bin/env python3
"""
The frame queue's 2D (bm3d 4.1) on the PC: small cartridges run in bmhost-gpu
(the V3D emulated) with the queue off and on, and their frames compared.
With the queue, the 2D drawn after 3D is recorded while the GPU draws and
drawn after it, a zclear() between 3D stays inside the GPU's job, and the
next frame's _update runs while the GPU draws (the 2D it draws goes on the
next frame's page): the frames must be the same. A cartridge that draws 3D
in _update gets its _update after the frame again (a line in the log).

bm3d 6.9: with each frame shown a frame later (gpu3d_later=1, two
jobs in flight) the frame's job starts at its end and the frame is held
while the next _update and _draw run on the next page; the held frame gets
its recorded 2D and is shown at the end of the next one. The first loop
shows nothing, so the n-th picture shown is still the n-th frame: the
pictures must be the same as without the queue, picture for picture (the
map and the sheet changed by _update while a frame is held included).

  tests/gpu/queue2d.py BUILD_DIR
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import mkbm  # noqa: E402

CART = r"""
MODE = "%s"
local cube, t = nil, 0
function _init()
  cube = mesh_cube(0x3080F0)
  for y = 0, 15 do for x = 0, 15 do sset(8 + x, y, ((x + y) %% 3 == 0) and 0xFFA040 or nil) end end
  for x = 0, 7 do for y = 0, 7 do sset(x, y, 0x40FF80) end end
  mset(0, 0, 1) mset(1, 0, 2) mset(2, 0, 1)
end
function _update()
  t = t + 1
  if MODE == "upd2d" then
    cls(0x203040)                                 -- the frame starts here
    print("update " .. t, 4, 4, 0xFFFF00)
    camera(2, 0)
    rectfill(0, 30, 30, 40, 0x80FF80)
    camera()
  elseif MODE == "upd3d" and t %% 20 == 5 then
    camera3d(0, 1, -4, 0, -0.2, 60)
    draw3d(cube, -1, 0, 0)                        -- 3D in _update: after the frame again
  end
  if MODE ~= "upd2d" and t %% 7 == 0 then mset(1, 0, t %% 2 + 1) end
  if MODE ~= "upd2d" and t %% 5 == 0 then sset(9, 1, t %% 2 == 0 and 0xFF2020 or 0x2020FF) end
end
function _draw()
  if MODE ~= "upd2d" then cls(0x102030) end
  camera3d(0, 1, -4, 0, -0.2, 60)
  light3d(0.3, 1, -0.5, 0.3)
  zclear()
  draw3d(cube, 0, 0, 0, t * 0.05, t * 0.03, 0)
  rectfill(10, 10, 60, 20, 0xFF0000)              -- recorded while the GPU draws
  camera(5, 5)
  clip(0, 0, 200, 150)
  local x = print("hud " .. t, 20, 40, 0xFFFFFF)
  print("end", x, 40, 0x00FF00, 2)
  clip()
  camera()
  circfill(100, 100, 10, 0x00FFFF)
  circ(100, 100, 14, 0xFF00FF)
  line(0, 0, 319, 179, 0xFFFFFF)
  tri(200, 10, 250, 60, 180, 80, 0xFF00FF, 0x00FF00, 0x0000FF)
  tri(260, 10, 300, 60, 250, 80, 0xA0A0A0)
  prompt("A", 280, 150)
  spr(1, 150, 120)
  sspr(8, 0, 16, 16, 220, 100, false, false, 2)
  map(0, 0, 0, 160, 4, 1)
  draw3d(cube, 1.5, 0, 0, 0, t * 0.1, 0, 0.5)     -- 3D after the 2D: over it
  draw3d(cube, 0.6, 0.2, -3.0, 0, t * 0.02, 0, 0.2, 64)    -- first person: a zclear inside the job
  rect(150, 20, 200, 60, 0xFFFF00)
  if MODE == "held" then                          -- the page not read: the frame's job left running
    map(0, 0, 0, 144, 4, 1)                       -- recorded with the frame held: before mset, sset
    spr(1, 180, 140)
  else
    pset(5, 170, pget(30, 15) or 0)
  end
  font("6x12") print("small", 250, 170, 0xC0C0FF) font("8x16")
  for i = 0, 40 do pset(100 + i, 5, 0xFFFFFF) end
end
"""


def run(host, cart, shots, queue, gpu2d=0, later=0):
    os.makedirs(shots, exist_ok=True)
    for f in os.listdir(shots):
        os.remove(os.path.join(shots, f))
    env = dict(os.environ, BMHOST_CONFIG=f"gpu3d_vs=2,gpu3d_queue={queue},gpu3d_2d={gpu2d},gpu3d_later={later}",
               BMHOST_NONET="1")
    r = subprocess.run([host, cart, "--seconds", "1", "--shots", shots, "--every", "3"], capture_output=True,
                       text=True, env=env)
    return r.returncode, r.stdout + r.stderr


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else "build"
    from PIL import Image, ImageChops
    host = os.path.join(build, "host", "bmhost-gpu")
    out = os.path.join(build, "queue2d")
    os.makedirs(out, exist_ok=True)
    fails = 0

    def check(cond, what, log=""):
        nonlocal fails
        print(("ok   " if cond else "FAIL ") + what)
        if not cond:
            fails += 1
            print(log[-2500:])

    for mode in ("draw", "upd2d", "upd3d", "held"):
        cart = os.path.join(out, f"{mode}.bm")
        with open(cart, "wb") as f:
            f.write(mkbm.pack((CART % mode).encode(), title=f"queue2d {mode}", author="bm", res=(320, 180)))
        logs, dirs = [], []
        for q in (0, 1):
            d = os.path.join(out, f"{mode}-q{q}")
            code, log = run(host, cart, d, q)
            check(code == 0 and "Lua error" not in log, f"{mode}, queue {q}: runs", log)
            logs.append(log)
            dirs.append(d)
        log = logs[1]
        check("frame queue: 2.00 jobs a frame started" in log or mode == "upd3d", f"{mode}: the jobs started", log)
        check("2D drawings a frame recorded" in log, f"{mode}: 2D recorded while the GPU draws", log)
        check("zclear() a frame inside a job" in log and "0.00 zclear()" not in log, f"{mode}: zclear in the job", log)
        late = "_update draws 3D or reads the page" in log
        check(late == (mode == "upd3d"), f"{mode}: _update {'after' if late else 'during'} the GPU's frame", log)
        shots = sorted(os.listdir(dirs[0]))
        differ = []
        for s in shots:
            # upd3d: from its first frame with 3D in _update on (frame 5,
            # shown a frame early once), the same as without the queue
            if mode == "upd3d" and int(s.split(".")[0]) < 9:
                continue
            a = Image.open(os.path.join(dirs[0], s)).convert("RGB")
            b = Image.open(os.path.join(dirs[1], s)).convert("RGB")
            if ImageChops.difference(a, b).getbbox():
                differ.append(s)
        check(len(shots) >= 15 and not differ, f"{mode}: {len(shots)} frames, the same with the queue "
              f"({len(differ)} differ: {' '.join(differ[:5])})", logs[0][-500:] + log[-500:])
        # bm3d 5.1: each frame shown a frame later (two jobs in flight), also with
        # the 2D over the 3D in the GPU's job: the same pictures as without
        # the queue, every one (no _update runs early here). "held" does not
        # read the page at the end of _draw: its last job is still running
        # when the next frame fills its own (the other modes' pget waits)
        for g2 in ((0, 1) if mode in ("draw", "held") else (0,)):
            d = os.path.join(out, f"{mode}-later{'-2d' if g2 else ''}")
            what = f"{mode}, a frame later{', 2D on the GPU' if g2 else ''}"
            code, logl = run(host, cart, d, 2, g2, 1)
            check(code == 0 and "Lua error" not in logl, f"{what}: runs", logl)
            check("each frame shown a frame later" in logl and "frame later:" in logl, f"{what}: frames held", logl)
            if mode == "held":
                filled = [l for l in logl.splitlines() if "frame later:" in l]
                busy = filled and float(filled[0].split("frame later, ")[1].split()[0]) > 0.5
                check(busy, f"{what}: the next frame's job filled while the GPU drew the one before", logl)
            check("_update draws 3D or reads the page" not in logl, f"{what}: no early _update", logl)
            got = set(os.listdir(d))
            differ = []
            for s in shots:
                if s not in got:
                    differ.append(s)
                    continue
                a = Image.open(os.path.join(dirs[0], s)).convert("RGB")
                b = Image.open(os.path.join(d, s)).convert("RGB")
                if ImageChops.difference(a, b).getbbox():
                    differ.append(s)
            check(not differ, f"{what}: the same {len(shots)} pictures as without the queue ({len(differ)} differ: "
                  f"{' '.join(differ[:5])})", logl[-800:])
        if mode != "draw":
            continue
        # bm3d 4.8: the 2D over the 3D drawn by the GPU in its job (gpu3d_2d=1),
        # without and with the queue: the same frames, fewer jobs
        for q in (0, 1):
            d = os.path.join(out, f"{mode}-2d-q{q}")
            code, log2 = run(host, cart, d, q, 1)
            check(code == 0 and "Lua error" not in log2, f"{mode}, 2D on the GPU, queue {q}: runs", log2)
            check("2D on the GPU:" in log2, f"{mode}, 2D on the GPU, queue {q}: quads in the job", log2)
            differ = []
            for s in shots:
                a = Image.open(os.path.join(dirs[0], s)).convert("RGB")
                b = Image.open(os.path.join(d, s)).convert("RGB")
                if ImageChops.difference(a, b).getbbox():
                    differ.append(s)
            check(not differ, f"{mode}, 2D on the GPU, queue {q}: the same frames ({len(differ)} differ: "
                  f"{' '.join(differ[:5])})", log2[-800:])
    print(f"\nqueue2d: {'all ok' if not fails else f'{fails} failed'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
