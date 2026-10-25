#!/usr/bin/env python3
"""Captures the background layer (BG2) of the first level from your own ROM in the emulator.

    python3 scripts/smw_bg2.py ROM.sfc OUTDIR

The layer scrolls at half the camera's speed on x and at the camera's speed on y ($1466, $1468 follow
$1A, $1C). Playing the level, every screen pixel that shows only the background (no foreground block,
no sprite, not the status bar, not Mario) is a sample of it at (x + camera x / 2, y + camera y).
Writes bg2.npy ((H, W, 4) uint8 RGBA, alpha 0 where nothing was seen) and bg2.json (the size, and how
well the picture repeats with a period of 128, 256 and 512 pixels on x)."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from smw_capture import *


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ex = sys.argv[2]
    level = json.load(open(ex + "/level.json"))
    empty = np.array([[v == 0x25 for v in row] for row in level])       # Map16 cells with nothing on layer 1
    H, W = 27 * 16, 2700
    img = np.zeros((H, W, 4), dtype=np.uint8)
    votes = np.zeros((H, W), dtype=np.int16)           # Boyer-Moore: a pixel keeps the colour most frames agree on
    conflicts = 0
    g = Game(sys.argv[1])
    g.to_first_level()
    g.step(n=90)                                        # past the fade-in
    for f in range(2400):
        g.poke(0x1497, 0x7F)
        g.step([RIGHT, Y] + ([B] if f % 40 < 24 else []))
        w = g.wram()
        if w[0x100] != 0x14:
            break
        cx, cy = w[0x1A] | w[0x1B] << 8, w[0x1C] | w[0x1D] << 8
        shot = g.e.get_screen()
        ok = np.zeros((224, 256), dtype=bool)
        ok[40:] = True                                  # not the status bar
        mx, my = (w[0x94] | w[0x95] << 8) - cx, (w[0x96] | w[0x97] << 8) - cy
        ok[max(0, my - 8):max(0, my + 40), max(0, mx - 10):max(0, mx + 28)] = False
        for i in range(12):                              # a box around each sprite
            if w[0x14C8 + i]:
                sx, sy = (w[0xE4 + i] | w[0x14E0 + i] << 8) - cx, (w[0xD8 + i] | w[0x14D4 + i] << 8) - cy
                ok[max(0, sy - 40):max(0, sy + 48), max(0, sx - 36):max(0, sx + 52)] = False
        for ty in range(cy // 16, min(27, (cy + 223) // 16 + 1)):          # the layer 1 blocks that are not empty
            for tx in range(cx // 16, (cx + 255) // 16 + 1):
                if tx < len(level[0]) and not empty[ty][tx]:
                    ok[max(0, ty * 16 - cy):max(0, ty * 16 - cy + 16), max(0, tx * 16 - cx):max(0, tx * 16 - cx + 16)] = False
        ys, xs = np.nonzero(ok)
        u, v = xs + cx // 2, ys + cy
        keep = (u < W) & (v < H)
        ys, xs, u, v = ys[keep], xs[keep], u[keep], v[keep]
        col = shot[ys, xs]
        cur = img[v, u]
        same = np.all(cur[:, :3] == col, axis=1) & (cur[:, 3] > 0)
        empty_px = cur[:, 3] == 0
        votes[v[same], u[same]] += 1
        conflicts += int((~same & ~empty_px).sum())
        lose = ~same & ~empty_px
        votes[v[lose], u[lose]] -= 1
        replace = empty_px | (lose & (votes[v, u] < 0))
        img[v[replace], u[replace], :3] = col[replace]
        img[v[replace], u[replace], 3] = 255
        votes[v[replace], u[replace]] = 0
    seen = img[..., 3] > 0
    rep = {}
    for p in (128, 256, 512):
        a, b = img[:, :W - p], img[:, p:]
        both = (a[..., 3] > 0) & (b[..., 3] > 0)
        rep[p] = float((np.all(a[..., :3] == b[..., :3], axis=2) & both).sum() / max(1, both.sum()))
    np.save(ex + "/bg2.npy", img)
    json.dump({"w": W, "h": H, "seen": float(seen.mean()), "conflicts": conflicts, "repeat": rep}, open(ex + "/bg2.json", "w"))
    print("seen %.2f of the %dx%d, conflicts %d, repeat %s" % (seen.mean(), W, H, conflicts, rep))


if __name__ == "__main__":
    main()
