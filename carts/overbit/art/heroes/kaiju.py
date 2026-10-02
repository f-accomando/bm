"""
Kaiju - tank (the kit of D.Mon): a little monster in a big red mech.

The mech, BIG RED: a round red body with white horns, a green glass dome on
top where the pilot sits, heavy shoulders, a plasma blade on the left arm
(green, glowing), a hexagon shield emitter with a repeater gun on the right
arm, thick legs with grey joints, yellow trim. The pilot: a small green
lizard monster with big eyes, a frill and a tail, holding a mini repeater.

Models: kaiju_mech, kaiju_pilot, kaiju_fp (the arms from the dome),
kaiju_pfp (the claws and the mini gun).
"""
import math

from geo import Mat, Mesh, Part, box, cylinder, ellipsoid, hull, lathe, add, sub, cross
import humanoid
from humanoid import Body
import rig
from rig import Skeleton, sample, leg_ik

RED = Mat(0xD8282E, glossy=True)
RED2 = Mat(0xA81C22, glossy=True)
WHITE = Mat(0xEDEFF2, glossy=True)
GREY = Mat(0x6E747C)
DARK = Mat(0x34383F)
YELLOW = Mat(0xF2C230, glossy=True)
GLASS = Mat(0x3CF08A, emissive=True, screen=True)
GLASS_RIM = Mat(0x2B8F55, glossy=True)
GREEN = Mat(0x5CFF9C, emissive=True)
GREEN2 = Mat(0x2FE07A, emissive=True)
BADGE = Mat(0xF4F4F4, emissive=True)

SKIN = Mat(0x7CCB4E)
BELLY = Mat(0xE6E08A)
EYE = Mat(0xFFFFFF, emissive=True)
PUPIL = Mat(0x101010)
FRILL = Mat(0xFF8A3C)
GUN = Mat(0x3A3F46, glossy=True)


def quad(p0, p1, p2, p3):
    return Part([p0, p1, p2, p3], [(0, 1, 2), (0, 2, 3)], False)


def facing(part, towards):
    out = []
    for f in part.faces:
        a, b, c = (part.pts[i] for i in f)
        n = cross(sub(b, a), sub(c, a))
        out.append(f if n[0] * towards[0] + n[1] * towards[1] + n[2] * towards[2] >= 0 else (f[0], f[2], f[1]))
    part.faces = out
    return part


def hexagon(r, z=0.0):
    pts = [(r * math.cos(math.pi / 3 * i + math.pi / 6), r * math.sin(math.pi / 3 * i + math.pi / 6), z) for i in range(6)]
    return Part(pts, [(0, 1, 2), (0, 2, 3), (0, 3, 4), (0, 4, 5)], False)


# ------------------------------------------------------------------ the mech

HIP_Y = 1.25
KNEE = (0.70, 0.12)
ANKLE = (0.16, -0.04)


def mech_skeleton():
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.5, 0))
    sk.bone("hips", "root", (0, HIP_Y, 0), (0, 1.55, 0))
    sk.bone("body", "hips", (0, 1.50, 0), (0, 2.35, 0))
    sk.bone("dome", "body", (0, 2.55, -0.20), (0, 2.55, 0.35))
    for s, x in (("L", -1), ("R", 1)):
        sk.bone(f"arm.{s}", "body", (x * 0.95, 2.05, 0.05), (x * 1.10, 1.55, 0.15))
        sk.bone(f"fore.{s}", f"arm.{s}", (x * 1.10, 1.55, 0.15), (x * 1.12, 1.30, 0.75))
    sk.bone("blade", "fore.L", (-1.12, 1.30, 0.75), (-1.12, 1.30, 2.10))
    for s, x in (("L", -1), ("R", 1)):
        sk.bone(f"thigh.{s}", "hips", (x * 0.52, HIP_Y, 0), (x * 0.56, KNEE[0], KNEE[1]))
        sk.bone(f"shin.{s}", f"thigh.{s}", (x * 0.56, KNEE[0], KNEE[1]), (x * 0.56, ANKLE[0], ANKLE[1]))
        sk.bone(f"foot.{s}", f"shin.{s}", (x * 0.56, ANKLE[0], ANKLE[1]), (x * 0.56, 0.05, 0.40))
    return sk


def both(m, make, mat, bl, br, lo=0, hi=3):
    p = make()
    m.add(p, mat, bl, lo, hi)
    m.add(p.copy().mirror_x(), mat, br, lo, hi)


