"""The hunter (Bloodborne fan art): tricorne, mask, long coat with capelet,
saw cleaver and pistol. Idle (4 frames) and walk (8 frames) in 8 directions,
rendered with sdf.py on a 15 colour SNES palette (5 bits per channel).

    frame(kind, k, n, direction) -> RGBA 64x72, feet at (CX, CY)
    python3 hunter.py DIR        the hunter on his own: sheet and GIFs
"""
import math
import numpy as np

from sdf import (D, Model, Skel, hash2, jag, material, nrm, render, pixelize, rx, ry, rz,
                 sd_box, sd_cone, sd_ellipsoid, sd_roundcone, sd_tri)

# 15 colours + transparent, every channel a multiple of 8 (SNES BGR555)
PAL = [
    (0, 0, 0),        # 0 transparent
    (24, 16, 32),     # 1 outline
    (40, 32, 48),     # 2 coat 0
    (64, 48, 64),     # 3 coat 1
    (96, 72, 80),     # 4 coat 2
    (136, 112, 104),  # 5 coat 3
    (72, 40, 32),     # 6 leather 0
    (112, 64, 40),    # 7 leather 1
    (168, 112, 80),   # 8 tan: leather light / skin shadow
    (224, 168, 136),  # 9 skin
    (160, 144, 128),  # 10 cloth 0
    (208, 192, 168),  # 11 cloth 1
    (88, 96, 112),    # 12 steel 0
    (152, 160, 176),  # 13 steel 1
    (232, 232, 224),  # 14 steel 2
    (144, 24, 32),    # 15 blood
]


def hmat(name, idx, cuts, **kw):
    return material('hunter_' + name, [PAL[i] for i in idx], cuts, **kw)


COAT = hmat('coat', [2, 3, 4, 5], [0.40, 0.68, 0.88])
CAPE = hmat('cape', [2, 3, 4, 5], [0.38, 0.66, 0.86])
HAT = hmat('hat', [2, 3, 4, 5], [0.38, 0.64, 0.85], weight=1.1)
PANTS = hmat('pants', [1, 2, 3], [0.35, 0.65])
LINING = hmat('lining', [1, 2], [0.6])
HAIR = hmat('hair', [1, 2, 3], [0.4, 0.7])
LEATH = hmat('leather', [6, 7, 8], [0.55, 0.92], weight=1.4)
BOOT = hmat('boot', [1, 6, 7], [0.40, 0.80])
BELT = hmat('belt', [1, 6, 7], [0.35, 0.70], weight=1.2)
MASK = hmat('mask', [10, 11], [0.36], weight=1.3, keep=True, noline=True)
SKIN = hmat('skin', [8, 9], [0.55], weight=1.6, keep=True, noline=True)
STEEL = hmat('steel', [12, 13, 14], [0.40, 0.80], weight=2.2, detail=True, keep=True, metal=True)
DSTEEL = hmat('dsteel', [1, 12, 13], [0.35, 0.72], weight=2.2, detail=True, keep=True, metal=True)
BLOOD = hmat('blood', [1, 15], [0.30], weight=2.0, detail=True, keep=True)
WOOD = hmat('wood', [6, 7], [0.55], weight=1.8, detail=True)
BUCKLE = hmat('buckle', [12, 13, 14], [0.3, 0.6], weight=3.0, detail=True, keep=True, noline=True, metal=True)

DIRS = [('S', (0, -1)), ('SE', (1, -1)), ('E', (1, 0)), ('NE', (1, 1)),
        ('N', (0, 1)), ('NW', (-1, 1)), ('W', (-1, 0)), ('SW', (-1, -1))]
W, H = 64, 72           # frame
CX, CY = 32, 62         # the point between the feet

