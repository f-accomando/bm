#!/usr/bin/env python3
"""icache_sim.py: counts the I-cache misses of the SNES core on a model of the ARM1176 cache (16 KB, 4 ways, 32-byte lines).

  python3 core/scripts/icache_sim.py ROM [--fw FILE] [--warm N] [--frames N] [--pad FILE] [--policy rr|lru|rand] [--out DIR] [--cflags "..."]

It builds tests/snes/icache_run.c with the SNES core (the flags of `make test-snes-cpu-arm`, plus -ffunction-sections), runs it in qemu-system-arm,
waits for the warm-up frames, switches the QEMU instruction trace on (one instruction a block, `-d exec`) and pipes it into tests/snes/icache_sim.c.
The model has no prefetch and no branch predictor: use the numbers to compare two builds, not as the miss count of the Pi.
The ROM is read from your disk and is not copied. Run it on Linux or in WSL (arm-none-eabi-gcc, qemu-system-arm, gcc)."""
import argparse, os, shutil, socket, subprocess, sys, time


def sh(cmd):
    subprocess.run(cmd, shell=True, check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("--fw", default="-")
    ap.add_argument("--warm", type=int, default=600)
    ap.add_argument("--frames", type=int, default=6)
    ap.add_argument("--pad", default="")
    ap.add_argument("--policy", default="rr")
    ap.add_argument("--out", default="build/icache")
    ap.add_argument("--cflags", default="")
    ap.add_argument("--filefl", action="append", default=[], help="NAME=FLAGS: extra compiler flags for core/src/bm/NAME.c (for example snes_spc=-Os)")
    ap.add_argument("--hot", action="store_true", help="link with core/src/bm/snes_hot.ld (the hot SNES code in one block)")
    a = ap.parse_args()
    out = a.out
    os.makedirs(out, exist_ok=True)
    rom = os.path.join(out, "rom.bin")
    shutil.copyfile(a.rom, rom)  # the QEMU run reads it from the current folder
    fw = "-"
    if a.fw != "-":
        fw = os.path.join(out, "fw.bin")
        shutil.copyfile(a.fw, fw)
    script = os.path.join(out, "pad.txt")
    if a.pad:
        shutil.copyfile(a.pad, script)
    else:
        open(script, "w").write("")
    defs = f'-DIC_ROM=\\"{rom}\\" -DIC_FW=\\"{fw}\\" -DIC_WARM=\\"{a.warm}\\" -DIC_TRACE=\\"{a.frames}\\" -DIC_SCRIPT=\\"{script}\\"'
    cc = ("arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -mfloat-abi=soft -O2 -std=gnu11 -ffunction-sections -fdata-sections "
          "-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Icore/src/bm -Itests/snes -w " + a.cflags)
    # one object per source, named as in the kernel build (snes_spc.c.o), so that core/src/bm/snes_hot.ld finds the sections
    names = "snes_bus snes_sa1 snes_sa1_cpu snes_ppu snes_spc snes_dsp snes_gsu snes_dsp77 snes_cpu".split()
    objs = []
    for n in names:
        extra = " ".join(x.split("=", 1)[1] for x in a.filefl if x.split("=", 1)[0] == n)
        sh(f"{cc} {extra} -c core/src/bm/{n}.c -o {out}/{n}.c.o")
        objs.append(f"{out}/{n}.c.o")
    sh(f"{cc} -c tests/snes/arm_mmu.c -o {out}/arm_mmu.c.o")
    sh(f"{cc} {defs} -c tests/snes/icache_run.c -o {out}/icache_run.c.o")
    objs += [f"{out}/arm_mmu.c.o", f"{out}/icache_run.c.o"]
    for s in ("ppu", "cpu", "gsu"):
        sh(f"arm-none-eabi-gcc -mcpu=arm1176jzf-s -marm -Icore/src/bm -c core/src/bm/snes_{s}_arm.S -o {out}/snes_{s}_arm.S.o")
        objs.append(f"{out}/snes_{s}_arm.S.o")
    ld = "-Wl,-Ttext=0x10000 -Wl,--gc-sections"
    if a.hot and os.path.exists("core/src/bm/snes_hot.ld"):
        open(f"{out}/hot.ld", "w").write("SECTIONS { .text.snes_hot_blk : { INCLUDE core/src/bm/snes_hot.ld } } INSERT BEFORE .text;\n")
        ld = "-Wl,-Ttext=0x30000 -Wl,--section-start=.text.snes_hot_blk=0x10000 -Wl,--gc-sections" + f" -Wl,-T,{out}/hot.ld"
    sh(f"{cc} -specs=rdimon.specs {ld} -Wl,-Map={out}/run.map -o {out}/run.elf {' '.join(objs)} -lm")
    sh(f"arm-none-eabi-nm -S -n {out}/run.elf > {out}/syms.txt")
    sh(f"arm-none-eabi-size {out}/run.elf | tail -1")
    sh(f"gcc -O2 -o {out}/icache_sim tests/snes/icache_sim.c")
    syms = {}
    for l in open(f"{out}/syms.txt"):
        p = l.split()
        if len(p) >= 3:
            syms[p[-1]] = p[0]
    snap = f"/tmp/icache_{os.getpid()}.qcow2"  # not on /mnt/c: the snapshot of the guest after the warm-up
    sh(f"qemu-img create -f qcow2 {snap} 8M > /dev/null")
    base = ["qemu-system-arm", "-M", "versatilepb", "-cpu", "arm1176", "-m", "128", "-semihosting", "-nographic", "-audio", "none",
            "-drive", f"file={snap},if=none,id=snapdisk", "-kernel", f"{out}/run.elf"]
    port = 45000 + os.getpid() % 1000

    def start(extra):
        q = subprocess.Popen(base + ["-monitor", f"tcp:127.0.0.1:{port},server,nowait"] + extra,
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        mon = None
        for _ in range(100):
            try:
                mon = socket.create_connection(("127.0.0.1", port))
                break
            except OSError:
                time.sleep(0.1)
        if mon is None:
            q.kill()
            sys.exit("cannot reach the QEMU monitor")
        return q, mon

    def hmp(mon, c, wait=0.3):
        mon.sendall((c + "\n").encode())
        time.sleep(wait)
        try:
            mon.settimeout(0.3)
            return mon.recv(65536)
        except OSError:
            return b""

    def wait_for(q, word):
        buf = b""
        while word not in buf:
            c = q.stdout.read1(256)
            if not c:
                sys.exit("QEMU ended before " + word.decode() + ": " + buf.decode(errors="replace")[-300:])
            buf += c
        print(buf.decode(errors="replace").strip().splitlines()[-1], flush=True)

    # 1. the warm-up at full speed, then a snapshot while the guest waits for a key
    q, mon = start([])
    wait_for(q, b"ICACHE_WAIT")
    hmp(mon, "savevm warm", 3)
    hmp(mon, "quit", 0.5)
    q.wait()
    # 2. the same guest from the snapshot, one instruction a block, with the trace on
    fifo = f"/tmp/icache_{os.getpid()}.fifo"  # not on /mnt/c: a Windows folder has no FIFO
    if os.path.exists(fifo):
        os.remove(fifo)
    os.mkfifo(fifo)
    sim = subprocess.Popen(f"{out}/icache_sim {out}/syms.txt {syms['icache_frame_mark']} {syms['icache_trace_end']} {a.policy} < {fifo}", shell=True,
                           env=dict(os.environ, ICACHE_DUMP=os.environ.get("ICACHE_DUMP", f"{out}/trace.bin"), ICACHE_TOP=os.environ.get("ICACHE_TOP", "25")))
    q, mon = start(["-accel", "tcg,one-insn-per-tb=on", "-loadvm", "warm", "-S"])  # paused: the trace must be on before the guest runs
    time.sleep(1)
    hmp(mon, f"logfile {fifo}")
    hmp(mon, "log exec,nochain")
    q.stdin.write(b"g")
    q.stdin.flush()
    hmp(mon, "cont", 0.1)
    sim.wait()
    os.set_blocking(q.stdout.fileno(), False)
    try:
        print((q.stdout.read() or b'').decode(errors='replace'))
    except OSError:
        pass
    q.kill()
    os.remove(fifo)
    os.remove(snap)
    sys.exit(sim.returncode)


if __name__ == "__main__":
    main()
