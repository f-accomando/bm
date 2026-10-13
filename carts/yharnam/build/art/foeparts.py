"""The colours and the body parts the creatures share (art/foe_*.py).

The whole sheet holds at most 255 colours, so the creatures draw from a few
ramps here (several of them the hunter's own), each 5 bits per channel.
Body parts are added to a Model on the bones of rig.humanoid.
"""
import numpy as np

from sdf import hash2, jag, material, sd_cone, sd_ellipsoid, sd_roundcone, sd_tri


def fmat(name, ramp, cuts, **kw):
    return material('foe_' + name, ramp, cuts, **kw)


INK = (24, 16, 32)

# ---------------------------------------------------------------- shared ramps
SKIN = fmat('skin', [(104, 72, 64), (160, 120, 104), (208, 168, 144)], [0.45, 0.80], weight=1.6, keep=True,
            noline=True)
SKIN_DARK = fmat('skin_dark', [(72, 48, 48), (104, 72, 64), (160, 120, 104)], [0.45, 0.80], weight=1.5,
                 keep=True, noline=True)
FLESH = fmat('flesh', [(72, 16, 24), (128, 32, 40), (184, 72, 72)], [0.40, 0.78], weight=1.4)
BLOOD = fmat('blood', [INK, (144, 24, 32)], [0.30], weight=2.0, detail=True, keep=True)
STEEL = fmat('steel', [(88, 96, 112), (152, 160, 176), (232, 232, 224)], [0.40, 0.80], weight=2.2, detail=True,
             keep=True, metal=True)
DSTEEL = fmat('dsteel', [INK, (88, 96, 112), (152, 160, 176)], [0.35, 0.72], weight=2.2, detail=True, keep=True,
              metal=True)
RUST = fmat('rust', [(48, 24, 24), (104, 56, 40), (152, 96, 64)], [0.40, 0.76], weight=2.0, detail=True,
            keep=True, metal=True)
WOOD = fmat('wood', [(72, 40, 32), (112, 64, 40), (168, 112, 80)], [0.45, 0.85], weight=1.8, detail=True)
LEATHER = fmat('leather', [(72, 40, 32), (112, 64, 40), (168, 112, 80)], [0.50, 0.90], weight=1.3)
BOOT = fmat('boot', [INK, (72, 40, 32), (112, 64, 40)], [0.40, 0.80])
BELT = fmat('belt', [INK, (72, 40, 32), (112, 64, 40)], [0.35, 0.70], weight=1.2)
SHIRT = fmat('shirt', [(112, 96, 88), (160, 144, 128), (208, 192, 168)], [0.58, 0.97])
LINEN = fmat('linen', [(112, 96, 88), (160, 144, 128), (208, 192, 168)], [0.42, 0.78])
BANDAGE = fmat('bandage', [(112, 96, 88), (160, 144, 128), (208, 192, 168)], [0.40, 0.75], weight=1.4,
               keep=True, noline=True)
TROUSERS = fmat('trousers', [INK, (40, 32, 48), (64, 48, 64)], [0.35, 0.65])
HAIR = fmat('hair', [INK, (40, 32, 48), (64, 48, 64)], [0.40, 0.70])
GREY_HAIR = fmat('grey_hair', [(96, 88, 96), (152, 144, 144), (200, 192, 184)], [0.40, 0.75])
BLACK = fmat('black', [INK, (40, 32, 48), (64, 48, 64), (96, 72, 80)], [0.38, 0.64, 0.86])
FLAME = fmat('flame', [(200, 72, 16), (248, 152, 40), (248, 232, 136), (248, 248, 208)], [0.55, 0.75, 0.92],
             weight=3.0, detail=True, keep=True, noline=True, glow=True)
GLINT = fmat('glint', [(152, 160, 176), (232, 232, 224)], [0.5], weight=3.0, detail=True, keep=True,
             noline=True, metal=True)
BONE = fmat('bone', [(136, 128, 112), (184, 176, 152), (224, 216, 192)], [0.40, 0.78], weight=1.6, keep=True)
TEETH = fmat('teeth', [(184, 176, 152), (224, 216, 192)], [0.4], weight=2.5, detail=True, keep=True,
             noline=True)
EYE = fmat('eye', [(232, 200, 120), (248, 240, 192)], [0.4], weight=4.0, detail=True, keep=True, noline=True,
           glow=True)
