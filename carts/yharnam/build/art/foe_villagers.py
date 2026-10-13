"""The mad townsfolk of the hunt (Bloodborne fan art): a man with a
pitchfork, one with a torch and a hatchet, one with a rifle, an old woman
with a cleaver; and their boss, the Butcher.
"""
import numpy as np

import foeparts as fp
from foeparts import (BANDAGE, BELT, BLACK, BLOOD, BOOT, COAT_BLUE, COAT_BROWN, COAT_GREEN, DSTEEL, GREY_HAIR,
                      HAIR, LEATHER, LINEN, SACK, SHAWL, SKIN, STEEL, TROUSERS)
from rig import (DEATH, DEATH_BACK, HUMAN, HURT, POSE, Creature, keyed, pose, reach, skel_of, standing,
                 walk_cycle)
from sdf import Model, hash2, sd_box, sd_ellipsoid, sd_roundcone


# ================================================================ the man with a pitchfork

PF = dict(HUMAN, thigh=12.0, shin=11.2)
PF_LEN = 30.0                    # from the grip to the end of the tines
PF_HOLD = 12.0                   # where the left hand holds the shaft


def pf_model(S, p):
    M = Model(S)
    fp.legs(M, PF, TROUSERS, BOOT)
    fp.skirt(M, COAT_GREEN, length=17.0, teeth=1.8, tex=fp.tex_cloth())
    fp.torso(M, PF, COAT_GREEN, tex=fp.tex_cloth())
    fp.belt(M, PF, BELT, z=2.2)
    fp.neck(M)
    fp.head(M, SKIN, HAIR, nose=1.2)
    # a bandage round the brow, the hair sticking out above
    M.add('head', lambda q: np.maximum(sd_ellipsoid(q - np.array([0, 0.3, 5.0]), (3.15, 3.45, 3.7)),
                                       np.abs(q[:, 2] - 6.4) - 0.7), BANDAGE, 'head')
    # a scarf of rags round the neck
    M.add('chest', lambda q: sd_roundcone(q, (0, 0.3, 10.2), (0, 0.6, 12.6), 3.6, 3.0), LINEN, 'body')
    fp.arms(M, PF, COAT_GREEN, fp.SKIN, cuff=LINEN)
    # the pitchfork: a long ash shaft, an iron fork of three tines
    fp.shaft(M, 'wpn_r', 7.0, -PF_LEN + 6.0, 0.55)

    def fork(q):
        x, y, z = q[:, 0], q[:, 1], q[:, 2]
        bar = sd_box(q - np.array([0, 0, -PF_LEN + 6.0]), (2.6, 0.45, 0.45), 0.2)
        d = bar
        for tx in (-2.3, 0.0, 2.3):
            t = sd_roundcone(q, (tx * 0.9, 0, -PF_LEN + 6.0), (tx, 0.4, -PF_LEN), 0.42, 0.22)
            d = np.minimum(d, t)
        return d
    M.add('wpn_r', fork, DSTEEL, 'weapon')
    fp.eyes(S, M)
    return M


pf_skel = standing(PF)


def pf_hold(p):
    """the left hand on the shaft"""
    return reach(p, lambda S: S.pt('wpn_r', (0, 0, -PF_HOLD)), 'l', skel_of(PF))


PF_STANCE = pose(POSE, lean=15, head_pitch=-24, th_r=-4, knee_r=14, th_l=10, knee_l=16, spread=7,
                 sw_r=24, abd_r=18, el_r=56, sw_l=40, abd_l=20, el_l=40)


def pf_build(p):
    S = pf_skel(p)
    return S, pf_model(S, p)


pf = Creature('pitchfork', 'Townsman', pf_build, points=[('wpn_r', (0, 0.4, -PF_LEN)), ('head', (0, 2, 6))],
              extent=[('wpn_r', (0, 0, -PF_LEN)), ('wpn_r', (0, 0, 7.0))], module=__name__)
PF_AIM = dict(aim=(36, 62))
pf.anim('idle', keyed(PF_STANCE, [(0, PF_AIM), (1, dict(PF_AIM, breath=1, lean=17.5, sw_r=26))], [0.0, 1.0],
                      post=pf_hold), [32, 32], loop=True)
pf.anim('walk', [pf_hold(keyed(walk_cycle(PF_STANCE, k, 8, stride=22, arms=0.25, lean=17), [(0, PF_AIM)],
                               [0.0])[0]) for k in range(8)], [7] * 8, loop=True)
