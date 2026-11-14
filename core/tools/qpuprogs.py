#!/usr/bin/env python3
"""QPU programs that run outside the drawing cycle (bm3d 7.x, "user" programs).

    core/tools/qpuprogs.py -o core/src/gpu/qpuprog.h     write the programs as C arrays

They are assembled with core/tools/qpuasm.py and launched by v3d_qpu_run (the
V3D's SRQ registers). A program gets its uniforms from memory (the job's
uniform list), reads its data through the TMU (a write to t0s with one
address per lane, then the signal ldtmu0: r4), computes 16 lanes at a time,
writes its result rows to the VPM and stores them to memory with the VPM
DMA (VDW). It owns the VPM only between mutex_acquire and mutex_release.
A loop runs a count taken from a uniform: the branch tests the flags of a
counter that is the same in every lane.

Instruction streams are written here as lists of lines; hazards (a regfile
register read in the instruction after its write) are fixed by fix() with a
nop, so a program reads as its algorithm.
"""
import re
import sys

import qpuasm

NOP = "nop                 ; nop"
VPM_H32 = 0xA00                       # generic VPM access: horizontal, 32 bits; | stride << 12 | first row
VDW_H32 = 0x80004000                  # DMA store: | units << 23 | depth << 16 | row << 7 | word << 3
VDW_STRIDE = 0xC0000000               # DMA store: bytes skipped between two rows


def vdw(units, depth, row=0, word=0):
    return VDW_H32 | units << 23 | depth << 16 | row << 7 | word << 3


def regs_of(text):
    """(written, read) regfile registers of an instruction line"""
    written, read = set(), set()
    if text.startswith(("ldi ", "bra")):
        m = re.match(r"ldi\s+(r[ab]\d+)", text)
        if m:
            written.add(m[1])
        return written, read
    for part in text.split(";")[:2]:
        part = part.strip()
        m = re.match(r"(\w+)((?:\.\w+)*)\s+(.*)", part)
        if not m:
            continue
        args = [a.strip() for a in m[3].split(",")]
        d = re.match(r"(r[ab]\d+)", args[0])
        if d:
            written.add(d[1])
        for a in args[1:]:
            r = re.match(r"(r[ab]\d+)", a)
            if r:
                read.add(r[1])
    return written, read


def fix(lines):
    """the lines with a nop where an instruction reads a regfile register the
    one before wrote (the QPU does not allow it)"""
    out, prev = [], set()
    for text in lines:
        if text.endswith(":"):
            out.append(text)             # (the instruction before a label falls into it)
            continue
        w, r = regs_of(text)
        if r & prev:
            out.append(NOP)
        out.append(text)
        prev = w
    return "\n".join(t if t.endswith(":") else "        " + t for t in out) + "\n"


def thrend():
    return ["nop                 ; nop           ; thrend", NOP, NOP]


def vpm_store(rows, setup, dma, stride):
    """the rows (registers) to the VPM and out to the address in ra1, under the VPM mutex"""
    L = ["mov nop, mutex_acquire ; nop", "mov vw_setup, %s    ; nop" % setup]
    L += ["mov vpm, %s          ; nop" % r for r in rows]
    L += ["mov vw_setup, %s    ; nop" % dma, "mov vw_setup, %s    ; nop" % stride,
          "mov vw_addr, ra1     ; nop", "mov nop, vw_wait     ; nop", "mov mutex_release, 0 ; nop"]
    return L


# ---------------------------------------------------------------- particles
# Blocks of 16 particles, 5 rows of 16 floats each: x, y, vx, vy, life (320
# bytes). Uniforms: src, dst (bus addresses of the first block), count (>= 1),
# dt, g*dt. For every block, as the ARM's particles_step() does it:
#   x += vx * dt; y += vy * dt; vy += g * dt; life = max(life - dt, 0)
# src is read and dst written, so a run that fails halfway can be done again.

