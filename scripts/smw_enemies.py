#!/usr/bin/env python3
"""Captures the sprites (enemies, items) of the first level from your own ROM in the emulator.

    python3 scripts/smw_enemies.py ROM.sfc OUTDIR [frames]

Runs the level to the right (Mario cannot be hurt) and, every frame, for every sprite slot
that is in use: its state from WRAM and its picture from the sprite table and VRAM. The
OAM entries of a slot start at $15EA/4 and last until the next slot's first entry. Writes:
    states.csv   frame, slot, id, status, x, y, vx, vy, face, mario x, mario y, mario vy, picture name
    pics.json    {"ID_STATUS_n": {id, status, ox, oy, w, h, file, count}} (one per distinct picture)
    pic_*.png    the pictures, in the colours of the screen
The pictures are the player's own: they stay out of git.
WRAM per slot: $9E number, $14C8 status, $E4/$14E0 x, $D8/$14D4 y, $B6 x speed, $AA y speed,
$157C facing, $15EA OAM start."""
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
    g = Game(sys.argv[1])
    g.to_first_level()
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    pics, palette, rows = {}, {}, []
    frames = int(sys.argv[3]) if len(sys.argv) > 3 else 2500
    for f in range(frames):
        g.poke(0x1497, 0x7F)
        g.step([RIGHT, Y] + ([B] if f % 40 < 24 else []))
        w = g.wram()
        if w[0x100] != 0x14:
            break
        v = g.blocks()["VRA"]
        shot = None
        cx, cy = w[0x1A] | w[0x1B] << 8, w[0x1C] | w[0x1D] << 8
        ents = sprites(w, v)
        active = [i for i in range(12) if w[0x14C8 + i]]
        starts = sorted((w[0x15EA + i] // 4, i) for i in active)
        for n, (start, i) in enumerate(starts):
            end = starts[n + 1][0] if n + 1 < len(starts) else 128
            sx, sy = w[0xE4 + i] | w[0x14E0 + i] << 8, w[0xD8 + i] | w[0x14D4 + i] << 8
            scx, scy = sx - cx, sy - cy
            mine = [e for e in ents if start <= e[0] < end and scx - 32 <= e[1] <= scx + 32 and scy - 40 <= e[2] <= scy + 40]
            px = {}
            for e in reversed(mine):                 # a lower entry is in front of a higher one
                px.update(draw(e, v))
            name = ""
            if px and 0 <= sx - cx < 256 and (max(q[0] for q in px) - min(q[0] for q in px) < 64) and (max(q[1] for q in px) - min(q[1] for q in px) < 64):
                mx0, my0 = min(q[0] for q in px), min(q[1] for q in px)
                key = hashlib.md5(repr(sorted(((a - mx0, b - my0), v) for (a, b), v in px.items())).encode()).hexdigest()[:8]
                name = "%02X_%X_%s" % (w[0x9E + i], w[0x14C8 + i], key)
            rows.append((f, i, w[0x9E + i], w[0x14C8 + i], sx, sy, s8(w[0xB6 + i]), s8(w[0xAA + i]), w[0x157C + i],
                         w[0x94] | w[0x95] << 8, w[0x96] | w[0x97] << 8, s8(w[0x7D]), name))
            if not name:
                continue
            if name in pics:
                pics[name]["count"] += 1
                continue
            mirror = w[0x703:0x703 + 512]
            col = {}
            for (qx, qy), (c, p) in px.items():
                if p >= 1:                  # sprite rows 1-7 are in the game's palette copy; row 0 is Mario's
                    k = 0x100 + (p * 16 + c) * 2
                    r = mirror[k] | mirror[k + 1] << 8
                    col[(p, c)] = ((r & 31) * 255 // 31, ((r >> 5) & 31) * 255 // 31, ((r >> 10) & 31) * 255 // 31)
                elif 0 <= qx < 256 and 0 <= qy < 224:
                    if shot is None:
                        shot = g.e.get_screen()
                    palette.setdefault((p, c), {}).setdefault(tuple(int(q) for q in shot[qy][qx]), 0)
                    palette[(p, c)][tuple(int(q) for q in shot[qy][qx])] += 1
            pics[name] = {"id": w[0x9E + i], "status": w[0x14C8 + i], "count": 1, "px": px, "sx": sx - cx, "sy": sy - cy, "col": col}
    pal = {k: max(d, key=d.get) for k, d in palette.items()}
    meta = {}
    for name, d in pics.items():
        xs, ys = [p[0] for p in d["px"]], [p[1] for p in d["px"]]
        x0, y0 = min(xs), min(ys)
        im = Image.new("RGBA", (max(xs) - x0 + 1, max(ys) - y0 + 1), (0, 0, 0, 0))
        for (qx, qy), (c, p) in d["px"].items():
            r, gg, b = d["col"].get((p, c)) or pal.get((p, c), (255, 0, 255))
            im.putpixel((qx - x0, qy - y0), (r, gg, b, 255))
        fn = "pic_%s.png" % name
        im.save(out + "/" + fn)
        meta[name] = {"id": d["id"], "status": d["status"], "ox": x0 - d["sx"], "oy": y0 - d["sy"], "w": im.width, "h": im.height,
                      "file": fn, "count": d["count"]}
    json.dump(meta, open(out + "/pics.json", "w"), indent=1)
    with open(out + "/states.csv", "w", newline="") as fh:
        wr = csv.writer(fh)
        wr.writerow("frame slot id status x y vx vy face mx my mvy pic".split())
        wr.writerows(rows)
    print(len(meta), "pictures,", len(rows), "state rows")


if __name__ == "__main__":
    main()