pf.anim('attack', keyed(PF_STANCE, [
    (0.0, PF_AIM),
    (0.34, dict(ty=-2, lean=10, chest_yaw=-22, pelvis_yaw=-8, sw_r=-6, abd_r=22, el_r=84, th_r=-14, knee_r=20,
                th_l=16, knee_l=18, head_pitch=-14, aim=(0, 16))),
    (0.52, dict(ty=5, lean=28, chest_yaw=6, pelvis_yaw=4, sw_r=66, abd_r=12, el_r=8, th_l=34, knee_l=36,
                th_r=-24, knee_r=10, head_pitch=-6, aim=(0, -2))),
    (0.66, dict(ty=5, lean=30, chest_yaw=8, sw_r=70, abd_r=12, el_r=6, th_l=34, knee_l=38, th_r=-24, knee_r=10,
                aim=(0, -4))),
    (1.0, PF_AIM)], [0.0, 0.16, 0.32, 0.42, 0.52, 0.62, 0.78, 0.92], post=pf_hold),
    [4, 5, 6, 2, 2, 6, 5, 5], hit=4)
PF_HELD = keyed(PF_STANCE, [(0, PF_AIM)], [0.0], post=pf_hold)[0]
pf.anim('hurt', keyed(PF_HELD, HURT, [0.14, 0.32, 0.62], post=pf_hold), [5, 6, 6])
pf.anim('death', keyed(PF_HELD, DEATH_BACK, [0.04, 0.12, 0.22, 0.34, 0.46, 0.58, 0.70, 0.84, 1.0]),
        [4, 4, 4, 4, 4, 4, 5, 6, 60])


# ================================================================ the man with a torch and a hatchet

TO = dict(HUMAN, thigh=12.8, shin=11.8, shoulder=6.0)
TORCH_L = 10.0                   # the stick of the torch; the flame past its end


def to_model(S, p):
    M = Model(S)
    fp.legs(M, TO, TROUSERS, BOOT)
    fp.skirt(M, COAT_BROWN, length=19.5, teeth=2.0, tex=fp.tex_cloth(seed=2.0))
    fp.torso(M, TO, COAT_BROWN, k=0.95, tex=fp.tex_cloth(seed=2.0))
    fp.belt(M, TO, BELT, z=1.8, k=0.95)
    fp.neck(M)
    fp.head(M, SKIN, HAIR, nose=1.3)
    # a bandage round the head, over one eye
    M.add('head', lambda q: np.maximum(sd_ellipsoid(q - np.array([0, 0.3, 5.3]), (3.15, 3.45, 3.6)),
                                       np.abs(q[:, 2] - 5.6 + 0.25 * q[:, 0]) - 0.75), BANDAGE, 'head')
    fp.wide_hat(M, BLACK, brim=5.0, crown_h=3.0)
    fp.capelet(M, COAT_BROWN, length=6.5, w=7.6, tex=fp.tex_cloth(seed=2.0))
    fp.arms(M, TO, COAT_BROWN, fp.SKIN, cuff=LEATHER)
    # the hatchet: a short handle, an iron head with a curved edge
    fp.shaft(M, 'wpn_r', 2.0, -10.5, 0.5)

    def axe(q):
        d = sd_box(q - np.array([0, 0.6, -9.6]), (0.4, 2.6, 1.1), 0.2)
        edge = sd_ellipsoid(q - np.array([0, 3.0, -9.6]), (0.45, 1.5, 2.6))
        return np.minimum(d, edge)
    M.add('wpn_r', axe, DSTEEL, 'weapon')
    # the torch: a stick, rags, the flame
    fp.shaft(M, 'wpn_l', 2.0, -TORCH_L, 0.55)
    fp.torch_flame(M, 'wpn_l', -TORCH_L, 1.0)
    fp.eyes(S, M, x=-1.15)
    return M


to_skel = standing(TO)


def to_build(p):
    S = to_skel(p)
    return S, to_model(S, p)


TO_STANCE = pose(POSE, lean=10, head_pitch=-22, th_r=-2, knee_r=10, th_l=8, knee_l=12, spread=6,
                 sw_r=14, abd_r=16, el_r=40, sw_l=34, abd_l=26, yaw_l=10, el_l=92)
TO_HOLD = dict(aim=(0, -20), aim_l=(8, 86))
to = Creature('torch', 'Torchbearer', to_build, points=[('wpn_r', (0, 3.6, -9.6)), ('wpn_l', (0, 0, -TORCH_L - 3.0))],
              extent=[('wpn_l', (0, 0, -TORCH_L - 5.0))], module=__name__)
