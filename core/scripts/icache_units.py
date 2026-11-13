#!/usr/bin/env python3
"""icache_units.py MAP > units.txt: the input sections of the code in a linker map, as lines "addr size objfile section".
Used by tests/snes/icache_layout.c to move sections in a recorded trace (see core/scripts/icache_sim.py, ICACHE_DUMP)."""
import os, re, sys

def main():
    lines = open(sys.argv[1], errors="replace").read().split("\n")
    out = []
    i = 0
    pending = None
    for l in lines:
        m = re.match(r"^ (\.text\S*|\.rodata\S*)\s*$", l)
        if m:
            pending = m.group(1)
            continue
        m = re.match(r"^ (\.text\S*)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", l)
        if m:
            name, addr, size, f = m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)
            pending = None
        elif pending and (m := re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$", l)):
            name, addr, size, f = pending, int(m.group(1), 16), int(m.group(2), 16), m.group(3)
            pending = None
        else:
            continue
        if size == 0 or addr == 0:
            continue
        f = os.path.basename(f.split("(")[-1].rstrip(")")) if "(" in f else os.path.basename(f)
        out.append((addr, size, f, name))
    for a, s, f, n in sorted(out):
        print(f"{a:x} {s:x} {f} {n}")

main()