def mech_mesh(sk, name="kaiju_mech"):
    m = Mesh(name)
    B = sk.index
    # -- the body: a big red ball, a white belly plate, yellow trim
    m.add(ellipsoid(0.98, 0.78, 0.92, segs=10, rings=7).move(0, 2.0, -0.02), RED, B["body"], 2, 3)
    m.add(ellipsoid(0.98, 0.78, 0.92, segs=7, rings=5).move(0, 2.0, -0.02), RED, B["body"], 0, 1)
    m.add(hull([(-0.55, 1.62, 0.70), (0.55, 1.62, 0.70), (-0.62, 1.95, 0.86), (0.62, 1.95, 0.86),
                (-0.45, 1.45, 0.45), (0.45, 1.45, 0.45), (0, 1.98, 0.95)], smooth=False), WHITE, B["body"], 1, 3)
    m.add(box(1.5, 0.05, 0.05).move(0, 1.62, 0.66), YELLOW, B["body"], 2, 3)
    # the badge: a white "KJ" plate on each side (our own mark)
    for x in (-1, 1):
        m.add(facing(quad((x * 0.97, 2.15, -0.20), (x * 0.97, 2.15, 0.15), (x * 0.97, 1.97, 0.15), (x * 0.97, 1.97, -0.20)),
                     (x, 0, 0)), BADGE, B["body"], 2, 3)
    # horns
    for x in (-1, 1):
        m.add(hull([(x * 0.35, 2.55, 0.10), (x * 0.45, 2.55, 0.10), (x * 0.35, 2.55, -0.05), (x * 0.45, 2.55, -0.05),
                    (x * 0.62, 2.98, 0.0)]), WHITE, B["body"], 1, 3)
    # -- the dome with the pilot's silhouette inside (seen through the glass)
    m.add(ellipsoid(0.48, 0.42, 0.48, segs=10, rings=6).move(0, 2.62, 0.08), GLASS, B["dome"], 1, 3)
    m.add(ellipsoid(0.48, 0.42, 0.48, segs=6, rings=4).move(0, 2.62, 0.08), Mat(0x3CF08A, emissive=True), B["dome"], 0, 0)
    m.add(lathe([(0.50, 2.50), (0.50, 2.58)], segs=10, close_top=False, close_bottom=False).move(0, 0, 0.08),
          GLASS_RIM, B["dome"], 1, 3)
    m.add(ellipsoid(0.16, 0.20, 0.16, segs=6, rings=4).move(0, 2.70, 0.12), SKIN, B["dome"], 2, 3)
    m.add(ellipsoid(0.05, 0.05, 0.03, segs=5, rings=3).move(-0.06, 2.76, 0.26), EYE, B["dome"], 3, 3)
    m.add(ellipsoid(0.05, 0.05, 0.03, segs=5, rings=3).move(0.06, 2.76, 0.26), EYE, B["dome"], 3, 3)
    # -- shoulders and arms
    both(m, lambda: ellipsoid(0.42, 0.40, 0.44, segs=8, rings=5).move(-1.0, 2.08, 0.02), RED2, B["arm.L"], B["arm.R"], 1, 3)
    both(m, lambda: ellipsoid(0.42, 0.40, 0.44, segs=6, rings=4).move(-1.0, 2.08, 0.02), RED2, B["arm.L"], B["arm.R"], 0, 0)
    both(m, lambda: box(0.36, 0.06, 0.40).move(-1.0, 2.40, 0.0), YELLOW, B["arm.L"], B["arm.R"], 2, 3)
    both(m, lambda: cylinder(0.16, 0.50, segs=6, caps=False).turn(rz=-14).move(-1.08, 1.55, 0.12), GREY,
         B["arm.L"], B["arm.R"], 1, 3)
    # forearms: big red gauntlets
    both(m, lambda: hull([(-1.28, 1.48, 0.10), (-0.94, 1.48, 0.10), (-1.28, 1.20, 0.10), (-0.94, 1.20, 0.10),
                          (-1.24, 1.42, 0.85), (-0.98, 1.42, 0.85), (-1.24, 1.18, 0.85), (-0.98, 1.18, 0.85)]),
         RED, B["fore.L"], B["fore.R"], 0, 3)
    both(m, lambda: box(0.30, 0.05, 0.60).move(-1.11, 1.50, 0.48), YELLOW, B["fore.L"], B["fore.R"], 2, 3)
    # left: the plasma blade, long and green
    m.add(hull([(-1.12, 1.30, 0.85), (-1.12, 1.38, 0.85), (-1.12, 1.30, 2.10), (-1.09, 1.33, 1.0),
                (-1.15, 1.33, 1.0), (-1.12, 1.36, 2.05)]), GREEN, B["blade"], 0, 3)
    m.add(box(0.14, 0.16, 0.10).move(-1.12, 1.32, 0.86), DARK, B["blade"], 1, 3)
    # right: the hexagon emitter and the repeater barrels
    m.add(facing(hexagon(0.24).move(1.11, 1.33, 0.87), (0, 0, 1)), GREEN2, B["fore.R"], 1, 3)
    m.add(cylinder(0.05, 0.25, segs=6, caps=False, axis="z").move(1.03, 1.16, 0.85), DARK, B["fore.R"], 2, 3)
    m.add(cylinder(0.05, 0.25, segs=6, caps=False, axis="z").move(1.19, 1.16, 0.85), DARK, B["fore.R"], 2, 3)
    # -- the waist and the legs (knees in front)
    m.add(box(0.80, 0.30, 0.60).move(0, 1.35, -0.02), DARK, B["hips"], 0, 3)
    for s, x in (("L", -1), ("R", 1)):
        T, S, F = B[f"thigh.{s}"], B[f"shin.{s}"], B[f"foot.{s}"]
        lx = x * 0.54
        m.add(hull([(lx - 0.22, 1.40, -0.22), (lx + 0.22, 1.40, -0.22), (lx - 0.22, 1.40, 0.24), (lx + 0.22, 1.40, 0.24),
                    (lx - 0.18, KNEE[0] + 0.05, KNEE[1] - 0.16), (lx + 0.18, KNEE[0] + 0.05, KNEE[1] - 0.16),
                    (lx - 0.18, KNEE[0] + 0.05, KNEE[1] + 0.18), (lx + 0.18, KNEE[0] + 0.05, KNEE[1] + 0.18)]),
              RED2, T, 0, 3)
        m.add(cylinder(0.15, 0.34, segs=6, axis="x").move(lx - 0.17, KNEE[0], KNEE[1]), GREY, T, 1, 3)
        m.add(hull([(lx - 0.20, KNEE[0] - 0.05, KNEE[1] - 0.18), (lx + 0.20, KNEE[0] - 0.05, KNEE[1] - 0.18),
                    (lx - 0.20, KNEE[0] - 0.05, KNEE[1] + 0.20), (lx + 0.20, KNEE[0] - 0.05, KNEE[1] + 0.20),
                    (lx - 0.16, ANKLE[0] + 0.05, ANKLE[1] - 0.14), (lx + 0.16, ANKLE[0] + 0.05, ANKLE[1] - 0.14),
                    (lx - 0.16, ANKLE[0] + 0.05, ANKLE[1] + 0.16), (lx + 0.16, ANKLE[0] + 0.05, ANKLE[1] + 0.16)]),
              RED, S, 0, 3)
        m.add(box(0.06, 0.32, 0.05).move(lx, 0.45, 0.19), YELLOW, S, 2, 3)
        m.add(hull([(lx - 0.22, 0.0, -0.22), (lx + 0.22, 0.0, -0.22), (lx - 0.24, 0.0, 0.42), (lx + 0.24, 0.0, 0.42),
                    (lx - 0.18, 0.22, -0.16), (lx + 0.18, 0.22, -0.16), (lx - 0.18, 0.14, 0.32), (lx + 0.18, 0.14, 0.32)]),
              DARK, F, 0, 3)
        m.add(box(0.40, 0.06, 0.20).move(lx, 0.17, 0.30), RED2, F, 1, 3)
    return m.weld()


