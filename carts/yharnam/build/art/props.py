"""Gothic props, modelled with sdf.py and seen from the game's camera: gas
lamps, braziers, pyres, graves, statues, coffins, a carriage, a fountain,
dead trees, iron fences, chimneys, a spire...

    PROPS: name -> function returning (Model, bound, canvas (W, H, CX, CY))
    sprite(name) -> RGBA image cropped, (ax, ay) of the base point in it
"""
import math
import numpy as np

from sdf import (D, Model, hash2, material, nrm, pixelize, render, rx, ry, rz, sd_box, sd_boxat, sd_capsule,
                 sd_cone, sd_cyl, sd_ellipsoid, sd_roundcone, sd_sphere, sd_torus_z, crop, tex_bark, tex_blocks,
                 tex_noise, tex_planks)

M = material
IRON = M('iron', [(24, 24, 32), (48, 48, 64), (88, 88, 104), (136, 136, 152)], [0.35, 0.62, 0.88], weight=1.6,
         detail=True)
STONE = M('stone', [(48, 48, 56), (80, 80, 88), (120, 112, 120), (160, 152, 152), (200, 192, 184)],
          [0.28, 0.5, 0.72, 0.9])
GSTONE = M('gothic stone', [(32, 32, 40), (56, 56, 64), (88, 88, 96), (128, 120, 128)], [0.3, 0.55, 0.8])
MARBLE = M('marble', [(88, 88, 96), (136, 136, 144), (184, 184, 184), (224, 224, 216)], [0.32, 0.6, 0.84])
MOSS = M('moss', [(32, 48, 32), (56, 72, 40), (88, 104, 56)], [0.4, 0.75])
WOOD = M('wood', [(40, 24, 16), (72, 48, 32), (104, 72, 40), (144, 104, 64)], [0.35, 0.6, 0.85])
DWOOD = M('dark wood', [(32, 16, 24), (56, 32, 32), (88, 56, 48), (120, 88, 64)], [0.35, 0.6, 0.85])
BRASS = M('brass', [(88, 64, 24), (152, 112, 40), (208, 176, 88)], [0.4, 0.8], weight=2.0, detail=True, metal=True)
GLASS = M('lit glass', [(248, 176, 72), (248, 224, 144), (248, 248, 208)], [0.55, 0.85], weight=2.0, glow=True,
          keep=True, noline=True)
# the hunter's lamp lit: a cold, pale light, as of something not of this world
SPIRIT_GLASS = M('spirit glass', [(88, 136, 176), (152, 208, 232), (224, 248, 248)], [0.5, 0.85], weight=2.0,
                 glow=True, keep=True, noline=True)
SPIRIT = M('spirit flame', [(176, 224, 248), (240, 248, 248)], [0.6], weight=3.0, detail=True, glow=True, keep=True,
           noline=True)
DGLASS = M('dark glass', [(24, 32, 48), (48, 64, 88), (88, 112, 136)], [0.5, 0.85], weight=1.5, keep=True)
COALS = M('coals', [(104, 24, 8), (200, 72, 16), (248, 152, 40)], [0.4, 0.75], weight=1.6, glow=True, keep=True,
          noline=True)
LEAF = M('leaves', [(16, 32, 24), (32, 48, 32), (56, 72, 40), (88, 96, 56)], [0.3, 0.55, 0.8])
BARK = M('bark', [(24, 16, 24), (48, 40, 40), (80, 64, 56), (120, 96, 80)], [0.3, 0.55, 0.8], weight=1.4,
         detail=True)
WATER = M('water', [(16, 40, 64), (32, 72, 96), (64, 112, 136), (136, 176, 192)], [0.3, 0.6, 0.9], keep=True)
CLOTH = M('cloth', [(40, 32, 40), (72, 56, 56), (112, 88, 80)], [0.35, 0.7])
RED = M('red cloth', [(56, 16, 24), (96, 24, 32), (144, 40, 40)], [0.35, 0.7])
BONE = M('bone', [(104, 96, 80), (160, 152, 128), (216, 208, 184)], [0.35, 0.72])
BRICK = M('brick', [(48, 24, 24), (88, 40, 32), (128, 64, 48), (168, 96, 72)], [0.3, 0.55, 0.8])
ROPE = M('rope', [(72, 56, 32), (120, 96, 56)], [0.5], weight=1.5, detail=True)
CANDLE = M('candle', [(160, 144, 112), (224, 208, 168)], [0.5], weight=2.0, detail=True, keep=True, noline=True)
FLAME = M('flame', [(248, 160, 56), (248, 232, 136)], [0.6], weight=3.0, detail=True, glow=True, keep=True,
          noline=True)


def brick_mat(q):
    """mortar lines on a brick surface (by height and position)"""
    z = q[:, 2]
    row = np.floor(z / 2.2)
    x = q[:, 0] + q[:, 1] + (row % 2) * 2.2
    m = np.full(len(q), BRICK)
    m[((z / 2.2) % 1 < 0.2) | ((x / 4.4) % 1 < 0.1)] = STONE
    return m


# ---------------------------------------------------------------- lamps

def gas_lamp(lit=True):
    m = Model()
    a = m.add
    a('root', lambda q: sd_cone(q, 0, 3.0, 3.2, 2.6), IRON, 'post')                 # plinth
    a('root', lambda q: sd_cone(q, 3.0, 9.0, 1.9, 1.2), IRON, 'post')
    a('root', lambda q: sd_torus_z(q, (0, 0, 9.0), 1.25, 0.55), IRON, 'post')
    a('root', lambda q: sd_cyl(q, (0, 0), 0.85, 9, 52), IRON, 'post')
    a('root', lambda q: sd_torus_z(q, (0, 0, 30.0), 1.0, 0.45), IRON, 'post')
    # the ladder bar under the lantern
    a('root', lambda q: sd_capsule(q, (-3.2, 0, 50.5), (3.2, 0, 50.5), 0.45), IRON, 'post')
    a('root', lambda q: sd_cone(q, 51.0, 53.0, 1.4, 2.6), IRON, 'post')
    # lantern: four panes, a roof and a finial
    glass = GLASS if lit else DGLASS
    a('root', lambda q: sd_box(q - np.array([0, 0, 57.0]), (2.9, 2.9, 4.0), 0.3), glass, 'lamp')
    for sx, sy in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
        a('root', (lambda sx, sy: lambda q: sd_capsule(q, (sx * 3.0, sy * 3.0, 53), (sx * 3.6, sy * 3.6, 61.2), 0.4))(
            sx, sy), IRON, 'lamp')
    a('root', lambda q: sd_cone(q, 61.0, 64.5, 4.6, 0.6), IRON, 'lamp')
    a('root', lambda q: sd_sphere(q, (0, 0, 65.0), 0.8), IRON, 'lamp')
    return m, ((0, 0, 33), 36), (40, 80, 20, 74)


