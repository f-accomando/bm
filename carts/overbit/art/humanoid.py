"""
humanoid.py - the skeleton and the animations shared by Overbit's heroes on
foot: one standard skeleton with proportions per hero, legs by inverse
kinematics, arms that hold the weapon by inverse kinematics (rifle, pistol,
arm cannon, launcher, two-hand blaster, thrown), and the standard set of
clips: idle, walk, run, jump, air, land, crouch, hit, death, victory, and the
upper-body layers aim, fire, reload, melee, throw. Each hero adds its own
parts (models) and its own special clips.

The body faces +z, its right is +x (bones .L at -x), y up, metres.
"""
import math

import rig
from rig import Skeleton, sample, leg_ik


def _sm(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


class Body:
    """proportions; every length scales with the height"""

    def __init__(self, height=1.75, shoulders=0.20, hips=0.10, arm=1.0, leg=1.0, head=1.0, stance=1.0):
        self.h = height
        k = height / 1.75
        self.k = k
        self.hip_y = 0.92 * k * leg + 0.02 * (1 - leg)
        self.knee_y = 0.50 * k * leg
        self.ankle_y = 0.08 * k
        self.waist_y = self.hip_y + 0.10 * k
        self.chest_y = self.hip_y + 0.32 * k
        self.neck_y = self.hip_y + 0.55 * k
        self.head_top = height
        self.shoulder_x = shoulders * k
        self.shoulder_y = self.hip_y + 0.49 * k
        self.hip_x = hips * k
        self.upper = 0.28 * k * arm
        self.lower = 0.25 * k * arm
        self.hand = 0.09 * k
        self.head = head
        self.stance = stance


def skeleton(b, extra=()):
    """the standard skeleton; extra: [(name, parent, head, tail)]"""
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.3 * b.k, 0))
    sk.bone("hips", "root", (0, b.hip_y, 0), (0, b.waist_y + 0.05 * b.k, 0))
    sk.bone("spine", "hips", (0, b.waist_y, 0), (0, b.chest_y, 0))
    sk.bone("chest", "spine", (0, b.chest_y, 0), (0, b.neck_y, 0))
    sk.bone("head", "chest", (0, b.neck_y, 0), (0, b.head_top, 0))
    for s, x in (("L", -1), ("R", 1)):
        sx = x * b.shoulder_x
        sk.bone(f"upperarm.{s}", "chest", (sx, b.shoulder_y, 0), (sx + x * 0.04 * b.k, b.shoulder_y - b.upper, 0))
        ex, ey = sx + x * 0.04 * b.k, b.shoulder_y - b.upper
        sk.bone(f"forearm.{s}", f"upperarm.{s}", (ex, ey, 0), (ex + x * 0.02 * b.k, ey - b.lower, 0.02 * b.k))
        hx, hy = ex + x * 0.02 * b.k, ey - b.lower
        sk.bone(f"hand.{s}", f"forearm.{s}", (hx, hy, 0.02 * b.k), (hx + x * 0.005, hy - b.hand, 0.03 * b.k))
    for s, x in (("L", -1), ("R", 1)):
        hx = x * b.hip_x
        sk.bone(f"thigh.{s}", "hips", (hx, b.hip_y, 0), (hx * 1.08, b.knee_y, 0.01 * b.k))
        sk.bone(f"shin.{s}", f"thigh.{s}", (hx * 1.08, b.knee_y, 0.01 * b.k), (hx * 1.08, b.ankle_y, -0.02 * b.k))
        sk.bone(f"foot.{s}", f"shin.{s}", (hx * 1.08, b.ankle_y, -0.02 * b.k), (hx * 1.08, 0.03 * b.k, 0.16 * b.k))
    for name, parent, head, tail in extra:
        sk.bone(name, parent, head, tail)
    return sk