def legs(phase, stride, lift, bob, crouch=0.0):
    pose = {}
    hip_y = HIP_Y + bob - crouch
    l1 = math.hypot(HIP_Y - KNEE[0], KNEE[1])
    l2 = math.hypot(KNEE[0] - ANKLE[0], KNEE[1] - ANKLE[1])
    for s, off in (("L", 0.0), ("R", 0.5)):
        p = (phase + off) % 1.0
        if p < 0.5:
            u = p / 0.5
            z, y, frx = stride / 2 - stride * u, ANKLE[0], 0.0
        else:
            u = (p - 0.5) / 0.5
            z = -stride / 2 + stride * (u * u * (3 - 2 * u))
            y = ANKLE[0] + lift * math.sin(math.pi * u)
            frx = 15 * math.sin(math.pi * u)
        th, sh = leg_ik((0, hip_y, 0), (0, y, ANKLE[1] + z), l1, l2, (KNEE[0] - HIP_Y, KNEE[1]),
                        (ANKLE[0] - KNEE[0], ANKLE[1] - KNEE[1]))
        pose[f"thigh.{s}"] = (th, 0, 0)
        pose[f"shin.{s}"] = (sh, 0, 0)
        pose[f"foot.{s}"] = (frx - th - sh, 0, 0)
    pose["root"] = (0, 0, 0, 0, bob - crouch, 0)
    return pose


