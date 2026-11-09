#!/usr/bin/env python3
"""
jit_unicorn.py - tests the ARM code of the PSX recompiler on the PC.

  python3 tests/psx/jit_unicorn.py [trials]

The translator (src/bm/psx_jit.c built with PSX_JIT_HOST) makes the ARM code of a block; the Unicorn emulator runs it
on a copy of the CPU state and of RAM; the C interpreter runs the same MIPS program; the registers, HI, LO and the data
area must come out the same. Random programs, as in the self test on the Pi (psx_jit_selftest), but only with what runs
without a helper call: ALU, shifts, multiply, branches and jumps with delay slots, loads and stores of RAM.
Needs: pip install unicorn; the library is built by this script (gcc).
"""
import ctypes, os, random, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
LIB = os.path.join(ROOT, "build", "host", "libpsxjit.so")

def build():
    os.makedirs(os.path.dirname(LIB), exist_ok=True)
    srcs = ["tests/psx/jit_host.c"] + ["src/bm/psx_%s.c" % n for n in "jit cpu gte bus gpu cd spu".split()]
    subprocess.check_call(["gcc", "-O1", "-g", "-shared", "-fPIC", "-DPSX_JIT_HOST", "-Isrc/bm", "-o", LIB] + srcs, cwd=ROOT)

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_PROT_ALL
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_PC

build()
L = ctypes.CDLL(LIB)
L.h_ram.restype = ctypes.c_void_p
L.h_cpu.restype = ctypes.c_void_p
L.h_dump.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_int]
L.h_interp.argtypes = [ctypes.c_uint32, ctypes.c_int]
RAM = L.h_ram()
CPU = L.h_cpu()
CPU_SIZE = L.h_cpu_size()
OFF_R, OFF_HI, OFF_LO, OFF_PC, OFF_NPC, OFF_NBD = (L.h_off_r(), L.h_off_hi(), L.h_off_lo(), L.h_off_pc(), L.h_off_npc(), L.h_off_nbd())

T_BASE, T_DATA = 0x80010000, 0x80020000
CODE_AT, CPU_AT, RAM_AT, FLAGS_AT, SENT_AT = 0x10000000, 0x20000000, 0x30000000, 0x40000000, 0x50000000

def LEN(prog): return len(prog) - 140

def rd(addr, n): return ctypes.string_at(addr, n)
def wr(addr, b): ctypes.memmove(addr, b, len(b))

POOL = [1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12]

