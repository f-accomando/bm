"""What the creatures of the town (art/foe_*.py) share: a humanoid skeleton
like the hunter's with its own proportions, key poses sampled into frames
(as in anims.py), the wrist turned so a weapon points somewhere, and the
render of a frame with the points the game needs (the tip of a weapon, the
flame of a torch...).

The creatures are drawn in 5 directions (S SE E NE N) and mirrored for the
other 3 (SW W NW), so they are lit from above (sdf.LIGHT_TOP), the same on
both sides.
"""
import math

import numpy as np

from sdf import D, LIGHT_TOP, RIM_TOP, Skel, camera, crop, pixelize, render, rx, ry, rz, INK

DIRS5 = [('S', (0, -1)), ('SE', (1, -1)), ('E', (1, 0)), ('NE', (1, 1)), ('N', (0, 1))]

# ---------------------------------------------------------------- humanoid

# proportions of the hunter, in world units (a unit is a pixel)
HUMAN = dict(thigh=12.6, shin=11.6, waist=4.0, chest=11.6, neck=14.0, shoulder=6.3, hipw=3.1, uarm=10.0,
             farm=9.0, hand=1.3, head_r=3.4, foot=1.9, cape=14.6, skirt=4.2)

# the joints, in degrees (the same as the hunter's, hunter.BASE): ty forward,
# air off the ground, pitch (backwards +), roll, yaw (to its left +); arms:
# sw forward, abd sideways, yaw across the body, roll, el elbow, tw forearm
# twist, wr / wd the wrist; legs: th thigh forward, tha sideways, knee, toe.
# grip / grip_l turn what is in the hands; jaw opens the mouth.
POSE = dict(ty=0.0, air=0.0, pitch=0.0, roll=0.0, yaw=0.0, pelvis_yaw=0.0, pelvis_roll=0.0,
            lean=7.0, chest_yaw=0.0, chest_roll=0.0, head_pitch=-4.0, head_yaw=0.0, head_roll=0.0,
            breath=0.0, sway=0.0, cape=0.0,
            sw_r=-4.0, abd_r=14.0, yaw_r=0.0, roll_r=0.0, el_r=22.0, tw_r=0.0, wr_r=0.0, wd_r=0.0,
            sw_l=2.0, abd_l=13.0, yaw_l=0.0, roll_l=0.0, el_l=26.0, tw_l=0.0, wr_l=0.0, wd_l=0.0,
            th_r=5.0, tha_r=0.0, knee_r=6.0, toe_r=0.0,
            th_l=-3.0, tha_l=0.0, knee_l=4.0, toe_l=0.0,
            spread=4.5, grip=0.0, grip_l=0.0, jaw=0.0, skirt_r=None, skirt_l=None)


def pose(base=None, **kw):
    p = dict(base or POSE)
    p.update(kw)
    return floats(p)


