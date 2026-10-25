#!/usr/bin/env python3
"""Collects the Map16 tile definitions of the first level from the emulator.

    python3 scripts/smw_map16.py ROM.sfc OUTDIR

Runs the level to the right. At every frame group it reads the level's Map16 grid (WRAM
$7EC800 low, $7FC800 high; screens of 16x27 cells, $1B0 bytes each) and the layer 1 tilemap
(VRAM $5000, 64 tiles wide) and pairs each Map16 number with the four tile words the game drew
for it. Writes map16.json {number: [TL, BL, TR, BR]}, level.json (size and grid) and one
vram_NN.bin per 8 new tiles found, for the tile graphics."""
import json, os, struct, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smw_capture import *

LO, HI, SCR = 0xC800, 0x1C800, 0x1B0


def cell(w, x, y):
    o = (x // 16) * SCR + y * 16 + (x % 16)
    return w[LO + o] | w[HI + o] << 8


def word(v, tx, ty):
    i = 0x5000 + ((tx % 64) // 32) * 0x800 + (ty % 32) * 64 + (tx % 32) * 2
    return v[i] | v[i + 1] << 8


def tile_rgba(v, cg, wd):
    """The 8x8 tile of a tilemap word as 64 RGB tuples (None = transparent), flips applied."""
    n, pal, fx, fy = wd & 0x3FF, (wd >> 10) & 7, (wd >> 14) & 1, (wd >> 15) & 1
    out = []
    for y in range(8):
        for x in range(8):
            sx, sy = (7 - x if fx else x), (7 - y if fy else y)
            o, c = n * 32, 0
            for pl in range(4):
                c |= ((v[(o + (pl & 1) + 2 * sy + (16 if pl >= 2 else 0)) % 65536] >> (7 - sx)) & 1) << pl
            if not c:
                out.append(None)
                continue
            r = cg[2 * (pal * 16 + c)] | cg[2 * (pal * 16 + c) + 1] << 8
            out.append(((r & 31) * 255 // 31, ((r >> 5) & 31) * 255 // 31, ((r >> 10) & 31) * 255 // 31))
    return tuple(out)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    g = Game(sys.argv[1])
    g.to_first_level()
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    defs, seen_conflict = {}, set()
    cells, sheet = {}, []          # unique 8x8 images -> sheet index (0 is empty)
    words, first = {}, None
    w = g.wram()
    screens = w[0x5E]
    for f in range(3600):
        keys = [RIGHT, Y] + ([B] if f % 50 < 30 else [])
        g.poke(0x1497, 0x7F)              # hit flash: Mario cannot be hurt, so the run reaches the end
        g.step(keys, 4)
        w = g.wram()
        v = g.blocks()["VRA"]
        cg = w[0x703:0x703 + 512]
        if first is None:
            first = [[cell(w, x, y) for x in range(screens * 16)] for y in range(27)]
        camx, camy = w[0x1A] | w[0x1B] << 8, w[0x1C] | w[0x1D] << 8
        for cy in range(camy // 16, camy // 16 + 14):
            for cx in range(camx // 16 + 1, camx // 16 + 15):     # skip the streaming edges
                k = cell(w, cx, cy)
                q = [word(v, 2 * cx, 2 * cy), word(v, 2 * cx, 2 * cy + 1),
                     word(v, 2 * cx + 1, 2 * cy), word(v, 2 * cx + 1, 2 * cy + 1)]
                if k not in defs:
                    ids = []
                    for wd in q:
                        im = tile_rgba(v, cg, wd)
                        if all(p is None for p in im):
                            ids.append(0)
                            continue
                        if im not in cells:
                            cells[im] = len(cells) + 1
                            sheet.append(im)
                        ids.append(cells[im])
                    defs[k] = ids
                    words[k] = q
                elif words[k] != q:
                    seen_conflict.add(k)
        if f % 100 == 0:
            print(f, "camera", camx, "mario", w[0x94] | w[0x95] << 8, "tiles", len(defs))
        if camx >= (screens - 1) * 256 - 8 or w[0x100] != 0x14:
            break
    json.dump({"%03X" % k: q for k, q in sorted(defs.items())}, open(out + "/map16.json", "w"))
    json.dump(first, open(out + "/level.json", "w"))
    cols = 16
    rows_n = (len(sheet) + 2 + cols - 1) // cols      # one spare cell after the last: the collision marker
    img = [[(0, 0, 0, 0)] * (cols * 8) for _ in range(rows_n * 8)]
    for i, im in enumerate(sheet, 1):
        for y in range(8):
            for x in range(8):
                p = im[y * 8 + x]
                img[(i // cols) * 8 + y][(i % cols) * 8 + x] = (p + (255,)) if p else (0, 0, 0, 0)
    raw = b"".join(b"\0" + bytes(c for px in row for c in px) for row in img)
    ch = lambda t, c: struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c))
    open(out + "/sheet.png", "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", cols * 8, rows_n * 8, 8, 6, 0, 0, 0))
                                         + ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b""))
    json.dump({"cells": len(sheet), "backdrop": [cg[0] & 31, (cg[0] >> 5 | cg[1] << 3) & 31, (cg[1] >> 2) & 31],
               "screens": screens}, open(out + "/meta.json", "w"))
    print("sheet cells", len(sheet))
    print("map16 definitions", len(defs), "with conflicts", sorted("%03X" % k for k in seen_conflict))


if __name__ == "__main__":
    main()