def pose_params(kind, k, n):
    """joint angles (radians) and offsets for frame k of n"""
    p = dict(lean=D(7), chest_yaw=0.0, pelvis_yaw=0.0, head_pitch=D(-4), head_yaw=0.0,
             breath=0.0, sway=0.0,
             th_r=D(5), th_l=D(-3), knee_r=D(6), knee_l=D(4), foot_r=0.0, foot_l=0.0,
             spread=D(4.5),
             sw_r=D(-4), sw_l=D(2), abd_r=D(14), abd_l=D(13), el_r=D(22), el_l=D(26),
             wr_r=0.0, wr_l=0.0, skirt_lag=0.0, cape=0.0)
    if kind == 'idle':
        ph = 2 * math.pi * k / n
        b = 0.5 - 0.5 * math.cos(ph)            # 0 .. 1 .. 0
        p['breath'] = b
        p['lean'] += D(1.5) * b
        p['head_pitch'] += D(-2) * b
        p['sw_r'] += D(1.5) * b
        p['sw_l'] += D(-1.5) * b
        p['el_r'] += D(3) * b
        p['el_l'] += D(3) * b
        p['sway'] = D(1.5) * math.sin(ph)
        p['cape'] = D(2.5) * b
    else:
        ph = 2 * math.pi * k / n
        c, s = math.cos(ph), math.sin(ph)
        A = D(26)
        p['lean'] = D(10)
        p['th_r'] = A * c
        p['th_l'] = -A * c
        p['knee_r'] = D(6) + D(55) * max(0.0, -s) ** 1.3 + D(6) * max(0.0, c)
        p['knee_l'] = D(6) + D(55) * max(0.0, s) ** 1.3 + D(6) * max(0.0, -c)
        # keep the feet level, push off with the toes at the back
        p['foot_r'] = (p['knee_r'] - p['th_r']) - D(18) * max(0.0, math.cos(ph - math.pi + 0.5)) ** 2
        p['foot_l'] = (p['knee_l'] - p['th_l']) - D(18) * max(0.0, math.cos(ph + 0.5)) ** 2
        p['spread'] = D(2)
        p['pelvis_yaw'] = D(7) * c
        p['chest_yaw'] = -D(11) * c
        p['head_yaw'] = D(4) * c
        p['sw_r'] = -D(16) * c - D(4)
        p['sw_l'] = D(20) * c + D(2)
        p['el_r'] = D(22) + D(14) * max(0.0, -c)
        p['el_l'] = D(26) + D(16) * max(0.0, c)
        p['abd_r'] = D(13)
        p['abd_l'] = D(11)
        p['wr_r'] = D(6) * c
        p['wr_l'] = -D(6) * c
        p['sway'] = D(3) * math.sin(2 * ph + 0.6)
        p['cape'] = D(4) * (0.5 - 0.5 * math.cos(2 * ph + 0.8))
        p['skirt_lag'] = 0.45
        p['ph'] = ph
    p['kind'] = kind
    return p

