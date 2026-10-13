"""The beasts of the hunt (Bloodborne fan art): a rabid dog, a scourge beast
on long arms, and their boss, the Great Hound. They stand on a four-legged
skeleton (quad): a spine from the hips to the chest, a neck, a head with a
jaw, a tail, and legs that bend like a dog's (the hind ones at the hock).
"""
import math

import numpy as np

import foeparts as fp
from foeparts import CLAW, FLESH, FUR_BROWN, FUR_DARK, FUR_PALE, NOSE, TEETH, shaggy
from rig import Creature, floats, grounded, sample
from sdf import D, Model, Skel, hash2, rx, ry, rz, sd_ellipsoid, sd_roundcone

# the joints, degrees: the body (pitch + lifts the front), arch bends the
# spine, twist turns the chest on the hips; neck lifts the neck, head pitches
# the head on it; the legs: fs / hs swing forward, fk folds the front leg at
# the wrist, hk / hh fold the hind leg at the knee and the hock, fa / ha
# spread it sideways
QPOSE = dict(ty=0.0, air=0.0, pitch=0.0, roll=0.0, yaw=0.0, arch=0.0, twist=0.0, neck=34.0, neck_yaw=0.0,
             head=-34.0, head_yaw=0.0, head_roll=0.0, jaw=0.0, tail=20.0, tail_yaw=0.0, tail_curl=10.0,
             breath=0.0, fs_r=0.0, fs_l=0.0, fk_r=0.0, fk_l=0.0, fp_r=0.0, fp_l=0.0, fa_r=0.0, fa_l=0.0,
             hs_r=0.0, hs_l=0.0, hk_r=0.0, hk_l=0.0, hh_r=0.0, hh_l=0.0, ha_r=0.0, ha_l=0.0)

EYE_GLOW = (248, 120, 80)         # the eyes: one pixel each, of the glowing red ramp
DOG = dict(L=12.5, H=14.0, neck_y=3.6, neck_z=2.0, NL=5.0, wf=2.3, wh=2.2, FU=6.4, FL=6.6, FP=1.4,
           HU=6.6, HL=6.4, HM=4.2, TL=5.0)


def qpose(base=None, **kw):
    p = dict(base or QPOSE)
    p.update(kw)
    return floats(p)


def quad(p, pr, lift=0.0):
    a = lambda k: D(p[k])
    S = Skel()
    S.add('body', 'root', [0, p['ty'], lift], rz(a('yaw')) @ rx(a('pitch')) @ ry(a('roll')))
    S.add('hips', 'body', [0, -pr['L'] / 2, pr['H']], rz(-a('twist') / 2) @ rx(-a('arch') / 2))
    S.add('chest', 'hips', [0, pr['L'], 0.3 * p['breath']], rz(a('twist')) @ rx(a('arch')))
    S.add('neck', 'chest', [0, pr['neck_y'], pr['neck_z']], rz(a('neck_yaw')) @ rx(a('neck')))
    S.add('head', 'neck', [0, pr['NL'], 0], rz(a('head_yaw')) @ rx(a('head')) @ ry(a('head_roll')))
    S.add('jaw', 'head', [0, 1.0, -0.9], rx(-a('jaw')))
    S.add('tail1', 'hips', [0, -3.0, 1.2], rz(a('tail_yaw')) @ rx(-a('tail')))
    S.add('tail2', 'tail1', [0, -pr['TL'], 0], rz(0.8 * a('tail_yaw')) @ rx(-a('tail_curl')))
    for side, sg in (('r', 1), ('l', -1)):
        S.add('fu_' + side, 'chest', [sg * pr['wf'], 1.2, -1.2],
              ry(-sg * a('fa_' + side)) @ rx(a('fs_' + side) - D(28)))
        S.add('fl_' + side, 'fu_' + side, [0, 0, -pr['FU']], rx(D(32) - a('fk_' + side)))
        S.add('fp_' + side, 'fl_' + side, [0, 0, -pr['FL']], rx(D(-4) + a('fp_' + side)))
        S.add('hu_' + side, 'hips', [sg * pr['wh'], 0.4, -1.0],
              ry(-sg * a('ha_' + side)) @ rx(a('hs_' + side) + D(30)))
        S.add('hl_' + side, 'hu_' + side, [0, 0, -pr['HU']], rx(-a('hk_' + side) - D(72)))
        S.add('hm_' + side, 'hl_' + side, [0, 0, -pr['HL']], rx(a('hh_' + side) + D(38)))
        S.add('hp_' + side, 'hm_' + side, [0, 0, -pr['HM']], rx(D(4)))
    return S