# cloth of the townsfolk
COAT_BROWN = fmat('coat_brown', [(48, 32, 32), (80, 56, 48), (120, 88, 64), (160, 128, 96)], [0.38, 0.66, 0.88])
COAT_GREEN = fmat('coat_green', [(32, 40, 32), (56, 64, 48), (88, 96, 72), (128, 128, 96)], [0.38, 0.66, 0.88])
COAT_BLUE = fmat('coat_blue', [(32, 32, 48), (48, 56, 80), (80, 88, 112), (120, 128, 144)], [0.38, 0.66, 0.88])
SHAWL = fmat('shawl', [(56, 24, 32), (96, 40, 40), (136, 64, 56)], [0.40, 0.76])
SACK = fmat('sack', [(88, 64, 40), (128, 96, 64), (176, 144, 96)], [0.40, 0.78])
LINING = fmat('lining', [INK, (40, 32, 48)], [0.6])


# ---------------------------------------------------------------- textures

def tex_cloth(amp=0.05, f=1.6, seed=0.0):
    """coarse weave: a faint noise of the light"""
    def t(q):
        return amp * (hash2(np.floor(q[:, 0] * f) + seed, np.floor(q[:, 2] * f) * 7.1 + np.floor(q[:, 1] * f)) * 2 - 1)
    return t


def tex_fur(amp=0.14, f=1.2, seed=0.0):
    """tufts: streaks of light and dark along the hair"""
    def t(q):
        s = np.sin(q[:, 0] * 2.3 * f + np.sin(q[:, 2] * 0.9 * f) * 2.0 + seed) * np.sin(q[:, 1] * 1.7 * f + seed)
        return amp * np.sign(s) * (0.4 + 0.6 * hash2(np.floor(q[:, 0] * f * 1.5), np.floor(q[:, 2] * f) + seed))
    return t


# ---------------------------------------------------------------- body parts

def legs(M, pr, pants=TROUSERS, boot=BOOT, k=1.0, boots=True):
    for side in ('r', 'l'):
        g = 'leg' + side
        th, sh = pr['thigh'], pr['shin']
        M.add('thigh_' + side, lambda q, th=th: sd_roundcone(q, (0, 0, 0), (0, 0, -th), 2.7 * k, 2.1 * k), pants, g)
        M.add('shin_' + side, lambda q, sh=sh: sd_roundcone(q, (0, 0, 0.3), (0, 0, -sh + 0.4), 2.15 * k, 1.6 * k),
              boot if boots else pants, g)
        if boots:
            M.add('shin_' + side, lambda q: sd_roundcone(q, (0, 0.15, -1.2), (0, 0.15, -3.0), 2.5 * k, 2.4 * k),
                  boot, g)

        def foot(q):
            d = sd_roundcone(q, (0, -0.6 * k, -0.9), (0, 3.4 * k, -1.2), 1.55 * k, 1.05 * k)
            return np.maximum(d, -1.95 - q[:, 2])
        M.add('foot_' + side, foot, boot, g)


def arms(M, pr, sleeve, hand=SKIN, cuff=None, k=1.0, kh=1.0):
    for side in ('r', 'l'):
        g = 'arm' + side
        ua, fa = pr['uarm'], pr['farm']
        M.add('uarm_' + side, lambda q, ua=ua: sd_roundcone(q, (0, 0, 0), (0, 0, -ua), 2.3 * k, 1.95 * k), sleeve, g)
        M.add('farm_' + side, lambda q, fa=fa: sd_roundcone(q, (0, 0, 0), (0, 0, -fa + 1.6), 1.95 * k, 1.6 * k),
              sleeve, g)
        if cuff is not None:
            M.add('farm_' + side, lambda q, fa=fa: sd_roundcone(q, (0, 0, -fa + 3.6), (0, 0, -fa + 1.0),
                                                                2.1 * k, 1.85 * k), cuff, g)
        M.add('hand_' + side, lambda q: sd_ellipsoid(q - np.array([0, 0.3, -1.1 * kh]),
                                                     (1.45 * kh, 1.75 * kh, 1.85 * kh)), hand, g)


def torso(M, pr, mat, k=1.0, belly=0.0, depth=0.70, tex=None, matf=None):
    """the chest from the waist to the shoulders; belly pushes the front out"""
    top = pr['chest'] - 1.6
    sc = np.array([1.0, depth, 1.0])

    def f(q):
        d = sd_roundcone(q / sc, (0, 0, 0.5), (0, 0, top), 4.4 * k, 5.5 * k) * depth
        if belly:
            d = np.minimum(d, sd_ellipsoid(q - np.array([0, 1.0 * belly, 3.0]), (4.4 * k, 3.0 + 0.8 * belly,
                                                                                    4.6 * k)))
        return d
    M.add('chest', f, mat, 'body', matf, tex)
    return f


