"""
Frost - damage (the kit of Mei): a climate scientist with a cryo blaster.

A cheerful young researcher in a big puffy parka, white with teal panels
and a fur-trimmed hood down on her shoulders; dark hair in a bun held by a
snowflake pin, round glasses, teal mittens, navy leggings, fur-topped
boots. On her back the cryo tank, glowing ice blue; in her hands the cryo
blaster; over her right shoulder her little helper drone, Pip, a white ball
with a blue eye (it becomes the Blizzard).

Models: frost (third person), frost_fp (the blaster and the mittens).
"""
import math

from geo import Mat, Mesh, box, cylinder, ellipsoid, hull, lathe, tube
import fp
import humanoid
from humanoid import Body
from rig import Skeleton, sample

SKIN = Mat(0xF2CDAE)
HAIR = Mat(0x2B201E)
PARKA = Mat(0xEEF4F7)
PARKA2 = Mat(0x5FB8CF)
FUR = Mat(0xFAF4E8)
NAVY = Mat(0x2E3D5E)
BOOT = Mat(0x5A4334)
MITTEN = Mat(0x4FA9C2)
FRAME = Mat(0x2A2A31)
LENS = Mat(0xCFEFFF, glossy=True)
ICE = Mat(0x9FE6FF, emissive=True)
METAL = Mat(0xA7B4C0, glossy=True)
DARK = Mat(0x343B45, glossy=True)
WHITE = Mat(0xF7F9FB, glossy=True)
EYE_W = Mat(0xFFFFFF)
EYE_I = Mat(0x3A2A22)

BODY = Body(height=1.62, shoulders=0.19, hips=0.115, arm=0.95, leg=0.95, head=1.12)
GRIP = None


def skeleton():
    global GRIP
    b = BODY
    k = b.k
    sk = humanoid.skeleton(b, extra=[("drone", "chest", (0.30 * k, b.shoulder_y + 0.30 * k, -0.05), (0.30 * k, b.shoulder_y + 0.42 * k, -0.05))])
    GRIP = humanoid.gun_bone(sk, 0.45)
    return sk


def blaster(m, g, hx, hy, hz, lo_detail=True):
    """the cryo blaster along +z from the grip at (hx, hy, hz)"""
    m.add(box(0.075, 0.10, 0.30, bevel=0.015).move(hx, hy + 0.045, hz + 0.10), WHITE, g, 1, 3)
    m.add(box(0.075, 0.10, 0.30).move(hx, hy + 0.045, hz + 0.10), WHITE, g, 0, 0)
    m.add(cylinder(0.034, 0.22, segs=6, axis="z").move(hx, hy + 0.115, hz + 0.0), ICE, g, 1, 3)        # the cell on top
    m.add(lathe([(0.05, 0.0), (0.058, 0.03), (0.04, 0.07)], segs=6).turn(rx=90).move(hx, hy + 0.05, hz + 0.25),
          PARKA2, g, 3, 3)                                                                               # nozzle
    m.add(cylinder(0.026, 0.02, segs=8, axis="z").move(hx, hy + 0.05, hz + 0.32), ICE, g, 3, 3)
    m.add(box(0.04, 0.09, 0.05).move(hx, hy - 0.03, hz + 0.0), DARK, g, 1, 3)                          # grip
    m.add(box(0.035, 0.08, 0.04).move(hx, hy - 0.02, hz + 0.20), DARK, g, 3, 3)                        # front grip
    m.add(box(0.08, 0.012, 0.18).move(hx, hy + 0.0, hz + 0.10), PARKA2, g, 3, 3)                       # stripe


