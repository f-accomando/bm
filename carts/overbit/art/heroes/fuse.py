"""
Fuse - damage (the kit of Junkrat): a scrapyard demolitions punk.

Lanky and wired: a tall orange mohawk, welding goggles on the forehead, a
sleeveless work vest over a red and white striped shirt, a bandolier of
little bombs across the chest, patched cargo trousers in heavy work boots,
thick leather welding gauntlets, a scrap backpack with a gas canister. In
his hands the Frag Launcher: an olive tube with a big drum and yellow
hazard stripes. His ultimate is the Boom Wheel, a spiked rolling bomb.

Models: fuse (third person: the Meshy figure art/meshy/fuse, on this
skeleton by meshyrig.py; the body below with --classic), fuse_fp,
fuse_wheel (Boom Wheel), fuse_mine (Concussion Mine), fuse_trap (Steel
Trap: jaws "open" and "shut").
"""
import math

from geo import Mat, Mesh, Part, box, cross, cylinder, dot, ellipsoid, hull, lathe, sub, tube
import fp
import humanoid
from humanoid import Body
from rig import Skeleton, sample

SKIN = Mat(0xD9A27C)
HAIR = Mat(0xF28A1E)
SOOT = Mat(0x4A3426)
GOGGLE = Mat(0x5A4A3A)
LENS = Mat(0xFFC94A, emissive=True)
TOP = Mat(0xD8B04A)
SHORTS = Mat(0x6B5A3E)
BELT = Mat(0x4A3424)
STEEL = Mat(0x9AA2A8, glossy=True)
DARK = Mat(0x3A3D40, glossy=True)
OLIVE = Mat(0x5E6B3A, glossy=True)
HAZARD = Mat(0xF2C230)
BOMB = Mat(0x2E2E30, glossy=True)
RED = Mat(0xFF3A2A, emissive=True)
BOOT = Mat(0x3C2E24)
TEETH = Mat(0xF4F0E0)
RUST = Mat(0x8A4A2A)

BODY = Body(height=1.85, shoulders=0.20, hips=0.10, arm=1.08, leg=1.05, head=1.0)
GRIP = None


def skeleton():
    global GRIP
    sk = humanoid.skeleton(BODY)
    GRIP = humanoid.gun_bone(sk, 0.55)
    return sk


def launcher(m, g, hx, hy, hz, lod_lo=1):
    """the Frag Launcher along +z from the grip"""
    m.add(cylinder(0.045, 0.52, segs=8, axis="z").move(hx, hy + 0.06, hz - 0.12), OLIVE, g, lod_lo, 3)
    m.add(cylinder(0.045, 0.52, segs=5, axis="z").move(hx, hy + 0.06, hz - 0.12), OLIVE, g, 0, lod_lo - 1)
    m.add(lathe([(0.05, 0.0), (0.065, 0.06), (0.06, 0.09)], segs=8).turn(rx=90).move(hx, hy + 0.06, hz + 0.40), DARK, g, 1, 3)
    m.add(cylinder(0.085, 0.07, segs=8, axis="x").move(hx - 0.035, hy - 0.02, hz + 0.12), BOMB, g, 1, 3)     # the drum
    m.add(cylinder(0.05, 0.075, segs=6, axis="x").move(hx - 0.037, hy - 0.02, hz + 0.12), HAZARD, g, 3, 3)
    for z in (0.0, 0.26):
        m.add(cylinder(0.048, 0.03, segs=8, axis="z").move(hx, hy + 0.06, hz + z), HAZARD, g, 3, 3)
    m.add(box(0.04, 0.10, 0.05).move(hx, hy - 0.02, hz - 0.01), DARK, g, 1, 3)                                # grip
    m.add(box(0.03, 0.05, 0.16).move(hx, hy + 0.12, hz + 0.05), STEEL, g, 3, 3)                               # sight rail


