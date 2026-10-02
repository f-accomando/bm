"""Hunters gone mad with the blood (Bloodborne fan art): one with a long
axe and a tall hat, one of the Church in white with a great sword; and their
boss, Father Graves, a priest with an axe and a blunderbuss.
"""
import numpy as np

import foeparts as fp
from foeparts import (BANDAGE, BELT, BLACK, BOOT, CHURCH, DSTEEL, GOLD, GREY_HAIR, HAIR, LEATHER, LINEN, SKIN,
                      STEEL, TROUSERS, WOOD)
from rig import (DEATH, DEATH_BACK, HUMAN, HURT, POSE, Creature, keyed, pose, reach, skel_of, standing,
                 walk_cycle)
from sdf import Model, sd_box, sd_ellipsoid, sd_roundcone


def axe_head(M, bone, z, size=1.0, mat=STEEL):
    """a bearded axe head at z along a shaft: the edge towards +y"""
    k = size

    def f(q):
        x, y, zz = q[:, 0], q[:, 1], q[:, 2] - z
        body = np.maximum.reduce([np.abs(x) - 0.45 * k, np.abs(zz) - 1.4 * k, -y - 0.8 * k, y - 2.4 * k])
        edge = sd_ellipsoid(np.stack([x, y - 2.6 * k, zz], 1), (0.42 * k, 2.2 * k, 3.6 * k))
        edge = np.maximum(edge, 1.6 * k - y)
        return np.minimum(body, edge)

    def mf(q):
        m = np.full(len(q), mat)
        m[q[:, 1] < 2.0 * k] = DSTEEL
        return m
    M.add(bone, f, mat, 'weapon', mf)


# ================================================================ the hunter with an axe

AX = dict(HUMAN, thigh=12.8, shin=11.8, shoulder=6.3)
AX_UP, AX_DOWN = 9.0, -15.0           # the shaft, from the butt (behind the hands) to the head
AX_LOW = 7.0                          # the left hand, low on the shaft


def ax_model(S, p):
    M = Model(S)
    fp.legs(M, AX, TROUSERS, BOOT)
    ct = fp.tex_cloth(seed=13.0)
    fp.skirt(M, fp.COAT_GREY, length=21.0, teeth=1.6, tex=ct)
    fp.torso(M, AX, fp.COAT_GREY, tex=ct)
    fp.belt(M, AX, BELT, z=1.6)
    fp.neck(M)
    fp.head(M, SKIN, GREY_HAIR)
    fp.scarf(M, LINEN)
    fp.top_hat(M, BLACK, h=7.5)
    fp.capelet(M, fp.COAT_GREY, length=9.0, w=8.8, tex=ct)
    fp.arms(M, AX, fp.COAT_GREY, LEATHER, cuff=LEATHER)
    fp.shaft(M, 'wpn_r', AX_UP, AX_DOWN + 0.5, 0.55)
    axe_head(M, 'wpn_r', AX_DOWN + 1.5, 1.4)
    fp.eyes(S, M)
    return M


ax_skel = standing(AX)


def ax_build(p):
    S = ax_skel(p)
    return S, ax_model(S, p)


def ax_hold(p):
    return reach(p, lambda S: S.pt('wpn_r', (0, 0, AX_LOW)), 'l', skel_of(AX))


AX_STANCE = pose(POSE, lean=10, head_pitch=-14, th_r=-4, knee_r=12, th_l=10, knee_l=14, spread=7,
                 sw_r=30, abd_r=18, el_r=50, sw_l=10, abd_l=20, el_l=60)
AX_CARRY = dict(aim=(-30, 50))
axe = Creature('axehunter', 'Mad hunter', ax_build, points=[('wpn_r', (0, 4.5, AX_DOWN + 1.5)), ('head', (0, 2, 6))],
               extent=[('wpn_r', (0, 0, AX_UP)), ('wpn_r', (0, 5, AX_DOWN))], module=__name__)
axe.anim('idle', keyed(AX_STANCE, [(0, AX_CARRY), (1, dict(AX_CARRY, breath=1, lean=11.5))], [0.0, 1.0],
                       post=ax_hold), [30, 30], loop=True)
axe.anim('walk', [ax_hold(keyed(walk_cycle(AX_STANCE, k, 8, stride=25, arms=0.25, lean=11), [(0, AX_CARRY)],
                                [0.0])[0]) for k in range(8)], [6] * 8, loop=True)