to.anim('idle', keyed(TO_STANCE, [(0, TO_HOLD), (1, dict(TO_HOLD, breath=1, lean=11.5, sw_l=36))], [0.0, 1.0]),
        [30, 30], loop=True)
to.anim('walk', [keyed(walk_cycle(TO_STANCE, k, 8, stride=24, arms=0.6, lean=12), [(0, TO_HOLD)], [0.0])[0]
                 for k in range(8)], [7] * 8, loop=True)
# raised high behind the head, down hard, the torch thrust out to the side
to.anim('attack', keyed(TO_STANCE, [
    (0.0, TO_HOLD),
    (0.36, dict(lean=2, chest_yaw=-18, pelvis_yaw=-6, head_pitch=-20, sw_r=168, abd_r=22, el_r=96, th_r=-10,
                knee_r=12, th_l=14, knee_l=14, sw_l=40, abd_l=34, aim=(0, 120), aim_l=(30, 70))),
    (0.52, dict(lean=26, chest_yaw=8, pelvis_yaw=4, head_pitch=-4, sw_r=84, abd_r=10, el_r=12, th_l=24, knee_l=30,
                th_r=-16, knee_r=14, sw_l=30, abd_l=40, aim=(0, -30), aim_l=(40, 60))),
    (0.66, dict(lean=30, chest_yaw=10, sw_r=58, abd_r=10, el_r=14, th_l=24, knee_l=32, th_r=-16, knee_r=16,
                sw_l=28, abd_l=40, aim=(0, -60), aim_l=(40, 60))),
    (1.0, TO_HOLD)], [0.0, 0.16, 0.30, 0.40, 0.48, 0.56, 0.70, 0.86]),
    [4, 5, 6, 3, 2, 4, 6, 6], hit=4)
TO_HELD = keyed(TO_STANCE, [(0, TO_HOLD)], [0.0])[0]
to.anim('hurt', keyed(TO_HELD, HURT, [0.14, 0.32, 0.62]), [5, 6, 6])
to.anim('death', keyed(TO_HELD, DEATH, [0.04, 0.12, 0.22, 0.34, 0.46, 0.58, 0.72, 0.86, 1.0]),
        [4, 4, 4, 5, 5, 5, 5, 6, 60])


# ================================================================ the man with a rifle

RI = dict(HUMAN, thigh=12.4, shin=11.6, shoulder=6.4)
RIFLE_L = 27.0                   # from the grip to the muzzle
RIFLE_HOLD = 11.0                # the left hand under the barrel


def ri_model(S, p):
    M = Model(S)
    fp.legs(M, RI, TROUSERS, BOOT)
    fp.skirt(M, COAT_BLUE, length=20.5, teeth=1.4, tex=fp.tex_cloth(seed=5.0))
    fp.torso(M, RI, COAT_BLUE, tex=fp.tex_cloth(seed=5.0))
    fp.belt(M, RI, BELT, z=1.4)
    # a crossbelt for the cartridges
    M.add('chest', lambda q: np.maximum(
        sd_roundcone(q / np.array([1.0, 0.70, 1.0]), (0, 0, 0.5), (0, 0, 10.0), 4.65, 5.75) * 0.70,
        np.abs(q[:, 2] - 6.0 - 0.95 * q[:, 0]) - 0.8), BELT, 'body')
    fp.neck(M)
    fp.head(M, SKIN, HAIR)
    # a scarf over the mouth, a peaked hat
    M.add('head', lambda q: np.maximum(sd_ellipsoid(q - np.array([0, 0.4, 3.6]), (3.2, 3.6, 2.6)),
                                       q[:, 2] - 4.9), LINEN, 'head')
    fp.top_hat(M, BLACK, h=4.0)
    fp.capelet(M, COAT_BLUE, length=8.0, w=8.4, tex=fp.tex_cloth(seed=5.0))
    fp.arms(M, RI, COAT_BLUE, LEATHER, cuff=LEATHER)

    # the rifle: stock behind the grip, a lock, a long barrel, a ramrod
    def stock(q):
        d = sd_roundcone(q, (0, -0.6, 6.0), (0, 0.2, -1.0), 1.2, 0.7)
        return np.minimum(d, sd_box(q - np.array([0, -1.0, 6.4]), (0.6, 1.5, 0.9), 0.3))
    M.add('wpn_r', stock, WOOD, 'weapon')
    M.add('wpn_r', lambda q: sd_roundcone(q, (0, 0.3, -1.0), (0, 0.3, -16.0), 0.75, 0.62), WOOD, 'weapon')
    M.add('wpn_r', lambda q: sd_box(q - np.array([0, 0.5, -1.6]), (0.6, 0.9, 1.2), 0.2), DSTEEL, 'weapon')
    M.add('wpn_r', lambda q: sd_roundcone(q, (0, 0.9, -2.0), (0, 0.9, -RIFLE_L), 0.5, 0.45), STEEL, 'weapon')
    fp.eyes(S, M)
    return M