def shrine(lit=True):
    """the hunter's lamp: a lantern on a short iron post over a stone step,
    where the hunt rests (a checkpoint); lit, it burns with a cold pale light"""
    m = Model()
    a = m.add
    a('root', lambda q: sd_box(q - np.array([0, 0, 1.2]), (7.0, 7.0, 1.2), 0.3), STONE, 'step')
    a('root', lambda q: sd_box(q - np.array([0, 0, 3.4]), (5.0, 5.0, 1.0), 0.3), STONE, 'step')
    a('root', lambda q: sd_cone(q, 4.0, 7.0, 2.4, 1.6), IRON, 'post')
    a('root', lambda q: sd_cyl(q, (0, 0), 1.0, 7, 22), IRON, 'post')
    a('root', lambda q: sd_torus_z(q, (0, 0, 14.0), 1.2, 0.5), IRON, 'post')
    a('root', lambda q: sd_cone(q, 22.0, 24.0, 1.6, 3.4), IRON, 'post')
    # the lantern: a wide cage of iron round its glass, a hood, a ring
    glass = SPIRIT_GLASS if lit else DGLASS
    a('root', lambda q: sd_box(q - np.array([0, 0, 29.0]), (3.4, 3.4, 4.6), 0.6), glass, 'lamp')
    for k in range(8):
        ang = k * math.pi / 4
        cx, cy = 4.2 * math.cos(ang), 4.2 * math.sin(ang)
        a('root', (lambda cx, cy: lambda q: sd_capsule(q, (cx, cy, 24.2), (cx * 1.05, cy * 1.05, 33.6), 0.35))(cx, cy),
          IRON, 'lamp')
    a('root', lambda q: sd_torus_z(q, (0, 0, 24.4), 4.2, 0.55), IRON, 'lamp')
    a('root', lambda q: sd_cone(q, 33.4, 37.0, 5.2, 1.0), IRON, 'lamp')
    a('root', lambda q: sd_torus_z(q - np.array([0, 0, 0]), (0, 0, 38.4), 1.2, 0.35) +
      0 * q[:, 0], IRON, 'lamp')
    if lit:
        a('root', lambda q: sd_ellipsoid(q - np.array([0, 0, 28.6]), (1.3, 1.3, 2.4)), SPIRIT, 'lamp')
    return m, ((0, 0, 19), 22), (40, 60, 20, 54)


def brazier():
    m = Model()
    a = m.add
    for k in range(3):
        ang = k * 2 * math.pi / 3 + 0.4
        x, y = math.cos(ang), math.sin(ang)
        a('root', (lambda x, y: lambda q: sd_capsule(q, (x * 5.0, y * 5.0, 0.5), (x * 3.0, y * 3.0, 13.0), 0.6))(x, y),
          IRON, 'legs')
    a('root', lambda q: sd_torus_z(q, (0, 0, 7), 3.6, 0.35), IRON, 'legs')

    def bowl(q):
        d = sd_cone(q, 12.0, 17.5, 3.4, 6.2)
        inner = sd_cone(q, 13.0, 18.5, 2.6, 5.4)
        return np.maximum(d, -inner)
    a('root', bowl, IRON, 'bowl')
    a('root', lambda q: sd_torus_z(q, (0, 0, 17.3), 6.0, 0.6), IRON, 'bowl')
    a('root', lambda q: np.maximum(sd_ellipsoid(q - np.array([0, 0, 16.4]), (5.2, 5.2, 2.4)), 0.0 * q[:, 2]),
      COALS, 'coals')
    return m, ((0, 0, 10), 14), (32, 40, 16, 34)


def pyre():
    """a wooden stake with a cross bar, logs piled at its foot"""
    m = Model()
    a = m.add
    a('root', lambda q: sd_cyl(q, (0, 0), 1.3, 0, 44), WOOD, 'stake')
    a('root', lambda q: sd_capsule(q, (-9, 0, 34), (9, 0, 34), 1.0), WOOD, 'stake')
    a('root', lambda q: sd_capsule(q, (-1.4, 1.2, 30), (1.4, 1.2, 40), 0.4), ROPE, 'stake')
    rng = np.random.default_rng(4)
    for k in range(14):
        ang = rng.uniform(0, 2 * math.pi)
        r0 = rng.uniform(2, 9)
        ln = rng.uniform(8, 14)
        z = rng.uniform(1.5, 7 - r0 * 0.4)
        dx, dy = math.cos(ang), math.sin(ang)
        tilt = rng.uniform(-0.3, 0.6)
        p0 = (dx * r0 - dy * ln / 2, dy * r0 + dx * ln / 2, z)
        p1 = (dx * r0 + dy * ln / 2, dy * r0 - dx * ln / 2, z + tilt * 4)
        a('root', (lambda p0, p1, r: lambda q: sd_capsule(q, p0, p1, r))(p0, p1, rng.uniform(1.1, 1.6)), BARK, 'logs')
    a('root', lambda q: sd_ellipsoid(q - np.array([0, 0, 1.0]), (9.0, 7.0, 2.0)), COALS, 'coals')
    return m, ((0, 0, 20), 26), (56, 64, 28, 54)


def candles(n=3, seed=1):
    m = Model()
    a = m.add
    rng = np.random.default_rng(seed)
    for k in range(n):
        x, y = rng.uniform(-3.5, 3.5), rng.uniform(-2.5, 2.5)
        h = rng.uniform(3, 7)
        a('root', (lambda x, y, h: lambda q: sd_cyl(q, (x, y), 0.7, 0, h))(x, y, h), CANDLE, 'c%d' % k)
        a('root', (lambda x, y, h: lambda q: sd_ellipsoid(q - np.array([x, y, h + 1.2]), (0.5, 0.5, 1.1)))(x, y, h),
          FLAME, 'c%d' % k)
    return m, ((0, 0, 4), 8), (20, 20, 10, 15)


# ---------------------------------------------------------------- graves