def quad_contacts(pr, k=1.0):
    c = []
    for s in ('r', 'l'):
        c += [('fp_' + s, (0, 1.4 * k, -0.6 * k)), ('fp_' + s, (0, -0.4 * k, -0.6 * k)),
              ('hp_' + s, (0, 1.4 * k, -0.6 * k)), ('hp_' + s, (0, -0.4 * k, -0.6 * k)),
              ('fl_' + s, (0, 0, 0)), ('hl_' + s, (0, 0, 0)), ('hm_' + s, (0, 0, 0))]
    c += [('chest', (0, 0.5, -4.4 * k)), ('chest', (3.6 * k, 0, -0.5)), ('chest', (-3.6 * k, 0, -0.5)),
          ('chest', (0, 0, 3.8 * k)), ('hips', (0, 0, -3.4 * k)), ('hips', (3.2 * k, 0, 0)),
          ('hips', (-3.2 * k, 0, 0)), ('hips', (0, -1, 3.2 * k)), ('head', (0, 3.0 * k, -1.6 * k)),
          ('head', (1.8 * k, 1, 0)), ('head', (-1.8 * k, 1, 0)), ('jaw', (0, 3.5 * k, -0.6 * k))]
    return c


def standing_quad(pr, k=1.0):
    cont = quad_contacts(pr, k)
    return lambda p: grounded(lambda q, lift=0.0: quad(q, pr, lift), p, cont)


def qkeyed(base, keys, times):
    kp = [(t, qpose(base, **floats(kw))) for t, kw in keys]
    return [sample(kp, t) for t in times]


def trot(base, k, n, A=30.0, lift=1.0, bob=0.6, phases=(0.0, math.pi, math.pi, 0.0)):
    """four legs, frame k of n; phases of (front right, front left, hind right,
    hind left): a trot moves the diagonals together"""
    p = dict(base)
    ph = 2 * math.pi * k / n
    for (leg, side), off in zip((('f', 'r'), ('f', 'l'), ('h', 'r'), ('h', 'l')), phases):
        c, s = math.cos(ph + off), math.sin(ph + off)
        up = max(0.0, -s) * lift
        if leg == 'f':
            p['fs_' + side] = base['fs_' + side] + A * c
            p['fk_' + side] = base['fk_' + side] + 70 * up
            p['fp_' + side] = base['fp_' + side] + 30 * up
        else:
            p['hs_' + side] = base['hs_' + side] + A * c
            p['hk_' + side] = base['hk_' + side] + 26 * up
            p['hh_' + side] = base['hh_' + side] + 40 * up
    p['air'] = base['air'] + bob * abs(math.sin(2 * ph))
    p['pitch'] = base['pitch'] + 2.0 * math.sin(2 * ph)
    p['neck'] = base['neck'] + 4.0 * math.sin(2 * ph + 0.8)
    p['tail_yaw'] = base['tail_yaw'] + 14 * math.sin(ph)
    p['twist'] = base['twist'] + 5 * math.sin(ph)
    return p


# ---------------------------------------------------------------- the model of a canine

