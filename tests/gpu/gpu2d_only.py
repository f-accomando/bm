#!/usr/bin/env python3
"""
gpu2d_only (bm3d 7.16) on the PC: a game with no 3D has its 2D drawn by the GPU (the V3D emulated in
bmhost-gpu) with the key on, and the ARM draws it with the key off: the pictures must be the same, pixel for
pixel. Small cartridges (sprites, a map, rectangles, text, lines and a circle that the GPU does not take, a
sprite stack, the light of dark_begin .. dark_end) and Yharnam's first seconds.

  tests/gpu/gpu2d_only.py BUILD_DIR
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import mkbm  # noqa: E402

CART = r"""
LIGHT = %s
local t = 0
function _init()
  for y = 0, 15 do for x = 0, 15 do sset(8 + x, y, ((x + y) %% 3 == 0) and 0xFFA040 or nil) end end
  for x = 0, 7 do for y = 0, 7 do sset(x, y, 0x40FF80) end end
  mset(0, 0, 1) mset(1, 0, 2) mset(2, 0, 1)
  fades({ { 0x40FF80, 0x001008, 0x10802C, 0x40FF80 }, { 0xFFA040, 0x201000, 0x805020, 0xFFA040 } })
end
function _update() t = t + 1 end
function _draw()
  cls(0x102030)
  rectfill(10, 10, 60, 20, 0xFF0000)
  rect(150, 20, 200, 60, 0xFFFF00)
  spr(1, 150, 120)
  spr(0, 20, 120, 1, 1, true, false)
  sspr(8, 0, 16, 16, 220, 100, false, false, 2)
  map(0, 0, 0, 160, 3, 1)
  camera(5, 5)
  clip(0, 0, 200, 150)
  local x = print("hud " .. t, 20, 40, 0xFFFFFF)
  print("end", x, 40, 0x00FF00, 2)
  clip()
  camera()
  circfill(100, 100, 10, 0x00FFFF)                 -- not taken by the GPU: recorded after the job
  line(0, 0, 319, 179, 0xFFFFFF)                   -- a slanted line: the ARM
  line(0, 100, 300, 100, 0xFF8000)                 -- a row: the GPU
  stack(8, 0, 8, 8, 3, 60, 100, 0.5, 2, 3)         -- the ARM
  rectfill(200, 140, 260, 170, 0x2040FF)           -- after the ARM's: the order stays
  for i = 0, 40 do pset(100 + i, 5, 0xFFFFFF) end
  if LIGHT then
    dark_begin(1)
    glow(80, 90, 40, 3, 0.5)
    glow(230, 120, 30, 2, 0.5)
    dark_end()
    print("lit " .. t, 4, 170, 0xFFFFFF)            -- after the light
    rectfill(4, 4, 30, 8, 0xFFFF00)
  end
end
"""


def run(host, cart, shots, key, extra=""):
    os.makedirs(shots, exist_ok=True)
    for f in os.listdir(shots):
        os.remove(os.path.join(shots, f))
    env = dict(os.environ, BMHOST_CONFIG=f"gpu3d_vs=2,gpu2d_only={key}{extra}", BMHOST_NONET="1")
    r = subprocess.run([host, cart, "--seconds", "2", "--shots", shots, "--every", "3"], capture_output=True,
                       text=True, env=env)
    return r.returncode, r.stdout + r.stderr


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else "build"
    from PIL import Image, ImageChops
    host = os.path.join(build, "host", "bmhost-gpu")
    out = os.path.join(build, "gpu2d_only")
    os.makedirs(out, exist_ok=True)
    fails = 0

    def check(cond, what, log=""):
        nonlocal fails
        print(("ok   " if cond else "FAIL ") + what)
        if not cond:
            fails += 1
            print(log[-2500:])

    carts = []
    for name, light in (("plain", "false"), ("light", "true")):
        cart = os.path.join(out, f"{name}.bm")
        with open(cart, "wb") as f:
            f.write(mkbm.pack((CART % light).encode(), title=f"gpu2d_only {name}", author="bm", res=(320, 180)))
        carts.append((name, cart))
    yharnam = os.path.join(build, "carts", "yharnam.bm")
    if os.path.exists(yharnam):
        carts.append(("yharnam", yharnam))
    for name, cart in carts:
        logs, dirs = [], []
        for key in (0, 1):
            d = os.path.join(out, f"{name}-{key}")
            code, log = run(host, cart, d, key)
            check(code == 0 and "Lua error" not in log, f"{name}, gpu2d_only={key}: runs", log)
            logs.append(log)
            dirs.append(d)
        check("gpu2d_only=1" in logs[1], f"{name}: the key is seen", logs[1])
        check("did not start" not in logs[1], f"{name}: the GPU started", logs[1])
        quads = [l for l in logs[1].splitlines() if "2D on the GPU:" in l]
        check(quads and float(quads[0].split("GPU:")[1].split()[0]) > 1, f"{name}: the GPU drew quads "
              f"({quads[0].strip() if quads else 'no line'})", logs[1])
        check(not any("2D on the GPU:" in l for l in logs[0].splitlines()), f"{name}: no quad with the key off", logs[0])
        shots = sorted(os.listdir(dirs[0]))
        got = set(os.listdir(dirs[1]))
        differ = []
        for s in shots:
            if s not in got:
                differ.append(s)
                continue
            a = Image.open(os.path.join(dirs[0], s)).convert("RGB")
            b = Image.open(os.path.join(dirs[1], s)).convert("RGB")
            if ImageChops.difference(a, b).getbbox():
                differ.append(s)
        check(len(shots) >= 8 and not differ, f"{name}: {len(shots)} pictures, the same with the GPU's 2D "
              f"({len(differ)} differ: {' '.join(differ[:5])})", logs[0][-400:] + logs[1][-400:])
    print("gpu2d_only:", "all ok" if not fails else f"{fails} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
