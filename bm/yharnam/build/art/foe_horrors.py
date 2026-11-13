"""Horrors of the nightmare (Bloodborne fan art): a celestial child, a
brainsucker, a lantern-headed woman, a spider with a human face; and their
boss, the Watcher, tall, with six arms and a head of eyes.
"""
import math

import numpy as np

import foeparts as fp
from foeparts import ASH, BLACK, CYAN, EYE, FLESH, KIN, LINEN, PALE, VIOLET
from rig import (DEATH, DEATH_BACK, HUMAN, HURT, POSE, Creature, floats, grounded, human_contacts, humanoid,
                 keyed, pose, sample, standing, walk_cycle)
from sdf import D, Model, Skel, hash2, jag, rx, ry, rz, sd_cone, sd_ellipsoid, sd_roundcone


def spots(base, glow, f=1.0, amount=0.82, seed=0.0):
    """a material function: glowing spots on a skin"""
    def mf(q):
        m = np.full(len(q), base)
        n = hash2(np.floor(q[:, 0] * f + 0.5 * np.floor(q[:, 2] * f)), np.floor(q[:, 2] * f) + seed)
        n2 = hash2(np.floor(q[:, 1] * f), np.floor(q[:, 2] * f) * 1.7 + seed)
        m[(n > amount) & (n2 > 0.4)] = glow
        return m
    return mf


# ================================================================ the celestial child

KP = dict(HUMAN, thigh=7.4, shin=7.0, waist=2.4, chest=7.0, neck=8.6, shoulder=3.6, hipw=1.8, uarm=7.0, farm=6.6,
          hand=1.0, cape=9.0, skirt=3.0, foot=1.2)


def kin_model(S, p):
    M = Model(S)
    for side in ('r', 'l'):
        g = 'leg' + side
        M.add('thigh_' + side, lambda q: sd_roundcone(q, (0, 0, 0), (0, 0, -KP['thigh']), 1.3, 0.9), KIN, g)
        M.add('shin_' + side, lambda q: sd_roundcone(q, (0, 0, 0), (0, 0, -KP['shin'] + 0.6), 0.9, 0.6), KIN, g)
        M.add('foot_' + side, lambda q: np.maximum(sd_roundcone(q, (0, -0.4, -0.6), (0, 2.0, -0.8), 0.9, 0.6),
                                                   -1.2 - q[:, 2]), KIN, g)
    # a small round body, ribs showing
    M.add('pelvis', lambda q: sd_ellipsoid(q - np.array([0, 0, 0.4]), (2.4, 1.9, 2.0)), KIN, 'body')
    M.add('chest', lambda q: sd_roundcone(q / np.array([1, 0.8, 1]), (0, 0, 0.5), (0, 0, 6.0), 2.2, 3.0) * 0.8, KIN,
          'body', None, fp.tex_cloth(0.1, 2.5))
    M.add('chest', lambda q: sd_roundcone(q, (0, 0.2, 5.0), (0, 0.4, 9.0), 1.2, 1.0), KIN, 'body')
    # the head, far too big, a bulb with lights inside it
    M.add('head', lambda q: sd_ellipsoid(q - np.array([0, 0.6, 5.2]), (4.6, 5.0, 5.8)), KIN, 'head',
          spots(KIN, CYAN, 1.1, 0.66, 2.0))
    M.add('head', lambda q: sd_ellipsoid(q - np.array([0, -0.8, 9.0]), (3.8, 4.0, 3.4)), KIN, 'head',
          spots(KIN, CYAN, 1.0, 0.64, 5.0))
    for side in ('r', 'l'):
        g = 'arm' + side
        M.add('uarm_' + side, lambda q: sd_roundcone(q, (0, 0, 0), (0, 0, -KP['uarm']), 1.0, 0.8), KIN, g)
        M.add('farm_' + side, lambda q: sd_roundcone(q, (0, 0, 0), (0, 0, -KP['farm']), 0.8, 0.6), KIN, g)
        M.add('hand_' + side, lambda q: sd_ellipsoid(q - np.array([0, 0.3, -1.2]), (0.9, 1.3, 1.6)), KIN, g)
    return M


kin_skel = standing(KP, [('head', (0, 4.0, 5.0)), ('head', (0, -3.0, 6.0))])


def kin_build(p):
    S = kin_skel(p)
    return S, kin_model(S, p)


KIN_STAND = pose(POSE, lean=14, head_pitch=-6, th_r=0, knee_r=12, th_l=4, knee_l=12, spread=10, sw_r=10, abd_r=24,
                 el_r=30, sw_l=12, abd_l=24, el_l=34)
kin = Creature('kin', 'Celestial child', kin_build, points=[('hand_r', (0, 0.3, -2.0)), ('head', (0, 0.6, 9))],
               module=__name__)
kin.anim('idle', keyed(KIN_STAND, [(0, {}), (1, dict(breath=1, lean=17, head_roll=8))], [0.0, 1.0]), [24, 24],
         loop=True)
