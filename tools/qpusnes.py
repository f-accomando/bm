#!/usr/bin/env python3
"""QPU programs for the SNES PPU on the V3D (prototype, item G2 of docs/progress/snes-gpu.md).

    tools/qpusnes.py -o src/gpu/qpuprog_snes.h     write the programs as C arrays

qp_snes_mode7 draws the Mode 7 layer of a frame: for each pixel of a line the picture of BG1 as ppu_line
does it (src/bm/snes_ppu.c, draw_mode7) when only BG1 is on (no sprites, windows or colour math): the
colour of the CGRAM entry the layer gives, or the backdrop. The program is built with the helpers of
tools/qpuprogs.py (hazards, the VPM store) and runs on the emulator of the V3D (tests/gpu/v3d_emu.c).

Block: 32 pixels of a line, 16 lanes, lane j takes the pixels 2j (low half of the stored word) and 2j + 1.
Uniforms (8 words): vram, pal, dst, tab, a, c, m7sel, blocks.
  vram    the 64 KB of video RAM (as the PPU has it: the tile map in the even bytes, the 8-bit tiles in the odd ones)
  pal     256 RGB565 halves: the colour table of ppu_line (the CGRAM colours as RGB565, the brightness applied; the ARM keeps it until the palette changes)
  dst     RGB565 pixels, 256 a line; the block n writes 32 pixels at dst + 64 * n (so a job takes whole lines)
  tab     two words for each line of the job: X and Y at the start of the line, as draw_mode7 makes them
          (the ARM does that: the 16-bit by 13-bit products of the set-up do not fit mul24)
  a, c    the matrix words M7A and M7C (signed 16 bits)
  m7sel   $211A: bit 0 flips x, bit 7-6 the screen over: 0/1 wrap, 2 transparent, 3 tile 0
  blocks  8 for each line (>= 1)
For each pixel, with xx = x or 255 - x:
  px = (X + a * xx) >> 8, py = (Y + c * xx) >> 8       (a * xx = (a + 32768) * xx - (xx << 15): mul24 is unsigned)
  out = (px | py) >> 10 != 0                              (an arithmetic shift: 0 only for 0..1023)
  t = vram[(((py >> 3) & 127) * 128 + ((px >> 3) & 127)) * 2], 0 if out and m7sel >> 6 == 3
  c = vram[(t * 64 + (py & 7) * 8 + (px & 7)) * 2 + 1], 0 if out and m7sel >> 6 == 2
  pixel = pal[c]
Every table is read with a gather: one word aligned address for each lane, the byte or half taken by a shift.
"""
import sys

import qpuasm
from qpuprogs import NOP, VDW_STRIDE, VPM_H32, fix, thrend, vdw, vpm_store


def tmu(addr_reg):
    """a gather of the word at the address in a register: the word in r4 two instructions later"""
    return ["mov t0s, %s          ; nop" % addr_reg, "nop                 ; nop           ; ldtmu0"]