def drone(m, bone, x, y, z, s=1.0):
    """Pip: a white ball with a dark face and a blue eye, two little fins"""
    m.add(ellipsoid(0.09 * s, 0.085 * s, 0.09 * s, segs=8, rings=5).move(x, y, z), WHITE, bone, 3, 3)
    m.add(ellipsoid(0.09 * s, 0.085 * s, 0.09 * s, segs=5, rings=3).move(x, y, z), WHITE, bone, 0, 2)
    m.add(ellipsoid(0.06 * s, 0.045 * s, 0.03 * s, segs=6, rings=3).move(x, y + 0.01 * s, z + 0.07 * s), DARK, bone, 1, 3)
    m.add(ellipsoid(0.022 * s, 0.022 * s, 0.012 * s, segs=5, rings=3).move(x, y + 0.012 * s, z + 0.095 * s), ICE, bone, 1, 3)
    for sx in (-1, 1):
        m.add(hull([(x + sx * 0.08 * s, y + 0.02 * s, z - 0.02 * s), (x + sx * 0.08 * s, y - 0.02 * s, z - 0.02 * s),
                    (x + sx * 0.08 * s, y, z + 0.03 * s), (x + sx * 0.14 * s, y + 0.05 * s, z - 0.03 * s)]), PARKA2, bone, 3, 3)