def skeleton(p, pz=0.0):
    S = Skel()
    S.add('pelvis', 'root', [0, 0, 26.0 + pz], rz(p['pelvis_yaw']))
    S.add('chest', 'pelvis', [0, 0, 4.0], rz(p['chest_yaw']) @ rx(-p['lean']))
    S.add('head', 'chest', [0, 0.6, 14.0 + 0.35 * p['breath']],
          rz(p['head_yaw'] - 0.4 * p['chest_yaw']) @ rx(p['head_pitch'] + 0.6 * p['lean']))
    S.add('hat', 'head', [0, -0.3, 7.7], rx(D(8)))
    for side, sg in (('r', 1), ('l', -1)):
        S.add('uarm_' + side, 'chest', [sg * 6.3, -0.3, 11.6 + 0.3 * p['breath']],
              ry(-sg * p['abd_' + side]) @ rx(p['sw_' + side] + 0.5 * p['lean']))
        S.add('farm_' + side, 'uarm_' + side, [0, 0, -10.0], rx(p['el_' + side]) @ ry(sg * D(6)))
        S.add('hand_' + side, 'farm_' + side, [0, 0, -9.0], rx(p['wr_' + side]))
        S.add('thigh_' + side, 'pelvis', [sg * 3.1, 0, -0.6],
              ry(-sg * p['spread']) @ rx(p['th_' + side]))
        S.add('shin_' + side, 'thigh_' + side, [0, 0, -12.6], rx(-p['knee_' + side]))
        S.add('foot_' + side, 'shin_' + side, [0, 0, -11.6],
              rz(-sg * D(8)) @ rx(p['foot_' + side]))
        # coat skirt halves follow the thighs, a bit late and less
        lag = p.get('skirt_lag', 0.0)
        if p['kind'] == 'walk':
            ph = p['ph'] - lag
            th = (D(26) * math.cos(ph)) * sg
        else:
            th = p['th_' + side]
        S.add('skirt_' + side, 'pelvis', [sg * 2.0, 0.3, 4.2],
              ry(-sg * D(7)) @ rx(0.62 * th + p['sway'] * 0.5))
    S.add('skirt_b', 'pelvis', [0, -0.8, 4.2], rx(-D(3) - abs(p['sway']) * 0.8 - 0.25 * abs(p['th_r'])))
    S.add('cape', 'chest', [0, 0, 14.6], rx(-p['cape']))
    # weapons
    S.add('cleaver', 'hand_r', [0.2, 0.5, -1.3], rz(D(-18)) @ rx(D(46)) @ ry(D(-8)))
    S.add('pistol', 'hand_l', [-0.1, 0.4, -1.2], ry(D(18)) @ rx(D(30)))
    return S

def grounded(p):
    """skeleton with the lowest sole on z = 0"""
    S = skeleton(p)
    low = 1e9
    for side in 'rl':
        for pt in ((0, -1.4, -1.9), (0, 3.6, -1.9)):
            low = min(low, S.pt('foot_' + side, pt)[2])
    return skeleton(p, -low)