def pixel(xx, out):
    """one pixel: xx the register of its x (after the flip), out the register that gets the RGB565 colour.
    px is in r2, py in r3 from the set-up to the character read; the flags of the 'outside' test stay until the end of the character read."""
    L = []
    # px, py
    L += ["nop                 ; mul24 r0, rb2, %s" % xx,
          "shl r1, %s, 15       ; nop" % xx,
          "sub r0, r0, r1       ; nop",
          "add r0, r0, ra5      ; nop",
          "asr r2, r0, 8        ; nop",
          "nop                 ; mul24 r0, rb3, %s" % xx,
          "shl r1, %s, 15       ; nop" % xx,
          "sub r0, r0, r1       ; nop",
          "add r0, r0, ra6      ; nop",
          "asr r3, r0, 8        ; nop",
          "or r0, r2, r3        ; nop",
          "asr.setf nop, r0, 10 ; nop"]                       # Z: inside the 1024 x 1024 plane
    # the tile number
    L += ["shr r0, r3, 3        ; nop",
          "and r0, r0, rb4      ; nop",
          "shl r0, r0, 7        ; nop",
          "shr r1, r2, 3        ; nop",
          "and r1, r1, rb4      ; nop",
          "add r0, r0, r1       ; nop",
          "shl r0, r0, 1        ; nop",
          "add r0, r0, ra0      ; nop",
          "and r1, r0, -4       ; nop"]
    L += tmu("r1")
    L += ["and r0, r0, 3        ; nop",
          "shl r0, r0, 3        ; nop",
          "shr r0, r4, r0       ; nop",
          "and r0, r0, rb8      ; nop",
          "and.ifnz r0, r0, rb5 ; nop"]                       # outside with the tile 0 rule: tile 0
    # the colour index
    L += ["shl r0, r0, 6        ; nop",
          "and r1, r3, 7        ; nop",
          "shl r1, r1, 3        ; nop",
          "add r0, r0, r1       ; nop",
          "and r1, r2, 7        ; nop",
          "add r0, r0, r1       ; nop",
          "shl r0, r0, 1        ; nop",
          "add r0, r0, ra0      ; nop",
          "add r0, r0, 1        ; nop",
          "and r1, r0, -4       ; nop"]
    L += tmu("r1")
    L += ["and r0, r0, 3        ; nop",
          "shl r0, r0, 3        ; nop",
          "shr r0, r4, r0       ; nop",
          "and r0, r0, rb8      ; nop",
          "and.ifnz r0, r0, rb6 ; nop"]                       # outside and transparent: index 0
    # the colour
    L += ["shl r0, r0, 1        ; nop",
          "add r0, r0, rb0      ; nop",
          "and r1, r0, -4       ; nop"]
    L += tmu("r1")
    L += ["and r0, r0, 2        ; nop",
          "shl r0, r0, 3        ; nop",
          "shr r0, r4, r0       ; nop",
          "and %s, r0, rb7      ; nop" % out]
    return L


def mode7():
    L = ["mov ra0, unif        ; nop",                      # vram
         "mov rb0, unif        ; nop",                      # pal
         "mov ra1, unif        ; nop",                      # dst
         "mov rb1, unif        ; nop",                      # tab
         "mov ra11, unif       ; nop",                      # a
         "ldi rb14, 32768",
         "add rb2, ra11, rb14  ; nop",                      # a + 32768
         "mov ra11, unif       ; nop",                      # c
         "add rb3, ra11, rb14  ; nop",
         "mov ra11, unif       ; nop",                      # m7sel
         "mov ra3, unif        ; nop",                      # blocks left
         "and ra4, ra11, 1     ; nop",                      # flip x
         "shr r0, ra11, 6      ; nop",                      # the screen-over mode
         "mov rb5, -1          ; nop",
         "sub.setf nop, r0, 3  ; nop",
         "mov.ifz rb5, 0       ; nop",                      # mode 3: the outside takes tile 0
         "mov rb6, -1          ; nop",
         "sub.setf nop, r0, 2  ; nop",
         "mov.ifz rb6, 0       ; nop",                      # mode 2: the outside is clear
         "ldi rb4, 127", "ldi rb7, 65535", "ldi rb8, 255", "ldi rb12, 64", "ldi rb15, 16",
         "ldi rb9, %d" % VPM_H32, "ldi rb10, %d" % vdw(1, 16), "ldi rb11, %d" % VDW_STRIDE,
         "shl rb13, elem_num, 1 ; nop",                     # the lane's first pixel in the block
         "mov ra2, 0           ; nop",                      # the block counter
         "loop:",
         # X and Y of the line: the table's two words, the same address in every lane
         "shr r0, ra2, 3       ; nop",
         "shl r0, r0, 3        ; nop",
         "add r0, r0, rb1      ; nop",
         "mov t0s, r0          ; nop",
         "add r0, r0, 4        ; nop",
         "mov t0s, r0          ; nop",
         "nop                 ; nop           ; ldtmu0",
         "mov ra5, r4          ; nop           ; ldtmu0",
         "mov ra6, r4          ; nop",
         # the two x of the lane, flipped when asked
         "and r1, ra2, 7       ; nop",
         "shl r1, r1, 5        ; nop",
         "add ra7, r1, rb13    ; nop",
         "add ra8, ra7, 1      ; nop",
         "sub.setf nop, ra4, 0 ; nop",
         "sub.ifnz ra7, rb8, ra7 ; nop",
         "sub.ifnz ra8, rb8, ra8 ; nop"]
    L += pixel("ra7", "ra9")
    L += pixel("ra8", "ra10")
    L += ["shl r0, ra10, rb15   ; nop",
          "or r0, r0, ra9       ; nop"]
    L += vpm_store(["r0"], "rb9", "rb10", "rb11")
    L += ["sub.setf ra3, ra3, 1 ; nop",
          NOP,
          "bra.anynz loop",
          "add ra1, ra1, rb12   ; nop",              # the delay slots: the next block
          "add ra2, ra2, 1      ; nop",
          NOP]
    return fix(L + thrend())