def dog_model(S, p, pr, fur, k=1.0, mane=0.0, ribs=True, scourge=False, fur_amp=0.16, wounds=0.86):
    M = Model(S)
    ft = fp.tex_fur(fur_amp, 1.1 / k, 1.0)

    def body_mat(q):
        m = np.full(len(q), fur)
        if ribs:
            n = hash2(np.floor(q[:, 0] * 0.9 / k), np.floor(q[:, 1] * 0.8 / k) + 3)
            m[(n > wounds) & (np.abs(q[:, 0]) > 2.0 * k)] = FLESH
        return m
    # haunches, a thin waist, a deep chest
    M.add('hips', shaggy(lambda q: sd_ellipsoid(q, (3.4 * k, 4.4 * k, 3.7 * k)), 0.5 * k, 1.2 / k, 1.0), fur,
          'body', body_mat, ft)
    M.add('hips', shaggy(lambda q: sd_roundcone(q, (0, 1.5 * k, 0.6 * k), (0, pr['L'] - 3.0 * k, 0.0), 2.4 * k,
                                                3.2 * k), 0.4 * k, 1.3 / k, 2.0), fur, 'body', body_mat, ft)

    def chest(q):
        d = sd_ellipsoid(q - np.array([0, 0.6 * k, -0.6 * k]), (3.8 * k, 5.0 * k, 4.6 * k))
        if mane:
            d = np.minimum(d, sd_ellipsoid(q - np.array([0, 1.4 * k, 1.8 * k]), ((3.6 + mane) * k, 4.4 * k,
                                                                               (3.4 + mane) * k)))
        return d
    M.add('chest', shaggy(chest, (0.6 + 0.5 * mane) * k, 1.1 / k, 3.0), fur, 'body', body_mat, ft)
    # neck, head, snout, ears, the jaw with its teeth
    M.add('neck', shaggy(lambda q: sd_roundcone(q, (0, -1.0 * k, 0), (0, pr['NL'], 0), (2.6 + 0.6 * mane) * k,
                                                2.0 * k), (0.5 + 0.3 * mane) * k, 1.4 / k, 4.0), fur, 'body',
          None, ft)
    M.add('head', lambda q: sd_ellipsoid(q - np.array([0, 0.9 * k, 0.5 * k]), (2.4 * k, 2.8 * k, 2.3 * k)), fur,
          'head', None, ft)
    snout_l = 6.4 if scourge else 5.4
    M.add('head', lambda q: sd_roundcone(q, (0, 2.2 * k, 0.2 * k), (0, snout_l * k, -0.5 * k), 1.6 * k, 0.95 * k),
          fur, 'head')
    M.add('head', lambda q: sd_ellipsoid(q - np.array([0, (snout_l + 0.2) * k, -0.25 * k]), (0.6 * k, 0.5 * k,
                                                                                             0.5 * k)), NOSE, 'head')
    for sg in (1, -1):
        M.add('head', lambda q, sg=sg: sd_roundcone(q, (sg * 1.3 * k, 0.2 * k, 1.8 * k),
                                                    (sg * 1.9 * k, -1.2 * k, 4.4 * k), 0.95 * k, 0.2 * k),
              fur, 'head')

    def jaw_mat(q):
        m = np.full(len(q), fur)
        m[(q[:, 2] > 0.1 * k)] = FLESH
        return m
    M.add('jaw', lambda q: sd_roundcone(q, (0, 0.6 * k, 0), (0, (snout_l - 0.8) * k, -0.2 * k), 1.0 * k, 0.6 * k),
          fur, 'head', jaw_mat)
    if p['jaw'] > 6:
        for sg in (1, -1):
            M.add('head', lambda q, sg=sg: sd_roundcone(q, (sg * 0.7 * k, (snout_l - 0.6) * k, -0.9 * k),
                                                        (sg * 0.7 * k, (snout_l - 0.6) * k, -1.8 * k), 0.3 * k,
                                                        0.1 * k), TEETH, 'head')
            M.add('jaw', lambda q, sg=sg: sd_roundcone(q, (sg * 0.6 * k, (snout_l - 1.6) * k, 0.3 * k),
                                                       (sg * 0.6 * k, (snout_l - 1.6) * k, 1.2 * k), 0.28 * k,
                                                       0.1 * k), TEETH, 'head')
    # tail
    M.add('tail1', shaggy(lambda q: sd_roundcone(q, (0, 0, 0), (0, -pr['TL'], 0), 1.2 * k, 0.9 * k), 0.4 * k),
          fur, 'tail', None, ft)
    M.add('tail2', shaggy(lambda q: sd_roundcone(q, (0, 0, 0), (0, -pr['TL'] * 0.9, 0), 0.9 * k, 0.25 * k),
                          0.3 * k), fur, 'tail', None, ft)
    # legs: a heavy forearm and thigh, thin below, paws with claws
    fr = 1.25 if scourge else 1.0
    for s in ('r', 'l'):
        g = 'leg' + s
        M.add('fu_' + s, lambda q: sd_roundcone(q, (0, 0, 0.6 * k), (0, 0, -pr['FU']), 1.7 * k * fr, 1.15 * k * fr),
              fur, g, None, ft)
        M.add('fl_' + s, lambda q: sd_roundcone(q, (0, 0, 0), (0, 0, -pr['FL']), 1.05 * k * fr, 0.8 * k * fr), fur,
              g, None, ft)
        M.add('fp_' + s, lambda q: sd_ellipsoid(q - np.array([0, 0.6 * k, -0.2 * k]),
                                                (1.0 * k * fr, 1.5 * k * fr, 0.7 * k)), fur, g)
        M.add('hu_' + s, lambda q: sd_roundcone(q, (0, 0.2 * k, 0.4 * k), (0, 0, -pr['HU']), 2.4 * k, 1.4 * k), fur,
              g, None, ft)
        M.add('hl_' + s, lambda q: sd_roundcone(q, (0, 0, 0), (0, 0, -pr['HL']), 1.2 * k, 0.75 * k), fur, g, None, ft)
        M.add('hm_' + s, lambda q: sd_roundcone(q, (0, 0, 0), (0, 0, -pr['HM']), 0.75 * k, 0.65 * k), fur, g)
        M.add('hp_' + s, lambda q: sd_ellipsoid(q - np.array([0, 0.6 * k, -0.2 * k]), (0.95 * k, 1.45 * k, 0.65 * k)),
              fur, g)
        for bone in ('fp_' + s, 'hp_' + s):
            for cx in (-0.5, 0.0, 0.5):
                M.add(bone, lambda q, cx=cx: sd_roundcone(q, (cx * k * fr, 1.6 * k * fr, -0.3 * k),
                                                          (cx * k * fr, 2.3 * k * fr, -0.6 * k), 0.22 * k, 0.1 * k),
                      CLAW, g)
    M.decals = [(S.pt('head', (1.05 * k, 2.4 * k, 1.15 * k)), EYE_GLOW),
                (S.pt('head', (-1.05 * k, 2.4 * k, 1.15 * k)), EYE_GLOW)]
    return M


