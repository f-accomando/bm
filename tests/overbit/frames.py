#!/usr/bin/env python3
"""
frames.py - what a frame of an Overbit match costs the Pi's ARM, counted
on the PC: bmhost built for ARM Linux (the ARM1176's flags) runs the
benchmark's fight under qemu-arm, and tests/overbit/framecount.c counts
the instructions of every frame, by function.

  frames.py [--root DIR] [--gpu] [--tag NAME] [--shim] [--res WxH] CONFIG...

CONFIG is a phase of the benchmark (84_bench), "arm:2" or "gpu:3"
(renderer:quality): the two teams face to face on the point
(OVERBIT_BENCH_HOT), 2 s in a bot's eyes and 2 s from above. --root takes
the runtime from another checkout (a git worktree of an older commit: the
cartridge stays this one), --shim gives the cartridge a gpu3d() that
answers "no" (a runtime from before the GPU), --gpu builds bmhost with the
GPU's 3D on the V3D emulator (BMHOST_EMU_SKIP: the jobs are not run),
--headless leaves out _draw (what the match costs without drawing it).

The report: instructions per frame (average and the worst) of the two
windows, by part (Lua, 3D, 2D, the GPU's driver...), and an estimate of
the Pi's milliseconds at ~2.2 ns an instruction (the 3D's loops measured
on the Pi; the Lua interpreter, with more cache misses, may cost more).
Needs gcc-arm-linux-gnueabihf and qemu-user.
"""
import argparse
import collections
import os
import re
import subprocess
import sys
import tempfile

TOP = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(TOP, "build", "frames")
CC = "arm-linux-gnueabihf-gcc"
CF = ["-mcpu=arm1176jzf-s", "-marm", "-mfpu=vfp", "-mfloat-abi=hard", "-O2", "-g", "-static"]
NS = 2.2                     # ns an instruction on the Pi (r3d's loops; see count_insns.py)
FPS = 60
SETTLE = 0.5                 # seconds of the measure not counted (84_bench)

# rough ARM1176 cycles by mnemonic (as tools/armprof.py)
COST = [
    ("vdiv", 19), ("vsqrt", 19), ("vcvt", 2), ("vmla", 2), ("vmls", 2), ("vpush", 2), ("vpop", 2),
    ("vldm", 2), ("vstm", 2), ("vmrs", 2), ("mul", 2), ("mla", 2), ("smull", 3), ("umull", 3),
    ("smlal", 3), ("ldm", 2), ("stm", 2), ("push", 2), ("pop", 3), ("bl", 2),
]


def cost(mn):
    for p, c in COST:
        if mn.startswith(p):
            return c
    return 1


def sh(cmd, **kw):
    r = subprocess.run(cmd, **kw)
    if r.returncode:
        sys.exit(f"frames: failed: {' '.join(cmd[:4])}...")
    return r


