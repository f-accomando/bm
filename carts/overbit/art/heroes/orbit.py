"""
Orbit - support (the kit of Juno): a young explorer born on a space station.

A white space suit with orange shoulder and knee pads and teal light panels,
a clear bubble helmet over a freckled face and curly coral hair, a small
thruster pack on the back, jet boots with glowing teal soles (she hovers).
The Mediblaster: a compact curved blaster, white and orange, with a teal
glowing emitter.

Models: orbit (third person), orbit_fp.
"""
import math

from geo import Mat, Mesh, box, cylinder, ellipsoid, hull, lathe, tube
import fp
import humanoid
from humanoid import Body
from rig import Skeleton, sample

SKIN = Mat(0xF4C9A8)
HAIR = Mat(0xF06A5A)
SUIT = Mat(0xF2F4F6, glossy=True)
SUIT2 = Mat(0xC8CED6)
ORANGE = Mat(0xF28A2E, glossy=True)
TEAL = Mat(0x3FF2D8, emissive=True)
GLASS = Mat(0xCFF6FF, glossy=True, screen=True)
DARK = Mat(0x3A404A, glossy=True)
EYE_W = Mat(0xFFFFFF)
EYE_I = Mat(0x5A3A2A)

BODY = Body(height=1.66, shoulders=0.19, hips=0.11, arm=0.98, leg=1.0, head=1.06)
GRIP = None


def skeleton():
    global GRIP
    sk = humanoid.skeleton(BODY)
    GRIP = humanoid.gun_bone(sk, 0.35)
    return sk


def blaster(m, g, hx, hy, hz):
    m.add(hull([(hx - 0.035, hy + 0.0, hz - 0.06), (hx + 0.035, hy + 0.0, hz - 0.06), (hx - 0.035, hy + 0.09, hz - 0.04),
                (hx + 0.035, hy + 0.09, hz - 0.04), (hx - 0.03, hy + 0.03, hz + 0.24), (hx + 0.03, hy + 0.03, hz + 0.24),
                (hx - 0.03, hy + 0.08, hz + 0.22), (hx + 0.03, hy + 0.08, hz + 0.22)]), SUIT, g, 0, 3)
    m.add(box(0.074, 0.03, 0.14).move(hx, hy + 0.09, hz + 0.05), ORANGE, g, 1, 3)
    m.add(cylinder(0.028, 0.04, segs=8, axis="z").move(hx, hy + 0.055, hz + 0.23), TEAL, g, 1, 3)
    m.add(box(0.035, 0.09, 0.045).move(hx, hy - 0.035, hz + 0.0), DARK, g, 1, 3)


