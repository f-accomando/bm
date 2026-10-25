#!/usr/bin/env python3
"""Records Mario's physics, frame by frame, from your own ROM in the emulator.

    python3 scripts/smw_trace.py ROM.sfc OUTDIR

Goes through the intro, walks the overworld right from Yoshi's House into the first level,
then plays fixed input scripts
and writes one CSV per script to OUTDIR: frame, buttons, x, y, x speed, y speed, in-air,
facing, P-meter. The CSV is the reference the .bme engine is tested against.
RAM: $94/$96 position, $7B/$7D speed (signed), $72 in air, $76 facing, $13E4 P-meter."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smw_capture import *

H = {"B": B, "Y": Y, "L": LEFT, "R": RIGHT, "A": A, "U": UP, "D": DOWN}
# name -> list of (frames, buttons) segments
SCRIPTS = {
    "walk":   [(20, ""), (90, "R"), (40, "")],
    "run":    [(20, ""), (150, "RY"), (60, "")],
    "jump":   [(20, ""), (50, "B"), (60, "")],
    "runjump": [(20, ""), (100, "RY"), (60, "RYB"), (60, "RY"), (60, "")],
    "turn":   [(20, ""), (60, "R"), (40, "L"), (30, "")],
}


def s8(v):
    return v - 256 if v > 127 else v


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    g = Game(sys.argv[1])
    g.to_first_level()
    start = g.e.get_state()
    os.makedirs(sys.argv[2], exist_ok=True)
    for name, segs in SCRIPTS.items():
        g.e.set_state(start)
        rows = []
        f = 0
        for n, keys in segs:
            for _ in range(n):
                g.step([H[k] for k in keys])
                w = g.wram()
                rows.append("%d,%s,%d,%d,%d,%d,%d,%d,%d" % (
                    f, keys, w[0x94] | w[0x95] << 8, w[0x96] | w[0x97] << 8, s8(w[0x7B]), s8(w[0x7D]),
                    w[0x72], w[0x76], w[0x13E4]))
                f += 1
        open("%s/trace_%s.csv" % (sys.argv[2], name), "w").write(
            "frame,keys,x,y,vx,vy,air,facing,pmeter\n" + "\n".join(rows) + "\n")
        print(name, len(rows), "frames; end x", rows[-1].split(",")[2])


if __name__ == "__main__":
    main()