def build_host(root, gpu, out):
    """bmhost for ARM Linux from the sources in root"""
    objs_dir = out + ".objs"
    os.makedirs(objs_dir, exist_ok=True)
    mk = open(os.path.join(root, "Makefile")).read()
    m = re.search(r"^BMHOST_RT :=(.*?)^BMHOST_LUA", mk, re.S | re.M)
    rt = re.findall(r"src/[a-z0-9_/]*\.c", m.group(1))
    lua = sorted(f"third_party/lua/{f}" for f in os.listdir(os.path.join(root, "third_party/lua"))
                 if f.endswith(".c") and f not in ("lua.c", "luac.c"))
    jobs, objs = [], []
    for f in rt + lua:
        o = os.path.join(objs_dir, f.replace("/", "_") + ".o")
        objs.append(o)
        if os.path.exists(o) and os.path.getmtime(o) > os.path.getmtime(os.path.join(root, f)):
            continue
        jobs.append(subprocess.Popen([CC] + CF + ["-std=c11", "-w", "-Itests/host/shim", "-Isrc", "-Isrc/bm",
                                                  "-Ithird_party/lua", "-c", "-o", o, f], cwd=root))
        if len(jobs) >= os.cpu_count():
            jobs.pop(0).wait()
    for j in jobs:
        j.wait()
    extra, defs = [], []
    if gpu:
        g = os.path.join(objs_dir, "gpu3d.o")
        sh([CC] + CF + ["-std=c11", "-w", "-Isrc", "-Itests/gpu", "-Daligned_alloc=test_aligned_alloc",
                        "-Dfree=test_free", "-c", "src/gpu/gpu3d.c", "-o", g], cwd=root)
        extra = [g, "src/gpu/v3d_cl.c", "tests/gpu/v3d_emu.c"]
        defs = ["-DBMHOST_GPU", "-Itests/gpu"]
    # libs.S's GNU-stack note is written for the PC's assembler; what it
    # embeds from the build (words.lua) is made in that checkout first
    s = open(os.path.join(root, "tests/host/libs.S")).read().replace("@progbits", "%progbits")
    if '"words.lua"' in s:
        sh(["make", "-s", "-C", root, "build/words.lua"])
    libs = os.path.join(objs_dir, "libs.S")
    open(libs, "w").write(s)
    inc = os.path.join(root, "build")
    sh([CC] + CF + ["-Wa,-I" + inc, "-c", "-o", libs[:-2] + ".o", libs], cwd=root)
    sh([CC] + CF + ["-D_DEFAULT_SOURCE", "-w", "-Itests/host/shim", "-Isrc", "-Isrc/bm", "-Ithird_party/lua", "-I" + inc]
       + defs + ["-o", out, "tests/host/bmhost.c", "tests/host/stubs.c", "tests/host/hostnet.c", libs[:-2] + ".o"]
       + objs + extra + ["-lm"], cwd=root, stderr=subprocess.DEVNULL)


def table(binary, path, marker="host_frame"):
    """every instruction of the binary: address, width, cycles; the address
    of the marker (0 for none)"""
    dis = subprocess.run(["arm-linux-gnueabihf-objdump", "-d", binary], capture_output=True, text=True).stdout
    re_d = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]{4,8}(?: [0-9a-f]{4})?)\s+(\S+)")
    with open(path, "w") as f:
        for line in dis.splitlines():
            m = re_d.match(line)
            if m:
                raw = m.group(2)
                w = 4 if len(raw.replace(" ", "")) == 8 else 2
                f.write(f"{m.group(1)} {w} {cost(m.group(3))}\n")
    if not marker:
        return 0
    nm = subprocess.run(["arm-linux-gnueabihf-nm", binary], capture_output=True, text=True).stdout
    for line in nm.splitlines():
        p = line.split()
        if len(p) == 3 and p[2] == marker:
            return int(p[0], 16)
    sys.exit(f"frames: no {marker}")


def framecount():
    """tests/overbit/framecount.c, built when it changes"""
    fc = os.path.join(OUT, "framecount")
    src = os.path.join(TOP, "tests/overbit/framecount.c")
    if not os.path.exists(fc) or os.path.getmtime(fc) < os.path.getmtime(src):
        os.makedirs(OUT, exist_ok=True)
        sh(["cc", "-O2", "-o", fc, src])
    return fc