def mesh(sk, name="orbit"):
    b = BODY
    m = Mesh(name)
    B = sk.index
    k = b.k
    hs = b.head * k
    ny, cy, hy = b.neck_y, b.chest_y, b.hip_y
    humanoid.body(m, sk, b, {"skin": SKIN, "top": SUIT, "top2": SUIT2, "sleeve": SUIT, "cuff": SUIT, "hand": SUIT2,
                             "pants": SUIT, "shin": SUIT, "boot": DARK, "belt": ORANGE},
                  chest=(1.1, 1.12), waist=1.1, hips=1.1, arms=1.2, legs_k=1.15)
    H, C, S, HP = B["head"], B["chest"], B["spine"], B["hips"]
    # -- the head inside the bubble: curls, freckles (dots), eyes
    m.add(ellipsoid(0.096 * hs, 0.08 * hs, 0.104 * hs, segs=8, rings=4).move(0, ny + 0.17 * hs, -0.012), HAIR, H, 2, 3)
    m.add(ellipsoid(0.096 * hs, 0.08 * hs, 0.104 * hs, segs=6, rings=3).move(0, ny + 0.17 * hs, -0.012), HAIR, H, 0, 1)
    for i, (x, y, z) in enumerate([(-0.07, 0.2, 0.05), (0.07, 0.2, 0.05), (-0.09, 0.14, -0.02), (0.09, 0.14, -0.02),
                                   (0.0, 0.25, 0.02), (-0.05, 0.24, -0.06), (0.05, 0.24, -0.06)]):
        m.add(ellipsoid(0.035 * hs, 0.035 * hs, 0.035 * hs, segs=5, rings=3).move(x * hs, ny + y * hs, z * hs), HAIR, H, 3 if i > 3 else 2, 3)
    humanoid.eyes(m, sk, b, EYE_W, EYE_I, x=0.035, y=0.13, z=0.086, r=0.015)
    m.add(ellipsoid(0.013 * hs, 0.018 * hs, 0.018 * hs, segs=5, rings=3).move(0, ny + 0.105 * hs, 0.1 * hs), SKIN, H, 2, 3)
    # the bubble helmet: a clear dome over the head, the collar ring
    m.add(ellipsoid(0.15 * hs, 0.16 * hs, 0.15 * hs, segs=10, rings=6).move(0, ny + 0.14 * hs, 0.005), GLASS, H, 1, 3)
    m.add(lathe([(0.13 * hs, ny - 0.02 * k), (0.14 * hs, ny + 0.02 * k)], segs=10, close_top=False, close_bottom=False),
          ORANGE, C, 1, 3)
    m.add(box(0.05 * hs, 0.012, 0.012).move(0.12 * hs, ny + 0.16 * hs, 0.06 * hs), TEAL, H, 3, 3)
    # -- the suit: a chest panel with lights, orange shoulder and knee pads
    m.add(box(0.16 * k, 0.12 * k, 0.03).move(0, cy + 0.10 * k, 0.12 * k), SUIT2, C, 2, 3)
    for i in range(3):
        m.add(box(0.025 * k, 0.025 * k, 0.012).move((-0.04 + i * 0.04) * k, cy + 0.10 * k, 0.137 * k), TEAL if i != 1 else ORANGE, C, 3, 3)
    for s in ("L", "R"):
        sh = sk.bones[B[f"upperarm.{s}"]][2]
        m.add(ellipsoid(0.085 * k, 0.06 * k, 0.085 * k, segs=6, rings=4).move(sh[0], sh[1] + 0.03 * k, 0), ORANGE,
              B[f"upperarm.{s}"], 1, 3)
        kn = sk.bones[B[f"shin.{s}"]][2]
        m.add(box(0.09 * k, 0.09 * k, 0.05 * k, bevel=0.012).move(kn[0], kn[1], kn[2] + 0.07 * k), ORANGE, B[f"shin.{s}"], 2, 3)
        an = sk.bones[B[f"foot.{s}"]][2]
        m.add(box(0.1 * k, 0.02, 0.2 * k).move(an[0], 0.012, an[2] + 0.05 * k), TEAL, B[f"foot.{s}"], 1, 3)
        m.add(lathe([(0.07 * k, an[1] + 0.02), (0.075 * k, an[1] + 0.1 * k)], segs=8, close_bottom=False).move(an[0], 0, an[2]),
              SUIT2, B[f"shin.{s}"], 2, 3)
    # the thruster pack: a rounded box and two nozzles
    m.add(box(0.22 * k, 0.26 * k, 0.1 * k, bevel=0.03).move(0, cy + 0.06 * k, -0.15 * k), SUIT, C, 0, 3)
    for sx in (-1, 1):
        m.add(cylinder(0.035 * k, 0.07 * k, segs=6, r2=0.045 * k).move(sx * 0.07 * k, cy - 0.12 * k, -0.17 * k), DARK, C, 2, 3)
        m.add(cylinder(0.03 * k, 0.01, segs=6).move(sx * 0.07 * k, cy - 0.125 * k, -0.17 * k), TEAL, C, 2, 3)
    m.add(box(0.2 * k, 0.03 * k, 0.02).move(0, cy + 0.12 * k, -0.2 * k), ORANGE, C, 2, 3)
    # -- the blaster
    g = B["gun"]
    hx, hy2, hz = sk.bones[g][2]
    blaster(m, g, hx, hy2, hz)
    return m.weld()


def _sm(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def clips(sk):
    b = BODY
    out = humanoid.clips(sk, b, "pistol", fire_rate=0.12, recoil=0.03, run_period=0.6, run_stride=0.9, grip=GRIP)

    def hp(base=None, **kw):
        return humanoid.hold_pose(sk, b, "pistol", base, grip=GRIP, **kw)

    def hover(t):
        # floating: the legs hang loose and sway, the free arm out for balance
        k = math.sin(2 * math.pi * t / 1.6)
        p = {"thigh.L": (-15 + 5 * k, 0, -4), "thigh.R": (-5 - 5 * k, 0, 4), "shin.L": (30, 0, 0), "shin.R": (15, 0, 0),
             "foot.L": (25, 0, 0), "foot.R": (20, 0, 0), "root": (0, 0, 0, 0, 0.03 * k, 0)}
        p = hp(p)
        p["upperarm.L"] = (-10, 0, -40 + 6 * k)
        return p
    out.append(sample(hover, 1.6, 8, name="hover"))

    def torpedo(t):
        # the free hand up to the helmet: locking the targets
        p = hp({})
        p["upperarm.L"] = (-120, 0, -20)
        p["forearm.L"] = (-90, 0, 0)
        p["head"] = (-4, 0, 0)
        return p
    out.append(sample(torpedo, 1.0, 2, name="torpedo"))

    def ring(t):
        u = t / 0.6
        k = math.sin(math.pi * min(1.0, u * 1.4))
        p = hp({})
        p["upperarm.L"] = (-85 * k, 0, -10)
        p["forearm.L"] = (-5, 0, 0)
        return p
    out.append(sample(ring, 0.6, 8, loop=False, name="ring"))

    def call(t):
        u = t / 1.0
        k = math.sin(math.pi * min(1.0, u * 1.2))
        p = hp({})
        p["upperarm.L"] = (-170 * k, 0, -15)
        p["forearm.L"] = (-10, 0, 0)
        p["head"] = (-20 * k, 0, 0)
        return p
    out.append(sample(call, 1.0, 10, loop=False, name="orbital"))
    return out


# ------------------------------------------------------------------ first person

def fp_skeleton():
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.1, 0))
    sk.bone("gun", "root", (0.18, -0.20, 0.34), (0.18, -0.20, 0.64))
    sk.bone("hand.L", "root", (-0.2, -0.32, 0.36), (-0.18, -0.26, 0.4))
    return sk