# ================================================================ the rabid dog

dog_skel = standing_quad(DOG)


def dog_build(p):
    S = dog_skel(p)
    return S, dog_model(S, p, DOG, FUR_DARK)


DOG_STAND = qpose(neck=30, head=-30, tail=14, tail_curl=6, fs_r=2, fs_l=-2, hs_r=-2, hs_l=2)
dog = Creature('dog', 'Rabid dog', dog_build, points=[('jaw', (0, 5.0, 0)), ('head', (0, 3, 1))],
               scale=1.3, module=__name__)
dog.anim('idle', qkeyed(DOG_STAND, [(0, {}), (1, dict(breath=1.0, neck=26, jaw=10, head=-26))], [0.0, 1.0]),
         [14, 14], loop=True)
dog.anim('walk', [trot(dict(DOG_STAND, neck=22, head=-24, tail=26), k, 8, A=32) for k in range(8)], [4] * 8,
         loop=True)
# crouch, leap with the jaws open, snap shut on what it reaches, land
dog.anim('attack', qkeyed(DOG_STAND, [
    (0.0, {}),
    (0.26, dict(ty=-2, pitch=-6, neck=10, head=-16, jaw=10, fs_r=-10, fs_l=-8, fk_r=20, fk_l=20, hs_r=18, hs_l=16,
                hk_r=30, hk_l=30, hh_r=20, hh_l=20, tail=4)),
    (0.46, dict(ty=7, air=5, pitch=10, neck=18, head=-20, jaw=44, fs_r=60, fs_l=56, fk_r=50, fk_l=46, hs_r=-46,
                hs_l=-40, hk_r=-20, hk_l=-16, hh_r=-10, hh_l=-10, tail=30)),
    (0.60, dict(ty=10, air=0, pitch=-4, neck=8, head=-20, jaw=2, fs_r=30, fs_l=26, fk_r=10, fk_l=8, hs_r=-24,
                hs_l=-20, tail=24)),
    (0.80, dict(ty=8, neck=20, head=-26, jaw=14, fs_r=10, fs_l=8, hs_r=-6, tail=20)),
    (1.0, {})], [0.0, 0.14, 0.28, 0.40, 0.50, 0.60, 0.74, 0.88]), [3, 4, 4, 3, 3, 4, 5, 5], hit=4)
