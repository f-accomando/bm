"""
Sarge - damage (the kit of Soldier: 76): a veteran with a pulse rifle.

An old soldier, broad and tall: short grey hair, an amber visor across the
eyes, an olive field jacket with orange chevrons on the shoulder, a dark
chest rig, grey trousers with knee pads, black boots; a long pulse rifle,
dark with white panels and cyan glowing strips, rockets under the barrel.

Models: sarge (third person), sarge_fp (hands and rifle seen from the eye).
"""
import math

from geo import Mat, Mesh, box, cylinder, ellipsoid, hull, lathe, wedge
import fp
import humanoid
from humanoid import Body
import rig
from rig import Skeleton, sample

SKIN = Mat(0xD9A587)
STUBBLE = Mat(0xB08A74)
HAIR = Mat(0xB4B4B8)
VISOR = Mat(0xFFA22E, emissive=True)
JACKET = Mat(0x4E5639)
JACKET2 = Mat(0x3E452D)
RIG = Mat(0x2B2E33)
CHEVRON = Mat(0xF07A22)
PANTS = Mat(0x4A4D52)
PAD = Mat(0x2E3034)
BOOT = Mat(0x1E1F22)
GLOVE = Mat(0x6B5B4A)
GUN = Mat(0x2C3036, glossy=True)
GUN_W = Mat(0xDDE2E6, glossy=True)
GLOW = Mat(0x4FD8FF, emissive=True)
BELT = Mat(0x5A4A36)

BODY = Body(height=1.86, shoulders=0.23, hips=0.11, arm=1.04, leg=1.0)
GRIP = None          # set by skeleton(): the foregrip in the gun bone's rest space


def limb(r0, r1, length, segs=6, bulge=1.0):
    rm = (r0 + r1) / 2 * bulge
    return lathe([(r1, -length), (rm, -length * 0.5), (r0, 0)], segs=segs, close_top=False, close_bottom=False)


def skeleton():
    global GRIP
    b = BODY
    sk = humanoid.skeleton(b)
    # the rifle: its grip at the right hand, pointing forward
    hand = sk.bones[sk["hand.R"]][2]
    sk.bone("gun", "hand.R", hand, (hand[0], hand[1], hand[2] + 0.6))
    GRIP = (hand[0], hand[1] + 0.02, hand[2] + 0.36)
    return sk


