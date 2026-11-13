"""
Akari - support (the kit of Kiriko): a shrine guardian with paper charms.

A short silver bob with a golden hairpin; a crimson haori jacket with gold
cloud patterns over a white top, a wide black belt with paper charms
hanging from it, dark green wide trousers, wrapped shins, wooden sandals.
Healing charms (white paper with red marks) fan out from her right hand,
kunai in the left. Her spirit fox runs ahead for the ultimate.

Models: akari (third person: the Meshy figure art/meshy/akari, on this
skeleton by meshyrig.py; the body below with --classic), akari_fp,
akari_fox (the spirit fox).
"""
import math

from geo import Mat, Mesh, box, cylinder, ellipsoid, hull, lathe, tube
import fp
import humanoid
from humanoid import Body
from rig import Skeleton, sample

SKIN = Mat(0xF1CDB0)
HAIR = Mat(0x16141A)
STREAK = Mat(0xEDEDF2)
JACKET = Mat(0x2C3160)
JACKET2 = Mat(0x22264A)
WHITE = Mat(0xF2EEE8)
RED = Mat(0xD8323A)
LEGS = Mat(0x1E1E26)
WRAP = Mat(0xD8D2C4)
BOOT = Mat(0x2A2A30)
MASK = Mat(0xF8F6F2, glossy=True)
MASK_R = Mat(0xE83A3A)
PAPER = Mat(0xFFF8E8, emissive=True)
STEEL = Mat(0xB8C0C8, glossy=True)
SPIRIT = Mat(0x5FE8FF, emissive=True, screen=True)
SPIRIT2 = Mat(0xCFFBFF, emissive=True)
EYE_W = Mat(0xFFFFFF)
EYE_I = Mat(0x3A2A2A)

BODY = Body(height=1.68, shoulders=0.185, hips=0.105, arm=0.98, leg=1.03, head=1.05)
GRIP = None


def skeleton():
    global GRIP
    b = BODY
    k = b.k
    sk = humanoid.skeleton(b, extra=[("tail.L", "spine", (-0.06 * k, b.hip_y + 0.05 * k, -0.11 * k), (-0.08 * k, b.hip_y - 0.35 * k, -0.16 * k)),
                                     ("tail.R", "spine", (-0.02 * k, b.hip_y + 0.05 * k, -0.12 * k), (0.0, b.hip_y - 0.42 * k, -0.18 * k))])
    GRIP = humanoid.gun_bone(sk, 0.2)
    hand = sk.bones[sk["hand.L"]][2]
    sk.bone("kunai", "hand.L", hand, (hand[0], hand[1] - 0.05, hand[2] + 0.22))
    return sk


def charms(m, g, hx, hy, hz, n=3, lo=1):
    """a fan of paper charms held between the fingers"""
    for i in range(n):
        a = (i - (n - 1) / 2) * 16
        p = box(0.035, 0.002, 0.11).turn(ry=a).move(0, 0, 0.05)
        m.add(p.move(hx, hy + 0.01 + i * 0.004, hz + 0.02), PAPER, g, lo, 3)
        m.add(box(0.012, 0.004, 0.05).turn(ry=a).move(hx + math.sin(math.radians(a)) * 0.05, hy + 0.013 + i * 0.004,
                                                       hz + 0.02 + math.cos(math.radians(a)) * 0.05), MASK_R, g, 3, 3)


def kunai(m, bone, hx, hy, hz, lo=1):
    m.add(hull([(hx - 0.012, hy - 0.01, hz + 0.08), (hx + 0.012, hy - 0.01, hz + 0.08), (hx, hy + 0.004, hz + 0.08),
                (hx, hy - 0.01, hz + 0.2)]), STEEL, bone, lo, 3)
    m.add(cylinder(0.008, 0.08, segs=4, axis="z").move(hx, hy - 0.01, hz + 0.0), HAIR, bone, lo, 3)
    m.add(lathe([(0.016, 0.0), (0.016, 0.006)], segs=6, close_top=False, close_bottom=False).turn(rx=90)
          .move(hx, hy - 0.01, hz - 0.01), RED, bone, 3, 3)


