"""The hunter's animations, as key poses on a timeline (0..1) sampled into
frames: a cubic curve through the keys (Hermite, tangents from the
neighbours, still at both ends), so the motion is smooth and keeps its
speed through a key (a sword swing does not stop at the moment of impact).

    ANIMS: name -> { 'frames': [pose], 'ticks': [1/60 s each], 'loop': bool,
                     'ext': the saw cleaver out, 'events': {name: frame} }
Every animation is rendered in the 8 directions by mkassets.py.
"""
import math

import numpy as np

from hunter import BASE, extended, idle_pose, intrusion, pose, skeleton, walk_pose

NUM = [k for k, v in BASE.items() if isinstance(v, float)]


def sample(keys, t):
    """keys: [(time, pose)], times rising; -> the pose at time t"""
    if t <= keys[0][0]:
        return dict(keys[0][1])
    if t >= keys[-1][0]:
        return dict(keys[-1][1])
    i = 0
    while keys[i + 1][0] < t:
        i += 1
    (t1, p1), (t2, p2) = keys[i], keys[i + 1]
    t0, p0 = keys[i - 1] if i > 0 else keys[i]
    t3, p3 = keys[i + 2] if i + 2 < len(keys) else keys[i + 1]
    h = t2 - t1
    u = (t - t1) / h
    h00, h10 = 2 * u ** 3 - 3 * u ** 2 + 1, u ** 3 - 2 * u ** 2 + u
    h01, h11 = -2 * u ** 3 + 3 * u ** 2, u ** 3 - u ** 2
    out = dict(p1)
    for k in NUM:
        a, b = p1[k], p2[k]
        # tangents: none at the first and last key (ease in, ease out)
        m1 = 0.0 if i == 0 else (p2[k] - p0[k]) / (t2 - t0)
        m2 = 0.0 if i + 2 >= len(keys) else (p3[k] - p1[k]) / (t3 - t1)
        out[k] = h00 * a + h10 * h * m1 + h01 * b + h11 * h * m2
    for k in ('skirt_r', 'skirt_l'):
        a, b = p1[k], p2[k]
        if a is None and b is None:
            out[k] = None
        else:
            a = 0.62 * p1['th_' + k[-1]] if a is None else a
            b = 0.62 * p2['th_' + k[-1]] if b is None else b
            out[k] = a + (b - a) * (3 * u * u - 2 * u ** 3)
    return out


def weapon_dir(p):
    S = skeleton(p)
    return S.b['wield'][0] @ np.array([0, 0, -1.0]), S.b['body'][0]


def aim(p, az, el):
    """turns the right wrist so the saw cleaver points at azimuth az (0 ahead
    of the body, +90 to its left) and elevation el (degrees)"""
    a, e = math.radians(az), math.radians(el)
    want_body = np.array([-math.sin(a) * math.cos(e), math.cos(a) * math.cos(e), math.sin(e)])
    best = None
    for wr in range(-90, 91, 10):
        for wd in range(-70, 71, 10):
            q = dict(p, wr_r=float(wr), wd_r=float(wd))
            d, R = weapon_dir(q)
            err = -float(d @ (R @ want_body))
            if best is None or err < best[0]:
                best = (err, wr, wd)
    _, wr0, wd0 = best
    for wr in range(wr0 - 8, wr0 + 9, 2):
        for wd in range(wd0 - 8, wd0 + 9, 2):
            q = dict(p, wr_r=float(wr), wd_r=float(wd))
            d, R = weapon_dir(q)
            err = -float(d @ (R @ want_body))
            if err < best[0]:
                best = (err, wr, wd)
    p['wr_r'], p['wd_r'] = float(best[1]), float(best[2])
    return p


def keyed(base, keys, times, ext=False):
    """keys: [(time, {overrides of base})] -> the poses at `times`; a key
    with 'aim': (az, el) points the saw cleaver there (turning the wrist)"""
    kp = []
    for t, kw in keys:
        kw = dict(kw)
        target = kw.pop('aim', None)
        p = pose(base, **kw)
        if ext:
            p['blade'], p['grip'] = 0.0, p['grip'] if p['grip'] != 0.0 else -24.0
        if target:
            aim(p, *target)
        kp.append((t, p))
    frames = [sample(kp, t) for t in times]
    if ext:
        for f in frames:
            f['blade'], f['grip'] = 0.0, f['grip'] if f['grip'] != 0.0 else -24.0
    return frames


ANIMS = {}


ROLLS = list(range(-150, 181, 15))