kin.anim('walk', [walk_cycle(dict(KIN_STAND, roll=0.0), k, 8, stride=20, arms=0.6, lean=14, bounce=0.8)
                  for k in range(8)], [6] * 8, loop=True)
# a waddle forward, both arms raised, slapped down
kin.anim('attack', keyed(KIN_STAND, [
    (0.0, {}),
    (0.34, dict(lean=-4, head_pitch=-14, sw_r=150, abd_r=40, el_r=50, sw_l=150, abd_l=40, el_l=50, th_l=14,
                knee_l=14)),
    (0.52, dict(ty=3, lean=34, head_pitch=10, sw_r=70, abd_r=16, el_r=10, sw_l=70, abd_l=16, el_l=10, th_l=24,
                knee_l=30, th_r=-12, knee_r=14)),
    (0.70, dict(ty=3, lean=30, sw_r=50, el_r=20, sw_l=50, el_l=20, th_l=24, knee_l=30)),
    (1.0, {})], [0.0, 0.16, 0.32, 0.42, 0.52, 0.62, 0.78, 0.90]), [4, 4, 5, 3, 2, 4, 5, 5], hit=4)
kin.anim('hurt', keyed(KIN_STAND, HURT, [0.14, 0.32, 0.62]), [4, 5, 6])
kin.anim('death', keyed(KIN_STAND, DEATH_BACK, [0.04, 0.12, 0.22, 0.34, 0.46, 0.58, 0.70, 0.84, 1.0]),
         [4, 4, 4, 4, 4, 4, 5, 6, 60])


# ================================================================ the brainsucker

BS = dict(HUMAN, thigh=13.4, shin=12.6, chest=12.0, neck=14.6, shoulder=5.8, uarm=11.0, farm=10.0)
TENT = 8.0


def bs_model(S, p):
    M = Model(S)
    rt = fp.tex_cloth(0.06, 1.4, 31.0)
    fp.legs(M, BS, BLACK, BLACK, k=0.85)
    hip = BS['thigh'] + BS['shin'] + BS['foot']
    fp.long_skirt(M, VIOLET, length=hip - 0.6, top_r=4.0, hem_r=7.6, tex=rt)
    fp.torso(M, BS, VIOLET, k=0.9, tex=rt)
    fp.capelet(M, VIOLET, length=11.0, w=8.4, tex=rt)
    fp.hood(M, VIOLET, peak=0.6, tex=rt)
    # in the hood: a bulb of a head, pale, and tentacles where the mouth would be
    M.add('head', lambda q: sd_ellipsoid(q - np.array([0, 1.0, 5.6]), (2.8, 3.2, 3.6)), PALE, 'face',
          spots(PALE, FLESH, 1.2, 0.85, 7.0))
    for i, (tx, tz) in enumerate(((-1.2, 0.0), (0.0, -0.4), (1.2, 0.0), (-0.6, 0.6), (0.6, 0.6))):
        def tent(q, tx=tx, tz=tz, i=i):
            a = np.array([tx, 0.5, tz])
            b = np.array([tx * 1.6, 2.6 + 0.4 * i % 2, tz - TENT])
            w = 0.35 * np.sin(q[:, 2] * 0.8 + i)
            qq = q + np.stack([w, w * 0.5, np.zeros(len(q))], 1)
            return sd_roundcone(qq, a, b, 0.6, 0.25)
        M.add('jaw', tent, PALE, 'face')
    fp.arms(M, BS, VIOLET, PALE, k=0.8, kh=0.9)
    # long grey fingers
    for side in ('r', 'l'):
        for fx in (-0.6, 0.0, 0.6):
            M.add('hand_' + side, lambda q, fx=fx: sd_roundcone(q, (fx, 0.4, -2.0), (fx * 1.4, 0.8, -4.6), 0.3, 0.18),
                  PALE, 'arm' + side)
    M.decals = [(S.pt('head', (1.0, 3.9, 6.4)), fp.EYE_RED_GLOW), (S.pt('head', (-1.0, 3.9, 6.4)), fp.EYE_RED_GLOW)]
    return M


bs_skel = standing(BS)


def bs_build(p):
    S = bs_skel(p)
    return S, bs_model(S, p)


BS_STAND = pose(POSE, lean=8, head_pitch=-10, th_r=0, knee_r=8, th_l=4, knee_l=8, spread=5, sw_r=14, abd_r=16,
                el_r=60, sw_l=14, abd_l=16, el_l=60, jaw=-10)
brain = Creature('brainsucker', 'Brainsucker', bs_build, points=[('jaw', (0, 3.0, -TENT)), ('head', (0, 2, 6))],
                 extent=[('jaw', (0, 3.0, -TENT))], module=__name__)
brain.anim('idle', keyed(BS_STAND, [(0, {}), (1, dict(breath=1, lean=9.5, jaw=-2))], [0.0, 1.0]), [26, 26],
           loop=True)
brain.anim('walk', [walk_cycle(BS_STAND, k, 8, stride=20, arms=0.3, lean=9, bounce=0.6) for k in range(8)],
           [7] * 8, loop=True)