def mesh(sk, name="fuse"):
    b = BODY
    m = Mesh(name)
    B = sk.index
    k = b.k
    ny, cy, hy = b.neck_y, b.chest_y, b.hip_y
    humanoid.body(m, sk, b, {"skin": SKIN, "top": TOP, "top2": TOP, "sleeve": SKIN, "cuff": SKIN, "hand": SKIN,
                             "pants": SHORTS, "shin": SKIN, "boot": BOOT, "belt": BELT},
                  chest=(0.92, 0.95), waist=0.9, hips=1.0, arms=0.85, legs_k=0.85)
    H, C, S, HP = B["head"], B["chest"], B["spine"], B["hips"]
    hs = b.head * k
    # -- the head: spikes of hair, goggles up, a wide grin
    m.add(ellipsoid(0.092 * hs, 0.06 * hs, 0.1 * hs, segs=8, rings=4).move(0, ny + 0.185 * hs, -0.01), HAIR, H, 1, 3)
    # spikes out of the crown, the tips singed
    cxy = (0, ny + 0.13 * hs, -0.01)
    for i, (x, y, z) in enumerate([(0.0, 0.25, 0.06), (-0.06, 0.23, 0.03), (0.06, 0.23, 0.03), (-0.05, 0.24, -0.06),
                                   (0.05, 0.24, -0.06), (0.0, 0.25, -0.09), (-0.08, 0.19, -0.04), (0.08, 0.19, -0.04)]):
        base = (x * hs, ny + y * hs, z * hs)
        d = (base[0] - cxy[0], base[1] - cxy[1], base[2] - cxy[2])
        l = math.sqrt(d[0] ** 2 + d[1] ** 2 + d[2] ** 2)
        tip = (base[0] + d[0] / l * 0.12 * hs, base[1] + d[1] / l * 0.12 * hs + 0.03, base[2] + d[2] / l * 0.12 * hs)
        lo = 3 if i >= 6 else 1
        m.add(tube(base, tip, 0.032 * hs, 0.004, segs=4), HAIR, H, lo, 3)
    m.add(lathe([(0.097 * hs, ny + 0.18 * hs), (0.1 * hs, ny + 0.205 * hs)], segs=8, close_top=False, close_bottom=False),
          GOGGLE, H, 3, 3)
    for sx in (-1, 1):
        m.add(cylinder(0.028 * hs, 0.02, segs=6, axis="z").move(sx * 0.04 * hs, ny + 0.205 * hs, 0.085 * hs), LENS, H, 1, 3)
    m.add(box(0.09 * hs, 0.022 * hs, 0.02).move(0, ny + 0.072 * hs, 0.088 * hs), TEETH, H, 1, 3)               # the grin
    humanoid.eyes(m, sk, b, Mat(0xFFFFFF), Mat(0x2A7A30), x=0.034, y=0.13, z=0.084, r=0.013)
    m.add(ellipsoid(0.014 * hs, 0.02 * hs, 0.02 * hs, segs=5, rings=3).move(0, ny + 0.105 * hs, 0.1 * hs), SKIN, H, 3, 3)
    # -- the torn hem of the top, the bandolier of bombs, the backpack and canister
    for i in range(6):
        a = 2 * math.pi * i / 6
        m.add(tri_out((math.cos(a) * 0.122 * k, hy + 0.06 * k, math.sin(a) * 0.086 * k),
                      (math.cos(a + 0.6) * 0.122 * k, hy + 0.06 * k, math.sin(a + 0.6) * 0.086 * k),
                      (math.cos(a + 0.3) * 0.124 * k, hy + 0.0, math.sin(a + 0.3) * 0.088 * k), (0, hy + 0.05 * k, 0)),
              TOP, S, 3, 3)
    for i in range(5):
        t = i / 4
        x = (-0.12 + 0.24 * t) * k
        y = cy + (0.18 - 0.25 * t) * k
        m.add(ellipsoid(0.03 * k, 0.03 * k, 0.03 * k, segs=5, rings=3).move(x, y, 0.13 * k), BOMB, C, 3 if i % 2 else 1, 3)
        m.add(box(0.01, 0.01, 0.01).move(x, y + 0.03 * k, 0.15 * k), RED, C, 3, 3)
    m.add(hull([(-0.15 * k, cy + 0.22 * k, 0.14 * k), (-0.1 * k, cy + 0.24 * k, 0.12 * k), (0.15 * k, cy - 0.06 * k, 0.14 * k),
                (0.12 * k, cy - 0.08 * k, 0.12 * k)]), BELT, C, 3, 3)
    m.add(box(0.26 * k, 0.32 * k, 0.12 * k, bevel=0.02).move(0, cy + 0.04 * k, -0.17 * k), RUST, C, 0, 3)
    m.add(cylinder(0.06 * k, 0.30 * k, segs=6).move(0.08 * k, cy - 0.06 * k, -0.25 * k), HAZARD, C, 1, 3)
    m.add(cylinder(0.02 * k, 0.08 * k, segs=5).move(0.08 * k, cy + 0.24 * k, -0.25 * k), STEEL, C, 3, 3)
    # -- the scrap arm (left): steel over the skin, a ring at the elbow, a claw
    ua, fa, hd = B["upperarm.L"], B["forearm.L"], B["hand.L"]
    el = sk.bones[fa][2]
    wr = sk.bones[fa][3]
    m.add(tube(el, wr, 0.05 * k, 0.045 * k, segs=6, caps=True), STEEL, fa, 1, 3)
    m.add(cylinder(0.058 * k, 0.04 * k, segs=6).move(el[0], el[1] - 0.02, el[2]), DARK, fa, 3, 3)
    hdp = sk.bones[hd][2]
    for dx in (-0.025, 0.025):
        m.add(box(0.015, 0.08 * k, 0.03).move(hdp[0] + dx, hdp[1] - 0.05 * k, hdp[2] + 0.02), STEEL, hd, 3, 3)
    # -- the peg leg (left): a pipe and a spring below the knee, a rubber foot
    sh = B["shin.L"]
    kn = sk.bones[sh][2]
    an = sk.bones[sh][3]
    m.add(tube(kn, (an[0], an[1] + 0.05, an[2]), 0.03 * k, 0.03 * k, segs=6), STEEL, sh, 0, 3)
    m.add(cylinder(0.045 * k, 0.06 * k, segs=6).move(kn[0], kn[1] - 0.07 * k, kn[2]), DARK, sh, 1, 3)
    for i in range(3):
        m.add(cylinder(0.04 * k, 0.012, segs=6).move(an[0], an[1] + 0.1 * k + i * 0.035 * k, an[2]), HAZARD, sh, 3, 3)
    m.add(cylinder(0.05 * k, 0.04, segs=6).move(an[0], 0.0, an[2]), BOMB, B["foot.L"], 0, 3)
    # -- the launcher
    g = B["gun"]
    hx, hy2, hz = sk.bones[g][2]
    launcher(m, g, hx, hy2, hz)
    return m.weld()