def particles():
    L = ["mov ra0, unif        ; nop",              # src
         "mov ra1, unif        ; nop",              # dst
         "mov ra3, unif        ; nop",              # blocks left
         "mov rb2, unif        ; nop",              # dt
         "mov rb3, unif        ; nop",              # g * dt
         "ldi rb4, 64",                             # one row
         "ldi rb5, 320",                            # one block
         "ldi rb6, %d" % (VPM_H32 | 1 << 12),
         "ldi rb7, %d" % vdw(5, 16),
         "ldi rb8, %d" % VDW_STRIDE,
         "shl r0, elem_num, 2  ; nop",              # the lane's byte
         "add ra0, ra0, r0     ; nop",              # src + 4 * lane
         "loop:"]
    for row in range(5):
        L += ["mov t0s, ra0         ; nop", "add ra0, ra0, rb4  ; nop"]
    L += ["nop                 ; nop           ; ldtmu0"]        # the five rows, in order
    for row in range(5):
        L += ["mov ra%d, r4         ; nop           %s" % (10 + row, "; ldtmu0" if row < 4 else "")]
    # ra10 x, ra11 y, ra12 vx, ra13 vy, ra14 life
    L += ["fmul r0, ra12, rb2   ; nop",              # vx * dt
          "fadd r1, ra10, r0    ; nop",              # x'
          "fmul r0, ra13, rb2   ; nop",              # vy * dt
          "fadd r2, ra11, r0    ; nop",              # y'
          "fadd r3, ra13, rb3   ; nop",              # vy'
          "fsub r0, ra14, rb2   ; nop",              # life - dt
          "fmax r0, r0, 0       ; nop"]              # life'
    L += vpm_store(["r1", "r2", "ra12", "r3", "r0"], "rb6", "rb7", "rb8")
    L += ["sub.setf ra3, ra3, 1 ; nop",
          NOP,
          "bra.anynz loop",
          "add ra1, ra1, rb5    ; nop",              # the delay slots
          NOP,
          NOP]
    return fix(L + thrend())


# ---------------------------------------------------------------- fade
# The levels of g16_fade_apply (core/src/bm/gfx16.c) on the QPUs. A block is 32
# pixels: lane j takes pixels 2j (low half of its word) and 2j + 1. Uniforms:
# src, dst (RGB565 pixels), lv (a level byte per pixel), index (65536 bytes:
# the colour's table, 255 = none), tab (16 x 256 halves), mulp (16 words:
# m0 | m1 << 10 | m2 << 20, the scale of the colours without a table), count
# (blocks, >= 1). Per pixel, with k its level and i = index[c]:
#   i != 255: tab[k * 256 + i]; else each channel c * m >> 8, at most 31/63/31.
# Every table is read with a gather (one address per lane, word aligned).

def pix(c, k, o):
    """one pixel: c its colour, k its level, o the result (regfile A all);
    ra16 is scratch"""
    return [
        "add r0, %s, rb0           ; nop" % c,            # index + c
        "and r1, r0, -4            ; nop",
        "mov t0s, r1               ; nop",
        "and r2, r0, 3             ; nop",
        "shl r2, r2, 3             ; nop",
        "nop                       ; nop           ; ldtmu0",
        "shr r0, r4, r2            ; nop",
        "and r0, r0, rb8           ; nop",                 # i
        "sub.setf nop, r0, rb8     ; nop",                 # Z: no table
        "shl r1, %s, 8             ; nop" % k,             # tab + 2 * (k * 256 + i)
        "add r1, r1, r0            ; nop",
        "shl r1, r1, 1             ; nop",
        "add r1, r1, rb1           ; nop",
        "and r2, r1, -4            ; nop",
        "mov t0s, r2               ; nop",
        "and r1, r1, 2             ; nop",
        "shl r1, r1, 3             ; nop",
        "shl r2, %s, 2             ; nop" % k,             # mulp + 4 * k
        "add r2, r2, rb2           ; nop",
        "mov t0s, r2               ; nop",
        "nop                       ; nop           ; ldtmu0",
        "shr r0, r4, r1            ; nop           ; ldtmu0",
        "and %s, r0, rb5           ; mov r3, r4" % o,      # the table's colour; r3 = the scale
        "and r0, r3, rb6           ; nop",                 # m0
        "shr r1, %s, 11            ; nop" % c,
        "nop                       ; mul24 r0, r0, r1",
        "shr r0, r0, 8             ; nop",
        "min r0, r0, rb9           ; nop",
        "shl ra16, r0, 11          ; nop",                 # red
        "shr r1, r3, 10            ; nop",
        "and r1, r1, rb6           ; nop",
        "shr r0, %s, 5             ; nop" % c,
        "and r0, r0, rb10          ; nop",
        "nop                       ; mul24 r0, r0, r1",
        "shr r0, r0, 8             ; nop",
        "min r0, r0, rb10          ; nop",
        "shl r0, r0, 5             ; nop",
        "or ra16, ra16, r0         ; nop",                 # green
        "shr r1, r3, rb15          ; nop",
        "and r1, r1, rb6           ; nop",
        "and r0, %s, rb9           ; nop" % c,
        "nop                       ; mul24 r0, r0, r1",
        "shr r0, r0, 8             ; nop",
        "min r0, r0, rb9           ; nop",
        "or ra16, ra16, r0         ; nop",                 # blue
        "mov.ifz %s, ra16          ; nop" % o,
    ]


