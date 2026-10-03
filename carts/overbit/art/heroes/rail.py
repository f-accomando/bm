"""
Rail - damage (the kit of Sojourn): a commander with a rail rifle.

A tall officer: silver-white hair in a single tight braid, a cyan monocle
visor over one eye, a long fitted coat, deep navy with gold piping and a
high collar, over a white chest plate, one armoured shoulder pauldron,
dark trousers, tall black boots, black gloves. The Rail Rifle: an angular
white and navy body with two parallel prongs at the front, a cyan coil
that glows between them.

Models: rail (third person: the Meshy figure art/meshy/rail, on this
skeleton by meshyrig.py; the body below with --classic), rail_fp.
"""
import math

from geo import Mat, Mesh, box, cylinder, ellipsoid, hull, lathe, tube
import fp
import humanoid
from humanoid import Body
from rig import Skeleton, sample

SKIN = Mat(0x8A5A3E)
HAIR = Mat(0x1C1614)
COAT = Mat(0x1F2E4E)
COAT2 = Mat(0x172238)
GOLD = Mat(0xE0B04A, glossy=True)
PLATE = Mat(0xEEF1F4, glossy=True)
PANTS = Mat(0x24262C)
BOOT = Mat(0x15161A, glossy=True)
CYBER = Mat(0xDDE3EA, glossy=True)
CYAN = Mat(0x4FE8FF, emissive=True)
GUN = Mat(0x263248, glossy=True)
GUN_W = Mat(0xE8ECF0, glossy=True)
EYE_W = Mat(0xFFFFFF)
EYE_I = Mat(0x2A1A12)

BODY = Body(height=1.82, shoulders=0.21, hips=0.11, arm=1.0, leg=1.04)
GRIP = None


def skeleton():
    global GRIP
    sk = humanoid.skeleton(BODY)
    GRIP = humanoid.gun_bone(sk, 0.65)
    return sk


def rifle(m, g, hx, hy, hz):
    """the rail rifle along +z from the grip"""
    m.add(box(0.07, 0.11, 0.42, bevel=0.016).move(hx, hy + 0.05, hz + 0.08), GUN, g, 1, 3)
    m.add(box(0.07, 0.11, 0.42).move(hx, hy + 0.05, hz + 0.08), GUN, g, 0, 0)
    m.add(box(0.072, 0.05, 0.26).move(hx, hy + 0.10, hz + 0.02), GUN_W, g, 1, 3)
    m.add(box(0.05, 0.07, 0.18).move(hx, hy + 0.03, hz - 0.20), GUN_W, g, 1, 3)                       # stock
    for dx in (-0.022, 0.022):
        m.add(box(0.016, 0.03, 0.30).move(hx + dx, hy + 0.07, hz + 0.42), GUN_W, g, 1, 3)              # prongs
    m.add(box(0.026, 0.016, 0.26).move(hx, hy + 0.07, hz + 0.40), CYAN, g, 1, 3)                      # the coil
    m.add(box(0.074, 0.012, 0.18).move(hx, hy + 0.14, hz + 0.05), CYAN, g, 3, 3)
    m.add(box(0.04, 0.10, 0.05).move(hx, hy - 0.04, hz + 0.0), GUN, g, 1, 3)                          # grip
    m.add(box(0.03, 0.06, 0.08).move(hx, hy + 0.17, hz + 0.0), GUN, g, 3, 3)                          # sight