def legs(b, phase, stride, lift, bob, duty=0.45, sway=0.0):
    """the leg pose at a phase of the gait (0..1): each foot on the ground
    `duty` of the cycle sliding back by `stride`, then up and forward"""
    pose = {}
    l1 = math.hypot(b.hip_y - b.knee_y, 0.01 * b.k)
    l2 = math.hypot(b.knee_y - b.ankle_y, 0.03 * b.k)
    rest_t = (b.knee_y - b.hip_y, 0.01 * b.k)
    rest_s = (b.ankle_y - b.knee_y, -0.03 * b.k)
    for s, off in (("L", 0.0), ("R", 0.5)):
        p = (phase + off) % 1.0
        if p < duty:
            u = p / duty
            z = stride / 2 - stride * u
            y = b.ankle_y
            frx = 0.0
        else:
            u = (p - duty) / (1 - duty)
            z = -stride / 2 + stride * _sm(u)
            y = b.ankle_y + lift * math.sin(math.pi * u) ** 0.8
            frx = 30 * math.sin(math.pi * u)
        th, sh = leg_ik((0, b.hip_y + bob, 0), (0, y, -0.02 * b.k + z), l1, l2, rest_t, rest_s)
        pose[f"thigh.{s}"] = (th, 0, 0)
        pose[f"shin.{s}"] = (sh, 0, 0)
        pose[f"foot.{s}"] = (frx - th - sh, 0, 0)
    pose["root"] = (0, 0, sway, 0, bob, 0)
    return pose


# ------------------------------------------------------------------ holding the weapon

# how each kind of weapon is held: the right hand's place, as fractions of
# the height (x right, y up from the hips, z forward), the elbow's pole; the
# left hand on the weapon's foregrip ("grip") or at its own place; the turn
# of the chest
HOLDS = {
    "rifle": {"R": ((0.10, 0.34, 0.16), (1.0, -1.0, -0.5)), "L": "grip", "Lpole": (-0.7, -1.0, 0.1),
              "chest": (0, 14, 0)},
    "pistol": {"R": ((0.10, 0.42, 0.38), (1.0, -0.6, -0.5)), "L": None, "chest": (0, 10, 0)},
    "arm": {"R": ((0.12, 0.44, 0.40), (1.0, -0.3, -0.6)), "L": ((-0.06, 0.30, 0.20), None),
            "Lpole": (-1.0, -0.8, 0.0), "chest": (0, 16, 0)},
    "launcher": {"R": ((0.13, 0.20, 0.12), (1.0, -0.4, -0.6)), "L": "grip", "Lpole": (-1.0, -0.6, 0.0),
                 "chest": (0, 10, 0)},
    "blaster": {"R": ((0.11, 0.24, 0.20), (1.0, -1.0, -0.2)), "L": "grip", "Lpole": (-1.0, -1.0, 0.0),
                "chest": (0, 6, 0)},
    "throw": {"R": ((0.14, 0.38, 0.24), (1.0, -1.0, -0.3)), "L": ((-0.18, 0.28, 0.14), None),
              "Lpole": (-1.0, -1.0, 0.2), "chest": (0, 4, 0)},
}


def _frame_point(sk, pose, bone, p):
    """a point given in a bone's rest space, where the pose puts it"""
    return sk.place(pose, bone, p)


def hold_pose(sk, b, kind, base=None, recoil=0.0, lift=0.0, grip=None):
    """the arms holding the weapon over a base pose (the legs): the right
    hand to its place (following the hips), the weapon (bone "gun", if any)
    pointing forward, kicked up by the recoil, the left hand on its foregrip
    (`grip`: a point in the gun bone's rest space) or at its own place"""
    pose = dict(base or {})
    h = HOLDS[kind]
    pose["chest"] = h["chest"]
    (fx, fy, fz), pole = h["R"]
    rest = (fx * b.h, b.hip_y + (fy + lift) * b.h, (fz - recoil) * b.h)
    rig.arm_ik(sk, pose, "upperarm.R", "forearm.R", _frame_point(sk, pose, "hips", rest), pole)
    if "gun" in sk.index:
        wh = sk.matrices(pose)[sk["hips"]][0]
        rig.orient(sk, pose, "gun", rig.mat_mul(wh, rig.rot_euler(-25 * recoil * 4, 0, 0)))
    else:
        pose["hand.R"] = (-10 - 30 * recoil, 0, 0)
    left = h["L"]
    if left == "grip" and grip is not None and "gun" in sk.index:
        rig.arm_ik(sk, pose, "upperarm.L", "forearm.L", _frame_point(sk, pose, "gun", grip), h["Lpole"])
    elif isinstance(left, tuple):
        (lx, ly, lz), _ = left
        rest = (lx * b.h, b.hip_y + (ly + lift) * b.h, lz * b.h)
        rig.arm_ik(sk, pose, "upperarm.L", "forearm.L", _frame_point(sk, pose, "hips", rest), h["Lpole"])
    return pose