def _sm(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


GUARD = {"arm.L": (-30, -10, -10), "fore.L": (-20, 0, 0), "arm.R": (-30, 10, 10), "fore.R": (-15, 0, 0)}


def mech_clips(sk):
    out = []

    def idle(t):
        k = math.sin(2 * math.pi * t / 2.6)
        p = legs(0, 0, 0, -0.03 + 0.02 * k)
        p.update(GUARD)
        p["body"] = (2 * k, 0, 0)
        p["dome"] = (0, 6 * math.sin(math.pi * t / 2.6), 0)
        return p
    out.append(sample(idle, 2.6, 8, name="idle"))

    def walker(period, stride, lift, bobk, name):
        def f(t):
            ph = t / period
            p = legs(ph, stride, lift, -0.05 + bobk * math.cos(4 * math.pi * ph))
            sw = math.sin(2 * math.pi * ph)
            p["body"] = (3, 6 * sw, 3 * sw)
            p["hips"] = (0, -6 * sw, 0)
            p["arm.L"] = (-30 + 10 * sw, -10, -10)
            p["arm.R"] = (-30 - 10 * sw, 10, 10)
            p["fore.L"], p["fore.R"] = (-20, 0, 0), (-15, 0, 0)
            return p
        return sample(f, period, 12, name=name)
    out.append(walker(1.0, 1.0, 0.25, 0.04, "walk"))
    out.append(walker(0.62, 1.5, 0.38, 0.07, "run"))

    def saber(t):
        # two sweeps, left to right then back (1.52 a second: 0.66 s a swing)
        u = (t % 0.66) / 0.66
        side = 1 if t < 0.66 else -1
        k = math.sin(math.pi * min(1.0, u * 1.3))
        sweep = -70 + 140 * _sm(u * 1.2) if side > 0 else 70 - 140 * _sm(u * 1.2)
        p = {"body": (4, 20 * side * (u - 0.5), 0), "arm.L": (-55 - 15 * k, sweep, -20), "fore.L": (-10, 0, 0),
             "arm.R": (-25, 10, 10), "fore.R": (-15, 0, 0)}
        return p
    out.append(sample(saber, 1.32, 16, name="saber"))

    def barrier(t):
        k = math.sin(2 * math.pi * t * 2)
        return {"body": (6, 0, 0), "arm.R": (-75 + k, 25, 15), "fore.R": (-10, 0, 0), "arm.L": (-20, -10, -15),
                "fore.L": (-30, 0, 0)}
    out.append(sample(barrier, 1.0, 6, name="barrier"))

    def strike(t):
        u = t / 0.6
        k = math.sin(math.pi * min(1.0, u * 1.5))
        return {"body": (14 * k, 0, 0), "arm.R": (-80 - 10 * k, 15, 10), "fore.R": (-5, 0, 0, 0, 0, 0.3 * k),
                "arm.L": (-60 * k, -30, -20), "fore.L": (-10, 0, 0)}
    out.append(sample(strike, 0.6, 9, loop=False, name="strike"))

    def repeater(t):
        u = (t % 0.128) / 0.128
        k = max(0.0, 1 - u * 2)
        return {"arm.R": (-78, 12, 8), "fore.R": (-6 - 3 * k, 0, 0, 0, 0, -0.06 * k), "arm.L": (-25, -10, -12),
                "fore.L": (-20, 0, 0), "body": (2, 8, 0)}
    out.append(sample(repeater, 0.128, 4, name="repeater", mode=0))

    def dash(t):
        k = math.sin(2 * math.pi * t * 3)
        p = legs(0, 0, 0, 0.1)
        p["hips"] = (16, 0, 0)
        p["body"] = (6, 0, 2 * k)
        p.update({"arm.L": (-10, -10, -20), "arm.R": (-10, 10, 20)})
        for s in "LR":
            p[f"thigh.{s}"] = (25, 0, 0)
            p[f"shin.{s}"] = (-40, 0, 0)
        return p
    out.append(sample(dash, 1.0, 4, name="dash"))

    def limit(t):
        # Limit Break: a wind-up, then a full circle with the blade
        u = t / 1.2
        wind = _sm(u / 0.3)
        spin = _sm((u - 0.3) / 0.5)
        p = legs(0, 0, 0, -0.1 * wind)
        p["body"] = (6, -60 * wind + 420 * spin, 0)
        p["arm.L"] = (-80, -60 * wind + 20 * spin, -30)
        p["fore.L"] = (-5, 0, 0)
        p["arm.R"] = (-20, 20, 40 * wind)
        return p
    out.append(sample(limit, 1.2, 18, loop=False, name="limit"))

    def jump(t):
        u = t / 0.45
        c = 0.3 * math.sin(math.pi * min(1, u * 1.6)) if u < 0.62 else 0.0
        p = legs(0, 0, 0, 0, crouch=c)
        p.update(GUARD)
        return p
    out.append(sample(jump, 0.45, 7, loop=False, name="jump"))

    def air(t):
        p = {"thigh.L": (-25, 0, 0), "shin.L": (35, 0, 0), "thigh.R": (-10, 0, 0), "shin.R": (20, 0, 0)}
        p.update(GUARD)
        return p
    out.append(sample(air, 1.0, 2, name="air"))

    def land(t):
        u = t / 0.35
        p = legs(0, 0, 0, 0, crouch=0.28 * math.sin(math.pi * u))
        p.update(GUARD)
        return p
    out.append(sample(land, 0.35, 6, loop=False, name="land"))

    def crouch(t):
        p = legs(0, 0, 0, 0, crouch=0.30)
        p.update(GUARD)
        return p
    out.append(sample(crouch, 1.0, 2, name="crouch"))

    def hit(t):
        k = math.sin(math.pi * t / 0.25) * (1 - t / 0.25)
        return {"body": (8 * k, 0, -4 * k)}
    out.append(sample(hit, 0.25, 5, loop=False, name="hit"))

    def eject(t):
        o = _sm(t / 0.35)
        sl = _sm((t - 0.3) / 0.8)
        p = legs(0, 0, 0, 0, crouch=0.3 * sl)
        p["dome"] = (-90 * o, 0, 0, 0, 0.2 * o, -0.2 * o)
        p["body"] = (14 * sl, 0, 0)
        p["arm.L"] = (20 * sl, 0, -20 * sl)
        p["arm.R"] = (20 * sl, 0, 20 * sl)
        return p
    out.append(sample(eject, 1.2, 12, loop=False, name="eject"))

    def death(t):
        u = _sm(t / 1.1)
        p = legs(0, 0, 0, 0, crouch=0.65 * u)
        p["hips"] = (18 * u, 0, -6 * u)
        p["body"] = (20 * u, 0, 8 * u)
        p["arm.L"] = (35 * u, 0, -30 * u)
        p["arm.R"] = (30 * u, 0, 35 * u)
        return p
    out.append(sample(death, 1.1, 11, loop=False, name="death"))

    def call(t):
        u = t / 0.8
        c = 0.4 * (1 - _sm(u * 1.3)) if u < 0.8 else 0.0
        p = legs(0, 0, 0, 0, crouch=c)
        p.update(GUARD)
        return p
    out.append(sample(call, 0.8, 10, loop=False, name="callmech"))

    def victory(t):
        u = t / 2.6
        up = _sm(u / 0.2) * (1 - _sm((u - 0.85) / 0.15))
        p = legs(0, 0, 0, -0.05 * up)
        p["arm.L"] = (-150 * up, -20, -20 * up)
        p["fore.L"] = (-20 * up, 0, 0)
        p["arm.R"] = (-60 * up, 30, 40 * up)
        p["body"] = (-6 * up, 15 * math.sin(2 * math.pi * u * 2) * up, 0)
        p["dome"] = (0, 30 * math.sin(2 * math.pi * u * 3) * up, 0)
        return p
    out.append(sample(victory, 2.6, 24, loop=False, name="victory"))
    return out


# ------------------------------------------------------------------ the pilot: a little monster

PBODY = Body(height=1.05, shoulders=0.15, hips=0.09, arm=0.85, leg=0.8, head=1.6)


def pilot_skeleton():
    b = PBODY
    sk = humanoid.skeleton(b, extra=[("tail", "hips", (0, b.hip_y, -0.08), (0, b.hip_y - 0.15, -0.55))])
    hand = sk.bones[sk["hand.R"]][2]
    sk.bone("gun", "hand.R", hand, (hand[0], hand[1], hand[2] + 0.3))
    return sk


def pilot_mesh(sk, name="kaiju_pilot"):
    b = PBODY
    m = Mesh(name)
    B = sk.index
    k = b.k
    ny = b.neck_y
    H = B["head"]
    # a big round head with a wide mouth, big eyes, a frill and two little horns
    m.add(ellipsoid(0.17, 0.15, 0.16, segs=8, rings=6).move(0, ny + 0.12, 0.02), SKIN, H, 1, 3)
    m.add(ellipsoid(0.17, 0.15, 0.16, segs=6, rings=4).move(0, ny + 0.12, 0.02), SKIN, H, 0, 0)
    for x in (-1, 1):
        m.add(ellipsoid(0.055, 0.06, 0.04, segs=6, rings=4).move(x * 0.07, ny + 0.17, 0.14), EYE, H, 0, 3)
        m.add(ellipsoid(0.025, 0.03, 0.02, segs=5, rings=3).move(x * 0.07, ny + 0.17, 0.175), PUPIL, H, 1, 3)
        m.add(hull([(x * 0.06, ny + 0.24, -0.02), (x * 0.09, ny + 0.24, 0.0), (x * 0.075, ny + 0.24, 0.03),
                    (x * 0.10, ny + 0.36, -0.05)]), FRILL, H, 1, 3)
    m.add(hull([(-0.17, ny + 0.10, -0.10), (0.17, ny + 0.10, -0.10), (-0.22, ny + 0.22, -0.14), (0.22, ny + 0.22, -0.14),
                (0, ny + 0.30, -0.12), (0, ny + 0.05, -0.12)]), FRILL, H, 2, 3)
    m.add(box(0.16, 0.012, 0.02).move(0, ny + 0.06, 0.165), PUPIL, H, 2, 3)          # the grin
    # a round body with a yellow belly, a tail
    m.add(ellipsoid(0.17, 0.22, 0.15, segs=8, rings=5).move(0, b.chest_y, 0), SKIN, B["chest"], 0, 3)
    m.add(ellipsoid(0.12, 0.17, 0.06, segs=6, rings=4).move(0, b.chest_y - 0.03, 0.10), BELLY, B["chest"], 1, 3)
    m.add(ellipsoid(0.15, 0.12, 0.14, segs=6, rings=4).move(0, b.hip_y + 0.04, 0), SKIN, B["hips"], 0, 3)
    tl = sk.bones[B["tail"]]
    m.add(cylinder(0.07, 0.48, segs=6, r2=0.02, caps=False).turn(rx=-105).move(0, b.hip_y, -0.08), SKIN, B["tail"], 0, 3)
    for s, x in (("L", -1), ("R", 1)):
        sh = sk.bones[B[f"upperarm.{s}"]][2]
        el = sk.bones[B[f"forearm.{s}"]][2]
        hd = sk.bones[B[f"hand.{s}"]][2]
        m.add(cylinder(0.045, b.upper, segs=6, r2=0.04, caps=False).turn(rz=180).move(sh[0], sh[1], 0), SKIN,
              B[f"upperarm.{s}"], 0, 3)
        m.add(cylinder(0.04, b.lower, segs=6, r2=0.035, caps=False).turn(rz=180).move(el[0], el[1], 0), SKIN,
              B[f"forearm.{s}"], 0, 3)
        m.add(ellipsoid(0.045, 0.05, 0.05, segs=6, rings=4).move(hd[0], hd[1] - 0.03, hd[2]), SKIN, B[f"hand.{s}"], 0, 3)
        th = sk.bones[B[f"thigh.{s}"]][2]
        kn = sk.bones[B[f"shin.{s}"]][2]
        an = sk.bones[B[f"foot.{s}"]][2]
        m.add(cylinder(0.07, th[1] - kn[1], segs=6, r2=0.055, caps=False).turn(rz=180).move(th[0], th[1], 0), SKIN,
              B[f"thigh.{s}"], 0, 3)
        m.add(cylinder(0.055, kn[1] - an[1], segs=6, r2=0.05, caps=False).turn(rz=180).move(kn[0], kn[1], 0), SKIN,
              B[f"shin.{s}"], 0, 3)
        m.add(ellipsoid(0.07, 0.04, 0.10, segs=6, rings=4).move(an[0], 0.03, 0.05), SKIN, B[f"foot.{s}"], 0, 3)
    g = B["gun"]
    hx, hy, hz = sk.bones[g][2]
    m.add(box(0.06, 0.07, 0.24).move(hx, hy + 0.03, hz + 0.08), GUN, g, 0, 3)
    m.add(cylinder(0.02, 0.10, segs=6, axis="z").move(hx, hy + 0.04, hz + 0.20), GUN, g, 1, 3)
    m.add(box(0.062, 0.012, 0.12).move(hx, hy + 0.07, hz + 0.08), GREEN, g, 2, 3)
    return m.weld()


def pilot_clips(sk):
    hand = sk.bones[sk["hand.R"]][2]
    grip = (hand[0], hand[1] + 0.02, hand[2] + 0.18)
    out = humanoid.clips(sk, PBODY, "rifle", fire_rate=0.128, recoil=0.04, run_period=0.42, run_stride=0.55, grip=grip)
    # the tail sways in every clip: add it to idle and run
    for c in out:
        if c.name in ("idle", "run", "walk"):
            for i, (t, pose) in enumerate(c.keys):
                pose["tail"] = (10, 25 * math.sin(2 * math.pi * t / c.length), 0)

    def eject(t):
        u = _sm(t / 0.3)
        return {"chest": (-40 * u, 0, 0), "thigh.L": (-100 * u, 0, 0), "thigh.R": (-100 * u, 0, 0),
                "shin.L": (120 * u, 0, 0), "shin.R": (120 * u, 0, 0), "tail": (60 * u, 0, 0)}
    out.append(sample(eject, 0.9, 9, loop=False, name="eject"))

    def call(t):
        u = _sm(t / 0.25) * (1 - _sm((t - 0.85) / 0.25))
        return {"upperarm.L": (-170 * u, 0, -20), "upperarm.R": (-170 * u, 0, 20), "head": (-25 * u, 0, 0),
                "tail": (0, 40 * math.sin(t * 20) * u, 0)}
    out.append(sample(call, 1.1, 11, loop=False, name="call"))
    return out


# ------------------------------------------------------------------ first person

def fp_skeleton():
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.2, 0))
    sk.bone("arm.L", "root", (-0.85, -0.45, 0.10), (-0.80, -0.55, 0.50))
    sk.bone("blade", "arm.L", (-0.80, -0.55, 0.50), (-0.80, -0.50, 2.0))
    sk.bone("arm.R", "root", (0.85, -0.45, 0.10), (0.80, -0.55, 0.50))
    sk.bone("muzzle", "arm.R", (0.80, -0.66, 0.85), (0.80, -0.66, 1.12))
    return sk