def headstone(kind, seed=0):
    m = Model()
    a = m.add
    mt = GSTONE if seed % 2 else STONE
    if kind == 'round':
        def f(q):
            body = sd_box(q - np.array([0, 0, 6.0]), (4.2, 1.2, 6.0), 0.4)
            top = sd_cyl(np.stack([q[:, 0], q[:, 2] - 12.0, q[:, 1]], 1), (0, 0), 4.2, -1.2, 1.2)
            return np.minimum(np.maximum(body, q[:, 2] - 12.0), top)
        a('root', f, mt, 'stone')
    elif kind == 'cross':
        a('root', lambda q: sd_box(q - np.array([0, 0, 9.0]), (1.3, 1.1, 9.0), 0.3), mt, 'stone')
        a('root', lambda q: sd_box(q - np.array([0, 0, 13.0]), (5.0, 1.1, 1.3), 0.3), mt, 'stone')
        a('root', lambda q: sd_box(q - np.array([0, 0, 1.0]), (3.4, 2.4, 1.0), 0.3), mt, 'stone')
    elif kind == 'celtic':
        a('root', lambda q: sd_box(q - np.array([0, 0, 9.0]), (1.5, 1.2, 9.5), 0.3), mt, 'stone')
        a('root', lambda q: sd_box(q - np.array([0, 0, 13.5]), (5.4, 1.2, 1.5), 0.3), mt, 'stone')
        a('root', lambda q: np.maximum(sd_torus_z(np.stack([q[:, 0], q[:, 2] - 13.5, q[:, 1]], 1), (0, 0, 0),
                                                   3.4, 0.9), 0 * q[:, 0]), mt, 'stone')
        a('root', lambda q: sd_box(q - np.array([0, 0, 1.2]), (3.6, 2.6, 1.2), 0.3), mt, 'stone')
    elif kind == 'obelisk':
        a('root', lambda q: sd_box(q - np.array([0, 0, 2.0]), (4.6, 4.6, 2.0), 0.3), mt, 'stone')
        a('root', lambda q: sd_cone(np.stack([q[:, 0] * 1.0, q[:, 1] * 1.0, q[:, 2]], 1), 4.0, 26.0, 3.2, 1.8),
          mt, 'stone')
        a('root', lambda q: sd_cone(q, 26.0, 30.0, 1.9, 0.1), mt, 'stone')
    elif kind == 'broken':
        def f(q):
            body = sd_box(q - np.array([0, 0, 4.5]), (4.0, 1.2, 4.5), 0.4)
            cut = q[:, 2] - 8.5 + 0.5 * q[:, 0] + 1.2 * np.sin(q[:, 0] * 2.1)
            return np.maximum(body, cut)
        a('root', f, mt, 'stone')
        a('root', lambda q: sd_box(q - np.array([3.5, -3.0, 0.8]), (2.0, 1.4, 0.8), 0.3), mt, 'rubble')
    elif kind == 'slab':
        a('root', lambda q: sd_box(q - np.array([0, 0, 1.6]), (5.0, 10.0, 1.6), 0.5), mt, 'stone')
        a('root', lambda q: sd_box(q - np.array([0, 2.0, 3.4]), (1.0, 5.0, 0.4), 0.2), mt, 'stone')
        a('root', lambda q: sd_box(q - np.array([0, 4.0, 3.4]), (3.4, 0.9, 0.4), 0.2), mt, 'stone')
    # moss at the foot
    if kind != 'obelisk':
        a('root', lambda q: sd_ellipsoid(q - np.array([1.5, 1.2, 0.0]), (2.6, 1.6, 1.0)), MOSS, 'moss')
    return m, ((0, 0, 10), 16), (32, 48, 16, 40)


def angel():
    """a weeping angel on a pedestal"""
    m = Model()
    a = m.add
    a('root', lambda q: sd_box(q - np.array([0, 0, 5.0]), (6.0, 6.0, 5.0), 0.4), GSTONE, 'pedestal', tex=tex_blocks(2.5, 4.0))
    a('root', lambda q: sd_box(q - np.array([0, 0, 10.6]), (6.8, 6.8, 0.8), 0.3), GSTONE, 'pedestal')
    a('root', lambda q: sd_cone(q, 11.0, 30.0, 5.0, 2.6), MARBLE, 'robe')
    a('root', lambda q: sd_ellipsoid(q - np.array([0, 0.2, 30.0]), (3.6, 2.6, 3.2)), MARBLE, 'robe')
    # head bowed, hands to the face
    a('root', lambda q: sd_sphere(q, (0, 1.6, 34.2), 2.2), MARBLE, 'head')
    a('root', lambda q: sd_capsule(q, (-2.8, 0.8, 30.5), (-0.6, 3.2, 33.5), 0.9), MARBLE, 'arms')
    a('root', lambda q: sd_capsule(q, (2.8, 0.8, 30.5), (0.6, 3.2, 33.5), 0.9), MARBLE, 'arms')

    def wing(sg):
        def f(q):
            p = q - np.array([sg * 3.6, -2.6, 28.0])
            p = p @ ry(sg * D(-25)).T
            d = sd_ellipsoid(p, (3.0, 1.0, 9.5))
            return np.maximum(d, -(p[:, 2]) - 6.0)
        return f
    a('root', wing(1), MARBLE, 'wingr')
    a('root', wing(-1), MARBLE, 'wingl')
    return m, ((0, 0, 20), 24), (40, 64, 20, 58)


def mausoleum():
    m = Model()
    a = m.add
    a('root', lambda q: sd_box(q - np.array([0, 0, 1.0]), (15.0, 13.0, 1.0), 0.3), GSTONE, 'base')
    a('root', lambda q: sd_box(q - np.array([0, 0, 12.0]), (12.0, 10.0, 10.0), 0.2), STONE, 'walls', tex=tex_blocks(2.4, 4.6))
    # the pediment: a triangular prism along y
    def roof(q):
        x, y, z = q[:, 0], q[:, 1], q[:, 2] - 22.0
        d = np.maximum(np.abs(y) - 11.5, np.maximum(-z, z + np.abs(x) * 0.62 - 8.5))
        return d
    a('root', roof, GSTONE, 'roof')
    a('root', lambda q: sd_box(q - np.array([0, 10.2, 8.0]), (4.0, 0.5, 7.0), 0.2), IRON, 'door')
    a('root', lambda q: sd_box(q - np.array([0, 10.6, 8.0]), (0.3, 0.4, 7.0)), DGLASS, 'door')
    for sx in (-1, 1):
        a('root', (lambda sx: lambda q: sd_cyl(q, (sx * 8.5, 11.0), 1.4, 2.0, 21.5))(sx), MARBLE, 'col%d' % sx)
    a('root', lambda q: sd_box(q - np.array([0, 10.5, 25.0]), (1.0, 0.6, 2.8)), MARBLE, 'cross')
    a('root', lambda q: sd_box(q - np.array([0, 10.5, 25.8]), (2.2, 0.6, 0.8)), MARBLE, 'cross')
    return m, ((0, 0, 14), 26), (64, 72, 32, 60)


