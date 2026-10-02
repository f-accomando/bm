#!/usr/bin/env python3
"""Yharnam: draws the whole sprite sheet (sheet.png) in code, reproducibly,
and writes where everything is into main.lua, between the lines
"-- [atlas begin]" and "-- [atlas end]".

    python3 carts/yharnam/mkassets.py        (needs numpy and Pillow)

The hunter's frames take long to render (1600 of them, a few seconds each):
they are kept in build/yharnam-frames/ and only the ones whose pose or
model changed are drawn again.

The sheet (2048 wide, at most 256 colours: packed as SHEET8):
  - 16x16 tiles at the top: ground, kerbs, grass edges, decals, building
    fronts and roofs (art/tiles.py, art/buildings.py), so their map cells
    stay small numbers;
  - props: lamps, braziers, graves, statues... (art/props.py);
  - the hunter: every animation of art/anims.py in the 8 directions (S SE E
    NE N NW W SW), each frame cut to its own box, with the point between
    the feet, the tip of the saw cleaver and the muzzle of the pistol.
"""
import hashlib
import os
import pickle
import sys
from multiprocessing import Pool

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'art'))

import anims  # noqa: E402
import buildings  # noqa: E402
import hunter  # noqa: E402
import props  # noqa: E402
import sdf  # noqa: E402
import tiles  # noqa: E402

SW = 2048
CACHE = os.path.join(HERE, '..', '..', 'build', 'yharnam-frames')


def model_version():
    h = hashlib.sha1()
    for f in ('hunter.py', 'sdf.py'):
        h.update(open(os.path.join(HERE, 'art', f), 'rb').read())
    return h.hexdigest()[:12]


VERSION = model_version()


def frame_job(a):
    p, d = a
    key = hashlib.sha1(repr((VERSION, sorted(p.items()), d)).encode()).hexdigest()
    path = os.path.join(CACHE, key + '.pkl')
    if os.path.exists(path):
        return pickle.load(open(path, 'rb'))
    r = hunter.render_pose(p, hunter.DIRS[d][1])
    pickle.dump(r, open(path, 'wb'))
    return r


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
    os.makedirs(CACHE, exist_ok=True)
    names = list(anims.ANIMS)
    jobs = [(p, d) for n in names for d in range(8) for p in anims.ANIMS[n]['frames']]
    with Pool(4) as pool:
        prop_imgs = pool.map(prop_job, list(props.PROPS))
        frames = pool.map(frame_job, jobs, chunksize=4)
    print('%d frames of the hunter' % len(frames))

    sheet = np.zeros((4096, SW, 4), np.uint8)
    tiles_list = []

    def tile(img):
        n = len(tiles_list)
        tiles_list.append(img)
        x, y = (n % (SW // 16)) * 16, (n // (SW // 16)) * 16
        sheet[y:y + 16, x:x + 16] = img
        return (y // 8) * (SW // 8) + x // 8          # its first map cell

    ground = {k: [tile(t) for t in v] for k, v in tiles.GROUND.items()}
    curbs = {m: tile(t) for m, t in tiles.CURBS.items()}
    gedges = {m: tile(t) for m, t in tiles.GRASS_EDGES.items()}
    decals = {k: [tile(t) for t in v] for k, v in tiles.DECALS.items()}
    facade = {}
    for (mat, kind, row), t in buildings.facade_tiles().items():
        facade.setdefault(mat, {}).setdefault(kind, [None] * buildings.FACADE_ROWS)[row] = tile(t)
    roof = {}
    for (r, row, side), t in buildings.roof_tiles().items():
        roof.setdefault(r, {}).setdefault(row, {})[side] = tile(t)
    tiles_end = ((len(tiles_list) + SW // 16 - 1) // (SW // 16)) * 16

    # props, then the hunter's frames, tallest first on shelves
    pk = Packer(0, tiles_end + 1, SW, 4096)
    spr = {}
    for name, img, (ax, ay) in sorted(prop_imgs, key=lambda p: -p[1].shape[0]):
        h, w = img.shape[:2]
        x, y = pk.place(w, h)
        sheet[y:y + h, x:x + w] = img
        spr[name] = (x, y, w, h, ax, ay)
    pk = Packer(0, pk.y + pk.row + 2, SW, 4096)
    order = sorted(range(len(frames)), key=lambda i: -frames[i][0].shape[0])
    boxes = [None] * len(frames)
    for i in order:
        img = frames[i][0]
        h, w = img.shape[:2]
        at = pk.place(w, h)
        if not at:
            raise SystemExit('the sheet is full')
        x, y = at
        sheet[y:y + h, x:x + w] = img
        boxes[i] = (x, y, w, h)
    used_h = (pk.y + pk.row + 8) // 8 * 8
    sheet = sheet[:used_h]

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
    print('sheet.png: %dx%d, %d colours, %d tiles, %d props, %d frames' % (SW, used_h, len(colours),
                                                                          len(tiles_list), len(spr), len(frames)))

    def lua_list(v):
        return '{ ' + ', '.join(str(x) for x in v) + ' }'

    def rgb(c):
        return '0x%02X%02X%02X' % c

    out = ['-- [atlas begin] written by mkassets.py: do not edit by hand',
           'local SHEET_W = %d' % SW,
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
    # the hunter: per animation its ticks, whether it loops, the saw cleaver
    # out or not, its events (the frame of a blow, a shot...), then per
    # direction the frames { sx, sy, w, h, ax, ay, tip x, tip y, muzzle x, muzzle y }
    out.append('local HUNT = {')
    i = 0
    for n in names:
        a = anims.ANIMS[n]
        ev = ''.join(', %s = %d' % (k, v + 1) for k, v in a['events'].items())
        out.append('  %s = { t = %s, loop = %s, x = %s%s, d = {' % (n, lua_list(a['ticks']), str(a['loop']).lower(),
                                                                    str(a['ext']).lower(), ev))
        for d in range(8):
            fr = []
            for _ in a['frames']:
                img, (ax, ay), tip, mz = frames[i]
                x, y, w, h = boxes[i]
                fr.append(lua_list((x, y, w, h, ax, ay, round(tip[0]), round(tip[1]), round(mz[0]), round(mz[1]))))
                i += 1
            out.append('    { %s },' % ', '.join(fr))
        out.append('  } },')
    out.append('}')
    out.append('local PALETTE = {')
    for k in range(0, len(colours), 8):
        out.append('  ' + ', '.join(rgb(c) for c in colours[k:k + 8]) + ',')
    out.append('}')
    out.append('local GLOWING = { %s }' % ', '.join('[%s] = true' % rgb(c) for c in glowing))
    out.append('-- [atlas end]')

    path = os.path.join(HERE, 'main.lua')
    src = open(path).read()
    a = src.index('-- [atlas begin]')
    b = src.index('-- [atlas end]') + len('-- [atlas end]')
    open(path, 'w').write(src[:a] + '\n'.join(out) + src[b:])
    print('main.lua: atlas written')


if __name__ == '__main__':
    main()
