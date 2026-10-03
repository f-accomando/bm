#!/usr/bin/env python3
"""ARM instructions to draw the heroes of Overbit (tests/bm/herobench.c), on
the ARM rasterizer and with the GPU backend (src/gpu/gpu3d.c, its V3D
emulated and not counted): qemu-arm runs herobench built for the ARM1176,
its translated blocks are counted by function (tests/overbit/framecount.c)
and the instructions of r3d.c, gfx16.c, gpu3d.c and v3d_cl.c are added up
for one frame (two frames minus one).

    tests/bm/herocost.py [CART.bm] [MODEL ...]

For each model: 1 and 8 copies at 6 and 15 m, levels of detail 0 to 3. The
instructions are the work of the CPU, not the time: on the Pi one of them
took about 2.2 ns in the 3D of the stress test (cache and bus included), see
docs/M33-PRIMA-DOPO.md (section 7). Needs arm-linux-gnueabihf-gcc and qemu-arm."""
import collections
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tests/overbit"))
import count_insns as ci          # noqa: E402  (the compiler and its flags)
import frames                     # noqa: E402  (the counter of the blocks, the files of the functions)

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
    return exe


def trace(exe, args):
    """the instructions of each function in a run (framecount.c, by blocks)"""
    tmp = tempfile.mkdtemp(prefix="herocost-")
    fifo = os.path.join(tmp, "log")
    os.mkfifo(fifo)
    q = subprocess.Popen(["qemu-arm", "-E", "BENCH_EMU_SKIP=1", "-d", "in_asm,exec,nochain", "-D", fifo, exe, *args],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with open(fifo) as fin:
        out = subprocess.run([frames.framecount(), exe + ".tbl", "0"], stdin=fin, capture_output=True, text=True,
                             check=True).stdout
    q.wait()
    os.unlink(fifo)
    os.rmdir(tmp)
    tot = collections.Counter()
    for line in out.splitlines():
        fn, n, _ = line.split(",", 1)[1].rsplit(",", 2)
        tot[fn] += int(n)
    return tot


def count(exe, files, cart, model, copies, dist, detail, gpu):
    e = exe + ("-gpu" if gpu else "")
    base = [cart, model, str(copies), str(dist), str(detail), str(gpu)]
    two, one = trace(e, base + ["2"]), trace(e, base + ["1"])
    where = files[e]
    total = 0
    for fn, n in two.items():
        f = where.get(fn) or where.get(fn.split(".")[0], "")
        if f.endswith(COUNTED) and not fn.startswith(SETUP):
            total += n - one.get(fn, 0)
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
        exe = build(tmp)
        frames.framecount()
        files = {}
        for e in (exe, exe + "-gpu"):
            frames.table(e, e + ".tbl", None)
            files[e] = frames.files_of(e)
        jobs = [(m, n, d, lod, gpu) for m in models for n, d in ((1, 6), (8, 15)) for lod in (0, 1, 2, 3)
                for gpu in (0, 1)]
        with ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
            res = list(pool.map(lambda j: count(exe, files, cart, *j), jobs))
        print(f"{'model':12s} {'n':>2s} {'m':>3s} {'lod':>3s} {'gpu':>3s} {'tris in':>7s} {'drawn':>6s} "
              f"{'instr':>9s} {'per hero':>9s} {'per tri in':>10s}")
        for (m, n, d, lod, gpu), (tot, tin, drawn) in zip(jobs, res):
            print(f"{m:12s} {n:2d} {d:3d} {lod:3d} {'GPU' if gpu else 'ARM':>3s} {tin:7d} {drawn:6d} "
                  f"{tot:9d} {tot // n:9d} {tot / max(tin, 1):10.0f}")


if __name__ == "__main__":
    main()
