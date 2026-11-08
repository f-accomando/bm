#!/usr/bin/env python3
"""make test-typed: the prototype of the typed subset (scripts/bmt.py).

  run.py LUAHOST BUILD_DIR

1. The compiler refuses programs with a type error, and says the line.
2. hot.bmt gives a Lua file and a C file. Both run the same random cases (doors and enemies of the
   size of Dark Night) and print the same results, byte for byte.
3. The two programs are timed on the same cases. The ratio is printed: it is a measure on this
   PC, not on a Raspberry Pi.
"""
import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bmt  # noqa: E402

LUAHOST = sys.argv[1]
BUILD = sys.argv[2]
os.makedirs(BUILD, exist_ok=True)

# ---------------------------------------------------------------- 1. the types are checked
BAD = [
    ("a float stored in an i32 local",
     "function f(x: f64): i32\n  local n = 1\n  n = x * 2.0\n  return n\nend\n", "line 3"),
    ("a float returned as i32",
     "function f(x: f64): i32\n  return x\nend\n", "line 2"),
    ("a number as a test",
     "function f(n: i32): bool\n  if n then return true end\n  return false\nend\n", "line 2"),
    ("an undeclared name",
     "function f(n: i32): i32\n  return m\nend\n", "line 2"),
    ("a float index",
     "function f(v: f64[], x: f64): f64\n  return v[x]\nend\n", "line 2"),
    ("a whole array used as a number",
     "function f(v: f64[]): f64\n  return v + 1\nend\n", "line 2"),
    ("a number as the test of a while",
     "function f(n: i32): i32\n  while n do\n    n = n - 1\n  end\n  return n\nend\n", "line 2"),
    ("a call of a function that is not in the subset",
     "function f(x: f64): f64\n  return print(x)\nend\n", "line 2"),
]
failed = 0
for what, src, where in BAD:
    try:
        bmt.translate(src)
        print("typed: FAIL accepted: " + what)
        failed += 1
    except bmt.Error as e:
        if where not in str(e):
            print("typed: FAIL wrong line for '%s': %s" % (what, e))
            failed += 1
GOOD = "function f(x: f64, n: i32): f64\n  local s = n\n  local t = 0.5\n  t = s\n  return t + x / n\nend\n"
try:
    bmt.translate(GOOD)          # i32 widens to f64
except bmt.Error as e:
    print("typed: FAIL refused a good program: %s" % e)
    failed += 1

# ---------------------------------------------------------------- 2. one source, two programs
src = os.path.join(HERE, "hot.bmt")
lua_file, c_file = os.path.join(BUILD, "hot.lua"), os.path.join(BUILD, "hot.c")
if bmt.main([src, "--lua", lua_file, "--c", c_file]) != 0:
    sys.exit(1)

rnd = random.Random(20261108)
lines = []
for _ in range(20000):
    if rnd.random() < 0.5:                       # a camera test: 0..8 doors in a 30 x 30 map
        n = rnd.randint(0, 8)
        v = ([rnd.uniform(0, 30) for _ in range(n)], [rnd.uniform(0, 30) for _ in range(n)],
             [rnd.uniform(0.2, 2.0) for _ in range(n)])
        px, pz = rnd.uniform(0, 30), rnd.uniform(0, 30)
        lines.append("C %.17g %.17g %d %s" % (px, pz, n, " ".join("%.17g" % q for a in v for q in a)))
    else:                                         # 17 enemies in a room 5 x 5: many are near
        n = rnd.randint(2, 17)
        xs = [rnd.uniform(0, 5) for _ in range(n)]
        zs = [rnd.uniform(0, 5) for _ in range(n)]
        al = [rnd.randint(0, 1) for _ in range(n)]
        lines.append("P %d %d %s" % (rnd.randint(1, n), n,
                                     " ".join(["%.17g" % q for q in xs + zs] + [str(a) for a in al])))
data = os.path.join(BUILD, "cases.txt")
open(data, "w").write("\n".join(lines) + "\n")

