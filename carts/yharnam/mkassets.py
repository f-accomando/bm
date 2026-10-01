#!/usr/bin/env python3
"""Yharnam: draws the whole sprite sheet (sheet.png) in code, reproducibly,
and writes where everything is into main.lua, between the lines
"-- [atlas begin]" and "-- [atlas end]".

    python3 carts/yharnam/mkassets.py        (needs numpy and Pillow; ~2 min)

The sheet (1024 wide, at most 256 colours: packed as SHEET8):
  - the hunter: 8 rows (S SE E NE N NW W SW) x 12 frames (idle 0-3, walk
    0-7) of 56x60, from art/hunter.py;
  - 16x16 tiles from y = 480 on: ground, kerbs, grass edges, decals,
    building fronts and roofs (art/tiles.py, art/buildings.py);
  - props: lamps, braziers, graves, statues... (art/props.py).
"""
import os
import sys
from multiprocessing import Pool

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'art'))

import buildings  # noqa: E402
import hunter  # noqa: E402
import props  # noqa: E402
import sdf  # noqa: E402
import tiles  # noqa: E402

SW = 1024
FW, FH = 56, 60                  # hunter frame
FX0, FY0 = 4, 12                 # where a frame is cut from the 64x72 render
TILE_Y = 480


def hunter_job(a):
    d, kind, k, n = a
    return hunter.frame(kind, k, n, hunter.DIRS[d][1])


def prop_job(name):
    img, anchor = props.sprite(name)
    return name, img, anchor


class Packer:
    """shelves of sprites in a rectangle"""

    def __init__(self, x0, y0, x1, y1):
        self.x0, self.x1, self.y1 = x0, x1, y1
        self.x, self.y, self.row = x0, y0, 0

    def place(self, w, h):
        if self.x + w > self.x1:
            self.x, self.y, self.row = self.x0, self.y + self.row + 1, 0
        if self.y + h > self.y1:
            return None
        p = (self.x, self.y)
        self.x += w + 1
        self.row = max(self.row, h)
        return p