def unclip(frames, start=True, end=True):
    """rolls the saw cleaver round its own line, frame by frame, so what is
    behind the hand (the hinge folded, the handle's tail open) keeps out of
    the body: the least intrusion, the roll changing little from one
    frame to the next (at most 60 degrees) and none at the first and the
    last frame when they are a stance (start, end)"""
    cost = [[4.0 * intrusion(dict(p, croll=float(c))) for c in ROLLS] for p in frames]
    if not any(min(row) < row[ROLLS.index(0)] for row in cost):
        return frames
    n, best, back = len(frames), [], []
    for f in range(n):
        row, brow = [], []
        for j, c in enumerate(ROLLS):
            fixed = (f == 0 and start) or (f == n - 1 and end)
            own = cost[f][j] + (1e6 if fixed and c else 0.03 * abs(c) if f in (0, n - 1) else 0.0)
            if f == 0:
                row.append(own)
                brow.append(-1)
                continue
            k = min((best[-1][i] + 0.05 * abs(c - ROLLS[i]) + (1e6 if abs(c - ROLLS[i]) > 60 else 0), i)
                    for i in range(len(ROLLS)))
            row.append(own + k[0])
            brow.append(k[1])
        best.append(row)
        back.append(brow)
    j = min(range(len(ROLLS)), key=lambda i: best[-1][i])
    rolls = []
    for f in range(n - 1, -1, -1):
        rolls.append(ROLLS[j])
        j = back[f][j]
    rolls.reverse()
    return [dict(p, croll=float(c)) for p, c in zip(frames, rolls)]


def anim(name, frames, ticks, loop=False, ext=False, **events):
    assert len(frames) == len(ticks), name
    lying = name.startswith(('down', 'getup', 'death'))
    frames = unclip(frames, start=not lying and not name.startswith('hurt'), end=not lying)
    ANIMS[name] = {'frames': frames, 'ticks': ticks, 'loop': loop, 'ext': ext, 'events': events}


# ---------------------------------------------------------------- standing, walking

STAND = idle_pose(0, 4)
anim('idle', [idle_pose(k, 4) for k in range(4)], [26, 16, 26, 16], loop=True)
anim('walk', [walk_pose(k, 8) for k in range(8)], [6] * 8, loop=True)
anim('idle_x', [idle_pose(k, 4, True) for k in range(4)], [26, 16, 26, 16], loop=True, ext=True)
anim('walk_x', [walk_pose(k, 8, True) for k in range(8)], [6] * 8, loop=True, ext=True)

# ---------------------------------------------------------------- the pistol
# the left arm comes up straight and taut (a little above level, so it
# reads as raised from every side, not as the arm at rest), the shoulder
# forward and the pistol in line with it; fires, kicks up with the recoil,
# comes down

AIM = dict(sw_l=108, abd_l=8, yaw_l=-18, el_l=0, wr_l=-30, wd_l=10, tw_l=0,
           chest_yaw=-12, pelvis_yaw=-8, lean=3, head_pitch=-2, head_yaw=4,
           sw_r=-18, abd_r=22, el_r=20, th_l=12, knee_l=12, th_r=-8, knee_r=10, spread=7)
SHOOT_KEYS = [
    (0.0, {}),
    (0.30, AIM),
    (0.42, AIM),
    (0.52, dict(AIM, sw_l=124, el_l=14, wr_l=-14, lean=0, head_pitch=-8, chest_yaw=-14)),
    (0.68, dict(AIM, sw_l=112, el_l=4, wr_l=-24)),
    (1.0, {}),
]
for ext, name in ((False, 'shoot'), (True, 'shoot_x')):
    base = extended(dict(STAND)) if ext else STAND
    anim(name, keyed(base, SHOOT_KEYS, [0.0, 0.15, 0.30, 0.42, 0.52, 0.66, 0.84], ext),
         [3, 3, 3, 3, 4, 5, 6], ext=ext, fire=3)

# ---------------------------------------------------------------- the saw cleaver, folded
# a fighting stance to start and end from, then three blows: a slash from
# right to left, a backhand from left to right, a chop from above

FIGHT = dict(lean=12, th_r=-6, knee_r=14, th_l=12, knee_l=16, spread=8, chest_yaw=-6,
             sw_r=24, abd_r=20, el_r=46, wr_r=-6, grip=-20, sw_l=8, abd_l=18, el_l=34)
STANCE = pose(STAND, **FIGHT)


def blow(windup, swing, hit, follow, base=STANCE):
    return [(0.0, {}), (0.30, windup), (0.46, swing), (0.56, hit), (0.74, follow), (1.0, {})]