# both hands seize, the head lunges, the tentacles lash forward
brain.anim('attack', keyed(BS_STAND, [
    (0.0, {}),
    (0.32, dict(lean=0, head_pitch=-20, jaw=-30, sw_r=90, abd_r=30, el_r=40, sw_l=90, abd_l=30, el_l=40,
                th_l=10, knee_l=10)),
    (0.50, dict(ty=4, lean=26, head_pitch=4, jaw=70, sw_r=70, abd_r=10, el_r=60, sw_l=70, abd_l=10, el_l=60,
                th_l=26, knee_l=26, th_r=-16, knee_r=12)),
    (0.66, dict(ty=4, lean=26, head_pitch=6, jaw=56, sw_r=68, abd_r=10, el_r=70, sw_l=68, abd_l=10, el_l=70,
                th_l=26, knee_l=26, th_r=-16)),
    (1.0, {})], [0.0, 0.16, 0.30, 0.42, 0.52, 0.62, 0.78, 0.90]), [4, 5, 6, 3, 3, 5, 6, 6], hit=4)
brain.anim('hurt', keyed(BS_STAND, [(0.0, {}), (0.22, dict(HURT[1][1], jaw=20)), (0.5, dict(HURT[2][1], jaw=0)),
                                    (1.0, {})], [0.14, 0.32, 0.62]), [5, 6, 6])
brain.anim('death', keyed(BS_STAND, DEATH, [0.04, 0.12, 0.22, 0.34, 0.46, 0.58, 0.72, 0.86, 1.0]),
           [4, 4, 4, 5, 5, 5, 5, 6, 60])


# ================================================================ the lantern

LA = dict(HUMAN, thigh=12.0, shin=11.6, chest=11.0, neck=12.0, shoulder=5.4, uarm=11.0, farm=10.6)


def la_model(S, p):
    M = Model(S)
    gt = fp.tex_cloth(0.07, 1.6, 41.0)
    fp.legs(M, LA, PALE, PALE, k=0.75, boots=False)
    hip = LA['thigh'] + LA['shin'] + LA['foot']

    def gown(q):
        qq = q / np.array([1.0, 0.85, 1.0])
        d = sd_cone(qq, -hip + 0.4, 1.0, 8.0, 3.8) * 0.85
        ang = np.arctan2(q[:, 1], q[:, 0])
        hem = -hip + 2.6 + jag(ang, 14, 2.6, seed=3.3)
        return np.maximum(d, hem - q[:, 2])
    M.add('pelvis', gown, LINEN, 'coat', None, gt)
    fp.torso(M, LA, LINEN, k=0.8, tex=gt)
    fp.capelet(M, LINEN, length=7.0, w=7.0, k=0.85, tex=gt)
    # where the head was: a cluster of pale bulbs, lit from inside
    bulbs = [((0, 0.6, 4.6), 2.8), ((1.9, 0.2, 6.6), 2.2), ((-1.8, 0.4, 6.8), 2.3), ((0.3, -1.3, 8.0), 2.4),
             ((0.6, 1.8, 7.6), 1.8), ((-1.0, -0.9, 3.4), 1.9), ((2.0, -1.2, 4.6), 1.6)]

    def head(q):
        d = np.full(len(q), 1e9)
        for c, r in bulbs:
            d = np.minimum(d, np.sqrt(((q - np.array(c)) ** 2).sum(1)) - r)
        return d
    M.add('head', head, PALE, 'head', spots(PALE, EYE, 1.2, 0.62, 9.0))
    fp.arms(M, LA, LINEN, PALE, k=0.7, kh=0.9)
    for side in ('r', 'l'):
        for fx in (-0.6, 0.0, 0.6):
            M.add('hand_' + side, lambda q, fx=fx: sd_roundcone(q, (fx, 0.4, -2.0), (fx * 1.5, 0.9, -5.0), 0.28, 0.15),
                  PALE, 'arm' + side)
    return M


la_skel = standing(LA)


def la_build(p):
    S = la_skel(p)
    return S, la_model(S, p)


LA_STAND = pose(POSE, lean=6, head_pitch=6, head_roll=8, th_r=0, knee_r=6, th_l=2, knee_l=6, spread=3, sw_r=6,
                abd_r=10, el_r=10, sw_l=6, abd_l=10, el_l=12)
lantern = Creature('lantern', 'Lantern', la_build, points=[('hand_r', (0, 0.6, -4.0)), ('head', (0, 0.4, 6.0))],
                   module=__name__)
lantern.anim('idle', keyed(LA_STAND, [(0, {}), (1, dict(breath=1, head_roll=-6, sw_r=10, sw_l=2))], [0.0, 1.0]),
             [30, 30], loop=True)
lantern.anim('walk', [walk_cycle(LA_STAND, k, 8, stride=14, arms=0.2, lean=6, bounce=0.4) for k in range(8)],
             [8] * 8, loop=True)