def mesh(sk, name="frost"):
    b = BODY
    m = Mesh(name)
    B = sk.index
    k = b.k
    hs = b.head * k
    ny, cy, hy = b.neck_y, b.chest_y, b.hip_y
    humanoid.body(m, sk, b, {"skin": SKIN, "top": PARKA, "top2": PARKA, "sleeve": PARKA, "cuff": PARKA2,
                             "hand": MITTEN, "pants": NAVY, "shin": NAVY, "boot": BOOT},
                  chest=(1.28, 1.35), waist=1.5, hips=1.32, arms=1.45, legs_k=1.05)
    H, C, S, HP = B["head"], B["chest"], B["spine"], B["hips"]
    # -- the head: hair cap with a fringe, the bun and its snowflake pin, round glasses
    m.add(ellipsoid(0.095 * hs, 0.082 * hs, 0.104 * hs, segs=8, rings=4).move(0, ny + 0.168 * hs, -0.01), HAIR, H, 3, 3)
    m.add(ellipsoid(0.095 * hs, 0.082 * hs, 0.104 * hs, segs=6, rings=3).move(0, ny + 0.168 * hs, -0.01), HAIR, H, 0, 2)
    m.add(hull([(-0.085 * hs, ny + 0.19 * hs, 0.06 * hs), (0.085 * hs, ny + 0.19 * hs, 0.06 * hs),
                (-0.07 * hs, ny + 0.15 * hs, 0.095 * hs), (0.04 * hs, ny + 0.155 * hs, 0.098 * hs),
                (0, ny + 0.215 * hs, 0.05 * hs)], smooth=True), HAIR, H, 3, 3)
    m.add(ellipsoid(0.058 * hs, 0.052 * hs, 0.058 * hs, segs=7, rings=4).move(0, ny + 0.255 * hs, -0.045 * hs), HAIR, H, 1, 3)
    for a in (0, 60, 120):
        m.add(box(0.075 * hs, 0.012, 0.01).turn(rz=a).move(-0.045 * hs, ny + 0.26 * hs, 0.012 * hs), ICE, H, 3, 3)
    humanoid.eyes(m, sk, b, EYE_W, EYE_I, x=0.035, y=0.128, z=0.088, r=0.014)
    for sx in (-1, 1):
        m.add(cylinder(0.026 * hs, 0.008, segs=8, axis="z").move(sx * 0.036 * hs, ny + 0.128 * hs, 0.098 * hs), FRAME, H, 3, 3)
        m.add(cylinder(0.021 * hs, 0.009, segs=8, axis="z").move(sx * 0.036 * hs, ny + 0.128 * hs, 0.1 * hs), LENS, H, 3, 3)
    m.add(box(0.02 * hs, 0.006, 0.006).move(0, ny + 0.13 * hs, 0.104 * hs), FRAME, H, 3, 3)
    m.add(ellipsoid(0.012 * hs, 0.01 * hs, 0.01 * hs, segs=5, rings=3).move(0, ny + 0.10 * hs, 0.105 * hs), SKIN, H, 3, 3)
    # -- the parka: fur hood down round the neck, the hem, zip and pockets, the cuffs
    m.add(lathe([(0.10 * k, ny - 0.05 * k), (0.14 * k, ny - 0.01 * k), (0.12 * k, ny + 0.05 * k), (0.07 * k, ny + 0.06 * k)],
                segs=8, close_top=False).scale(1, 1, 0.9).move(0, 0, -0.02), FUR, C, 3, 3)
    m.add(hull([(-0.12 * k, ny + 0.04 * k, -0.08 * k), (0.12 * k, ny + 0.04 * k, -0.08 * k), (-0.10 * k, ny - 0.14 * k, -0.15 * k),
                (0.10 * k, ny - 0.14 * k, -0.15 * k), (0, ny + 0.06 * k, -0.16 * k)], smooth=True), PARKA, C, 3, 3)  # hood
    m.add(lathe([(0.20 * k, hy - 0.16 * k), (0.19 * k, hy - 0.04 * k), (0.165 * k, hy + 0.08 * k)], segs=8,
                close_top=False).scale(1, 1, 0.8), PARKA, HP, 3, 3)
    m.add(lathe([(0.20 * k, hy - 0.16 * k), (0.165 * k, hy + 0.08 * k)], segs=6, close_top=False).scale(1, 1, 0.8),
          PARKA, HP, 0, 2)
    m.add(lathe([(0.205 * k, hy - 0.17 * k), (0.205 * k, hy - 0.13 * k)], segs=8, close_top=False, close_bottom=False)
          .scale(1, 1, 0.82), PARKA2, HP, 3, 3)
    m.add(box(0.016 * k, 0.40 * k, 0.012).move(0, cy - 0.02 * k, 0.163 * k), PARKA2, C, 3, 3)          # zip
    for sx in (-1, 1):
        m.add(box(0.07 * k, 0.06 * k, 0.02).move(sx * 0.10 * k, hy - 0.02 * k, 0.15 * k), PARKA2, HP, 3, 3)
    # the cryo tank on the back, with its glowing window and two straps
    m.add(cylinder(0.075 * k, 0.30 * k, segs=8).move(0, cy - 0.08 * k, -0.17 * k), METAL, C, 1, 3)
    m.add(cylinder(0.075 * k, 0.30 * k, segs=5).move(0, cy - 0.08 * k, -0.17 * k), METAL, C, 0, 0)
    m.add(cylinder(0.05 * k, 0.20 * k, segs=6).move(0, cy - 0.03 * k, -0.20 * k), ICE, C, 1, 3)
    m.add(ellipsoid(0.075 * k, 0.03 * k, 0.075 * k, segs=8, rings=3).move(0, cy + 0.22 * k, -0.17 * k), DARK, C, 1, 3)
    for sx in (-1, 1):
        m.add(box(0.03 * k, 0.03 * k, 0.30 * k).move(sx * 0.09 * k, cy + 0.17 * k, -0.02 * k), DARK, C, 3, 3)
    # fur on the boots
    for s in ("L", "R"):
        an = sk.bones[B[f"foot.{s}"]][2]
        m.add(lathe([(0.07 * k, an[1] + 0.06 * k), (0.075 * k, an[1] + 0.12 * k)], segs=8, close_bottom=False)
              .move(an[0], 0, an[2]), FUR, B[f"shin.{s}"], 3, 3)
    # -- the blaster
    g = B["gun"]
    hx, hy2, hz = sk.bones[g][2]
    blaster(m, g, hx, hy2, hz)
    # -- Pip, over the right shoulder
    dh = sk.bones[B["drone"]][2]
    drone(m, B["drone"], dh[0], dh[1], dh[2])
    return m.weld()