def fp_mesh(sk, name="kaiju_fp"):
    """the mech's forearms from the dome: layered red gauntlets with a
    yellow stripe and grey knuckles; the left holds the plasma blade on a
    dark hilt, the right has the hexagon emitter and two barrels"""
    m = Mesh(name)
    L, BL, R = sk.index["arm.L"], sk.index["blade"], sk.index["arm.R"]
    for bone, x in ((L, -1), (R, 1)):
        cx = x * 0.80
        # the gauntlet: wide at the back, narrower at the wrist
        m.add(hull([(cx - 0.17, -0.42, -0.1), (cx + 0.17, -0.42, -0.1), (cx - 0.17, -0.72, -0.1), (cx + 0.17, -0.72, -0.1),
                    (cx - 0.13, -0.45, 0.78), (cx + 0.13, -0.45, 0.78), (cx - 0.13, -0.67, 0.78), (cx + 0.13, -0.67, 0.78)]),
              RED, bone)
        # the top plate, a step higher, and its yellow stripe
        m.add(hull([(cx - 0.12, -0.40, -0.05), (cx + 0.12, -0.40, -0.05), (cx - 0.1, -0.37, 0.55), (cx + 0.1, -0.37, 0.55),
                    (cx - 0.13, -0.44, 0.6), (cx + 0.13, -0.44, 0.6), (cx - 0.15, -0.44, -0.05), (cx + 0.15, -0.44, -0.05)]),
              RED2, bone)
        m.add(box(0.05, 0.012, 0.5).move(cx, -0.37, 0.25), YELLOW, bone)
        # the knuckle block and the joints
        m.add(box(0.24, 0.17, 0.1, bevel=0.02).move(cx, -0.56, 0.82), GREY, bone)
        for i in range(3):
            m.add(box(0.06, 0.05, 0.04).move(cx - 0.07 + i * 0.07, -0.47, 0.88), DARK, bone)
        m.add(cylinder(0.06, 0.36, segs=8, axis="x").move(cx - 0.18, -0.70, 0.2), GREY, bone)
    # the blade: a dark hilt, the green edge, a brighter core
    m.add(box(0.07, 0.07, 0.14).move(-0.80, -0.47, 0.92), DARK, BL)
    m.add(hull([(-0.80, -0.50, 0.95), (-0.80, -0.43, 0.95), (-0.80, -0.50, 2.2), (-0.775, -0.47, 1.0),
                (-0.825, -0.47, 1.0), (-0.80, -0.455, 2.1)]), GREEN, BL)
    m.add(box(0.012, 0.012, 1.0).move(-0.80, -0.465, 1.5), Mat(0xE0FFE8, emissive=True), BL)
    # right: the hexagon emitter on a dark ring, the barrels below
    m.add(cylinder(0.25, 0.05, segs=6, axis="z").turn(rz=30).move(0.80, -0.55, 0.82), DARK, R)
    m.add(facing(hexagon(0.21).move(0.80, -0.55, 0.875), (0, 0, 1)), GREEN2, R)
    m.add(facing(hexagon(0.09).move(0.80, -0.55, 0.88), (0, 0, 1)), Mat(0xE0FFE8, emissive=True), R)
    for dx in (-0.08, 0.08):
        m.add(cylinder(0.045, 0.28, segs=8, axis="z").move(0.80 + dx, -0.70, 0.82), DARK, R)
        m.add(cylinder(0.026, 0.01, segs=8, axis="z").move(0.80 + dx, -0.70, 1.1), GREEN, R)
    return m.weld()


