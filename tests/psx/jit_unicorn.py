#!/usr/bin/env python3
"""
jit_unicorn.py - tests the ARM code of the PSX recompiler on the PC.

  python3 tests/psx/jit_unicorn.py [trials] [first trial]

The translator (core/src/bm/psx_jit.c built with PSX_JIT_HOST) makes the ARM code of a block; the Unicorn emulator runs it
on a copy of the CPU state and of RAM; the C interpreter runs the same MIPS program; the registers, HI, LO, the data
area, the scratchpad and the GTE registers must come out the same. The programs are the ones of the self test that runs
on the Pi (psx_jit_selftest): the same generator, in C. A call of the compiled code to a helper (the interpreter for an
instruction it does not translate, a slow load or store, the GTE) runs the real C helper on the host state.
Needs: pip install unicorn; the library is built by this script (gcc).
"""
import ctypes, os, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
LIB = os.path.join(ROOT, "build", "host", "libpsxjit.so")

def build():
    os.makedirs(os.path.dirname(LIB), exist_ok=True)
    srcs = ["tests/psx/jit_host.c"] + ["core/src/bm/psx_%s.c" % n for n in "jit cpu gte bus gpu cd spu".split()]
    subprocess.check_call(["gcc", "-O1", "-g", "-shared", "-fPIC", "-DPSX_JIT_HOST", "-Icore/src/bm", "-o", LIB] + srcs, cwd=ROOT)

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_PROT_ALL, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R9, \
    UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R8, UC_ARM_REG_LR

build()
L = ctypes.CDLL(LIB)
L.h_ram.restype = ctypes.c_void_p
L.h_cpu.restype = ctypes.c_void_p
L.h_scratch.restype = ctypes.c_void_p
L.h_dump.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_int]
L.h_interp.argtypes = [ctypes.c_uint32, ctypes.c_int]
for n in ("psx_jit_exec", "psx_jit_read", "psx_jit_write", "psx_jit_div", "psx_jit_divu", "psx_gte_get_data", "psx_gte_set_data",
          "psx_gte_get_ctl", "psx_gte_set_ctl", "psx_gte_command"):
    getattr(L, n).restype = ctypes.c_uint32
RAM, SCR, CPU = L.h_ram(), L.h_scratch(), L.h_cpu()
CPU_SIZE = L.h_cpu_size()
OFF_R, OFF_HI, OFF_LO, OFF_PC, OFF_NPC, OFF_NBD = (L.h_off_r(), L.h_off_hi(), L.h_off_lo(), L.h_off_pc(), L.h_off_npc(), L.h_off_nbd())

T_BASE, T_DATA = 0x80010000, 0x80020000
CODE_AT, CPU_AT, RAM_AT, FLAGS_AT, SENT_AT, SCR_AT, HELP_AT = 0x10000000, 0x20000000, 0x30000000, 0x40000000, 0x50000000, 0x60000000, 0xF0000000
SYNC_LO, SYNC_HI = 0x10000, 0x22000      # the part of RAM the programs use (program, data): copied around helper calls

def rd(addr, n): return ctypes.string_at(addr, n)
def wr(addr, b): ctypes.memmove(addr, b, len(b))

class Trial:
    def __init__(self, t):
        self.prog = (ctypes.c_uint32 * 256)()
        ln = ctypes.c_int()
        self.regs = (ctypes.c_uint32 * 32)(); self.hi = ctypes.c_uint32(); self.lo = ctypes.c_uint32()
        self.data = (ctypes.c_uint8 * 0x400)(); self.scr = (ctypes.c_uint8 * 0x400)(); self.gte = (ctypes.c_int32 * 64)()
        L.h_gen(t, self.prog, ctypes.byref(ln), self.regs, ctypes.byref(self.hi), ctypes.byref(self.lo), self.data, self.scr, self.gte)
        self.len = ln.value
        self.words = list(self.prog[:self.len + 140])

