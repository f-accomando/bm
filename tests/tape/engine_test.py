#!/usr/bin/env python3
"""
engine_test.py - carts/tape/src/engine.b on the stack VM of tools/bml/bml.py and as Lua (--lua output, run by the host Lua).

  python3 tests/tape/engine_test.py LUAHOST BUILD_DIR

Both run 300 random inputs per function and must give the same results as a Python reference, array contents included.
euclid is also checked for the pulse count and for an even spread of the gaps, and against known patterns.
The native ARMv6 path of the same source is run by tests/tape/render_test.py (the cartridge in bmhost).
"""
import os, random, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "bml"))
import bml  # noqa: E402

LUAHOST, BUILD = sys.argv[1], sys.argv[2]
os.makedirs(BUILD, exist_ok=True)
SRC = open(os.path.join(ROOT, "carts", "tape", "src", "engine.b")).read()
N = 300
fails = []


def check(name, ok, detail=""):
    if not ok:
        fails.append(name)
    print(("ok   " if ok else "FAIL ") + name + ("  " + detail if detail and not ok else ""))


# ---------------------------------------------------------------- Python references
def ref_euclid(k, n, r):
    steps = max(1, min(n, 64))
    pulses = max(0, min(k, steps))
    pat, acc = [], 0                        # acc = (i * pulses) % steps, kept by addition
    for _ in range(steps):
        pat.append(1 if acc < pulses else 0)
        acc = (acc + pulses) % steps
    s = (-r) % steps                        # out[(i + r) % steps] = pat[i]: a right rotation by r
    rot = pat[s:] + pat[:s]
    return pulses, rot


def ref_meter(levels, peaks, n, dt):
    for i in range(n):
        lv = max(0, levels[i] - dt * 4 // 5)
        levels[i] = lv
        if lv >= peaks[i]:
            peaks[i] = lv
            peaks[n + i] = 600
        elif peaks[n + i] > 0:
            peaks[n + i] = max(0, peaks[n + i] - dt)
        else:
            peaks[i] = max(lv, peaks[i] - dt * 3 // 10)


# ---------------------------------------------------------------- random cases
rnd = random.Random(1234)
eu_cases = [(rnd.randint(-3, 70), rnd.randint(-2, 70), rnd.randint(-100, 100)) for _ in range(N - 10)]
eu_cases += [(0, 16, 0), (16, 16, 3), (3, 8, 0), (5, 8, 0), (4, 16, 0), (7, 12, 0), (1, 1, 5), (64, 64, -1), (9, 16, 15), (6, 16, 0)]
mt_cases = []
for _ in range(N):
    n = rnd.randint(1, 8)
    lv = [rnd.randint(0, 1200) for _ in range(n)]
    pk = [rnd.randint(0, 1200) for _ in range(n)] + [rnd.choice([0, rnd.randint(0, 700)]) for _ in range(n)]
    mt_cases.append((lv, pk, n, rnd.choice([rnd.randint(0, 200), rnd.randint(0, 5000)])))

# ---------------------------------------------------------------- stack VM
vm = bml.StackVM(bml.compile_source(SRC))
vm_eu, vm_mt = [], []
for k, n, r in eu_cases:
    out = [-1] * 64
    vm_eu.append((vm.call("euclid", [k, n, r, out]), out))
for lv, pk, n, dt in mt_cases:
    a, b = list(lv), list(pk)
    vm.call("meter_fall", [a, b, n, dt])
    vm_mt.append((a, b))

# ---------------------------------------------------------------- Lua (stripped source) on the host Lua
lines = ["local abs, min, max, floor, sqrt = math.abs, math.min, math.max, math.floor, math.sqrt", bml.to_lua(SRC)]
lines.append("local function lst(t, n) local s = {} for i = 1, n do s[i] = tostring(t[i]) end return table.concat(s, ',') end")
for k, n, r in eu_cases:
    lines.append("do local o = {} for i = 1, 64 do o[i] = -1 end local p = euclid(%d, %d, %d, o) print(p .. ' ' .. lst(o, 64)) end" % (k, n, r))
for lv, pk, n, dt in mt_cases:
    lines.append("do local a, b = {%s}, {%s} meter_fall(a, b, %d, %d) print(lst(a, %d) .. ' ' .. lst(b, %d)) end"
                 % (",".join(map(str, lv)), ",".join(map(str, pk)), n, dt, n, 2 * n))
path = os.path.join(BUILD, "engine_test.lua")
open(path, "w").write("\n".join(lines) + "\n")
lua_out = subprocess.check_output([LUAHOST, path], text=True).strip().splitlines()
check("Lua output gives one line per case", len(lua_out) == len(eu_cases) + len(mt_cases))
lua_eu, lua_mt = lua_out[:len(eu_cases)], lua_out[len(eu_cases):]

# ---------------------------------------------------------------- compare
def fmt(p, arr): return "%d %s" % (p, ",".join(map(str, arr)))

bad_vm = bad_lua = bad_gap = bad_count = 0
for i, (k, n, r) in enumerate(eu_cases):
    pulses, rot = ref_euclid(k, n, r)
    want = fmt(pulses, rot + [-1] * (64 - len(rot)))
    vp, vo = vm_eu[i]
    bad_vm += fmt(vp, vo) != want
    bad_lua += lua_eu[i] != want
    bad_count += sum(rot) != pulses
    ones = [j for j, x in enumerate(rot) if x]                      # the gaps between pulses differ by 1 at most
    if len(ones) > 1:
        gaps = [(ones[(j + 1) % len(ones)] - ones[j]) % len(rot) or len(rot) for j in range(len(ones))]
        bad_gap += max(gaps) - min(gaps) > 1
check("euclid: StackVM equals the Python reference (%d inputs)" % len(eu_cases), bad_vm == 0, "%d differ" % bad_vm)
check("euclid: Lua output equals the Python reference", bad_lua == 0, "%d differ" % bad_lua)
check("euclid: the pulse count is k clamped to 0..n", bad_count == 0)
check("euclid: the gaps between pulses differ by 1 at most", bad_gap == 0)
known = {(3, 8): "10010010", (5, 8): "10101101", (4, 16): "1000100010001000", (7, 12): "101010110101"}
check("euclid: known patterns", all("".join(map(str, ref_euclid(k, n, 0)[1])) == p and
                                    "".join(str(x) for x in vm_eu[eu_cases.index((k, n, 0))][1][:n]) == p
                                    for (k, n), p in known.items()))

bad_vm = bad_lua = 0
for i, (lv, pk, n, dt) in enumerate(mt_cases):
    a, b = list(lv), list(pk)
    ref_meter(a, b, n, dt)
    bad_vm += vm_mt[i] != (a, b)
    bad_lua += lua_mt[i] != "%s %s" % (",".join(map(str, a)), ",".join(map(str, b)))
check("meter_fall: StackVM equals the Python reference (%d inputs)" % len(mt_cases), bad_vm == 0, "%d differ" % bad_vm)
check("meter_fall: Lua output equals the Python reference", bad_lua == 0, "%d differ" % bad_lua)
check("meter_fall: a level never goes below 0 and never rises",
      all(0 <= x <= y for (lv, _, n, _), (a, _) in zip(mt_cases, vm_mt) for x, y in zip(a, lv)))

print("tape engine: %s" % ("FAIL" if fails else "ok"))
sys.exit(1 if fails else 0)