def mesh(sk, name="rail"):
    b = BODY
    m = Mesh(name)
    B = sk.index
    k = b.k
    hs = b.head * k
    ny, cy, hy = b.neck_y, b.chest_y, b.hip_y
    humanoid.body(m, sk, b, {"skin": SKIN, "top": COAT, "top2": COAT, "sleeve": COAT, "cuff": COAT2, "hand": COAT2,
                             "pants": PANTS, "shin": PANTS, "boot": BOOT, "belt": GOLD},
                  chest=(1.0, 1.0), waist=0.95, hips=1.05, arms=0.95, legs_k=0.95)
    H, C, S, HP = B["head"], B["chest"], B["spine"], B["hips"]
    # -- the head: short hair, shaved on the right with the implant line
    m.add(ellipsoid(0.092 * hs, 0.075 * hs, 0.1 * hs, segs=8, rings=4).move(-0.006, ny + 0.17 * hs, -0.012), HAIR, H, 3, 3)
    m.add(ellipsoid(0.092 * hs, 0.075 * hs, 0.1 * hs, segs=6, rings=3).move(-0.006, ny + 0.17 * hs, -0.012), HAIR, H, 0, 2)
    m.add(box(0.006, 0.006, 0.07 * hs).move(0.09 * hs, ny + 0.15 * hs, 0.0), CYAN, H, 3, 3)
    m.add(box(0.006, 0.04 * hs, 0.006).move(0.09 * hs, ny + 0.13 * hs, 0.035 * hs), CYAN, H, 3, 3)
    humanoid.eyes(m, sk, b, EYE_W, EYE_I, x=0.034, y=0.13, z=0.086, r=0.013)
    m.add(ellipsoid(0.014 * hs, 0.02 * hs, 0.02 * hs, segs=5, rings=3).move(0, ny + 0.105 * hs, 0.1 * hs), SKIN, H, 3, 3)
    # -- the coat: high collar, the white plate, gold piping, the long tails
    m.add(lathe([(0.075 * k, ny - 0.02 * k), (0.085 * k, ny + 0.06 * k), (0.09 * k, ny + 0.09 * k)], segs=8,
                close_top=False), COAT2, C, 3, 3)
    m.add(hull([(-0.12 * k, cy + 0.20 * k, 0.09 * k), (0.12 * k, cy + 0.20 * k, 0.09 * k), (-0.11 * k, cy - 0.02 * k, 0.11 * k),
                (0.11 * k, cy - 0.02 * k, 0.11 * k), (-0.10 * k, cy + 0.20 * k, 0.125 * k), (0.10 * k, cy + 0.20 * k, 0.125 * k),
                (-0.09 * k, cy + 0.0, 0.135 * k), (0.09 * k, cy + 0.0, 0.135 * k)]), PLATE, C, 1, 3)
    m.add(box(0.012, 0.26 * k, 0.012).move(-0.11 * k, cy + 0.08 * k, 0.12 * k), GOLD, C, 3, 3)
    m.add(box(0.012, 0.26 * k, 0.012).move(0.11 * k, cy + 0.08 * k, 0.12 * k), GOLD, C, 3, 3)
    for sx in (-1, 1):                                     # the coat tails, behind and to the sides
        m.add(hull([(sx * 0.02 * k, hy + 0.04 * k, -0.08 * k), (sx * 0.13 * k, hy + 0.04 * k, -0.05 * k),
                    (sx * 0.02 * k, hy - 0.36 * k, -0.11 * k), (sx * 0.15 * k, hy - 0.34 * k, -0.08 * k),
                    (sx * 0.13 * k, hy + 0.04 * k, 0.02 * k), (sx * 0.16 * k, hy - 0.30 * k, 0.0)]), COAT, HP, 1, 3)
        m.add(box(0.13 * k, 0.012, 0.03).move(sx * 0.085 * k, hy - 0.35 * k, -0.095 * k), GOLD, HP, 3, 3)
    for sx in (-1, 1):                                     # epaulettes
        sh = sk.bones[B["upperarm.L" if sx < 0 else "upperarm.R"]][2]
        m.add(box(0.11 * k, 0.025 * k, 0.11 * k, bevel=0.01).move(sh[0], sh[1] + 0.06 * k, 0), GOLD if sx < 0 else CYBER,
              B["upperarm.L" if sx < 0 else "upperarm.R"], 3, 3)
    # -- the cybernetic right arm: white plates, cyan lines
    ua, fa, hd = B["upperarm.R"], B["forearm.R"], B["hand.R"]
    sh, el = sk.bones[ua][2], sk.bones[ua][3]
    wr = sk.bones[fa][3]
    m.add(tube(sh, el, 0.064 * k, 0.054 * k, segs=6, caps=True), CYBER, ua, 3, 3)
    m.add(tube(el, wr, 0.054 * k, 0.042 * k, segs=6, caps=True), CYBER, fa, 3, 3)
    m.add(tube((el[0] + 0.05 * k, el[1], el[2]), (wr[0] + 0.04 * k, wr[1], wr[2]), 0.008, 0.008, segs=3), CYAN, fa, 3, 3)
    m.add(tube((sh[0] + 0.06 * k, sh[1] - 0.04, sh[2]), (el[0] + 0.052 * k, el[1], el[2]), 0.008, 0.008, segs=3), CYAN, ua, 3, 3)
    # -- the rifle
    g = B["gun"]
    hx, hy2, hz = sk.bones[g][2]
    rifle(m, g, hx, hy2, hz)
    return m.weld()