def _sm(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def clips(sk):
    b = BODY
    out = humanoid.clips(sk, b, "blaster", fire_rate=0.1, recoil=0.012, run_period=0.6, run_stride=0.85, grip=GRIP)
    # Pip floats and looks around in the loops
    for c in out:
        for t, pose in c.keys:
            ph = 2 * math.pi * t / max(0.1, c.length)
            pose["drone"] = (6 * math.sin(ph), 25 * math.sin(ph * 0.5 + 1), 0, 0, 0.03 * math.sin(ph), 0)

    def hp(base=None, **kw):
        return humanoid.hold_pose(sk, b, "blaster", base, grip=GRIP, **kw)

    def icicle(t):
        u = t / 0.5
        k = math.sin(math.pi * min(1.0, u * 2.5)) * (1 - _sm(u))
        p = hp({}, recoil=0.05 * k, lift=0.03 * k)
        p["chest"] = (p["chest"][0] - 6 * k, p["chest"][1], p["chest"][2])
        return p
    out.append(sample(icicle, 0.5, 8, loop=False, name="icicle"))

    def freeze(t):
        # curls up inside the ice block
        u = _sm(t / 0.25)
        p = humanoid.legs(b, 0, 0, 0, -0.22 * b.k * u)
        p.update(humanoid.REST_ARMS)
        p["spine"] = (25 * u, 0, 0)
        p["chest"] = (15 * u, 0, 0)
        p["head"] = (20 * u, 0, 0)
        p["upperarm.L"] = (-70 * u, 0, -25 * u)
        p["upperarm.R"] = (-70 * u, 0, 25 * u)
        p["forearm.L"] = (-90 * u, 0, 0)
        p["forearm.R"] = (-90 * u, 0, 0)
        return p
    out.append(sample(freeze, 1.0, 6, name="freeze"))

    def wall(t):
        # the left mitten forward and down: the wall rises there
        u = t / 0.6
        k = math.sin(math.pi * min(1.0, u * 1.5))
        p = hp({})
        p["upperarm.L"] = (-75 * k, 0, -15)
        p["forearm.L"] = (-10 * k, 0, 0)
        p["chest"] = (8 * k, -15 * k, 0)
        return p
    out.append(sample(wall, 0.6, 8, loop=False, name="wall"))

    def blizzard(t):
        # Pip flies up and away from the shoulder; the arm points the way
        u = t / 0.8
        p = hp({})
        p["upperarm.L"] = (-150 * _sm(u / 0.4), 0, -10)
        p["forearm.L"] = (-15, 0, 0)
        up = _sm(u / 0.6)
        p["drone"] = (0, 360 * up, 0, 0, 1.4 * up, 2.0 * up * up)
        if u > 0.6:
            p["drone"] = (0, 0, 0, 0, -60, 0)            # gone (far below the floor) until the clip ends
        return p
    out.append(sample(blizzard, 0.8, 10, loop=False, name="blizzard"))
    return out


# ------------------------------------------------------------------ first person

def fp_skeleton():
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.1, 0))
    sk.bone("gun", "root", (0.19, -0.235, 0.34), (0.19, -0.235, 0.68))
    sk.bone("hand.L", "root", (0.09, -0.30, 0.50), (0.09, -0.24, 0.54))
    return sk


def fp_mesh(sk, name="frost_fp"):
    m = Mesh(name)
    g, L = sk.index["gun"], sk.index["hand.L"]
    x, y, z = 0.19, -0.235, 0.34
    # the blaster seen close: a white body with a teal band, the glowing cell
    # in a metal cage on top, the nozzle
    m.add(box(0.075, 0.10, 0.32, bevel=0.016).move(x, y + 0.04, z + 0.12), WHITE, g)
    m.add(box(0.077, 0.026, 0.2).move(x, y + 0.005, z + 0.12), PARKA2, g)
    m.add(cylinder(0.026, 0.2, segs=10, axis="z").move(x, y + 0.11, z + 0.0), ICE, g)
    for dx in (-0.024, 0.024):
        m.add(box(0.008, 0.035, 0.22).move(x + dx, y + 0.105, z + 0.10), METAL, g)
    m.add(lathe([(0.05, 0.0), (0.058, 0.03), (0.042, 0.07)], segs=10).turn(rx=90).move(x, y + 0.05, z + 0.27), PARKA2, g)
    m.add(cylinder(0.028, 0.02, segs=10, axis="z").move(x, y + 0.05, z + 0.335), ICE, g)
    # the right mitten on the grip, the parka sleeve
    fp.forearm(m, g, (x + 0.01, y - 0.07, z - 0.02), (x + 0.12, y - 0.24, z - 0.36), PARKA, r=0.06, cuff=PARKA2)
    fp.fist(m, g, (x + 0.01, y - 0.07, z - 0.02), (x + 0.0, y + 0.0, z + 0.04), MITTEN, r=0.05, thumb=-1)
    # the left mitten under the front
    fp.forearm(m, L, (0.10, -0.28, 0.52), (-0.16, -0.44, 0.16), PARKA, r=0.06, cuff=PARKA2)
    fp.fist(m, L, (0.10, -0.28, 0.52), (0.16, -0.22, 0.55), MITTEN, r=0.05, thumb=1)
    return m.weld()


