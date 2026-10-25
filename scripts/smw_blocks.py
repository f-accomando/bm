#!/usr/bin/env python3
"""Hits each kind of block of the first level from below and records what the game does.

    python3 scripts/smw_blocks.py ROM.sfc OUTDIR

For each Map16 number of the level that is not plain ground, finds one cell with air under
it, puts Mario there moving up and watches for 20 frames: the cell's new Map16 number, the coin
counter ($0DBF), the score, and any sprite, bounce sprite or effect that appears.
Writes blocks.json {map16: {x, y, after, coins, sprites, bounce}} and prints a table."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smw_capture import *

LO, HI, SCR = 0xC800, 0x1C800, 0x1B0


def cell(w, x, y):
    o = (x // 16) * SCR + y * 16 + (x % 16)
    return w[LO + o] | w[HI + o] << 8


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ex = sys.argv[2]
    level = json.load(open(ex + "/level.json"))
    g = Game(sys.argv[1])
    g.to_first_level()
    base = g.e.get_state()
    ids = sorted({k for row in level for k in row if k >= 0x100 and k not in (0x100, 0x101, 0x102, 0x103, 0x104)})
    ids += [0x02B, 0x02D, 0x02E]
    res = {}
    for k in ids:
        spot = None
        for cy in range(2, 24):
            for cx in range(2, len(level[0]) - 2):
                if level[cy][cx] == k and level[cy + 1][cx] == 0x25 and level[cy + 2][cx] == 0x25:
                    spot = (cx, cy)
                    break
            if spot:
                break
        if not spot:
            print("%03X: no free cell below" % k)
            continue
        g.e.set_state(base)
        cx, cy = spot
        # Mario's head just under the block, moving up fast
        g.poke({0x94: (cx * 16) & 255, 0x95: (cx * 16) >> 8, 0x96: (cy * 16 + 2) & 255, 0x97: (cy * 16 + 2) >> 8,
                0x7D: 0xB0, 0x7B: 0, 0x72: 0x0B})
        w0 = g.wram()
        coins0, score0 = w0[0xDBF], bytes(w0[0xF34:0xF37])
        for f in range(24):
            g.step(n=1)
        w = g.wram()
        sprites = [(i, w[0x9E + i], w[0x14C8 + i]) for i in range(12) if w[0x14C8 + i] and (w0[0x14C8 + i] == 0)]
        bounce = [(i, w[0x1699 + i]) for i in range(4) if w[0x1699 + i]]
        res["%03X" % k] = {"x": cx, "y": cy, "after": "%03X" % cell(w, cx, cy), "coins": w[0xDBF] - coins0,
                           "score": list(bytes(w[0xF34:0xF37])), "sprites": sprites, "bounce": bounce}
        print("%03X at %d,%d -> %03X coins %+d sprites %s bounce %s" % (k, cx, cy, cell(w, cx, cy), w[0xDBF] - coins0, sprites, bounce))
    json.dump(res, open(ex + "/blocks.json", "w"), indent=1)


if __name__ == "__main__":
    main()
