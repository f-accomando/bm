#!/usr/bin/env python3
"""
armprof.py - how many ARM instructions each function of a program executes,
counted by QEMU (user mode): the code is the console's (ARM1176, -marm),
only the C library around it is Linux's. A stand-in for a profiler on the
Pi: instruction counts are not cycles (a VFP division takes ~19 cycles, a
load that misses the cache tens), so the report also weighs every
instruction with a rough ARM1176 cost.

  armprof.py BINARY [ARGS...]          (built with arm-linux-gnueabihf-gcc -static)

The program runs once under qemu-arm -d in_asm,exec,nochain; the log can be
big (hundreds of MB for a few hundred million instructions).
"""
import collections
import os
import re
import subprocess
import sys
import tempfile

# rough costs of the ARM1176's instructions in cycles (by mnemonic prefix)
COST = [
    ("vdiv", 19), ("vsqrt", 19), ("vcvt", 2), ("vmul", 1), ("vadd", 1), ("vsub", 1), ("vmla", 2), ("vmls", 2),
    ("vneg", 1), ("vabs", 1), ("vcmp", 1), ("vmrs", 2), ("vmov", 1), ("vldr", 1), ("vstr", 1), ("vpush", 2),
    ("vpop", 2), ("vldm", 2), ("vstm", 2),
    ("mul", 2), ("mla", 2), ("smull", 3), ("umull", 3), ("smlal", 3), ("ldm", 2), ("stm", 2), ("push", 2),
    ("pop", 3), ("ldr", 1), ("str", 1), ("b", 1), ("bl", 2),
]


def cost(mn):
    for p, c in COST:
        if mn.startswith(p):
            return c
    return 1


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    binary = sys.argv[1]
    # the instructions of the program, from objdump: address -> mnemonic
    dis = subprocess.run(["arm-linux-gnueabihf-objdump", "-d", "--no-show-raw-insn", binary],
                         capture_output=True, text=True).stdout
    mnem = {}
    re_d = re.compile(r"^\s*([0-9a-f]+):\s+(\S+)")
    for line in dis.splitlines():
        m = re_d.match(line)
        if m:
            mnem[int(m.group(1), 16)] = m.group(2)
    log = tempfile.NamedTemporaryFile(prefix="armprof-", suffix=".log", delete=False).name
    try:
        subprocess.run(["qemu-arm", "-d", "in_asm,exec,nochain", "-D", log] + sys.argv[1:], check=False)
        blocks = {}          # pc -> (symbol, n instructions, weighted cost)
        execs = collections.Counter()
        re_tr = re.compile(r"^Trace \d+: 0x[0-9a-f]+ \[[0-9a-f]+/([0-9a-f]+)/")
        re_pc = re.compile(r"^0x([0-9a-f]+):")
        sym, pc, nbytes = "", None, 0

        def close():
            if pc is not None and nbytes:
                if pc in mnem and (pc + 4 in mnem or nbytes == 4):
                    k = nbytes // 4                 # ARM code (the console's)
                    blocks[pc] = (sym or "?", k, sum(cost(mnem.get(pc + 4 * i, "")) for i in range(k)))
                else:
                    blocks[pc] = (sym or "?", nbytes // 2, nbytes // 2)   # Thumb (the C library)

        with open(log, errors="replace") as f:
            for line in f:
                if line.startswith("IN:"):
                    close()
                    sym, pc, nbytes = line[3:].strip(), None, 0
                    continue
                m = re_pc.match(line)
                if m and pc is None:
                    pc = int(m.group(1), 16)
                    continue
                if line.startswith("OBJD-"):
                    nbytes += len(line.split(":", 1)[1].strip()) // 2
                    continue
                if line.startswith("Trace"):
                    close()
                    pc, nbytes = None, 0
                    m = re_tr.match(line)
                    if m:
                        execs[int(m.group(1), 16)] += 1
        close()
        n = collections.Counter()
        c = collections.Counter()
        for p_, k in execs.items():
            if p_ in blocks:
                s_, ni, nc = blocks[p_]
                n[s_] += ni * k
                c[s_] += nc * k
        tn, tc = sum(n.values()), sum(c.values())
        print(f"{'function':32s} {'instructions':>14s} {'%':>6s} {'~cycles':>14s} {'%':>6s}")
        for s_, v in c.most_common(25):
            print(f"{s_[:32]:32s} {n[s_]:14d} {100 * n[s_] / max(1, tn):6.1f} {v:14d} {100 * v / max(1, tc):6.1f}")
        print(f"{'total':32s} {tn:14d} {'':6s} {tc:14d}")
    finally:
        os.unlink(log)


if __name__ == "__main__":
    sys.exit(main())