# ---------------------------------------------------------------- street furniture

def coffin(upright=False, seed=0):
    m = Model()
    a = m.add

    def shape(q):
        # a hexagonal coffin, wider at the shoulders, lying along y
        y = q[:, 1]
        w = np.where(y > 5.0, 4.6 - (y - 5.0) * 0.30, 3.2 + (y + 11.0) * 0.088)
        d = np.maximum(np.abs(q[:, 0]) - w, np.abs(y + 1.5) - 10.0)
        return np.maximum(d, np.abs(q[:, 2] - 2.6) - 2.6)
    if upright:
        def f(q):
            p = np.stack([q[:, 0], q[:, 2] - 11.6, -q[:, 1] + 2.6], 1)
            return shape(p)
        a('root', f, DWOOD, 'box')
        a('root', lambda q: sd_box(q - np.array([0, 2.9, 14.0]), (3.6, 0.4, 0.6)), IRON, 'band')
        a('root', lambda q: sd_box(q - np.array([0, 2.9, 6.0]), (3.4, 0.4, 0.6)), IRON, 'band')
        return m, ((0, 0, 12), 16), (32, 48, 16, 40)
    # lying across the view (along x), so its long shape shows
    sw = lambda q: np.stack([q[:, 1], q[:, 0], q[:, 2]], 1)
    a('root', lambda q: shape(sw(q)), DWOOD, 'box', tex=tex_planks(1, 2.2))
    a('root', lambda q: sd_box(sw(q) - np.array([0, 0, 5.4]), (4.0, 0.6, 0.35)), IRON, 'band')
    a('root', lambda q: sd_box(sw(q) - np.array([0, -6, 5.4]), (3.6, 0.6, 0.35)), IRON, 'band')
    a('root', lambda q: sd_box(sw(q) - np.array([0, 3.0, 5.5]), (0.5, 2.6, 0.3)), BRASS, 'cross')
    a('root', lambda q: sd_box(sw(q) - np.array([0, 3.8, 5.5]), (1.6, 0.5, 0.3)), BRASS, 'cross')
    return m, ((0, 0, 3), 14), (40, 40, 20, 26)


def carriage():
    m = Model()
    a = m.add
    # body: a box on springs, with a curved roof, along x
    a('root', lambda q: sd_box(q - np.array([0, 0, 16.0]), (13.0, 7.0, 8.0), 1.0), DWOOD, 'body')
    a('root', lambda q: sd_box(q - np.array([0, 0, 24.6]), (13.6, 7.6, 1.0), 0.6), IRON, 'roof')
    a('root', lambda q: sd_box(q - np.array([0, -7.2, 17.0]), (3.0, 0.5, 4.0), 0.4), DGLASS, 'window')
    a('root', lambda q: sd_box(q - np.array([-7.5, -7.2, 17.0]), (2.6, 0.5, 4.0), 0.4), DGLASS, 'window')
    a('root', lambda q: sd_box(q - np.array([7.5, -7.2, 17.0]), (2.6, 0.5, 4.0), 0.4), DGLASS, 'window')
    a('root', lambda q: sd_box(q - np.array([0, -7.1, 10.0]), (13.2, 0.4, 0.6)), BRASS, 'trim')
    # the driver's box at the front (east) and the shafts
    a('root', lambda q: sd_box(q - np.array([16.5, 0, 14.0]), (3.6, 6.0, 1.2), 0.4), DWOOD, 'seat')
    for sy in (-1, 1):
        a('root', (lambda sy: lambda q: sd_capsule(q, (14, sy * 5.0, 9.0), (30, sy * 5.0, 7.0), 0.6))(sy), WOOD,
          'shaft')
    # wheels: tori with spokes
    for wx, wr in ((-9.0, 7.0), (9.5, 5.6)):
        for sy in (-1, 1):
            c = (wx, sy * 8.2, wr)

            def wheel(q, c=c, wr=wr):
                p = q - np.array(c)
                p = np.stack([p[:, 0], p[:, 2], p[:, 1]], 1)
                ring = np.sqrt((np.sqrt(p[:, 0] ** 2 + p[:, 1] ** 2) - wr) ** 2 + p[:, 2] ** 2) - 0.7
                ang = np.arctan2(p[:, 1], p[:, 0])
                spoke = (np.abs(((ang * 8 / (2 * np.pi)) % 1) - 0.5) * 2 - 0.85) * 2.5
                rr = np.sqrt(p[:, 0] ** 2 + p[:, 1] ** 2)
                spk = np.maximum.reduce([np.maximum(spoke * rr * 0.3, -spoke * 0) - 0.35, rr - wr, np.abs(p[:, 2]) - 0.35])
                hub = np.sqrt(p[:, 0] ** 2 + p[:, 1] ** 2 + p[:, 2] ** 2) - 1.2
                return np.minimum(np.minimum(ring, spk), hub)
            a('root', wheel, IRON, 'wheel%d%d' % (wx > 0, sy))
    return m, ((4, 0, 14), 30), (80, 72, 38, 58)


def barrel():
    m = Model()
    a = m.add

    def f(q):
        r = 4.2 + 0.7 * np.cos((q[:, 2] - 5.5) / 5.5 * 1.3)
        dx = np.sqrt(q[:, 0] ** 2 + q[:, 1] ** 2) - r
        return np.maximum(dx * 0.9, np.abs(q[:, 2] - 5.5) - 5.5)
    a('root', f, WOOD, 'b')
    for z in (2.0, 9.0):
        a('root', (lambda z: lambda q: sd_torus_z(q, (0, 0, z), 4.5 + 0.4, 0.45))(z), IRON, 'b')
    return m, ((0, 0, 6), 9), (24, 32, 12, 26)