# a wide swing with both hands, from the right shoulder across to the left
axe.anim('attack', keyed(AX_STANCE, [
    (0.0, AX_CARRY),
    (0.32, dict(chest_yaw=40, pelvis_yaw=14, lean=8, sw_r=70, abd_r=20, yaw_r=50, el_r=60, th_r=-12, knee_r=16,
                th_l=14, knee_l=16, head_yaw=-10, aim=(120, 30))),
    (0.50, dict(chest_yaw=0, lean=16, sw_r=80, abd_r=24, yaw_r=0, el_r=20, knee_r=22, knee_l=24, aim=(10, -6))),
    (0.60, dict(chest_yaw=-36, pelvis_yaw=-12, lean=18, sw_r=70, abd_r=50, yaw_r=-50, el_r=14, knee_r=24,
                knee_l=22, head_yaw=10, aim=(-70, -10))),
    (0.78, dict(chest_yaw=-46, pelvis_yaw=-16, lean=16, sw_r=50, abd_r=60, yaw_r=-66, el_r=26, aim=(-110, -16))),
    (1.0, AX_CARRY)], [0.0, 0.16, 0.32, 0.42, 0.50, 0.58, 0.72, 0.88], post=ax_hold),
    [4, 5, 7, 3, 2, 3, 6, 6], hit=4)
AX_HELD = keyed(AX_STANCE, [(0, AX_CARRY)], [0.0], post=ax_hold)[0]
axe.anim('hurt', keyed(AX_HELD, HURT, [0.14, 0.32, 0.62], post=ax_hold), [5, 6, 6])
axe.anim('death', keyed(AX_HELD, DEATH, [0.04, 0.12, 0.22, 0.34, 0.46, 0.58, 0.72, 0.86, 1.0]),
         [4, 4, 4, 5, 5, 5, 5, 6, 60])


# ================================================================ the Church hunter

CH = dict(HUMAN, thigh=13.0, shin=12.0, shoulder=6.2, neck=14.4)
SWORD = 20.0                          # the blade, past the guard


def ch_model(S, p):
    M = Model(S)
    fp.legs(M, CH, BLACK, BOOT)
    ct = fp.tex_cloth(amp=0.04, seed=17.0)

    def trim(q):
        """gold along the hem of the white coat"""
        m = np.full(len(q), CHURCH)
        m[q[:, 2] < -19.0] = GOLD
        return m
    fp.skirt(M, CHURCH, length=22.5, teeth=0.8, tex=ct, matf=trim)
    fp.torso(M, CH, CHURCH, tex=ct)
    fp.belt(M, CH, BELT, z=1.6)
    M.add('chest', lambda q: sd_box(q - np.array([0, 3.9, 1.6]), (0.9, 0.5, 0.8), 0.2), GOLD, 'body')
    fp.neck(M)
    fp.head(M, SKIN, HAIR)
    fp.hood(M, CHURCH, k=1.0, peak=0.8, tex=ct)
    # a mask of cloth over the lower face
    M.add('head', lambda q: np.maximum(sd_ellipsoid(q - np.array([0, 0.5, 3.8]), (3.1, 3.5, 2.8)), q[:, 2] - 4.8),
          BLACK, 'head')
    fp.capelet(M, CHURCH, length=10.0, w=8.6, tex=ct)
    fp.arms(M, CH, CHURCH, LEATHER, cuff=BLACK)
    # the great sword: a long grip, a cross guard, a broad straight blade
    fp.shaft(M, 'wpn_r', 4.0, -1.6, 0.6, LEATHER)
    M.add('wpn_r', lambda q: sd_box(q - np.array([0, 0, -1.8]), (0.5, 2.8, 0.5), 0.2), GOLD, 'weapon')
    fp.blade_box(M, 'wpn_r', -2.0, -2.0 - SWORD, -0.9, 0.9, 0.35, STEEL, point=1.2)
    fp.eyes(S, M)
    return M


ch_skel = standing(CH)


def ch_build(p):
    S = ch_skel(p)
    return S, ch_model(S, p)


CH_STANCE = pose(POSE, lean=6, head_pitch=-10, th_r=-6, knee_r=12, th_l=12, knee_l=14, spread=8,
                 sw_r=30, abd_r=20, el_r=40, sw_l=6, abd_l=16, el_l=30)