def main():
    with Pool(4) as pool:
        jobs = [(d, 'idle', k, 4) for d in range(8) for k in range(4)]
        jobs += [(d, 'walk', k, 8) for d in range(8) for k in range(8)]
        frames = pool.map(hunter_job, jobs)
        prop_imgs = pool.map(prop_job, list(props.PROPS))

    sheet = np.zeros((1024, SW, 4), np.uint8)
    for i, (d, kind, k, n) in enumerate(jobs):
        col = k if kind == 'idle' else 4 + k
        f = frames[i][FY0:FY0 + FH, FX0:FX0 + FW]
        sheet[d * FH:(d + 1) * FH, col * FW:(col + 1) * FW] = f

    # tiles: a grid of 16x16 from TILE_Y
    tiles_list = []

    def tile(img):
        n = len(tiles_list)
        tiles_list.append(img)
        x, y = (n % (SW // 16)) * 16, TILE_Y + (n // (SW // 16)) * 16
        sheet[y:y + 16, x:x + 16] = img
        return (y // 8) * (SW // 8) + x // 8          # its first map cell

    atlas = []
    ground = {k: [tile(t) for t in v] for k, v in tiles.GROUND.items()}
    curbs = {m: tile(t) for m, t in tiles.CURBS.items()}
    gedges = {m: tile(t) for m, t in tiles.GRASS_EDGES.items()}
    decals = {k: [tile(t) for t in v] for k, v in tiles.DECALS.items()}
    fac = buildings.facade_tiles()
    facade = {}
    for (mat, kind, row), t in fac.items():
        facade.setdefault(mat, {}).setdefault(kind, [None] * buildings.FACADE_ROWS)[row] = tile(t)
    rf = buildings.roof_tiles()
    roof = {}
    for (r, row, side), t in rf.items():
        roof.setdefault(r, {}).setdefault(row, {})[side] = tile(t)
    tiles_end = TILE_Y + ((len(tiles_list) + SW // 16 - 1) // (SW // 16)) * 16

    # props: right of the hunter, then under the tiles; tallest first
    pk = [Packer(12 * FW + 1, 0, SW, TILE_Y - 1), Packer(0, tiles_end + 1, SW, 1024)]
    spr = {}
    for name, img, (ax, ay) in sorted(prop_imgs, key=lambda p: -p[1].shape[0]):
        h, w = img.shape[:2]
        for p in pk:
            at = p.place(w, h)
            if at:
                break
        else:
            raise SystemExit('the sheet is full: ' + name)
        x, y = at
        sheet[y:y + h, x:x + w] = img
        spr[name] = (x, y, w, h, ax, ay)
    used_h = max(tiles_end, max(s[1] + s[3] for s in spr.values()) + 1)
    used_h = (used_h + 7) // 8 * 8
    sheet = sheet[:used_h]

    # the palette, and the colours that make their own light
    opaque = sheet[:, :, 3] > 0
    colours = sorted({tuple(int(v) for v in c) for c in sheet[opaque][:, :3]})
    if len(colours) > 255:
        raise SystemExit('%d colours: more than SHEET8 and fades() take' % len(colours))
    glowing = set()
    for m in sdf.MATS:
        if m.glow:
            glowing.update(m.ramp)
    glowing.update(tuple(c) for c in tiles.RAMPS['lit'])
    glowing = sorted(c for c in glowing if c in set(colours))

    Image.fromarray(sheet, 'RGBA').save(os.path.join(HERE, 'sheet.png'))
    print('sheet.png: %dx%d, %d colours, %d tiles, %d props' % (SW, used_h, len(colours), len(tiles_list), len(spr)))

    def lua_list(v):
        return '{ ' + ', '.join(str(x) for x in v) + ' }'

    def rgb(c):
        return '0x%02X%02X%02X' % c

    out = ['-- [atlas begin] written by mkassets.py: do not edit by hand',
           'local SHEET_W = %d' % SW,
           'local HUNTER = { w = %d, h = %d, ax = %d, ay = %d }' % (FW, FH, hunter.CX - FX0, hunter.CY - FY0),
           'local GROUND = {']
    for k, v in ground.items():
        out.append('  %s = %s,' % (k, lua_list(v)))
    out.append('}')
    out.append('local CURB = %s' % lua_list(curbs[m] for m in range(1, 16)))
    out.append('local GRASS_EDGE = %s' % lua_list(gedges[m] for m in range(1, 16)))
    out.append('local DECAL = {')
    for k, v in decals.items():
        out.append('  %s = %s,' % (k, lua_list(v)))
    out.append('}')
    out.append('local FACADE = {')
    for mat, kinds in facade.items():
        out.append('  %s = {' % mat)
        for kind, cells in kinds.items():
            out.append('    %s = %s,' % (kind, lua_list(cells)))
        out.append('  },')
    out.append('}')
    out.append('local ROOF = {')
    for r, rows in roof.items():
        out.append('  %s = { %s },' % (r, ', '.join('%s = { l = %d, m = %d, r = %d }' % (row, s['l'], s['m'], s['r'])
                                                    for row, s in rows.items())))
    out.append('}')
    out.append('local SPR = {')
    for name in props.PROPS:
        out.append('  %s = %s,' % (name, lua_list(spr[name])))
    out.append('}')
    out.append('local PALETTE = {')
    for i in range(0, len(colours), 8):
        out.append('  ' + ', '.join(rgb(c) for c in colours[i:i + 8]) + ',')
    out.append('}')
    out.append('local GLOWING = { %s }' % ', '.join('[%s] = true' % rgb(c) for c in glowing))
    out.append('-- [atlas end]')

    path = os.path.join(HERE, 'main.lua')
    src = open(path).read() if os.path.exists(path) else '-- [atlas begin]\n-- [atlas end]\n'
    a = src.index('-- [atlas begin]')
    b = src.index('-- [atlas end]') + len('-- [atlas end]')
    open(path, 'w').write(src[:a] + '\n'.join(out) + src[b:])
    print('main.lua: atlas written')


if __name__ == '__main__':
    main()