ri_skel = standing(RI)


def ri_build(p):
    S = ri_skel(p)
    return S, ri_model(S, p)


def ri_hold(p):
    return reach(p, lambda S: S.pt('wpn_r', (0, -0.6, -RIFLE_HOLD)), 'l', skel_of(RI))


RI_STANCE = pose(POSE, lean=8, head_pitch=-14, th_r=-2, knee_r=8, th_l=6, knee_l=10, spread=6,
                 sw_r=20, abd_r=14, el_r=60, sw_l=40, abd_l=20, el_l=60)
RI_CARRY = dict(aim=(40, 58))
ri = Creature('rifle', 'Rifleman', ri_build, points=[('wpn_r', (0, 0.9, -RIFLE_L)), ('wpn_r', (0, 0.9, -RIFLE_L))],
              extent=[('wpn_r', (0, 0, -RIFLE_L)), ('wpn_r', (0, 0, 7.0))], module=__name__)
ri.anim('idle', keyed(RI_STANCE, [(0, RI_CARRY), (1, dict(RI_CARRY, breath=1, lean=9.5))], [0.0, 1.0],
                      post=ri_hold), [32, 32], loop=True)
ri.anim('walk', [ri_hold(keyed(walk_cycle(RI_STANCE, k, 8, stride=24, arms=0.2, lean=9), [(0, RI_CARRY)],
                               [0.0])[0]) for k in range(8)], [7] * 8, loop=True)
# up to the shoulder, a steady aim, the shot (it kicks), down again
RI_AIM = dict(lean=4, chest_yaw=-24, pelvis_yaw=-14, head_pitch=-4, head_yaw=18, sw_r=40, abd_r=56, yaw_r=-8,
              el_r=112, th_r=-10, knee_r=10, th_l=12, knee_l=12, spread=9, aim=(22, 0))
ri.anim('attack', keyed(RI_STANCE, [
    (0.0, RI_CARRY),
    (0.30, RI_AIM),
    (0.50, RI_AIM),
    (0.58, dict(RI_AIM, lean=-4, chest_yaw=-22, el_r=118, aim=(22, 14))),
    (0.72, dict(RI_AIM, lean=0, aim=(22, 6))),
    (1.0, RI_CARRY)], [0.0, 0.14, 0.30, 0.42, 0.50, 0.58, 0.72, 0.88], post=ri_hold),
    [4, 5, 6, 10, 3, 5, 6, 6], fire=4)
RI_HELD = keyed(RI_STANCE, [(0, RI_CARRY)], [0.0], post=ri_hold)[0]
ri.anim('hurt', keyed(RI_HELD, HURT, [0.14, 0.32, 0.62], post=ri_hold), [5, 6, 6])
ri.anim('death', keyed(RI_HELD, DEATH_BACK, [0.04, 0.12, 0.22, 0.34, 0.46, 0.58, 0.70, 0.84, 1.0]),
        [4, 4, 4, 4, 4, 4, 5, 6, 60])


# ================================================================ the old woman with a cleaver

CR = dict(HUMAN, thigh=10.6, shin=10.2, waist=3.4, chest=10.2, neck=12.6, shoulder=5.4, hipw=2.8, uarm=9.0,
          farm=8.2, cape=12.8, skirt=3.6)


