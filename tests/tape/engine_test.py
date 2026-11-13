#!/usr/bin/env python3
"""
engine_test.py - bm/tape/src/engine.b on the stack VM of core/tools/bml/bml.py and as Lua (--lua output, run by the host Lua).

  python3 tests/tape/engine_test.py LUAHOST BUILD_DIR

Both run 300 random inputs per function and must give the same results as a Python reference, array contents included.
euclid is also checked for the pulse count and for an even spread of the gaps, and against known patterns.
song_window runs 500 random cases (random patterns of 4 tracks x 8, slot tables with empty and out-of-range slots, loop in and
out, lengths, swing, mutes, windows across a slot boundary and across the loop end, small capacities); the reference builds
the events of each loop and slot and sorts them, so it does not share the scan of engine.b. PATTERN mode (one slot per track) is
checked against the same reference. A window split at a random point gives the same events as the whole window, and the
function never writes past the capacity.
The native ARMv6 path of the same source is run by tests/tape/render_test.py (the cartridge in bmhost).
"""
import os, random, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "core", "tools", "bml"))
import bml  # noqa: E402

LUAHOST, BUILD = sys.argv[1], sys.argv[2]
os.makedirs(BUILD, exist_ok=True)
SRC = open(os.path.join(ROOT, "bm", "tape", "core", "src", "engine.b")).read()
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