REST_ARMS = {"upperarm.L": (0, 0, -8), "upperarm.R": (0, 0, 8), "forearm.L": (-14, 0, 0), "forearm.R": (-14, 0, 0)}


def clips(sk, b, kind="rifle", fire_rate=0.12, recoil=0.05, run_period=0.62, run_stride=0.95, grip=None):
    """the standard clips of a hero on foot"""
    out = []
    lean = 1.0

    def hp(base=None, recoil=0.0, lift=0.0):
        return hold_pose(sk, b, kind, base, recoil, lift, grip)

    def idle(t):
        k = math.sin(2 * math.pi * t / 2.2)
        p = legs(b, 0.0, 0.0, 0.0, -0.01 * b.k + 0.006 * b.k * k)
        p = hp(p, lift=0.004 * k - 0.02)
        p["head"] = (-1.5 * k, 3 * math.sin(math.pi * t / 2.2), 0)
        p["spine"] = (1.5 * k, 0, 0)
        return p
    out.append(sample(idle, 2.2, 8, name="idle"))

    def walk(t):
        ph = t / (run_period * 1.6)
        p = legs(b, ph, run_stride * 0.6, 0.12 * b.k, -0.02 * b.k + 0.015 * b.k * math.cos(4 * math.pi * ph))
        sw = math.sin(2 * math.pi * ph)
        p["hips"] = (0, -5 * sw, 0)
        p["spine"] = (2, 5 * sw, 0)
        return hp(p, lift=-0.02)
    out.append(sample(walk, run_period * 1.6, 12, name="walk"))

    def run(t):
        ph = t / run_period
        p = legs(b, ph, run_stride * b.stance, 0.28 * b.k, -0.04 * b.k + 0.035 * b.k * math.cos(4 * math.pi * ph))
        sw = math.sin(2 * math.pi * ph)
        p["hips"] = (0, -8 * sw, 0)
        p["spine"] = (8 * lean, 8 * sw, 0)
        return hp(p, lift=-0.03 + 0.01 * math.cos(4 * math.pi * ph))
    out.append(sample(run, run_period, 12, name="run"))

    def jump(t):
        u = t / 0.35
        p = legs(b, 0.0, 0.0, 0.0, -0.12 * b.k * math.sin(math.pi * min(1, u * 1.4)))
        return hp(p, lift=0.03 * u)
    out.append(sample(jump, 0.35, 7, loop=False, name="jump"))

    def air(t):
        k = math.sin(2 * math.pi * t)
        p = {"thigh.L": (-40 + 5 * k, 0, 0), "shin.L": (65, 0, 0), "thigh.R": (-18 - 5 * k, 0, 0),
             "shin.R": (35, 0, 0), "foot.L": (-15, 0, 0), "foot.R": (-10, 0, 0)}
        return hp(p, lift=0.03)
    out.append(sample(air, 1.0, 4, name="air"))

    def land(t):
        u = t / 0.3
        p = legs(b, 0.0, 0.0, 0.0, -0.14 * b.k * math.sin(math.pi * u))
        return hp(p)
    out.append(sample(land, 0.3, 6, loop=False, name="land"))

    def crouch(t):
        p = legs(b, 0.0, 0.0, 0.0, -0.30 * b.k)
        p["spine"] = (14, 0, 0)
        return hp(p)
    out.append(sample(crouch, 1.0, 2, name="crouch"))

    # upper-body layers (the chest and the arms)
    def aim(t):
        return hp({})
    out.append(sample(aim, 1.0, 2, name="aim"))

    def fire(t):
        u = (t % fire_rate) / fire_rate
        k = max(0.0, 1 - u * 2)
        p = hp({}, recoil=recoil * k)
        p["chest"] = (p["chest"][0] - 4 * k, p["chest"][1], p["chest"][2])
        return p
    out.append(sample(fire, fire_rate, 4, name="fire", mode=0))

    def reload(t):
        u = t / 1.4
        dip = math.sin(math.pi * _sm(u))
        p = hp({}, lift=-0.08 * dip)
        p["chest"] = (p["chest"][0] + 12 * dip, p["chest"][1] - 10 * dip, 0)
        # the left hand goes down to the belt and back
        if "upperarm.L" in p:
            ua = p["upperarm.L"]
            p["upperarm.L"] = (ua[0] + 50 * dip, ua[1], ua[2] - 20 * dip)
            fa = p.get("forearm.L", (0, 0, 0))
            p["forearm.L"] = (fa[0] - 30 * dip, fa[1], fa[2])
        return p
    out.append(sample(reload, 1.4, 10, loop=False, name="reload"))

    def melee(t):
        u = t / 0.5
        k = math.sin(math.pi * min(1.0, u * 1.6))
        p = hp({}, lift=0.06 * k)
        p["chest"] = (8 * k, 25 * k, 0)
        ua = p["upperarm.L"] if "upperarm.L" in p else (0, 0, 0)
        p["upperarm.L"] = (ua[0] - 70 * k, ua[1], ua[2] - 30 * k)
        p["forearm.L"] = (-20 * k, 0, 0)
        return p
    out.append(sample(melee, 0.5, 8, loop=False, name="melee"))

    def throw(t):
        u = t / 0.45
        wind = _sm(u / 0.4) * (1 - _sm((u - 0.4) / 0.25))
        rel = _sm((u - 0.4) / 0.25) * (1 - _sm((u - 0.8) / 0.2))
        p = hp({})
        p["chest"] = (-5 * wind + 10 * rel, 25 * wind - 15 * rel, 0)
        p["upperarm.R"] = (-150 * wind - 90 * rel, 0, 30 * wind)
        p["forearm.R"] = (-60 * wind - 10 * rel, 0, 0)
        return p
    out.append(sample(throw, 0.45, 9, loop=False, name="throw"))

    def hit(t):
        k = math.sin(math.pi * t / 0.25) * (1 - t / 0.25)
        return {"chest": (-12 * k, 0, 6 * k), "head": (-10 * k, 0, 0)}
    out.append(sample(hit, 0.25, 5, loop=False, name="hit"))

    def death(t):
        kk = _sm(t / 0.3)
        u = _sm((t - 0.15) / 0.75)
        p = {"root": (-84 * u, 0, 6 * u, 0, 0.06 * u, -0.25 * u)}
        p["thigh.L"], p["thigh.R"] = (-25 * kk * (1 - u), 0, 0), (-15 * kk * (1 - u), 0, 0)
        p["shin.L"], p["shin.R"] = (40 * kk * (1 - u) + 8 * u, 0, 0), (30 * kk * (1 - u) + 12 * u, 0, 0)
        p["chest"] = (-12 * kk, 0, 0)
        p["head"] = (-25 * kk + 20 * u, 20 * u, 0)
        p["upperarm.L"] = (-60 * u, 0, -55 * u)
        p["upperarm.R"] = (-40 * u, 0, 65 * u)
        p["forearm.L"], p["forearm.R"] = (-30 * u, 0, 0), (-20 * u, 0, 0)
        return p
    out.append(sample(death, 0.9, 10, loop=False, name="death"))

    def victory(t):
        u = t / 2.4
        up = _sm(u / 0.15) * (1 - _sm((u - 0.85) / 0.15))
        p = legs(b, 0.0, 0.0, 0.0, -0.01)
        p.update(REST_ARMS)
        p["upperarm.R"] = (-150 * up, 0, 25 * up + 10 * math.sin(2 * math.pi * u * 3) * up)
        p["forearm.R"] = (-25 * up, 0, 0)
        p["upperarm.L"] = (0, 0, -35 * up)
        p["chest"] = (-6 * up, -15 * up, 0)
        p["head"] = (-10 * up, 12 * up, 0)
        return p
    out.append(sample(victory, 2.4, 24, loop=False, name="victory"))
    return out