def cart(cfg, shim, headless, secs, res=None):
    """the benchmark's fight, one phase: secs / 2 in a bot's eyes, secs / 2 from above"""
    lua = os.path.join(OUT, f"hot{secs:g}-{cfg.replace(':', '')}{'-shim' if shim else ''}"
                            f"{'-nodraw' if headless else ''}{'-' + res if res else ''}.lua")
    bm = lua[:-4] + ".bm"
    sh(["make", "-s", "-C", TOP, "build/overbit/models.bm", "build/overbit/sounds.json", "build/overbit/21_map.lua"])
    d = ["--define", f"OVERBIT_BENCH_HOT={secs:g}", "--define", f'OVERBIT_BENCH_ONE="{cfg}"',
         "--define", f"OVERBIT_BENCH_STOP={SETTLE + secs:g}"]
    if shim:
        d += ["--define", "gpu3d=gpu3d or function() return false, false end"]
    # a runtime from before bm3d 5.5 (--root) has no visible3d: everything seen
    d += ["--define", "visible3d=visible3d or function() return true end"]
    if res:
        d += ["--define", f'OVERBIT_RES="{res}"']
    if headless:
        d += ["--define", "OVERBIT_HEADLESS=true"]
    sh([sys.executable, os.path.join(TOP, "carts/overbit/build.py"), lua, "--start", "bench",
        "--extra", os.path.join(TOP, "build/overbit/21_map.lua")] + d, cwd=TOP)
    sh([sys.executable, os.path.join(TOP, "scripts/mkbm.py"), "-o", bm, "--lua", lua, "--title", "Overbit bench",
        "--author", "bm", "--res", "480x270", "--models", os.path.join(TOP, "build/overbit/models.bm"),
        "--audio", os.path.join(TOP, "build/overbit/sounds.json")], cwd=TOP, stdout=subprocess.DEVNULL)
    return bm


# the parts of a frame, by the source file of a function
PARTS = [("src/bm/r3d.c", "3D (r3d)"), ("src/gpu/", "GPU driver"), ("third_party/lua/", "Lua"),
         ("src/bm/gfx16.c", "2D"), ("src/bm/world3d.c", "collisions"), ("src/audio/", "audio"),
         ("src/bm/", "runtime"), ("src/", "runtime"), ("tests/", "bmhost")]


def files_of(binary):
    """function -> its source file (nm -l: the debug lines)"""
    out = subprocess.run(["arm-linux-gnueabihf-nm", "-l", "--defined-only", binary], capture_output=True,
                         text=True).stdout
    where = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 4 and p[1] in "tTwW" and ":" in p[3]:
            f = p[3].rsplit(":", 1)[0]
            for top in ("/third_party/", "/src/", "/tests/"):      # from the top of the checkout
                if top in f:
                    f = f[f.rindex(top) + 1:]
                    break
            where.setdefault(p[2], f)
    return where


def part(fn, where):
    """the part of the frame a function belongs to"""
    f = where.get(fn) or where.get(fn.split(".")[0], "")
    for prefix, name in PARTS:
        if f.startswith(prefix):
            return name
    return "C library"


def count(binary, bm, csv_path, gpu):
    """the CSV of the frames, and the hot blocks in csv_path + ".blocks\""""
    tbl = csv_path + ".tbl"
    marker = table(binary, tbl)
    fc = framecount()
    tmp = tempfile.mkdtemp(prefix="frames-", dir=OUT)
    fifo = os.path.join(tmp, "log")
    os.mkfifo(fifo)
    env = dict(os.environ)
    if gpu:
        env["BMHOST_EMU_SKIP"] = "1"
    sd = os.path.join(tmp, "sd")
    os.makedirs(sd)
    q = subprocess.Popen(["qemu-arm", "-d", "in_asm,exec,nochain", "-D", fifo, binary, bm, "--seconds", "60",
                          "--quiet", "--sd", sd], env=env, stdout=subprocess.DEVNULL,
                         stderr=open(csv_path + ".log", "w"))
    with open(fifo) as fin, open(csv_path, "w") as fout:
        sh([fc, tbl, hex(marker), csv_path + ".blocks"], stdin=fin, stdout=fout)
    q.wait()
    os.unlink(fifo)
    os.rmdir(os.path.join(tmp, "sd"))
    os.rmdir(tmp)