def mesh(sk, name="akari"):
    b = BODY
    m = Mesh(name)
    B = sk.index
    k = b.k
    hs = b.head * k
    ny, cy, hy = b.neck_y, b.chest_y, b.hip_y
    humanoid.body(m, sk, b, {"skin": SKIN, "top": JACKET, "top2": WHITE, "sleeve": JACKET, "cuff": WHITE, "hand": SKIN,
                             "pants": LEGS, "shin": WRAP, "boot": BOOT, "belt": RED},
                  chest=(0.95, 1.0), waist=0.92, hips=1.0, arms=0.95, legs_k=0.92)
    H, C, S, HP = B["head"], B["chest"], B["spine"], B["hips"]
    # -- the head: black hair, a white streak, a ponytail; the fox mask on the side
    m.add(ellipsoid(0.095 * hs, 0.082 * hs, 0.104 * hs, segs=8, rings=4).move(0, ny + 0.168 * hs, -0.01), HAIR, H, 3, 3)
    m.add(ellipsoid(0.095 * hs, 0.082 * hs, 0.104 * hs, segs=6, rings=3).move(0, ny + 0.168 * hs, -0.01), HAIR, H, 0, 2)
    m.add(hull([(-0.09 * hs, ny + 0.20 * hs, 0.05 * hs), (0.09 * hs, ny + 0.20 * hs, 0.05 * hs),
                (-0.085 * hs, ny + 0.13 * hs, 0.09 * hs), (0.02 * hs, ny + 0.15 * hs, 0.1 * hs),
                (0.07 * hs, ny + 0.16 * hs, 0.095 * hs)], smooth=True), HAIR, H, 3, 3)              # fringe
    m.add(box(0.02 * hs, 0.012, 0.08 * hs).turn(rx=-20).move(0.045 * hs, ny + 0.245 * hs, 0.04 * hs), STREAK, H, 3, 3)
    m.add(tube((0, ny + 0.2 * hs, -0.09 * hs), (0, ny + 0.03 * hs, -0.17 * hs), 0.035 * hs, 0.015 * hs, segs=5), HAIR, H, 1, 3)
    m.add(cylinder(0.022 * hs, 0.025, segs=6).move(0, ny + 0.175 * hs, -0.1 * hs), RED, H, 3, 3)
    humanoid.eyes(m, sk, b, EYE_W, EYE_I, x=0.034, y=0.128, z=0.088, r=0.015)
    m.add(ellipsoid(0.012 * hs, 0.017 * hs, 0.017 * hs, segs=5, rings=3).move(0, ny + 0.105 * hs, 0.1 * hs), SKIN, H, 3, 3)
    # the fox mask on the left side of the head: a white muzzle and ears, red marks
    mx, my, mz = -0.1 * hs, ny + 0.17 * hs, 0.02 * hs
    m.add(hull([(mx, my + 0.05 * hs, mz - 0.05 * hs), (mx, my + 0.05 * hs, mz + 0.05 * hs), (mx, my - 0.04 * hs, mz - 0.04 * hs),
                (mx, my - 0.04 * hs, mz + 0.04 * hs), (mx - 0.07 * hs, my - 0.01 * hs, mz)], smooth=True), MASK, H, 1, 3)
    for dz in (-0.035, 0.035):
        m.add(hull([(mx, my + 0.04 * hs, mz + dz * hs - 0.015 * hs), (mx, my + 0.04 * hs, mz + dz * hs + 0.015 * hs),
                    (mx - 0.01 * hs, my + 0.04 * hs, mz + dz * hs), (mx + 0.005, my + 0.1 * hs, mz + dz * hs)]), MASK, H, 3, 3)
    m.add(box(0.004, 0.012 * hs, 0.05 * hs).move(mx - 0.035 * hs, my + 0.01 * hs, mz), MASK_R, H, 3, 3)
    # -- the jacket: hood down, white trim, open front over the white top; the sash
    m.add(hull([(-0.11 * k, ny + 0.03 * k, -0.07 * k), (0.11 * k, ny + 0.03 * k, -0.07 * k), (-0.09 * k, ny - 0.12 * k, -0.13 * k),
                (0.09 * k, ny - 0.12 * k, -0.13 * k), (0, ny + 0.05 * k, -0.14 * k)], smooth=True), JACKET2, C, 3, 3)
    m.add(hull([(-0.06 * k, cy + 0.20 * k, 0.1 * k), (0.06 * k, cy + 0.20 * k, 0.1 * k), (-0.07 * k, cy - 0.02 * k, 0.115 * k),
                (0.07 * k, cy - 0.02 * k, 0.115 * k), (0, cy + 0.10 * k, 0.13 * k)]), WHITE, C, 1, 3)
    for sx in (-1, 1):
        m.add(box(0.012, 0.25 * k, 0.012).turn(rz=sx * 8).move(sx * 0.075 * k, cy + 0.09 * k, 0.118 * k), WHITE, C, 3, 3)
    m.add(lathe([(0.20 * k, hy - 0.10 * k), (0.15 * k, hy + 0.0)], segs=8, close_top=False).scale(1, 1, 0.75), JACKET, HP, 3, 3)
    m.add(box(0.06 * k, 0.07 * k, 0.04 * k).move(-0.06 * k, hy + 0.06 * k, -0.115 * k), RED, S, 3, 3)          # the knot
    for side in ("L", "R"):
        tb = B[f"tail.{side}"]
        h0, t0 = sk.bones[tb][2], sk.bones[tb][3]
        m.add(tube(h0, t0, 0.03 * k, 0.04 * k, segs=4).scale(1, 1, 1), RED, tb, 1, 3)
    # -- charms in the right hand, a kunai in the left
    g = B["gun"]
    hx, hy2, hz = sk.bones[g][2]
    charms(m, g, hx, hy2 - 0.03, hz)
    kb = B["kunai"]
    kx, ky, kz = sk.bones[kb][2]
    kunai(m, kb, kx, ky, kz)
    return m.weld()