def mesh(sk, name="sarge"):
    b = BODY
    m = Mesh(name)
    B = sk.index
    H, C, S, HP = B["head"], B["chest"], B["spine"], B["hips"]
    k = b.k
    ny, cy, hy = b.neck_y, b.chest_y, b.hip_y
    # -- head: a square jaw, short grey hair, the visor
    m.add(ellipsoid(0.092 * k, 0.112 * k, 0.102 * k, segs=8, rings=6).move(0, ny + 0.13 * k, 0.01), SKIN, H, 1, 3)
    m.add(ellipsoid(0.092 * k, 0.112 * k, 0.102 * k, segs=6, rings=4).move(0, ny + 0.13 * k, 0.01), SKIN, H, 0, 0)
    m.add(hull([(-0.075 * k, ny + 0.03 * k, 0.02), (0.075 * k, ny + 0.03 * k, 0.02), (-0.06 * k, ny + 0.0, 0.075 * k),
                (0.06 * k, ny + 0.0, 0.075 * k), (-0.085 * k, ny + 0.11 * k, 0.05 * k), (0.085 * k, ny + 0.11 * k, 0.05 * k),
                (0, ny - 0.01 * k, 0.09 * k)], smooth=True), STUBBLE, H, 1, 3)
    m.add(ellipsoid(0.098 * k, 0.07 * k, 0.106 * k, segs=8, rings=4).move(0, ny + 0.19 * k, -0.005), HAIR, H, 1, 3)
    m.add(hull([(-0.096 * k, ny + 0.155 * k, 0.07 * k), (0.096 * k, ny + 0.155 * k, 0.07 * k),
                (-0.094 * k, ny + 0.12 * k, 0.075 * k), (0.094 * k, ny + 0.12 * k, 0.075 * k),
                (-0.08 * k, ny + 0.155 * k, 0.108 * k), (0.08 * k, ny + 0.155 * k, 0.108 * k),
                (-0.08 * k, ny + 0.12 * k, 0.112 * k), (0.08 * k, ny + 0.12 * k, 0.112 * k)]), VISOR, H, 0, 3)
    # -- torso: a broad field jacket, the chest rig, chevrons on the left shoulder
    m.add(lathe([(0.13 * k, cy - 0.03 * k), (0.19 * k, cy + 0.10 * k), (0.205 * k, cy + 0.20 * k),
                 (0.12 * k, ny + 0.02 * k)], segs=8).scale(1, 1, 0.66), JACKET, C, 0, 3)
    m.add(hull([(-0.16 * k, cy + 0.20 * k, 0.07 * k), (0.16 * k, cy + 0.20 * k, 0.07 * k),
                (-0.14 * k, cy + 0.0, 0.10 * k), (0.14 * k, cy + 0.0, 0.10 * k),
                (-0.16 * k, cy + 0.20 * k, 0.13 * k), (0.16 * k, cy + 0.20 * k, 0.13 * k),
                (-0.14 * k, cy - 0.02 * k, 0.135 * k), (0.14 * k, cy - 0.02 * k, 0.135 * k)]), RIG, C, 1, 3)
    m.add(hull([(-0.10 * k, ny + 0.02 * k, 0.06 * k), (0.10 * k, ny + 0.02 * k, 0.06 * k), (-0.12 * k, ny - 0.04 * k, 0.09 * k),
                (0.12 * k, ny - 0.04 * k, 0.09 * k), (0, ny + 0.03 * k, -0.07 * k), (-0.11 * k, ny + 0.05 * k, -0.02 * k),
                (0.11 * k, ny + 0.05 * k, -0.02 * k)], smooth=True), JACKET2, C, 2, 3)            # collar
    for i in range(2):
        y = cy + (0.14 - i * 0.05) * k
        m.add(box(0.005, 0.02 * k, 0.07 * k).turn(rz=20).move(-0.215 * k, y, 0.0), CHEVRON, C, 2, 3)
    m.add(lathe([(0.125 * k, hy + 0.06 * k), (0.12 * k, cy - 0.05 * k), (0.14 * k, cy + 0.02 * k)], segs=8)
          .scale(1, 1, 0.68), JACKET, S, 0, 3)
    m.add(lathe([(0.13 * k, hy + 0.04 * k), (0.13 * k, hy + 0.09 * k)], segs=8, close_top=False, close_bottom=False)
          .scale(1, 1, 0.70), BELT, S, 1, 3)
    m.add(box(0.06 * k, 0.07 * k, 0.04 * k).move(0.10 * k, hy + 0.06 * k, 0.085 * k), BELT, S, 2, 3)   # pouch
    m.add(lathe([(0.13 * k, hy - 0.10 * k), (0.135 * k, hy), (0.128 * k, hy + 0.06 * k)], segs=8)
          .scale(1, 1, 0.72), PANTS, HP, 0, 3)
    for s, x in (("L", -1), ("R", 1)):
        UA, FA, HD = B[f"upperarm.{s}"], B[f"forearm.{s}"], B[f"hand.{s}"]
        TH, SH, FT = B[f"thigh.{s}"], B[f"shin.{s}"], B[f"foot.{s}"]
        sh = sk.bones[UA][2]
        el = sk.bones[FA][2]
        hd = sk.bones[HD][2]
        m.add(ellipsoid(0.075 * k, 0.07 * k, 0.08 * k, segs=6, rings=4).move(sh[0], sh[1] + 0.01, 0), JACKET, UA, 1, 3)
        m.add(limb(0.068 * k, 0.058 * k, b.upper, bulge=1.1).turn(rz=x * -8).move(sh[0], sh[1], 0), JACKET, UA, 0, 3)
        m.add(limb(0.058 * k, 0.046 * k, b.lower).turn(rz=x * -5).move(el[0], el[1], 0.0), JACKET2, FA, 0, 3)
        m.add(hull([(hd[0] - 0.035, hd[1] + 0.02, hd[2] - 0.02), (hd[0] + 0.035, hd[1] + 0.02, hd[2] - 0.02),
                    (hd[0] - 0.03, hd[1] + 0.02, hd[2] + 0.05), (hd[0] + 0.03, hd[1] + 0.02, hd[2] + 0.05),
                    (hd[0], hd[1] - 0.09 * k, hd[2] + 0.02), (hd[0] - 0.03, hd[1] - 0.06 * k, hd[2]),
                    (hd[0] + 0.03, hd[1] - 0.06 * k, hd[2])], smooth=True), GLOVE, HD, 0, 3)
        th = sk.bones[TH][2]
        kn = sk.bones[SH][2]
        an = sk.bones[FT][2]
        m.add(limb(0.095 * k, 0.068 * k, th[1] - kn[1], bulge=1.05).move(th[0], th[1], 0), PANTS, TH, 0, 3)
        m.add(limb(0.07 * k, 0.058 * k, kn[1] - an[1] - 0.06 * k).move(kn[0], kn[1], 0.01), PANTS, SH, 0, 3)
        m.add(box(0.10 * k, 0.11 * k, 0.05 * k, bevel=0.012).move(kn[0], kn[1], 0.07 * k), PAD, SH, 1, 3)
        # boots
        m.add(hull([(an[0] - 0.06 * k, 0.0, -0.07 * k), (an[0] + 0.06 * k, 0.0, -0.07 * k),
                    (an[0] - 0.06 * k, 0.0, 0.19 * k), (an[0] + 0.06 * k, 0.0, 0.19 * k),
                    (an[0] - 0.06 * k, 0.16 * k, -0.06 * k), (an[0] + 0.06 * k, 0.16 * k, -0.06 * k),
                    (an[0] - 0.05 * k, 0.09 * k, 0.15 * k), (an[0] + 0.05 * k, 0.09 * k, 0.15 * k)]), BOOT, FT, 0, 3)
    # -- the pulse rifle along the gun bone (+z from the grip)
    g = B["gun"]
    hx, hy2, hz = sk.bones[g][2]
    m.add(box(0.06, 0.10, 0.44, bevel=0.012).move(hx, hy2 + 0.04, hz + 0.16), GUN, g, 1, 3)        # body
    m.add(box(0.06, 0.10, 0.44).move(hx, hy2 + 0.04, hz + 0.16), GUN, g, 0, 0)
    m.add(box(0.064, 0.05, 0.30).move(hx, hy2 + 0.10, hz + 0.20), GUN_W, g, 1, 3)                    # top panel
    m.add(box(0.05, 0.07, 0.20).move(hx, hy2 + 0.035, hz - 0.17), GUN, g, 1, 3)                     # stock
    m.add(cylinder(0.022, 0.24, segs=6, axis="z").move(hx, hy2 + 0.05, hz + 0.38), GUN, g, 1, 3)    # barrel
    m.add(cylinder(0.03, 0.12, segs=6, axis="z").move(hx, hy2 - 0.005, hz + 0.30), GUN_W, g, 2, 3)  # rocket tube
    m.add(box(0.04, 0.10, 0.05).move(hx, hy2 - 0.05, hz + 0.05), GUN, g, 1, 3)                      # magazine
    m.add(box(0.066, 0.012, 0.22).move(hx, hy2 + 0.125, hz + 0.20), GLOW, g, 2, 3)                  # glow strip
    m.add(box(0.012, 0.03, 0.14).move(hx + 0.031, hy2 + 0.04, hz + 0.14), GLOW, g, 3, 3)
    return m.weld()