def humanoid(p, pr=HUMAN, lift=0.0):
    """the bones of a humanoid in pose p with proportions pr"""
    a = lambda k: D(p[k])
    S = Skel()
    hip = pr['thigh'] + pr['shin'] + pr['foot']
    S.add('body', 'root', [0, p['ty'], lift], rz(a('yaw')) @ rx(a('pitch')) @ ry(a('roll')))
    S.add('pelvis', 'body', [0, 0, hip], rz(a('pelvis_yaw')) @ ry(a('pelvis_roll')))
    S.add('chest', 'pelvis', [0, 0, pr['waist']], rz(a('chest_yaw')) @ ry(a('chest_roll')) @ rx(-a('lean')))
    S.add('head', 'chest', [0, 0.6, pr['neck'] + 0.35 * p['breath']],
          rz(a('head_yaw') - 0.4 * a('chest_yaw')) @ ry(a('head_roll')) @ rx(a('head_pitch') + 0.6 * a('lean')))
    S.add('jaw', 'head', [0, 0.8, 2.6], rx(a('jaw')))
    S.add('cape', 'chest', [0, 0, pr['cape']], rx(-a('cape')))
    for side, sg in (('r', 1), ('l', -1)):
        S.add('uarm_' + side, 'chest', [sg * pr['shoulder'], -0.3, pr['chest'] + 0.3 * p['breath']],
              rz(sg * a('yaw_' + side)) @ ry(-sg * a('abd_' + side)) @ rx(a('sw_' + side) + 0.5 * a('lean'))
              @ rz(sg * a('roll_' + side)))
        S.add('farm_' + side, 'uarm_' + side, [0, 0, -pr['uarm']], rx(a('el_' + side)) @ ry(sg * D(6))
              @ rz(sg * a('tw_' + side)))
        S.add('hand_' + side, 'farm_' + side, [0, 0, -pr['farm']], rx(a('wr_' + side)) @ ry(sg * a('wd_' + side)))
        S.add('thigh_' + side, 'pelvis', [sg * pr['hipw'], 0, -0.6],
              ry(-sg * D(p['spread'] + p['tha_' + side])) @ rx(a('th_' + side)))
        S.add('shin_' + side, 'thigh_' + side, [0, 0, -pr['thigh']], rx(-a('knee_' + side)))
        foot = p['knee_' + side] - p['th_' + side] + p['toe_' + side]
        S.add('foot_' + side, 'shin_' + side, [0, 0, -pr['shin']], rz(-sg * D(8)) @ rx(D(foot)))
        sk = p['skirt_' + side]
        th = D(sk) if sk is not None else 0.62 * a('th_' + side)
        S.add('skirt_' + side, 'pelvis', [sg * 2.0, 0.3, pr['skirt']], ry(-sg * D(7)) @ rx(th + 0.5 * a('sway')))
    S.add('skirt_b', 'pelvis', [0, -0.8, pr['skirt']],
          rx(-D(3) - abs(a('sway')) * 0.8 - 0.25 * abs(a('th_r'))))
    # what the hands hold: along -z of these bones, the grip at the origin
    S.add('wpn_r', 'hand_r', [0.2, 0.5, -pr['hand']], rz(D(-18)) @ rx(D(46 + p['grip'])) @ ry(D(-8)))
    S.add('wpn_l', 'hand_l', [-0.2, 0.5, -pr['hand']], rz(D(18)) @ rx(D(46 + p['grip_l'])) @ ry(D(8)))
    return S


def skel_of(pr):
    """humanoid with these proportions, as skel(p, lift)"""
    return lambda p, lift=0.0: humanoid(p, pr, lift)


def standing(pr, extra=()):
    """build(p) -> the skeleton of pose p on the ground"""
    sk = skel_of(pr)
    cont = human_contacts(pr) + list(extra)
    return lambda p: grounded(sk, p, cont)


def human_contacts(pr=HUMAN):
    """points of a humanoid that can touch the ground"""
    k = pr['chest'] / 11.6
    return [('foot_r', (0, -1.4, -1.9)), ('foot_r', (0, 3.6, -1.9)), ('foot_l', (0, -1.4, -1.9)),
            ('foot_l', (0, 3.6, -1.9)), ('shin_r', (0, 0.6, 0.6)), ('shin_l', (0, 0.6, 0.6)),
            ('pelvis', (0, -4.2, 0.5)), ('pelvis', (0, 3.8, 0.5)), ('chest', (0, -4.4 * k, 6.0 * k)),
            ('chest', (0, 4.0 * k, 6.0 * k)), ('chest', (5.5 * k, 0, 10 * k)), ('chest', (-5.5 * k, 0, 10 * k)),
            ('head', (0, -3.0, 5.0)), ('head', (0, 3.4, 4.5)), ('hand_r', (0, 0, -1.6)),
            ('hand_l', (0, 0, -1.6)), ('farm_r', (0, 0, 0)), ('farm_l', (0, 0, 0))]


def grounded(skel, p, contacts):
    """skel(p, lift) resting on z = 0 on whatever touches it, lifted by air"""
    S = skel(p, 0.0)
    low = min(S.pt(b, q)[2] for b, q in contacts)
    return skel(p, -low + p.get('air', 0.0))


# ---------------------------------------------------------------- key poses