def _sm(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def clips(sk):
    b = BODY
    out = humanoid.clips(sk, b, "throw", fire_rate=0.12, recoil=0.03, run_period=0.56, run_stride=1.0, grip=GRIP)
    for c in out:                                     # the sash flutters
        for t, pose in c.keys:
            ph = 2 * math.pi * t / max(0.1, c.length)
            sw = 12 if c.name in ("run", "walk") else 5
            pose["tail.L"] = (25 + sw * math.sin(ph), 0, -5)
            pose["tail.R"] = (20 + sw * math.sin(ph + 1), 0, 5)

    def hp(base=None, **kw):
        return humanoid.hold_pose(sk, b, "throw", base, grip=GRIP, **kw)

    def ofuda(t):
        # a flick of the charms forward
        u = (t % 0.25) / 0.25
        k = math.sin(math.pi * u)
        p = hp({})
        p["upperarm.R"] = (-60 - 30 * k, 0, 15)
        p["forearm.R"] = (-30 + 20 * k, 0, 0)
        return p
    out.append(sample(ofuda, 0.25, 5, name="ofuda"))

    def kunai_(t):
        u = t / 0.5
        wind = _sm(u / 0.3) * (1 - _sm((u - 0.3) / 0.2))
        rel = _sm((u - 0.3) / 0.2) * (1 - _sm((u - 0.7) / 0.3))
        p = hp({})
        p["upperarm.L"] = (-120 * wind - 70 * rel, 0, -20)
        p["forearm.L"] = (-80 * wind - 5 * rel, 0, 0)
        p["chest"] = (0, 20 * wind - 15 * rel, 0)
        return p
    out.append(sample(kunai_, 0.5, 9, loop=False, name="kunai"))

    def suzu(t):
        u = t / 0.5
        k = math.sin(math.pi * min(1.0, u * 1.5))
        p = hp({})
        p["upperarm.L"] = (-130 * k, 0, -10)
        p["forearm.L"] = (-20 * k, 0, 0)
        return p
    out.append(sample(suzu, 0.5, 8, loop=False, name="suzu"))

    def step(t):
        # a crouch with the charms up: she vanishes in leaves
        u = t / 0.4
        k = math.sin(math.pi * min(1.0, u))
        p = humanoid.legs(b, 0, 0, 0, -0.2 * b.k * k)
        p = hp(p)
        p["upperarm.R"] = (-150 * k, 0, 10)
        return p
    out.append(sample(step, 0.4, 6, loop=False, name="step"))

    def rush(t):
        u = t / 1.2
        k = math.sin(math.pi * min(1.0, u * 1.2))
        p = humanoid.legs(b, 0, 0, 0, -0.1 * b.k * k)
        p.update(humanoid.REST_ARMS)
        p["upperarm.R"] = (-90 * k, 0, 0)
        p["forearm.R"] = (-10, 0, 0)
        p["upperarm.L"] = (-90 * k, 0, 0)
        p["forearm.L"] = (-10, 0, 0)
        p["chest"] = (10 * k, 0, 0)
        return p
    out.append(sample(rush, 1.2, 10, loop=False, name="rush"))
    return out


# ------------------------------------------------------------------ first person

def fp_skeleton():
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.1, 0))
    sk.bone("hand.R", "root", (0.2, -0.24, 0.38), (0.2, -0.2, 0.46))
    sk.bone("hand.L", "root", (-0.2, -0.25, 0.38), (-0.2, -0.21, 0.46))
    return sk


