#!/usr/bin/env python3
"""
Hero Stress (carts/hero) in bmhost, no hardware (stage 1 of
docs/design/hero-stress-cart-and-cel-shader.md):

  1. the models: carts/hero/build/mkheroes.py --check (the 8 triangle counts);
  2. a scripted run: the printed line has its fields, `tri` rises with the hero
     count and falls from level 0 to level 3, the keys change `n` and `lod`;
  3. the sweep (Start + A): 10 table lines `sweep n=..` and the load line.

  tests/hero/check.py BMHOST-BIN CART.bm WORKDIR
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
LINE = re.compile(r"^hero 1 t=(\d+) n=(\d+) lod=(\S+) res=(\d+)x(\d+) opt=(\S+) \| ms=([\d.]+) max=([\d.]+) "
                  r"fps=(\d+) gpu=([\d.]+) wait=([\d.]+) anim=([\d.]+) tri=(\d+) vtx=(\d+) lua=(\d+) kb=(\d+) skip=(\d+)$")


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
    return [l for l in out.splitlines() if l.startswith(("hero", "sweep"))]


def main():
    host, cart, work = sys.argv[1:4]
    os.makedirs(work, exist_ok=True)
    r = subprocess.run([sys.executable, os.path.join(ROOT, "carts", "hero", "build", "mkheroes.py"), "--check"],
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

    # 2b. rows 20 (cel bands) and 12 (dither) are live (26 rows: up from row 1 wraps to row 26): six ups more reach row 20,
    # right three times gives 4 bands, up eight times reaches row 12, A turns the dither off
    taps = [(10 + 6 * k, "up") for k in range(7)] + [(60 + 6 * k, "right") for k in range(3)]
    taps += [(100 + 6 * k, "up") for k in range(8)] + [(170, "a")]
    lines = run(host, cart, work, "cel", taps, 6)
    opts = [LINE.match(l).group(6) for l in lines if LINE.match(l)]
    assert opts and any("cel4" in o and ",dith," in o for o in opts), opts
    assert "cel4" in opts[-1] and "nodith" in opts[-1], opts
    print("cel/dither rows:", opts[0].split(",")[-4:], "->", opts[-1].split(",")[-4:])

    # 2c. the Resolution row (18: nine ups from row 1, through row 26) goes through the labels to 1920x1080 and the
    # printed res= is the mode really in force; the opt string has the probe level ("vs0p0": nothing
    # asked, no probe on the ARM)
    taps = [(10 + 6 * k, "up") for k in range(9)] + [(80 + 40 * k, "right") for k in range(3)]
    lines = run(host, cart, work, "res", taps, 8)
    rows = [LINE.match(l).groups() for l in lines if LINE.match(l)]
    assert rows and all(re.search(r"^arm,vs0p0,", r[5]) for r in rows), rows[0][5]
    assert (rows[-1][3], rows[-1][4]) == ("1920", "1080"), [r[3:5] for r in rows]
    print("resolution row:", "x".join(rows[0][3:5]), "->", "x".join(rows[-1][3:5]))

    # 2e. the tests of the bridge: the keys hero_test and hero_prof run a suite (BMHOST_CONFIG is the host's config.txt)
    env = dict(os.environ, BMHOST_CONFIG="hero_test=2d-text.2d-sprites.plaza,hero_prof=arm")
    r = subprocess.run([host, cart, "--seconds", "100"], capture_output=True, text=True, env=env)
    out = r.stdout + r.stderr
    assert r.returncode == 0, out[-1500:]
    res = [l for l in out.splitlines() if l.startswith("hero 1 result ")]
    assert [l.split()[3] for l in res] == ["2d-text", "2d-sprites", "plaza"], res
    assert "hero 1 suite done tests=3" in out, out[-800:]
    print("suite:", len(res), "results")

    # 2d. the help of the panel: one table, a text of at most two lines of 44 characters for every row
    src = open(os.path.join(ROOT, "carts", "hero", "src", "30_options.lua"), encoding="utf-8").read()
    names = re.findall(r'(?:choice|grey|local \w+ = \{ name =) ?\(?"([^"]+)"', src) + ["Heroes"]
    help_ = {k: (a, b) for k, a, b in re.findall(r'\["([^"]+)"\] = \{ "([^"]*)", "([^"]*)" \}', src)}
    assert set(names) == set(help_), (sorted(set(names) ^ set(help_)))
    assert all(len(a) <= 44 and len(b) <= 44 for a, b in help_.values()), help_
    print("help:", len(help_), "rows")

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
    print("hero: ok")


if __name__ == "__main__":
    main()