# the long arms rise slowly, then snatch
lantern.anim('attack', keyed(LA_STAND, [
    (0.0, {}),
    (0.36, dict(lean=-4, head_pitch=-6, sw_r=120, abd_r=40, el_r=30, sw_l=120, abd_l=40, el_l=30)),
    (0.52, dict(ty=3, lean=20, head_pitch=10, sw_r=90, abd_r=8, el_r=4, sw_l=90, abd_l=8, el_l=4, th_l=16,
                knee_l=14)),
    (0.68, dict(ty=3, lean=22, sw_r=70, abd_r=4, el_r=40, sw_l=70, abd_l=4, el_l=40, th_l=16, knee_l=14)),
    (1.0, {})], [0.0, 0.16, 0.32, 0.42, 0.52, 0.62, 0.78, 0.90]), [5, 6, 7, 4, 3, 5, 6, 6], hit=4)
lantern.anim('hurt', keyed(LA_STAND, HURT, [0.14, 0.32, 0.62]), [5, 6, 6])
lantern.anim('death', keyed(LA_STAND, DEATH, [0.04, 0.12, 0.22, 0.34, 0.46, 0.58, 0.72, 0.86, 1.0]),
             [4, 4, 4, 5, 5, 5, 5, 6, 60])


# ================================================================ the spider with a human face

# eight legs at these angles from ahead, on each side
LEG_ANGLES = (38, 70, 104, 140)
SP = dict(H=7.0, F=10.0, T=13.0, R=2.6)
SPOSE = dict(ty=0.0, air=0.0, pitch=0.0, roll=0.0, yaw=0.0, abd=10.0, torso=40.0, head_pitch=-20.0, head_yaw=0.0,
             breath=0.0, **{'%s%d_%s' % (k, i, s): v for i in range(4) for s in 'rl'
                            for k, v in (('lift', 48.0), ('sw', 0.0), ('knee', 110.0))})


def spider(p, lift=0.0):
    a = lambda k: D(p[k])
    S = Skel()
    S.add('body', 'root', [0, p['ty'], lift + SP['H']], rz(a('yaw')) @ rx(a('pitch')) @ ry(a('roll')))
    S.add('abdomen', 'body', [0, -2.4, 1.0], rx(a('abd')))
    S.add('torso', 'body', [0, 3.0, 0.8], rx(-a('torso')))
    S.add('head', 'torso', [0, 0, 6.0 + 0.3 * p['breath']], rz(a('head_yaw')) @ rx(a('head_pitch') + a('torso')))
    for i, ang in enumerate(LEG_ANGLES):
        for s, sg in (('r', 1), ('l', -1)):
            k = '%d_%s' % (i, s)
            angle = ang - p['sw' + k]
            theta = 90 - angle if sg > 0 else 90 + angle
            base = [sg * SP['R'] * math.sin(D(angle)), SP['R'] * math.cos(D(angle)) * 0.8, 0]
            S.add('fem' + k, 'body', base, rz(D(theta)) @ ry(-a('lift' + k)))
            S.add('tib' + k, 'fem' + k, [SP['F'], 0, 0], ry(a('knee' + k)))
    return S


def spider_contacts():
    c = [('tib%d_%s' % (i, s), (SP['T'], 0, 0)) for i in range(4) for s in 'rl']
    return c + [('body', (0, 0, -2.2)), ('abdomen', (0, -4, -3.6)), ('head', (0, 1.5, 3.0)), ('torso', (0, 1, 3))]


def sp_model(S, p):
    M = Model(S)
    ct = fp.tex_cloth(0.08, 1.4, 51.0)
    M.add('body', lambda q: sd_ellipsoid(q, (3.2, 3.6, 2.4)), BLACK, 'body', None, ct)
    M.add('abdomen', lambda q: sd_ellipsoid(q - np.array([0, -4.2, 0.6]), (4.6, 6.0, 4.2)), PALE, 'body',
          spots(PALE, FLESH, 0.9, 0.80, 3.0))
    # a pale naked torso rising at the front, and a small human head
    M.add('torso', lambda q: sd_roundcone(q / np.array([1, 0.8, 1]), (0, 0, 0), (0, 0, 5.4), 2.2, 2.9) * 0.8, PALE,
          'torso')
    for sg in (1, -1):
        M.add('torso', lambda q, sg=sg: sd_roundcone(q, (sg * 2.4, 0, 5.0), (sg * 3.4, 2.4, 1.6), 0.8, 0.5), PALE,
              'torso')
    M.add('head', lambda q: sd_ellipsoid(q - np.array([0, 0.3, 2.0]), (2.0, 2.2, 2.6)), PALE, 'head')
    M.add('head', lambda q: np.maximum(sd_ellipsoid(q - np.array([0, -0.3, 2.8]), (2.2, 2.3, 2.4)), 1.6 - q[:, 2]),
          BLACK, 'head')
    for i in range(4):
        for s in 'rl':
            k = '%d_%s' % (i, s)
            M.add('fem' + k, lambda q: sd_roundcone(q, (0, 0, 0), (SP['F'], 0, 0), 0.9, 0.6), BLACK, 'leg' + k)
            M.add('tib' + k, lambda q: sd_roundcone(q, (0, 0, 0), (SP['T'], 0, 0), 0.6, 0.15), BLACK, 'leg' + k)
            M.add('tib' + k, lambda q: sd_ellipsoid(q, (0.9, 0.8, 0.8)), PALE, 'leg' + k)
    M.decals = [(S.pt('head', (0.7, 2.45, 2.3)), fp.EYE_RED_GLOW), (S.pt('head', (-0.7, 2.45, 2.3)), fp.EYE_RED_GLOW)]
    return M


