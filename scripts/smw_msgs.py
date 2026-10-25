#!/usr/bin/env python3
"""Captures the messages of the message blocks of the first level, from your own ROM in the emulator.

    python3 scripts/smw_msgs.py ROM.sfc OUTDIR

For each message block (sprite 0xB9, places from spawns.json) it plays to the block, puts Mario under it
moving up, waits for the box to be written, and keeps the black box with its text as msgN.png
(in the screen's colours). msgs.json: {N: {x, y, w, h, frames}}: where the box sits
on the 256x224 picture and how many frames it took to be written. The pictures are the player's own."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image
from smw_capture import *


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ex = sys.argv[2]
    spawns = json.load(open(ex + "/spawns.json"))
    blocks = sorted({(s["x"], s["y"]) for s in spawns if s["id"] == 0xB9})
    g = Game(sys.argv[1])
    g.to_first_level()
    base = g.e.get_state()
    meta = {}
    for n, (bx, by) in enumerate(blocks, 1):
        g.e.set_state(base)
        for f in range(4000):                                   # walk until the block is on screen
            w = g.wram()
            camx = w[0x1A] | w[0x1B] << 8
            if camx + 40 <= bx <= camx + 200:
                break
            g.poke(0x1497, 0x7F)
            g.step([RIGHT, Y] + ([B] if f % 40 < 24 else []), 2)
        y0 = by + 16 - 14                                      # his head just under the block
        g.poke({0x94: bx & 255, 0x95: bx >> 8, 0x96: y0 & 255, 0x97: y0 >> 8, 0x7D: 0xB0, 0x7B: 0, 0x1497: 0x7F})
        opened, last, still = None, None, 0
        for f in range(900):                                   # until the text stops changing
            g.step(n=1)
            s = g.e.get_screen().copy()
            if opened is None and g.wram()[0x1426]:
                opened = f
            still = still + 1 if last is not None and (s == last).all() else 0
            last = s
            if opened is not None and still > 30:
                break
        w = g.wram()
        black = np.all(last == 0, axis=2)
        black[:30] = False                                     # not the status bar
        rows = np.nonzero(black.sum(axis=1) > 60)[0]
        if len(rows):                                          # the first run of rows: the box, not the black of the ground
            run = [rows[0]]
            for r in rows[1:]:
                if r != run[-1] + 1:
                    break
                run.append(r)
            rows = np.array(run)
        cols = np.nonzero(black[rows.min():rows.max() + 1].sum(axis=0) > (rows.max() - rows.min()) * 0.6)[0] if len(rows) else []
        print("block", bx, by, "message", w[0x1426], "opened at", opened, "frames", f, "box rows", rows.min() if len(rows) else None)
        if not len(rows):
            continue
        x0, x1, y0b, y1 = cols.min(), cols.max(), rows.min(), rows.max()
        im = np.zeros((y1 - y0b + 1, x1 - x0 + 1, 4), dtype=np.uint8)
        im[..., :3] = last[y0b:y1 + 1, x0:x1 + 1]
        im[..., 3] = 255
        Image.fromarray(im, "RGBA").save("%s/msg%d.png" % (ex, n))
        Image.fromarray(last).save("%s/msg%d_screen.png" % (ex, n))
        meta[n] = {"x": int(x0), "y": int(y0b), "w": int(im.shape[1]), "h": int(im.shape[0]), "frames": int(f), "id": int(w[0x1426])}
    json.dump(meta, open(ex + "/msgs.json", "w"), indent=1)
    print(meta)


if __name__ == "__main__":
    main()