def sample(keys, t):
    """keys: [(time, pose)], times rising; -> the pose at time t, through a
    cubic curve (Hermite, tangents from the neighbours, still at both ends)"""
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
    for k, a in p1.items():
        b = p2.get(k)
        if isinstance(a, float) and isinstance(b, float):
            m1 = 0.0 if i == 0 else (p2[k] - p0[k]) / (t2 - t0)
            m2 = 0.0 if i + 2 >= len(keys) else (p3[k] - p1[k]) / (t3 - t1)
            out[k] = h00 * a + h10 * h * m1 + h01 * b + h11 * h * m2
    for k in ('skirt_r', 'skirt_l'):
        if k not in p1:
            continue
        a, b = p1[k], p2[k]
        if a is None and b is None:
            out[k] = None
        else:
            a = 0.62 * p1['th_' + k[-1]] if a is None else a
            b = 0.62 * p2['th_' + k[-1]] if b is None else b
            out[k] = a + (b - a) * (3 * u * u - 2 * u ** 3)
    return out


def floats(p):
    """ints in a pose become floats, so they are interpolated"""
    return {k: float(v) if isinstance(v, int) and not isinstance(v, bool) else v for k, v in p.items()}


def aim(p, az, el, skel=humanoid, bone='wpn_r', side='r'):
    """turns the wrist so what the hand holds points at azimuth az (0 ahead of
    the body, +90 to its left) and elevation el (degrees)"""
    a, e = math.radians(az), math.radians(el)
    want = np.array([-math.sin(a) * math.cos(e), math.cos(a) * math.cos(e), math.sin(e)])
    wr, wd = 'wr_' + side, 'wd_' + side

    def err(q):
        S = skel(q)
        d = S.b[bone][0] @ np.array([0, 0, -1.0])
        return -float(d @ (S.b['body'][0] @ want))
    best = None
    for x in range(-90, 91, 10):
        for y in range(-70, 71, 10):
            v = err(dict(p, **{wr: float(x), wd: float(y)}))
            if best is None or v < best[0]:
                best = (v, x, y)
    _, x0, y0 = best
    for x in range(x0 - 8, x0 + 9, 2):
        for y in range(y0 - 8, y0 + 9, 2):
            v = err(dict(p, **{wr: float(x), wd: float(y)}))
            if v < best[0]:
                best = (v, x, y)
    p[wr], p[wd] = float(best[1]), float(best[2])
    return p


def reach(p, target, side='l', skel=humanoid, at=(0, 0.3, -1.1)):
    """moves an arm (swing, lift, yaw, elbow) so its hand is at target(S), a
    point of the world: the other hand on a long shaft, both on a hilt"""
    keys = ['sw_' + side, 'abd_' + side, 'yaw_' + side, 'el_' + side]
    lim = {keys[0]: (-80, 190), keys[1]: (-30, 120), keys[2]: (-80, 80), keys[3]: (0, 150)}

    def err(q):
        S = skel(q)
        return float(np.sum((S.pt('hand_' + side, at) - target(S)) ** 2))
    q = dict(p)
    best = err(q)
    for step in (16.0, 8.0, 4.0, 2.0, 1.0):
        better = True
        while better:
            better = False
            for k in keys:
                for sgn in (1, -1):
                    v = q[k] + sgn * step
                    if not lim[k][0] <= v <= lim[k][1]:
                        continue
                    t = dict(q, **{k: v})
                    e = err(t)
                    if e < best - 1e-6:
                        q, best, better = t, e, True
    p.update({k: q[k] for k in keys})
    return p


def keyed(base, keys, times, skel=humanoid, post=None):
    """keys: [(time, {overrides of base})] -> the poses at `times`; a key with
    'aim': (az, el) (or 'aim_l') points the right (left) hand's weapon there;
    post(pose) finishes every frame (a hand put back on a shaft...)"""
    kp = []
    base = floats(base)
    for t, kw in keys:
        kw = floats(kw)
        tr = kw.pop('aim', None)
        tl = kw.pop('aim_l', None)
        p = pose(base, **kw)
        if tr:
            aim(p, tr[0], tr[1], skel, 'wpn_r', 'r')
        if tl:
            aim(p, tl[0], tl[1], skel, 'wpn_l', 'l')
        kp.append((t, p))
    frames = [sample(kp, t) for t in times]
    return [post(f) for f in frames] if post else frames