def crate(stack=1):
    m = Model()
    a = m.add
    for k in range(stack):
        z = 4.5 + k * 9.0
        ox = (k % 2) * 1.5
        a('root', (lambda z, ox: lambda q: sd_box(q - np.array([ox, 0, z]), (4.5, 4.5, 4.5), 0.3))(z, ox), WOOD, 'c%d' % k)
        a('root', (lambda z, ox: lambda q: np.maximum(sd_box(q - np.array([ox, 0, z]), (4.7, 4.7, 4.7), 0.3),
                                                      np.abs(np.abs(q[:, 0] - ox) + np.abs(q[:, 2] - z) - 0) - 0.0 +
                                                      np.minimum(np.abs(q[:, 0] - ox - (q[:, 2] - z)),
                                                                 np.abs(q[:, 0] - ox + (q[:, 2] - z))) * 2 - 2.0))(z, ox),
          DWOOD, 'c%d' % k)
    return m, ((0, 0, 4.5 * stack), 6 + 5 * stack), (24, 16 + 16 * stack, 12, 8 + 16 * stack)


def fountain():
    m = Model()
    a = m.add

    def basin(q):
        outer = sd_cyl(q, (0, 0), 16.0, 0, 5.0)
        inner = sd_cyl(q, (0, 0), 14.0, 1.5, 6.0)
        return np.maximum(outer, -inner)
    a('root', basin, STONE, 'basin', tex=tex_blocks(2.5, 4.4, seed=5))
    a('root', lambda q: sd_cyl(q, (0, 0), 14.0, 0, 3.6), WATER, 'water')
    a('root', lambda q: sd_cone(q, 3.0, 14.0, 3.0, 1.8), STONE, 'column')
    a('root', lambda q: np.maximum(sd_cone(q, 13.0, 16.0, 2.0, 6.0), -sd_cone(q, 14.5, 17.0, 1.0, 5.2)), STONE,
      'bowl')
    a('root', lambda q: sd_cyl(q, (0, 0), 5.0, 14.0, 15.0), WATER, 'bowlwater')
    a('root', lambda q: sd_cone(q, 15.0, 22.0, 1.4, 0.8), STONE, 'column2')
    a('root', lambda q: sd_sphere(q, (0, 0, 23.0), 1.6), MARBLE, 'top')
    return m, ((0, 0, 8), 22), (72, 64, 36, 44)


def well():
    m = Model()
    a = m.add
    a('root', lambda q: np.maximum(sd_cyl(q, (0, 0), 7.0, 0, 7.0), -sd_cyl(q, (0, 0), 5.4, 1.0, 8.0)), STONE,
      'ring')
    a('root', lambda q: sd_cyl(q, (0, 0), 5.4, 0, 4.0), DGLASS, 'hole')
    for sx in (-1, 1):
        a('root', (lambda sx: lambda q: sd_box(q - np.array([sx * 6.2, 0, 13.0]), (0.8, 0.8, 7.0)))(sx), WOOD,
          'post%d' % sx)

    def roof(q):
        x, z = q[:, 0], q[:, 2] - 20.0
        return np.maximum(np.abs(q[:, 1]) - 5.5, np.maximum(-z - 0.4, z + np.abs(x) * 0.7 - 4.4)) - 0.2
    a('root', roof, DWOOD, 'roof')
    a('root', lambda q: sd_capsule(q, (-6, 0, 15), (6, 0, 15), 0.6), WOOD, 'axle')
    a('root', lambda q: sd_capsule(q, (0, 0, 15), (0, 0, 8), 0.25), ROPE, 'rope')
    return m, ((0, 0, 12), 16), (40, 48, 20, 38)


def bench():
    m = Model()
    a = m.add
    a('root', lambda q: sd_box(q - np.array([0, 0, 5.6]), (9.0, 2.6, 0.5), 0.2), WOOD, 'seat')
    a('root', lambda q: sd_box(q - np.array([0, -2.6, 9.0]), (9.0, 0.4, 2.0), 0.2), WOOD, 'back')
    for sx in (-1, 1):
        a('root', (lambda sx: lambda q: sd_box(q - np.array([sx * 7.6, 0, 3.0]), (0.6, 2.4, 3.0), 0.2))(sx), IRON,
          'leg%d' % sx)
        a('root', (lambda sx: lambda q: sd_box(q - np.array([sx * 7.6, -2.6, 8.0]), (0.6, 0.4, 3.4), 0.2))(sx),
          IRON, 'leg%d' % sx)
    return m, ((0, 0, 5), 11), (32, 32, 16, 24)


def chimney(pots=2):
    m = Model()
    a = m.add
    a('root', lambda q: sd_box(q - np.array([0, 0, 8.0]), (4.0, 3.0, 8.0), 0.2), BRICK, 'stack', brick_mat)
    a('root', lambda q: sd_box(q - np.array([0, 0, 16.4]), (4.6, 3.6, 0.6), 0.2), STONE, 'stack')
    for k in range(pots):
        x = (k - (pots - 1) / 2) * 3.6
        a('root', (lambda x: lambda q: np.maximum(sd_cyl(q, (x, 0), 1.3, 16.5, 20.0),
                                                   -sd_cyl(q, (x, 0), 0.7, 18.0, 21.0)))(x), BRICK, 'pot%d' % k)
    return m, ((0, 0, 10), 13), (24, 40, 12, 34)


def spire():
    """the top of a bell tower: a stone lantern with lancet openings, a slate
    spire and an iron cross"""
    m = Model()
    a = m.add

    def tower(q):
        body = sd_box(q - np.array([0, 0, 14.0]), (10.0, 10.0, 14.0), 0.3)
        # lancet openings on the four sides
        x, y, z = q[:, 0], q[:, 1], q[:, 2] - 16.0
        arch = np.maximum(np.abs(np.maximum(np.abs(x), np.abs(y)) * 0) , 0)
        op = np.maximum(np.minimum(np.abs(x), np.abs(y)) - 2.6, np.abs(z) - 6.5)
        op = np.maximum(op, -(np.maximum(np.abs(x), np.abs(y)) - 8.5))
        return np.maximum(body, -op)
    a('root', tower, GSTONE, 'tower', tex=tex_blocks(2.6, 5.0, seed=3))
    a('root', lambda q: sd_box(q - np.array([0, 0, 16.0]), (8.4, 8.4, 6.6)), DGLASS, 'inside')
    for sx, sy in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
        a('root', (lambda sx, sy: lambda q: sd_cone(q - np.array([sx * 9.5, sy * 9.5, 0]), 28.0, 36.0, 1.6, 0.2))(
            sx, sy), GSTONE, 'pin')
    a('root', lambda q: sd_box(q - np.array([0, 0, 28.6]), (10.6, 10.6, 0.8)), GSTONE, 'cornice')

    def roof(q):
        x, y, z = q[:, 0], q[:, 1], q[:, 2]
        h = 29.0
        top = 66.0
        r = (top - z) / (top - h) * 9.0
        d = np.maximum(np.abs(x) + np.abs(y) * 0 - r, np.abs(y) - r)
        d = np.maximum(d * 0.9, h - z)
        return np.maximum(d, z - top)
    a('root', roof, IRON, 'roof')
    a('root', lambda q: sd_box(q - np.array([0, 0, 70.0]), (0.6, 0.6, 4.5)), IRON, 'cross')
    a('root', lambda q: sd_box(q - np.array([0, 0, 71.5]), (2.6, 0.6, 0.6)), IRON, 'cross')
    return m, ((0, 0, 37), 40), (64, 112, 32, 100)