CH_GUARD = dict(aim=(10, 30))
church = Creature('church', 'Church hunter', ch_build, points=[('wpn_r', (0, 0, -2.0 - SWORD)), ('head', (0, 2, 6))],
                  extent=[('wpn_r', (0, 0, -2.0 - SWORD))], module=__name__)
church.anim('idle', keyed(CH_STANCE, [(0, CH_GUARD), (1, dict(CH_GUARD, breath=1, lean=7.5))], [0.0, 1.0]),
            [30, 30], loop=True)
church.anim('walk', [keyed(walk_cycle(CH_STANCE, k, 8, stride=26, arms=0.4, lean=8), [(0, CH_GUARD)], [0.0])[0]
                     for k in range(8)], [6] * 8, loop=True)
# raised over the right shoulder, a long diagonal cut down to the left
church.anim('attack', keyed(CH_STANCE, [
    (0.0, CH_GUARD),
    (0.32, dict(chest_yaw=24, pelvis_yaw=8, lean=0, head_pitch=-16, sw_r=160, abd_r=40, yaw_r=20, el_r=80,
                th_r=-10, knee_r=12, th_l=14, knee_l=14, sw_l=40, abd_l=30, aim=(60, 110))),
    (0.48, dict(chest_yaw=0, lean=14, sw_r=100, abd_r=24, el_r=20, aim=(0, 20))),
    (0.58, dict(chest_yaw=-30, pelvis_yaw=-10, lean=24, sw_r=60, abd_r=30, yaw_r=-30, el_r=10, th_l=24,
                knee_l=30, th_r=-18, knee_r=14, sw_l=20, abd_l=40, aim=(-50, -40))),
    (0.78, dict(chest_yaw=-36, pelvis_yaw=-12, lean=24, sw_r=48, abd_r=34, yaw_r=-40, el_r=20, th_l=24, knee_l=30,
                th_r=-18, aim=(-70, -60))),
    (1.0, CH_GUARD)], [0.0, 0.16, 0.32, 0.42, 0.50, 0.58, 0.72, 0.88]), [4, 5, 6, 3, 2, 3, 6, 6], hit=4)
CH_HELD = keyed(CH_STANCE, [(0, CH_GUARD)], [0.0])[0]
church.anim('hurt', keyed(CH_HELD, HURT, [0.14, 0.32, 0.62]), [5, 6, 6])
church.anim('death', keyed(CH_HELD, DEATH_BACK, [0.04, 0.12, 0.22, 0.34, 0.46, 0.58, 0.70, 0.84, 1.0]),
            [4, 4, 4, 4, 4, 4, 5, 6, 60])


# ================================================================ Father Graves (boss)

FA = dict(HUMAN, thigh=13.2, shin=12.2, shoulder=6.8, chest=12.0, neck=14.4, uarm=10.4, farm=9.4)
FA_UP, FA_DOWN = 4.0, -17.0
GUN = 15.0                            # the blunderbuss, grip to muzzle


def fa_model(S, p):
    M = Model(S)
    fp.legs(M, FA, TROUSERS, BOOT, k=1.1)
    ct = fp.tex_cloth(amp=0.05, seed=21.0)
    # the long black coat of a priest, to the ankles
    fp.skirt(M, fp.BLACK_LIT, length=24.5, top_r=4.4, hem_r=7.4, teeth=1.2, tex=ct)
    fp.torso(M, FA, fp.BLACK_LIT, k=1.08, tex=ct)
    fp.belt(M, FA, BELT, z=1.6, k=1.1)
    fp.neck(M)
    # a white collar
    M.add('chest', lambda q: np.maximum(sd_roundcone(q, (0, 0.6, 11.6), (0, 0.8, 13.6), 2.4, 2.2),
                                        -q[:, 1] + 0.5), LINEN, 'body')
    fp.head(M, SKIN, GREY_HAIR, nose=1.2)
    # a bandage over the eyes
    M.add('head', lambda q: np.maximum(sd_ellipsoid(q - np.array([0, 0.3, 4.9]), (3.1, 3.45, 3.7)),
                                       np.abs(q[:, 2] - 5.4) - 0.8), BANDAGE, 'head')
    # a wide hat of a priest
    fp.wide_hat(M, BLACK, brim=6.0, crown_h=2.6, droop=0.3)
    # a short cape, its edge ragged
    fp.capelet(M, fp.BLACK_LIT, length=12.0, w=9.4, k=1.05, tex=ct)
    fp.arms(M, FA, fp.BLACK_LIT, LEATHER, cuff=LEATHER, k=1.08)
    # the axe in the right hand
    fp.shaft(M, 'wpn_r', FA_UP, FA_DOWN + 0.5, 0.6)
    axe_head(M, 'wpn_r', FA_DOWN + 1.6, 1.3)
    # the blunderbuss in the left: a stock, a lock, a flared barrel

    def gun(q):
        d = sd_roundcone(q, (0, -0.4, 2.8), (0, 0.2, -0.8), 1.0, 0.8)
        return d
    M.add('wpn_l', gun, WOOD, 'gun')
    M.add('wpn_l', lambda q: sd_box(q - np.array([0, 0.3, -1.8]), (0.65, 1.0, 1.3), 0.2), DSTEEL, 'gun')
    M.add('wpn_l', lambda q: sd_roundcone(q, (0, 0.5, -2.0), (0, 0.5, -GUN), 0.7, 1.3), DSTEEL, 'gun')
    return M