def cr_model(S, p):
    M = Model(S)
    fp.legs(M, CR, TROUSERS, BOOT, k=0.85)
    hip = CR['thigh'] + CR['shin'] + CR['foot']
    fp.long_skirt(M, BLACK, length=hip - 1.0, top_r=3.6, hem_r=7.4, tex=fp.tex_cloth(seed=8.0))
    # an apron, grey with dirt
    M.add('pelvis', lambda q: np.maximum(sd_ellipsoid(q - np.array([0, 2.0, -6.0]), (4.4, 2.6, 9.0)),
                                         np.maximum(q[:, 2] - 1.0, 1.0 - q[:, 1])), LINEN, 'coat')
    fp.torso(M, CR, BLACK, k=0.82, tex=fp.tex_cloth(seed=8.0))
    fp.neck(M, SKIN, k=0.8)
    fp.head(M, SKIN, GREY_HAIR, k=0.92, nose=1.8)
    fp.capelet(M, SHAWL, length=9.0, w=7.8, k=0.9, tex=fp.tex_cloth(amp=0.07, seed=9.0))
    fp.hood(M, SHAWL, k=0.95, peak=0.4, tex=fp.tex_cloth(amp=0.07, seed=9.0))
    fp.arms(M, CR, BLACK, SKIN, k=0.8, kh=0.9)
    # a butcher's cleaver: a square blade, dark and notched
    fp.shaft(M, 'wpn_r', 1.5, -4.0, 0.5)

    def cl_mat(q):
        m = np.full(len(q), STEEL)
        m[q[:, 1] < 0.0] = DSTEEL
        n = hash2(np.floor(q[:, 2] * 1.1), np.floor(q[:, 1] * 1.1) + 5)
        m[(q[:, 1] > 2.0) & (n > 0.55)] = BLOOD
        return m
    fp.blade_box(M, 'wpn_r', -3.6, -10.6, -0.8, 4.2, 0.35, STEEL, cl_mat)
    fp.eyes(S, M, x=1.0, y=3.0, z=4.9)
    return M


cr_skel = standing(CR)


def cr_build(p):
    S = cr_skel(p)
    return S, cr_model(S, p)


CR_STANCE = pose(POSE, lean=26, head_pitch=-44, th_r=-2, knee_r=18, th_l=8, knee_l=20, spread=6,
                 sw_r=26, abd_r=20, el_r=70, sw_l=30, abd_l=22, el_l=80, wr_l=30)
CR_HOLD = dict(aim=(0, 30))
crone = Creature('crone', 'Crone', cr_build, points=[('wpn_r', (0, 4.0, -10.0)), ('head', (0, 2.5, 5))],
                 module=__name__)
crone.anim('idle', keyed(CR_STANCE, [(0, CR_HOLD), (1, dict(CR_HOLD, breath=1, lean=34))], [0.0, 1.0]),
           [24, 24], loop=True)
crone.anim('walk', [keyed(walk_cycle(CR_STANCE, k, 8, stride=15, arms=0.5, lean=32, bounce=0.6), [(0, CR_HOLD)],
                          [0.0])[0] for k in range(8)], [6] * 8, loop=True)
# two chops in a frenzy
CR_UP = dict(lean=18, head_pitch=-30, chest_yaw=-12, sw_r=160, abd_r=24, el_r=100, sw_l=50, abd_l=30, el_l=60,
             aim=(0, 110))
CR_DOWN = dict(lean=38, head_pitch=-26, chest_yaw=10, sw_r=70, abd_r=12, el_r=16, sw_l=20, abd_l=30, el_l=70,
               th_l=18, knee_l=26, aim=(0, -50))
crone.anim('attack', keyed(CR_STANCE, [
    (0.0, CR_HOLD), (0.22, CR_UP), (0.36, CR_DOWN), (0.52, dict(CR_UP, chest_yaw=-4, sw_r=150)),
    (0.66, dict(CR_DOWN, chest_yaw=16)), (0.80, dict(CR_DOWN, chest_yaw=12, sw_r=60, aim=(0, -60))),
    (1.0, CR_HOLD)], [0.0, 0.12, 0.22, 0.32, 0.44, 0.54, 0.64, 0.76, 0.90]),
    [3, 4, 3, 2, 3, 4, 2, 4, 6], hit=[3, 6])
CR_HELD = keyed(CR_STANCE, [(0, CR_HOLD)], [0.0])[0]
crone.anim('hurt', keyed(CR_HELD, [(0.0, {}), (0.22, dict(HURT[1][1], lean=10)), (0.5, dict(HURT[2][1], lean=22)),
                                   (1.0, {})], [0.14, 0.32, 0.62]), [5, 6, 6])
crone.anim('death', keyed(CR_HELD, DEATH, [0.04, 0.12, 0.22, 0.34, 0.46, 0.58, 0.72, 0.86, 1.0]),
           [4, 4, 4, 5, 5, 5, 5, 6, 60])


# ================================================================ the Butcher (boss)

BU = dict(HUMAN, thigh=11.5, shin=10.5, waist=4.5, chest=13.0, neck=15.0, shoulder=8.6, hipw=4.0, uarm=11.0,
          farm=10.0, hand=2.0, cape=16.0, skirt=4.6, foot=2.4)
BU_BLADE = 20.0


