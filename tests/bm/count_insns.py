#!/usr/bin/env python3
"""Counts the ARM instructions the rasterizer executes for the scenes of
tests/bm/bench3d.c (M30): builds bench3d for ARM (ARM1176, VFP, -O2), runs
each scene under qemu-arm with one instruction per translation block and
the exec trace, and adds up the instructions of the functions of r3d.c and
gfx16.c. Cache and bus are not simulated: the numbers are the work of the
CPU, not the time on the Pi.

    tests/bm/count_insns.py [SCENE ...]      default: the main scenes
    tests/bm/count_insns.py --hot SCENE      the 30 busiest instructions

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
SCENES = ["flat", "noz", "gouraud", "tex", "texunlit", "texsmooth", "spheres", "spheres_tex", "room"]
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


def build(tmp):
    exe = os.path.join(tmp, "bench3d.arm")
    src = [os.path.join(ROOT, p) for p in ("tests/bm/bench3d.c", "src/bm/gfx16.c", "src/bm/r3d.c")]
    subprocess.run([CC, *CFLAGS, "-I" + os.path.join(ROOT, "src"), "-o", exe, *src, "-lm"], check=True)
    hist = os.path.join(tmp, "pchist")
    with open(hist + ".c", "w") as f:
        f.write(PCHIST)
    subprocess.run(["gcc", "-O2", "-o", hist, hist + ".c"], check=True)
    return exe, hist


def symbols(exe):
    """(address, name, file) of the functions of r3d.c and gfx16.c"""
    out = subprocess.run(["arm-linux-gnueabihf-nm", "-n", "-l", "--defined-only", exe],
                         capture_output=True, text=True, check=True).stdout
    syms = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1] in "tTwW":
            where = parts[3] if len(parts) > 3 else ""
            syms.append((int(parts[0], 16), parts[2], where))
    return syms


def run(exe, hist, scene):
    trace = subprocess.Popen(["qemu-arm", "-one-insn-per-tb", "-d", "exec,nochain", "-D", "/dev/stdout",
                              exe, scene, "1"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    counted = subprocess.run([hist], stdin=trace.stdout, capture_output=True, text=True, check=True)
    trace.wait()
    pcs = {}
    for line in counted.stdout.splitlines():
        pc, n = line.split()
        pcs[int(pc, 16)] = int(n)
    res = subprocess.run(["qemu-arm", exe, scene], capture_output=True, text=True, check=True).stdout.split()
    return pcs, int(res[2]), int(res[4])            # name checksum PX px TRI tri


def main():
    args = sys.argv[1:]
    hot = "--hot" in args
    args = [a for a in args if a != "--hot"]
    scenes = args or SCENES
    with tempfile.TemporaryDirectory() as tmp:
        exe, hist = build(tmp)
        syms = symbols(exe)
        addrs = [s[0] for s in syms]
        with ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
            results = list(pool.map(lambda s: run(exe, hist, s), scenes))
        print(f"{'scene':15s} {'pixels':>8s} {'tris':>6s} {'raster':>10s} {'per px':>7s} "
              f"{'setup+draw':>10s} {'per tri':>8s}")
        for scene, (pcs, px, tris) in zip(scenes, results):
            by_fn = collections.Counter()
            for pc, n in pcs.items():
                i = bisect.bisect_right(addrs, pc) - 1
                if i >= 0 and ("r3d.c" in syms[i][2] or "gfx16.c" in syms[i][2]):
                    name = syms[i][1]
                    if name.startswith(("r3d_mesh", "r3d_init", "g16_sheet", "g16_cls")):
                        continue            # setup of the bench and clearing the screen
                    by_fn[name] += n
            raster = sum(n for f, n in by_fn.items() if f.startswith(("raster", "span_")))
            other = sum(by_fn.values()) - raster
            print(f"{scene:15s} {px:8d} {tris:6d} {raster:10d} {raster / max(px, 1):7.1f} "
                  f"{other:10d} {other / max(tris, 1):8.0f}")
            if hot:
                for f, n in by_fn.most_common():
                    print(f"    {f:24s} {n:10d}")
                top = sorted(pcs.items(), key=lambda kv: -kv[1])[:int(os.environ.get("HOT", "30"))]
                dis = subprocess.run(["arm-linux-gnueabihf-objdump", "-d", "--no-show-raw-insn", exe],
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