C_MAIN = r'''
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "hot.c"
typedef struct { char k; double x, z; int n, self; double a[17*2+17], r[17]; bool al[17]; } cs;
static cs cases[20000]; static int ncases;
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "r"); char k;
    while (fscanf(f, " %c", &k) == 1) {
        cs *c = &cases[ncases++]; c->k = k;
        if (k == 'C') {
            fscanf(f, "%lf %lf %d", &c->x, &c->z, &c->n);
            for (int i = 0; i < c->n * 3; i++) fscanf(f, "%lf", &c->a[i]);
        } else {
            fscanf(f, "%d %d", &c->self, &c->n);
            for (int i = 0; i < c->n * 2; i++) fscanf(f, "%lf", &c->a[i]);
            for (int i = 0; i < c->n; i++) { int v; fscanf(f, "%d", &v); c->al[i] = v; }
        }
    }
    int reps = atoi(argv[2]); long sum = 0; int32_t out[17];
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int rep = 0; rep < reps; rep++)
        for (int q = 0; q < ncases; q++) {
            cs *c = &cases[q];
            if (c->k == 'C') {
                int n = c->n;
                bool b = bmt_cam_blocked(c->x, c->z, n, c->a, c->a + n, c->a + 2 * n);
                if (rep == 0 && argc > 3) printf("C %d\n", b);
                sum += b;
            } else {
                int n = c->n;
                int m = bmt_near_pairs(c->self, n, c->a, c->a + n, c->al, out);
                if (rep == 0 && argc > 3) { printf("P %d", m); for (int i = 0; i < m; i++) printf(" %d", out[i]); printf("\n"); }
                sum += m;
            }
        }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3;
    fprintf(stderr, "time %.4f us per call (checksum %ld)\n", us / ((double)reps * ncases), sum);
    return 0;
}
'''
LUA_MAIN = r'''
dofile(arg[1])
local reps, show = tonumber(arg[3]), arg[4] == "show"
local cb, np = cam_blocked, near_pairs
if arg[5] == "c" then cb, np = cam_blocked_c, near_pairs_c end   -- the native ones, through the Lua C API
local cases = {}
for line in io.lines(arg[2]) do
  local w = {}
  for t in line:gmatch("%S+") do w[#w + 1] = t end
  local c = { k = w[1] }
  if c.k == "C" then
    c.x, c.z, c.n = tonumber(w[2]), tonumber(w[3]), math.tointeger(tonumber(w[4]))
    c.ox, c.oz, c.r = {}, {}, {}
    for i = 1, c.n do
      c.ox[i] = tonumber(w[4 + i]); c.oz[i] = tonumber(w[4 + c.n + i]); c.r[i] = tonumber(w[4 + 2 * c.n + i])
    end
  else
    c.self, c.n = math.tointeger(tonumber(w[2])), math.tointeger(tonumber(w[3]))
    c.x, c.z, c.alive, c.out = {}, {}, {}, {}
    for i = 1, c.n do
      c.x[i] = tonumber(w[3 + i]); c.z[i] = tonumber(w[3 + c.n + i]); c.alive[i] = w[3 + 2 * c.n + i] == "1"
    end
  end
  cases[#cases + 1] = c
end
local sum, out = 0, {}
local t0 = os.clock()
for rep = 1, reps do
  for q = 1, #cases do
    local c = cases[q]
    if c.k == "C" then
      local b = cb(c.x, c.z, c.n, c.ox, c.oz, c.r)
      if rep == 1 and show then io.write("C ", b and 1 or 0, "\n") end
      if b then sum = sum + 1 end
    else
      local m = np(c.self, c.n, c.x, c.z, c.alive, c.out)
      if rep == 1 and show then
        io.write("P ", m)
        for i = 1, m do io.write(" ", c.out[i]) end
        io.write("\n")
      end
      sum = sum + m
    end
  end
end
io.stderr:write(string.format("time %.4f us per call (checksum %d)\n", (os.clock() - t0) * 1e6 / (reps * #cases), sum))
'''
open(os.path.join(BUILD, "main.c"), "w").write(C_MAIN)
open(os.path.join(BUILD, "main.lua"), "w").write(LUA_MAIN)
exe = os.path.join(BUILD, "hot_c")
subprocess.run(["gcc", "-O2", "-ffp-contract=off", "-Wall", "-Wextra", "-Wno-unused-result", "-I", BUILD, "-o", exe,
                os.path.join(BUILD, "main.c"), "-lm"], check=True)


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
    if r.returncode:
        print(r.stderr)
        sys.exit(1)
    return r.stdout, r.stderr


c_out, _ = run([exe, data, "1", "show"])
l_out, _ = run([LUAHOST, os.path.join(BUILD, "main.lua"), lua_file, data, "1", "show"])
ncases = len(lines)
if c_out != l_out:
    cl, ll = c_out.splitlines(), l_out.splitlines()
    bad = next((i for i in range(min(len(cl), len(ll))) if cl[i] != ll[i]), None)
    print("typed: FAIL the C and the Lua differ at case %s: C '%s' Lua '%s'" % (
        bad, cl[bad] if bad is not None else "", ll[bad] if bad is not None else ""))
    failed += 1
else:
    hits = sum(1 for l in c_out.splitlines() if l.startswith("C 1"))
    pairs = sum(int(l.split()[1]) for l in c_out.splitlines() if l.startswith("P"))
    print("typed: %d cases, the same result from C and from Lua (%d camera hits, %d near pairs)" % (ncases, hits, pairs))

# the loops: while, and break in a while
loops_lua, loops_c = os.path.join(BUILD, "loops.lua"), os.path.join(BUILD, "loops.c")
if bmt.main([os.path.join(HERE, "loops.bmt"), "--lua", loops_lua, "--c", loops_c]) != 0:
    sys.exit(1)
