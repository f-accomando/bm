#!/usr/bin/env python3
"""Watches what the question blocks of the first level give, from your own ROM in the emulator.

    python3 scripts/smw_items.py ROM.sfc OUTDIR [map16 numbers, default 126 11A]

Plays to the right until the first block of each kind is on screen, puts Mario under it moving
up, and records for 200 frames every sprite that appears: state each frame (items.csv) and its
pictures (item_*.png). The game's palette copy gives the colours. Prints what appeared."""
import csv, hashlib, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from smw_capture import *
from smw_sprites import sprites, draw


def s8(v):
    return v - 256 if v > 127 else v


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ex = sys.argv[2]
    kinds = [int(a, 16) for a in sys.argv[3:]] or [0x126, 0x11A]
    level = json.load(open(ex + "/level.json"))
    g = Game(sys.argv[1])
    g.to_first_level()
    rows, pics = [], {}
    for k in kinds:
        cell = next(((cx, cy) for cy, r in enumerate(level) for cx, v in enumerate(r)
                     if v == k and level[cy + 1][cx] == 0x25 and level[cy + 2][cx] == 0x25), None)
        if not cell:
            print("%03X: no free cell" % k)
            continue
        cx, cy = cell
        for f in range(4000):                                   # walk until the block is on screen
            w = g.wram()
            camx = w[0x1A] | w[0x1B] << 8
            if camx + 40 <= cx * 16 <= camx + 200:
                break
            g.poke(0x1497, 0x7F)
            g.step([RIGHT, Y] + ([B] if f % 40 < 24 else []), 2)
        g.poke({0x94: (cx * 16) & 255, 0x95: (cx * 16) >> 8, 0x96: (cy * 16 + 2) & 255, 0x97: (cy * 16 + 2) >> 8,
                0x7D: 0xB0, 0x7B: 0, 0x1497: 0x7F})
        first = None
        for f in range(200):
            before = bytes(g.wram())
            g.step()
            w = g.wram()
            cam_x, cam_y = w[0x1A] | w[0x1B] << 8, w[0x1C] | w[0x1D] << 8
            v = None
            for i in range(12):
                if not w[0x14C8 + i]:
                    continue
                num = w[0x9E + i]
                if num in (5, 1, 4, 0) or first is None and before[0x14C8 + i]:
                    continue
                if first is None:
                    first = f
                x, y = w[0xE4 + i] | w[0x14E0 + i] << 8, w[0xD8 + i] | w[0x14D4 + i] << 8
                row = [k, f, i, num, w[0x14C8 + i], x, y, s8(w[0xB6 + i]), s8(w[0xAA + i]), w[0x157C + i], w[0x19], w[0x94] | w[0x95] << 8, ""]
                rows.append(row)
                if v is None:
                    v = g.blocks()["VRA"]
                start = before[0x15EA + i] // 4
                ents = [e for e in sprites(before, v) if start <= e[0] < start + 4]
                px = {}
                for e in ents:
                    px.update(draw(e, v))
                if not px or max(q[0] for q in px) - min(q[0] for q in px) > 40:
                    continue
                x0, y0 = min(q[0] for q in px), min(q[1] for q in px)
                key = hashlib.md5(repr(sorted(((a - x0, b - y0), c) for (a, b), c in px.items())).encode()).hexdigest()[:6]
                name = "%03X_%02X_%s" % (k, num, key)
                row[-1] = name
                if name not in pics:
                    mirror = before[0x703:0x703 + 512]
                    im = Image.new("RGBA", (max(q[0] for q in px) - x0 + 1, max(q[1] for q in px) - y0 + 1), (0, 0, 0, 0))
                    for (qx, qy), (c, p) in px.items():
                        o = 0x100 + (p * 16 + c) * 2
                        r = mirror[o] | mirror[o + 1] << 8
                        im.putpixel((qx - x0, qy - y0), ((r & 31) * 255 // 31, ((r >> 5) & 31) * 255 // 31, ((r >> 10) & 31) * 255 // 31, 255))
                    im.save("%s/item_%s.png" % (ex, name))
                    pics[name] = {"num": num, "w": im.width, "h": im.height, "ox": x0 - (x - cam_x), "oy": y0 - (y - cam_y)}
        print("%03X: first sprite at frame %s" % (k, first))
    with open(ex + "/items.csv", "w", newline="") as fh:
        wr = csv.writer(fh)
        wr.writerow("block frame slot id status x y vx vy face power mario_x pic".split())
        wr.writerows(rows)
    json.dump(pics, open(ex + "/items.json", "w"), indent=1)
    print(len(rows), "rows,", len(pics), "pictures:", sorted(pics))


if __name__ == "__main__":
    main()
