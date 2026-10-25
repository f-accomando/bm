#!/usr/bin/env python3
"""Watches the vine of the block 0x11A and Mario climbing it, from your own ROM in the emulator.

    python3 scripts/smw_vine.py ROM.sfc OUTDIR

Bumps the block from below, then for 400 ticks logs the vine's head (sprite 0x79) and the Map16 cells that
change under it (vine.csv: tick, head y, changed cells). Then puts Mario on the vine and plays up, down,
left and right, logging his place, speed, pose ($13E0) and the climbing flag ($74) (climb.csv)."""
import csv, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smw_capture import *
from smw_sprites import sprites, draw, POSE, FACE
from smw_map16 import word, tile_rgba
from PIL import Image

LO, HI, SCR = 0xC800, 0x1C800, 0x1B0


def grid(w, screens=20):
    return [[w[LO + (x // 16) * SCR + y * 16 + x % 16] | w[HI + (x // 16) * SCR + y * 16 + x % 16] << 8 for x in range(screens * 16)]
            for y in range(27)]


def s8(v):
    return v - 256 if v > 127 else v


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    global EX
    ex = EX = sys.argv[2]
    level = json.load(open(ex + "/level.json"))
    cell = next((cx, cy) for cy, r in enumerate(level) for cx, v in enumerate(r) if v == 0x11A)
    cx, cy = cell
    g = Game(sys.argv[1])
    g.to_first_level()
    for f in range(8000):
        w = g.wram()
        camx = w[0x1A] | w[0x1B] << 8
        if camx + 40 <= cx * 16 <= camx + 200:
            break
        g.poke(0x1497, 0x7F)
        g.step([RIGHT, Y] + ([B] if f % 40 < 24 else []))
    g0 = grid(g.wram())
    g.poke({0x94: (cx * 16) & 255, 0x95: (cx * 16) >> 8, 0x96: (cy * 16 + 2) & 255, 0x97: (cy * 16 + 2) >> 8, 0x7D: 0xB0, 0x7B: 0, 0x1497: 0x7F})
    rows = []
    pics = {}
    global BEFORE
    BEFORE = g.e.get_screen().copy()
    for t in range(420):
        g.step([])
        w = g.wram()
        slot = [i for i in range(12) if w[0x14C8 + i] and w[0x9E + i] == 0x79]
        head = (w[0xD8 + slot[0]] | w[0x14D4 + slot[0]] << 8) if slot else -1
        if t % 4 == 0 and slot and "head" not in pics:
            pics.update(vine_pics(g, w, slot[0], cx, head))
        if t % 4 == 0:
            gr = grid(w)
            ch = [(x, y, g0[y][x], gr[y][x]) for y in range(27) for x in range(max(0, cx - 2), cx + 3) if gr[y][x] != g0[y][x]]
            rows.append((t, head, len(ch), ch[:6]))
    with open(ex + "/vine.csv", "w", newline="") as fh:
        wr = csv.writer(fh)
        wr.writerow("tick head_y changed cells".split())
        wr.writerows(rows)
    for r in rows[::10]:
        print(r)
    w = g.wram()
    gr = grid(w)
    col = sorted({(gr[y][x]) for y in range(27) for x in range(cx - 1, cx + 2) if gr[y][x] != g0[y][x]})
    print("new Map16 numbers near the vine:", ["%03X" % c for c in col])
    # Mario on the vine
    top = min((y for y in range(27) if gr[y][cx] != g0[y][cx]), default=cy)
    g.poke({0x94: (cx * 16) & 255, 0x95: (cx * 16) >> 8, 0x96: ((top + 6) * 16) & 255, 0x97: ((top + 6) * 16) >> 8, 0x7D: 0, 0x7B: 0, 0x1497: 0x7F})
    seq = [("up", 60, [UP]), ("down", 40, [DOWN]), ("left", 24, [LEFT]), ("right", 24, [RIGHT]), ("up", 30, [UP]), ("jump", 30, [B])]
    out = []
    t = 0
    for name, n, keys in seq:
        for _ in range(n):
            g.step(keys)
            w = g.wram()
            out.append((t, name, w[0x94] | w[0x95] << 8, w[0x96] | w[0x97] << 8, s8(w[0x7B]), s8(w[0x7D]), w[0x13E0], w[0x74], w[0x72]))
            t += 1
    with open(ex + "/climb.csv", "w", newline="") as fh:
        wr = csv.writer(fh)
        wr.writerow("tick action x y vx vy pose climbing air".split())
        wr.writerows(out)
    for r in out[::6]:
        print(r)
    climb_poses(g, ex, cx, top)


def vine_pics(g, w, slot, cx, head):
    """The vine's two pictures once the head is on the screen: the body (Map16 $006: four tiles of the layer 1
    tilemap, read from the VRAM) and the head (sprite $79: its OAM entries, in the sprite palette)."""
    camx, camy = w[0x1A] | w[0x1B] << 8, w[0x1C] | w[0x1D] << 8
    sy = head - camy
    if os.environ.get('VDBG'): print('head', head, 'camy', camy, 'sy', sy)
    if not 10 <= sy <= 30:
        return {}
    v = g.blocks()["VRA"]
    cg = w[0x703:0x703 + 512]
    gr = grid(w)
    cell = max((y for y in range(27) if gr[y][cx] == 0x006), default=27)      # the oldest cell: its tiles are in the VRAM for long
    if os.environ.get('VDBG'): print('cell', cell, [hex(r[cx]) for r in gr[cell-2:cell+3] if True][:5] if cell < 27 else None)
    if cell >= 27 or (cell * 16 - camy) > 200:
        return {}
    q = [word(v, 2 * cx, 2 * cell), word(v, 2 * cx, 2 * cell + 1), word(v, 2 * cx + 1, 2 * cell), word(v, 2 * cx + 1, 2 * cell + 1)]
    if os.environ.get('VDBG'): print('words', [hex(x) for x in q], 'cell', cell, 'camy', camy)
    body = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    shot = g.e.get_screen()               # the pixels that differ from the screen before the vine grew: the picture of the cell
    bx, by = cx * 16 - camx, cell * 16 - camy
    for yy in range(16):
        for xx in range(16):
            a, b = tuple(int(t) for t in shot[by + yy][bx + xx]), tuple(int(t) for t in BEFORE[by + yy][bx + xx])
            if a != b:
                body.putpixel((xx, yy), a + (255,))
    core = {body.getpixel((xx, yy)) for yy in range(16) for xx in (7, 8) if body.getpixel((xx, yy))[3]}
    for yy in range(16):                  # the sides can differ by the sliding background: keep only the vine's own colours
        for xx in range(16):
            if body.getpixel((xx, yy)) not in core and not (3 <= xx <= 13 and body.getpixel((xx, yy))[1] > body.getpixel((xx, yy))[0] + 60):
                body.putpixel((xx, yy), (0, 0, 0, 0))
    hx = (w[0xE4 + slot] | w[0x14E0 + slot] << 8) - camx
    # the game's copy of the high table is not final yet at this point: the head is one 16x16 entry at the vine's x
    ents = [(e[0], e[1] & 255 if e[1] < 0 else e[1], e[2], 16, e[4], e[5]) for e in sprites(w, v) if abs((e[1] & 255) - hx) < 6 and abs(e[2] - sy) < 4 and e[4] >= 0xA0]
    px = {}
    for e in reversed(ents):
        px.update(draw(e, v))
    if os.environ.get('VDBG'): print('hx', hx, 'sy', sy, 'ents', ents, [e for e in sprites(w, v)][:12])
    if not px:
        return {}
    xs, ys = [k[0] for k in px], [k[1] for k in px]
    x0, y0 = min(xs), min(ys)
    img = Image.new("RGBA", (max(xs) - x0 + 1, max(ys) - y0 + 1), (0, 0, 0, 0))
    for (qx, qy), (c, pl) in px.items():
        o = 0x100 + 2 * (pl * 16 + c)
        r = cg[o] | cg[o + 1] << 8
        img.putpixel((qx - x0, qy - y0), ((r & 31) * 255 // 31, ((r >> 5) & 31) * 255 // 31, ((r >> 10) & 31) * 255 // 31, 255))
    os.makedirs(EX + "/vine", exist_ok=True)
    body.save(EX + "/vine/body.png")
    img.save(EX + "/vine/head.png")
    json.dump({"ox": x0 - hx, "oy": y0 - sy, "w": img.width, "h": img.height, "cell": cell}, open(EX + "/vine/head.json", "w"))
    print("vine pictures: body, head", img.size, "offset", x0 - hx, y0 - sy)
    return {"head": 1}


def climb_poses(g, ex, cx, top):
    """The climbing poses ($15 and its twin): put Mario on the vine again, climb and take his pictures."""
    g.poke({0x94: (cx * 16) & 255, 0x95: (cx * 16) >> 8, 0x96: ((top + 8) * 16) & 255, 0x97: ((top + 8) * 16) >> 8, 0x7D: 0, 0x7B: 0, 0x1497: 0})
    for _ in range(60):                               # let the invincibility and the flash run out
        g.step([])
    g.poke({0x94: (cx * 16) & 255, 0x95: (cx * 16) >> 8, 0x96: ((top + 8) * 16) & 255, 0x97: ((top + 8) * 16) >> 8, 0x7D: 0, 0x7B: 0})
    poses, palette = {}, {}
    for _ in range(60):                               # the camera follows him back to the vine
        g.poke({0x94: (cx * 16) & 255, 0x95: (cx * 16) >> 8, 0x96: ((top + 8) * 16) & 255, 0x97: ((top + 8) * 16) >> 8, 0x7D: 0, 0x7B: 0})
        g.step([])
    poses, palette = {}, {}
    seq = [[UP]] * 70 + [[DOWN]] * 40 + [[]] * 10
    for keys in seq:
        g.step(keys)
        w = g.wram()
        v = g.blocks()["VRA"]
        x, y = w[0x94] | w[0x95] << 8, w[0x96] | w[0x97] << 8
        sx, sy = x - (w[0x1A] | w[0x1B] << 8), y - (w[0x1C] | w[0x1D] << 8)
        pose, face = w[POSE], w[FACE]
        mine = [e for e in sprites(w, v) if (e[5] >> 1) & 7 == 0 and not e[5] & 1 and e[4] < 0x40 and sx - 12 <= e[1] <= sx + 20 and sy - 4 <= e[2] <= sy + 36]
        key = "%02X_%d" % (pose, face)
        if not mine:
            continue
        px = {}
        for e in reversed(mine):
            px.update(draw(e, v))
        shot = g.e.get_screen()
        if w[0x1497] == 0 and w[0x1490] == 0:
            for (qx, qy), (c, p) in px.items():
                if 0 <= qx < 256 and 0 <= qy < 224:
                    rgb = tuple(int(q) for q in shot[qy][qx])
                    palette.setdefault(c, {}).setdefault(rgb, 0)
                    palette[c][rgb] += 1
        poses[key] = (pose, face, sx, sy, px)
    pal = {c: max(d, key=d.get) for c, d in palette.items()}
    meta = json.load(open(ex + "/mario/poses.json"))
    for key, (pose, face, sx, sy, px) in poses.items():
        xs, ys = [p[0] for p in px], [p[1] for p in px]
        x0, y0 = min(xs), min(ys)
        im = Image.new("RGBA", (max(xs) - x0 + 1, max(ys) - y0 + 1), (0, 0, 0, 0))
        for (qx, qy), (c, p) in px.items():
            im.putpixel((qx - x0, qy - y0), pal.get(c, (255, 0, 255)) + (255,))
        fn = "pose_%s.png" % key
        im.save(ex + "/mario/" + fn)
        meta[key] = {"pose": pose, "face": face, "ox": x0 - sx, "oy": y0 - sy, "w": im.width, "h": im.height, "file": fn}
    json.dump(meta, open(ex + "/mario/poses.json", "w"), indent=1)
    print("climb poses:", sorted(poses))


if __name__ == "__main__":
    main()