def fp_mesh(sk, name="akari_fp"):
    m = Mesh(name)
    R, L = sk.index["hand.R"], sk.index["hand.L"]
    # the right hand: charms fanned between the fingers
    fp.forearm(m, R, (0.2, -0.24, 0.38), (0.34, -0.44, 0.02), JACKET, r=0.05, cuff=WHITE)
    fp.fist(m, R, (0.2, -0.24, 0.38), (0.19, -0.17, 0.44), SKIN, r=0.04, thumb=-1)
    for i in range(4):
        a = (i - 1.5) * 14
        p = box(0.04, 0.003, 0.13).turn(rx=-30, ry=a).move(0.19 + math.sin(math.radians(a)) * 0.05, -0.16 + i * 0.003,
                                                            0.48 + math.cos(math.radians(a)) * 0.03)
        m.add(p, PAPER, R)
        m.add(box(0.014, 0.005, 0.05).turn(rx=-30, ry=a).move(0.19 + math.sin(math.radians(a)) * 0.07, -0.14 + i * 0.003,
                                                              0.5 + math.cos(math.radians(a)) * 0.05), MASK_R, R)
    # the left hand: a kunai held point down
    fp.forearm(m, L, (-0.2, -0.25, 0.38), (-0.34, -0.45, 0.02), JACKET, r=0.05, cuff=WHITE)
    fp.fist(m, L, (-0.2, -0.25, 0.38), (-0.19, -0.18, 0.44), SKIN, r=0.04, thumb=1)
    m.add(hull([(-0.205, -0.22, 0.44), (-0.19, -0.22, 0.44), (-0.198, -0.21, 0.44), (-0.198, -0.24, 0.62)]), STEEL, L)
    m.add(cylinder(0.01, 0.1, segs=6, axis="z").move(-0.198, -0.225, 0.33), HAIR, L)
    m.add(lathe([(0.02, 0.0), (0.02, 0.008)], segs=8, close_top=False, close_bottom=False).turn(rx=90).move(-0.198, -0.225, 0.32), RED, L)
    return m.weld()


def fp_clips(sk):
    out = []

    def idle(t):
        k = math.sin(2 * math.pi * t / 2.0)
        return {"root": (0.7 * k, 0, 0, 0, 0.005 * k, 0)}
    out.append(sample(idle, 2.0, 8, name="idle"))

    def run(t):
        ph = t / 0.56
        return {"root": (0, 2 * math.sin(2 * math.pi * ph), 0, 0.016 * math.sin(2 * math.pi * ph),
                         0.014 * math.cos(4 * math.pi * ph), 0)}
    out.append(sample(run, 0.56, 8, name="run"))

    def ofuda(t):
        u = (t % 0.25) / 0.25
        k = math.sin(math.pi * u)
        return {"hand.R": (-20 * k, -10 * k, 0, -0.03 * k, 0.04 * k, 0.08 * k)}
    out.append(sample(ofuda, 0.25, 5, name="ofuda"))

    def kunai_(t):
        u = t / 0.5
        wind = _sm(u / 0.3) * (1 - _sm((u - 0.3) / 0.2))
        rel = _sm((u - 0.3) / 0.2) * (1 - _sm((u - 0.7) / 0.3))
        return {"hand.L": (40 * wind - 50 * rel, 0, 0, 0.05 * wind + 0.1 * rel, 0.08 * wind, -0.1 * wind + 0.15 * rel)}
    out.append(sample(kunai_, 0.5, 9, loop=False, name="kunai"))

    def reload(t):
        u = t / 1.0
        dip = math.sin(math.pi * u)
        return {"hand.R": (30 * dip, 0, -20 * dip, 0, -0.15 * dip, -0.05 * dip),
                "hand.L": (30 * dip, 0, 20 * dip, 0, -0.15 * dip, -0.05 * dip)}
    out.append(sample(reload, 1.0, 8, loop=False, name="reload"))

    def suzu(t):
        u = t / 0.5
        k = math.sin(math.pi * min(1.0, u * 1.5))
        return {"hand.L": (-60 * k, 0, 0, 0.05 * k, 0.25 * k, 0.15 * k)}
    out.append(sample(suzu, 0.5, 8, loop=False, name="suzu"))

    def raise_(t):
        u = 1 - _sm(t / 0.35)
        return {"root": (30 * u, 0, 0, 0, -0.3 * u, 0)}
    out.append(sample(raise_, 0.35, 5, loop=False, name="raise"))
    return out