SLASH1 = blow(
    dict(chest_yaw=-36, pelvis_yaw=-12, lean=10, sw_r=92, abd_r=58, yaw_r=-58, el_r=78, tw_r=-20,
         sw_l=36, abd_l=24, yaw_l=24, el_l=46, head_yaw=10, th_r=-8, knee_r=18, th_l=14, knee_l=20, aim=(-120, 25)),
    dict(chest_yaw=-4, pelvis_yaw=-2, sw_r=86, abd_r=30, yaw_r=-6, el_r=30, tw_r=-8, sw_l=26, aim=(-35, 4)),
    dict(chest_yaw=28, pelvis_yaw=10, lean=18, sw_r=76, abd_r=12, yaw_r=46, el_r=14, tw_r=6,
         sw_l=10, abd_l=30, yaw_l=-10, knee_l=24, head_yaw=-6, aim=(40, -6)),
    dict(chest_yaw=40, pelvis_yaw=12, lean=16, sw_r=56, abd_r=8, yaw_r=74, el_r=32, sw_l=4, abd_l=34,
         knee_l=22, aim=(85, -14)),
)
SLASH2 = blow(
    dict(chest_yaw=34, pelvis_yaw=10, lean=12, sw_r=82, abd_r=4, yaw_r=82, el_r=84, tw_r=62,
         sw_l=20, abd_l=30, yaw_l=-14, head_yaw=-8, th_r=6, knee_r=18, th_l=10, knee_l=18, aim=(115, 28)),
    dict(chest_yaw=6, sw_r=84, abd_r=16, yaw_r=14, el_r=36, tw_r=40, aim=(30, 4)),
    dict(chest_yaw=-30, pelvis_yaw=-12, lean=17, sw_r=80, abd_r=42, yaw_r=-50, el_r=12, tw_r=20,
         sw_l=30, abd_l=16, yaw_l=26, head_yaw=8, th_r=-10, knee_r=22, aim=(-48, -6)),
    dict(chest_yaw=-42, pelvis_yaw=-14, lean=15, sw_r=62, abd_r=56, yaw_r=-74, el_r=26, sw_l=34,
         yaw_l=30, aim=(-92, -14)),
)
SLASH3 = [(0.0, {}),
          (0.34, dict(lean=-4, chest_yaw=-14, sw_r=172, abd_r=18, yaw_r=6, el_r=104, tw_r=-6,
                      head_pitch=-12, sw_l=62, abd_l=26, el_l=40, th_l=18, knee_l=18, th_r=-10, knee_r=12,
                      aim=(175, 35))),
          (0.48, dict(lean=10, chest_yaw=-6, sw_r=128, abd_r=14, el_r=42, head_pitch=-4, sw_l=40, aim=(0, 70))),
          (0.56, dict(lean=30, chest_yaw=6, sw_r=64, abd_r=10, yaw_r=8, el_r=8, head_pitch=10, sw_l=16,
                      abd_l=30, th_l=22, knee_l=36, th_r=-14, knee_r=26, aim=(4, -40))),
          (0.76, dict(lean=33, chest_yaw=8, sw_r=46, abd_r=10, el_r=10, head_pitch=12, sw_l=10,
                      abd_l=32, th_l=22, knee_l=38, th_r=-14, knee_r=28, aim=(6, -62))),
          (1.0, {})]
T8 = [0.0, 0.14, 0.28, 0.42, 0.52, 0.62, 0.76, 0.90]
TK8 = [3, 3, 4, 2, 2, 3, 4, 5]
anim('slash1', keyed(STANCE, SLASH1, T8), TK8, hit=4)
anim('slash2', keyed(STANCE, SLASH2, T8), TK8, hit=4)
anim('slash3', keyed(STANCE, SLASH3, [0.0, 0.14, 0.26, 0.36, 0.48, 0.56, 0.66, 0.80, 0.92]),
     [3, 3, 4, 4, 2, 2, 4, 5, 6], hit=5)

# ---------------------------------------------------------------- opening the saw cleaver
# a flick of the wrist: the blade swings out on its hinge and locks

OPEN = [(0.0, {}),
        (0.30, dict(sw_r=52, abd_r=18, el_r=52, wr_r=-34, tw_r=-10, lean=9, head_pitch=4, blade=178)),
        (0.44, dict(sw_r=62, abd_r=16, el_r=30, wr_r=26, tw_r=-6, blade=124)),
        (0.54, dict(sw_r=58, el_r=26, wr_r=36, blade=44)),
        (0.62, dict(sw_r=50, el_r=28, wr_r=30, blade=-8)),
        (0.74, dict(sw_r=34, el_r=30, wr_r=22, blade=0)),
        (1.0, dict(extended(dict(STAND)), blade=0.0))]
open_frames = keyed(STAND, OPEN, [0.0, 0.16, 0.30, 0.40, 0.48, 0.55, 0.62, 0.76, 0.90])
anim('open', open_frames, [4, 3, 3, 2, 2, 2, 3, 4, 5], lock=6)

# ---------------------------------------------------------------- the saw cleaver, out
# heavier and wider: two sweeps at the height of the waist, then a slam

XFIGHT = dict(FIGHT, lean=14, th_r=-10, knee_r=18, th_l=14, knee_l=20, spread=10, sw_r=14, abd_r=26, el_r=30,
              grip=-24, blade=0.0)
XSTANCE = pose(STAND, **XFIGHT)
XSLASH1 = [(0.0, {}),
           (0.32, dict(chest_yaw=-48, pelvis_yaw=-16, lean=12, sw_r=74, abd_r=70, yaw_r=-62, el_r=40, tw_r=-24,
                       sw_l=40, abd_l=22, yaw_l=30, el_l=50, head_yaw=14, knee_r=24, knee_l=24,
                       aim=(-135, 12))),
           (0.48, dict(chest_yaw=-8, pelvis_yaw=-4, sw_r=80, abd_r=36, yaw_r=-10, el_r=16, tw_r=-8, sw_l=24,
                       knee_r=26, knee_l=28, aim=(-40, -2))),
           (0.58, dict(chest_yaw=34, pelvis_yaw=14, lean=20, sw_r=72, abd_r=14, yaw_r=50, el_r=10, sw_l=6,
                       abd_l=34, yaw_l=-14, knee_r=26, knee_l=30, head_yaw=-8, aim=(45, -10))),
           (0.78, dict(chest_yaw=52, pelvis_yaw=18, lean=18, sw_r=50, abd_r=8, yaw_r=82, el_r=24, sw_l=2,
                       abd_l=38, knee_r=22, knee_l=26, aim=(100, -18))),
           (1.0, {})]