sp_cont = spider_contacts()


def sp_build(p):
    S = grounded(spider, p, sp_cont)
    return S, sp_model(S, p)


def spose(base=None, **kw):
    p = dict(base or SPOSE)
    p.update(kw)
    return floats(p)


def skeyed(base, keys, times):
    kp = [(t, spose(base, **floats(kw))) for t, kw in keys]
    return [sample(kp, t) for t in times]


def legs_all(**kw):
    """the same change on every leg: lift=.., knee=.., sw=.."""
    return {'%s%d_%s' % (k, i, s): v for k, v in kw.items() for i in range(4) for s in 'rl'}


def crawl(base, k, n, A=12.0):
    """legs in two sets of four, moving in turn"""
    p = dict(base)
    ph = 2 * math.pi * k / n
    for i in range(4):
        for s, sg in (('r', 1), ('l', -1)):
            off = 0.0 if (i % 2 == 0) == (sg > 0) else math.pi
            c, si = math.cos(ph + off), math.sin(ph + off)
            key = '%d_%s' % (i, s)
            p['sw' + key] = base['sw' + key] + A * c
            p['lift' + key] = base['lift' + key] + 18 * max(0.0, -si)
            p['knee' + key] = base['knee' + key] - 10 * max(0.0, -si)
    p['air'] = 0.4 * abs(math.sin(2 * ph))
    p['torso'] = base['torso'] + 3 * math.sin(2 * ph)
    return p


SP_STAND = spose()
spider_ = Creature('spider', 'Nightmare apostle', sp_build, points=[('tib0_r', (SP['T'], 0, 0)), ('head', (0, 1, 2))],
                   extent=[('tib%d_%s' % (i, s), (SP['T'], 0, 0)) for i in range(4) for s in 'rl'],
                   scale=1.5, module=__name__)
spider_.anim('idle', skeyed(SP_STAND, [(0, {}), (1, dict(breath=1.0, torso=46, head_pitch=-24))], [0.0, 1.0]),
             [26, 26], loop=True)
spider_.anim('walk', [crawl(SP_STAND, k, 8) for k in range(8)], [5] * 8, loop=True)
# up on the back legs, the two front legs raised, stabbing down
SP_UP = dict(pitch=24, torso=10, head_pitch=-30, lift0_r=96, lift0_l=96, knee0_r=40, knee0_l=40, sw0_r=-10,
             sw0_l=-10, lift1_r=70, lift1_l=70, knee1_r=80, knee1_l=80)
spider_.anim('attack', skeyed(SP_STAND, [
    (0.0, {}),
    (0.34, SP_UP),
    (0.50, dict(ty=3, pitch=-6, torso=50, head_pitch=-10, lift0_r=10, lift0_l=10, knee0_r=60, knee0_l=60, sw0_r=-14,
                sw0_l=-14)),
    (0.66, dict(ty=3, pitch=-6, torso=50, lift0_r=6, lift0_l=6, knee0_r=70, knee0_l=70, sw0_r=-14, sw0_l=-14)),
    (1.0, {})], [0.0, 0.16, 0.32, 0.42, 0.52, 0.62, 0.78, 0.90]), [4, 5, 6, 3, 2, 4, 6, 6], hit=4)
spider_.anim('hurt', skeyed(SP_STAND, [(0.0, {}), (0.25, dict(pitch=12, roll=8, ty=-2, torso=70, head_pitch=10,
                                                               **legs_all(lift=60.0, knee=90.0))),
                                       (0.6, dict(pitch=4, torso=56)), (1.0, {})], [0.14, 0.34, 0.64]), [4, 5, 6])
SP_DEAD = dict(roll=180, air=0, torso=10, head_pitch=20, abd=-10, **legs_all(lift=80.0, knee=140.0))
spider_.anim('death', skeyed(SP_STAND, [
    (0.0, {}),
    (0.16, dict(pitch=20, torso=70, head_pitch=20, **legs_all(lift=70.0, knee=80.0))),
    (0.36, dict(roll=60, air=4, torso=50, **legs_all(lift=80.0, knee=110.0))),
    (0.56, dict(roll=140, air=4, torso=30, **legs_all(lift=90.0, knee=130.0))),
    (0.76, dict(SP_DEAD, air=1.0)),
    (1.0, SP_DEAD)], [0.04, 0.14, 0.26, 0.38, 0.50, 0.62, 0.74, 0.86, 1.0]), [4, 4, 4, 4, 4, 5, 5, 6, 60])


# ================================================================ the Watcher (boss)