limits = [0.5, 1.0, 1.5, 2.0, 10.0, 1000.0, 123456.789, 1e9]
open(os.path.join(BUILD, "loops_main.c"), "w").write(
    '#include <stdio.h>\n#include "loops.c"\nint main(void)\n{\n'
    '    for (int n = -2; n <= 300; n++) printf("sum %d\\n", bmt_sum_to(n));\n'
    '    double lim[] = {' + ", ".join(repr(x) for x in limits) + '};\n'
    '    for (int i = 0; i < %d; i++) printf("steps %%d\\n", bmt_steps_to_reach(lim[i]));\n' % len(limits) +
    '    for (int n = -3; n <= 5000; n += 7) printf("sq %d\\n", bmt_first_square_over(n));\n'
    '    for (int n = 0; n <= 200; n++) printf("small %d\\n", bmt_squares_of_small(n));\n'
    '    return 0;\n}\n')
open(os.path.join(BUILD, "loops_main.lua"), "w").write(
    'dofile(arg[1])\n'
    'for n = -2, 300 do print("sum " .. sum_to(n)) end\n'
    'for _, l in ipairs({' + ", ".join(repr(x) for x in limits) + '}) do print("steps " .. steps_to_reach(l)) end\n'
    'for n = -3, 5000, 7 do print("sq " .. first_square_over(n)) end\n'
    'for n = 0, 200 do print("small " .. squares_of_small(n)) end\n')
subprocess.run(["gcc", "-O2", "-Wall", "-Wextra", "-I", BUILD, "-o", os.path.join(BUILD, "loops_c"),
                os.path.join(BUILD, "loops_main.c"), "-lm"], check=True)
lc, _ = run([os.path.join(BUILD, "loops_c")])
ll, _ = run([LUAHOST, os.path.join(BUILD, "loops_main.lua"), loops_lua])
if lc != ll:
    cl, ll2 = lc.splitlines(), ll.splitlines()
    bad = next((i for i in range(min(len(cl), len(ll2))) if cl[i] != ll2[i]), None)
    print("typed: FAIL the loops differ: C '%s' Lua '%s'" % (cl[bad] if bad is not None else "?", ll2[bad] if bad is not None else "?"))
    failed += 1
else:
    print("typed: %d results of while loops, the same in C and in Lua" % len(lc.splitlines()))

# ---------------------------------------------------------------- 3. the time
bind = os.path.join(BUILD, "bindhost")
subprocess.run(["gcc", "-O2", "-w", "-I", os.path.join(ROOT, "third_party", "lua"), "-I", BUILD, "-o", bind,
                os.path.join(HERE, "bindhost.c")] + sorted(
                    os.path.join(ROOT, "third_party", "lua", f) for f in os.listdir(os.path.join(ROOT, "third_party", "lua"))
                    if f.endswith(".c")) + ["-lm"], check=True)
b_out, _ = run([bind, os.path.join(BUILD, "main.lua"), lua_file, data, "1", "show", "c"])
if b_out != l_out:
    print("typed: FAIL the native functions called from Lua differ from the Lua ones")
    failed += 1
else:
    print("typed: the native functions called from Lua give the same results too")

_, c_err = run([exe, data, "400"])
_, l_err = run([LUAHOST, os.path.join(BUILD, "main.lua"), lua_file, data, "10"])
c_us = float(c_err.split()[1])
l_us = float(l_err.split()[1])
_, b_err = run([bind, os.path.join(BUILD, "main.lua"), lua_file, data, "10", "-", "c"])
b_us = float(b_err.split()[1])
print("typed: Lua %.3f us a call, C %.4f us a call: C is %.0f times faster on this PC (x86, not a Pi)" % (
    l_us, c_us, l_us / c_us))
print("typed: called from Lua with the game's tables (the numbers are copied each call): %.3f us a call, %.1f times faster than the Lua" % (
    b_us, l_us / b_us))
# ---------------------------------------------------------------- 4. a whole frame of 17 enemies
frame = os.path.join(HERE, "frame.lua")
res = {}
for mode in "ABC":
    out, _ = run([bind, frame, "200", mode])
    m, us, pn, hs = out.split()
    res[m] = (float(us), int(pn), int(hs))
if len({(v[1], v[2]) for v in res.values()}) != 1:
    print("typed: FAIL the three frames do not give the same sums: %s" % res)
    failed += 1
else:
    print("typed: a frame of 17 enemies (move, find the pairs, read the hits), %d pairs in 200 frames, the same sums in all three:" % res["A"][1])
    print("typed:   A tables and Lua scan %.2f us   B data in C, a call for each enemy %.2f us (%.1f times)   C data in C, one call %.2f us (%.1f times)" % (
        res["A"][0], res["B"][0], res["A"][0] / res["B"][0], res["C"][0], res["A"][0] / res["C"][0]))
total = len(BAD) + 5          # the bad programs, the good one, the C against the Lua, the native functions against the Lua, the three frames
print("typed: %d/%d checks passed" % (total - failed, total) if not failed else "typed: FAILED, %d of %d checks" % (failed, total))
sys.exit(1 if failed else 0)