def clips(sk):
    out = humanoid.clips(sk, BODY, "rifle", fire_rate=1 / 9, recoil=0.03, grip=GRIP)

    def sprint(t):
        ph = t / 0.5
        p = humanoid.legs(BODY, ph, 1.35, 0.34 * BODY.k, -0.05 * BODY.k + 0.04 * BODY.k * math.cos(4 * math.pi * ph))
        sw = math.sin(2 * math.pi * ph)
        p["spine"] = (16, 6 * sw, 0)
        p["hips"] = (0, -10 * sw, 0)
        # the rifle held across the chest, the free arm pumping
        p = humanoid.hold_pose(sk, BODY, "rifle", p, lift=0.06, grip=GRIP)
        rig.orient(sk, p, "gun", rig.mat_mul(sk.matrices(p)[sk["chest"]][0], rig.rot_euler(-30, -40, 0)))
        return p
    out.append(sample(sprint, 0.5, 10, name="sprint"))

    def rockets(t):
        u = (t % 0.12) / 0.12
        k = max(0.0, 1 - u * 2)
        return humanoid.hold_pose(sk, BODY, "rifle", {}, recoil=0.07 * k, grip=GRIP)
    out.append(sample(rockets, 0.36, 9, name="rockets", mode=0))

    def field(t):
        # crouches and plants the medkit field
        u = t / 0.6
        k = math.sin(math.pi * min(1, u * 1.4))
        p = humanoid.legs(BODY, 0.0, 0.0, 0.0, -0.15 * BODY.k * k)
        p.update(humanoid.REST_ARMS)
        p["upperarm.L"] = (-60 * k, 0, -10)
        p["forearm.L"] = (-20 * k, 0, 0)
        p["chest"] = (20 * k, 0, 0)
        return p
    out.append(sample(field, 0.6, 8, loop=False, name="field"))
    return out