WA = dict(HUMAN, thigh=15.0, shin=14.0, waist=5.0, chest=14.0, neck=16.6, shoulder=6.6, hipw=3.2, uarm=13.0,
          farm=13.0, hand=1.6, cape=17.0, skirt=5.0, foot=1.6)
# the second and third pairs of arms, lower on the chest
EXTRA = (('2', 10.2, 5.8), ('3', 6.6, 5.0))
WPOSE = dict(POSE, **{'%s_%s%s' % (k, s, n): v for n in '23' for s in 'rl'
                      for k, v in (('sw', 10.0), ('abd', 30.0), ('yaw', 0.0), ('el', 30.0), ('wr', 0.0))})


def watcher_skel(p, lift=0.0):
    S = humanoid(p, WA, lift)
    a = lambda k: D(p[k])
    for n, z, w in EXTRA:
        for side, sg in (('r', 1), ('l', -1)):
            k = side + n
            S.add('uarm_' + k, 'chest', [sg * w, -0.2, z],
                  rz(sg * a('yaw_' + k)) @ ry(-sg * a('abd_' + k)) @ rx(a('sw_' + k) + 0.5 * a('lean')))
            S.add('farm_' + k, 'uarm_' + k, [0, 0, -WA['uarm'] * 0.85], rx(a('el_' + k)) @ ry(sg * D(6)))
            S.add('hand_' + k, 'farm_' + k, [0, 0, -WA['farm'] * 0.85], rx(a('wr_' + k)))
    return S


def wa_model(S, p):
    M = Model(S)
    st = fp.tex_cloth(0.07, 1.2, 61.0)
    for side in ('r', 'l'):
        g = 'leg' + side
        M.add('thigh_' + side, lambda q: sd_roundcone(q, (0, 0, 0), (0, 0, -WA['thigh']), 2.0, 1.3), ASH, g, None, st)
        M.add('shin_' + side, lambda q: sd_roundcone(q, (0, 0, 0), (0, 0, -WA['shin'] + 0.6), 1.3, 0.8), ASH, g, None,
              st)
        M.add('foot_' + side, lambda q: np.maximum(sd_roundcone(q, (0, -0.6, -0.4), (0, 3.4, -0.9), 1.1, 0.5),
                                                   -1.6 - q[:, 2]), ASH, g)
    # a thin body, every rib showing, a hanging rag
    M.add('pelvis', lambda q: sd_ellipsoid(q - np.array([0, 0, 0.5]), (3.4, 2.4, 2.6)), ASH, 'body', None, st)

    def ribs(q):
        return st(q) - 0.18 * (np.sin(q[:, 2] * 2.4) > 0.3) * (q[:, 1] > 0.5)
    M.add('chest', lambda q: sd_roundcone(q / np.array([1, 0.72, 1]), (0, 0, 0.0), (0, 0, 12.0), 2.6, 4.6) * 0.72, ASH,
          'body', None, ribs)
    M.add('pelvis', lambda q: np.maximum(sd_cone(q / np.array([1, 0.8, 1]), -12.0, 1.0, 4.6, 3.6) * 0.8,
                                         -12.0 + jag(np.arctan2(q[:, 1], q[:, 0]), 9, 2.4, 1.1) - q[:, 2]), BLACK,
          'coat', None, st)
    # the head: a long skull crowded with eyes, a crown of crooked fingers
    M.add('head', lambda q: sd_ellipsoid(q - np.array([0, 0.6, 5.6]), (3.3, 3.6, 5.6)), ASH, 'head',
          spots(ASH, fp.EYE, 1.3, 0.60, 13.0))
    for i in range(7):
        ang = -1.3 + i * 2.6 / 6
        x, y = 2.6 * math.sin(ang), 1.6 * math.cos(ang) - 0.6

        def finger(q, x=x, y=y, i=i):
            return sd_roundcone(q, (x, y, 9.0), (x * 1.8, y * 1.4 - 1.2, 14.0 + (i % 3)), 0.8, 0.25)
        M.add('head', finger, ASH, 'head')
    M.add('head', lambda q: sd_roundcone(q, (0, 0.0, 0.0), (0, 0.4, 3.0), 1.6, 1.8), ASH, 'head')
    # six long arms, hands with hooked fingers
    arms = [('r', 'l')] + [('r' + n, 'l' + n) for n, _, _ in EXTRA]
    for j, pair in enumerate(arms):
        kk = 1.0 if j == 0 else 0.8
        for k in pair:
            g = 'arm' + k
            L1 = WA['uarm'] * (1.0 if j == 0 else 0.85)
            L2 = WA['farm'] * (1.0 if j == 0 else 0.85)
            M.add('uarm_' + k, lambda q, L1=L1, kk=kk: sd_roundcone(q, (0, 0, 0), (0, 0, -L1), 1.6 * kk, 1.1 * kk), ASH,
                  g, None, st)
            M.add('farm_' + k, lambda q, L2=L2, kk=kk: sd_roundcone(q, (0, 0, 0), (0, 0, -L2), 1.1 * kk, 0.8 * kk), ASH,
                  g, None, st)
            M.add('hand_' + k, lambda q, kk=kk: sd_ellipsoid(q - np.array([0, 0.3, -1.4 * kk]), (1.4 * kk, 1.8 * kk,
                                                                                              2.0 * kk)), ASH, g)
            for fx in (-0.8, 0.0, 0.8):
                M.add('hand_' + k, lambda q, fx=fx, kk=kk: sd_roundcone(q, (fx * kk, 0.6, -2.6 * kk),
                                                                        (fx * 1.3 * kk, 1.8, -5.2 * kk), 0.4 * kk,
                                                                        0.15), ASH, g)
    return M