def dead_tree(seed=0, scale=1.0):
    """a bare, twisted tree: recursive branches of capsules"""
    m = Model()
    rng = np.random.default_rng(seed)
    segs = []

    def grow(p, d, ln, r, depth):
        q = p + d * ln
        segs.append((p.copy(), q.copy(), r, r * 0.68))
        if depth == 0:
            return
        n = 2 if depth > 1 else rng.integers(1, 3)
        for k in range(int(n)):
            ang = rng.uniform(0.35, 0.8) * (1 if k == 0 else -1) + rng.normal(0, 0.15)
            axis = nrm(np.cross(d, [rng.normal(), rng.normal(), 0.2]))
            c, s = math.cos(ang), math.sin(ang)
            nd = d * c + np.cross(axis, d) * s + axis * (axis @ d) * (1 - c)
            nd = nrm(nd + np.array([0, 0, 0.15]))
            grow(q, nd, ln * rng.uniform(0.62, 0.78), r * 0.68, depth - 1)

    trunk = np.array([0, 0, 0.0])
    d0 = nrm([rng.normal(0, 0.15), rng.normal(0, 0.15), 1])
    grow(trunk, d0, 18 * scale, 2.8 * scale, 4)
    # roots
    for k in range(4):
        ang = k * math.pi / 2 + rng.uniform(-0.4, 0.4)
        segs.append((np.array([0, 0, 2.0]), np.array([math.cos(ang) * 5, math.sin(ang) * 5, 0.0]), 1.6, 0.6))
    for (p, q, r1, r2) in segs:
        m.add('root', (lambda p, q, r1, r2: lambda x: sd_roundcone(x, p, q, max(r1, 0.45), max(r2, 0.4)))(p, q, r1, r2),
              BARK, 'tree')
    zs = [s[1][2] for s in segs]
    top = max(zs)
    return m, ((0, 0, top / 2), top / 2 + 16), (96, int(top * 0.85 + 40), 48, int(top * 0.85 + 32))


def bush(seed=0):
    m = Model()
    rng = np.random.default_rng(seed)
    for k in range(7):
        c = (rng.uniform(-5, 5), rng.uniform(-3, 3), rng.uniform(3, 7))
        r = rng.uniform(3.2, 5.0)

        def f(q, c=c, r=r):
            p = q - np.array(c)
            n = 0.6 * np.sin(p[:, 0] * 2.1 + p[:, 2] * 1.7) * np.sin(p[:, 1] * 2.3 + p[:, 0] * 1.3)
            return (np.sqrt((p ** 2).sum(1)) - r + n) * 0.8
        m.add('root', f, LEAF, 'bush')
    return m, ((0, 0, 5), 13), (40, 36, 20, 28)


def fence(axis='x', gate=False):
    """a 16 px length of spiked iron railing"""
    m = Model()
    a = m.add
    L = 8.0                                     # half length (16 world units)
    rot = (lambda q: q) if axis == 'x' else (lambda q: np.stack([q[:, 1], q[:, 0], q[:, 2]], 1))
    for k in range(4):
        x = -L + 2.0 + k * 4.0
        a('root', (lambda x: lambda q: sd_cyl(rot(q), (x, 0), 0.5, 0, 19.0))(x), IRON, 'bar%d' % k)
        a('root', (lambda x: lambda q: sd_cone(rot(q) - np.array([x, 0, 0]), 19.0, 22.0, 0.9, 0.05))(x), IRON,
          'bar%d' % k)
    for z in (3.0, 16.0):
        a('root', (lambda z: lambda q: sd_box(rot(q) - np.array([0, 0, z]), (L, 0.4, 0.5)))(z), IRON, 'rail')
    a('root', lambda q: sd_box(rot(q) - np.array([0, 0, 0.6]), (L, 1.4, 0.6)), STONE, 'kerb')
    return m, ((0, 0, 11), 14), (32, 40, 16, 32)


def fence_post():
    m = Model()
    a = m.add
    a('root', lambda q: sd_box(q - np.array([0, 0, 11.0]), (2.4, 2.4, 11.0), 0.3), GSTONE, 'post')
    a('root', lambda q: sd_box(q - np.array([0, 0, 22.6]), (3.0, 3.0, 0.8), 0.3), GSTONE, 'post')
    a('root', lambda q: sd_sphere(q, (0, 0, 25.4), 2.2), GSTONE, 'post')
    return m, ((0, 0, 13), 16), (24, 44, 12, 38)


def bollard():
    m = Model()
    m.add('root', lambda q: sd_cone(q, 0, 8.0, 2.4, 1.8), IRON, 'b')
    m.add('root', lambda q: sd_sphere(q, (0, 0, 8.4), 1.9), IRON, 'b')
    return m, ((0, 0, 5), 7), (16, 20, 8, 15)


def cage():
    """a hanging-cage style iron cage on the ground"""
    m = Model()
    a = m.add
    for k in range(10):
        ang = k * 2 * math.pi / 10
        x, y = math.cos(ang) * 6, math.sin(ang) * 6
        a('root', (lambda x, y: lambda q: sd_capsule(q, (x, y, 0.5), (x * 0.9, y * 0.9, 16), 0.45))(x, y), IRON, 'bars')
    a('root', lambda q: sd_torus_z(q, (0, 0, 0.6), 6.0, 0.7), IRON, 'bars')
    a('root', lambda q: np.maximum(sd_sphere(q, (0, 0, 15.0), 5.8), -sd_sphere(q, (0, 0, 15.0), 5.0)) +
      0 * q[:, 0], IRON, 'top')
    a('root', lambda q: sd_torus_z(q, (0, 0, 21.0), 1.4, 0.4), IRON, 'top')
    return m, ((0, 0, 11), 14), (32, 44, 16, 36)