def set_cpu_state(regs, hi, lo, pc):
    cpu = bytearray(rd(CPU, CPU_SIZE))
    for i in range(32): struct.pack_into("<I", cpu, OFF_R + 4 * i, regs[i])
    struct.pack_into("<I", cpu, OFF_HI, hi); struct.pack_into("<I", cpu, OFF_LO, lo)
    struct.pack_into("<I", cpu, OFF_PC, pc); struct.pack_into("<I", cpu, OFF_NPC, pc + 4); struct.pack_into("<I", cpu, OFF_NBD, 0)
    return cpu

def snapshot(c):
    regs = [struct.unpack_from("<I", c, OFF_R + 4 * i)[0] for i in range(32)]
    g = (ctypes.c_int32 * 64)(); L.h_get_gte(g)
    return (regs, struct.unpack_from("<I", c, OFF_HI)[0], struct.unpack_from("<I", c, OFF_LO)[0], bytes(g))

def prepare(words, tr):
    """the host machine in the start state of a trial"""
    L.h_init()
    wr(RAM + (T_BASE & 0x1FFFFF), struct.pack("<%dI" % len(words), *words))
    wr(RAM + (T_DATA & 0x1FFFFF), bytes(tr.data))
    wr(SCR, bytes(tr.scr))
    L.h_set_gte(tr.gte)
    wr(CPU, bytes(set_cpu_state(list(tr.regs), tr.hi.value, tr.lo.value, T_BASE)))

def run_interp(words, tr):
    prepare(words, tr)
    end = T_BASE + 4 * tr.len
    L.h_interp(end, 3000)
    c = rd(CPU, CPU_SIZE)
    if struct.unpack_from("<I", c, OFF_PC)[0] != end: return None      # a loop that does not end: not a test
    s = snapshot(c)
    return s + (rd(RAM + (T_DATA & 0x1FFFFF), 0x400) + rd(SCR, 0x400),)

HELPERS = ["psx_jit_exec", "psx_jit_read", "psx_jit_write", "psx_jit_div", "psx_jit_divu", "psx_gte_get_data", "psx_gte_set_data",
           "psx_gte_get_ctl", "psx_gte_set_ctl", "psx_gte_command"]
NARGS = {"psx_jit_exec": 3, "psx_jit_read": 3, "psx_jit_write": 4, "psx_jit_div": 2, "psx_jit_divu": 2, "psx_gte_get_data": 1,
         "psx_gte_set_data": 2, "psx_gte_get_ctl": 1, "psx_gte_set_ctl": 2, "psx_gte_command": 1}