wa_cont = human_contacts(WA)


def wa_build(p):
    S = grounded(watcher_skel, p, wa_cont)
    return S, wa_model(S, p)


def wkeyed(base, keys, times):
    return keyed(base, keys, times, skel=watcher_skel)


WA_STAND = pose(WPOSE, lean=14, head_pitch=-16, th_r=-2, knee_r=16, th_l=6, knee_l=18, spread=8, sw_r=20, abd_r=24,
                el_r=40, sw_l=24, abd_l=26, el_l=44, sw_r2=10, abd_r2=40, el_r2=50, sw_l2=14, abd_l2=40, el_l2=54,
                sw_r3=-10, abd_r3=30, el_r3=30, sw_l3=-6, abd_l3=30, el_l3=34)


def arms_all(**kw):
    """the same change on all six arms: sw=.., el=.., abd=.."""
    out = {}
    for k, v in kw.items():
        for side in ('r', 'l', 'r2', 'l2', 'r3', 'l3'):
            out['%s_%s' % (k, side)] = v
    return out


watcher = Creature('watcher', 'The Watcher', wa_build,
                   points=[('hand_r', (0, 1.0, -4.0)), ('head', (0, 3.6, 6.0))],
                   scale=1.5, boss=True, module=__name__)
watcher.anim('idle', wkeyed(WA_STAND, [(0, {}), (1, dict(breath=1.4, lean=16, head_roll=6, el_r2=60, el_l3=44))],
                            [0.0, 1.0]), [34, 34], loop=True)
watcher.anim('walk', [walk_cycle(WA_STAND, k, 8, stride=22, arms=0.6, lean=15, bounce=0.6) for k in range(8)],
             [9] * 8, loop=True)
WA_UP = dict(lean=2, head_pitch=-24, chest_yaw=-14, sw_r=170, abd_r=26, el_r=60, sw_r2=60, abd_r2=60, el_r2=60,
             th_r=-10, knee_r=18, th_l=14, knee_l=18)
WA_SLAM = dict(lean=30, head_pitch=-4, chest_yaw=10, sw_r=60, abd_r=10, el_r=6, th_l=26, knee_l=34, th_r=-18,
               knee_r=18, sw_r2=40, el_r2=40)
watcher.anim('attack', wkeyed(WA_STAND, [
    (0.0, {}), (0.36, WA_UP), (0.52, WA_SLAM), (0.70, dict(WA_SLAM, sw_r=50, el_r=10)), (1.0, {})],
    [0.0, 0.16, 0.30, 0.40, 0.48, 0.56, 0.72, 0.88]), [5, 6, 7, 4, 2, 5, 7, 7], hit=4)
watcher.anim('hurt', wkeyed(WA_STAND, [(0.0, {}), (0.22, dict(HURT[1][1], **arms_all(abd=60.0))),
                                       (0.5, dict(HURT[2][1], **arms_all(abd=44.0))), (1.0, {})],
                            [0.14, 0.32, 0.62]), [6, 7, 7])
watcher.anim('death', wkeyed(WA_STAND, [
    (0.0, {}),
    (0.12, dict(lean=-18, head_pitch=-30, pitch=6, **arms_all(sw=60.0, abd=70.0, el=30.0))),
    (0.34, dict(pitch=40, air=6, lean=-10, head_pitch=-14, th_r=30, knee_r=40, th_l=24, knee_l=44,
                **arms_all(sw=120.0, abd=60.0, el=40.0))),
    (0.54, dict(pitch=74, air=3, lean=-6, th_r=20, knee_r=30, th_l=14, knee_l=30, **arms_all(sw=60.0, abd=80.0))),
    (0.72, dict(pitch=88, head_pitch=-10, th_r=12, knee_r=26, th_l=4, knee_l=18, spread=9,
                **arms_all(sw=0.0, abd=80.0, el=30.0))),
    (0.86, dict(pitch=84, air=1.5, head_pitch=-6, th_r=12, knee_r=30, th_l=4, knee_l=22, spread=9,
                **arms_all(sw=0.0, abd=84.0, el=50.0))),
    (1.0, dict(pitch=88, head_pitch=-6, head_yaw=30, th_r=12, knee_r=26, th_l=4, knee_l=18, spread=9,
               **arms_all(sw=-4.0, abd=88.0, el=60.0)))],
    [0.03, 0.10, 0.18, 0.28, 0.40, 0.52, 0.64, 0.76, 0.88, 1.0]), [5, 5, 6, 6, 7, 8, 6, 5, 6, 90])