dog.anim('hurt', qkeyed(DOG_STAND, [(0.0, {}), (0.25, dict(ty=-3, pitch=8, roll=10, neck=50, head=-10, jaw=30,
                                                           fs_r=-16, fs_l=10, hs_r=10, tail=-10)),
                                    (0.6, dict(ty=-2, pitch=3, neck=40, head=-20, jaw=10, tail=0)), (1.0, {})],
                        [0.14, 0.34, 0.64]), [4, 5, 5])
DOG_DEAD = dict(roll=88, air=0, neck=10, head=-10, head_roll=-10, jaw=18, fs_r=30, fs_l=40, fk_r=20, fk_l=30,
                hs_r=-20, hs_l=-6, hk_r=10, hk_l=20, tail=-10, tail_curl=-10)
dog.anim('death', qkeyed(DOG_STAND, [
    (0.0, {}),
    (0.14, dict(ty=-2, pitch=12, neck=56, head=-6, jaw=36, fs_r=-20, fs_l=30, tail=-6)),
    (0.34, dict(roll=30, pitch=4, air=2, neck=40, head=-20, jaw=30, fs_r=10, fs_l=50, fk_l=40, hs_r=10, hk_r=30)),
    (0.56, dict(DOG_DEAD, roll=70, air=2, neck=26)),
    (0.72, dict(DOG_DEAD, roll=94, neck=4)),
    (1.0, DOG_DEAD)], [0.04, 0.14, 0.26, 0.38, 0.50, 0.62, 0.74, 0.86, 1.0]), [3, 4, 4, 4, 4, 4, 5, 6, 60])


# ================================================================ the scourge beast

SC = dict(L=13.5, H=19.0, neck_y=4.6, neck_z=2.6, NL=4.6, wf=3.6, wh=2.6, FU=10.0, FL=10.0, FP=1.8,
          HU=7.6, HL=7.2, HM=4.8, TL=4.0)
sc_skel = standing_quad(SC, 1.3)


def sc_build(p):
    S = sc_skel(p)
    return S, dog_model(S, p, SC, FUR_BROWN, k=1.3, mane=1.2, scourge=True, fur_amp=0.11, wounds=0.93)


# a hunched back: the chest higher than the hips, the long arms ahead
SC_STAND = qpose(pitch=14, arch=10, neck=6, head=-26, tail=-10, tail_curl=-6, fs_r=-10, fs_l=-14, fk_r=-10,
                 fk_l=-8, fa_r=8, fa_l=8, hs_r=-12, hs_l=-8, hk_r=10, hk_l=12, hh_r=4, hh_l=4)
scourge = Creature('scourge', 'Scourge beast', sc_build, points=[('fp_r', (0, 2.5, -0.5)), ('head', (0, 3, 1))],
                   scale=1.4, module=__name__)
scourge.anim('idle', qkeyed(SC_STAND, [(0, {}), (1, dict(breath=1.0, arch=14, neck=2, jaw=12))], [0.0, 1.0]),
             [20, 20], loop=True)