def tri_out(p0, p1, p2, centre):
    """one triangle, turned to face away from `centre`"""
    n = cross(sub(p1, p0), sub(p2, p0))
    mid = tuple((p0[i] + p1[i] + p2[i]) / 3 for i in range(3))
    f = (0, 1, 2) if dot(n, sub(mid, centre)) >= 0 else (0, 2, 1)
    return Part([p0, p1, p2], [f], False)


def _sm(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def clips(sk):
    b = BODY
    out = humanoid.clips(sk, b, "launcher", fire_rate=0.66, recoil=0.09, run_period=0.58, run_stride=0.95, grip=GRIP)

    def hp(base=None, **kw):
        return humanoid.hold_pose(sk, b, "launcher", base, grip=GRIP, **kw)

    def mine(t):
        # the mine thrown underhand with the scrap arm
        u = t / 0.45
        wind = _sm(u / 0.4) * (1 - _sm((u - 0.4) / 0.25))
        rel = _sm((u - 0.4) / 0.25) * (1 - _sm((u - 0.8) / 0.2))
        p = hp({})
        p["upperarm.L"] = (40 * wind - 80 * rel, 0, -15)
        p["forearm.L"] = (-20 - 20 * wind, 0, 0)
        p["chest"] = (p["chest"][0] + 6 * rel, p["chest"][1] - 15 * wind, 0)
        return p
    out.append(sample(mine, 0.45, 9, loop=False, name="mine"))

    def trap(t):
        u = t / 0.6
        k = math.sin(math.pi * min(1.0, u * 1.4))
        p = hp({})
        p["upperarm.L"] = (-50 * k, 0, -20)
        p["forearm.L"] = (-30 * k, 0, 0)
        p["chest"] = (20 * k, 0, 0)
        return p
    out.append(sample(trap, 0.6, 8, loop=False, name="trap"))

    def wheel(t):
        # kneels with the remote while the wheel rolls
        p = humanoid.legs(b, 0, 0, 0, -0.28 * b.k)
        p.update(humanoid.REST_ARMS)
        p["upperarm.R"] = (-60, 0, 10)
        p["forearm.R"] = (-60, 0, 0)
        p["upperarm.L"] = (-50, 0, -10)
        p["forearm.L"] = (-70, 0, 0)
        p["chest"] = (18, 0, 0)
        p["head"] = (8 + 4 * math.sin(t * 8), 0, 0)
        return p
    out.append(sample(wheel, 1.0, 6, name="wheel"))
    return out


# ------------------------------------------------------------------ first person

def fp_skeleton():
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.1, 0))
    sk.bone("gun", "root", (0.2, -0.24, 0.30), (0.2, -0.24, 0.82))
    sk.bone("hand.L", "root", (0.04, -0.30, 0.48), (0.06, -0.26, 0.52))
    return sk