XSLASH2 = [(0.0, {}),
           (0.32, dict(chest_yaw=44, pelvis_yaw=14, lean=13, sw_r=78, abd_r=6, yaw_r=86, el_r=70, tw_r=60,
                       sw_l=18, abd_l=32, yaw_l=-16, head_yaw=-10, knee_r=22, knee_l=22, aim=(130, 16))),
           (0.48, dict(chest_yaw=8, sw_r=82, abd_r=18, yaw_r=16, el_r=24, tw_r=36, knee_r=26, knee_l=26,
                       aim=(35, -2))),
           (0.58, dict(chest_yaw=-36, pelvis_yaw=-14, lean=19, sw_r=76, abd_r=48, yaw_r=-52, el_r=10, tw_r=18,
                       sw_l=32, abd_l=18, yaw_l=28, head_yaw=10, knee_r=28, knee_l=24, aim=(-55, -10))),
           (0.78, dict(chest_yaw=-52, pelvis_yaw=-18, lean=17, sw_r=56, abd_r=64, yaw_r=-80, el_r=22, sw_l=38,
                       yaw_l=34, knee_r=24, knee_l=22, aim=(-105, -18))),
           (1.0, {})]
XSLASH3 = [(0.0, {}),
           (0.36, dict(lean=-8, chest_yaw=-18, sw_r=176, abd_r=14, yaw_r=4, el_r=60, tw_r=-6, head_pitch=-16,
                       sw_l=70, abd_l=30, el_l=36, th_l=20, knee_l=16, th_r=-12, knee_r=10, aim=(180, 50))),
           (0.50, dict(lean=12, chest_yaw=-6, sw_r=134, abd_r=12, el_r=24, head_pitch=-4, sw_l=44,
                       aim=(0, 78))),
           (0.58, dict(lean=36, chest_yaw=4, sw_r=62, abd_r=8, yaw_r=6, el_r=6, head_pitch=14, sw_l=20, abd_l=34,
                       th_l=30, knee_l=52, th_r=-18, knee_r=40, aim=(2, -48))),
           (0.80, dict(lean=40, chest_yaw=6, sw_r=48, abd_r=8, el_r=6, head_pitch=16, sw_l=14, abd_l=36,
                       th_l=30, knee_l=54, th_r=-18, knee_r=42, aim=(4, -60))),
           (1.0, {})]
anim('xslash1', keyed(XSTANCE, XSLASH1, [0.0, 0.12, 0.24, 0.34, 0.44, 0.52, 0.60, 0.72, 0.86], True),
     [3, 3, 4, 4, 2, 2, 3, 5, 6], ext=True, hit=5)
anim('xslash2', keyed(XSTANCE, XSLASH2, [0.0, 0.12, 0.24, 0.34, 0.44, 0.52, 0.60, 0.72, 0.86], True),
     [3, 3, 4, 4, 2, 2, 3, 5, 6], ext=True, hit=5)
anim('xslash3', keyed(XSTANCE, XSLASH3, [0.0, 0.12, 0.24, 0.36, 0.46, 0.53, 0.59, 0.70, 0.82, 0.93], True),
     [4, 4, 4, 5, 3, 2, 3, 5, 6, 6], ext=True, hit=6)

# ---------------------------------------------------------------- hurt, three ways
# a blow in the chest (head snaps back), one from the side (twisted round),
# one in the gut (doubled over)

HURT1 = [(0.0, {}),
         (0.18, dict(pitch=7, lean=-20, head_pitch=-30, head_roll=6, sw_r=-18, abd_r=44, el_r=44, sw_l=-14,
                     abd_l=46, el_l=48, knee_r=20, knee_l=16, th_r=-10, th_l=8, cape=-8)),
         (0.40, dict(pitch=3, lean=-8, head_pitch=-12, sw_r=-8, abd_r=30, el_r=34, sw_l=-6, abd_l=30, knee_r=16,
                     knee_l=12)),
         (1.0, {})]
HURT2 = [(0.0, {}),
         (0.18, dict(chest_yaw=32, pelvis_yaw=10, chest_roll=-14, lean=4, head_yaw=30, head_roll=-14,
                     sw_r=30, abd_r=50, el_r=50, sw_l=-20, abd_l=20, el_l=30, knee_r=20, knee_l=10, th_r=8,
                     roll=-4)),
         (0.42, dict(chest_yaw=18, chest_roll=-6, head_yaw=14, sw_r=12, abd_r=32, knee_r=14)),
         (1.0, {})]