def fp_skeleton():
    """the rifle and the hands seen from the eye (camera at the origin, +z)"""
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.1, 0))
    sk.bone("gun", "root", (0.16, -0.20, 0.22), (0.16, -0.20, 0.80))
    sk.bone("hand.L", "root", (0.13, -0.28, 0.52), (0.15, -0.24, 0.56))
    return sk


def fp_mesh(sk, name="sarge_fp"):
    """the pulse rifle close up: a tapered dark receiver with white side
    panels and cyan light lines, a holo sight, a shrouded barrel, the helix
    launcher under it; gloves and the jacket's sleeves"""
    m = Mesh(name)
    g, L = sk.index["gun"], sk.index["hand.L"]
    x, y, z = 0.16, -0.20, 0.22
    # receiver: taller at the back, narrowing to the front
    m.add(hull([(x - 0.036, y - 0.03, z - 0.06), (x + 0.036, y - 0.03, z - 0.06), (x - 0.036, y + 0.075, z - 0.06),
                (x + 0.036, y + 0.075, z - 0.06), (x - 0.032, y - 0.01, z + 0.36), (x + 0.032, y - 0.01, z + 0.36),
                (x - 0.03, y + 0.06, z + 0.34), (x + 0.03, y + 0.06, z + 0.34)]), GUN, g)
    # white side panels, the cyan lines on them
    for sx in (-1, 1):
        m.add(hull([(x + sx * 0.037, y + 0.005, z - 0.02), (x + sx * 0.037, y + 0.06, z - 0.02),
                    (x + sx * 0.034, y + 0.012, z + 0.26), (x + sx * 0.033, y + 0.05, z + 0.24),
                    (x + sx * 0.03, y + 0.03, z + 0.1)]), GUN_W, g)
        m.add(box(0.004, 0.008, 0.2).move(x + sx * 0.039, y + 0.03, z + 0.1), GLOW, g)
    m.add(box(0.05, 0.012, 0.3).move(x, y + 0.082, z + 0.12), GUN, g)                      # top rail
    # the holo sight: a frame and its amber dot
    m.add(box(0.044, 0.008, 0.05).move(x, y + 0.095, z + 0.06), GUN, g)
    for sx in (-1, 1):
        m.add(box(0.006, 0.05, 0.012).move(x + sx * 0.02, y + 0.12, z + 0.08), GUN, g)
    m.add(box(0.046, 0.006, 0.012).move(x, y + 0.145, z + 0.08), GUN, g)
    m.add(box(0.008, 0.008, 0.004).move(x, y + 0.115, z + 0.08), VISOR, g)
    # barrel: a six-sided shroud, the muzzle
    m.add(cylinder(0.026, 0.22, segs=6, axis="z").move(x, y + 0.03, z + 0.34), GUN, g)
    m.add(cylinder(0.031, 0.05, segs=6, axis="z").move(x, y + 0.03, z + 0.55), GUN_W, g)
    m.add(cylinder(0.014, 0.012, segs=6, axis="z").move(x, y + 0.03, z + 0.6), GLOW, g)
    # the helix launcher under the barrel: three dark mouths
    m.add(cylinder(0.034, 0.18, segs=8, axis="z").move(x, y - 0.03, z + 0.3), GUN_W, g)
    for i in range(3):
        a = 2 * math.pi * i / 3 + 0.5
        m.add(cylinder(0.01, 0.008, segs=5, axis="z").move(x + math.cos(a) * 0.016, y - 0.03 + math.sin(a) * 0.016,
                                                            z + 0.48), GUN, g)
    m.add(box(0.04, 0.1, 0.055).turn(rx=12).move(x, y - 0.08, z + 0.1), GUN, g)            # magazine
    m.add(box(0.036, 0.09, 0.045).turn(rx=-12).move(x, y - 0.075, z - 0.02), GUN, g)       # grip
    # the right glove on the grip, the sleeve
    fp.forearm(m, g, (x + 0.01, y - 0.09, z - 0.04), (x + 0.12, y - 0.27, z - 0.38), JACKET2, r=0.05, cuff=JACKET)
    fp.fist(m, g, (x + 0.01, y - 0.09, z - 0.04), (x, y - 0.03, z + 0.01), GLOVE, r=0.043, thumb=-1)
    # the left glove under the launcher
    fp.forearm(m, L, (x - 0.03, y - 0.08, z + 0.3), (-0.2, -0.42, 0.06), JACKET2, r=0.05, cuff=JACKET)
    fp.fist(m, L, (x - 0.03, y - 0.08, z + 0.3), (x + 0.02, y - 0.035, z + 0.33), GLOVE, r=0.043, thumb=1)
    return m.weld()


