#!/usr/bin/env python3
"""Counts the ARM instructions the rasterizer executes for the scenes of
tests/bm/bench3d.c (M33): builds bench3d for ARM (ARM1176, VFP, -O2), runs
each scene under qemu-arm with one instruction per translation block and
the exec trace, and adds up the instructions of the functions of r3d.c and
gfx16.c. Cache and bus are not simulated: the numbers are the work of the
CPU, not the time on the Pi.

    tests/bm/count_insns.py [SCENE ...]      default: the main scenes
    tests/bm/count_insns.py --hot SCENE      the 30 busiest instructions

A scene named SCENE+gpu is drawn by the GPU backend (src/gpu/gpu3d.c) on
the V3D emulator (tests/gpu/v3d_emu.c, not counted): the ARM's share of
the 3D with the GPU. Its counts are those of a second frame (two frames
minus one), when the textures are already made.

Needs arm-linux-gnueabihf-gcc and qemu-arm (Ubuntu: gcc-arm-linux-gnueabihf,
qemu-user)."""
import bisect
import collections
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CC = "arm-linux-gnueabihf-gcc"
CFLAGS = ["-mcpu=arm1176jzf-s", "-marm", "-mfpu=vfp", "-mfloat-abi=hard", "-O2", "-static", "-g"]
SCENES = ["flat", "noz", "gouraud", "tex", "texunlit", "texsmooth", "spheres", "spheres_tex", "room",
          "tex+gpu", "spheres+gpu", "spheres_tex+gpu", "room+gpu", "room2", "room2+gpu"]