def fp_mesh(sk, name="orbit_fp"):
    m = Mesh(name)
    g, L = sk.index["gun"], sk.index["hand.L"]
    x, y, z = 0.18, -0.20, 0.34
    m.add(hull([(x - 0.038, y + 0.0, z - 0.06), (x + 0.038, y + 0.0, z - 0.06), (x - 0.038, y + 0.1, z - 0.04),
                (x + 0.038, y + 0.1, z - 0.04), (x - 0.032, y + 0.03, z + 0.26), (x + 0.032, y + 0.03, z + 0.26),
                (x - 0.032, y + 0.085, z + 0.24), (x + 0.032, y + 0.085, z + 0.24)]), SUIT, g)
    m.add(box(0.078, 0.03, 0.15).move(x, y + 0.1, z + 0.05), ORANGE, g)
    m.add(cylinder(0.03, 0.04, segs=10, axis="z").move(x, y + 0.058, z + 0.25), TEAL, g)
    m.add(box(0.06, 0.008, 0.1).move(x, y + 0.117, z + 0.05), TEAL, g)
    fp.forearm(m, g, (x + 0.01, y - 0.07, z + 0.0), (x + 0.14, y - 0.26, z - 0.32), SUIT, r=0.055, cuff=ORANGE)
    fp.fist(m, g, (x + 0.01, y - 0.07, z + 0.0), (x, y, z + 0.05), SUIT2, r=0.045, thumb=-1)
    # the free hand, low on the left (it comes up for the torpedoes)
    fp.forearm(m, L, (-0.2, -0.32, 0.36), (-0.32, -0.5, 0.0), SUIT, r=0.055, cuff=ORANGE)
    fp.fist(m, L, (-0.2, -0.32, 0.36), (-0.19, -0.25, 0.43), SUIT2, r=0.045, thumb=1)
    m.add(box(0.03, 0.012, 0.03).move(-0.2, -0.29, 0.40), TEAL, L)
    return m.weld()


def fp_clips(sk):
    out = []

    def idle(t):
        k = math.sin(2 * math.pi * t / 2.0)
        return {"root": (0.6 * k, 0, 0, 0, 0.004 * k, 0)}
    out.append(sample(idle, 2.0, 8, name="idle"))

    def run(t):
        ph = t / 0.6
        return {"root": (0, 2 * math.sin(2 * math.pi * ph), 0, 0.014 * math.sin(2 * math.pi * ph),
                         0.012 * math.cos(4 * math.pi * ph), 0)}
    out.append(sample(run, 0.6, 8, name="run"))

    def fire(t):
        u = (t % 0.12) / 0.12
        k = max(0.0, 1 - u * 2)
        return {"gun": (-4 * k, 0, 0, 0, 0.008 * k, -0.03 * k)}
    out.append(sample(fire, 0.12, 4, name="fire", mode=0))

    def reload(t):
        u = t / 1.5
        dip = math.sin(math.pi * u)
        return {"gun": (25 * dip, 0, -30 * dip, -0.05 * dip, -0.1 * dip, 0)}
    out.append(sample(reload, 1.5, 10, loop=False, name="reload"))

    def torpedo(t):
        return {"hand.L": (-30, 0, 0, 0.12, 0.18, 0.05)}
    out.append(sample(torpedo, 1.0, 2, name="torpedo"))

    def ring(t):
        u = t / 0.6
        k = math.sin(math.pi * min(1.0, u * 1.4))
        return {"hand.L": (-40 * k, 0, 0, 0.1 * k, 0.15 * k, 0.2 * k)}
    out.append(sample(ring, 0.6, 8, loop=False, name="ring"))

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
