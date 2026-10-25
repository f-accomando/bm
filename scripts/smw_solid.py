#!/usr/bin/env python3
"""Finds which Map16 tiles of the first level are solid, by testing them in the emulator.

    python3 scripts/smw_solid.py ROM.sfc OUTDIR

For each Map16 number in the level it finds a cell with air above and one with air to its left,
puts Mario there and watches: from above, does he stand on it (his Y stays)? from the left,
does he stop (his X stays)? Writes solid.json {map16: {top, side, cells}} and prints a table.
Tile 0x25 is the empty cell."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smw_capture import *


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ex = sys.argv[2]
    level = json.load(open(ex + "/level.json"))
    g = Game(sys.argv[1])
    g.to_first_level()
    base = g.e.get_state()
    H, W = len(level), len(level[0])
    ids = sorted(k for k in {k for r in level for k in r} if k >= 0x100)
    free = lambda t: t < 0x100                   # the tiles below 0x100 were tested: none is solid
    res = {}
    for k in ids:
        out = {"top": None, "side": None, "bottom": None}
        for cy in range(2, H - 3):
            for cx in range(3, W - 3):
                if level[cy][cx] != k:
                    continue
                if out["top"] is None and free(level[cy - 1][cx]) and free(level[cy - 2][cx]):
                    g.e.set_state(base)
                    y0 = cy * 16 - 32
                    g.poke({0x94: (cx * 16) & 255, 0x95: (cx * 16) >> 8, 0x96: y0 & 255, 0x97: y0 >> 8, 0x7D: 0, 0x7B: 0, 0x72: 0})
                    g.step(n=10)
                    w = g.wram()
                    y = w[0x96] | w[0x97] << 8
                    out["top"] = abs(y - y0) <= 1
                    out["top_cell"] = [cx, cy]
                if out["side"] is None and free(level[cy][cx - 1]) and free(level[cy - 1][cx - 1]) and free(level[cy][cx - 2]):
                    g.e.set_state(base)
                    x0 = cx * 16 - 16
                    g.poke({0x94: x0 & 255, 0x95: x0 >> 8, 0x96: (cy * 16 - 16) & 255, 0x97: (cy * 16 - 16) >> 8, 0x7D: 0, 0x7B: 0x20, 0x72: 0})
                    g.step([RIGHT], 10)
                    w = g.wram()
                    x = w[0x94] | w[0x95] << 8
                    out["side"] = x < cx * 16 - 10
                    out["side_cell"] = [cx, cy]
                if out["bottom"] is None and free(level[cy + 1][cx]) and free(level[cy + 2][cx]) and free(level[cy + 3][cx]):
                    g.e.set_state(base)
                    y0 = cy * 16 + 2                       # his head just under the tile, moving up
                    g.poke({0x94: (cx * 16) & 255, 0x95: (cx * 16) >> 8, 0x96: y0 & 255, 0x97: y0 >> 8, 0x7D: 0xB0, 0x7B: 0, 0x72: 0x0B})
                    g.step(n=8)
                    w = g.wram()
                    y = w[0x96] | w[0x97] << 8
                    out["bottom"] = y > cy * 16 - 8              # he was stopped under it (a head bump), not carried through
                    out["bottom_cell"] = [cx, cy]
            if all(out[q] is not None for q in ("top", "side", "bottom")):
                break
        res["%03X" % k] = out
        print("%03X top %-5s side %-5s bottom %-5s" % (k, out["top"], out["side"], out["bottom"]))
    json.dump(res, open(ex + "/solid.json", "w"), indent=1)


if __name__ == "__main__":
    main()
