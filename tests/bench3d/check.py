#!/usr/bin/env python3
"""make test-bench3d: reads the bmhost-gpu log of carts/bench3d (the Lua edition of the 3D Bench).
It checks the ramp's lines, the report's R lines and that the numbers hang together.
  check.py LOG   prints `bench3d: N/N checks passed` (exit 0) or `bench3d: FAIL <what>` (exit 1)"""
import re
import sys


def main(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    lines = text.splitlines()
    checks = fails = 0

    def check(ok, what):
        nonlocal checks, fails
        checks += 1
        if not ok:
            fails += 1
            print(f"bench3d: FAIL {what}")

    def has(prefix):
        return any(l.startswith(prefix) for l in lines)

    check(has("b3d spheres ARM n="), "a ramp line of spheres on the ARM")
    check(has("b3d quad_flat GPU n="), "a ramp line of quad_flat on the GPU")
    check(has("R,spheres,ARM"), "the report line of spheres on the ARM")
    check(has("R,quad_flat,GPU"), "the report line of quad_flat on the GPU")
    check(has("columns R,test,profile,version,n60,n30,over,ms"), "the R columns line")
    check(has("bm 3D Bench (Lua edition)"), "the report's first line")

    # the line of b3d.c: b3d <id> <profile> n=N X.XX ms worst X.XX, T tri, V vtx, I instr, GPU X.XX ms, J jobs, ...
    pat = re.compile(r"^b3d (\w+) (\S+) n=(\d+) (\d+)\.(\d\d) ms worst (\d+)\.(\d\d), (\d+) tri, (\d+) vtx, (\d+) instr, "
                     r"GPU (\d+)\.(\d\d) ms, (\d+) jobs, (\d+) meshes, core (\d+) GPU (\d+) MHz$")
    ramps = {}
    for l in lines:
        if l.startswith("b3d ") and " n=" in l:
            m = pat.match(l)
            check(m is not None, f"the format of a ramp line: {l}")
            if m:
                ramps.setdefault((m.group(1), m.group(2)), []).append(int(m.group(3)))
    for key, ns in ramps.items():
        check(ns[0] == 1 and ns == sorted(ns) and len(set(ns)) == len(ns), f"the loads of {key} grow: {ns}")
        steps = [1, 2, 3, 4]
        check(ns[:4] == steps[:len(ns[:4])], f"the first steps of {key} are 1, 2, 3, 4: {ns}")

    # the R lines: 36 fields after the R, the ramp's own samples behind them
    rcols = None
    for l in lines:
        if l.startswith("columns R,"):
            rcols = l[len("columns "):].split(",")
    check(rcols is not None and len(rcols) == 36, "36 R columns")
    for l in lines:
        if l.startswith("R,"):
            f = l.split(",")
            check(len(f) == len(rcols or []), f"the width of an R line: {l}")
            if len(f) == len(rcols or []):
                d = dict(zip(rcols, f))
                key = (d["test"], d["profile"])
                if key in ramps:
                    last = ramps[key][-1]
                    check(float(d["n60"]) == -1 or float(d["n60"]) <= last, f"n60 is within the ramp {key}")
                check(float(d["ms"]) >= 0, f"the ms of {key}")
    check(has("P,quad_flat,GPU") or has("P,quad_flat,ARM"), "a pixel cost line of quad_flat")
    check(not any("FAIL" in l and "bench3d" in l for l in lines), "no failure line in the log")
    check(has("total "), "the report ends with its total")

    if fails:
        return 1
    print(f"bench3d: {checks}/{checks} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