fa_skel = standing(FA)


def fa_build(p):
    S = fa_skel(p)
    return S, fa_model(S, p)


def fa_two(p):
    if p.get('two', 0.0) > 0.5:
        return reach(p, lambda S: S.pt('wpn_r', (0, 0, FA_UP - 1.0)), 'l', skel_of(FA))
    return p


FA_STANCE = pose(POSE, lean=10, head_pitch=-12, th_r=-4, knee_r=14, th_l=10, knee_l=16, spread=8,
                 sw_r=16, abd_r=20, el_r=40, sw_l=20, abd_l=20, el_l=50, two=0.0)
FA_HOLD = dict(aim=(0, -40), aim_l=(0, -10))
father = Creature('father', 'Father Graves', fa_build,
                  points=[('wpn_r', (0, 5.0, FA_DOWN + 1.6)), ('wpn_l', (0, 0.5, -GUN))],
                  extent=[('wpn_r', (0, 6, FA_DOWN)), ('wpn_l', (0, 0, -GUN))], scale=1.35, boss=True,
                  module=__name__)
father.anim('idle', keyed(FA_STANCE, [(0, FA_HOLD), (1, dict(FA_HOLD, breath=1, lean=11.5))], [0.0, 1.0]),
            [30, 30], loop=True)
father.anim('walk', [keyed(walk_cycle(FA_STANCE, k, 8, stride=24, arms=0.5, lean=11), [(0, FA_HOLD)], [0.0])[0]
                     for k in range(8)], [7] * 8, loop=True)
FA_UPPER = dict(lean=-2, chest_yaw=-16, head_pitch=-20, sw_r=168, abd_r=20, el_r=90, th_r=-10, knee_r=14, th_l=14,
                knee_l=14, sw_l=20, abd_l=30, aim=(0, 120), aim_l=(0, -20))
FA_CHOP = dict(lean=28, chest_yaw=8, head_pitch=-4, sw_r=80, abd_r=12, el_r=10, th_l=26, knee_l=32, th_r=-18,
               knee_r=16, sw_l=10, abd_l=36, aim=(0, -50), aim_l=(0, -30))
father.anim('attack', keyed(FA_STANCE, [
    (0.0, FA_HOLD), (0.36, FA_UPPER), (0.52, FA_CHOP), (0.70, dict(FA_CHOP, sw_r=64, aim=(0, -66))),
    (1.0, FA_HOLD)], [0.0, 0.16, 0.30, 0.40, 0.48, 0.56, 0.72, 0.88]), [4, 5, 6, 3, 2, 5, 6, 6], hit=4)
FA_HELD = keyed(FA_STANCE, [(0, FA_HOLD)], [0.0])[0]
father.anim('hurt', keyed(FA_HELD, HURT, [0.14, 0.32, 0.62]), [5, 6, 6])
father.anim('death', keyed(FA_HELD, DEATH, [0.03, 0.10, 0.18, 0.28, 0.40, 0.52, 0.64, 0.76, 0.88, 1.0]),
            [5, 5, 6, 6, 7, 8, 6, 5, 6, 90])
# special 1: the blunderbuss, raised level and fired at close range
FA_AIM = dict(sw_l=88, abd_l=8, yaw_l=-4, el_l=4, chest_yaw=-14, pelvis_yaw=-8, lean=6, head_pitch=-6,
              th_l=14, knee_l=14, th_r=-10, knee_r=12, spread=9, aim_l=(0, 2), aim=(0, -50))