# ------------------------------------------------------------------ the spirit fox

def fox_skeleton():
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.2, 0))
    sk.bone("body", "root", (0, 0.9, -0.5), (0, 0.9, 0.5))
    sk.bone("head", "body", (0, 1.0, 0.5), (0, 1.1, 0.9))
    sk.bone("tail", "body", (0, 1.0, -0.6), (0, 1.3, -1.4))
    for s, x, z in (("fl", -1, 0.4), ("fr", 1, 0.4), ("bl", -1, -0.45), ("br", 1, -0.45)):
        sk.bone(f"leg.{s}", "body", (x * 0.18, 0.85, z), (x * 0.18, 0.05, z))
    return sk


def fox_mesh(sk, name="akari_fox"):
    """a big fox of blue light, see-through, with a bright mane and tail tip"""
    m = Mesh(name)
    B = sk.index
    m.add(ellipsoid(0.28, 0.3, 0.6, segs=8, rings=5).move(0, 0.95, 0), SPIRIT, B["body"], 0, 3)
    m.add(ellipsoid(0.2, 0.2, 0.26, segs=8, rings=5).move(0, 1.1, 0.65), SPIRIT, B["head"], 0, 3)
    m.add(hull([(-0.08, 1.05, 0.85), (0.08, 1.05, 0.85), (0, 1.12, 0.85), (0, 1.02, 1.08)]), SPIRIT2, B["head"], 0, 3)
    for x in (-1, 1):
        m.add(hull([(x * 0.06, 1.25, 0.6), (x * 0.16, 1.25, 0.62), (x * 0.1, 1.25, 0.7), (x * 0.13, 1.48, 0.62)]), SPIRIT2,
              B["head"], 0, 3)
    m.add(tube((0, 1.0, -0.6), (0, 1.3, -1.4), 0.16, 0.06, segs=6), SPIRIT, B["tail"], 0, 3)
    m.add(ellipsoid(0.1, 0.1, 0.16, segs=6, rings=4).move(0, 1.32, -1.42), SPIRIT2, B["tail"], 0, 3)
    for s in ("fl", "fr", "bl", "br"):
        b = B[f"leg.{s}"]
        h, t = sk.bones[b][2], sk.bones[b][3]
        m.add(tube(h, t, 0.08, 0.05, segs=5), SPIRIT, b, 0, 3)
    return m.weld()


def fox_clips(sk):
    def run(t):
        ph = 2 * math.pi * t / 0.45
        return {"leg.fl": (40 * math.sin(ph), 0, 0), "leg.br": (40 * math.sin(ph), 0, 0),
                "leg.fr": (-40 * math.sin(ph), 0, 0), "leg.bl": (-40 * math.sin(ph), 0, 0),
                "body": (4 * math.sin(2 * ph), 0, 0, 0, 0.06 * math.sin(2 * ph), 0),
                "tail": (15 * math.sin(ph), 20 * math.sin(ph * 0.5), 0), "head": (-5 * math.sin(2 * ph), 0, 0)}
    return [sample(run, 0.45, 9, name="run")]


def build():
    sk = skeleton()
    out = [(mesh(sk), sk, clips(sk))]
    fsk = fp_skeleton()
    out.append((fp_mesh(fsk), fsk, fp_clips(fsk)))
    xsk = fox_skeleton()
    out.append((fox_mesh(xsk), xsk, fox_clips(xsk)))
    return out