HURT3 = [(0.0, {}),
         (0.20, dict(lean=38, head_pitch=18, sw_r=34, abd_r=6, yaw_r=24, el_r=76, sw_l=40, abd_l=4, yaw_l=34,
                     el_l=86, knee_r=30, knee_l=28, th_r=10, th_l=12, cape=6)),
         (0.50, dict(lean=30, head_pitch=14, sw_r=22, yaw_r=18, el_r=60, sw_l=30, yaw_l=26, el_l=70, knee_r=24,
                     knee_l=22)),
         (1.0, {})]
for ext in (False, True):
    sx = '_x' if ext else ''
    base = extended(dict(STAND)) if ext else STAND
    anim('hurt1' + sx, keyed(base, HURT1, [0.10, 0.18, 0.30, 0.50, 0.75], ext), [3, 4, 5, 6, 6], ext=ext)
    anim('hurt2' + sx, keyed(base, HURT2, [0.10, 0.18, 0.30, 0.50, 0.75], ext), [3, 4, 5, 6, 6], ext=ext)
    anim('hurt3' + sx, keyed(base, HURT3, [0.10, 0.20, 0.34, 0.50, 0.66, 0.84], ext), [3, 5, 5, 6, 6, 6],
         ext=ext)

# ---------------------------------------------------------------- knocked down, getting up
# thrown back off the feet, a hard landing on the back, a bounce, lying
# still; then up on an elbow, a knee, and back on the feet

LIE = dict(pitch=88, air=0, lean=0, head_pitch=-6, head_yaw=18, sw_r=-4, abd_r=74, el_r=26, sw_l=8, abd_l=64,
           el_l=40, th_r=12, knee_r=26, th_l=4, knee_l=18, spread=9, cape=-10)
DOWN = [(0.0, {}),
        (0.12, dict(lean=-16, head_pitch=-24, pitch=6, sw_r=10, abd_r=46, el_r=40, sw_l=14, abd_l=48, el_l=40,
                    knee_r=14, knee_l=14)),
        (0.32, dict(pitch=42, air=7, lean=-10, head_pitch=-14, sw_r=110, abd_r=60, el_r=40, sw_l=118, abd_l=56,
                    el_l=40, th_r=34, knee_r=40, th_l=26, knee_l=46, cape=-14)),
        (0.48, dict(pitch=72, air=5, lean=-6, sw_r=50, abd_r=72, sw_l=56, abd_l=66, th_r=22, knee_r=30, th_l=16,
                    knee_l=30, cape=-14)),
        (0.60, dict(LIE, head_pitch=-14, knee_r=20, knee_l=14)),
        (0.72, dict(LIE, pitch=82, air=2.5, head_pitch=-10, knee_r=34, knee_l=24)),
        (0.86, LIE),
        (1.0, LIE)]
GETUP = [(0.0, LIE),
         (0.22, dict(pitch=56, lean=4, head_pitch=6, head_yaw=4, sw_r=-30, abd_r=24, el_r=20, sw_l=-36, abd_l=26,
                     el_l=16, th_r=60, knee_r=90, th_l=20, knee_l=40, spread=8, cape=-6)),
         (0.46, dict(pitch=10, lean=24, head_pitch=6, sw_r=10, abd_r=22, el_r=50, sw_l=30, abd_l=10, el_l=70,
                     th_l=78, knee_l=88, th_r=-8, knee_r=100, spread=6)),
         (0.70, dict(pitch=2, lean=16, sw_r=4, abd_r=18, el_r=34, sw_l=12, el_l=40, th_l=40, knee_l=48, th_r=-4,
                     knee_r=40)),
         (1.0, {})]
for ext in (False, True):
    sx = '_x' if ext else ''
    base = extended(dict(STAND)) if ext else STAND
    anim('down' + sx, keyed(base, DOWN, [0.0, 0.12, 0.24, 0.36, 0.48, 0.60, 0.72, 0.86], ext),
         [3, 3, 3, 3, 3, 4, 6, 20], ext=ext)
    anim('getup' + sx, keyed(base, GETUP, [0.08, 0.22, 0.36, 0.48, 0.62, 0.78, 0.92], ext),
         [6, 6, 6, 6, 5, 5, 6], ext=ext)

# ---------------------------------------------------------------- death
# struck, doubled over, down on the knees, a slump, and face down on the stones

KNEEL = dict(lean=24, head_pitch=24, th_r=2, knee_r=96, th_l=2, knee_l=96, spread=7, sw_r=6, abd_r=16, el_r=10,
             sw_l=4, abd_l=14, el_l=12)
FALLEN = dict(pitch=-86, lean=6, head_pitch=4, head_yaw=70, head_roll=10, th_r=-4, knee_r=14, th_l=6, knee_l=24,
              sw_r=150, abd_r=40, el_r=24, sw_l=160, abd_l=60, el_l=50, spread=8, cape=6)
DEATH = [(0.0, {}),
         (0.08, dict(lean=-14, head_pitch=-24, sw_r=-10, abd_r=40, el_r=40, sw_l=-8, abd_l=40, el_l=40, knee_r=14,
                     knee_l=14)),
         (0.22, dict(lean=34, head_pitch=20, sw_r=6, abd_r=14, el_r=12, sw_l=46, yaw_l=40, el_l=96, abd_l=6,
                     knee_r=34, knee_l=30, th_r=12, th_l=14)),
         (0.40, KNEEL),
         (0.55, dict(KNEEL, lean=42, head_pitch=34)),
         (0.70, dict(KNEEL, pitch=-34, lean=40, head_pitch=26, sw_r=30, sw_l=30)),
         (0.84, dict(FALLEN, pitch=-80, head_yaw=40)),
         (0.92, dict(FALLEN, pitch=-84, air=1.5)),
         (1.0, FALLEN)]
