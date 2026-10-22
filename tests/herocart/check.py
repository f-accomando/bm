#!/usr/bin/env python3
"""
Hero Stress (carts/herocart) in bmhost, no hardware (stage 1 of
docs/design/hero-stress-cart-and-cel-shader.md):

  1. the models: carts/herocart/build/mkheroes.py --check (the 8 triangle counts);
  2. a scripted run: the printed line has its fields, `tri` rises with the hero
     count and falls from level 0 to level 3, the keys change `n` and `lod`;
  3. the sweep (Start + A): 10 table lines `sweep n=..` and the load line.

  tests/herocart/check.py BMHOST-BIN CART.bm WORKDIR
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
LINE = re.compile(r"^herocart 1 t=(\d+) n=(\d+) lod=(\S+) res=(\d+)x(\d+) opt=(\S+) \| ms=([\d.]+) max=([\d.]+) "
                  r"fps=(\d+) gpu=([\d.]+) wait=([\d.]+) anim=([\d.]+) tri=(\d+) vtx=(\d+) lua=(\d+) kb=(\d+)$")


def script(path, taps):
    """taps: [(frame, buttons)]: the buttons held for 2 frames"""
    with open(path, "w") as f:
        f.write("0 device 1 keyboard\n")
        for frame, b in taps:
            f.write(f"{frame} pad 1 {b}\n{frame + 2} pad 1 none\n")


def run(host, cart, work, name, taps, seconds):
    inp = os.path.join(work, name + ".txt")
    script(inp, taps)
    r = subprocess.run([host, cart, "--input", inp, "--seconds", str(seconds)], capture_output=True, text=True)
    out = r.stdout + r.stderr
    assert r.returncode == 0, out[-2000:]
    assert "error" not in out.lower().replace("0 error", ""), out[-2000:]
    return [l for l in out.splitlines() if l.startswith(("herocart", "sweep"))]


def main():
    host, cart, work = sys.argv[1:4]
    os.makedirs(work, exist_ok=True)
    r = subprocess.run([sys.executable, os.path.join(ROOT, "carts", "herocart", "build", "mkheroes.py"), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    print(r.stdout.strip().splitlines()[-1])

    # 2. counts and levels: n = 0, 10, 20 at level 0, then level 3
    taps = [(10 + 6 * k, "l2") for k in range(4)] + [(40, "y")]
    taps += [(270, "r2"), (276, "r2"), (510, "r2"), (516, "r2"), (750, "y"), (756, "y"), (762, "y")]
    lines = run(host, cart, work, "levels", taps, 17)
    rows = []
    for l in lines:
        m = LINE.match(l)
        if m:
            rows.append(m.groups())
    assert len(rows) >= 14, lines
    assert all(r[5].startswith(("arm", "gpu")) and "tex" in r[5] and "cel0" in r[5] for r in rows), rows[0]
    tri = {}
    prev = None
    for r in rows:                              # a line whose n and lod did not change: the whole second is that state
        key = (int(r[1]), r[2])
        if key == prev:
            tri[key] = int(r[12])
        prev = key
    print("tri by (n, lod):", tri)
    assert tri[(0, "0")] < tri[(10, "0")] < tri[(20, "0")], tri
    assert tri[(20, "3")] < tri[(20, "0")], tri
    assert tri[(0, "0")] > 1000, tri             # the plaza: the base cost the panel prints as `map`

    # 3. the sweep
    inp = os.path.join(work, "sweep.txt")
    with open(inp, "w") as f:
        f.write("0 device 1 keyboard\n5 pad 1 start a\n8 pad 1 none\n")
    r = subprocess.run([host, cart, "--input", inp, "--seconds", "30"], capture_output=True, text=True)
    assert r.returncode == 0, (r.stdout + r.stderr)[-2000:]
    sw = [l for l in (r.stdout + r.stderr).splitlines() if l.startswith("sweep n=")]
    assert [int(re.match(r"sweep n=(\d+)", l).group(1)) for l in sw] == [0, 4, 8, 12, 16, 20, 28, 36, 48, 64], sw
    tris = [int(re.search(r"tri=(\d+)", l).group(1)) for l in sw]
    assert tris == sorted(tris) and tris[0] < tris[-1], tris
    assert "sweep load60=" in r.stdout + r.stderr
    print("sweep:", len(sw), "lines, tri", tris[0], "->", tris[-1])
    print("herocart: ok")


if __name__ == "__main__":
    main()