def fade():
    L = ["mov ra0, unif        ; nop",              # src
         "mov ra1, unif        ; nop",              # dst
         "mov ra2, unif        ; nop",              # lv
         "mov rb0, unif        ; nop",              # index
         "mov rb1, unif        ; nop",              # tab
         "mov rb2, unif        ; nop",              # mulp
         "mov ra3, unif        ; nop",              # blocks left
         "ldi rb3, 64", "ldi rb4, 32", "ldi rb5, 65535", "ldi rb6, 1023", "ldi rb8, 255",
         "ldi rb9, 31", "ldi rb10, 63", "ldi rb14, 16", "ldi rb15, 20",
         "ldi rb11, %d" % VPM_H32, "ldi rb12, %d" % vdw(1, 16), "ldi rb13, %d" % VDW_STRIDE,
         "shl r0, elem_num, 2  ; nop",
         "add ra0, ra0, r0     ; nop",              # src + 4 * lane
         "shr r0, elem_num, 1  ; nop",
         "shl r0, r0, 2        ; nop",
         "add ra2, ra2, r0     ; nop",              # lv + 4 * (lane / 2)
         "and r0, elem_num, 1  ; nop",
         "shl rb7, r0, 4       ; nop",              # 16 on the odd lanes
         "loop:",
         "mov t0s, ra0         ; nop",
         "mov t0s, ra2         ; nop",
         "nop                  ; nop           ; ldtmu0",
         "mov ra8, r4          ; nop           ; ldtmu0",     # the two pixels
         "mov ra9, r4          ; nop",                          # their two levels, in a word
         "shr r0, ra9, rb7     ; nop",
         "and ra10, r0, rb8    ; nop",                          # k of the low pixel
         "shr r0, r0, 8        ; nop",
         "and ra11, r0, rb8    ; nop",                          # k of the high one
         "and ra12, ra8, rb5   ; nop",                          # the low pixel
         "shr ra13, ra8, rb14  ; nop"]                          # the high one
    L += pix("ra12", "ra10", "ra14")
    L += pix("ra13", "ra11", "ra15")
    L += ["shl r0, ra15, rb14   ; nop",
          "or r0, r0, ra14      ; nop"]
    L += vpm_store(["r0"], "rb11", "rb12", "rb13")
    L += ["sub.setf ra3, ra3, 1 ; nop",
          "add ra0, ra0, rb3    ; nop",
          "bra.anynz loop",
          "add ra1, ra1, rb3    ; nop",              # the delay slots
          "add ra2, ra2, rb4    ; nop",
          NOP]
    return fix(L + thrend())


# ---------------------------------------------------------------- micro-benchmark (qpu_bench.c)
def loop_tail():
    """the end of a loop on the counter in ra3 (the same in every lane): decrement, branch, three delay slots"""
    return ["sub.setf ra3, ra3, 1 ; nop", NOP, "bra.anynz loop", NOP, NOP, NOP]


def bench_nop():
    """a job that does nothing: the fixed cost of one QPU job"""
    return fix(thrend())


def bench_gather():
    """random gathers. Uniforms: base, mask, iterations, start. Each lane takes base + (x & mask), x from an LCG seeded with
    start; four gathers in flight, 64 words an iteration. mask: a multiple of 4 below the size of the region (512 bytes: a
    palette; 64 KB: the video RAM)."""
    L = ["mov ra0, unif        ; nop",
         "mov rb2, unif        ; nop",
         "mov ra3, unif        ; nop",
         "mov rb7, unif        ; nop",
         "ldi rb0, 69069", "ldi rb1, 16777215", "ldi rb4, 12345",
         "shl r0, elem_num, 3  ; nop",
         "add r0, r0, rb7      ; nop",
         "add ra2, r0, 1       ; nop",
         "mov r2, 0            ; nop",
         "loop:"]
    for _ in range(4):
        L += ["nop                 ; mul24 r0, ra2, rb0",
              NOP,
              "add r0, r0, rb4      ; nop",
              "and ra2, r0, rb1     ; nop",
              "and r1, ra2, rb2     ; nop",
              "add r1, r1, ra0      ; nop",
              "mov t0s, r1          ; nop"]
    L += ["nop                 ; nop           ; ldtmu0",
          "add r2, r2, r4       ; nop           ; ldtmu0",
          "add r2, r2, r4       ; nop           ; ldtmu0",
          "add r2, r2, r4       ; nop           ; ldtmu0",
          "add r2, r2, r4       ; nop"]
    return fix(L + loop_tail() + thrend())