def fp_mesh(sk, name="fuse_fp"):
    m = Mesh(name)
    g, L = sk.index["gun"], sk.index["hand.L"]
    x, y, z = 0.2, -0.24, 0.30
    m.add(cylinder(0.05, 0.52, segs=10, axis="z").move(x, y + 0.06, z - 0.08), OLIVE, g)
    m.add(lathe([(0.055, 0.0), (0.07, 0.05), (0.064, 0.09)], segs=10).turn(rx=90).move(x, y + 0.06, z + 0.43), DARK, g)
    m.add(cylinder(0.035, 0.012, segs=10, axis="z").move(x, y + 0.06, z + 0.515), BOMB, g)
    m.add(cylinder(0.09, 0.075, segs=10, axis="x").move(x - 0.04, y - 0.03, z + 0.16), BOMB, g)
    m.add(cylinder(0.055, 0.08, segs=8, axis="x").move(x - 0.042, y - 0.03, z + 0.16), HAZARD, g)
    for zz in (0.02, 0.3):
        m.add(cylinder(0.053, 0.03, segs=10, axis="z").move(x, y + 0.06, z + zz), HAZARD, g)
    m.add(box(0.03, 0.05, 0.18).move(x, y + 0.125, z + 0.1), STEEL, g)
    m.add(box(0.012, 0.02, 0.012).move(x, y + 0.16, z + 0.16), RED, g)
    # the right hand (skin) on the grip
    fp.forearm(m, g, (x + 0.01, y - 0.06, z + 0.0), (x + 0.12, y - 0.24, z - 0.34), SKIN, r=0.045)
    fp.fist(m, g, (x + 0.01, y - 0.06, z + 0.0), (x, y + 0.01, z + 0.05), SKIN, r=0.042, thumb=-1)
    # the scrap arm under the barrel
    fp.forearm(m, L, (0.07, -0.29, 0.50), (-0.16, -0.46, 0.14), STEEL, r=0.048, cuff=DARK)
    fp.fist(m, L, (0.07, -0.29, 0.50), (0.15, -0.24, 0.53), STEEL, r=0.045, thumb=1)
    return m.weld()


def fp_clips(sk):
    out = []

    def idle(t):
        k = math.sin(2 * math.pi * t / 1.6)
        return {"root": (0.9 * k, 0, 0.6 * k, 0, 0.005 * k, 0)}
    out.append(sample(idle, 1.6, 8, name="idle"))

    def run(t):
        ph = t / 0.58
        return {"root": (0, 2.5 * math.sin(2 * math.pi * ph), 1.5 * math.sin(2 * math.pi * ph), 0.016 * math.sin(2 * math.pi * ph),
                         0.014 * math.cos(4 * math.pi * ph), 0)}
    out.append(sample(run, 0.58, 8, name="run"))

    def fire(t):
        u = t / 0.66
        k = math.sin(math.pi * min(1.0, u * 4)) * (1 - _sm(u * 1.5))
        return {"gun": (-14 * k, 0, 0, 0, 0.03 * k, -0.09 * k), "hand.L": (0, 0, 0, 0, 0.02 * k, -0.06 * k)}
    out.append(sample(fire, 0.66, 9, loop=False, name="fire"))

    def reload(t):
        u = t / 1.5
        dip = math.sin(math.pi * u)
        return {"root": (15 * dip, 0, -30 * dip, 0, -0.08 * dip, 0),
                "hand.L": (20 * dip, 0, 0, -0.02 * dip, -0.2 * dip, -0.15 * dip)}
    out.append(sample(reload, 1.5, 10, loop=False, name="reload"))

    def throw(t):
        u = t / 0.45
        k = math.sin(math.pi * min(1.0, u * 1.3))
        return {"hand.L": (-50 * k, 0, 0, -0.05 * k, 0.25 * k, 0.15 * k), "gun": (8 * k, 0, 0, 0.03 * k, -0.05 * k, 0)}
    out.append(sample(throw, 0.45, 8, loop=False, name="throw"))

    def raise_(t):
        u = 1 - _sm(t / 0.35)
        return {"root": (30 * u, 0, 0, 0, -0.3 * u, 0)}
    out.append(sample(raise_, 0.35, 5, loop=False, name="raise"))
    return out


# ------------------------------------------------------------------ the gadgets

def one_bone():
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.2, 0))
    return sk