def fp_clips(sk):
    out = []

    def idle(t):
        k = math.sin(2 * math.pi * t / 2.0)
        return {"root": (0.6 * k, 0, 0, 0, 0.01 * k, 0)}
    out.append(sample(idle, 2.0, 8, name="idle"))

    def walk(t):
        ph = t / 0.62
        return {"root": (0, 1.5 * math.sin(2 * math.pi * ph), 1.0 * math.sin(2 * math.pi * ph), 0.02 * math.sin(2 * math.pi * ph),
                         0.03 * math.cos(4 * math.pi * ph), 0)}
    out.append(sample(walk, 0.62, 8, name="walk"))

    def saber(t):
        u = (t % 0.66) / 0.66
        side = 1 if t < 0.66 else -1
        sweep = (-50 + 100 * _sm(u * 1.2)) * side
        return {"arm.L": (-20 * math.sin(math.pi * u), sweep, 30 * side * math.sin(math.pi * u), 0.4 * math.sin(math.pi * u), 0.1, 0)}
    out.append(sample(saber, 1.32, 16, name="saber"))

    def barrier(t):
        return {"arm.R": (-8, -12, 0, -0.12, 0.08, 0.05)}
    out.append(sample(barrier, 1.0, 2, name="barrier"))

    def strike(t):
        u = t / 0.6
        k = math.sin(math.pi * min(1.0, u * 1.5))
        return {"arm.R": (-8 - 6 * k, -12, 0, -0.12, 0.08, 0.05 + 0.4 * k), "root": (-4 * k, 0, 0)}
    out.append(sample(strike, 0.6, 9, loop=False, name="strike"))

    def repeater(t):
        u = (t % 0.128) / 0.128
        k = max(0.0, 1 - u * 2)
        return {"arm.R": (-4, -10, 0, -0.15, 0.15, -0.05 * k)}
    out.append(sample(repeater, 0.128, 4, name="repeater", mode=0))

    def limit(t):
        u = t / 1.2
        spin = _sm((u - 0.3) / 0.5)
        return {"arm.L": (-30, -80 + 200 * spin, 40, 0.5, 0.2, 0.2), "root": (0, 0, 10 * math.sin(math.pi * spin))}
    out.append(sample(limit, 1.2, 14, loop=False, name="limit"))

    def raise_(t):
        u = 1 - _sm(t / 0.35)
        return {"root": (25 * u, 0, 0, 0, -0.5 * u, 0)}
    out.append(sample(raise_, 0.35, 5, loop=False, name="raise"))
    return out