# ---------------------------------------------------------------- broken
# what is left of the wooden things the hunter (or a creature) smashes:
# staves, planks and hoops lying round where they stood, the same woods

def _plank(c, size, yaw=0.0, tilt=0.0, roll=0.0):
    """a box lying at c, turned yaw about the vertical, tilted (pitch) and rolled"""
    R = rz(yaw) @ ry(tilt) @ rx(roll)
    c = np.asarray(c, float)
    return lambda q: sd_box((q - c) @ R, size, 0.15)


def _scatter(a, n, rad, size, mat, seed, z=0.35, group='plank', spread=0.9):
    """n planks thrown round the middle, from rad[0] to rad[1] out"""
    rng = np.random.default_rng(seed)
    for k in range(n):
        an = k * 2 * math.pi / n + rng.uniform(-0.5, 0.5)
        d = rng.uniform(*rad)
        yaw = an + math.pi / 2 + rng.uniform(-spread, spread)
        tilt = rng.uniform(-0.25, 0.25)
        a('root', _plank((math.cos(an) * d, math.sin(an) * d * 0.8, z + abs(tilt) * size[0] * 0.5), size, yaw, tilt),
          mat, '%s%d' % (group, k), tex=tex_planks(0, 1.6, seed=k))


def barrel_broken():
    m = Model()
    a = m.add
    # the bottom of the barrel, open, and its staves all round
    a('root', lambda q: np.maximum(sd_cyl(q, (0, 0), 4.5, 0, 2.6), -sd_cyl(q, (0, 0), 3.6, 0.9, 4.0)), WOOD, 'stump')
    _scatter(a, 7, (5.0, 8.0), (3.0, 0.75, 0.3), WOOD, 3)
    # one hoop flat on the street, one leaning on the stump
    a('root', lambda q: sd_torus_z(q, (5.2, -3.0, 0.45), 4.6, 0.42), IRON, 'hoop')
    lean = rx(D(64))
    a('root', lambda q: sd_torus_z((q - np.array([-2.6, 2.0, 3.6])) @ lean, (0, 0, 0), 4.4, 0.42), IRON, 'hoop2')
    return m, ((0, 0, 2), 13), (40, 32, 20, 22)


def crate_broken(stack=1, seed=5):
    m = Model()
    a = m.add
    # the floor of the crate, a low open tray, and its sides fallen out
    a('root', lambda q: np.maximum(sd_box(q - np.array([0, 0, 0.9]), (4.5, 4.5, 0.9), 0.2),
                                   -sd_box(q - np.array([0, 0, 1.6]), (3.7, 3.7, 1.2))), WOOD, 'tray')
    rng = np.random.default_rng(seed)
    for k in range(2 + stack):
        an = k * 2 * math.pi / (2 + stack) + rng.uniform(-0.4, 0.4)
        d = 6.2 + rng.uniform(0, 1.5)
        a('root', _plank((math.cos(an) * d, math.sin(an) * d * 0.8, 0.9), (4.4, 4.4, 0.4), an, rng.uniform(-0.3, 0.3),
                         rng.uniform(-0.3, 0.3)), WOOD, 'side%d' % k, tex=tex_planks(0, 2.2, seed=k))
    _scatter(a, 4 + 3 * stack, (3.0, 7.5 + stack), (3.6, 0.6, 0.3), DWOOD, seed + 11, group='brace')
    return m, ((0, 0, 1.5), 12 + 2 * stack), (40, 32, 20, 22)


def bench_broken():
    m = Model()
    a = m.add
    # the seat snapped in two, one half on the street, one against a leg
    a('root', _plank((-4.6, 0.6, 2.8), (4.4, 2.6, 0.5), 0.15, 0.55), WOOD, 'seat0', tex=tex_planks(0, 2.2))
    a('root', _plank((4.4, -0.4, 0.55), (4.2, 2.6, 0.5), -0.25, 0.0), WOOD, 'seat1', tex=tex_planks(0, 2.2, seed=1))
    # the back fallen behind
    a('root', _plank((0.5, -4.6, 0.45), (8.6, 1.9, 0.4), 0.12, 0.0, 1.5), WOOD, 'back', tex=tex_planks(0, 2.2, seed=2))
    # one iron end still standing, the other over on its side
    a('root', lambda q: sd_box(q - np.array([-7.6, 0, 3.0]), (0.6, 2.4, 3.0), 0.2), IRON, 'leg0')
    a('root', lambda q: sd_box(q - np.array([-7.6, -2.6, 8.0]), (0.6, 0.4, 3.4), 0.2), IRON, 'leg0')
    for dy in (-2.0, 2.0):                      # the fallen end: its two bars and the foot
        a('root', _plank((9.6 - dy * 0.3, 0.8 + dy, 0.5), (3.0, 0.4, 0.45), 0.3), IRON, 'leg1')
    a('root', _plank((12.4, 0.2, 0.5), (0.4, 2.4, 0.45), 0.3), IRON, 'leg1')
    _scatter(a, 3, (3.0, 7.0), (1.6, 0.5, 0.25), WOOD, 9, group='chip')
    return m, ((0, 0, 3), 13), (40, 32, 20, 22)


