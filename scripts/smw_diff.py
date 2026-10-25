#!/usr/bin/env python3
"""Captures the pictures of sprites by taking them out of the screen, from your own ROM in the emulator.

    python3 scripts/smw_diff.py ROM.sfc OUTDIR ID [ID ...] [--frames N] [--stop X]

The sprite table and the sprite graphics of the savestate do not always match the picture on the screen
(graphics that the game sends again every frame). So for each frame where a sprite of number ID is on screen
it plays two frames twice from the same state: once as it is, once with that sprite's slot cleared. The pixels
that differ are the sprite, in the exact colours of the screen. Writes to OUTDIR/ID:
    pic_<key>.png, pics.json {key: {ox, oy, w, h, count, file}}, seq.csv (frame, slot, x, y, vx, vy, face, pic)
ox and oy are from the sprite's x and y. --stop X holds Mario still from x = X (a sprite that he would kill stays alive). The pictures are the player's own: they stay out of git."""
import csv, hashlib, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from PIL import Image
from smw_capture import *


def s8(v):
    return v - 256 if v > 127 else v


def main():
    args = [a for a in sys.argv[3:] if not a.startswith("--")]
    ids = [int(a, 16) for a in args if a.lower().replace("0x", "").isalnum()]
    frames = int(sys.argv[sys.argv.index("--frames") + 1]) if "--frames" in sys.argv else 2000
    ids = [i for i in ids if i != frames]
    stop = int(sys.argv[sys.argv.index("--stop") + 1]) if "--stop" in sys.argv else None
    ids = [i for i in ids if i != stop]
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    g = Game(sys.argv[1])
    g.to_first_level()
    out = {i: {"pics": {}, "rows": []} for i in ids}
    for f in range(frames):
        g.poke(0x1497, 0x7F)
        keys = [RIGHT, Y] + ([B] if f % 40 < 24 else [])
        w = g.wram()
        if stop is not None and (w[0x94] | w[0x95] << 8) >= stop:       # hold Mario still so that nothing is hurt
            keys = []
            g.poke({0x94: stop & 255, 0x95: stop >> 8, 0x96: 0x60, 0x97: 0x01, 0x7B: 0, 0x7D: 0})
        cx0 = w[0x1A] | w[0x1B] << 8
        slots = [(i, w[0x9E + i]) for i in range(12) if w[0x14C8 + i] and w[0x9E + i] in ids]
        slots = [(i, n) for i, n in slots if 0 <= (w[0xE4 + i] | w[0x14E0 + i] << 8) - cx0 < 256]
        if slots and f % 2 == 0:
            st = g.e.get_state()
            g.step(keys, 2)
            s1 = g.e.get_screen().copy()
            w1 = g.wram()
            cx, cy = w1[0x1A] | w1[0x1B] << 8, w1[0x1C] | w1[0x1D] << 8
            rec = [(i, n, w1[0xE4 + i] | w1[0x14E0 + i] << 8, w1[0xD8 + i] | w1[0x14D4 + i] << 8, s8(w1[0xB6 + i]), s8(w1[0xAA + i]),
                    w1[0x157C + i], w1[0x14C8 + i]) for i, n in slots]
            g.e.set_state(st)
            for i, n in slots:
                g.poke({0x14C8 + i: 0})
            g.step(keys, 2)
            s2 = g.e.get_screen().copy()
            g.e.set_state(st)
            for (i, n, x, y, vx, vy, face, status) in rec:
                if len(slots) > 1:
                    continue                                   # two at once: their pixels would mix
                mask = np.any(s1 != s2, axis=2)
                ys, xs = np.nonzero(mask)
                if len(xs) < 8:
                    continue
                x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
                im = np.zeros((y1 - y0 + 1, x1 - x0 + 1, 4), dtype=np.uint8)
                sub = mask[y0:y1 + 1, x0:x1 + 1]
                im[..., :3] = s1[y0:y1 + 1, x0:x1 + 1]
                im[..., 3] = np.where(sub, 255, 0)
                key = hashlib.md5(im.tobytes()).hexdigest()[:8]
                d = out[n]
                if key not in d["pics"]:
                    Image.fromarray(im, "RGBA").save("%s/%02X/pic_%s.png" % (sys.argv[2], n, key)) if os.path.isdir("%s/%02X" % (sys.argv[2], n)) else (os.makedirs("%s/%02X" % (sys.argv[2], n), exist_ok=True), Image.fromarray(im, "RGBA").save("%s/%02X/pic_%s.png" % (sys.argv[2], n, key)))
                    d["pics"][key] = {"ox": int(x0 - (x - cx)), "oy": int(y0 - (y - cy)), "w": int(im.shape[1]), "h": int(im.shape[0]), "count": 0,
                                      "file": "pic_%s.png" % key}
                d["pics"][key]["count"] += 1
                d["rows"].append((f, i, x, y, vx, vy, face, status, key))
        g.step(keys)
    for n, d in out.items():
        os.makedirs("%s/%02X" % (sys.argv[2], n), exist_ok=True)
        json.dump(d["pics"], open("%s/%02X/pics.json" % (sys.argv[2], n), "w"), indent=1)
        with open("%s/%02X/seq.csv" % (sys.argv[2], n), "w", newline="") as fh:
            wr = csv.writer(fh)
            wr.writerow("frame slot x y vx vy face status pic".split())
            wr.writerows(d["rows"])
        print("%02X: %d pictures, %d rows" % (n, len(d["pics"]), len(d["rows"])))


if __name__ == "__main__":
    main()