def _sm(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def clips(sk):
    b = BODY
    out = humanoid.clips(sk, b, "rifle", fire_rate=1 / 14, recoil=0.012, run_period=0.6, run_stride=1.0, grip=GRIP)

    def hp(base=None, **kw):
        return humanoid.hold_pose(sk, b, "rifle", base, grip=GRIP, **kw)

    def rail(t):
        u = t / 0.5
        k = math.sin(math.pi * min(1.0, u * 3)) * (1 - _sm(u))
        p = hp({}, recoil=0.09 * k, lift=0.04 * k)
        p["chest"] = (p["chest"][0] - 10 * k, p["chest"][1], p["chest"][2])
        p["head"] = (-6 * k, 0, 0)
        return p
    out.append(sample(rail, 0.5, 8, loop=False, name="rail"))

    def slide(t):
        # down on one hip, the right leg forward, the rifle up
        p = {"root": (-8, 0, 0, 0, -0.42 * b.k, 0)}
        p["thigh.R"], p["shin.R"], p["foot.R"] = (-80, 0, 0), (10, 0, 0), (30, 0, 0)
        p["thigh.L"], p["shin.L"], p["foot.L"] = (-40, 0, -10), (100, 0, 0), (-20, 0, 0)
        p["spine"] = (-10, 0, 0)
        p = hp(p, lift=0.05)
        return p
    out.append(sample(slide, 1.0, 2, name="slide"))

    def disruptor(t):
        u = t / 0.5
        k = math.sin(math.pi * min(1.0, u * 2.5)) * (1 - _sm(u))
        p = hp({}, recoil=0.05 * k, lift=0.08 * k)
        return p
    out.append(sample(disruptor, 0.5, 8, loop=False, name="disruptor"))

    def overclock(t):
        u = t / 0.8
        k = math.sin(math.pi * min(1.0, u * 1.25))
        p = hp({}, lift=0.25 * k)
        p["chest"] = (-15 * k, 0, 0)
        p["head"] = (-15 * k, 0, 0)
        return p
    out.append(sample(overclock, 0.8, 9, loop=False, name="overclock"))
    return out


# ------------------------------------------------------------------ first person

def fp_skeleton():
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.1, 0))
    sk.bone("gun", "root", (0.17, -0.21, 0.28), (0.17, -0.21, 0.92))
    sk.bone("hand.L", "root", (0.14, -0.26, 0.52), (0.16, -0.22, 0.56))
    return sk