scourge.anim('walk', [trot(SC_STAND, k, 8, A=26, phases=(0.0, math.pi, math.pi * 0.5, math.pi * 1.5))
                      for k in range(8)], [6] * 8, loop=True)
# rears up on the hind legs, both claws high, down in a tearing swipe
SC_REAR = dict(pitch=58, arch=4, neck=-14, head=-12, jaw=48, fs_r=150, fs_l=138, fk_r=-18, fk_l=-14, fp_r=-30,
               fp_l=-30, fa_r=64, fa_l=58, hs_r=-58, hs_l=-50, hk_r=20, hk_l=24, hh_r=10, hh_l=10, tail=-30)
scourge.anim('attack', qkeyed(SC_STAND, [
    (0.0, {}),
    (0.32, SC_REAR),
    (0.50, dict(SC_REAR, pitch=40, ty=4, neck=-6, fs_r=44, fa_r=26, fk_r=0, fs_l=104, fa_l=52, jaw=24)),
    (0.64, dict(pitch=24, ty=6, arch=12, neck=0, head=-20, jaw=8, fs_r=10, fk_r=0, fs_l=30, fa_r=10, hs_r=-26,
                hs_l=-20, hk_r=16)),
    (1.0, {})], [0.0, 0.14, 0.26, 0.36, 0.46, 0.54, 0.68, 0.84]), [4, 5, 5, 4, 2, 3, 6, 6], hit=4)
scourge.anim('hurt', qkeyed(SC_STAND, [(0.0, {}), (0.25, dict(pitch=24, ty=-3, roll=-8, neck=30, head=-6, jaw=36,
                                                              fs_r=30, fs_l=-20, fa_r=30)),
                                       (0.6, dict(pitch=18, neck=18, jaw=12)), (1.0, {})], [0.14, 0.34, 0.64]),
              [4, 5, 6])
SC_DEAD = dict(roll=-86, pitch=0, arch=-6, neck=0, head=-6, head_roll=10, jaw=26, fs_r=40, fs_l=70, fk_r=20,
               fk_l=10, hs_r=-10, hs_l=20, hk_r=30, hk_l=10, tail=-20)
scourge.anim('death', qkeyed(SC_STAND, [
    (0.0, {}),
    (0.16, dict(pitch=44, neck=36, head=0, jaw=50, fs_r=110, fs_l=120, fa_r=40, fa_l=40, hs_r=-40, hs_l=-36)),
    (0.38, dict(pitch=20, roll=-20, neck=10, jaw=30, fs_r=40, fs_l=60, hs_r=-16, hk_r=30, hk_l=40)),
    (0.60, dict(SC_DEAD, roll=-60, air=2)),
    (0.76, dict(SC_DEAD, roll=-92)),
    (1.0, SC_DEAD)], [0.04, 0.14, 0.26, 0.38, 0.50, 0.62, 0.74, 0.86, 1.0]), [4, 4, 4, 4, 4, 5, 5, 6, 60])


# ================================================================ the Great Hound (boss)

HO = dict(L=15.0, H=16.5, neck_y=4.4, neck_z=2.6, NL=5.6, wf=3.0, wh=2.8, FU=7.4, FL=7.4, FP=1.8,
          HU=7.4, HL=7.2, HM=4.8, TL=6.4)
ho_skel = standing_quad(HO, 1.25)


def ho_build(p):
    S = ho_skel(p)
    return S, dog_model(S, p, HO, FUR_PALE, k=1.25, mane=1.6, ribs=True, fur_amp=0.09)


HO_STAND = qpose(neck=28, head=-30, tail=8, tail_curl=10, fs_r=2, fs_l=-2, hs_r=-2, hs_l=2, jaw=6)
hound = Creature('hound', 'The Great Hound', ho_build, points=[('jaw', (0, 6.0, 0)), ('head', (0, 3, 1))],
                 scale=1.9, boss=True, module=__name__)
hound.anim('idle', qkeyed(HO_STAND, [(0, {}), (1, dict(breath=1.0, neck=24, jaw=14, head=-26))], [0.0, 1.0]),
           [22, 22], loop=True)
