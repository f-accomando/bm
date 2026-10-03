#!/usr/bin/env python3
"""ARM instructions to draw the heroes of Overbit (tests/bm/herobench.c), on
the ARM rasterizer and with the GPU backend (src/gpu/gpu3d.c, its V3D
emulated and not counted), as tests/bm/count_insns.py does for bench3d:
qemu-arm runs herobench built for the ARM1176, one instruction per block,
and the instructions of r3d.c, gfx16.c, gpu3d.c and v3d_cl.c are added up
for one frame (two frames minus one).

    tests/bm/herocost.py [CART.bm] [MODEL ...]

For each model: 1 and 8 copies at 6 and 15 m, levels of detail 0 to 3. The
instructions are the work of the CPU, not the time: on the Pi one of them
took about 2.2 ns in the 3D of the stress test (cache and bus included), see
docs/LIMITI.md. Needs arm-linux-gnueabihf-gcc and qemu-arm."""
import bisect
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import count_insns as ci          # noqa: E402  (the build of the histogram, symbols)

COUNTED = ("r3d.c", "gfx16.c", "gpu3d.c", "v3d_cl.c")
SETUP = ("r3d_mesh", "r3d_init", "g16_sheet", "g16_cls", "gpu3d_init", "probe", "block_alloc", "load_model",
         "bm_", "rig_")


def build(tmp):
    exe = os.path.join(tmp, "herobench.arm")
    inc = ["-I" + os.path.join(ROOT, "src"), "-I" + os.path.join(ROOT, "tests/gpu")]
    src = [os.path.join(ROOT, p) for p in ("tests/bm/herobench.c", "src/bm/format.c", "src/bm/r3d.c",
                                            "src/bm/gfx16.c", "src/lib/crc32.c")]
    subprocess.run([ci.CC, *ci.CFLAGS, *inc, "-o", exe, *src, "-lm"], check=True)
    gobj = os.path.join(tmp, "gpu3d.o")
    subprocess.run([ci.CC, *ci.CFLAGS, *inc, "-Daligned_alloc=test_aligned_alloc", "-Dfree=test_free", "-c",
                    os.path.join(ROOT, "src/gpu/gpu3d.c"), "-o", gobj], check=True)
    gsrc = src + [os.path.join(ROOT, p) for p in ("tests/gpu/v3d_emu.c", "src/gpu/v3d_cl.c")]
    subprocess.run([ci.CC, *ci.CFLAGS, *inc, "-DBENCH_GPU", "-o", exe + "-gpu", *gsrc, gobj, "-lm"], check=True)
    return exe, ci.build_hist(tmp)


def trace(exe, hist, args):
    t = subprocess.Popen(["qemu-arm", "-E", "BENCH_EMU_SKIP=1", "-one-insn-per-tb", "-d", "exec,nochain",
                          "-D", "/dev/stdout", exe, *args], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    counted = subprocess.run([hist], stdin=t.stdout, capture_output=True, text=True, check=True)
    t.wait()
    return {int(pc, 16): int(n) for pc, n in (line.split() for line in counted.stdout.splitlines())}


def count(exe, syms, hist, cart, model, copies, dist, detail, gpu):
    e = exe + ("-gpu" if gpu else "")
    base = [cart, model, str(copies), str(dist), str(detail), str(gpu)]
    two, one = trace(e, hist, base + ["2"]), trace(e, hist, base + ["1"])
    addrs, table = syms[e]
    total = 0
    for pc, n in two.items():
        n -= one.get(pc, 0)
        i = bisect.bisect_right(addrs, pc) - 1
        if i < 0:
            continue
        _, name, where = table[i]
        if any(where.split(":")[0].endswith(f) for f in COUNTED) and not name.startswith(SETUP):
            total += n
    out = subprocess.run(["qemu-arm", e, *base, "1"], capture_output=True, text=True, check=True).stdout
    words = out.split()
    tin, drawn = int(words[words.index("triangles") - 1]), int(words[words.index("drawn,") - 1])
    return total, tin, drawn


def main():
    args = sys.argv[1:]
    cart = os.path.join(ROOT, "build/overbit/models.bm")
    if args and args[0].endswith(".bm"):
        cart = args.pop(0)
    models = args or ["rally_mech", "sarge", "frost", "akari"]
    with tempfile.TemporaryDirectory() as tmp:
        exe, hist = build(tmp)
        syms = {}
        for e in (exe, exe + "-gpu"):
            s = ci.symbols(e)
            syms[e] = ([x[0] for x in s], s)
        jobs = [(m, n, d, lod, gpu) for m in models for n, d in ((1, 6), (8, 15)) for lod in (0, 1, 2, 3)
                for gpu in (0, 1)]
        with ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
            res = list(pool.map(lambda j: count(exe, syms, hist, cart, *j), jobs))
        print(f"{'model':12s} {'n':>2s} {'m':>3s} {'lod':>3s} {'gpu':>3s} {'tris in':>7s} {'drawn':>6s} "
              f"{'instr':>9s} {'per hero':>9s} {'per tri in':>10s}")
        for (m, n, d, lod, gpu), (tot, tin, drawn) in zip(jobs, res):
            print(f"{m:12s} {n:2d} {d:3d} {lod:3d} {'GPU' if gpu else 'ARM':>3s} {tin:7d} {drawn:6d} "
                  f"{tot:9d} {tot // n:9d} {tot / max(tin, 1):10.0f}")


if __name__ == "__main__":
    main()