# ---------------------------------------------------------------- model
def model(S, p):
    M = Model(S)
    add = M.add

    # legs: trousers, tall boots, feet
    for side, sg in (('r', 1), ('l', -1)):
        add('thigh_' + side, lambda q: sd_roundcone(q, (0, 0, 0), (0, 0, -12.6), 2.7, 2.1), PANTS, 'leg' + side)
        add('shin_' + side, lambda q: sd_roundcone(q, (0, 0, 0.3), (0, 0, -11.2), 2.15, 1.6), BOOT, 'leg' + side)
        add('shin_' + side, lambda q: sd_roundcone(q, (0, 0.15, -1.2), (0, 0.15, -3.0), 2.5, 2.4), BOOT, 'leg' + side)
        def foot(q):
            d = sd_roundcone(q, (0, -0.6, -0.9), (0, 3.4, -1.2), 1.55, 1.05)
            return np.maximum(d, -1.95 - q[:, 2])
        add('foot_' + side, foot, BOOT, 'leg' + side)

    # coat skirt: two halves around the legs and a back panel, ragged hem
    def skirt(sg):
        def f(q):
            qq = q / np.array([1.0, 0.80, 1.0])
            d = sd_cone(qq, -21.0, 0.5, 6.0, 4.0) * 0.80
            ang = np.arctan2(q[:, 1], q[:, 0] * sg)
            hem = -19.8 + jag(ang, 9, 1.6, seed=1.3 + sg)
            d = np.maximum(d, hem - q[:, 2])
            return d
        return f
    def skirt_mat(sg):
        def mf(q):
            m = np.full(len(q), COAT)
            # the front opening: dark lining on the inner front edge
            inner = (q[:, 0] * sg < -2.2) & (q[:, 1] > 2.2) & (q[:, 2] < -9.0)
            m[inner] = LINING
            return m
        return mf
    for side, sg in (('r', 1), ('l', -1)):
        add('skirt_' + side, skirt(sg), COAT, 'coat', skirt_mat(sg))
    def back(q):
        qq = q / np.array([1.0, 0.7, 1.0])
        d = sd_cone(qq, -21.5, 0.5, 6.8, 4.3) * 0.7
        d = np.maximum(d, -q[:, 1] - 0.6 + 0.0 * q[:, 2])   # back half only
        d = np.maximum(d, q[:, 1] - 1.5)
        ang = np.arctan2(q[:, 1], q[:, 0])
        hem = -20.3 + jag(ang, 10, 1.7, seed=4.1)
        return np.maximum(d, hem - q[:, 2])
    add('skirt_b', lambda q: np.maximum(back(q * np.array([1, -1, 1])), -1e9), COAT, 'coat')

    # torso of the coat, belt and buckle
    torso = lambda q: sd_roundcone(q / np.array([1.0, 0.70, 1.0]), (0, 0, 0.5), (0, 0, 10.0), 4.4, 5.5) * 0.70
    add('chest', torso, COAT, 'coat')
    def belt(q):
        d = sd_roundcone(q / np.array([1.0, 0.72, 1.0]), (0, 0, 0.5), (0, 0, 10.0), 4.75, 5.85) * 0.72
        return np.maximum(d, np.abs(q[:, 2] - 1.6) - 0.85)
    add('chest', belt, BELT, 'coat')
    add('chest', lambda q: sd_box(q - np.array([0.0, 3.9, 1.6]), (1.0, 0.5, 0.9), 0.2), BUCKLE, 'coat')
    # a strap across the chest, from the right shoulder to the left hip
    def strap(q):
        a = np.array([4.2, 0.0, 12.0]); b = np.array([-4.8, 0.0, 2.2])
        d = sd_roundcone(q / np.array([1.0, 0.70, 1.0]), (0, 0, 0.5), (0, 0, 10.0), 4.65, 5.75) * 0.70
        n = nrm(np.cross(b - a, [0, 1, 0]))
        band = np.abs((q - a) @ n) - 0.8
        return np.maximum(d, band)
    add('chest', strap, BELT, 'coat')

    # capelet over the shoulders, high collar
    def cape(q):
        qq = q / np.array([1.0, 0.64, 1.0])
        d = sd_cone(qq, -9.4, -2.2, 8.7, 7.7) * 0.64
        top = sd_ellipsoid(q - np.array([0, -0.2, -2.4]), (7.7, 4.9, 3.4))
        d = np.minimum(d, top)
        ang = np.arctan2(q[:, 1], q[:, 0])
        hem = -8.3 + jag(ang, 11, 1.4, seed=7.7)
        return np.maximum(d, hem - q[:, 2])
    add('cape', cape, CAPE, 'cape')
    def collar(q):
        d = sd_roundcone(q, (0, 0.2, -1.5), (0, 0.6, 2.4), 3.9, 3.6)
        d = np.maximum(d, -(sd_roundcone(q, (0, 0.2, -1.5), (0, 0.6, 3.5), 2.9, 2.8)))
        top = 1.7 - 1.6 * np.clip(q[:, 1] / 3.5, 0, 1)
        return np.maximum(d, q[:, 2] - top)
    add('cape', collar, CAPE, 'cape')

    # head: hair, face, mask
    def head_mat(q):
        m = np.full(len(q), HAIR)
        face = (q[:, 1] > 0.9) & (q[:, 2] > 3.6) & (q[:, 2] < 7.0) & (np.abs(q[:, 0]) < 2.8)
        m[face] = SKIN
        return m
    add('head', lambda q: sd_ellipsoid(q - np.array([0, 0.3, 4.9]), (2.9, 3.2, 3.5)), HAIR, 'head', head_mat)
    def mask(q):
        d = sd_ellipsoid(q - np.array([0, 0.75, 3.7]), (3.15, 3.35, 2.9))
        d = np.minimum(d, sd_roundcone(q, (0, 0.2, -0.5), (0, 0.6, 3.0), 2.7, 3.0))
        top = 4.75 - 0.45 * np.clip(q[:, 1], 0, 4) / 4
        return np.maximum(d, q[:, 2] - top)
    add('head', mask, MASK, 'head')

    # tricorne hat
    def hat(q):
        x, y, z = q[:, 0], q[:, 1] + 0.3, q[:, 2]
        t = sd_tri(x / 1.25, y, 5.0) * 1.0 - 1.6
        plate = np.maximum(t, np.abs(z - 0.25) - 0.45)
        # the turned-up sides: low at the front point, high at the side corners
        wh = 0.8 + 0.6 * np.clip(np.abs(x) / 6.5, 0, 1) - 0.35 * np.clip(y / 6, 0, 1)
        wall = np.maximum(np.maximum(t, -(t + 1.0)), np.maximum(-z, z - wh))
        # the turned-up sides dip towards the corners
        crown = sd_ellipsoid(q - np.array([0, -0.3, 0.8]), (3.2, 3.4, 2.7))
        crown = np.maximum(crown, -z - 0.2)
        return np.minimum(np.minimum(plate, wall), crown)
    add('hat', hat, HAT, 'hat')

    # arms: sleeves, gauntlets, gloves
    for side, sg in (('r', 1), ('l', -1)):
        g = 'arm' + side
        add('uarm_' + side, lambda q: sd_roundcone(q, (0, 0, 0), (0, 0, -10.0), 2.3, 1.95), COAT, g)
        add('farm_' + side, lambda q: sd_roundcone(q, (0, 0, 0), (0, 0, -6.6), 1.95, 1.75), COAT, g)
        add('farm_' + side, lambda q: sd_roundcone(q, (0, 0, -5.6), (0, 0, -8.6), 2.15, 1.95), LEATH, g)
        add('hand_' + side, lambda q: sd_ellipsoid(q - np.array([0, 0.3, -1.2]), (1.45, 1.75, 1.85)), LEATH, g)

    # saw cleaver (folded): handle, then the serrated blade
    def handle(q):
        return sd_roundcone(q, (0, 0, 2.4), (0, 0, -1.6), 0.75, 0.7)
    add('cleaver', handle, WOOD, 'cleaver')
    def blade(q):
        x, y, z = q[:, 0], q[:, 1], q[:, 2]
        L0, L1 = -1.2, -15.0
        u = np.clip((z - L0) / (L1 - L0), 0, 1)          # 0 at the guard, 1 at the tip
        y0 = -0.9
        y1 = 1.7 + 1.6 * u
        teeth = 0.75 * ((-z * 0.8) % 1.0)
        y1t = y1 + teeth * (z < L0 - 0.8)
        cy = (y0 + y1t) / 2
        hy = (y1t - y0) / 2
        dz = np.maximum(z - L0, L1 - z)
        # rounded tip: cut a corner of the back edge
        d = np.maximum.reduce([np.abs(x) - 0.45, np.abs(y - cy) - hy, dz])
        tip = (y - y0) * 0.7 + (z - L1 - 1.5)
        return np.maximum(d, -tip * 0.6)
    def blade_mat(q):
        x, y, z = q[:, 0], q[:, 1], q[:, 2]
        m = np.full(len(q), STEEL)
        u = (z + 1.2) / -13.8
        n = hash2(np.floor(z * 0.9), np.floor(y * 0.9) + 3)
        m[(u > 0.55) & (n > 0.45 - (u - 0.55))] = BLOOD
        m[(y < -0.4) & (u < 0.9)] = DSTEEL              # the dark back of the blade
        return m
    add('cleaver', blade, STEEL, 'cleaver', blade_mat)

    # hunter pistol: grip, lock, long barrel
    add('pistol', lambda q: sd_roundcone(q, (0, -0.4, 2.6), (0, 0.2, -0.6), 0.9, 0.75), WOOD, 'pistol')
    add('pistol', lambda q: sd_box(q - np.array([0, 0.3, -1.8]), (0.6, 1.0, 1.3), 0.2), DSTEEL, 'pistol')
    add('pistol', lambda q: sd_roundcone(q, (0, 0.4, -2.0), (0, 0.4, -9.6), 0.62, 0.55), STEEL, 'pistol')
    add('pistol', lambda q: sd_roundcone(q, (0, 0.4, -9.2), (0, 0.4, -9.9), 0.8, 0.8), DSTEEL, 'pistol')

    # single pixels that must show: the eyes, the glint of the buckle
    M.decals = [(S.pt('head', (1.15, 3.35, 5.25)), PAL[1]),
                (S.pt('head', (-1.15, 3.35, 5.25)), PAL[1]),
                (S.pt('chest', (0.0, 4.35, 1.6)), PAL[14])]
    return M