def belt(M, pr, mat=BELT, z=1.6, k=1.0, depth=0.72):
    top = pr['chest'] - 1.6
    sc = np.array([1.0, depth, 1.0])

    def f(q):
        d = sd_roundcone(q / sc, (0, 0, 0.5), (0, 0, top), 4.75 * k, 5.85 * k) * depth
        return np.maximum(d, np.abs(q[:, 2] - z) - 0.85)
    M.add('chest', f, mat, 'body')


def skirt(M, mat, length=21.0, top_r=4.0, hem_r=6.0, teeth=1.6, depth=0.80, matf=None, tex=None, group='coat',
          back_len=None):
    """the two halves of a coat's skirt over the thighs and a back panel,
    ragged at the hem"""
    def half(sg):
        def f(q):
            qq = q / np.array([1.0, depth, 1.0])
            d = sd_cone(qq, -length, 0.5, hem_r, top_r) * depth
            ang = np.arctan2(q[:, 1], q[:, 0] * sg)
            hem = -length + 1.2 + jag(ang, 9, teeth, seed=1.3 + sg)
            return np.maximum(d, hem - q[:, 2])
        return f
    for side, sg in (('r', 1), ('l', -1)):
        M.add('skirt_' + side, half(sg), mat, group, matf, tex)
    bl = back_len or length + 0.5

    def back(q):
        q = q * np.array([1, -1, 1])
        qq = q / np.array([1.0, 0.7, 1.0])
        d = sd_cone(qq, -bl, 0.5, hem_r + 0.8, top_r + 0.3) * 0.7
        d = np.maximum(d, -q[:, 1] - 0.6)
        d = np.maximum(d, q[:, 1] - 1.5)
        ang = np.arctan2(q[:, 1], q[:, 0])
        hem = -bl + 1.2 + jag(ang, 10, teeth, seed=4.1)
        return np.maximum(d, hem - q[:, 2])
    M.add('skirt_b', back, mat, group, matf, tex)


def long_skirt(M, mat, length, top_r=4.2, hem_r=8.0, depth=0.85, tex=None, group='coat'):
    """a dress down to the ground: one bell from the waist, on the pelvis"""
    def f(q):
        qq = q / np.array([1.0, depth, 1.0])
        d = sd_cone(qq, -length, 1.0, hem_r, top_r) * depth
        ang = np.arctan2(q[:, 1], q[:, 0])
        hem = -length + 0.8 + jag(ang, 12, 1.2, seed=2.2)
        return np.maximum(d, hem - q[:, 2])
    M.add('pelvis', f, mat, group, None, tex)


def capelet(M, mat, length=8.3, w=8.7, k=1.0, group='cape', tex=None):
    def f(q):
        qq = q / np.array([1.0, 0.64, 1.0])
        d = sd_cone(qq, -length - 1.1, -2.2, w * k, (w - 1.0) * k) * 0.64
        top = sd_ellipsoid(q - np.array([0, -0.2, -2.4]), (7.7 * k, 4.9 * k, 3.4))
        d = np.minimum(d, top)
        ang = np.arctan2(q[:, 1], q[:, 0])
        hem = -length + jag(ang, 11, 1.4, seed=7.7)
        return np.maximum(d, hem - q[:, 2])
    M.add('cape', f, mat, group, None, tex)


def scarf(M, mat, k=1.0):
    """wound round the neck, up to the nose"""
    def f(q):
        d = sd_roundcone(q, (0, 0.2, -1.6), (0, 0.6, 2.4), 3.6 * k, 3.3 * k)
        return d
    M.add('cape', f, mat, 'cape')


def head(M, skin=SKIN, hair=HAIR, k=1.0, bald=False, nose=1.0):
    """a head: hair at the back and on top, the face in front, a nose"""
    def mf(q):
        m = np.full(len(q), skin if bald else hair)
        face = (q[:, 1] > 0.9 * k) & (q[:, 2] > 3.2) & (q[:, 2] < 7.0 * k) & (np.abs(q[:, 0]) < 2.8 * k)
        m[face] = skin
        return m
    M.add('head', lambda q: sd_ellipsoid(q - np.array([0, 0.3, 4.9 * k]), (2.9 * k, 3.2 * k, 3.5 * k)), skin, 'head',
          mf)
    if nose:
        M.add('head', lambda q: sd_roundcone(q, (0, 2.9 * k, 5.2 * k), (0, 3.6 * k + 0.6 * nose, 4.0 * k), 0.6 * k,
                                             0.45 * k), skin, 'head')