def run_jit(words, tr):
    prepare(words, tr)                                           # (the translator reads the program from the host RAM)
    end = T_BASE + 4 * tr.len
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    for a, sz in ((CODE_AT, 0x100000), (CPU_AT, 0x1000), (RAM_AT, 0x200000), (FLAGS_AT, 0x4000), (SENT_AT, 0x1000), (SCR_AT, 0x1000), (HELP_AT, 0x1000)):
        uc.mem_map(a, sz, UC_PROT_ALL)
    uc.mem_write(HELP_AT, struct.pack("<I", 0xE12FFF1E) * 1024)    # every helper address: bx lr
    uc.mem_write(RAM_AT + SYNC_LO, rd(RAM + SYNC_LO, SYNC_HI - SYNC_LO))
    uc.mem_write(SCR_AT, rd(SCR, 0x400))
    uc.mem_write(CPU_AT, rd(CPU, CPU_SIZE))

    def to_host():
        wr(CPU, bytes(uc.mem_read(CPU_AT, CPU_SIZE)))
        wr(RAM + SYNC_LO, bytes(uc.mem_read(RAM_AT + SYNC_LO, SYNC_HI - SYNC_LO)))
        wr(SCR, bytes(uc.mem_read(SCR_AT, 0x400)))
    def from_host():
        uc.mem_write(CPU_AT, rd(CPU, CPU_SIZE))
        uc.mem_write(RAM_AT + SYNC_LO, rd(RAM + SYNC_LO, SYNC_HI - SYNC_LO))
        uc.mem_write(SCR_AT, rd(SCR, 0x400))

    def helper(uc_, address, size, user):
        k = (address - HELP_AT) // 4
        if k >= len(HELPERS): raise RuntimeError("a call to an unknown helper %08x" % address)
        a = [uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
        to_host()
        name = HELPERS[k]
        r = getattr(L, name)(*[ctypes.c_uint32(x) for x in a[:NARGS[name]]])
        from_host()
        uc.reg_write(UC_ARM_REG_R0, (r or 0) & 0xFFFFFFFF)
    uc.hook_add(UC_HOOK_CODE, helper, None, HELP_AT, HELP_AT + 0x1000 - 1)

    buf = (ctypes.c_uint32 * 2048)()
    for _ in range(3000):
        pc = struct.unpack("<I", bytes(uc.mem_read(CPU_AT + OFF_PC, 4)))[0]
        if pc == end: break
        n = L.h_dump(pc, buf, 2048)
        if n == 0: raise RuntimeError("no block at %08x" % pc)
        uc.ctl_flush_tb()                                        # Unicorn keeps what it translated: the same address would run the old block
        uc.mem_write(CODE_AT, struct.pack("<%dI" % n, *buf[:n]))
        uc.reg_write(UC_ARM_REG_R11, CPU_AT); uc.reg_write(UC_ARM_REG_R10, RAM_AT); uc.reg_write(UC_ARM_REG_R9, FLAGS_AT)
        uc.reg_write(UC_ARM_REG_R4, SCR_AT); uc.reg_write(UC_ARM_REG_R8, SENT_AT); uc.reg_write(UC_ARM_REG_LR, SENT_AT)
        uc.emu_start(CODE_AT, SENT_AT)
    else:
        return None
    to_host()
    c = bytes(uc.mem_read(CPU_AT, CPU_SIZE))
    s = snapshot(c)
    return s + (bytes(uc.mem_read(RAM_AT + (T_DATA & 0x1FFFFF), 0x400)) + bytes(uc.mem_read(SCR_AT, 0x400)),)

def diff(x, y):
    d = [(i, "%08x" % x[0][i], "%08x" % y[0][i]) for i in range(32) if x[0][i] != y[0][i]]
    if x[1] != y[1]: d.append(("hi", "%08x" % x[1], "%08x" % y[1]))
    if x[2] != y[2]: d.append(("lo", "%08x" % x[2], "%08x" % y[2]))
    if x[3] != y[3]:
        gx, gy = struct.unpack("<64i", x[3]), struct.unpack("<64i", y[3])
        d += [("gte%d" % i, "%08x" % (gx[i] & 0xFFFFFFFF), "%08x" % (gy[i] & 0xFFFFFFFF)) for i in range(64) if gx[i] != gy[i]]
    if x[4] != y[4]: d.append(("mem", "", ""))
    return d

def main():
    trials = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    first = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    bad = skipped = 0
    for t in range(first, first + trials):
        tr = Trial(t)
        a = run_interp(tr.words, tr)
        if a is None: skipped += 1; continue
        try:
            b = run_jit(tr.words, tr)
        except Exception as e:
            print("trial %d: JIT run failed: %s" % (t, e)); bad += 1; continue
        if b is None: print("trial %d: the compiled code did not end" % t); bad += 1; continue
        if a != b:
            bad += 1
            if bad <= 4:
                for l in range(1, tr.len + 1):          # the shortest prefix that still differs
                    cut = tr.words[:l] + [0] * (tr.len - l) + tr.words[tr.len:]
                    x = run_interp(cut, tr)
                    if x is None: continue
                    try: y = run_jit(cut, tr)
                    except Exception: y = None
                    if y is None or x != y:
                        print("trial %d: prefix %d differs: %s" % (t, l, diff(x, y) if y else "run failed"))
                        print("  program: " + " ".join("%08x" % w for w in tr.words[max(0, l - 6):l + 1]))
                        break
    print("%d of %d trials differ (%d skipped: they do not end)" % (bad, trials, skipped))
    sys.exit(1 if bad else 0)

main()