for ext in (False, True):
    sx = '_x' if ext else ''
    base = extended(dict(STAND)) if ext else STAND
    anim('death' + sx, keyed(base, DEATH, [0.0, 0.08, 0.16, 0.26, 0.38, 0.48, 0.58, 0.68, 0.78, 0.86, 0.93,
                                           1.0], ext),
         [3, 4, 4, 5, 6, 8, 8, 5, 4, 3, 4, 60], ext=ext)

# ---------------------------------------------------------------- backstep
# a crouch, a hop backwards (the coat flies forward), a landing on bent knees

BACKSTEP = [(0.0, {}),
            (0.14, dict(lean=16, knee_r=30, knee_l=28, th_r=8, th_l=10, sw_r=6, sw_l=8, head_pitch=2)),
            (0.30, dict(air=5, lean=18, knee_r=10, knee_l=12, th_r=-16, th_l=8, sw_r=34, sw_l=36, el_r=40,
                        el_l=40, skirt_r=26, skirt_l=-26, cape=8)),
            (0.50, dict(air=7, lean=12, knee_r=40, knee_l=36, th_r=22, th_l=26, sw_r=26, sw_l=30, skirt_r=30,
                        skirt_l=-30, cape=10)),
            (0.70, dict(air=0, lean=20, knee_r=38, knee_l=34, th_r=14, th_l=8, sw_r=20, sw_l=24, skirt_r=18,
                        skirt_l=-18, cape=4)),
            (0.86, dict(lean=14, knee_r=20, knee_l=18)),
            (1.0, {})]
for ext in (False, True):
    sx = '_x' if ext else ''
    base = extended(dict(STAND)) if ext else STAND
    anim('backstep' + sx, keyed(base, BACKSTEP, [0.0, 0.14, 0.30, 0.42, 0.56, 0.70, 0.86], ext),
         [3, 3, 4, 4, 4, 5, 6], ext=ext)

# ---------------------------------------------------------------- quicksteps and the roll
# with a target held: a short dash ahead, left or right (back is the
# backstep), low and fast, the coat flying after; without one: a roll

QSTEP = {
    'f': [(0.0, {}),
          (0.20, dict(lean=20, knee_r=26, knee_l=24, th_r=10, th_l=-6, sw_r=10, sw_l=-10, head_pitch=4)),
          (0.42, dict(air=2.5, lean=26, knee_r=18, knee_l=40, th_r=30, th_l=-26, sw_r=-14, sw_l=26, el_r=40,
                      el_l=40, skirt_r=-20, skirt_l=10, cape=12)),
          (0.66, dict(air=0.5, lean=18, knee_r=30, knee_l=30, th_r=16, th_l=-10, skirt_r=-8, skirt_l=4, cape=6)),
          (0.84, dict(lean=12, knee_r=18, knee_l=16)),
          (1.0, {})],
    'l': [(0.0, {}),
          (0.20, dict(lean=12, roll=-6, knee_r=24, knee_l=24, tha_l=6, head_yaw=10)),
          (0.42, dict(air=2.5, lean=14, roll=10, chest_roll=-6, knee_r=34, knee_l=20, tha_l=26, tha_r=-8, abd_r=30,
                      abd_l=40, el_r=40, el_l=30, head_yaw=14, skirt_r=16, skirt_l=-20, cape=10)),
          (0.66, dict(air=0.5, lean=12, roll=4, knee_r=28, knee_l=30, tha_l=14, abd_l=24, cape=5)),
          (0.84, dict(lean=10, knee_r=16, knee_l=18, tha_l=4)),
          (1.0, {})],
    'r': [(0.0, {}),
          (0.20, dict(lean=12, roll=6, knee_r=24, knee_l=24, tha_r=6, head_yaw=-10)),
          (0.42, dict(air=2.5, lean=14, roll=-10, chest_roll=6, knee_r=20, knee_l=34, tha_r=26, tha_l=-8, abd_r=40,
                      abd_l=30, el_r=30, el_l=40, head_yaw=-14, skirt_r=20, skirt_l=-16, cape=10)),
          (0.66, dict(air=0.5, lean=12, roll=-4, knee_r=30, knee_l=28, tha_r=14, abd_r=24, cape=5)),
          (0.84, dict(lean=10, knee_r=18, knee_l=16, tha_r=4)),
          (1.0, {})],
}
TUCK = dict(knee_r=120, knee_l=116, th_r=70, th_l=74, sw_r=60, sw_l=60, el_r=90, el_l=90, abd_r=20, abd_l=20,
            head_pitch=30, lean=30, cape=-10, skirt_r=60, skirt_l=60)