hound.anim('walk', [trot(dict(HO_STAND, neck=22, head=-26, tail=14), k, 8, A=30,
                         phases=(0.0, math.pi, math.pi * 0.5, math.pi * 1.5)) for k in range(8)], [7] * 8, loop=True)
HO_BITE = dict(ty=8, pitch=-4, neck=6, head=-14, jaw=4, fs_r=36, fs_l=24, fk_r=10, hs_r=-26, hs_l=-20, tail=20)
hound.anim('attack', qkeyed(HO_STAND, [
    (0.0, {}),
    (0.30, dict(ty=-3, pitch=6, neck=40, head=-34, jaw=48, fs_r=-12, fs_l=-10, hs_r=16, hs_l=14, hk_r=20, hk_l=20,
                tail=0)),
    (0.48, HO_BITE),
    (0.70, dict(HO_BITE, ty=6, neck=16, jaw=10)),
    (1.0, {})], [0.0, 0.14, 0.28, 0.40, 0.50, 0.62, 0.76, 0.90]), [4, 5, 6, 3, 3, 5, 6, 6], hit=4)
hound.anim('hurt', qkeyed(HO_STAND, [(0.0, {}), (0.25, dict(ty=-3, pitch=10, roll=8, neck=54, head=-6, jaw=40,
                                                            fs_r=-16, fs_l=14, hs_r=8, tail=-10)),
                                     (0.6, dict(pitch=4, neck=40, head=-20, jaw=16)), (1.0, {})],
                         [0.14, 0.34, 0.64]), [5, 6, 6])
HO_DEAD = dict(DOG_DEAD, jaw=24)
hound.anim('death', qkeyed(HO_STAND, [
    (0.0, {}),
    (0.12, dict(ty=-3, pitch=16, neck=60, head=-2, jaw=50, fs_r=-24, fs_l=40, tail=-6)),
    (0.30, dict(pitch=10, neck=52, head=-10, jaw=50, fs_r=20, fs_l=30)),
    (0.48, dict(roll=30, pitch=4, air=2, neck=36, head=-20, jaw=30, fs_r=10, fs_l=50, fk_l=40, hs_r=10, hk_r=30)),
    (0.66, dict(HO_DEAD, roll=70, air=2, neck=26)),
    (0.80, dict(HO_DEAD, roll=94, neck=4)),
    (1.0, HO_DEAD)], [0.03, 0.10, 0.20, 0.30, 0.40, 0.50, 0.60, 0.70, 0.82, 1.0]), [5, 5, 6, 8, 6, 5, 5, 5, 6, 90])
# special 1, the pounce: a long crouch, a leap high over the ground, all
# four paws come down on the prey
hound.anim('pounce', qkeyed(HO_STAND, [
    (0.0, {}),
    (0.22, dict(ty=-4, pitch=-8, neck=12, head=-20, jaw=10, fs_r=-16, fs_l=-14, fk_r=30, fk_l=30, hs_r=22, hs_l=20,
                hk_r=40, hk_l=40, hh_r=30, hh_l=30, tail=-6)),
    (0.42, dict(ty=8, air=14, pitch=16, neck=24, head=-18, jaw=40, fs_r=70, fs_l=66, fk_r=60, fk_l=56, fa_r=14,
                fa_l=14, hs_r=-50, hs_l=-46, hk_r=-20, hk_l=-16, tail=30)),
    (0.58, dict(ty=16, air=6, pitch=-10, neck=10, head=-24, jaw=46, fs_r=50, fs_l=46, fk_r=-10, fk_l=-10, fa_r=20,
                fa_l=20, hs_r=-20, hs_l=-16, tail=24)),
    (0.66, dict(ty=18, air=0, pitch=-14, neck=0, head=-20, jaw=8, fs_r=34, fs_l=30, fk_r=-6, fk_l=-6, fa_r=24,
                fa_l=24, hs_r=-6, hs_l=-4, hk_r=30, hk_l=30, tail=20)),
    (0.84, dict(ty=16, pitch=-6, neck=16, head=-26, jaw=12, fs_r=18, fs_l=14, fa_r=12, fa_l=12, tail=14)),
    (1.0, dict(ty=14))], [0.0, 0.12, 0.24, 0.34, 0.44, 0.54, 0.62, 0.70, 0.84, 0.95]),
    [5, 6, 6, 4, 4, 4, 3, 10, 7, 7], slam=6)