def neck(M, mat=SKIN, k=1.0):
    M.add('chest', lambda q: sd_roundcone(q, (0, 0.4, 9.0), (0, 0.6, 13.6), 1.6 * k, 1.4 * k), mat, 'body')


def eyes(S, M, colour=INK, y=3.35, z=5.25, x=1.15):
    M.decals += [(S.pt('head', (x, y, z)), colour), (S.pt('head', (-x, y, z)), colour)]


# ---------------------------------------------------------------- hats

def cap(M, mat, k=1.0):
    """a flat cap with a short peak"""
    def f(q):
        crown = sd_ellipsoid(q - np.array([0, 0.2, 7.2 * k]), (3.3 * k, 3.6 * k, 1.8 * k))
        crown = np.maximum(crown, 6.7 * k - q[:, 2])
        peak = sd_ellipsoid(q - np.array([0, 3.2 * k, 6.8 * k]), (2.4 * k, 1.6 * k, 0.35))
        return np.minimum(crown, peak)
    M.add('head', f, mat, 'hat')


def wide_hat(M, mat, k=1.0, brim=6.0, crown_h=3.2, droop=0.6):
    """a wide brim, drooping at the edge, and a round crown"""
    def f(q):
        r = np.sqrt(q[:, 0] ** 2 + q[:, 1] ** 2)
        zb = 7.4 * k - droop * np.clip((r - 3.0) / 3.0, 0, 1) ** 2
        plate = np.maximum(r - brim * k, np.abs(q[:, 2] - zb) - 0.4)
        crown = sd_cone(q - np.array([0, 0, 7.2 * k]), 0.0, crown_h * k, 3.2 * k, 2.7 * k)
        return np.minimum(plate, crown - 0.3)
    M.add('head', f, mat, 'hat')


def top_hat(M, mat, k=1.0, h=7.0):
    def f(q):
        q = q - np.array([0, -0.4, 0])
        r = np.sqrt(q[:, 0] ** 2 + (q[:, 1] / 1.1) ** 2)
        plate = np.maximum(r - 4.6 * k, np.abs(q[:, 2] - 7.6 * k) - 0.35)
        crown = sd_cone(q - np.array([0, 0, 7.6 * k]), 0.0, h * k, 3.0 * k, 3.2 * k)
        return np.minimum(plate, crown - 0.2)
    M.add('head', f, mat, 'hat')


def hood(M, mat, k=1.0, peak=1.4, tex=None):
    """a hood over the head, open in front, its point at the back"""
    def f(q):
        c = np.array([0, -0.2, 5.0 * k])
        d = sd_ellipsoid(q - c, (3.6 * k, 3.9 * k, 4.2 * k))
        tip = sd_roundcone(q, (0, -1.5, 6.5 * k), (0, -3.4 * k, 9.0 * k + peak), 2.2 * k, 0.4)
        d = np.minimum(d, tip)
        hole = sd_ellipsoid(q - np.array([0, 3.2 * k, 4.6 * k]), (2.4 * k, 2.6 * k, 2.8 * k))
        return np.maximum(d, -hole)
    M.add('head', f, mat, 'hat', None, tex)


def tricorne(M, mat, k=1.0):
    def f(q):
        x, y, z = q[:, 0], q[:, 1] + 0.3, q[:, 2] - 7.7 * k
        t = sd_tri(x / 1.25, y, 5.0 * k) - 1.6
        plate = np.maximum(t, np.abs(z - 0.25) - 0.45)
        wh = 0.8 + 0.6 * np.clip(np.abs(x) / 6.5, 0, 1) - 0.35 * np.clip(y / 6, 0, 1)
        wall = np.maximum(np.maximum(t, -(t + 1.0)), np.maximum(-z, z - wh))
        crown = sd_ellipsoid(np.stack([x, y - 0.3, z - 0.8], 1), (3.2 * k, 3.4 * k, 2.7))
        crown = np.maximum(crown, -z - 0.2)
        return np.minimum(np.minimum(plate, wall), crown)
    M.add('head', f, mat, 'hat')


# ---------------------------------------------------------------- weapons, along -z of a weapon bone

def shaft(M, bone, z0, z1, r, mat=WOOD, group='weapon'):
    M.add(bone, lambda q: sd_roundcone(q, (0, 0, z0), (0, 0, z1), r, r * 0.9), mat, group)