def bu_model(S, p):
    M = Model(S)
    fp.legs(M, BU, TROUSERS, BOOT, k=1.5)
    sh = fp.tex_cloth(amp=0.06, seed=11.0)
    body = fp.torso(M, BU, fp.SHIRT, k=1.45, belly=2.6, depth=0.78, tex=sh)
    fp.belt(M, BU, BELT, z=2.0, k=1.5, depth=0.86)

    # a leather apron from the chest to the knees, black with old blood
    def apron_mat(q):
        m = np.full(len(q), LEATHER)
        n = hash2(np.floor(q[:, 0] * 0.4), np.floor(q[:, 2] * 0.35) + 2)
        m[n > 0.74] = BLOOD
        return m
    M.add('chest', lambda q: np.maximum(body(q) - 0.45, np.maximum(q[:, 2] - 10.5, 1.2 - q[:, 1])), LEATHER,
          'body', apron_mat)
    M.add('pelvis', lambda q: np.maximum(sd_ellipsoid(q - np.array([0, 2.2, -6.0]), (6.2, 4.4, 10.0)),
                                         np.maximum(q[:, 2] - 4.0, 2.4 - q[:, 1])), LEATHER, 'coat', apron_mat)
    M.add('chest', lambda q: sd_roundcone(q, (0, 0.6, 10.0), (0, 0.8, 15.0), 2.8, 2.4), SKIN, 'body')
    # the head in a sack, tied at the neck with a rope
    M.add('head', lambda q: sd_ellipsoid(q - np.array([0, 0.4, 5.0]), (3.7, 4.0, 4.4)), SACK, 'head', None,
          fp.tex_cloth(amp=0.08, f=2.2, seed=3.0))
    M.add('head', lambda q: sd_roundcone(q, (0, -0.4, 8.6), (0.5, -1.4, 10.6), 1.1, 0.4), SACK, 'head')
    M.add('head', lambda q: sd_roundcone(q, (0, 0.4, 0.6), (0, 0.6, 1.4), 3.0, 3.0), BELT, 'head')
    # sleeves rolled up over huge forearms
    for side in ('r', 'l'):
        g = 'arm' + side
        M.add('uarm_' + side, lambda q: sd_roundcone(q, (0, 0, 0.5), (0, 0, -8.0), 3.7, 3.0), fp.SHIRT, g, None, sh)
        M.add('uarm_' + side, lambda q: sd_roundcone(q, (0, 0, -7.0), (0, 0, -11.0), 2.9, 2.7), SKIN, g)
        M.add('farm_' + side, lambda q: sd_roundcone(q, (0, 0, 0), (0, 0, -8.6), 2.9, 2.2), SKIN, g)
        M.add('hand_' + side, lambda q: sd_ellipsoid(q - np.array([0, 0.4, -1.6]), (2.2, 2.5, 2.6)), SKIN, g)
    # the cleaver: a long handle, a blade as big as a door
    fp.shaft(M, 'wpn_r', 3.0, -4.4, 0.85)

    def bl_mat(q):
        m = np.full(len(q), STEEL)
        m[q[:, 1] < 0.4] = DSTEEL
        n = hash2(np.floor(q[:, 2] * 0.45), np.floor(q[:, 1] * 0.5) + 7)
        m[(q[:, 1] > 4.0) & (n > 0.62)] = BLOOD
        return m

    def blade(q):
        d = sd_box(q - np.array([0, 3.3, -4.0 - BU_BLADE / 2]), (0.55, 4.6, BU_BLADE / 2), 0.3)
        hole = sd_roundcone(q, (-2, 5.6, -6.4), (2, 5.6, -6.4), 1.0, 1.0)
        return np.maximum(d, -hole)
    M.add('wpn_r', blade, STEEL, 'weapon', bl_mat)
    if p.get('bomb', 0.0) > 0.5:
        # a bottle of oil, its rag alight
        M.add('hand_l', lambda q: sd_roundcone(q, (0, 0.4, -2.0), (0, 0.4, -5.0), 1.5, 0.7), DSTEEL, 'weapon')
        M.add('hand_l', lambda q: sd_ellipsoid(q - np.array([0, 0.4, -6.4]), (0.9, 0.9, 1.5)), fp.FLAME, 'weapon')
    M.decals = [(S.pt('head', (1.4, 3.95, 5.6)), fp.INK), (S.pt('head', (-1.4, 3.95, 5.6)), fp.INK),
                (S.pt('head', (1.0, 3.95, 5.6)), fp.INK), (S.pt('head', (-1.0, 3.95, 5.6)), fp.INK)]
    return M


bu_skel = standing(BU)


def bu_build(p):
    S = bu_skel(p)
    return S, bu_model(S, p)