# special 1, the gaze: the head thrown back, then every eye at once on the prey
watcher.anim('beam', wkeyed(WA_STAND, [
    (0.0, {}),
    (0.28, dict(lean=-6, head_pitch=-50, **arms_all(sw=40.0, abd=80.0, el=60.0))),
    (0.44, dict(lean=22, head_pitch=6, **arms_all(sw=20.0, abd=90.0, el=20.0))),
    (0.80, dict(lean=24, head_pitch=8, head_roll=6, **arms_all(sw=16.0, abd=92.0, el=24.0))),
    (1.0, {})], [0.0, 0.12, 0.24, 0.34, 0.44, 0.54, 0.64, 0.74, 0.84, 0.94]), [5, 6, 6, 6, 4, 10, 10, 10, 6, 6],
    beam=4)
# special 2, the grasp: all six hands high, then down on the stones at once
watcher.anim('grasp', wkeyed(WA_STAND, [
    (0.0, {}),
    (0.36, dict(lean=-4, head_pitch=-30, th_r=-10, knee_r=20, th_l=14, knee_l=20, **arms_all(sw=170.0, abd=30.0,
                                                                                            el=40.0))),
    (0.54, dict(lean=40, head_pitch=4, th_l=30, knee_l=50, th_r=-20, knee_r=40, **arms_all(sw=60.0, abd=24.0,
                                                                                          el=4.0))),
    (0.76, dict(lean=42, head_pitch=4, th_l=30, knee_l=52, th_r=-20, knee_r=42, **arms_all(sw=50.0, abd=26.0,
                                                                                          el=6.0))),
    (1.0, {})], [0.0, 0.12, 0.24, 0.36, 0.46, 0.54, 0.62, 0.74, 0.86, 0.95]), [5, 6, 6, 8, 4, 3, 10, 7, 7, 7],
    slam=5)
# combo 1: the right arms sweep across, then the left arms back
watcher.anim('combo1', wkeyed(WA_STAND, [
    (0.0, {}),
    (0.14, dict(chest_yaw=40, pelvis_yaw=12, sw_r=90, abd_r=80, yaw_r=40, el_r=20, sw_r2=80, abd_r2=80, yaw_r2=40,
                sw_r3=70, abd_r3=80)),
    (0.28, dict(chest_yaw=-30, pelvis_yaw=-10, lean=20, sw_r=80, abd_r=20, yaw_r=-60, el_r=10, sw_r2=70, abd_r2=20,
                yaw_r2=-50, sw_r3=60, abd_r3=20, yaw_r3=-40)),
    (0.42, dict(chest_yaw=-36, sw_r=40, abd_r=40, yaw_r=-60)),
    (0.56, dict(chest_yaw=-40, pelvis_yaw=-12, sw_l=90, abd_l=80, yaw_l=40, el_l=20, sw_l2=80, abd_l2=80,
                yaw_l2=40, sw_l3=70, abd_l3=80)),
    (0.70, dict(chest_yaw=30, pelvis_yaw=10, lean=20, sw_l=80, abd_l=20, yaw_l=-60, el_l=10, sw_l2=70, abd_l2=20,
                yaw_l2=-50, sw_l3=60, abd_l3=20, yaw_l3=-40)),
    (0.84, dict(chest_yaw=30, sw_l=40, abd_l=40, yaw_l=-60)),
    (1.0, {})], [0.0, 0.10, 0.18, 0.26, 0.34, 0.46, 0.56, 0.64, 0.72, 0.84, 0.94]),
    [5, 5, 3, 3, 5, 6, 5, 3, 3, 7, 7], hit=[3, 8])
# combo 2: the upper hands stab ahead, then the lowest pair sweeps up
watcher.anim('combo2', wkeyed(WA_STAND, [
    (0.0, {}),
    (0.16, dict(lean=4, sw_r=40, el_r=110, sw_l=40, el_l=110, abd_r=20, abd_l=20)),
    (0.28, dict(ty=3, lean=24, sw_r=88, el_r=4, sw_l=88, el_l=4, abd_r=8, abd_l=8, th_l=20, knee_l=24)),
    (0.42, dict(ty=3, lean=20, sw_r=70, el_r=30, sw_l=70, el_l=30)),
    (0.58, dict(ty=3, lean=30, sw_r3=-40, el_r3=60, sw_l3=-40, el_l3=60, sw_r=40, sw_l=40)),
    (0.70, dict(ty=5, lean=4, head_pitch=-20, sw_r3=150, el_r3=10, sw_l3=150, el_l3=10, abd_r3=20, abd_l3=20,
                sw_r=50, sw_l=50, th_l=10)),
    (0.84, dict(ty=5, lean=8, sw_r3=130, el_r3=30, sw_l3=130, el_l3=30)),
    (1.0, {})], [0.0, 0.10, 0.18, 0.26, 0.34, 0.46, 0.56, 0.64, 0.72, 0.84, 0.94]),
    [5, 5, 3, 4, 5, 5, 5, 3, 3, 7, 7], hit=[3, 8])