# special 2, the howl: up on the hind legs, the head thrown back, a cry
# that shakes the street
HO_HOWL = dict(pitch=34, arch=8, neck=70, head=-4, jaw=56, fs_r=50, fs_l=44, fk_r=40, fk_l=36, hs_r=-34,
               hs_l=-30, hk_r=10, hk_l=10, tail=-20)
hound.anim('howl', qkeyed(HO_STAND, [
    (0.0, {}),
    (0.24, dict(pitch=10, neck=10, head=-30, jaw=10, fs_r=10, hs_r=-10, hs_l=-8, hk_r=20, hk_l=20)),
    (0.44, HO_HOWL),
    (0.70, dict(HO_HOWL, neck=74, jaw=60, head_roll=6)),
    (0.84, dict(pitch=12, neck=40, head=-20, jaw=20)),
    (1.0, {})], [0.0, 0.12, 0.24, 0.36, 0.46, 0.56, 0.66, 0.76, 0.86, 0.95]),
    [5, 5, 5, 5, 6, 10, 10, 6, 6, 6], howl=4)
# combo 1: a swipe of the right paw, then of the left, half reared
HO_PAW = dict(pitch=22, arch=6, neck=16, head=-24, jaw=20, hs_r=-24, hs_l=-20, hk_r=14, hk_l=14)
hound.anim('combo1', qkeyed(HO_STAND, [
    (0.0, {}),
    (0.16, dict(HO_PAW, twist=-20, fs_r=110, fk_r=40, fa_r=40, fs_l=10)),
    (0.28, dict(HO_PAW, ty=4, twist=16, pitch=10, fs_r=20, fk_r=-10, fa_r=-10, fs_l=20)),
    (0.40, dict(HO_PAW, ty=4, twist=10, fs_r=10, fs_l=20)),
    (0.56, dict(HO_PAW, ty=4, twist=20, fs_l=110, fk_l=40, fa_l=40, fs_r=10)),
    (0.68, dict(HO_PAW, ty=8, twist=-16, pitch=10, fs_l=20, fk_l=-10, fa_l=-10, fs_r=20)),
    (0.84, dict(ty=8, pitch=6, neck=22, fs_r=8, fs_l=8)),
    (1.0, dict(ty=6))], [0.0, 0.10, 0.18, 0.26, 0.34, 0.46, 0.56, 0.64, 0.70, 0.82, 0.93]),
    [5, 4, 3, 3, 4, 5, 4, 3, 3, 6, 7], hit=[3, 8])
# combo 2: a bite ahead, then the head swung across with the jaws open
hound.anim('combo2', qkeyed(HO_STAND, [
    (0.0, {}),
    (0.16, dict(ty=-2, neck=40, head=-34, jaw=50, hs_r=10, hs_l=10, hk_r=16, hk_l=16)),
    (0.28, HO_BITE),
    (0.42, dict(HO_BITE, neck=20, neck_yaw=-40, head_yaw=-20, jaw=50, twist=-16)),
    (0.60, dict(HO_BITE, ty=10, neck=12, neck_yaw=44, head_yaw=24, jaw=40, twist=18, fs_l=30, fa_l=20)),
    (0.74, dict(HO_BITE, ty=10, neck=16, neck_yaw=54, head_yaw=30, jaw=10, twist=22)),
    (1.0, dict(ty=6))], [0.0, 0.10, 0.18, 0.26, 0.34, 0.42, 0.50, 0.58, 0.66, 0.80, 0.92]),
    [5, 4, 3, 3, 4, 4, 3, 3, 3, 7, 7], hit=[3, 7])