def bu_two(p):
    """both hands on the handle when the pose says so (two > 0.5)"""
    if p.get('two', 0.0) > 0.5:
        return reach(p, lambda S: S.pt('wpn_r', (0, 0, 1.6)), 'l', skel_of(BU))
    return p


BU_STANCE = pose(POSE, lean=12, head_pitch=-18, th_r=-4, knee_r=14, th_l=8, knee_l=16, spread=10,
                 sw_r=12, abd_r=24, el_r=34, sw_l=10, abd_l=26, el_l=46, two=0.0, bomb=0.0)
BU_HOLD = dict(aim=(10, -42))
butcher = Creature('butcher', 'The Butcher', bu_build,
                   points=[('wpn_r', (0, 6.0, -4.0 - BU_BLADE * 0.75)), ('hand_l', (0, 0.4, -6.0))],
                   extent=[('wpn_r', (0, 8.0, -4.0 - BU_BLADE)), ('wpn_r', (0, -1.0, -4.0 - BU_BLADE))],
                   scale=1.5, boss=True, module=__name__)
butcher.anim('idle', keyed(BU_STANCE, [(0, BU_HOLD), (1, dict(BU_HOLD, breath=1.6, lean=14.5, sw_l=13))],
                           [0.0, 1.0]), [36, 36], loop=True)
butcher.anim('walk', [keyed(walk_cycle(BU_STANCE, k, 8, stride=20, arms=0.5, lean=14, bounce=0.7),
                            [(0, BU_HOLD)], [0.0])[0] for k in range(8)], [9] * 8, loop=True)
BU_UP = dict(lean=-2, chest_yaw=-16, pelvis_yaw=-6, head_pitch=-22, sw_r=170, abd_r=20, el_r=100, th_r=-10,
             knee_r=16, th_l=14, knee_l=16, sw_l=60, abd_l=30, el_l=70, aim=(0, 120))
BU_DOWN = dict(lean=30, chest_yaw=10, pelvis_yaw=4, head_pitch=-6, sw_r=78, abd_r=14, el_r=10, th_l=26, knee_l=34,
               th_r=-18, knee_r=18, sw_l=20, abd_l=40, el_l=50, aim=(0, -55))
butcher.anim('attack', keyed(BU_STANCE, [
    (0.0, BU_HOLD), (0.38, BU_UP), (0.54, BU_DOWN), (0.70, dict(BU_DOWN, sw_r=66, aim=(0, -70))), (1.0, BU_HOLD)],
    [0.0, 0.16, 0.30, 0.40, 0.49, 0.56, 0.72, 0.88]), [5, 6, 8, 4, 2, 6, 8, 8], hit=4)
BU_HELD = keyed(BU_STANCE, [(0, BU_HOLD)], [0.0])[0]
butcher.anim('hurt', keyed(BU_HELD, HURT, [0.14, 0.32, 0.62]), [6, 7, 7])
butcher.anim('death', keyed(BU_HELD, DEATH, [0.03, 0.10, 0.18, 0.28, 0.40, 0.52, 0.64, 0.76, 0.88, 1.0]),
             [5, 5, 6, 6, 7, 8, 6, 5, 6, 90])
# special 1, the slam: down on his knees, up in the air with both hands on
# the handle, and the blade buried in the stones
BU_SLAM_UP = dict(air=9.0, lean=-6, head_pitch=-26, sw_r=176, abd_r=12, el_r=70, th_r=10, knee_r=40, th_l=30,
                  knee_l=50, two=1.0, aim=(0, 140))
butcher.anim('slam', keyed(BU_STANCE, [
    (0.0, BU_HOLD),
    (0.18, dict(lean=26, knee_r=50, knee_l=52, th_r=20, th_l=24, sw_r=60, el_r=60, two=1.0, aim=(0, 20))),
    (0.38, BU_SLAM_UP),
    (0.50, dict(air=4.0, lean=20, sw_r=130, el_r=30, th_r=0, knee_r=20, th_l=20, knee_l=30, two=1.0, aim=(0, 40))),
    (0.58, dict(air=0.0, lean=44, head_pitch=0, sw_r=70, abd_r=10, el_r=6, th_l=40, knee_l=70, th_r=-20,
                knee_r=60, two=1.0, aim=(0, -80))),
    (0.80, dict(air=0.0, lean=42, sw_r=66, el_r=8, th_l=40, knee_l=70, th_r=-20, knee_r=60, two=1.0,
                aim=(0, -82))),
    (1.0, BU_HOLD)], [0.0, 0.12, 0.22, 0.32, 0.42, 0.52, 0.58, 0.66, 0.80, 0.92], post=bu_two),
    [6, 6, 6, 5, 4, 3, 3, 14, 8, 8], slam=6)