ROLL = [(0.0, {}),
        (0.16, dict(lean=30, knee_r=40, knee_l=40, th_r=20, th_l=24, sw_r=30, sw_l=30, head_pitch=14)),
        (0.30, dict(TUCK, pitch=-80, air=1.0)),
        (0.46, dict(TUCK, pitch=-170, air=2.0)),
        (0.62, dict(TUCK, pitch=-270, air=1.0)),
        (0.78, dict(lean=26, pitch=-350, knee_r=60, knee_l=50, th_r=40, th_l=20, sw_r=20, sw_l=20, head_pitch=10)),
        (1.0, dict(pitch=-360))]
for ext in (False, True):
    sx = '_x' if ext else ''
    base = extended(dict(STAND)) if ext else STAND
    for k, keys in QSTEP.items():
        anim('qstep_' + k + sx, keyed(base, keys, [0.0, 0.18, 0.36, 0.52, 0.70, 0.88], ext), [2, 2, 3, 3, 3, 4],
             ext=ext)
    frames = keyed(base, ROLL, [0.08, 0.22, 0.34, 0.46, 0.58, 0.70, 0.86], ext)
    for f in frames:
        f['pitch'] = f['pitch'] % 360.0 if f['pitch'] < -1 else f['pitch']
    anim('roll' + sx, frames, [3, 3, 3, 3, 3, 4, 5], ext=ext)

# ---------------------------------------------------------------- the heavy blow, charged
# drawn back over the right shoulder (held there while the button is held:
# the charge), then down hard across the body

HEAVY = [(0.0, {}),
         (0.26, dict(chest_yaw=-40, pelvis_yaw=-16, lean=2, head_pitch=-10, head_yaw=16, sw_r=150, abd_r=40,
                     yaw_r=-20, el_r=110, tw_r=-20, sw_l=40, abd_l=30, yaw_l=30, el_l=60, th_r=-14, knee_r=24,
                     th_l=16, knee_l=20, spread=10, aim=(-150, 50))),
         (0.40, dict(chest_yaw=-44, pelvis_yaw=-18, lean=0, head_pitch=-12, head_yaw=18, sw_r=158, abd_r=42,
                     yaw_r=-22, el_r=116, tw_r=-22, sw_l=42, abd_l=32, yaw_l=32, el_l=62, th_r=-16, knee_r=28,
                     th_l=18, knee_l=24, spread=11, aim=(-155, 55))),
         (0.54, dict(chest_yaw=-4, pelvis_yaw=0, lean=24, sw_r=110, abd_r=20, yaw_r=10, el_r=30, sw_l=30,
                     th_l=26, knee_l=34, th_r=-18, knee_r=20, aim=(0, 30))),
         (0.62, dict(chest_yaw=30, pelvis_yaw=10, lean=34, head_pitch=10, sw_r=60, abd_r=16, yaw_r=40, el_r=10,
                     sw_l=10, abd_l=36, th_l=30, knee_l=46, th_r=-20, knee_r=28, aim=(40, -40))),
         (0.80, dict(chest_yaw=36, pelvis_yaw=12, lean=34, head_pitch=12, sw_r=50, abd_r=14, yaw_r=50, el_r=16,
                     abd_l=36, th_l=30, knee_l=46, th_r=-20, knee_r=28, aim=(60, -60))),
         (1.0, {})]
XHEAVY = [(0.0, {}),
          (0.26, dict(lean=-6, chest_yaw=-20, head_pitch=-16, sw_r=172, abd_r=16, el_r=70, sw_l=80, abd_l=30,
                      el_l=50, th_r=-14, knee_r=22, th_l=18, knee_l=18, spread=10, aim=(180, 40))),
          (0.40, dict(lean=-8, chest_yaw=-22, head_pitch=-18, sw_r=176, abd_r=18, el_r=74, sw_l=84, abd_l=32,
                      el_l=52, th_r=-16, knee_r=26, th_l=20, knee_l=22, spread=11, aim=(180, 46))),
          (0.54, dict(lean=16, chest_yaw=-6, sw_r=130, abd_r=12, el_r=24, sw_l=50, aim=(0, 70))),
          (0.62, dict(lean=40, chest_yaw=6, head_pitch=14, sw_r=60, abd_r=8, el_r=6, sw_l=20, abd_l=36, th_l=34,
                      knee_l=58, th_r=-20, knee_r=44, aim=(2, -54))),
          (0.80, dict(lean=42, chest_yaw=6, head_pitch=16, sw_r=48, abd_r=8, el_r=6, sw_l=14, abd_l=36, th_l=34,
                      knee_l=58, th_r=-20, knee_r=44, aim=(4, -62))),
          (1.0, {})]
HT = [0.0, 0.14, 0.26, 0.40, 0.50, 0.57, 0.63, 0.74, 0.88]
anim('heavy', keyed(STANCE, HEAVY, HT), [4, 4, 5, 4, 3, 2, 3, 6, 7], hit=6, charge=3)
anim('heavy_x', keyed(XSTANCE, XHEAVY, HT, True), [4, 4, 5, 4, 3, 2, 3, 6, 7], ext=True, hit=6, charge=3)

