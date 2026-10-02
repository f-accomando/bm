#!/usr/bin/env python3
"""Yharnam: draws the whole sprite sheet (sheet.png) in code, reproducibly,
and writes where everything is into main.lua, between the lines
"-- [atlas begin]" and "-- [atlas end]".

    python3 carts/yharnam/mkassets.py        (needs numpy and Pillow)

The frames of the hunter and of the creatures take long to render (about
5000 of them, a few seconds each): they are kept in build/yharnam-frames/
and only the ones whose pose or model changed are drawn again.

The sheet (4096 wide, at most 256 colours: packed as SHEET8):
  - 16x16 tiles at the top: ground, kerbs, grass edges, decals, building
    fronts and roofs (art/tiles.py, art/buildings.py), so their map cells
    stay small numbers;
  - props: lamps, braziers, graves, statues... (art/props.py);
  - the hunter: every animation of art/anims.py in the 8 directions (S SE E
    NE N NW W SW), each frame cut to its own box, with the point between
    the feet, the tip of the saw cleaver and the muzzle of the pistol;
  - the creatures of art/foe_*.py in 5 directions (S SE E NE N: the game
    mirrors them for SW W NW), with two points each (what strikes, and a
    flame, a muzzle, the eyes...).
Frames are packed on a skyline, tallest first; identical frames are kept once.
"""
import ast
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
# the creatures, in this order: townsfolk, beasts, hunters, horrors
import foe_villagers  # noqa: E402,F401,I100
import foe_beasts  # noqa: E402,F401
import foe_hunters  # noqa: E402,F401
import foe_horrors  # noqa: E402,F401
import props  # noqa: E402
import rig  # noqa: E402
import sdf  # noqa: E402
import tiles  # noqa: E402

SW, SH = 4096, 4096
CACHE = os.path.join(HERE, '..', '..', 'build', 'yharnam-frames')
# the kind of each creature module, for the game: where it lives, how it fights
KIND = {'foe_villagers': 'town', 'foe_beasts': 'beast', 'foe_hunters': 'hunter', 'foe_horrors': 'horror'}


def version(*files):
    """the frames of these sources: they change when their code changes (not
    its comments or imports), or the renderer (sdf.SDF_VERSION)"""
    h = hashlib.sha1()
    for f in files:
        tree = ast.parse(open(os.path.join(HERE, 'art', f)).read())
        tree.body = [n for n in tree.body if not isinstance(n, (ast.Import, ast.ImportFrom))]
        h.update(ast.dump(tree).encode())
    h.update(b'sdf%d' % sdf.SDF_VERSION)
    return h.hexdigest()[:12]


VERSION = version('hunter.py')
FOE_VERSION = {m: version('rig.py', 'foeparts.py', m + '.py') for m in KIND}


def cached(key, make):
    path = os.path.join(CACHE, key + '.pkl')
    if os.path.exists(path):
        return pickle.load(open(path, 'rb'))
    r = make()
    pickle.dump(r, open(path + '.tmp', 'wb'))
    os.replace(path + '.tmp', path)
    return r


def frame_job(a):
    p, d = a
    key = hashlib.sha1(repr((VERSION, sorted(p.items()), d)).encode()).hexdigest()
    return cached(key, lambda: hunter.render_pose(p, hunter.DIRS[d][1]))


# YH_DRAFT=1: the creatures' frames not rendered yet are small grey blocks,
# so the game can be tried (and tested) while the rest renders
DRAFT = os.environ.get('YH_DRAFT') == '1'


def foe_job(a):
    name, p, d = a
    cr = rig.CREATURES[name]
    key = hashlib.sha1(repr((FOE_VERSION[cr.module], name, sorted(p.items()), d)).encode()).hexdigest()
    if DRAFT and not os.path.exists(os.path.join(CACHE, key + '.pkl')):
        img = np.zeros((24, 10, 4), np.uint8)
        img[:, :] = (96, 72, 80, 255)
        return img, (5, 23), [(4.0, -12.0), (0.0, -20.0)]
    return cached(key, lambda: cr.render(p, d))


def prop_job(name):
    img, anchor = props.sprite(name)
    return name, img, anchor


class Skyline:
    """rectangles packed on a skyline: each goes where its top is lowest"""

    def __init__(self, x0, y0, x1, y1):
        self.x0, self.y1 = x0, y1
        self.h = np.full(x1 - x0, y0, np.int64)

    def place(self, w, h):
        w += 1                                  # a column of air between frames
        if w > len(self.h):
            return None
        tops = np.lib.stride_tricks.sliding_window_view(self.h, w).max(axis=1)
        x = int(np.argmin(tops))
        y = int(tops[x])
        if y + h > self.y1:
            return None
        self.h[x:x + w] = y + h + 1
        return self.x0 + x, y

    def bottom(self):
        return int(self.h.max())


