#!/usr/bin/env python3
"""
gpu3d_light (bm3d 7.17) on the PC: the light of dark_begin .. dark_end made by the GPU (the V3D emulated in
bmhost-gpu, key gpu3d_light=1) against the model it implements, computed here from the unlit picture:

  S = sum of level * (1 - d2 / r2)^2 (0 outside a lamp), M = A + S * W for each channel,
  A = the ambient row of fades() / 256, W = (the top row - A) / 256, level between the ambient and the top;
  the pixel times M, to 8 bits.

The ARM's light (levels, the highest lamp, dither) is another picture; this checks that the GPU path runs, that
the job is the page's and that the numbers are the model's within the 8-bit and 5-6-5 roundings.

  tests/gpu/gpu3d_light.py BUILD_DIR
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "scripts"))
import mkbm  # noqa: E402

COLOURS = (0x40FF80, 0xFFA040, 0x2060C0, 0xC0C0C0)
LEVELS = (
    (0x40FF80, (0x001008, 0x10802C, 0x40FF80)),
    (0xFFA040, (0x201000, 0x805020, 0xFFA040)),
    (0x2060C0, (0x040C18, 0x103060, 0x2060C0)),
    (0xC0C0C0, (0x181818, 0x606060, 0xC0C0C0)),
)
LAMPS = ((80, 90, 40, 2), (230, 120, 30, 1), (150, 60, 55, 2))     # x, y, r, level (the ambient is 0: levels 0..2)

CART = r"""
LIGHT = %s
function _init()
  fades({ %s })
end
function _update() end
function _draw()
  cls(0x102030)
  if LIGHT then dark_begin(0) end                  -- what is drawn from here is lit (the RAM page)
  for i = 0, 7 do rectfill(i * 40, 0, i * 40 + 39, 179, COLS[i %% %d + 1]) end
  rectfill(0, 100, 319, 110, 0x808080)
  if LIGHT then
    glow(80, 90, 40, 2, 0.5)
    glow(230, 120, 30, 1, 0.5)
    glow(150, 60, 55, 2, 0.5)
    dark_end()
  end
end
"""


def make_cart(path, light):
    rows = ", ".join("{ 0x%06X, %s }" % (c, ", ".join("0x%06X" % x for x in lv)) for c, lv in LEVELS)
    lua = "COLS = { %s }\n" % ", ".join("0x%06X" % c for c in COLOURS) + CART % (light, rows, len(COLOURS))
    with open(path, "wb") as f:
        f.write(mkbm.pack(lua.encode(), title="gpu3d_light", author="bm", res=(320, 180)))


def c565(rgb):
    return (rgb >> 19 & 31, rgb >> 10 & 63, rgb >> 3 & 31)


def mul_rows():
    """g16_fade_done: the average ratio, channel by channel, of the colours with a table (x 256, at most 512)"""
    rows = []
    for k in range(len(LEVELS[0][1])):
        num, den = [0, 0, 0], [0, 0, 0]
        for c, lv in LEVELS:
            t, f = c565(lv[k]), c565(c)
            for ch in range(3):
                num[ch] += t[ch]
                den[ch] += f[ch]
        rows.append([min(512, num[ch] * 256 // den[ch]) if den[ch] else 0 for ch in range(3)])
    return rows


def run(host, cart, shots, key):
    os.makedirs(shots, exist_ok=True)
    for f in os.listdir(shots):
        os.remove(os.path.join(shots, f))
    env = dict(os.environ, BMHOST_CONFIG=f"gpu3d_vs=2,gpu3d_light={key}", BMHOST_NONET="1")
    r = subprocess.run([host, cart, "--seconds", "1", "--shots", shots, "--every", "6"], capture_output=True,
                       text=True, env=env)
    return r.returncode, r.stdout + r.stderr


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else "build"
    from PIL import Image
    host = os.path.join(build, "host", "bmhost-gpu")
    out = os.path.join(build, "gpu3d_light")
    os.makedirs(out, exist_ok=True)
    fails = 0

    def check(cond, what, log=""):
        nonlocal fails
        print(("ok   " if cond else "FAIL ") + what)
        if not cond:
            fails += 1
            print(log[-2500:])

    plain, lit = os.path.join(out, "plain.bm"), os.path.join(out, "lit.bm")
    make_cart(plain, "false")
    make_cart(lit, "true")
    code, log0 = run(host, plain, os.path.join(out, "plain"), 0)
    check(code == 0 and "Lua error" not in log0, "unlit scene runs", log0)
    code, log1 = run(host, lit, os.path.join(out, "gpu"), 1)
    check(code == 0 and "Lua error" not in log1, "lit scene runs with gpu3d_light=1", log1)
    check("gpu3d_light=1" in log1 and "did not start" not in log1, "the key is seen and the GPU started", log1)
    code, log2 = run(host, lit, os.path.join(out, "arm"), 0)
    check(code == 0, "lit scene runs with the key off (the ARM's light)", log2)
    shots = sorted(os.listdir(os.path.join(out, "gpu")))
    check(len(shots) >= 4, f"{len(shots)} pictures")

    mul = mul_rows()
    amb, top = 0, len(mul) - 1
    a = [mul[amb][c] / 256.0 for c in range(3)]
    w = [mul[top][c] / 256.0 - a[c] for c in range(3)]
    base = Image.open(os.path.join(out, "plain", sorted(os.listdir(os.path.join(out, "plain")))[0])).convert("RGB")
    W, H = base.size
    sx, sy = W / 320.0, H / 180.0                       # the host may scale the shots
    worst, total, count, bad = 0, 0, 0, 0
    for name in shots:
        got = Image.open(os.path.join(out, "gpu", name)).convert("RGB")
        check(got.size == base.size, f"{name}: the size is the scene's")
        for y in range(0, H, 3):
            for x in range(0, W, 3):
                s = 0.0
                for lx, ly, r, lv in LAMPS:
                    d2 = (x / sx - lx) ** 2 + (y / sy - ly) ** 2
                    f = max(0.0, 1.0 - d2 / (r * r))
                    s += (lv - amb) / (top - amb) * f * f
                p = base.getpixel((x, y))
                g = got.getpixel((x, y))
                for ch in range(3):
                    want = min(255.0, p[ch] * (a[ch] + s * w[ch]))
                    err = abs(g[ch] - want)
                    worst = max(worst, err)
                    total += err
                    count += 1
                    if err > 14:
                        bad += 1
        break                                           # one picture is the whole test: the scene does not move
    mean = total / max(count, 1)
    check(mean < 4.0, f"the mean error against the model is {mean:.2f} of 255 (under 4: the store keeps the top 5 and 6 bits)")
    check(bad * 200 < max(count, 1), f"{bad} of {count} values are over 14 off (the worst {worst:.0f})")
    jobs = [l for l in log1.splitlines() if "light2d" in l or "light on the GPU" in l]
    check(jobs, "the report counts the GPU's light", log1)
    print("gpu3d_light:", "all ok" if not fails else f"{fails} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