def fp_clips(sk):
    out = []

    def idle(t):
        k = math.sin(2 * math.pi * t / 2.0)
        return {"root": (0.7 * k, 0, 0, 0, 0.004 * k, 0)}
    out.append(sample(idle, 2.0, 8, name="idle"))

    def run(t):
        ph = t / 0.6
        return {"root": (0, 2 * math.sin(2 * math.pi * ph), 0, 0.014 * math.sin(2 * math.pi * ph),
                         0.012 * math.cos(4 * math.pi * ph), 0)}
    out.append(sample(run, 0.6, 8, name="run"))

    def fire(t):
        # the stream: a fast shiver
        k = math.sin(2 * math.pi * t / 0.1)
        return {"gun": (-0.6 * k, 0, 0.6 * k, 0, 0.002 * k, -0.006)}
    out.append(sample(fire, 0.1, 4, name="fire"))

    def icicle(t):
        u = t / 0.5
        k = math.sin(math.pi * min(1.0, u * 2.5)) * (1 - _sm(u))
        return {"gun": (-9 * k, 0, 0, 0, 0.02 * k, -0.06 * k), "hand.L": (0, 0, 0, 0, 0.01 * k, -0.04 * k)}
    out.append(sample(icicle, 0.5, 8, loop=False, name="icicle"))

    def reload(t):
        u = t / 1.5
        dip = math.sin(math.pi * u)
        return {"root": (20 * dip, 0, -25 * dip, 0, -0.06 * dip, 0),
                "hand.L": (0, 0, 0, 0.0, -0.18 * dip, -0.1 * dip)}
    out.append(sample(reload, 1.5, 10, loop=False, name="reload"))

    def wall(t):
        u = t / 0.6
        k = math.sin(math.pi * min(1.0, u * 1.5))
        return {"hand.L": (-30 * k, 0, 0, -0.08 * k, 0.12 * k, 0.25 * k), "gun": (0, 0, 0, 0.04 * k, -0.04 * k, 0)}
    out.append(sample(wall, 0.6, 8, loop=False, name="wall"))

    def freeze(t):
        u = _sm(t / 0.25)
        return {"root": (40 * u, 0, 0, 0, -0.35 * u, 0)}
    out.append(sample(freeze, 1.0, 4, name="freeze"))

    def blizzard(t):
        u = t / 0.8
        k = math.sin(math.pi * min(1.0, u * 1.4))
        return {"hand.L": (-60 * k, 0, 0, -0.1 * k, 0.3 * k, 0.2 * k), "gun": (10 * k, 0, 0, 0.03 * k, -0.08 * k, 0)}
    out.append(sample(blizzard, 0.8, 8, loop=False, name="blizzard"))

    def raise_(t):
        u = 1 - _sm(t / 0.35)
        return {"root": (30 * u, 0, 0, 0, -0.3 * u, 0)}
    out.append(sample(raise_, 0.35, 5, loop=False, name="raise"))
    return out


def build():
    sk = skeleton()
    out = [(mesh(sk), sk, clips(sk))]
    fsk = fp_skeleton()
    out.append((fp_mesh(fsk), fsk, fp_clips(fsk)))
    return out
