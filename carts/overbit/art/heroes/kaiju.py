"""
Kaiju - tank (the kit of D.Mon): a young pilot in a big red mech.

The mech, BIG RED: a round red body with white horns and a dome where the
pilot sits, heavy shoulders, a sword in the left fist (a steel blade with
a glowing green plasma edge, a yellow guard), a big shield on the right
forearm (red, a yellow rim, the hexagon of the barrier's emitter, the
repeater's barrels under it), thick legs with grey joints, yellow trim.
The pilot: a young woman in a cropped red bomber, a high black ponytail
with red tips and a headset, holding a mini repeater.

Models: kaiju_mech and kaiju_pilot (third person: the Meshy figures of
art/meshy, by meshyrig.py: the mech on a skeleton fitted to it with these
bones and clips, the sword and the shield from here; the bodies below with
--classic), kaiju_fp (the arms from the dome), kaiju_pfp (the gloved hands
and the mini gun).
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

FACE = Mat(0xF0C2A2)
HAIR = Mat(0x16161C)
RED_TIP = Mat(0xE2232E)
EYE = Mat(0xFFFFFF)
PUPIL = Mat(0x3A2418)
JACKET = Mat(0xE0262E, glossy=True)
COLLAR = Mat(0xF4F4F4)
TRIM = Mat(0xF2C230)
SUIT = Mat(0x1E2026)
SHORTS = Mat(0x24262C)
LEGS = Mat(0x3A3D45)
BOOT = Mat(0xD8282E, glossy=True)
SOLE = Mat(0xF0F0F0)
GLOVE = Mat(0xF2F2F2)
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
    sk.bone("shield", "fore.R", (1.30, 1.36, 0.40), (1.30, 1.36, 0.95))
    for s, x in (("L", -1), ("R", 1)):
        sk.bone(f"thigh.{s}", "hips", (x * 0.52, HIP_Y, 0), (x * 0.56, KNEE[0], KNEE[1]))
        sk.bone(f"shin.{s}", f"thigh.{s}", (x * 0.56, KNEE[0], KNEE[1]), (x * 0.56, ANKLE[0], ANKLE[1]))
        sk.bone(f"foot.{s}", f"shin.{s}", (x * 0.56, ANKLE[0], ANKLE[1]), (x * 0.56, 0.05, 0.40))
    return sk


def both(m, make, mat, bl, br, lo=0, hi=3):
    p = make()
    m.add(p, mat, bl, lo, hi)
    m.add(p.copy().mirror_x(), mat, br, lo, hi)


STEEL = Mat(0xE4E9EE, glossy=True)


def sword(m, bone, x, y, z):
    """the sword along +z from the fist at (x, y, z): a dark grip, a yellow
    guard, a broad steel blade with glowing green plasma edges"""
    m.add(cylinder(0.06, 0.30, segs=6, axis="z").move(x, y, z - 0.05), DARK, bone, 0, 3)          # the grip
    m.add(box(0.12, 0.12, 0.10).move(x, y, z - 0.16), YELLOW, bone, 1, 3)                       # the pommel
    m.add(box(0.12, 0.42, 0.10, bevel=0.02).move(x, y, z + 0.28), YELLOW, bone, 0, 3)           # the guard
    m.add(hull([(x - 0.035, y - 0.11, z + 0.33), (x + 0.035, y - 0.11, z + 0.33), (x - 0.035, y + 0.11, z + 0.33),
                (x + 0.035, y + 0.11, z + 0.33), (x - 0.03, y - 0.09, z + 1.55), (x + 0.03, y - 0.09, z + 1.55),
                (x - 0.03, y + 0.09, z + 1.55), (x + 0.03, y + 0.09, z + 1.55), (x, y, z + 1.85)]),
          STEEL, bone, 0, 3)                                                                     # the blade
    for e in (-1, 1):                                                                            # the plasma edges
        m.add(hull([(x - 0.02, y + e * 0.11, z + 0.36), (x + 0.02, y + e * 0.11, z + 0.36),
                    (x - 0.02, y + e * 0.14, z + 0.40), (x + 0.02, y + e * 0.14, z + 0.40),
                    (x - 0.015, y + e * 0.10, z + 1.56), (x + 0.015, y + e * 0.10, z + 1.56), (x, y, z + 1.88)]),
              GREEN, bone, 1, 3)
    m.add(box(0.074, 0.03, 1.0).move(x, y, z + 0.92), GREEN2, bone, 2, 3)                      # the fuller


def shield(m, bone, x, y, z, k=1.0, barrels=True):
    """the shield on the outer side of the right forearm from (x, y, z), a
    little turned to the front, k times its size: red with a yellow rim and
    the barrier's hexagon emitter; the repeater's two barrels under the
    forearm"""
    a = math.radians(30)
    n = (math.cos(a), 0.0, math.sin(a))            # facing out (+x), 30 degrees to the front
    ud = (-math.sin(a), 0.0, math.cos(a))          # along the forearm, in the shield's plane

    def at(u, v, w):
        u, v, w = u * k, v * k, w * k
        return (x + u * ud[0] + w * n[0], y + v, z + u * ud[2] + w * n[2])
    outline = [(0.0, 0.62), (0.40, 0.70), (0.80, 0.55), (0.92, 0.0), (0.70, -0.62), (0.40, -0.80), (0.10, -0.62),
               (-0.05, 0.0)]
    m.add(hull([at(u, v, 0.0) for u, v in outline] + [at(u, v, -0.10) for u, v in outline]), RED, bone, 0, 3)
    m.add(hull([at(u, v * 1.06, 0.015) for u, v in outline] + [at(u, v * 1.06, -0.12) for u, v in outline]),
          YELLOW, bone, 1, 3)
    m.add(hull([at(0.42 + (u - 0.42) * 0.85, v * 0.85, 0.035) for u, v in outline] +
               [at(0.42 + (u - 0.42) * 0.85, v * 0.85, 0.0) for u, v in outline]), RED2, bone, 1, 3)

    def hexa(r, w):
        pts = [at(0.42 + r * math.cos(math.pi / 3 * i), r * math.sin(math.pi / 3 * i), w) for i in range(6)]
        return facing(Part(pts, [(0, 1, 2), (0, 2, 3), (0, 3, 4), (0, 4, 5)], False), n)
    m.add(hexa(0.20, 0.045), GREEN2, bone, 1, 3)
    m.add(hexa(0.09, 0.05), Mat(0xE0FFE8, emissive=True), bone, 2, 3)
    for dx in (-0.08, 0.08) if barrels else ():
        m.add(cylinder(0.05, 0.40, segs=6, caps=False, axis="z").move(x - 0.2 + dx, y - 0.22, z + 0.40), DARK,
              bone, 1, 3)


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
    m.add(ellipsoid(0.10, 0.12, 0.11, segs=6, rings=4).move(0, 2.72, 0.14), FACE, B["dome"], 2, 3)
    m.add(ellipsoid(0.11, 0.10, 0.12, segs=6, rings=3).move(0, 2.78, 0.12), HAIR, B["dome"], 2, 3)
    m.add(ellipsoid(0.04, 0.10, 0.04, segs=5, rings=3).move(0, 2.80, -0.02), HAIR, B["dome"], 3, 3)
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
    sword(m, B["blade"], -1.12, 1.32, 0.75)
    shield(m, B["shield"], 1.30, 1.36, 0.40, k=0.8)
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


# ------------------------------------------------------------------ the pilot: a young mech pilot

PBODY = Body(height=1.6, shoulders=0.18, hips=0.11, arm=0.95, leg=0.98, head=1.12)


def pilot_skeleton():
    sk = humanoid.skeleton(PBODY)
    hand = sk.bones[sk["hand.R"]][2]
    sk.bone("gun", "hand.R", hand, (hand[0], hand[1], hand[2] + 0.3))
    return sk


def pilot_mesh(sk, name="kaiju_pilot"):
    """a young woman in a cropped red bomber with yellow trim over a black
    pilot top, black shorts on dark leggings, red boots, white gloves, a
    high black ponytail with red tips and a headset; the mini repeater"""
    b = PBODY
    m = Mesh(name)
    B = sk.index
    k = b.k
    hs = b.head * k
    ny, cy, hy = b.neck_y, b.chest_y, b.hip_y
    humanoid.body(m, sk, b, {"skin": FACE, "top": SUIT, "top2": SUIT, "sleeve": JACKET, "cuff": JACKET,
                             "hand": GLOVE, "pants": SHORTS, "shin": LEGS, "boot": BOOT},
                  chest=(1.05, 1.1), waist=0.92, hips=1.05, arms=1.1, legs_k=0.95)
    H, C, S, HP = B["head"], B["chest"], B["spine"], B["hips"]
    # -- the head: a black cap of hair with a fringe, the high ponytail with red tips, the headset
    m.add(ellipsoid(0.096 * hs, 0.09 * hs, 0.104 * hs, segs=8, rings=4).move(0, ny + 0.16 * hs, -0.012), HAIR, H, 2, 3)
    m.add(ellipsoid(0.096 * hs, 0.09 * hs, 0.104 * hs, segs=6, rings=3).move(0, ny + 0.16 * hs, -0.012), HAIR, H, 0, 1)
    m.add(hull([(-0.085 * hs, ny + 0.19 * hs, 0.06 * hs), (0.085 * hs, ny + 0.19 * hs, 0.06 * hs),
                (-0.07 * hs, ny + 0.155 * hs, 0.097 * hs), (0.05 * hs, ny + 0.15 * hs, 0.1 * hs),
                (0, ny + 0.215 * hs, 0.05 * hs)], smooth=True), HAIR, H, 2, 3)
    m.add(hull([(-0.03 * hs, ny + 0.25 * hs, -0.07 * hs), (0.03 * hs, ny + 0.25 * hs, -0.07 * hs),
                (0, ny + 0.27 * hs, -0.05 * hs), (-0.035 * hs, ny + 0.10 * hs, -0.17 * hs),
                (0.035 * hs, ny + 0.10 * hs, -0.17 * hs), (0, ny + 0.0, -0.17 * hs)], smooth=True), HAIR, H, 1, 3)
    m.add(hull([(-0.03 * hs, ny + 0.0, -0.17 * hs), (0.03 * hs, ny + 0.0, -0.17 * hs),
                (0, ny - 0.09 * hs, -0.15 * hs), (0, ny + 0.03 * hs, -0.19 * hs)], smooth=True), RED_TIP, H, 2, 3)
    for x in (-1, 1):
        m.add(ellipsoid(0.02 * hs, 0.035 * hs, 0.035 * hs, segs=6, rings=3).move(x * 0.098 * hs, ny + 0.13 * hs, 0.0),
              DARK, H, 2, 3)
    m.add(box(0.006, 0.006, 0.07 * hs).move(0.09 * hs, ny + 0.095 * hs, 0.05 * hs), DARK, H, 3, 3)    # the microphone
    humanoid.eyes(m, sk, b, EYE, PUPIL, x=0.034, y=0.128, z=0.088, r=0.014)
    # -- the cropped bomber: red, open, a white collar, yellow trim at the hem
    m.add(lathe([(0.15 * k, cy - 0.02 * k), (0.19 * k, cy + 0.10 * k), (0.195 * k, cy + 0.19 * k),
                 (0.12 * k, ny + 0.02 * k)], segs=8, close_top=False).scale(1, 1, 0.72), JACKET, C, 1, 3)
    m.add(lathe([(0.152 * k, cy - 0.04 * k), (0.152 * k, cy - 0.01 * k)], segs=8, close_top=False,
                close_bottom=False).scale(1, 1, 0.74), TRIM, C, 2, 3)
    m.add(hull([(-0.11 * k, ny + 0.0, 0.07 * k), (0.11 * k, ny + 0.0, 0.07 * k), (-0.12 * k, ny - 0.04 * k, 0.11 * k),
                (0.12 * k, ny - 0.04 * k, 0.11 * k), (-0.07 * k, ny + 0.03 * k, 0.02 * k), (0.07 * k, ny + 0.03 * k, 0.02 * k),
                (0, ny + 0.02 * k, -0.06 * k)], smooth=True), COLLAR, C, 2, 3)
    m.add(box(0.012, 0.30 * k, 0.012).move(0, cy - 0.03 * k, 0.122 * k), TRIM, C, 3, 3)               # the top's seam
    # boots up to the knee, white soles
    for s_ in ("L", "R"):
        an = sk.bones[B[f"foot.{s_}"]][2]
        kn = sk.bones[B[f"shin.{s_}"]][2]
        m.add(lathe([(0.058 * k, an[1] + 0.02), (0.064 * k, kn[1] - 0.02)], segs=7, close_bottom=False)
              .move(an[0], 0, an[2] + 0.01), BOOT, B[f"shin.{s_}"], 1, 3)
        m.add(box(0.12 * k, 0.02, 0.27 * k).move(an[0], 0.01, an[2] + 0.06 * k), SOLE, B[f"foot.{s_}"], 2, 3)
    # -- the mini repeater in the right hand
    g = B["gun"]
    hx, hy2, hz = sk.bones[g][2]
    m.add(box(0.06, 0.07, 0.24).move(hx, hy2 + 0.03, hz + 0.08), GUN, g, 0, 3)
    m.add(cylinder(0.02, 0.10, segs=6, axis="z").move(hx, hy2 + 0.04, hz + 0.20), GUN, g, 1, 3)
    m.add(box(0.062, 0.012, 0.12).move(hx, hy2 + 0.07, hz + 0.08), GREEN, g, 2, 3)
    return m.weld()


def pilot_clips(sk):
    hand = sk.bones[sk["hand.R"]][2]
    grip = (hand[0], hand[1] + 0.02, hand[2] + 0.18)
    out = humanoid.clips(sk, PBODY, "rifle", fire_rate=0.128, recoil=0.04, run_period=0.5, run_stride=0.8, grip=grip)

    def eject(t):
        u = _sm(t / 0.3)
        return {"chest": (-40 * u, 0, 0), "thigh.L": (-100 * u, 0, 0), "thigh.R": (-100 * u, 0, 0),
                "shin.L": (120 * u, 0, 0), "shin.R": (120 * u, 0, 0)}
    out.append(sample(eject, 0.9, 9, loop=False, name="eject"))

    def call(t):
        u = _sm(t / 0.25) * (1 - _sm((t - 0.85) / 0.25))
        return {"upperarm.L": (-170 * u, 0, -20), "upperarm.R": (-170 * u, 0, 20), "head": (-25 * u, 0, 0)}
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
    yellow stripe and grey knuckles; the left holds the sword, the right
    has the shield on its outer side, the hexagon emitter and two barrels"""
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
    # the sword in the left fist
    sword(m, BL, -0.80, -0.50, 0.80)
    # right: the shield on the outer side, the hexagon emitter on a dark ring, the barrels below
    shield(m, R, 0.99, -0.52, -0.05, k=0.55, barrels=False)
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
    m.add(ellipsoid(0.045, 0.05, 0.06, segs=6, rings=4).move(0.12, -0.19, 0.20), GLOVE, g, 0, 3)
    m.add(ellipsoid(0.045, 0.05, 0.06, segs=6, rings=4).move(0.04, -0.18, 0.40), GLOVE, g, 0, 3)
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