def foe_jobs():
    """every frame of every creature: (name, pose, direction 0..4)"""
    return [(name, p, d) for name, cr in rig.CREATURES.items() for an in cr.anims.values()
            for d in range(5) for p in an['frames']]


def main():
    os.makedirs(CACHE, exist_ok=True)
    names = list(anims.ANIMS)
    jobs = [(p, d) for n in names for d in range(8) for p in anims.ANIMS[n]['frames']]
    fjobs = foe_jobs()
    with Pool(4) as pool:
        prop_imgs = pool.map(prop_job, list(props.PROPS))
        frames = pool.map(frame_job, jobs, chunksize=4)
        print('%d frames of the hunter' % len(frames))
        foes = pool.map(foe_job, fjobs, chunksize=2)
        print('%d frames of %d creatures' % (len(foes), len(rig.CREATURES)))

    sheet = np.zeros((SH, SW, 4), np.uint8)
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

    # props, the hunter's frames and the creatures', tallest first on a
    # skyline; a frame drawn twice the same is kept once
    pk = Skyline(0, tiles_end + 1, SW, SH)
    items = [('prop', name, img) for name, img, _ in prop_imgs]
    items += [('hunt', i, f[0]) for i, f in enumerate(frames)]
    items += [('foe', i, f[0]) for i, f in enumerate(foes)]
    items.sort(key=lambda it: (-it[2].shape[0], -it[2].shape[1]))
    placed, where = {}, {}
    same = 0
    for kind, i, img in items:
        h, w = img.shape[:2]
        key = (w, h, hashlib.sha1(img.tobytes()).digest())
        if key in placed:
            where[kind, i] = placed[key]
            same += 1
            continue
        at = pk.place(w, h)
        if not at:
            raise SystemExit('the sheet is full (%d of %d placed)' % (len(placed), len(items)))
        x, y = at
        sheet[y:y + h, x:x + w] = img
        placed[key] = where[kind, i] = (x, y, w, h)
    spr = {}
    for name, img, (ax, ay) in prop_imgs:
        spr[name] = where['prop', name] + (ax, ay)
    boxes = [where['hunt', i] for i in range(len(frames))]
    fboxes = [where['foe', i] for i in range(len(foes))]
    used_h = (pk.bottom() + 8) // 8 * 8
    sheet = sheet[:used_h]
    print('%d frames the same as another, kept once' % same)
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
    print('sheet.png: %dx%d, %d colours, %d tiles, %d props, %d frames' % (
        SW, used_h, len(colours), len(tiles_list), len(spr), len(frames) + len(foes)))

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
    # the creatures, in order: name, title, kind (town, beast, hunter,
    # horror), boss or not, and per animation its ticks, whether it loops,
    # its events (lists of frames: the blows of a combo...), then for the 5
    # directions drawn (S SE E NE N) the frames { sx, sy, w, h, ax, ay,
    # point 1 x, y, point 2 x, y }
    out.append('local FOES = {')
    i = 0
    for name, cr in rig.CREATURES.items():
        out.append('  { name = "%s", title = "%s", kind = "%s", boss = %s, order = { %s }, a = {' % (
            name, cr.title, KIND[cr.module], str(cr.boss).lower(), ', '.join('"%s"' % a for a in cr.anims)))
        for an, a in cr.anims.items():
            ev = ''.join(', %s = %s' % (k, lua_list(x + 1 for x in (v if isinstance(v, list) else [v])))
                         for k, v in a['events'].items())
            # how far ahead the body ends up (a pounce): the game moves it there
            shift = round(a['frames'][-1].get('ty', 0.0) * cr.scale)
            if abs(shift) >= 2 and not a['loop']:
                ev += ', shift = %d' % shift
            out.append('    %s = { t = %s, loop = %s%s, d = {' % (an, lua_list(a['ticks']), str(a['loop']).lower(), ev))
            for d in range(5):
                fr = []
                for _ in a['frames']:
                    img, (ax, ay), pts = foes[i]
                    x, y, w, h = fboxes[i]
                    (p1x, p1y), (p2x, p2y) = pts
                    fr.append(lua_list((x, y, w, h, ax, ay, round(p1x), round(p1y), round(p2x), round(p2y))))
                    i += 1
                out.append('      { %s },' % ', '.join(fr))
            out.append('    } },')
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
