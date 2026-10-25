#!/usr/bin/env python3
"""Scores the Lua movement against the emulator traces, and fits its constants.

    python3 scripts/smw_physics_check.py TRACE_DIR [SCENARIO]          # the table (SCENARIO: side by side)
    python3 scripts/smw_physics_check.py TRACE_DIR --fit NAME[,NAME]   # coordinate descent on Phys.NAME

Builds a cartridge from carts/smw/src/physics.lua and tests/smw/trace_test.lua, runs it in
bmhost (make build/host/bmhost-bin first) and compares x, y and both speeds with the CSV
files of scripts/smw_trace.py. The scenarios are in tests/smw/trace_scripts.json. A scenario
is cut where the emulator run meets an enemy (LIMIT). --fit prints the constants it found."""
import csv, json, os, subprocess, sys, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIMIT = {"runjump": 164, "airturn": 85, "cycle": 108}       # frames before Mario meets something the test does not have


def run_sim(overrides=None):
    tmp = tempfile.mkdtemp()
    scripts = json.load(open(ROOT + "/tests/smw/trace_scripts.json"))
    table = "local SCRIPTS = {\n" + "".join("  { %s, { %s } },\n" % (
        json.dumps(n), ", ".join("{ %d, %s }" % (f, json.dumps(k)) for f, k in segs)) for n, segs in scripts.items()) + "}\n"
    pre = "".join("Phys.%s = %r\n" % (k, v) for k, v in (overrides or {}).items())
    lua = os.path.join(tmp, "main.lua")
    open(lua, "w").write(open(ROOT + "/carts/smw/src/physics.lua").read() + "\n" + pre +
                         open(ROOT + "/tests/smw/trace_test.lua").read().replace("--SCRIPTS--", table))
    cart = os.path.join(tmp, "t.bm")
    subprocess.run([sys.executable, ROOT + "/scripts/mkbm.py", "-o", cart, "--lua", lua, "--title", "t"],
                   check=True, capture_output=True)
    out = subprocess.run([ROOT + "/build/host/bmhost-bin", cart, "--seconds", "2"], capture_output=True, text=True)
    got = {}
    for line in (out.stdout + out.stderr).splitlines():
        if "TRACE " in line:
            p = line[line.index("TRACE ") + 6:].split()
            got.setdefault(p[0], []).append([float(v) for v in p[2:6]] + [float(p[6])])
    return got, list(scripts)


def errors(got, ref, name):
    rows = list(csv.DictReader(open("%s/trace_%s.csv" % (ref, name))))[:LIMIT.get(name, 10 ** 9)]
    g = got.get(name, [])
    return [[abs(g[i][0] - float(r["x"])), abs(g[i][1] - float(r["y"])), abs(g[i][2] - float(r["vx"])),
             abs(g[i][3] - float(r["vy"])) if r["air"] != "0" else 0.0] for i, r in enumerate(rows) if i < len(g)]


def loss(got, ref, names):
    total = 0.0
    for n in names:
        e = errors(got, ref, n)
        if e:
            total += sum(0.5 * a[0] + a[1] + a[2] + a[3] for a in e) / len(e)
    return total


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    ref = sys.argv[1]
    got, names = run_sim()
    if "--fit" in sys.argv:
        params = sys.argv[sys.argv.index("--fit") + 1].split(",")
        src = open(ROOT + "/carts/smw/src/physics.lua").read()
        cur = {}
        for p in params:
            line = [l for l in src.splitlines() if l.strip().startswith(p + " =")][0]
            cur[p] = float(line.split("=")[1].split(",")[0])
        best = loss(got, ref, names)
        print("start", round(best, 3), cur)
        for rnd in range(3):
            step = 0.1 / (rnd + 1)
            for p in params:
                for f in (1 - step, 1 + step):
                    trial = dict(cur)
                    trial[p] = round(cur[p] * f, 3)
                    l = loss(run_sim(trial)[0], ref, names)
                    if l < best - 1e-6:
                        best, cur = l, trial
            print("round", rnd, round(best, 3), cur)
        return
    show = sys.argv[2] if len(sys.argv) > 2 else None
    tot = 0
    for name in names:
        rows = list(csv.DictReader(open("%s/trace_%s.csv" % (ref, name))))
        g = got.get(name)
        if not g:
            print(name, "no output"); continue
        if show == name:
            for i, r in enumerate(rows):
                if i < len(g) and i % 6 == 0:
                    print("%3d %-4s x %4s/%7.1f  y %4s/%6.1f  vx %3s/%6.1f  vy %4s/%6.1f  pm %3s/%5.1f" % (
                        i, r["keys"], r["x"], g[i][0], r["y"], g[i][1], r["vx"], g[i][2], r["vy"], g[i][3], r["pmeter"], g[i][4]))
        err = errors(got, ref, name)
        n = len(err)
        tot += sum(0.5 * a[0] + a[1] + a[2] + a[3] for a in err) / n
        print("%-8s mean err x %.2f y %.2f vx %.2f vy %.2f | worst x %.1f y %.1f vx %.1f vy %.1f" % (
            (name,) + tuple(sum(e[j] for e in err) / n for j in range(4)) + tuple(max(e[j] for e in err) for j in range(4))))
    print("loss", round(tot, 3))


if __name__ == "__main__":
    main()
