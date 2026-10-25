#!/usr/bin/env python3
"""make test-bench3d: reads the bmhost-gpu log of carts/bench3d (the Lua edition of the 3D Bench).
It checks the ramp's lines, the report's R, P, H, C and T lines and that the numbers hang together.
  check.py LOG [--tests a/b/c] [--profiles ARM/VSQ]
LOG is the log of a run with the same `3d_tests` and `3d_profiles` (the defaults: every test, ARM and VSQ).
Prints `bench3d: N/N checks passed` (exit 0) or `bench3d: FAIL <what>` (exit 1)."""
import re
import sys

# the tests of the cartridge, in its order: id -> the profiles (short names) that run it; (carts/bench3d/main.lua TESTS)
ARM_VSQ, ARM_ONLY = ("ARM", "VSQ"), ("ARM",)
ALL3 = ("ARM", "VSQ", "VSQ2")
TESTS = {
    "spheres": ARM_VSQ, "heroes": ARM_VSQ, "heroes_skin": ARM_VSQ, "heroes_shadow": ARM_VSQ, "tiny": ARM_VSQ,
    "draws": ARM_VSQ, "quad_flat": ARM_VSQ, "quad_tex": ARM_VSQ, "quad_alpha": ARM_VSQ, "cel_lit": ARM_VSQ,
    "cel": ARM_VSQ, "cel_outline": ARM_VSQ, "map8": ARM_ONLY, "sprites": ARM_VSQ, "stack": ARM_ONLY,
    "fade": ARM_ONLY, "light": ARM_ONLY, "herotris": ALL3, "ht_p100": ALL3, "ht_p100m": ALL3, "ht_p10m": ALL3,
    "stress": ("VSQ",),
}
NAMES = {"ARM": "ARM", "VSQ": "GPU+VS+Q", "VSQ2": "GPU+VS+Q2"}
QUAD_PX = {"quad_flat", "quad_tex", "quad_alpha", "map8", "sprites", "stack", "fade", "light"}


def main(path, tests, profiles):
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
        check(ns == sorted(ns) and len(set(ns)) == len(ns), f"the loads of {key} grow: {ns}")
        steps = [1, 2, 3, 4]
        if key[0] not in ("sprites",):          # sprites start at 16
            check(ns[0] == 1 and ns[:4] == steps[:len(ns[:4])], f"the first steps of {key} are 1, 2, 3, 4: {ns}")

    # the R lines: 36 fields after the R, the ramp's own samples behind them
    rcols = None
    for l in lines:
        if l.startswith("columns R,"):
            rcols = l[len("columns "):].split(",")
    check(rcols is not None and len(rcols) == 36, "36 R columns")
    rows = {}
    for l in lines:
        if l.startswith("R,"):
            f = l.split(",")
            check(len(f) == len(rcols or []), f"the width of an R line: {l}")
            if len(f) == len(rcols or []):
                d = dict(zip(rcols, f))
                key = (d["test"], d["profile"])
                rows[key] = d
                if key in ramps:
                    last = ramps[key][-1]
                    check(float(d["n60"]) == -1 or float(d["n60"]) <= last, f"n60 is within the ramp {key}")
                check(float(d["ms"]) >= 0, f"the ms of {key}")

    # every test asked for ran on every profile asked for (a profile the console cannot run gives a b3d-skip line)
    for t in tests:
        for p in TESTS[t]:
            if p not in profiles:
                continue
            name = NAMES[p]
            skipped = has(f"b3d-skip {t} {name}")
            if t == "stress":
                check(skipped or (has(f"b3d {t} {name} n=") and (t, name) in rows), f"{t} on {name}")
            else:
                check(skipped or (has(f"b3d {t} {name} n=") and (t, name) in rows), f"{t} on {name}: a ramp line and an R line")
            if t in QUAD_PX and not skipped:
                check(has(f"P,{t},{name},"), f"a pixel cost line of {t} on {name}")
    # the extra sections
    if "herotris" in tests and "VSQ" in profiles and not has("b3d-skip herotris"):
        check(has("columns H,test,profile,sets60"), "the H columns line")
        check(has("H,herotris,GPU+VS+Q,"), "an H line of herotris on the GPU")
    if "cel" in tests and "cel_lit" in tests:
        check(has("columns C,profile,n,tris,lit,cel,lit_nd,cel_nd,outline"), "the C columns line")
        check(has("C,ARM,1,"), "a C line of the ARM")
    if "stress" in tests and "VSQ" in profiles and not has("b3d-skip stress"):
        check(has("columns T,profile,load,n,tris_in,frames"), "the T columns line")
        check(has("T,GPU+VS+Q,1x,") and has("T,GPU+VS+Q,2x,"), "the T lines of the stress test at 1x and 2x")
    check(not any("FAIL" in l and "bench3d" in l for l in lines), "no failure line in the log")
    check(not any(l.startswith("v3d_emu:") or "attempt to" in l for l in lines), "no emulator or Lua error in the log")
    check(has("total "), "the report ends with its total")

    if fails:
        return 1
    print(f"bench3d: {checks}/{checks} checks passed")
    return 0


if __name__ == "__main__":
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    tests, profiles = list(TESTS), ["ARM", "VSQ"]
    if "--tests" in args:
        i = args.index("--tests")
        tests = [t for t in args[i + 1].split("/") if t in TESTS]
        del args[i:i + 2]
    if "--profiles" in args:
        i = args.index("--profiles")
        profiles = args[i + 1].split("/")
        del args[i:i + 2]
    sys.exit(main(args[0], tests, profiles))
