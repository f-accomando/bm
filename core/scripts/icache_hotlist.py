#!/usr/bin/env python3
"""icache_hotlist.py executed.txt > hot.txt: the sections that the search may move (see core/scripts/icache_search.sh).
executed.txt: lines "instructions size objfile section" (tests/snes/icache_layout.c with LIST=1). The movable set is the executed sections of the SNES
core files with 1000 instructions or more, the libc memset and memcpy, in the order of the phases of a line (CPU, bus, SPC700, S-DSP, PPU, Super FX)."""
import sys

order = {'snes_cpu_arm.S.o': 0, 'snes_cpu.c.o': 1, 'snes_bus.c.o': 2, 'snes_spc.c.o': 3, 'snes_dsp.c.o': 4,
         'snes_ppu.c.o': 5, 'snes_ppu_arm.S.o': 6, 'snes_gsu.c.o': 7, 'snes_gsu_arm.S.o': 8}
rows = []
for line in open(sys.argv[1]):
    c, sz, f, sec = line.split()
    if f in ('libc_a-memset.o', 'libc_a-memcpy-stub.o'):
        rows.append((2.5, f, sec, int(c)))
    elif f in order and sec != '.text.snes_cpu_cold' and int(c) >= 1000:
        rows.append((order[f], f, sec, int(c)))
rows.sort(key=lambda r: (r[0], -r[3]))
for r in rows:
    print(r[1], r[2])