# ---------------------------------------------------------------- the trick: transforming in a blow
# folded to out: a rising sweep that flings the blade open on its hinge;
# out to folded: a chop that snaps it shut on the way down

TRICK = [(0.0, {}),
         (0.24, dict(chest_yaw=-30, pelvis_yaw=-10, lean=14, sw_r=40, abd_r=50, yaw_r=-50, el_r=70, wr_r=-30,
                     knee_r=22, knee_l=20, blade=178, aim=(-110, -20))),
         (0.40, dict(chest_yaw=-6, lean=10, sw_r=80, abd_r=30, yaw_r=-10, el_r=30, blade=120, aim=(-40, 10))),
         (0.50, dict(chest_yaw=20, pelvis_yaw=8, lean=16, sw_r=96, abd_r=16, yaw_r=30, el_r=12, blade=40,
                     aim=(30, 26))),
         (0.60, dict(chest_yaw=36, pelvis_yaw=12, lean=18, sw_r=90, abd_r=8, yaw_r=60, el_r=20, blade=0, grip=-24,
                     aim=(80, 30))),
         (0.80, dict(chest_yaw=30, lean=14, sw_r=60, abd_r=12, yaw_r=50, el_r=30, blade=0, grip=-24, aim=(90, 0))),
         (1.0, dict(XFIGHT))]
TRICK_X = [(0.0, {}),
           (0.26, dict(lean=-4, chest_yaw=-16, head_pitch=-14, sw_r=168, abd_r=16, el_r=70, sw_l=60, abd_l=30,
                       el_l=40, blade=0, grip=-24, aim=(175, 40))),
           (0.44, dict(lean=14, chest_yaw=-4, sw_r=120, abd_r=12, el_r=30, blade=0, grip=-24, aim=(0, 60))),
           (0.54, dict(lean=30, chest_yaw=6, head_pitch=10, sw_r=66, abd_r=10, el_r=8, blade=60, grip=-12,
                       th_l=24, knee_l=36, th_r=-14, knee_r=24, aim=(4, -40))),
           (0.66, dict(lean=30, chest_yaw=8, sw_r=50, abd_r=10, el_r=12, blade=150, grip=0, th_l=24, knee_l=36,
                       th_r=-14, knee_r=24, aim=(6, -56))),
           (0.82, dict(lean=20, sw_r=40, el_r=30, blade=180, grip=0, aim=(10, -40))),
           (1.0, dict(FIGHT, blade=180.0))]
TT = [0.0, 0.14, 0.28, 0.40, 0.48, 0.56, 0.66, 0.80, 0.92]
anim('trick', keyed(STANCE, TRICK, TT), [3, 3, 4, 3, 2, 2, 3, 5, 6], hit=5, lock=6)
anim('trick_x', keyed(XSTANCE, TRICK_X, TT), [3, 3, 4, 3, 2, 2, 3, 5, 6], ext=True, hit=5, lock=6)

# ---------------------------------------------------------------- the visceral attack
# the left hand driven into the staggered prey, a twist, torn out in a
# spray of blood, the hunter thrown back a step by it

VISCERAL = [(0.0, {}),
            (0.16, dict(chest_yaw=24, pelvis_yaw=8, lean=8, sw_l=-30, abd_l=20, el_l=100, sw_r=-10, abd_r=30,
                        th_l=-6, knee_l=10, th_r=10, knee_r=14)),
            (0.28, dict(chest_yaw=-30, pelvis_yaw=-12, lean=24, sw_l=92, abd_l=4, yaw_l=6, el_l=4, wr_l=-10, sw_r=-14,
                        abd_r=34, th_l=28, knee_l=34, th_r=-18, knee_r=16, head_pitch=6)),
            (0.44, dict(chest_yaw=-28, pelvis_yaw=-12, lean=22, sw_l=96, abd_l=6, el_l=10, tw_l=60, sw_r=-14,
                        abd_r=34, th_l=28, knee_l=34, th_r=-18, knee_r=16)),
            (0.58, dict(chest_yaw=-20, pelvis_yaw=-8, lean=16, sw_l=110, abd_l=10, el_l=24, tw_l=80, sw_r=-10,
                        abd_r=34, th_l=26, knee_l=30, th_r=-16, knee_r=16, head_pitch=-6)),
            (0.68, dict(chest_yaw=20, pelvis_yaw=6, lean=-10, head_pitch=-14, sw_l=40, abd_l=40, el_l=90, tw_l=0,
                        sw_r=-20, abd_r=40, th_l=10, knee_l=20, th_r=-6, knee_r=24)),
            (0.82, dict(chest_yaw=24, lean=-4, sw_l=20, abd_l=46, el_l=70, abd_r=36, knee_l=18, knee_r=20)),
            (1.0, {})]
VT = [0.0, 0.12, 0.22, 0.30, 0.40, 0.50, 0.58, 0.64, 0.72, 0.84, 0.94]
for ext in (False, True):
    sx = '_x' if ext else ''
    base = extended(dict(STAND)) if ext else STAND
    anim('visceral' + sx, keyed(base, VISCERAL, VT, ext), [3, 3, 3, 4, 8, 6, 3, 3, 5, 6, 8], ext=ext, hit=7)