def ref_song(t0, t1, length, swing, slots, stride, lstart, lend, notes, vels, gates, mutes, cap):
    """The events of the ticks t0 .. t1 - 1 as (tick, track, note, velocity, gate), built loop by loop and slot by slot."""
    steps = max(1, min(length, 64))
    sw = max(0, min(swing, 3))
    plen = steps * 6
    st = max(1, stride)
    first = max(0, min(lstart, st - 1))
    last = max(first + 1, min(lend, st))
    total = (last - first) * plen
    ev = []
    for loop in range(max(0, t0) // total, max(0, t1 - 1) // total + 1):
        for j in range(first, last):
            for p in range(steps):
                tick = loop * total + (j - first) * plen + p * 6 + (sw if p % 2 else 0)
                if not (max(0, t0) <= tick < t1):
                    continue
                for k in range(4):
                    q = slots[k * st + j]
                    if mutes[k] or not 1 <= q <= 8:
                        continue
                    for sl in range(4):
                        i = ((k * 8 + q - 1) * 64 + p) * 4 + sl
                        if notes[i] > 0:
                            ev.append((tick, k, sl, notes[i], vels[i], gates[i]))
    ev.sort(key=lambda e: e[:3])
    return [(e[0], e[1]) + e[3:] for e in ev[:max(0, cap)]]


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

NPAT = 4 * 8 * 64 * 4
SQ_SETS = []
for _ in range(10):
    dens = rnd.choice([0.02, 0.1, 0.3, 0.7])
    pat = {"notes": [0] * NPAT, "vels": [0] * NPAT, "gates": [0] * NPAT, "mutes": [rnd.choice([0, 0, 0, 1]) for _ in range(4)]}
    for i in range(NPAT):
        if rnd.random() < dens:
            pat["notes"][i], pat["vels"][i], pat["gates"][i] = rnd.randint(1, 127), rnd.randint(1, 127), rnd.randint(1, 96)
    SQ_SETS.append(pat)
SQ_SETS[0]["mutes"] = [0, 0, 0, 0]
SQ_CAP_EXTRA = 10                                        # sentinel records after the capacity: they must stay as they are


def rand_slots(stride):
    """A slot table of 4 rows: empty slots (0), patterns 1..8, and now and then a value out of range (-1, 9, 40)."""
    pick = rnd.choice([0.2, 0.5])
    return [(rnd.randint(1, 8) if rnd.random() > pick else rnd.choice([0, 0, 0, -1, 9, 40])) for _ in range(4 * stride)]


sq_cases = []
for n in range(520):
    length = rnd.choice([16, 32, 48, 64, rnd.randint(-2, 70)])
    swing = rnd.choice([0, 1, 2, 3, rnd.randint(-2, 6)])
    plen = max(1, min(length, 64)) * 6
    if n < 60:                                           # PATTERN mode: one slot per track, loop 0 to 1
        stride, lstart, lend = 1, 0, 1
    else:
        stride = rnd.choice([2, 4, 6, 8, 64, rnd.randint(1, 64)])
        lstart = rnd.choice([0, rnd.randint(-1, stride), rnd.randint(0, stride - 1)])
        lend = rnd.choice([stride, lstart + 1, rnd.randint(-1, stride + 2), lstart + rnd.randint(1, 5)])
    total = plen * max(1, min(lend, stride) - max(0, min(lstart, stride - 1)))
    t0 = rnd.choice([rnd.randint(-10, 5), rnd.randint(0, 3 * plen), rnd.randint(0, 6 * total), rnd.randint(0, 40 * plen)])
    if n % 3 == 0:                                       # a window that starts just before a slot boundary or the loop end
        t0 = max(0, rnd.randint(1, 6) * plen * rnd.randint(1, 3) - rnd.randint(1, 20))
    t1 = t0 + rnd.choice([0, 1, rnd.randint(1, 30), rnd.randint(1, plen), rnd.randint(1, 3 * plen), rnd.randint(1, 2 * total)])
    cap = rnd.choice([0, 1, 2, rnd.randint(3, 20), 300, 300])
    sq_cases.append((rnd.randrange(len(SQ_SETS)), t0, t1, length, swing, cap, stride, lstart, lend, rand_slots(stride)))

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

vm_sq = []
for c in sq_cases:
    pat = SQ_SETS[c[0]]
    out = [-7] * (5 * c[5] + 5 * SQ_CAP_EXTRA)
    n = vm.call("song_window", [c[1], c[2], c[3], c[4], list(c[9]), c[6], c[7], c[8], list(pat["notes"]), list(pat["vels"]),
                                list(pat["gates"]), list(pat["mutes"]), out, c[5]])
    vm_sq.append((n, out))

# ---------------------------------------------------------------- Lua (stripped source) on the host Lua
lines = ["local abs, min, max, floor, sqrt = math.abs, math.min, math.max, math.floor, math.sqrt", bml.to_lua(SRC)]
lines.append("local function lst(t, n) local s = {} for i = 1, n do s[i] = tostring(t[i]) end return table.concat(s, ',') end")
for k, n, r in eu_cases:
    lines.append("do local o = {} for i = 1, 64 do o[i] = -1 end local p = euclid(%d, %d, %d, o) print(p .. ' ' .. lst(o, 64)) end" % (k, n, r))
for lv, pk, n, dt in mt_cases:
    lines.append("do local a, b = {%s}, {%s} meter_fall(a, b, %d, %d) print(lst(a, %d) .. ' ' .. lst(b, %d)) end"
                 % (",".join(map(str, lv)), ",".join(map(str, pk)), n, dt, n, 2 * n))
for i, pat in enumerate(SQ_SETS):
    lines.append("local PAT%d = {n = {%s}, v = {%s}, g = {%s}, m = {%s}}" % (
        i, ",".join(map(str, pat["notes"])), ",".join(map(str, pat["vels"])), ",".join(map(str, pat["gates"])), ",".join(map(str, pat["mutes"]))))
for c in sq_cases:
    L = 5 * c[5] + 5 * SQ_CAP_EXTRA
    lines.append("do local o = {} for i = 1, %d do o[i] = -7 end local sl = {%s} local n = song_window(%d, %d, %d, %d, sl, %d, %d, %d, PAT%d.n, PAT%d.v, PAT%d.g, PAT%d.m, o, %d) "
                 "print(n .. ' ' .. lst(o, %d)) end" % (L, ",".join(map(str, c[9])), c[1], c[2], c[3], c[4], c[6], c[7], c[8], c[0], c[0], c[0], c[0], c[5], L))
path = os.path.join(BUILD, "engine_test.lua")
open(path, "w").write("\n".join(lines) + "\n")
lua_out = subprocess.check_output([LUAHOST, path], text=True).strip().splitlines()
check("Lua output gives one line per case", len(lua_out) == len(eu_cases) + len(mt_cases) + len(sq_cases))
lua_eu, lua_mt, lua_sq = lua_out[:len(eu_cases)], lua_out[len(eu_cases):len(eu_cases) + len(mt_cases)], lua_out[len(eu_cases) + len(mt_cases):]

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

# song_window
bad_vm = bad_lua = bad_over = bad_ord = 0
for i, c in enumerate(sq_cases):
    pat = SQ_SETS[c[0]]
    want = ref_song(c[1], c[2], c[3], c[4], c[9], c[6], c[7], c[8], pat["notes"], pat["vels"], pat["gates"], pat["mutes"], c[5])
    flat = [x for e in want for x in e]
    out0 = flat + [-7] * (5 * c[5] + 5 * SQ_CAP_EXTRA - len(flat))
    n, out = vm_sq[i]
    bad_vm += (n, out) != (len(want), out0)
    bad_lua += lua_sq[i] != "%d %s" % (len(want), ",".join(map(str, out0)))
    bad_over += any(x != -7 for x in out[5 * c[5]:])
    bad_ord += [e[:2] for e in want] != sorted(e[:2] for e in want)
check("song_window: StackVM equals the Python reference (%d cases)" % len(sq_cases), bad_vm == 0, "%d differ" % bad_vm)
check("song_window: Lua output equals the Python reference", bad_lua == 0, "%d differ" % bad_lua)
check("song_window: nothing is written past the capacity", bad_over == 0, "%d cases" % bad_over)
check("song_window: events come in tick, track order", bad_ord == 0)
check("song_window: the cases hold events, empty windows and full capacities",
      sum(n > 0 for n, _ in vm_sq) > 150 and sum(n == 0 for n, _ in vm_sq) > 20
      and sum(n == c[5] > 0 for (n, _), c in zip(vm_sq, sq_cases)) > 10)
check("song_window: the cases hold PATTERN mode, loops of several slots and windows across the loop end",
      sum(c[6] == 1 for c in sq_cases) >= 60 and sum(c[8] - c[7] > 1 for c in sq_cases) > 150 and
      sum(c[2] - c[1] > (max(1, min(c[8], c[6]) - max(0, min(c[7], c[6] - 1)))) * max(1, min(c[3], 64)) * 6 for c in sq_cases) > 30)

bad_split = bad_loop = bad_swing = 0
split_cases = 0
for _ in range(400):
    pat = rnd.choice(SQ_SETS)
    length, swing = rnd.choice([16, 32, 48, 64, rnd.randint(1, 64)]), rnd.randint(0, 3)
    stride = rnd.choice([1, 4, 8, 64])
    lstart = rnd.randint(0, stride - 1)
    lend = rnd.randint(lstart + 1, stride)
    slots = rand_slots(stride)
    plen = length * 6
    total = (lend - lstart) * plen
    a = rnd.randint(0, 5 * total)
    c = a + rnd.randint(0, min(3 * total, 2500))        # windows across slot boundaries and the loop end, short enough for the VM
    b = rnd.randint(a, c)

    def window(x, y):
        out = [-7] * (5 * 5000)
        n = vm.call("song_window", [x, y, length, swing, list(slots), stride, lstart, lend, list(pat["notes"]), list(pat["vels"]),
                                    list(pat["gates"]), list(pat["mutes"]), out, 5000])
        return [tuple(out[5 * j:5 * j + 5]) for j in range(n)]
    whole, left, right = window(a, c), window(a, b), window(b, c)
    split_cases += 1
    bad_split += whole != left + right
    shifted = window(a + total, c + total)                     # the song loops: one loop later is the same, total ticks on
    bad_loop += [(e[0] - total,) + e[1:] for e in shifted] != whole
    bad_swing += any((e[0] % plen) % 6 != (swing if (e[0] % plen) // 6 % 2 else 0) for e in whole)
check("song_window: a window split at a random point gives the same events (%d cases)" % split_cases, bad_split == 0, "%d differ" % bad_split)
check("song_window: one loop later gives the same events, one loop of ticks on", bad_loop == 0, "%d differ" % bad_loop)
check("song_window: every event is on a step tick, odd steps late by the swing", bad_swing == 0, "%d differ" % bad_swing)

print("tape engine: %s" % ("FAIL" if fails else "ok"))
sys.exit(1 if fails else 0)
