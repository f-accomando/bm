#!/usr/bin/env python3
"""Captures Mario's poses from your own ROM in the emulator.

    python3 scripts/smw_sprites.py ROM.sfc OUTDIR [frames]

Plays random input in the first level (when Mario dies, the run goes back to the start) and, every frame, rebuilds Mario's
picture from the sprite table (WRAM $0300 and $0400) and the sprite graphics in VRAM. The
palette comes from the screenshot pixels under his tiles. Writes to OUTDIR:
    poses.json     {"PPP_F": {pose, face, ox, oy, w, h, file}}  (face 1 = right; ox, oy from his position)
    pose_PPP_F.png one RGBA picture per pose and facing
    frames.csv     frame, pose, face, x speed, y speed, air, buttons: how the game picks a pose
The pictures are the player's own: they stay out of git. SMW's sprites are 16x16 and 32x32;
the OBJ graphics start at VRAM $C000 (name table 1 adds $2000)."""
import csv, json, os, random, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from smw_capture import *

POSE, FACE = 0x13E0, 0x76


def sprites(w, v):
    """Every visible OAM entry: (x, y, size, tile, attr) with the size in pixels."""
    out = []
    for i in range(128):
        x, y, t, a = w[0x300 + i * 4:0x300 + i * 4 + 4]
        hi = (w[0x400 + i // 4] >> ((i % 4) * 2)) & 3
        x |= (hi & 1) << 8
        if x >= 256:
            x -= 512
        if y >= 224:
            continue
        out.append((i, x, y, 32 if hi & 2 else 16, t, a))
    return out


def tile_index(v, o, px, py):
    c = 0
    for pl in range(4):
        c |= ((v[(o + (pl & 1) + 2 * py + (16 if pl >= 2 else 0)) % 65536] >> (7 - px)) & 1) << pl
    return c


def draw(ent, v):
    """The pixels of one entry as {(x, y): colour index}, flips applied."""
    _, x, y, sz, t, a = ent
    fx, fy, nb = (a >> 6) & 1, (a >> 7) & 1, a & 1
    px = {}
    for ty in range(sz // 8):
        for tx in range(sz // 8):
            o = (0xC000 + (0x2000 if nb else 0) + ((t + tx + ty * 16) & 255) * 32) % 65536
            for py in range(8):
                for pxl in range(8):
                    c = tile_index(v, o, pxl, py)
                    if c:
                        X = tx * 8 + pxl
                        Y = ty * 8 + py
                        X = sz - 1 - X if fx else X
                        Y = sz - 1 - Y if fy else Y
                        px[(x + X, y + Y)] = (c, (a >> 1) & 7)     # as the emulator draws it: no extra line
    return px


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    g = Game(sys.argv[1])
    g.to_first_level()
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    rnd = random.Random(7)
    start = g.e.get_state()
    sets = [[], [RIGHT], [RIGHT, Y], [RIGHT, Y, B], [RIGHT, B], [LEFT], [LEFT, Y], [LEFT, Y, B], [B], [DOWN],
            [UP], [RIGHT, A], [LEFT, B], [DOWN, RIGHT], [A], [RIGHT, Y, A]]
    frames = int(sys.argv[3]) if len(sys.argv) > 3 else 4000
    poses, palette, rows = {}, {}, []
    keys, hold = [], 0
    for f in range(frames):
        if hold == 0:
            keys, hold = rnd.choice(sets), (rnd.randint(60, 150) if rnd.random() < 0.3 else rnd.randint(6, 28))
        hold -= 1
        g.step(keys)
        w = g.wram()
        if w[0x100] != 0x14 or w[0x71] not in (0, 5, 6, 7, 8, 9):    # dead or leaving: start again
            g.e.set_state(start)
            continue
        v = g.blocks()["VRA"]
        x, y = w[0x94] | w[0x95] << 8, w[0x96] | w[0x97] << 8
        cx, cy = w[0x1A] | w[0x1B] << 8, w[0x1C] | w[0x1D] << 8
        sx, sy = x - cx, y - cy
        pose, face = w[POSE], w[FACE]
        mine = [e for e in sprites(w, v)
                if (e[5] >> 1) & 7 == 0 and not e[5] & 1 and e[4] < 0x40 and sx - 12 <= e[1] <= sx + 20 and sy - 4 <= e[2] <= sy + 36]
        rows.append((f, pose, face, w[0x7B] if w[0x7B] < 128 else w[0x7B] - 256, w[0x7D] if w[0x7D] < 128 else w[0x7D] - 256,
                     w[0x72], "".join(n for n, k in zip("RLUDBYAX", (RIGHT, LEFT, UP, DOWN, B, Y, A, X)) if k in keys)))
        key = "%02X_%d" % (pose, face)
        if not mine or key in poses:
            continue
        px = {}
        for e in mine:
            px.update(draw(e, v))
        # the palette: the colour on the screenshot under each index of Mario's tiles (small Mario)
        shot = g.e.get_screen()
        calm = w[0x1497] == 0 and w[0x1490] == 0 and w[0x71] == 0
        for (qx, qy), (c, p) in px.items():
            if calm and 0 <= qx < 256 and 0 <= qy < 224:
                rgb = tuple(int(q) for q in shot[qy][qx])
                palette.setdefault(c, {}).setdefault(rgb, 0)
                palette[c][rgb] += 1
        poses[key] = (pose, face, sx, sy, px)
    pal = {c: max(d, key=d.get) for c, d in palette.items()}
    meta = {}
    for key, (pose, face, sx, sy, px) in poses.items():
        xs, ys = [p[0] for p in px], [p[1] for p in px]
        x0, y0, x1, y1 = min(xs), min(ys), max(xs), max(ys)
        im = Image.new("RGBA", (x1 - x0 + 1, y1 - y0 + 1), (0, 0, 0, 0))
        for (qx, qy), (c, p) in px.items():
            r, gg, b = pal.get(c, (255, 0, 255))
            im.putpixel((qx - x0, qy - y0), (r, gg, b, 255))
        fn = "pose_%s.png" % key
        im.save(out + "/" + fn)
        meta[key] = {"pose": pose, "face": face, "ox": x0 - sx, "oy": y0 - sy, "w": im.width, "h": im.height, "file": fn}
    json.dump(meta, open(out + "/poses.json", "w"), indent=1)
    with open(out + "/frames.csv", "w", newline="") as fh:
        wr = csv.writer(fh)
        wr.writerow("frame pose face vx vy air keys".split())
        wr.writerows(rows)
    print(len(meta), "poses;", len(pal), "colours")


if __name__ == "__main__":
    main()