class Gen:
    def __init__(self, seed): self.r = random.Random(seed)
    def reg(self): return self.r.choice(POOL)
    def src(self):
        k = self.r.randrange(10)
        return 0 if k == 0 else 31 if k == 1 else self.reg()
    def R(self, rs, rt, rd_, sh, fn): return (rs << 21) | (rt << 16) | (rd_ << 11) | (sh << 6) | fn
    def I(self, op, rs, rt, imm): return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)
    def simple(self):
        r = self.r
        k = r.randrange(32)
        rs, rt, sh = self.src(), self.src(), r.randrange(32)
        rd_ = 0 if r.randrange(12) == 0 else self.reg()
        dt = 0 if r.randrange(12) == 0 else self.reg()
        imm = r.randrange(0x10000)
        bo = r.randrange(256) - 128
        R = self.R
        table = [R(0, rt, rd_, sh, 0x00), R(0, rt, rd_, sh, 0x02), R(0, rt, rd_, sh, 0x03), R(rs, rt, rd_, 0, 0x04),
                 R(rs, rt, rd_, 0, 0x06), R(rs, rt, rd_, 0, 0x07), R(0, 0, rd_, 0, 0x10), R(rs, 0, 0, 0, 0x11),
                 R(0, 0, rd_, 0, 0x12), R(rs, 0, 0, 0, 0x13), R(rs, rt, 0, 0, 0x18), R(rs, rt, 0, 0, 0x19),
                 R(rs, rt, rd_, 0, 0x21), R(rs, rt, rd_, 0, 0x23), R(rs, rt, rd_, 0, 0x24), R(rs, rt, rd_, 0, 0x25),
                 R(rs, rt, rd_, 0, 0x26), R(rs, rt, rd_, 0, 0x27), R(rs, rt, rd_, 0, 0x2A), R(rs, rt, rd_, 0, 0x2B),
                 self.I(0x09, rs, dt, imm), self.I(0x0A, rs, dt, imm), self.I(0x0B, rs, dt, imm),
                 self.I(0x0C + r.randrange(3), rs, dt, imm), self.I(0x0F, 0, dt, imm),
                 self.I(0x23, 29, dt, 0x200 + (bo & ~3)), self.I(r.choice([0x21, 0x25]), 29, dt, 0x200 + (bo & ~1)),
                 self.I(r.choice([0x20, 0x24]), 29, dt, 0x200 + bo), self.I(0x2B, 29, rt, 0x200 + (bo & ~3)),
                 self.I(0x29, 29, rt, 0x200 + (bo & ~1)), self.I(0x28, 29, rt, 0x200 + bo), self.I(0x09, rs, dt, imm)]
        return table[k] & 0xFFFFFFFF
    def slot(self):                          # a delay slot instruction: not a load (a load there lands at once in the recompiler)
        while True:
            w = self.simple()
            if (w >> 26) not in (0x20, 0x21, 0x23, 0x24, 0x25): return w
    def program(self, length):
        r = self.r
        p = []
        while len(p) < length - 6:
            t = r.randrange(14)
            k = len(p)
            if t < 9:
                p.append(self.simple()); continue
            span = length - (k + 2)
            tgt = k + 2 + r.randrange(min(7, span) + 1)
            if t < 12:
                o = r.choice([4, 5, 6, 7, 1, 1, 1, 1])
                rt = r.choice([0, 1, 16, 17]) if o == 1 else self.src()
                p.append(self.I(o, self.src(), rt, tgt - (k + 1)))
                p.append(self.slot())
            elif t == 12:
                p.append(((r.choice([2, 3])) << 26) | (((T_BASE + 4 * tgt) & 0x0FFFFFFF) >> 2))
                p.append(self.slot())
            else:
                room = length - (k + 4)
                tg2 = k + 4 + r.randrange(min(5, room) + 1)
                a = T_BASE + 4 * tg2
                p.append(self.I(0x0F, 0, 13, a >> 16))
                p.append(self.I(0x0D, 13, 13, a & 0xFFFF))
                p.append(self.R(13, 0, 0, 0, 0x08) if r.randrange(2) else self.R(13, 0, 31, 0, 0x09))
                p.append(self.slot())
        while len(p) < length: p.append(0)
        jend = 0x08000000 | (((T_BASE + 4 * length) & 0x0FFFFFFF) >> 2)
        for k in range(140):                       # past the end: j end, nop, j end, nop ...: a jump that overshoots comes back
            p.append(jend if k % 2 == 0 else 0)
        return p

def init_state(rng, length):
    regs = [rng.getrandbits(32) for _ in range(32)]
    # r13 is the jump register of the generated jr/jalr; if a branch skips the lui/ori that set it, the jump goes to the end
    regs[0] = 0; regs[13] = T_BASE + 4 * length; regs[29] = T_DATA              # the loads and stores use 0x200 +- 0x80 from it
    return regs, rng.getrandbits(32), rng.getrandbits(32), bytes(rng.getrandbits(8) for _ in range(0x400))

def set_cpu_state(base, regs, hi, lo, pc):
    cpu = bytearray(rd(CPU, CPU_SIZE))
    for i in range(32): struct.pack_into("<I", cpu, OFF_R + 4 * i, regs[i])
    struct.pack_into("<I", cpu, OFF_HI, hi); struct.pack_into("<I", cpu, OFF_LO, lo)
    struct.pack_into("<I", cpu, OFF_PC, pc); struct.pack_into("<I", cpu, OFF_NPC, pc + 4); struct.pack_into("<I", cpu, OFF_NBD, 0)
    return cpu

def run_interp(prog, regs, hi, lo, data):
    wr(RAM + (T_BASE & 0x1FFFFF), struct.pack("<%dI" % len(prog), *prog))
    wr(RAM + (T_DATA & 0x1FFFFF), data)
    wr(CPU, bytes(set_cpu_state(CPU, regs, hi, lo, T_BASE)))
    L.h_interp(T_BASE + 4 * LEN(prog), 3000)
    c = rd(CPU, CPU_SIZE)
    if struct.unpack_from("<I", c, OFF_PC)[0] != T_BASE + 4 * LEN(prog): return None      # a loop that does not end (a jump back by a jr whose lui was skipped): not a test
    return ([struct.unpack_from("<I", c, OFF_R + 4 * i)[0] for i in range(32)], struct.unpack_from("<I", c, OFF_HI)[0],
            struct.unpack_from("<I", c, OFF_LO)[0], rd(RAM + (T_DATA & 0x1FFFFF), 0x400))

