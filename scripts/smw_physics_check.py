#!/usr/bin/env python3
"""Scores the Lua movement against the emulator traces.

    python3 scripts/smw_physics_check.py TRACE_DIR [--quiet]

Builds a cartridge from carts/smw/src/physics.lua and tests/smw/trace_test.lua, runs it in
bmhost (make build/host/bmhost-bin first) and prints, per script, the mean and worst error of
x, y and the two speeds against the CSV files of scripts/smw_trace.py."""
import csv, os, subprocess, sys, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    ref = sys.argv[1]
    tmp = tempfile.mkdtemp()
    lua = os.path.join(tmp, "main.lua")
    open(lua, "w").write(open(ROOT + "/carts/smw/src/physics.lua").read() + "\n" +
                         open(ROOT + "/tests/smw/trace_test.lua").read())
    cart = os.path.join(tmp, "t.bm")
    subprocess.run([sys.executable, ROOT + "/scripts/mkbm.py", "-o", cart, "--lua", lua, "--title", "t"],
                   check=True, capture_output=True)
    out = subprocess.run([ROOT + "/build/host/bmhost-bin", cart, "--seconds", "2"], capture_output=True, text=True)
    got = {}
    for line in (out.stdout + out.stderr).splitlines():
        if "TRACE " in line:
            p = line[line.index("TRACE ") + 6:].split()
            got.setdefault(p[0], []).append([float(v) for v in p[2:6]] + [float(p[6])])
    show = sys.argv[2] if len(sys.argv) > 2 else None
    for name in ("walk", "run", "jump", "runjump", "turn"):
        rows = list(csv.DictReader(open("%s/trace_%s.csv" % (ref, name))))
        g = got.get(name)
        if not g:
            print(name, "no output"); continue
        if show == name:
            for i, r in enumerate(rows):
                if i < len(g) and i % 6 == 0:
                    print("%3d %-4s x %4s/%7.1f  y %4s/%6.1f  vx %3s/%6.1f  vy %4s/%6.1f  pm %3s/%5.1f" % (
                        i, r["keys"], r["x"], g[i][0], r["y"], g[i][1], r["vx"], g[i][2], r["vy"], g[i][3], r["pmeter"], g[i][4]))
        err = [[abs(g[i][0] - float(r["x"])), abs(g[i][1] - float(r["y"])), abs(g[i][2] - float(r["vx"])),
                abs(g[i][3] - float(r["vy"])) if r["air"] != "0" else 0.0]
               for i, r in enumerate(rows) if i < len(g)]
        n = len(err)
        print("%-8s mean err x %.2f y %.2f vx %.2f vy %.2f | worst x %.1f y %.1f vx %.1f vy %.1f" % (
            (name,) + tuple(sum(e[j] for e in err) / n for j in range(4)) + tuple(max(e[j] for e in err) for j in range(4))))


if __name__ == "__main__":
    main()