def pfp_skeleton():
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.1, 0))
    sk.bone("gun", "root", (0.12, -0.16, 0.18), (0.12, -0.16, 0.45))
    return sk


def pfp_mesh(sk, name="kaiju_pfp"):
    m = Mesh(name)
    g = sk.index["gun"]
    m.add(box(0.07, 0.08, 0.30).move(0.12, -0.14, 0.32), GUN, g, 0, 3)
    m.add(box(0.072, 0.014, 0.16).move(0.12, -0.096, 0.32), GREEN, g, 0, 3)
    m.add(cylinder(0.024, 0.12, segs=8, axis="z").move(0.12, -0.13, 0.47), GUN, g, 0, 3)
    m.add(ellipsoid(0.05, 0.05, 0.06, segs=6, rings=4).move(0.12, -0.19, 0.20), SKIN, g, 0, 3)
    m.add(ellipsoid(0.05, 0.05, 0.06, segs=6, rings=4).move(0.04, -0.18, 0.40), SKIN, g, 0, 3)
    return m.weld()


def pfp_clips(sk):
    out = []

    def idle(t):
        k = math.sin(2 * math.pi * t / 2.0)
        return {"root": (0.8 * k, 0, 0, 0, 0.004 * k, 0)}
    out.append(sample(idle, 2.0, 8, name="idle"))

    def run(t):
        ph = t / 0.42
        return {"root": (0, 2 * math.sin(2 * math.pi * ph), 0, 0.012 * math.sin(2 * math.pi * ph),
                         0.014 * math.cos(4 * math.pi * ph), 0)}
    out.append(sample(run, 0.42, 8, name="run"))

    def fire(t):
        u = (t % 0.128) / 0.128
        k = max(0.0, 1 - u * 2)
        return {"gun": (-6 * k, 0, 0, 0, 0.008 * k, -0.03 * k)}
    out.append(sample(fire, 0.128, 4, name="fire", mode=0))

    def raise_(t):
        u = 1 - _sm(t / 0.3)
        return {"root": (30 * u, 0, 0, 0, -0.3 * u, 0)}
    out.append(sample(raise_, 0.3, 5, loop=False, name="raise"))
    return out


def build():
    out = []
    sk = mech_skeleton()
    out.append((mech_mesh(sk), sk, mech_clips(sk)))
    sk = pilot_skeleton()
    out.append((pilot_mesh(sk), sk, pilot_clips(sk)))
    sk = fp_skeleton()
    fm = fp_mesh(sk)
    # the arms a little higher and closer in than they are on the mech:
    # more of the gauntlets shows in the lower corners
    move = lambda p: (p[0] * 0.9, p[1] + 0.13, p[2])        # noqa: E731
    fm.verts = [move(v) for v in fm.verts]
    sk.bones = [(n, par, move(h), move(t)) for n, par, h, t in sk.bones]
    out.append((fm, sk, fp_clips(sk)))
    sk = pfp_skeleton()
    out.append((pfp_mesh(sk), sk, pfp_clips(sk)))
    return out