def report(csv_path, secs, where):
    frames = collections.defaultdict(collections.Counter)
    for line in open(csv_path):
        fr, rest = line.split(",", 1)
        fn, n, c = rest.rstrip("\n").rsplit(",", 2)
        frames[int(fr)][fn] += int(n)
    last = max(frames)
    # the last frame stops half way (quit); the two windows before it
    h = int(secs * FPS / 2)
    win = {"first person": range(last - 1 - 2 * h, last - 1 - h), "overview": range(last - 1 - h, last - 1)}
    out = {}
    for name, r in win.items():
        tot = [sum(frames[f].values()) for f in r]
        parts = collections.Counter()
        fns = collections.Counter()
        for f in r:
            for fn, n in frames[f].items():
                parts[part(fn, where)] += n
                fns[fn] += n
        k = len(r)
        out[name] = {
            "avg": sum(tot) / k, "max": max(tot), "min": min(tot),
            "parts": {p: v / k for p, v in parts.items()},
            "top": [(fn, v / k) for fn, v in fns.most_common(25)],
        }
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("configs", nargs="+")
    ap.add_argument("--root", default=TOP)
    ap.add_argument("--gpu", action="store_true")
    ap.add_argument("--shim", action="store_true")
    ap.add_argument("--headless", action="store_true", help="without _draw: the match's own cost")
    ap.add_argument("--tag", default="now")
    ap.add_argument("--res", help="the screen the match starts with (OVERBIT_RES, e.g. 1920x1080)")
    ap.add_argument("--top", type=int, default=0, help="also the N hottest functions")
    ap.add_argument("--blocks", type=int, default=0, help="also the N hottest blocks, with their lines")
    ap.add_argument("--secs", type=float, default=4, help="seconds measured (half and half)")
    ap.add_argument("--again", action="store_true", help="only the report of the last count")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    host = os.path.join(OUT, f"bmhost-arm-{a.tag}")
    if not a.again:
        build_host(os.path.abspath(a.root), a.gpu, host)
    for cfg in a.configs:
        path = os.path.join(OUT, f"{a.tag}-{cfg.replace(':', '')}{'-' + a.res if a.res else ''}.csv")
        if not a.again:
            count(host, cart(cfg, a.shim, a.headless, a.secs, a.res), path, a.gpu)
        r = report(path, a.secs, files_of(host))
        for w, v in r.items():
            ms = v["avg"] * NS / 1e6
            print(f"{a.tag} {cfg} {w:12s}: {v['avg'] / 1e6:6.2f} M instructions a frame "
                  f"(worst {v['max'] / 1e6:5.2f}) ~{ms:5.1f} ms on the Pi, "
                  f"~{min(FPS, 1000 / max(ms, 1e-3)):4.1f} fps")
            print("    " + "  ".join(f"{p} {n / 1e6:.2f}" for p, n in
                                     sorted(v["parts"].items(), key=lambda x: -x[1])))
            for fn, n in v["top"][:a.top]:
                print(f"      {fn[:32]:32s} {n / 1e6:6.3f}")
        if a.blocks:
            hot_blocks(host, path + ".blocks", a.blocks)
    return 0


def hot_blocks(binary, path, n):
    """the hottest blocks of the run (frames after the first), with their lines"""
    rows = []
    for line in open(path):
        pc, ni, nc, runs, fn = line.rstrip("\n").split(" ", 4)
        rows.append((int(ni) * int(runs), int(pc, 16), int(ni), int(runs), fn))
    rows.sort(reverse=True)
    tot = sum(r[0] for r in rows)
    top = rows[:n]
    src = subprocess.run(["arm-linux-gnueabihf-addr2line", "-e", binary] + [hex(r[1]) for r in top],
                         capture_output=True, text=True).stdout.split("\n")
    print(f"    {'block':>8s} {'instr':>5s} {'runs':>9s} {'%':>5s}  source")
    for (t, pc, ni, runs, fn), s_ in zip(top, src):
        print(f"    {pc:8x} {ni:5d} {runs:9d} {100 * t / tot:5.1f}  {fn[:22]} {os.path.basename(s_)}")


if __name__ == "__main__":
    sys.exit(main())