# ------------------------------------------------------------------ the standard body (models)

def body(m, sk, b, mat, chest=(1.0, 1.0), waist=1.0, hips=1.0, arms=1.0, legs_k=1.0, head=True, hands=True,
         boots=True):
    """the body on the standard skeleton, made of round parts along the
    bones at rest: neck and head (skin), chest, waist and hips, arms with
    shoulders and hands, legs and boots. `mat`: Mat by part (skin, top,
    top2, sleeve, cuff, hand, pants, shin, boot, belt; the missing ones take
    the nearest). chest=(width, depth), waist, hips, arms, legs_k: thickness
    multipliers. Each hero adds the face, hair, clothes and weapon."""
    from geo import ellipsoid, lathe, tube, hull
    g = lambda k: mat.get(k)                           # noqa: E731
    skin = g("skin")
    top = g("top") or skin
    top2 = g("top2") or top
    sleeve = g("sleeve") or top
    cuff = g("cuff") or sleeve
    hand = g("hand") or skin
    pants = g("pants") or top2
    shin = g("shin") or pants
    boot = g("boot") or shin
    belt = g("belt")
    B = sk.index
    k = b.k
    ny, cy, hy = b.neck_y, b.chest_y, b.hip_y
    cw, cd = chest
    if head:
        hs = b.head
        hp = (0, ny + 0.125 * k * hs, 0.01)
        hr = (0.088 * k * hs, 0.108 * k * hs, 0.098 * k * hs)
        m.add(ellipsoid(*hr, segs=8, rings=6).move(*hp), skin, B["head"], 3, 3)
        m.add(ellipsoid(*hr, segs=6, rings=4).move(*hp), skin, B["head"], 0, 2)
    m.add(tube((0, ny - 0.03 * k, 0), (0, ny + 0.06 * k, 0.005), 0.045 * k, 0.042 * k, segs=6), skin, B["head"], 3, 3)
    # the torso: chest (a lathe, flattened front to back), waist, hips
    m.add(lathe([(0.12 * k * cw, cy - 0.03 * k), (0.172 * k * cw, cy + 0.10 * k), (0.185 * k * cw, cy + 0.20 * k),
                 (0.11 * k * cw, ny + 0.02 * k)], segs=8).scale(1, 1, 0.66 * cd), top, B["chest"], 3, 3)
    m.add(lathe([(0.12 * k * cw, cy - 0.03 * k), (0.18 * k * cw, cy + 0.15 * k), (0.11 * k * cw, ny + 0.02 * k)],
                segs=6).scale(1, 1, 0.66 * cd), top, B["chest"], 0, 2)
    m.add(lathe([(0.118 * k * waist, hy + 0.05 * k), (0.112 * k * waist, cy - 0.06 * k), (0.125 * k * cw, cy + 0.0)],
                segs=8).scale(1, 1, 0.68 * cd), top2, B["spine"], 3, 3)
    m.add(lathe([(0.118 * k * waist, hy + 0.05 * k), (0.125 * k * cw, cy + 0.0)],
                segs=6).scale(1, 1, 0.68 * cd), top2, B["spine"], 0, 2)
    if belt:
        m.add(lathe([(0.123 * k * waist, hy + 0.03 * k), (0.121 * k * waist, hy + 0.075 * k)], segs=8, close_top=False,
                    close_bottom=False).scale(1, 1, 0.71 * cd), belt, B["spine"], 3, 3)
    m.add(lathe([(0.118 * k * hips, hy - 0.10 * k), (0.128 * k * hips, hy - 0.01 * k), (0.12 * k * hips, hy + 0.06 * k)],
                segs=8).scale(1, 1, 0.72 * cd), pants, B["hips"], 3, 3)
    m.add(lathe([(0.118 * k * hips, hy - 0.10 * k), (0.12 * k * hips, hy + 0.06 * k)],
                segs=6).scale(1, 1, 0.72 * cd), pants, B["hips"], 0, 2)
    for s, x in (("L", -1), ("R", 1)):
        UA, FA, HD = B[f"upperarm.{s}"], B[f"forearm.{s}"], B[f"hand.{s}"]
        TH, SH, FT = B[f"thigh.{s}"], B[f"shin.{s}"], B[f"foot.{s}"]
        sh, el = sk.bones[UA][2], sk.bones[UA][3]
        wr = sk.bones[FA][3]
        hd, ht = sk.bones[HD][2], sk.bones[HD][3]
        ra = arms
        m.add(ellipsoid(0.068 * k * ra, 0.064 * k * ra, 0.072 * k * ra, segs=6, rings=4).move(sh[0], sh[1] + 0.008, 0),
              sleeve, UA, 3, 3)
        m.add(tube(sh, el, 0.060 * k * ra, 0.050 * k * ra, segs=6), sleeve, UA, 3, 3)
        m.add(tube(sh, el, 0.060 * k * ra, 0.050 * k * ra, segs=4), sleeve, UA, 0, 2)
        m.add(tube(el, wr, 0.050 * k * ra, 0.038 * k * ra, segs=6), cuff, FA, 3, 3)
        m.add(tube(el, wr, 0.050 * k * ra, 0.038 * k * ra, segs=4), cuff, FA, 0, 2)
        if hands:
            d = (ht[0] - hd[0], ht[1] - hd[1], ht[2] - hd[2])
            m.add(hull([(hd[0] - 0.032 * k, hd[1] + 0.015, hd[2] - 0.02), (hd[0] + 0.032 * k, hd[1] + 0.015, hd[2] - 0.02),
                        (hd[0] - 0.028 * k, hd[1] + 0.015, hd[2] + 0.045 * k), (hd[0] + 0.028 * k, hd[1] + 0.015, hd[2] + 0.045 * k),
                        (hd[0] + d[0], hd[1] + d[1], hd[2] + d[2] + 0.015), (hd[0] - 0.03 * k, hd[1] - 0.06 * k, hd[2]),
                        (hd[0] + 0.03 * k, hd[1] - 0.06 * k, hd[2])], smooth=True), hand, HD, 1, 3)
        th, kn = sk.bones[TH][2], sk.bones[TH][3]
        an = sk.bones[SH][3]
        rl = legs_k
        m.add(tube(th, kn, 0.088 * k * rl, 0.064 * k * rl, segs=7), pants, TH, 3, 3)
        m.add(tube(th, kn, 0.088 * k * rl, 0.064 * k * rl, segs=4), pants, TH, 0, 2)
        m.add(tube(kn, an, 0.064 * k * rl, 0.05 * k * rl, segs=6), shin, SH, 3, 3)
        m.add(tube(kn, an, 0.064 * k * rl, 0.05 * k * rl, segs=4), shin, SH, 0, 2)
        if boots:
            m.add(hull([(an[0] - 0.055 * k, 0.0, -0.065 * k), (an[0] + 0.055 * k, 0.0, -0.065 * k),
                        (an[0] - 0.055 * k, 0.0, 0.18 * k), (an[0] + 0.055 * k, 0.0, 0.18 * k),
                        (an[0] - 0.055 * k, 0.15 * k, -0.055 * k), (an[0] + 0.055 * k, 0.15 * k, -0.055 * k),
                        (an[0] - 0.045 * k, 0.085 * k, 0.14 * k), (an[0] + 0.045 * k, 0.085 * k, 0.14 * k)]),
                  boot, FT, 0, 3)

def eyes(m, sk, b, white, iris, x=0.034, y=0.135, z=0.085, r=0.016, lo=1):
    """two eyes on the face of the standard head"""
    from geo import ellipsoid
    k, ny = b.k * b.head, b.neck_y
    for s in (-1, 1):
        m.add(ellipsoid(r * 1.25 * k, r * k, r * 0.6 * k, segs=6, rings=3).move(s * x * k, ny + y * k, z * k),
              white, sk.index["head"], max(lo, 2), 3)
        m.add(ellipsoid(r * 0.6 * k, r * 0.7 * k, r * 0.4 * k, segs=5, rings=3).move(s * x * k, ny + y * k, (z + 0.008) * k),
              iris, sk.index["head"], lo, 3)


def gun_bone(sk, length=0.6):
    """a bone `gun` from the right hand forward (the weapon follows it); the
    foregrip point in its rest space"""
    hand = sk.bones[sk["hand.R"]][2]
    sk.bone("gun", "hand.R", hand, (hand[0], hand[1], hand[2] + length))
    return (hand[0], hand[1] + 0.02, hand[2] + length * 0.6)
