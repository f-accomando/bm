#!/usr/bin/env python3
"""Captures the goal tape of the first level from your own ROM in the emulator.

    python3 scripts/smw_goal.py ROM.sfc OUTDIR

Runs to the goal, stops Mario before it, and rebuilds the tape (sprite 0x7B, three 16x16
entries) from OAM and VRAM; the colours come from the game's palette copy. Writes goal.json
(ox, oy, w, h from the sprite's x, y; the tape's x and y range) and tape.png.
Measured with Mario held away: the tape rises one pixel a tick from y 368 to 244 and falls back
the same way (248 ticks a cycle). After a touch it falls faster. Touching it starts the end: $1493 counts 255 down
while Mario walks right. The time (digits at WRAM $0F31) drops one every 35 ticks from 400."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from smw_capture import *
from smw_sprites import sprites, draw


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    g = Game(sys.argv[1])
    g.to_first_level()
    os.makedirs(sys.argv[2], exist_ok=True)
    for f in range(5200):
        g.poke(0x1497, 0x7F)
        before = bytes(g.wram())          # the sprite table of the frame that the next step draws
        x = before[0x94] | before[0x95] << 8
        g.step([] if x > 4650 else [RIGHT, Y] + ([B] if f % 40 < 24 else []))
        w = g.wram()
        for i in range(12):
            sy = w[0xD8 + i] | w[0x14D4 + i] << 8
            if w[0x14C8 + i] and w[0x9E + i] == 0x7B and sy < 340:
                cx, cy = w[0x1A] | w[0x1B] << 8, w[0x1C] | w[0x1D] << 8
                gx = w[0xE4 + i] | w[0x14E0 + i] << 8
                v = g.blocks()["VRA"]
                start = before[0x15EA + i] // 4
                ents = [e for e in sprites(before, v) if start <= e[0] < start + 3]  # the tape is three entries
                px = {}
                for e in reversed(ents):             # a lower entry is in front of a higher one
                    px.update(draw(e, v))
                mirror = before[0x703:0x703 + 512]                  # the game's palette copy: sprite rows start at 0x100
                col = {}
                for (qx, qy), (c, p) in px.items():
                    k = 0x100 + (p * 16 + c) * 2
                    r = mirror[k] | mirror[k + 1] << 8
                    col[(p, c)] = ((r & 31) * 255 // 31, ((r >> 5) & 31) * 255 // 31, ((r >> 10) & 31) * 255 // 31)
                x0, y0 = min(p[0] for p in px), min(p[1] for p in px)
                # keep the visible part: from the bar's left end, 24 pixels (the rest hides behind the right post)
                im = Image.new("RGBA", (24, max(p[1] for p in px) - y0 + 1), (0, 0, 0, 0))
                for (qx, qy), (c, p) in px.items():
                    if qx - x0 < 24:
                        r, gg, b = col[(p, c)]
                        im.putpixel((qx - x0, qy - y0), (r, gg, b, 255))
                im = im.crop((0, 0, 24, 8))           # the bar is the top rows; the rest of the entries is unrelated graphics
                bb = im.getbbox()
                im = im.crop(bb)
                im.save(sys.argv[2] + "/tape.png")
                json.dump({"x": gx, "ox": x0 + bb[0] - (gx - cx), "oy": y0 + bb[1] - (sy - cy), "w": im.width, "h": im.height,
                           "y_bottom": 368, "y_top": 288}, open(sys.argv[2] + "/goal.json", "w"))
                print("tape", im.size, "at", gx, sy)
                return


if __name__ == "__main__":
    main()