COUNTED = ("r3d.c", "gfx16.c", "gpu3d.c", "v3d_cl.c")
SETUP = ("r3d_mesh", "r3d_init", "g16_sheet", "g16_cls", "gpu3d_init", "probe", "block_alloc")
PCHIST = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N (1 << 22)
static unsigned keys[N], vals[N];
int main(void)
{
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        char *p = strchr(line, '[');
        if (!p || !(p = strchr(p, '/'))) continue;
        unsigned long pc = strtoul(p + 1, NULL, 16);
        unsigned h = (unsigned)(pc * 2654435761u) & (N - 1);
        while (keys[h] && keys[h] != pc) h = (h + 1) & (N - 1);
        keys[h] = (unsigned)pc;
        vals[h]++;
    }
    for (unsigned i = 0; i < N; i++)
        if (keys[i]) printf("%x %u\n", keys[i], vals[i]);
    return 0;
}
"""


def build(tmp, gpu):
    exe = os.path.join(tmp, "bench3d.arm")
    src = [os.path.join(ROOT, p) for p in ("tests/bm/bench3d.c", "src/bm/gfx16.c", "src/bm/r3d.c")]
    inc = ["-I" + os.path.join(ROOT, "src"), "-I" + os.path.join(ROOT, "tests/gpu")]
    subprocess.run([CC, *CFLAGS, *inc, "-o", exe, *src, "-lm"], check=True)
    if not gpu:
        return exe, build_hist(tmp)
    # with the GPU backend: its memory from the emulator's arena, as in make test-gpu3d
    gobj = os.path.join(tmp, "gpu3d.o")
    subprocess.run([CC, *CFLAGS, *inc, "-Daligned_alloc=test_aligned_alloc", "-Dfree=test_free", "-c",
                    os.path.join(ROOT, "src/gpu/gpu3d.c"), "-o", gobj], check=True)
    gsrc = src + [os.path.join(ROOT, p) for p in ("tests/gpu/v3d_emu.c", "src/gpu/v3d_cl.c")]
    subprocess.run([CC, *CFLAGS, *inc, "-DBENCH_GPU", "-o", exe + "-gpu", *gsrc, gobj, "-lm"], check=True)
    return exe, build_hist(tmp)


def build_hist(tmp):
    hist = os.path.join(tmp, "pchist")
    with open(hist + ".c", "w") as f:
        f.write(PCHIST)
    subprocess.run(["gcc", "-O2", "-o", hist, hist + ".c"], check=True)
    return hist


def symbols(exe):
    """(address, name, file) of the functions"""
    out = subprocess.run(["arm-linux-gnueabihf-nm", "-n", "-l", "--defined-only", exe],
                         capture_output=True, text=True, check=True).stdout
    syms = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1] in "tTwW":
            where = parts[3] if len(parts) > 3 else ""
            syms.append((int(parts[0], 16), parts[2], where))
    return syms


def trace(exe, hist, scene, frames):
    # the emulator of the V3D only counts the jobs (BENCH_EMU_SKIP): it is not
    # counted, and tracing it took minutes a scene
    t = subprocess.Popen(["qemu-arm", "-E", "BENCH_EMU_SKIP=1", "-one-insn-per-tb", "-d", "exec,nochain",
                          "-D", "/dev/stdout", exe, scene, str(frames)],
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    counted = subprocess.run([hist], stdin=t.stdout, capture_output=True, text=True, check=True)
    t.wait()
    pcs = {}
    for line in counted.stdout.splitlines():
        pc, n = line.split()
        pcs[int(pc, 16)] = int(n)
    return pcs


def run(exe, hist, scene):
    gpu = scene.endswith("+gpu")
    if gpu:
        exe += "-gpu"
        two, one = trace(exe, hist, scene, 2), trace(exe, hist, scene, 1)
        pcs = {pc: n - one.get(pc, 0) for pc, n in two.items()}
    else:
        pcs = trace(exe, hist, scene, 1)
    res = subprocess.run(["qemu-arm", exe, scene], capture_output=True, text=True, check=True).stdout.split()
    return exe, pcs, int(res[2]), int(res[4])       # name checksum PX px TRI tri


def main():
    args = sys.argv[1:]
    hot = "--hot" in args
    args = [a for a in args if a != "--hot"]
    scenes = args or SCENES
    with tempfile.TemporaryDirectory() as tmp:
        gpu = any(s.endswith("+gpu") for s in scenes)
        exe, hist = build(tmp, gpu)
        table = {}
        for e in (exe, exe + "-gpu") if gpu else (exe,):
            syms = symbols(e)
            table[e] = ([s[0] for s in syms], syms)
        with ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
            results = list(pool.map(lambda s: run(exe, hist, s), scenes))
        print(f"{'scene':15s} {'pixels':>8s} {'tris':>6s} {'raster':>10s} {'per px':>7s} "
              f"{'setup+draw':>10s} {'per tri':>8s} {'total':>10s}")
        for scene, (e, pcs, px, tris) in zip(scenes, results):
            addrs, syms = table[e]
            by_fn = collections.Counter()
            for pc, n in pcs.items():
                i = bisect.bisect_right(addrs, pc) - 1
                if i >= 0 and any(f in syms[i][2] for f in COUNTED):
                    name = syms[i][1]
                    if name.startswith(SETUP):
                        continue            # setup of the bench and clearing the screen
                    by_fn[name] += n
            raster = sum(n for f, n in by_fn.items() if f.startswith(("raster", "span_")))
            other = sum(by_fn.values()) - raster
            print(f"{scene:15s} {px:8d} {tris:6d} {raster:10d} {raster / max(px, 1):7.1f} "
                  f"{other:10d} {other / max(tris, 1):8.0f} {raster + other:10d}")
            if hot:
                for f, n in by_fn.most_common():
                    print(f"    {f:24s} {n:10d}")
                top = sorted(pcs.items(), key=lambda kv: -kv[1])[:int(os.environ.get("HOT", "30"))]
                dis = subprocess.run(["arm-linux-gnueabihf-objdump", "-d", "--no-show-raw-insn", e],
                                     capture_output=True, text=True).stdout
                lines = {}
                for line in dis.splitlines():
                    s = line.strip()
                    if s[:1].isalnum() and ":" in s:
                        try:
                            lines[int(s.split(":")[0], 16)] = s
                        except ValueError:
                            pass
                for pc, n in sorted(top):
                    print(f"    {n:9d}  {lines.get(pc, hex(pc))}")


if __name__ == "__main__":
    main()