def spread(n, a=0.0, b=1.0):
    """n times from a to b, both included"""
    return [a + (b - a) * i / (n - 1) for i in range(n)]


def walk_cycle(base, k, n, stride=26.0, arms=1.0, lean=10.0, bounce=1.0):
    """a humanoid walking, frame k of n"""
    p = dict(base)
    ph = 2 * math.pi * k / n
    c, s = math.cos(ph), math.sin(ph)
    A = stride
    p['lean'] = lean
    p['th_r'] = A * c
    p['th_l'] = -A * c
    p['knee_r'] = 6 + 55 * bounce * max(0.0, -s) ** 1.3 + 6 * max(0.0, c)
    p['knee_l'] = 6 + 55 * bounce * max(0.0, s) ** 1.3 + 6 * max(0.0, -c)
    p['toe_r'] = -18 * max(0.0, math.cos(ph - math.pi + 0.5)) ** 2
    p['toe_l'] = -18 * max(0.0, math.cos(ph + 0.5)) ** 2
    p['spread'] = 2.0
    p['pelvis_yaw'] = 7 * c
    p['chest_yaw'] = -11 * c
    p['head_yaw'] = 4 * c
    p['sw_r'] = base['sw_r'] + arms * (-16 * c)
    p['sw_l'] = base['sw_l'] + arms * (20 * c)
    p['el_r'] = base['el_r'] + arms * 14 * max(0.0, -c)
    p['el_l'] = base['el_l'] + arms * 16 * max(0.0, c)
    p['sway'] = 3 * math.sin(2 * ph + 0.6)
    p['cape'] = base['cape'] + 4 * (0.5 - 0.5 * math.cos(2 * ph + 0.8))
    p['skirt_r'] = 0.62 * A * math.cos(ph - 0.45)
    p['skirt_l'] = -0.62 * A * math.cos(ph - 0.45)
    return p


def breathe(base, k, n, amount=1.0):
    """standing still, frame k of n"""
    p = dict(base)
    ph = 2 * math.pi * k / n
    b = (0.5 - 0.5 * math.cos(ph)) * amount
    p['breath'] = b
    p['lean'] += 1.5 * b
    p['head_pitch'] += -2 * b
    p['sw_r'] += 1.5 * b
    p['sw_l'] += -1.5 * b
    p['el_r'] += 3 * b
    p['el_l'] += 3 * b
    p['sway'] = 1.5 * math.sin(ph) * amount
    p['cape'] += 2.5 * b
    return p


# the usual ways a humanoid is hurt and dies: overrides of its own stance
HURT = [(0.0, {}),
        (0.22, dict(pitch=6, lean=-16, head_pitch=-26, head_roll=6, sw_r=-14, abd_r=40, el_r=40, sw_l=-12,
                    abd_l=42, el_l=44, knee_r=18, knee_l=14, th_r=-8, th_l=8, cape=-6)),
        (0.50, dict(pitch=2, lean=-6, head_pitch=-10, sw_r=-6, abd_r=26, el_r=30, sw_l=-4, abd_l=26, knee_r=14,
                    knee_l=10)),
        (1.0, {})]
KNEEL = dict(lean=24, head_pitch=24, th_r=2, knee_r=96, th_l=2, knee_l=96, spread=7, sw_r=6, abd_r=16, el_r=10,
             sw_l=4, abd_l=14, el_l=12)
FALLEN = dict(pitch=-86, lean=6, head_pitch=4, head_yaw=70, head_roll=10, th_r=-4, knee_r=14, th_l=6, knee_l=24,
              sw_r=150, abd_r=40, el_r=24, sw_l=160, abd_l=60, el_l=50, spread=8, cape=6)
DEATH = [(0.0, {}),
         (0.10, dict(lean=-14, head_pitch=-24, sw_r=-10, abd_r=40, el_r=40, sw_l=-8, abd_l=40, el_l=40, knee_r=14,
                     knee_l=14)),
         (0.26, dict(lean=34, head_pitch=20, sw_r=6, abd_r=14, el_r=12, sw_l=46, yaw_l=40, el_l=96, abd_l=6,
                     knee_r=34, knee_l=30, th_r=12, th_l=14)),
         (0.44, KNEEL),
         (0.62, dict(KNEEL, pitch=-34, lean=40, head_pitch=26, sw_r=30, sw_l=30)),
         (0.80, dict(FALLEN, pitch=-80, head_yaw=40)),
         (0.90, dict(FALLEN, pitch=-84, air=1.5)),
         (1.0, FALLEN)]