def bench_store():
    """VPM DMA stores: 1 KB an iteration (16 rows of 16 words) to dst, dst grows by 1 KB. Uniforms: dst, iterations."""
    L = ["mov ra1, unif        ; nop",
         "mov ra3, unif        ; nop",
         "ldi rb6, %d" % (VPM_H32 | 1 << 12),
         "ldi rb7, %d" % vdw(16, 16),
         "ldi rb8, %d" % VDW_STRIDE,
         "ldi rb5, 1024",
         "mov r0, elem_num     ; nop",
         "loop:"]
    L += vpm_store(["r0"] * 16, "rb6", "rb7", "rb8")
    L += ["add ra1, ra1, rb5    ; nop"]
    return fix(L + loop_tail() + thrend())


# ---------------------------------------------------------------- SNES mix (snes_ppu.c, the colour maths with a sub screen)
# The mix of the lines that have colour maths and a sub screen, at brightness 15 and with no clip rule (the ARM loop
# ppu_mix_sub_arm). A line record (from the ARM) holds idx[256] (main colour index), flag[256] (1: the pixel takes the
# maths) and sidx[256] (sub colour index, 0: the fixed colour) at rec, rec + 256 and rec + 512. pal8 is the palette as
# three byte lanes of 8 times the 5-bit value (red, green, blue). Uniforms: lines; then for each line rec, dst, pal8,
# fix8, mode (0 sub, 1 sub half, 2 add, 3 add half). A lane makes pixels 2 * lane and 2 * lane + 1 of 32; eight vectors
# a line; the eight result rows go to the VPM and out to dst (512 bytes of RGB565) under the mutex.

def snes_pixel(j, out):
    """pixel j (0 or 1) of the lane: c in ra13 / ra15, sc in ra14 / ra16; flag word in ra11, sidx word in ra12; the RGB565 in out"""
    c, sc = ("ra13", "ra14") if j == 0 else ("ra15", "ra16")
    L = []
    if j == 0:
        L += ["and.setf r0, ra12, rb12 ; nop"]                    # Z: sidx 0
    else:
        L += ["shr r0, ra12, 8      ; nop", "and.setf r0, r0, rb12 ; nop"]
    L += ["mov r1, %s           ; nop" % sc,
          "mov.ifz r1, ra6      ; nop",                           # the fixed colour
          "v8subs r2, %s, r1    ; nop" % c,                       # mode 0
          "shr r3, r2, 1        ; nop",
          "and r3, r3, rb10     ; nop",                           # mode 1: the difference halved
          "sub.setf r0, ra7, rb13 ; nop",
          "mov.ifz r2, r3       ; nop",
          "v8adds r3, %s, r1    ; nop" % c,                       # mode 2
          "sub.setf r0, ra7, rb14 ; nop",
          "mov.ifz r2, r3       ; nop",
          "shr r3, %s, 1        ; nop" % c,
          "and r3, r3, rb10     ; nop",
          "shr r0, r1, 1        ; nop",
          "and r0, r0, rb10     ; nop",
          "add r3, r3, r0       ; nop",                           # mode 3: (c + sc) / 2, the lanes are multiples of 8
          "sub.setf r0, ra7, rb15 ; nop",
          "mov.ifz r2, r3       ; nop"]
    if j == 0:
        L += ["and.setf r0, ra11, rb12 ; nop"]                    # Z: no maths on this pixel
    else:
        L += ["shr r0, ra11, 8      ; nop", "and.setf r0, r0, rb12 ; nop"]
    L += ["mov.ifz r2, %s       ; nop" % c,
          "shr r0, r2, 3        ; nop",                           # RGB565 of the lanes
          "and r0, r0, rb11     ; nop",
          "shl r3, r0, 11       ; nop",
          "shr r0, r2, 11       ; nop",
          "and r0, r0, rb11     ; nop",
          "shl r1, r0, 1        ; nop",
          "shr r0, r0, 4        ; nop",
          "or r0, r1, r0        ; nop",
          "shl r0, r0, 5        ; nop",
          "or r3, r3, r0        ; nop",
          "shr r0, r2, rb24     ; nop",
          "and r0, r0, rb11     ; nop",
          "or %s, r3, r0        ; nop" % out]
    return L