def frame(kind, k, n, facing):
    p = pose_params(kind, k, n)
    img, proj = render(model(grounded(p), p), W, H, CX, CY, facing=facing)
    return pixelize(img, proj, outline=PAL[1])


def export(out):
    """the hunter on his own: an indexed sheet (rows S SE E NE N NW W SW,
    columns idle 0-3 and walk 0-7, 64x72 cells, feet at (CX, CY)) and two
    GIFs of the 8 directions, standing and walking, 3x"""
    import os
    from multiprocessing import Pool
    from PIL import Image
    os.makedirs(out, exist_ok=True)
    jobs = [(d, 'idle', k, 4) for d in range(8) for k in range(4)] + \
           [(d, 'walk', k, 8) for d in range(8) for k in range(8)]
    with Pool(4) as pool:
        frames = pool.starmap(frame, [(kind, k, n, DIRS[d][1]) for d, kind, k, n in jobs])
    index = {c: i for i, c in enumerate(PAL)}
    sheet = np.zeros((8 * H, 12 * W), np.uint8)
    for (d, kind, k, n), f in zip(jobs, frames):
        col = k if kind == 'idle' else 4 + k
        idx = np.array([[index[tuple(p[:3])] if p[3] else 0 for p in row] for row in f], np.uint8)
        sheet[d * H:(d + 1) * H, col * W:(col + 1) * W] = idx
    pal = [v for c in PAL for v in c] + [0] * (768 - 3 * len(PAL))
    im = Image.fromarray(sheet, 'P')
    im.putpalette(pal)
    im.save(os.path.join(out, 'hunter_sheet.png'), transparency=0)
    # the 8 directions on a compass, on a dark street
    order = ['NW', 'N', 'NE', 'W', None, 'E', 'SW', 'S', 'SE']
    names = [d[0] for d in DIRS]
    for kind, n, first, ms in (('idle', 4, 0, 260), ('walk', 8, 4, 110)):
        imgs = []
        for k in range(n):
            canvas = np.zeros((3 * H - 30, 3 * W, 3), np.uint8)
            canvas[:] = (56, 60, 72)
            for slot, name in enumerate(order):
                if name is None:
                    continue
                d = names.index(name)
                cell = sheet[d * H:(d + 1) * H, (first + k) * W:(first + k + 1) * W]
                ox, oy = (slot % 3) * W, (slot // 3) * (H - 15) - 8
                for y in range(H):
                    if 0 <= oy + y < canvas.shape[0]:
                        row = cell[y]
                        m = row > 0
                        canvas[oy + y, ox:ox + W][m] = np.array(PAL, np.uint8)[row[m]]
            imgs.append(Image.fromarray(canvas).resize((canvas.shape[1] * 3, canvas.shape[0] * 3), Image.NEAREST))
        imgs[0].save(os.path.join(out, 'hunter_%s.gif' % kind), save_all=True, append_images=imgs[1:], loop=0,
                     duration=ms)
    print('written to', out)


if __name__ == '__main__':
    import sys
    export(sys.argv[1] if len(sys.argv) > 1 else 'hunter_out')