def run_jit(prog, regs, hi, lo, data):
    end = T_BASE + 4 * LEN(prog)
    wr(RAM + (T_BASE & 0x1FFFFF), struct.pack("<%dI" % len(prog), *prog))      # the translator reads the program from the host RAM
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(CODE_AT, 0x100000, UC_PROT_ALL)
    uc.mem_map(CPU_AT, 0x1000, UC_PROT_ALL)
    uc.mem_map(RAM_AT, 0x200000, UC_PROT_ALL)
    uc.mem_map(FLAGS_AT, 0x4000, UC_PROT_ALL)
    uc.mem_map(SENT_AT, 0x1000, UC_PROT_ALL)
    uc.mem_write(RAM_AT, bytes(0x200000))
    uc.mem_write(RAM_AT + (T_BASE & 0x1FFFFF), struct.pack("<%dI" % len(prog), *prog))
    uc.mem_write(RAM_AT + (T_DATA & 0x1FFFFF), data)
    cpu = set_cpu_state(CPU, regs, hi, lo, T_BASE)
    uc.mem_write(CPU_AT, bytes(cpu))
    uc.reg_write(UC_ARM_REG_R11, CPU_AT); uc.reg_write(UC_ARM_REG_R10, RAM_AT); uc.reg_write(UC_ARM_REG_R9, FLAGS_AT)
    uc.reg_write(UC_ARM_REG_R8, SENT_AT)
    buf = (ctypes.c_uint32 * 2048)()
    for _ in range(3000):
        pc = struct.unpack("<I", bytes(uc.mem_read(CPU_AT + OFF_PC, 4)))[0]
        if pc == end: break
        n = L.h_dump(pc, buf, 2048)
        if n == 0: raise RuntimeError("no block at %08x" % pc)
        uc.ctl_flush_tb()                                  # Unicorn keeps what it translated: the same address would run the old block
        uc.mem_write(CODE_AT, struct.pack("<%dI" % n, *buf[:n]))
        uc.reg_write(UC_ARM_REG_R8, SENT_AT)
        uc.reg_write(UC_ARM_REG_R11, CPU_AT); uc.reg_write(UC_ARM_REG_R10, RAM_AT); uc.reg_write(UC_ARM_REG_R9, FLAGS_AT)
        # the block starts with "mov r8, lr": lr is the sentinel
        from unicorn.arm_const import UC_ARM_REG_LR
        uc.reg_write(UC_ARM_REG_LR, SENT_AT)
        uc.emu_start(CODE_AT, SENT_AT)
    c = bytes(uc.mem_read(CPU_AT, CPU_SIZE))
    return ([struct.unpack_from("<I", c, OFF_R + 4 * i)[0] for i in range(32)], struct.unpack_from("<I", c, OFF_HI)[0],
            struct.unpack_from("<I", c, OFF_LO)[0], bytes(uc.mem_read(RAM_AT + (T_DATA & 0x1FFFFF), 0x400)))

def describe(prog, upto):
    return " ".join("%08x" % w for w in prog[:upto])

def main():
    trials = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    bad = 0
    skipped = 0
    L.h_init()
    for t in range(trials):
        g = Gen(1000 + t)
        length = 24 + g.r.randrange(40)
        prog = g.program(length)
        regs, hi, lo, data = init_state(g.r, length)
        L.h_init()
        a = run_interp(prog, regs, hi, lo, data)
        if a is None: skipped += 1; continue
        L.h_init()
        try:
            b = run_jit(prog, regs, hi, lo, data)
        except Exception as e:
            print("trial %d: JIT run failed: %s" % (t, e)); bad += 1; continue
        if a != b:
            bad += 1
            if bad <= 3:
                # the shortest prefix that still differs
                for l in range(1, length + 1):
                    cut = prog[:l] + [0] * (length - l) + prog[length:]
                    L.h_init(); x = run_interp(cut, regs, hi, lo, data)
                    if x is None: continue
                    L.h_init(); y = run_jit(cut, regs, hi, lo, data)
                    if x != y:
                        diff = [(i, "%08x" % x[0][i], "%08x" % y[0][i]) for i in range(32) if x[0][i] != y[0][i]]
                        print("trial %d: prefix %d differs: regs %s hi %s lo %s data %s" % (t, l, diff, x[1] == y[1], x[2] == y[2], x[3] == y[3]))
                        print("  program: " + describe(prog, l + 1))
                        break
    print("%d of %d trials differ (%d skipped: they do not end)" % (bad, trials, skipped))
    sys.exit(1 if bad else 0)

main()