father.anim('blast', keyed(FA_STANCE, [
    (0.0, FA_HOLD), (0.30, FA_AIM), (0.46, FA_AIM),
    (0.54, dict(FA_AIM, sw_l=112, el_l=24, lean=-4, head_pitch=-12, aim_l=(0, 26))),
    (0.70, dict(FA_AIM, sw_l=96, el_l=12, aim_l=(0, 10))),
    (1.0, FA_HOLD)], [0.0, 0.12, 0.24, 0.34, 0.44, 0.52, 0.60, 0.70, 0.82, 0.93]),
    [4, 5, 6, 6, 8, 3, 4, 5, 6, 6], fire=5)
# special 2: the whirl, the axe in both hands swung round and round
FA_WHIRL = dict(two=1.0, lean=18, sw_r=78, abd_r=60, yaw_r=-10, el_r=10, th_r=-14, knee_r=24, th_l=16, knee_l=24,
                spread=12)
father.anim('whirl', keyed(FA_STANCE, [
    (0.0, dict(FA_HOLD, two=1.0)),
    (0.16, dict(FA_WHIRL, yaw=-30, chest_yaw=40, aim=(110, 0))),
    (0.32, dict(FA_WHIRL, yaw=90, chest_yaw=0, aim=(100, -6))),
    (0.48, dict(FA_WHIRL, yaw=210, chest_yaw=0, aim=(100, -6))),
    (0.64, dict(FA_WHIRL, yaw=330, chest_yaw=0, aim=(100, -6))),
    (0.80, dict(FA_WHIRL, yaw=380, chest_yaw=-30, aim=(-60, -20))),
    (1.0, dict(FA_HOLD, yaw=360, two=1.0))], [0.0, 0.12, 0.22, 0.30, 0.38, 0.46, 0.54, 0.62, 0.72, 0.86],
    post=fa_two), [5, 6, 4, 3, 3, 3, 3, 4, 6, 7], hit=[3, 5, 7])
# combo 1: a swing from the right, a backhand from the left
father.anim('combo1', keyed(FA_STANCE, [
    (0.0, FA_HOLD),
    (0.14, dict(chest_yaw=-40, pelvis_yaw=-14, lean=10, sw_r=84, abd_r=60, yaw_r=-60, el_r=60, aim=(-120, 20))),
    (0.26, dict(chest_yaw=34, pelvis_yaw=12, lean=18, sw_r=76, abd_r=14, yaw_r=50, el_r=12, aim=(60, -6))),
    (0.36, dict(chest_yaw=42, pelvis_yaw=14, lean=16, sw_r=60, abd_r=8, yaw_r=74, el_r=30, aim=(100, -14))),
    (0.52, dict(chest_yaw=46, pelvis_yaw=14, lean=12, sw_r=82, abd_r=4, yaw_r=82, el_r=84, tw_r=60, aim=(120, 20))),
    (0.66, dict(chest_yaw=-32, pelvis_yaw=-12, lean=19, sw_r=78, abd_r=46, yaw_r=-50, el_r=12, tw_r=20,
                aim=(-56, -8))),
    (0.82, dict(chest_yaw=-44, pelvis_yaw=-16, lean=17, sw_r=60, abd_r=60, yaw_r=-74, el_r=24, aim=(-100, -16))),
    (1.0, FA_HOLD)], [0.0, 0.10, 0.18, 0.26, 0.34, 0.46, 0.56, 0.64, 0.70, 0.82, 0.93]),
    [4, 4, 3, 3, 4, 5, 4, 3, 3, 6, 7], hit=[3, 8])
# combo 2: a chop, then the blunderbuss in the face
father.anim('combo2', keyed(FA_STANCE, [
    (0.0, FA_HOLD), (0.16, FA_UPPER), (0.28, FA_CHOP), (0.40, dict(FA_CHOP, sw_r=64, aim=(0, -66))),
    (0.58, dict(FA_AIM, aim=(0, -60))), (0.70, dict(FA_AIM, aim=(0, -60))),
    (0.78, dict(FA_AIM, sw_l=112, el_l=24, lean=-4, aim_l=(0, 26), aim=(0, -60))),
    (1.0, FA_HOLD)], [0.0, 0.10, 0.18, 0.26, 0.34, 0.46, 0.58, 0.68, 0.76, 0.84, 0.94]),
    [4, 4, 4, 3, 4, 5, 5, 6, 3, 6, 7], hit=3, fire=8)