# thrown on the back
SUPINE = dict(pitch=88, lean=0, head_pitch=-6, head_yaw=18, sw_r=-4, abd_r=74, el_r=26, sw_l=8, abd_l=64,
              el_l=40, th_r=12, knee_r=26, th_l=4, knee_l=18, spread=9, cape=-10)
DEATH_BACK = [(0.0, {}),
              (0.12, dict(lean=-18, head_pitch=-28, pitch=6, sw_r=10, abd_r=46, el_r=40, sw_l=14, abd_l=48,
                          el_l=40, knee_r=14, knee_l=14)),
              (0.34, dict(pitch=42, air=5, lean=-10, head_pitch=-14, sw_r=100, abd_r=60, el_r=40, sw_l=108,
                          abd_l=56, el_l=40, th_r=30, knee_r=40, th_l=24, knee_l=44, cape=-14)),
              (0.54, dict(pitch=74, air=3, lean=-6, sw_r=50, abd_r=72, sw_l=56, abd_l=66, th_r=20, knee_r=30,
                          th_l=14, knee_l=30, cape=-14)),
              (0.70, dict(SUPINE, head_pitch=-14, knee_r=20, knee_l=14)),
              (0.84, dict(SUPINE, pitch=84, air=1.5, head_pitch=-10, knee_r=30, knee_l=22)),
              (1.0, SUPINE)]


# ---------------------------------------------------------------- render

def render_frame(build, p, facing, points=(), extent=(), ss=3, scale=1.0, outline=INK):
    """build(p) -> (skeleton, model). -> RGBA image cropped, (ax, ay) the
    point on the ground under the creature in it, and each of `points` (bone,
    local point) on the screen, from that point"""
    S, M = build(p)
    M.scale = scale
    pts = np.array([t for (_, t) in S.b.values()] + [S.pt(b, q) for b, q in list(points) + list(extent)])
    pts = pts * scale
    c = (pts.min(0) + pts.max(0)) / 2
    R = float(np.max(np.linalg.norm(pts - c, axis=1))) + 9.0 * scale
    _, r, u, _ = camera(facing)
    W = H = int(2 * R + 8)
    CX = int(round(W / 2 - c @ r))
    CY = int(round(H / 2 + c @ u))
    img, proj = render(M, W, H, CX, CY, facing=facing, bound=(c, R), ss=ss, light=LIGHT_TOP, rim_dir=RIM_TOP)
    rgba = pixelize(img, proj, outline=outline)
    out, (x0, y0) = crop(rgba)
    where = []
    for b, q in points:
        v = S.pt(b, q) * scale
        where.append((float(v @ r), float(-(v @ u))))
    return out, (CX - x0, CY - y0), where


# ---------------------------------------------------------------- the creatures

CREATURES = {}          # name -> Creature, in the order they are made


class Creature:
    """build(p) -> (skeleton, model); points: [(bone, local point)] the game
    is told about in every frame (1: what strikes, 2: a flame, a muzzle...);
    extent: more points the frame must hold (the end of a long weapon)"""

    def __init__(self, name, title, build, points, extent=(), scale=1.0, boss=False, module=None):
        self.name, self.title, self.build, self.points = name, title, build, list(points)
        self.extent, self.scale, self.boss, self.module = list(extent), scale, boss, module
        self.anims = {}
        CREATURES[name] = self

    def anim(self, name, frames, ticks, loop=False, **events):
        """events: name -> frame (0-based) or a list of frames (a combo's blows)"""
        assert len(frames) == len(ticks), (self.name, name)
        self.anims[name] = {'frames': frames, 'ticks': list(ticks), 'loop': loop, 'events': events}

    def render(self, p, d, ss=3):
        return render_frame(self.build, p, DIRS5[d][1], self.points, self.extent, ss=ss, scale=self.scale)
