#!/usr/bin/env python3
"""The trial of Pallene (docs/design/typed-native.md): the same two hot functions and the same frame of 17 enemies.

  run.py PALLENE_PREFIX BUILD_DIR

PALLENE_PREFIX is WORKDIR/prefix of setup.sh. BUILD_DIR is the build/typed of `make test-typed`, which has the random cases
and the Lua harness. The script
  1. compiles hot.pln and frame.pln with pallenec;
  2. runs the 20000 random cases through the Pallene functions and compares every result with the Lua ones;
  3. times a frame of 17 enemies in four ways (A: Lua tables, P1: arrays of numbers, P2: tables as the game, P3: the move and the
     scan in Pallene) and compares the sums.
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PREFIX = os.path.abspath(sys.argv[1])
BUILD = os.path.abspath(sys.argv[2])
PLUA = os.path.join(PREFIX, "bin", "plua")
PALLENEC = os.path.join(PREFIX, "bin", "pallenec")
for need in (PLUA, PALLENEC, os.path.join(BUILD, "cases.txt"), os.path.join(BUILD, "main.lua"), os.path.join(BUILD, "hot.lua")):
    if not os.path.exists(need):
        sys.exit("missing %s (run tests/typed/pallene/setup.sh, and `make test-typed` for the cases)" % need)

work = os.path.join(BUILD, "pallene")
shutil.rmtree(work, ignore_errors=True)
os.makedirs(work)
for f in ("hot.pln", "frame.pln", "frame_bench.lua", "wrapper.lua", "parts.pln"):
    shutil.copy(os.path.join(HERE, f), work)
shutil.copy(os.path.join(HERE, "..", "parts_bench.lua"), work)
env = dict(os.environ, LUA_CPATH="./?.so;;")


def run(cmd):
    r = subprocess.run(cmd, cwd=work, env=env, capture_output=True, text=True, timeout=600)
    if r.returncode:
        print(r.stdout, r.stderr)
        sys.exit(1)
    return r.stdout, r.stderr


for m in ("hot.pln", "frame.pln", "parts.pln"):
    run([PALLENEC, m])

failed = 0
cases = os.path.join(BUILD, "cases.txt")
main = os.path.join(BUILD, "main.lua")
hot = os.path.join(BUILD, "hot.lua")
lua_out, _ = run([PLUA, main, hot, cases, "1", "show"])
pal_out, _ = run([PLUA, "wrapper.lua", hot, cases, "1", "show", "c", main])
if lua_out != pal_out:
    print("pallene: FAIL the results differ from the Lua ones")
    failed += 1
else:
    print("pallene: %d random cases, the same result from Pallene and from Lua" % len(pal_out.splitlines()))

rows = {}
for mode in ("A", "P1", "P2", "P3"):
    out, _ = run([PLUA, "frame_bench.lua", "300", mode])
    m, us, pn, hs = out.split()
    rows[m] = (float(us), int(pn), int(hs))
if len({(v[1], v[2]) for v in rows.values()}) != 1:
    print("pallene: FAIL the frames do not give the same sums: %s" % rows)
    failed += 1
else:
    a = rows["A"][0]
    print("pallene: a frame of 17 enemies, %d pairs in 200 frames, the same sums in all four:" % rows["A"][1])
    print("pallene:   A Lua tables %.2f us | P1 arrays of numbers %.2f us (%.1f times) | P2 tables as the game %.2f us (%.1f times) | "
          "P3 move and scan in Pallene %.2f us (%.1f times)" % (
              a, rows["P1"][0], a / rows["P1"][0], rows["P2"][0], a / rows["P2"][0], rows["P3"][0], a / rows["P3"][0]))
# the particles of Yharnam (update_parts): the function of the game in Lua, and in Pallene on the same tables
prow = {}
for mode in ("A", "P"):
    out, _ = run([PLUA, "parts_bench.lua", "30", mode])
    m, us, n, sm = out.split()
    prow[m] = (float(us), int(n), float(sm))
if (prow["A"][1], prow["A"][2]) != (prow["P"][1], prow["P"][2]):
    print("pallene: FAIL the particles differ: %s" % prow)
    failed += 1
else:
    print("pallene: particles of Yharnam (up to 160, 600 frames), the same count and sums: update_parts Lua %.2f us, Pallene on the "
          "tables of the game %.2f us (%.1f times)" % (prow["A"][0], prow["P"][0], prow["A"][0] / prow["P"][0]))
print("pallene: %s" % ("FAILED" if failed else "3/3 checks passed"))
sys.exit(1 if failed else 0)