def torch_flame(M, bone, z, size=1.0, group='weapon'):
    """a burning head on a stick: rags, and the flame going up (+z of the world
    is not known here: the flame is drawn along the stick, past its end)"""
    M.add(bone, lambda q: sd_ellipsoid(q - np.array([0, 0, z]), (1.3 * size, 1.3 * size, 1.8 * size)), SACK, group)

    def fl(q):
        p = q - np.array([0, 0, z - 1.8 * size])
        d = sd_roundcone(p, (0, 0, 0.8 * size), (0, 0, -3.4 * size), 1.9 * size, 0.3)
        wob = 0.35 * np.sin(p[:, 2] * 2.1 + p[:, 0] * 1.3)
        return d + wob * 0.4
    M.add(bone, fl, FLAME, group)


def blade_box(M, bone, z0, z1, y0, y1, thick=0.45, mat=STEEL, matf=None, group='weapon', point=0.0):
    """a flat blade along -z from z0 to z1 (z1 < z0), from y0 (the back) to
    y1 (the edge); point > 0 sharpens the end"""
    def f(q):
        x, y, z = q[:, 0], q[:, 1], q[:, 2]
        cy, hy = (y0 + y1) / 2, (y1 - y0) / 2
        d = np.maximum.reduce([np.abs(x) - thick, np.abs(y - cy) - hy, np.maximum(z - z0, z1 - z)])
        if point:
            d = np.maximum(d, ((y - y0) * point + (z - z1 - point * (y1 - y0))) * -0.6)
        return d
    M.add(bone, f, mat, group, matf)
FUR_DARK = fmat('fur_dark', [(32, 32, 40), (56, 48, 56), (88, 80, 88), (120, 112, 112)], [0.36, 0.62, 0.86],
                weight=1.0)
FUR_BROWN = fmat('fur_brown', [(48, 32, 32), (80, 56, 48), (120, 88, 64), (160, 128, 96)], [0.36, 0.62, 0.86],
                 weight=1.0)
FUR_PALE = fmat('fur_pale', [(88, 88, 96), (136, 136, 136), (184, 176, 168), (224, 216, 208)],
                [0.42, 0.68, 0.90], weight=1.0)
EYE_RED = fmat('eye_red', [(200, 40, 32), (248, 120, 80)], [0.4], weight=4.0, detail=True, keep=True, noline=True,
               glow=True)
NOSE = fmat('nose', [INK, (40, 32, 48)], [0.6], weight=2.0, keep=True, noline=True)
CLAW = fmat('claw', [(40, 32, 48), (136, 128, 112)], [0.5], weight=2.0, detail=True, keep=True)


def shaggy(f, amp=0.6, freq=1.3, seed=0.0):
    """a ragged surface: the distance pushed in and out by a noise of tufts"""
    def g(q):
        n = (np.sin(q[:, 0] * 2.1 * freq + seed) * np.sin(q[:, 1] * 1.7 * freq + 1.3 * seed)
             * np.sin(q[:, 2] * 2.6 * freq + 0.7 * seed))
        return f(q) - amp * (0.5 + 0.5 * n)
    return g
CHURCH = fmat('church', [(88, 88, 96), (136, 136, 136), (184, 176, 168), (224, 216, 208)], [0.40, 0.66, 0.88],
              weight=1.0)
GOLD = fmat('gold', [(96, 64, 24), (160, 120, 40), (216, 184, 88)], [0.40, 0.78], weight=2.2, detail=True,
            keep=True, metal=True)
# the hunter's own coat colours, and black cloth that catches more light
COAT_GREY = fmat('coat_grey', [(40, 32, 48), (64, 48, 64), (96, 72, 80), (136, 112, 104)], [0.38, 0.64, 0.86])
BLACK_LIT = fmat('black_lit', [INK, (40, 32, 48), (64, 48, 64), (96, 72, 80)], [0.30, 0.52, 0.76])
# the things from beyond
KIN = fmat('kin', [(48, 64, 96), (88, 112, 152), (136, 168, 200), (192, 216, 232)], [0.36, 0.62, 0.86], weight=1.1)
CYAN = fmat('cyan', [(88, 200, 216), (176, 240, 248)], [0.5], weight=3.0, detail=True, keep=True, noline=True,
            glow=True)
VIOLET = fmat('violet', [(40, 32, 56), (72, 48, 88), (112, 80, 120), (160, 128, 160)], [0.36, 0.62, 0.86])
PALE = fmat('pale', [(88, 80, 88), (136, 128, 112), (184, 176, 152), (224, 216, 192)], [0.36, 0.62, 0.86],
            weight=1.1)
ASH = fmat('ash', [(56, 48, 64), (88, 80, 96), (128, 120, 128), (176, 168, 168)], [0.36, 0.62, 0.86], weight=1.1)
EYE_RED_GLOW = (248, 120, 80)            # a decal: one glowing red pixel