def coffin_broken(seed=0):
    m = Model()
    a = m.add

    def shape(q):
        y = q[:, 1]
        w = np.where(y > 5.0, 4.6 - (y - 5.0) * 0.30, 3.2 + (y + 11.0) * 0.088)
        return np.maximum(np.abs(q[:, 0]) - w, np.abs(y + 1.5) - 10.0)
    sw = lambda q: np.stack([q[:, 1], q[:, 0], q[:, 2]], 1)
    # the open box, low, along x; what was inside it; the lid in pieces
    a('root', lambda q: np.maximum(np.maximum(shape(sw(q)), np.abs(q[:, 2] - 1.3) - 1.3),
                                   -np.maximum(shape(sw(q)) + 0.7, 0.8 - q[:, 2])), DWOOD, 'box',
      tex=tex_planks(1, 2.2))
    a('root', lambda q: sd_sphere(q, (5.6, 0.2, 1.6), 1.5), BONE, 'skull')
    a('root', lambda q: sd_capsule(q, (2.8, -0.8, 1.0), (-4.0, 0.6, 1.0), 0.45), BONE, 'bones')
    a('root', lambda q: sd_capsule(q, (1.0, 1.4, 1.1), (-6.5, 2.0, 0.9), 0.4), BONE, 'bones')
    rng = np.random.default_rng(seed + 4)
    for k in range(3):
        y = -9.0 + k * 6.5 + rng.uniform(-1, 1)
        x = rng.choice([-1, 1]) * rng.uniform(5.5, 7.5)
        a('root', _plank((y * 0.9, x, 0.4), (3.4, 4.0, 0.35), rng.uniform(-0.5, 0.5), rng.uniform(-0.2, 0.2)),
          DWOOD, 'lid%d' % k, tex=tex_planks(1, 2.2, seed=k))
    a('root', lambda q: sd_box(q - np.array([3.0, -6.6, 0.35]), (2.6, 0.5, 0.3)), BRASS, 'cross')
    a('root', lambda q: sd_box(q - np.array([3.8, -6.6, 0.35]), (0.5, 1.6, 0.3)), BRASS, 'cross')
    return m, ((0, 0, 1.5), 16), (48, 40, 24, 26)


BROKEN = {'barrel': 'barrel_broken', 'crate': 'crate_broken', 'crates': 'crates_broken', 'bench': 'bench_broken',
          'coffin': 'coffin_broken', 'coffin_up': 'coffin_up_broken'}


PROPS = {
    'lamp': lambda: gas_lamp(True),
    'lamp_off': lambda: gas_lamp(False),
    'brazier': brazier,
    'pyre': pyre,
    'candles': lambda: candles(3, 1),
    'candles2': lambda: candles(4, 7),
    'grave_round': lambda: headstone('round', 0),
    'grave_round2': lambda: headstone('round', 1),
    'grave_cross': lambda: headstone('cross', 1),
    'grave_celtic': lambda: headstone('celtic', 0),
    'grave_obelisk': lambda: headstone('obelisk', 1),
    'grave_broken': lambda: headstone('broken', 0),
    'grave_slab': lambda: headstone('slab', 1),
    'angel': angel,
    'mausoleum': mausoleum,
    'coffin': lambda: coffin(False),
    'coffin_up': lambda: coffin(True),
    'carriage': carriage,
    'barrel': barrel,
    'crate': lambda: crate(1),
    'crates': lambda: crate(2),
    'fountain': fountain,
    'well': well,
    'bench': bench,
    'chimney': lambda: chimney(2),
    'chimney1': lambda: chimney(1),
    'spire': spire,
    'tree': lambda: dead_tree(3, 1.0),
    'tree2': lambda: dead_tree(11, 0.85),
    'tree3': lambda: dead_tree(21, 1.15),
    'bush': lambda: bush(0),
    'bush2': lambda: bush(5),
    'fence_x': lambda: fence('x'),
    'fence_y': lambda: fence('y'),
    'fence_post': fence_post,
    'bollard': bollard,
    'cage': cage,
    'shrine': lambda: shrine(False),
    'shrine_lit': lambda: shrine(True),
    'barrel_broken': barrel_broken,
    'crate_broken': lambda: crate_broken(1, 5),
    'crates_broken': lambda: crate_broken(2, 8),
    'bench_broken': bench_broken,
    'coffin_broken': lambda: coffin_broken(0),
    'coffin_up_broken': lambda: coffin_broken(3),
}


# how much bigger than modelled each prop is drawn, next to the hunter
SCALE = {
    'lamp': 1.3, 'lamp_off': 1.3, 'brazier': 1.6, 'pyre': 1.5, 'candles': 1.5, 'candles2': 1.5,
    'grave_round': 2.0, 'grave_round2': 2.0, 'grave_cross': 2.0, 'grave_celtic': 2.0, 'grave_obelisk': 2.0,
    'grave_broken': 2.0, 'grave_slab': 2.0, 'angel': 2.0, 'mausoleum': 2.2, 'coffin': 2.6, 'coffin_up': 2.6,
    'carriage': 2.0, 'barrel': 2.0, 'crate': 2.0, 'crates': 2.0, 'fountain': 2.6, 'well': 1.9, 'bench': 2.2,
    'chimney': 1.6, 'chimney1': 1.6, 'spire': 1.9, 'tree': 1.9, 'tree2': 1.9, 'tree3': 1.9, 'bush': 1.8,
    'bush2': 1.8, 'fence_x': 2.0, 'fence_y': 2.0, 'fence_post': 2.0, 'bollard': 1.4, 'cage': 2.2,
    'shrine': 1.5, 'shrine_lit': 1.5,
    'barrel_broken': 2.0, 'crate_broken': 2.0, 'crates_broken': 2.0, 'bench_broken': 2.2, 'coffin_broken': 2.6,
    'coffin_up_broken': 2.6,
}


def auto_tex(model):
    """a texture for every part that has none, by material"""
    for pr in model.prims:
        if pr.tex is not None or pr.matf is not None:
            continue
        if pr.mat in (STONE, GSTONE):
            pr.tex = tex_noise(0.06, 1.1, seed=pr.group)
        elif pr.mat == MARBLE:
            pr.tex = tex_noise(0.035, 0.9)
        elif pr.mat in (WOOD, DWOOD):
            pr.tex = tex_planks(0, 2.4, seed=pr.group)
        elif pr.mat == BARK:
            pr.tex = tex_bark()
        elif pr.mat == LEAF:
            pr.tex = tex_noise(0.12, 0.9)
        elif pr.mat == IRON:
            pr.tex = tex_noise(0.03, 1.0)


def sprite(name, ss=3, scale=None):
    model, (c, R), _ = PROPS[name]()
    auto_tex(model)
    k = scale or SCALE.get(name, 1.0)
    model.scale = k
    from sdf import camera
    _, r, u, _ = camera()
    c = np.asarray(c, float) * k
    R = R * k + 2
    W = H = int(2 * R + 8)
    CX = int(round(W / 2 - c @ r))
    CY = int(round(H / 2 + c @ u))
    img, proj = render(model, W, H, CX, CY, bound=(c, R), ss=ss)
    rgba = pixelize(img, proj)
    c, (x0, y0) = crop(rgba)
    return c, (CX - x0, CY - y0)