def fp_mesh(sk, name="rail_fp"):
    """the rail rifle close up: a long angular navy body with white armour
    on the sides, two prongs at the front and the cyan coil between them, a
    sight; the cyber hand on the grip, a gloved hand on the front"""
    m = Mesh(name)
    g, L = sk.index["gun"], sk.index["hand.L"]
    x, y, z = 0.17, -0.21, 0.28
    m.add(hull([(x - 0.034, y - 0.03, z - 0.08), (x + 0.034, y - 0.03, z - 0.08), (x - 0.034, y + 0.08, z - 0.05),
                (x + 0.034, y + 0.08, z - 0.05), (x - 0.03, y - 0.0, z + 0.34), (x + 0.03, y - 0.0, z + 0.34),
                (x - 0.028, y + 0.06, z + 0.32), (x + 0.028, y + 0.06, z + 0.32)]), GUN, g)
    for sx in (-1, 1):
        m.add(hull([(x + sx * 0.035, y + 0.0, z - 0.04), (x + sx * 0.035, y + 0.07, z - 0.02),
                    (x + sx * 0.031, y + 0.01, z + 0.22), (x + sx * 0.03, y + 0.055, z + 0.2),
                    (x + sx * 0.028, y + 0.035, z + 0.28)]), GUN_W, g)
        m.add(box(0.004, 0.006, 0.16).move(x + sx * 0.037, y + 0.045, z + 0.08), CYAN, g)
    # the prongs and the coil glowing between them
    for sx in (-1, 1):
        m.add(hull([(x + sx * 0.012, y + 0.01, z + 0.3), (x + sx * 0.03, y + 0.01, z + 0.3), (x + sx * 0.012, y + 0.06, z + 0.3),
                    (x + sx * 0.03, y + 0.06, z + 0.3), (x + sx * 0.02, y + 0.03, z + 0.66), (x + sx * 0.024, y + 0.045, z + 0.64)]),
              GUN_W, g)
    m.add(box(0.016, 0.018, 0.3).move(x, y + 0.035, z + 0.46), CYAN, g)
    for i in range(4):
        m.add(box(0.03, 0.03, 0.01).move(x, y + 0.035, z + 0.36 + i * 0.07), GUN, g)
    # the sight: a long scope with a cyan lens
    m.add(cylinder(0.018, 0.12, segs=8, axis="z").move(x, y + 0.11, z + 0.02), GUN, g)
    m.add(cylinder(0.014, 0.006, segs=8, axis="z").move(x, y + 0.11, z + 0.141), CYAN, g)
    m.add(box(0.02, 0.03, 0.03).move(x, y + 0.09, z + 0.08), GUN, g)
    m.add(box(0.036, 0.09, 0.045).turn(rx=-14).move(x, y - 0.07, z - 0.02), GUN, g)        # grip
    m.add(box(0.05, 0.07, 0.12).move(x, y + 0.02, z - 0.16), GUN_W, g)                       # stock
    # the cyber hand on the grip, the coat sleeve with its gold cuff
    fp.forearm(m, g, (x + 0.01, y - 0.09, z - 0.04), (x + 0.13, y - 0.27, z - 0.38), CYBER, r=0.046, cuff=COAT2)
    m.add(tube((x + 0.06, y - 0.15, z - 0.17), (x + 0.03, y - 0.11, z - 0.08), 0.006, 0.006, segs=3), CYAN, g)
    fp.fist(m, g, (x + 0.01, y - 0.09, z - 0.04), (x, y - 0.03, z + 0.01), CYBER, r=0.042, thumb=-1)
    fp.forearm(m, L, (x - 0.03, y - 0.05, z + 0.24), (-0.18, -0.44, 0.02), COAT, r=0.05, cuff=GOLD)
    fp.fist(m, L, (x - 0.03, y - 0.05, z + 0.24), (x + 0.015, y - 0.0, z + 0.27), COAT2, r=0.042, thumb=1)
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
        u = (t % (1 / 14)) / (1 / 14)
        k = max(0.0, 1 - u * 2)
        return {"gun": (-1.5 * k, 0, 0, 0, 0.004 * k, -0.02 * k)}
    out.append(sample(fire, 1 / 14, 4, name="fire", mode=0))

    def rail(t):
        u = t / 0.5
        k = math.sin(math.pi * min(1.0, u * 3)) * (1 - _sm(u))
        return {"gun": (-12 * k, 0, 2 * k, 0, 0.03 * k, -0.1 * k), "hand.L": (0, 0, 0, 0, 0.02 * k, -0.07 * k)}
    out.append(sample(rail, 0.5, 8, loop=False, name="rail"))

    def reload(t):
        u = t / 1.4
        dip = math.sin(math.pi * u)
        return {"root": (20 * dip, 0, -20 * dip, 0, -0.07 * dip, 0),
                "hand.L": (0, 0, 0, 0.0, -0.16 * dip, -0.15 * dip)}
    out.append(sample(reload, 1.4, 10, loop=False, name="reload"))

    def slide(t):
        return {"root": (0, 0, -8, 0.02, -0.03, 0)}
    out.append(sample(slide, 1.0, 2, name="slide"))

    def disruptor(t):
        u = t / 0.5
        k = math.sin(math.pi * min(1.0, u * 2.5)) * (1 - _sm(u))
        return {"gun": (-8 * k, 0, 0, 0, 0.03 * k, -0.06 * k)}
    out.append(sample(disruptor, 0.5, 8, loop=False, name="disruptor"))

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