def snes_mix():
    L = ["mov ra3, unif        ; nop",                          # lines
         "ldi rb10, 0x7F7F7F7F", "ldi rb11, 31", "ldi rb12, 255",
         "ldi rb13, 1", "ldi rb14, 2", "ldi rb15, 3",
         "ldi rb20, 32", "ldi rb21, 256", "ldi rb22, 512", "ldi rb24, 19", "ldi rb26, 16",
         "ldi rb17, %d" % (VPM_H32 | 1 << 12),
         "ldi rb18, %d" % vdw(8, 16),
         "ldi rb19, %d" % VDW_STRIDE,
         "shr r0, elem_num, 1  ; nop",
         "shl rb27, r0, 2      ; nop",                          # the lane's word: (lane / 2) * 4
         "and r0, elem_num, 1  ; nop",
         "shl rb28, r0, 4      ; nop",                          # the lane's shift: (lane & 1) * 16
         "line:",
         "mov ra4, unif        ; nop",                          # rec
         "mov ra1, unif        ; nop",                          # dst
         "mov ra5, unif        ; nop",                          # pal8
         "mov ra6, unif        ; nop",                          # fix8
         "mov ra7, unif        ; nop",                          # mode
         "add ra8, ra4, rb27   ; nop"]
    outs = ["ra%d" % (22 + v) for v in range(8)]
    for v in range(8):
        L += ["mov t0s, ra8         ; nop",                     # idx word
              "add r0, ra8, rb21    ; nop",
              "mov t0s, r0          ; nop",                     # flag word
              "add r0, ra8, rb22    ; nop",
              "mov t0s, r0          ; nop",                     # sidx word
              "nop                 ; nop           ; ldtmu0",
              "shr ra10, r4, rb28   ; nop           ; ldtmu0",
              "shr ra11, r4, rb28   ; nop           ; ldtmu0",
              "shr ra12, r4, rb28   ; nop",
              "and r0, ra10, rb12   ; nop",
              "shl r0, r0, 2        ; nop",
              "add t0s, r0, ra5     ; nop",                     # c of pixel 0
              "and r0, ra12, rb12   ; nop",
              "shl r0, r0, 2        ; nop",
              "add t0s, r0, ra5     ; nop",                     # sc of pixel 0
              "shr r0, ra10, 8      ; nop",
              "and r0, r0, rb12     ; nop",
              "shl r0, r0, 2        ; nop",
              "add t0s, r0, ra5     ; nop",                     # c of pixel 1
              "shr r0, ra12, 8      ; nop",
              "and r0, r0, rb12     ; nop",
              "shl r0, r0, 2        ; nop",
              "add t0s, r0, ra5     ; nop",                     # sc of pixel 1
              "nop                 ; nop           ; ldtmu0",
              "mov ra13, r4         ; nop           ; ldtmu0",
              "mov ra14, r4         ; nop           ; ldtmu0",
              "mov ra15, r4         ; nop           ; ldtmu0",
              "mov ra16, r4         ; nop"]
        L += snes_pixel(0, "ra17") + snes_pixel(1, "ra18")
        L += ["shl r0, ra18, rb26   ; nop",
              "or %s, ra17, r0      ; nop" % outs[v],
              "add ra8, ra8, rb20   ; nop"]
    L += ["mov nop, mutex_acquire ; nop", "mov vw_setup, rb17    ; nop"]
    L += ["mov vpm, %s          ; nop" % o for o in outs]
    L += ["mov vw_setup, rb18    ; nop", "mov vw_setup, rb19    ; nop",
          "mov vw_addr, ra1     ; nop", "mov nop, vw_wait     ; nop", "mov mutex_release, 0 ; nop",
          "sub.setf ra3, ra3, 1 ; nop", NOP, "bra.anynz line", NOP, NOP, NOP]
    return fix(L + thrend())


PROGRAMS = {"qp_particles": particles, "qp_fade": fade, "qp_bench_nop": bench_nop, "qp_bench_gather": bench_gather,
            "qp_bench_store": bench_store, "qp_snes_mix": snes_mix}


def write_header(path):
    lines = ["/* Generated by core/tools/qpuprogs.py: do not edit. User programs of the QPUs. */",
             "#ifndef GPU_QPUPROG_H", "#define GPU_QPUPROG_H", "", "#include <stdint.h>", ""]
    for name, fn in PROGRAMS.items():
        code = qpuasm.assemble(fn())
        lines.append(f"static const uint32_t {name}[{2 * len(code)}] __attribute__((aligned(8))) = {{")
        for (lo, hi), text in code:
            lines.append(f"    0x{lo:08x}, 0x{hi:08x},     /* {text} */")
        lines.append("};")
        lines.append("")
    lines.append("#endif")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "-o":
        write_header(sys.argv[2])
    else:
        print(__doc__)