def loop_tail():
    """the end of a loop on the counter in ra3 (the same in every lane): decrement, branch, three delay slots"""
    return ["sub.setf ra3, ra3, 1 ; nop", NOP, "bra.anynz loop", NOP, NOP, NOP]


def bench_nop():
    """a job that does nothing: the fixed cost of one QPU job (launch, the cache clean and invalidate, the wait)"""
    return fix(thrend())


def bench_gather():
    """random gathers (the access pattern of the Mode 7 program). Uniforms: base, mask, iterations, start.
    Each lane draws an address base + (x & mask) from an LCG and four gathers are in flight; 64 words an iteration.
    A fourth uniform, start, is added to the seed so that the jobs do not read the same addresses (the TMU and the L2 cache would serve all but the first).
    mask: a multiple of 4 below the size of the region (64 KB: the VRAM; 8 MB: SDRAM that the caches do not hold)."""
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


def bench_stream():
    """sequential reads: a lane takes the word at base + ((pos + 4 * lane) & mask), four lines of 64 bytes in flight,
    256 bytes an iteration, pos grows by 256. Uniforms: base, mask (the region size - 1), iterations, start (the first pos
    of the job, a multiple of 256: the jobs read different lines)."""
    L = ["mov ra0, unif        ; nop",
         "mov rb2, unif        ; nop",
         "mov ra3, unif        ; nop",
         "mov rb7, unif        ; nop",
         "ldi rb5, 64", "ldi rb6, 256",
         "shl r0, elem_num, 2  ; nop",
         "add ra2, r0, rb7     ; nop",
         "mov r2, 0            ; nop",
         "loop:",
         "mov r1, ra2          ; nop"]
    for _ in range(4):
        L += ["and r0, r1, rb2      ; nop",
              "add r0, r0, ra0      ; nop",
              "mov t0s, r0          ; nop",
              "add r1, r1, rb5      ; nop"]
    L += ["add ra2, ra2, rb6    ; nop",
          "nop                 ; nop           ; ldtmu0",
          "add r2, r2, r4       ; nop           ; ldtmu0",
          "add r2, r2, r4       ; nop           ; ldtmu0",
          "add r2, r2, r4       ; nop           ; ldtmu0",
          "add r2, r2, r4       ; nop"]
    return fix(L + loop_tail() + thrend())


PROGRAMS = {"qp_snes_mode7": mode7, "qp_bench_nop": bench_nop, "qp_bench_gather": bench_gather,
            "qp_bench_stream": bench_stream}


def write_header(path):
    lines = ["/* Generated by tools/qpusnes.py: do not edit. SNES PPU programs of the QPUs (G2) and the micro-benchmark of the V3D (docs/progress/snes-gpu.md). */",
             "#ifndef GPU_QPUPROG_SNES_H", "#define GPU_QPUPROG_SNES_H", "", "#include <stdint.h>", ""]
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