def wheel_mesh(sk, name="fuse_wheel"):
    """the Boom Wheel: a fat wheel (axle along x) with spikes, a bomb drum
    in the middle and a red light; rolls about x"""
    m = Mesh(name)
    r = 0.5
    m.add(cylinder(r, 0.34, segs=12, axis="x").move(-0.17, 0, 0), BOMB, 0, 1, 3)
    m.add(cylinder(r, 0.34, segs=7, axis="x").move(-0.17, 0, 0), BOMB, 0, 0, 0)
    m.add(cylinder(r * 0.62, 0.36, segs=10, axis="x").move(-0.18, 0, 0), HAZARD, 0, 1, 3)
    m.add(cylinder(r * 0.3, 0.38, segs=8, axis="x").move(-0.19, 0, 0), RUST, 0, 1, 3)
    for i in range(10):
        a = 2 * math.pi * i / 10
        cy, cz = math.cos(a) * r, math.sin(a) * r
        sp = cylinder(0.05, 0.14, segs=4, r2=0.005, caps=False)
        m.add(sp.turn(rx=math.degrees(a) - 90).move(0, cy, cz), STEEL, 0, 3, 3)
    m.add(ellipsoid(0.06, 0.06, 0.06, segs=6, rings=4).move(0.2, 0, 0), RED, 0, 0, 3)
    m.add(ellipsoid(0.06, 0.06, 0.06, segs=6, rings=4).move(-0.2, 0, 0), RED, 0, 0, 3)
    return m.weld()


def mine_mesh(sk, name="fuse_mine"):
    m = Mesh(name)
    m.add(cylinder(0.16, 0.07, segs=10).move(0, -0.035, 0), BOMB, 0, 0, 3)
    m.add(cylinder(0.12, 0.08, segs=10).move(0, -0.04, 0), HAZARD, 0, 1, 3)
    m.add(ellipsoid(0.04, 0.03, 0.04, segs=6, rings=3).move(0, 0.05, 0), RED, 0, 0, 3)
    return m.weld()


def trap_skeleton():
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.05, 0))
    sk.bone("jaw.L", "root", (0, 0.02, 0), (-0.25, 0.02, 0))
    sk.bone("jaw.R", "root", (0, 0.02, 0), (0.25, 0.02, 0))
    return sk


def trap_mesh(sk, name="fuse_trap"):
    """the Steel Trap: a base plate and two toothed jaws hinged in the middle"""
    m = Mesh(name)
    m.add(cylinder(0.12, 0.03, segs=8), DARK, 0, 0, 3)
    m.add(box(0.5, 0.02, 0.06).move(0, 0.02, 0), STEEL, 0, 1, 3)
    for s, x in (("L", -1), ("R", 1)):
        jb = sk.index[f"jaw.{s}"]
        # a half ring of steel lying flat, teeth up
        for i in range(5):
            a0, a1 = math.pi * i / 5, math.pi * (i + 1) / 5
            p0 = (x * 0.25 * math.sin(a0), 0.02, 0.25 * math.cos(a0))
            p1 = (x * 0.25 * math.sin(a1), 0.02, 0.25 * math.cos(a1))
            m.add(tube(p0, p1, 0.022, 0.022, segs=4), STEEL, jb, 0, 3)
            mx, mz = (p0[0] + p1[0]) / 2, (p0[2] + p1[2]) / 2
            m.add(hull([(mx - 0.02, 0.03, mz), (mx + 0.02, 0.03, mz), (mx, 0.03, mz + 0.02), (mx * 0.92, 0.1, mz * 0.92)]),
                  STEEL, jb, 3, 3)
    return m.weld()


def trap_clips(sk):
    out = [sample(lambda t: {"jaw.L": (0, 0, 0), "jaw.R": (0, 0, 0)}, 1.0, 2, name="open")]

    def shut(t):
        u = _sm(t / 0.08)
        return {"jaw.L": (0, 0, -85 * u), "jaw.R": (0, 0, 85 * u)}
    out.append(sample(shut, 0.3, 6, loop=False, name="shut"))
    return out


def build():
    sk = skeleton()
    out = [(mesh(sk), sk, clips(sk))]
    fsk = fp_skeleton()
    out.append((fp_mesh(fsk), fsk, fp_clips(fsk)))
    sk1 = one_bone()
    out.append((wheel_mesh(sk1), sk1, []))
    sk2 = one_bone()
    out.append((mine_mesh(sk2), sk2, []))
    tsk = trap_skeleton()
    out.append((trap_mesh(tsk), tsk, trap_clips(tsk)))
    return out