# special 2, the firebomb: a bottle from the belt, lit, thrown overhand
butcher.anim('throw', keyed(BU_STANCE, [
    (0.0, BU_HOLD),
    (0.20, dict(bomb=1.0, chest_yaw=-10, sw_l=-10, abd_l=20, el_l=90, aim=(10, -50))),
    (0.42, dict(bomb=1.0, chest_yaw=30, pelvis_yaw=10, lean=0, sw_l=150, abd_l=40, yaw_l=-10, el_l=100,
                head_pitch=-20, th_r=16, knee_r=16, th_l=-10, knee_l=12)),
    (0.56, dict(bomb=1.0, chest_yaw=-24, pelvis_yaw=-10, lean=22, sw_l=100, abd_l=16, el_l=20, th_r=24,
                knee_r=24, th_l=-18, knee_l=14)),
    (0.62, dict(bomb=0.0, chest_yaw=-30, pelvis_yaw=-12, lean=26, sw_l=70, abd_l=14, el_l=10, th_r=24, knee_r=26,
                th_l=-18, knee_l=14)),
    (0.80, dict(chest_yaw=-20, lean=20, sw_l=30, el_l=30)),
    (1.0, BU_HOLD)], [0.0, 0.12, 0.22, 0.32, 0.42, 0.50, 0.57, 0.66, 0.80, 0.92]),
    [5, 6, 6, 6, 5, 3, 3, 6, 7, 8], throw=6)
# combo 1: a sweep at the waist, then the chop from above
butcher.anim('combo1', keyed(BU_STANCE, [
    (0.0, BU_HOLD),
    (0.14, dict(chest_yaw=-40, pelvis_yaw=-14, lean=10, sw_r=80, abd_r=60, yaw_r=-60, el_r=50, sw_l=30, abd_l=30,
                aim=(-120, 10))),
    (0.26, dict(chest_yaw=34, pelvis_yaw=12, lean=18, sw_r=74, abd_r=14, yaw_r=50, el_r=12, sw_l=10, abd_l=36,
                aim=(60, -6))),
    (0.36, dict(chest_yaw=40, pelvis_yaw=14, lean=16, sw_r=60, abd_r=10, yaw_r=70, el_r=30, aim=(100, -14))),
    (0.58, BU_UP),
    (0.72, BU_DOWN),
    (0.86, dict(BU_DOWN, sw_r=66, aim=(0, -70))),
    (1.0, BU_HOLD)], [0.0, 0.10, 0.18, 0.26, 0.34, 0.46, 0.58, 0.66, 0.72, 0.84, 0.94]),
    [5, 5, 3, 3, 5, 6, 6, 3, 2, 8, 8], hit=[3, 8])
# combo 2: a punch with the left fist, a backhand with the cleaver
butcher.anim('combo2', keyed(BU_STANCE, [
    (0.0, BU_HOLD),
    (0.14, dict(chest_yaw=24, pelvis_yaw=8, sw_l=30, abd_l=20, el_l=110, lean=10, aim=(10, -40))),
    (0.26, dict(chest_yaw=-30, pelvis_yaw=-12, lean=22, sw_l=88, abd_l=10, yaw_l=8, el_l=4, th_l=24, knee_l=26,
                th_r=-14, aim=(10, -40))),
    (0.40, dict(chest_yaw=-20, lean=14, sw_l=40, el_l=60, aim=(10, -30))),
    (0.56, dict(chest_yaw=44, pelvis_yaw=14, lean=12, sw_r=82, abd_r=4, yaw_r=80, el_r=80, tw_r=60, sw_l=10,
                aim=(120, 14))),
    (0.70, dict(chest_yaw=-34, pelvis_yaw=-14, lean=20, sw_r=76, abd_r=48, yaw_r=-54, el_r=12, tw_r=18,
                th_r=-12, knee_r=22, aim=(-60, -8))),
    (0.84, dict(chest_yaw=-46, pelvis_yaw=-16, lean=17, sw_r=60, abd_r=62, yaw_r=-76, el_r=24, aim=(-100, -16))),
    (1.0, BU_HOLD)], [0.0, 0.10, 0.18, 0.26, 0.36, 0.48, 0.58, 0.66, 0.72, 0.84, 0.94]),
    [5, 5, 3, 4, 5, 6, 4, 3, 3, 8, 8], hit=[3, 8])