def fp_clips(sk):
    out = []

    def idle(t):
        k = math.sin(2 * math.pi * t / 2.0)
        return {"root": (0.7 * k, 0, 0, 0, 0.004 * k, 0)}
    out.append(sample(idle, 2.0, 8, name="idle"))

    def run(t):
        ph = t / 0.62
        return {"root": (0, 2 * math.sin(2 * math.pi * ph), 0, 0.014 * math.sin(2 * math.pi * ph),
                         0.012 * math.cos(4 * math.pi * ph), 0)}
    out.append(sample(run, 0.62, 8, name="run"))

    def fire(t):
        u = (t % (1 / 9)) / (1 / 9)
        k = max(0.0, 1 - u * 2)
        return {"gun": (-2.5 * k, 0, 0, 0, 0.006 * k, -0.03 * k), "hand.L": (0, 0, 0, 0, 0.004 * k, -0.02 * k)}
    out.append(sample(fire, 1 / 9, 4, name="fire", mode=0))

    def reload(t):
        u = t / 1.5
        dip = math.sin(math.pi * u)
        return {"root": (25 * dip, 0, -15 * dip, 0, -0.08 * dip, 0),
                "hand.L": (0, 0, 0, -0.05 * dip, -0.15 * dip, -0.2 * dip)}
    out.append(sample(reload, 1.5, 10, loop=False, name="reload"))

    def sprint(t):
        ph = t / 0.5
        return {"root": (35, 40, -10, 0.05 * math.sin(2 * math.pi * ph), -0.12 + 0.02 * math.cos(4 * math.pi * ph), 0)}
    out.append(sample(sprint, 0.5, 8, name="sprint"))

    def rockets(t):
        u = (t % 0.12) / 0.12
        k = max(0.0, 1 - u * 2)
        return {"gun": (-6 * k, 0, 0, 0, 0.015 * k, -0.05 * k)}
    out.append(sample(rockets, 0.36, 9, name="rockets", mode=0))

    def raise_(t):
        u = 1 - _sm(t / 0.35)
        return {"root": (30 * u, 0, 0, 0, -0.3 * u, 0)}
    out.append(sample(raise_, 0.35, 5, loop=False, name="raise"))
    return out


def _sm(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def build():
    sk = skeleton()
    out = [(mesh(sk), sk, clips(sk))]
    fsk = fp_skeleton()
    out.append((fp_mesh(fsk), fsk, fp_clips(fsk)))
    return out
