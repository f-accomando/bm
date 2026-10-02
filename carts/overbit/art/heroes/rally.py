"""
Rally - tank (the kit of D.Va): a racing driver in a white mech.

The mech: a glossy white shell like a sports car, a black canopy with light
strips, two arm cannons, missile pods on the shoulders, two thrusters at
the back, bird legs (knee forward, hock back) with claw feet; orange lights.
The pilot: short dark hair, orange glasses, white top under a grey vest, a
purple jacket, grey trousers with white stripes, purple trainers; a small
light gun.

Models: rally_mech (third person), rally_pilot, rally_fp (the cockpit seen
from inside: the two cannons), rally_pfp (the pilot's hands and gun).
"""
import math

from geo import (Mat, Mesh, Part, box, cylinder, ellipsoid, hull, join, lathe, wedge, add, sub, mul,
                 norm, cross, length)
import rig
from rig import Skeleton, Clip, sample, mix, over, leg_ik

# ------------------------------------------------------------------ colours

WHITE = Mat(0xE9EDF0, glossy=True)
WHITE2 = Mat(0xD4D9DE, glossy=True)          # panels a step darker
BLACK = Mat(0x1C1E23, glossy=True)
DARK = Mat(0x3E4249)
GREY = Mat(0x777D86)
LIGHTGREY = Mat(0xA9AFB7, glossy=True)
ORANGE = Mat(0xFF6E1C, emissive=True)
ORANGE_PAINT = Mat(0xF26A21, glossy=True)
GLOW = Mat(0xEAF8FF, emissive=True)
RED = Mat(0xFF2A2A, emissive=True)
SEAT = Mat(0xC9A271)
THRUST = Mat(0xFFB04A, emissive=True)

SKIN = Mat(0xEDBB9C)
HAIR = Mat(0x2B1E1A)
LENS = Mat(0xFF8A24, glossy=True)
TOP = Mat(0xF3F3F3)
VEST = Mat(0xB4B8BE)
JACKET = Mat(0x6E4C80)
JACKET2 = Mat(0x5A3C6A)
PANTS = Mat(0x8C929B)
STRIPE = Mat(0xEEEEEE)
SHOE = Mat(0x6D4A7B)
SOLE = Mat(0xCDA77E)
GLOVE = Mat(0x3B2B24)
GUN_W = Mat(0xF1EAF1, glossy=True)
GUN_P = Mat(0xFF73AE, glossy=True)
GUN_GLOW = Mat(0x6CF2FF, emissive=True)


def quad(p0, p1, p2, p3):
    """a flat panel, clockwise seen from the side that shows"""
    return Part([p0, p1, p2, p3], [(0, 1, 2), (0, 2, 3)], False)


def strip(centre, w, h, ry=0, rx=0):
    """a light strip facing +z (turned by ry around y)"""
    x, y, z = centre
    q = quad((-w / 2, h / 2, 0), (w / 2, h / 2, 0), (w / 2, -h / 2, 0), (-w / 2, -h / 2, 0))
    # clockwise seen from +z means counterclockwise in x-y as usual: check
    q.turn(rx=rx, ry=ry)
    return q.move(x, y, z)


def facing(part, towards):
    """flips a flat part's faces so that they show towards `towards`"""
    out = []
    for f in part.faces:
        a, b, c = (part.pts[i] for i in f)
        n = cross(sub(b, a), sub(c, a))
        out.append(f if n[0] * towards[0] + n[1] * towards[1] + n[2] * towards[2] >= 0 else (f[0], f[2], f[1]))
    part.faces = out
    return part


def both(mesh, make, mat, bone_l, bone_r, lo=0, hi=3):
    """a part on the left (made at -x) and its mirror on the right"""
    p = make()
    mesh.add(p, mat, bone_l, lo, hi)
    mesh.add(p.copy().mirror_x(), mat, bone_r, lo, hi)


# ------------------------------------------------------------------ the mech

HIP_Y = 1.45
LEG_X = 0.58
KNEE = (0.88, 0.36)       # y, z (x is the leg's)
HOCK = (0.40, -0.12)
TOE = (0.07, 0.20)


def mech_skeleton():
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.5, 0))
    sk.bone("hips", "root", (0, HIP_Y, 0), (0, 1.75, 0))
    sk.bone("body", "hips", (0, 1.72, 0), (0, 2.5, 0))
    sk.bone("canopy", "body", (0, 2.42, 0.05), (0, 2.25, 0.85))
    for s, x in (("L", -1), ("R", 1)):
        sk.bone(f"arm.{s}", "body", (x * 0.95, 2.0, 0.25), (x * 1.18, 1.78, 0.30))
        sk.bone(f"gun.{s}", f"arm.{s}", (x * 1.18, 1.76, 0.30), (x * 1.18, 1.76, 1.48))
        sk.bone(f"pod.{s}", "body", (x * 0.72, 2.50, -0.12), (x * 0.72, 2.52, 0.35))
    for s, x in (("L", -1), ("R", 1)):
        sk.bone(f"thigh.{s}", "hips", (x * LEG_X, HIP_Y, 0), (x * 0.60, KNEE[0], KNEE[1]))
        sk.bone(f"shin.{s}", f"thigh.{s}", (x * 0.60, KNEE[0], KNEE[1]), (x * 0.60, HOCK[0], HOCK[1]))
        sk.bone(f"foot.{s}", f"shin.{s}", (x * 0.60, HOCK[0], HOCK[1]), (x * 0.60, TOE[0], TOE[1]))
    return sk


def mech_mesh(sk, name="rally_mech"):
    """about 650 / 420 / 260 / 130 triangles at details 3 / 2 / 1 / 0"""
    m = Mesh(name)
    B = sk.index
    # -- the shell: a sports car's roof, white and glossy
    m.add(ellipsoid(0.96, 0.60, 1.16, segs=10, rings=6).move(0, 2.06, -0.08), WHITE, B["body"], 2, 3)
    m.add(ellipsoid(0.96, 0.60, 1.16, segs=8, rings=5).move(0, 2.06, -0.08), WHITE, B["body"], 0, 1)
    # -- the canopy: black glass in front, light strips on its sides
    m.add(ellipsoid(0.80, 0.42, 0.62, segs=8, rings=5).move(0, 1.94, 0.50), BLACK, B["canopy"], 1, 3)
    m.add(ellipsoid(0.80, 0.42, 0.62, segs=6, rings=3).move(0, 1.94, 0.50), BLACK, B["canopy"], 0, 0)
    for k in range(3):
        y = 1.86 + k * 0.085
        for x in (-1, 1):
            z = 0.50 + 0.62 * math.sqrt(max(0, 1 - (0.47 / 0.80) ** 2 - ((y - 1.94) / 0.42) ** 2)) + 0.015
            p = strip((x * 0.47, y, z), 0.20 - k * 0.02, 0.028, ry=x * 36)
            m.add(facing(p, (x * 0.6, 0, 1)), GLOW, B["canopy"], 1 if k == 1 else 2, 3)
    # -- fins on the roof, swept back
    for x in (-1, 1):
        m.add(hull([(x * 0.30, 2.55, -0.20), (x * 0.36, 2.55, -0.20), (x * 0.30, 2.55, -0.55), (x * 0.36, 2.55, -0.55),
                    (x * 0.44, 2.98, -0.78), (x * 0.48, 2.98, -0.70), (x * 0.44, 2.92, -0.88)]), BLACK, B["body"], 1, 3)
    # -- back: thrusters and the tail light
    for x in (-1, 1):
        m.add(cylinder(0.17, 0.30, segs=6, r2=0.20, caps=False, axis="z").move(x * 0.42, 2.02, -1.30), GREY, B["body"], 1, 3)
        m.add(facing(cylinder(0.13, 0.01, segs=6, caps=True, smooth=False, axis="z").move(x * 0.42, 2.02, -1.31),
                     (0, 0, -1)), THRUST, B["body"], 1, 3)
    m.add(facing(strip((0, 1.93, -1.235), 0.36, 0.05), (0, 0, -1)), RED, B["body"], 2, 3)
    # -- under the shell: the pelvis block
    m.add(box(0.86, 0.34, 0.78).move(0, 1.52, -0.05), DARK, B["hips"], 0, 3)
    # -- shoulders, arms and cannons
    both(m, lambda: cylinder(0.20, 0.30, segs=6, caps=False, axis="x").move(-1.10, 2.0, 0.25), DARK,
         B["arm.L"], B["arm.R"], 2, 3)
    both(m, lambda: box(0.42, 0.50, 0.62, bevel=0.07).move(-1.18, 1.95, 0.24), WHITE, B["arm.L"], B["arm.R"], 3, 3)
    both(m, lambda: box(0.42, 0.50, 0.62).move(-1.18, 1.95, 0.24), WHITE, B["arm.L"], B["arm.R"], 0, 2)
    both(m, lambda: box(0.40, 0.38, 1.16, bevel=0.06).move(-1.18, 1.74, 0.80), WHITE, B["gun.L"], B["gun.R"], 2, 3)
    both(m, lambda: box(0.40, 0.38, 1.16).move(-1.18, 1.74, 0.80), WHITE, B["gun.L"], B["gun.R"], 0, 1)
    # black front of the cannon with two light bars, muzzles
    both(m, lambda: facing(quad((-1.33, 1.875, 1.387), (-1.03, 1.875, 1.387), (-1.03, 1.605, 1.387), (-1.33, 1.605, 1.387)),
                           (0, 0, 1)), BLACK, B["gun.L"], B["gun.R"], 1, 3)
    for y in (1.80, 1.68):
        both(m, lambda y=y: facing(strip((-1.18, y, 1.392), 0.22, 0.03), (0, 0, 1)), GLOW, B["gun.L"], B["gun.R"], 1, 3)
    for dx in (-0.07, 0.07):
        both(m, lambda dx=dx: cylinder(0.06, 0.14, segs=5, caps=False, axis="z").move(-1.18 + dx, 1.74, 1.39), DARK,
             B["gun.L"], B["gun.R"], 3, 3)
    # orange strips along the cannons' outer sides, a black grille
    both(m, lambda: facing(quad((-1.385, 1.79, 0.50), (-1.385, 1.79, 1.20), (-1.385, 1.76, 1.20), (-1.385, 1.76, 0.50)),
                           (-1, 0, 0)), ORANGE, B["gun.L"], B["gun.R"], 2, 3)
    both(m, lambda: facing(quad((-1.385, 1.70, 0.60), (-1.385, 1.70, 1.05), (-1.385, 1.60, 1.05), (-1.385, 1.60, 0.60)),
                           (-1, 0, 0)), BLACK, B["gun.L"], B["gun.R"], 3, 3)
    # -- missile pods on the shoulders: the lid opens (pod bone)
    both(m, lambda: box(0.32, 0.16, 0.48).move(-0.72, 2.43, 0.10), DARK, B["body"], B["body"], 1, 3)
    both(m, lambda: box(0.34, 0.06, 0.50).move(-0.72, 2.53, 0.10), WHITE, B["pod.L"], B["pod.R"], 1, 3)
    both(m, lambda: facing(quad((-0.84, 2.45, 0.345), (-0.60, 2.45, 0.345), (-0.60, 2.40, 0.345), (-0.84, 2.40, 0.345)),
                           (0, 0, 1)), ORANGE, B["body"], B["body"], 2, 3)
    # -- legs
    for s, x in (("L", -1), ("R", 1)):
        T, S, F = B[f"thigh.{s}"], B[f"shin.{s}"], B[f"foot.{s}"]
        lx = x * LEG_X
        kx = x * 0.60
        # the thigh: a big white armour plate
        m.add(hull([(lx - 0.21, 1.62, -0.24), (lx + 0.21, 1.62, -0.24), (lx - 0.21, 1.60, 0.28), (lx + 0.21, 1.60, 0.28),
                    (kx - 0.16, KNEE[0] + 0.02, KNEE[1] - 0.18), (kx + 0.16, KNEE[0] + 0.02, KNEE[1] - 0.18),
                    (kx - 0.15, KNEE[0] + 0.08, KNEE[1] + 0.16), (kx + 0.15, KNEE[0] + 0.08, KNEE[1] + 0.16),
                    (lx, 1.30, 0.42)]), WHITE, T, 0, 3)
        m.add(cylinder(0.13, 0.36, segs=6, axis="x").move(kx - 0.18, KNEE[0], KNEE[1]), DARK, T, 1, 3)
        # the shin: dark strut, white guard in front
        m.add(hull([(kx - 0.11, KNEE[0], KNEE[1] - 0.10), (kx + 0.11, KNEE[0], KNEE[1] - 0.10),
                    (kx - 0.11, KNEE[0], KNEE[1] + 0.06), (kx + 0.11, KNEE[0], KNEE[1] + 0.06),
                    (kx - 0.08, HOCK[0], HOCK[1] - 0.07), (kx + 0.08, HOCK[0], HOCK[1] - 0.07),
                    (kx - 0.08, HOCK[0], HOCK[1] + 0.07), (kx + 0.08, HOCK[0], HOCK[1] + 0.07)]), GREY, S, 0, 3)
        m.add(hull([(kx - 0.13, KNEE[0] - 0.08, KNEE[1] + 0.02), (kx + 0.13, KNEE[0] - 0.08, KNEE[1] + 0.02),
                    (kx - 0.12, 0.55, 0.03), (kx + 0.12, 0.55, 0.03), (kx, 0.70, 0.16),
                    (kx - 0.10, 0.75, 0.10), (kx + 0.10, 0.75, 0.10)]), WHITE, S, 1, 3)
        m.add(cylinder(0.08, 0.22, segs=5, caps=False, axis="x").move(kx - 0.11, HOCK[0], HOCK[1]), DARK, S, 2, 3)
        # yellow coil at the back of the shin (as in the reference)
        m.add(cylinder(0.055, 0.16, segs=5, caps=False, axis="y").move(kx, 0.58, -0.04), Mat(0xE8C23A, glossy=True), S, 3, 3)
        # the foot: long white toes, orange tips, a claw behind
        m.add(hull([(kx - 0.10, HOCK[0], HOCK[1] - 0.05), (kx + 0.10, HOCK[0], HOCK[1] - 0.05),
                    (kx - 0.12, 0.10, 0.05), (kx + 0.12, 0.10, 0.05), (kx, HOCK[0] + 0.04, HOCK[1] + 0.06),
                    (kx - 0.12, 0.02, 0.20), (kx + 0.12, 0.02, 0.20)]), WHITE, F, 0, 3)
        for dx, dz, ang in ((-0.10, 0.05, -18), (0.0, 0.08, 0), (0.10, 0.05, 18)):
            toe = hull([(-0.045, 0.00, 0.0), (0.045, 0.00, 0.0), (-0.045, 0.10, 0.0), (0.045, 0.10, 0.0),
                        (-0.03, 0.00, 0.30), (0.03, 0.00, 0.30), (0.0, 0.05, 0.34)]).turn(ry=ang * x)
            m.add(toe.move(kx + dx * x, 0.0, 0.18 + dz), WHITE2, F, 1 if dx == 0 else 2, 3)
            tip = facing(quad((-0.03, 0.015, 0.0), (0.03, 0.015, 0.0), (0.03, 0.015, 0.06), (-0.03, 0.015, 0.06)),
                         (0, 1, 0)).turn(ry=ang * x).move(kx + dx * x, 0.06, 0.40 + dz)
            m.add(tip, ORANGE, F, 3, 3)
        m.add(hull([(kx - 0.05, 0.0, -0.05), (kx + 0.05, 0.0, -0.05), (kx - 0.05, 0.12, 0.0), (kx + 0.05, 0.12, 0.0),
                    (kx, 0.0, -0.30)]), WHITE2, F, 2, 3)
        m.add(facing(quad((kx - 0.08, 0.20, 0.13), (kx + 0.08, 0.20, 0.13), (kx + 0.08, 0.17, 0.15), (kx - 0.08, 0.17, 0.15)),
                     (0, 0.3, 1)), ORANGE, F, 2, 3)
    return m.weld()


# ------------------------------------------------------------------ the pilot

def pilot_skeleton():
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.4, 0))
    sk.bone("hips", "root", (0, 0.90, 0), (0, 1.05, 0))
    sk.bone("spine", "hips", (0, 1.00, 0), (0, 1.30, 0))
    sk.bone("chest", "spine", (0, 1.22, 0), (0, 1.44, 0))
    sk.bone("head", "chest", (0, 1.44, 0), (0, 1.66, 0))
    for s, x in (("L", -1), ("R", 1)):
        sk.bone(f"upperarm.{s}", "chest", (x * 0.19, 1.39, 0), (x * 0.24, 1.14, 0))
        sk.bone(f"forearm.{s}", f"upperarm.{s}", (x * 0.24, 1.14, 0), (x * 0.27, 0.91, 0.02))
        sk.bone(f"hand.{s}", f"forearm.{s}", (x * 0.27, 0.91, 0.02), (x * 0.28, 0.82, 0.03))
    for s, x in (("L", -1), ("R", 1)):
        sk.bone(f"thigh.{s}", "hips", (x * 0.10, 0.90, 0), (x * 0.11, 0.49, 0.01))
        sk.bone(f"shin.{s}", f"thigh.{s}", (x * 0.11, 0.49, 0.01), (x * 0.11, 0.08, -0.02))
        sk.bone(f"foot.{s}", f"shin.{s}", (x * 0.11, 0.08, -0.02), (x * 0.11, 0.03, 0.16))
    return sk


def pilot_mesh(sk, name="rally_pilot"):
    m = Mesh(name)
    B = sk.index
    # head: face, hair (a bob with a fringe), glasses
    m.add(ellipsoid(0.085, 0.105, 0.095, segs=8, rings=6).move(0, 1.555, 0.01), SKIN, B["head"], 1, 3)
    m.add(ellipsoid(0.085, 0.105, 0.095, segs=6, rings=4).move(0, 1.555, 0.01), SKIN, B["head"], 0, 0)
    m.add(hull([(-0.095, 1.60, -0.02), (0.095, 1.60, -0.02), (-0.09, 1.66, 0.06), (0.09, 1.66, 0.06),
                (0.0, 1.685, 0.0), (-0.07, 1.67, -0.07), (0.07, 1.67, -0.07), (-0.10, 1.50, -0.05),
                (0.10, 1.50, -0.05), (-0.06, 1.47, -0.09), (0.06, 1.47, -0.09), (0.0, 1.60, 0.115),
                (-0.07, 1.585, 0.10), (0.07, 1.585, 0.10)], smooth=True), HAIR, B["head"], 0, 3)
    m.add(hull([(-0.075, 1.575, 0.085), (0.075, 1.575, 0.085), (-0.075, 1.545, 0.085), (0.075, 1.545, 0.085),
                (-0.07, 1.575, 0.105), (0.07, 1.575, 0.105), (-0.07, 1.545, 0.105), (0.07, 1.545, 0.105)]),
          LENS, B["head"], 1, 3)
    # neck, torso: white top, grey vest, purple jacket
    m.add(cylinder(0.04, 0.08, segs=6).move(0, 1.42, 0), SKIN, B["chest"], 1, 3)
    m.add(hull([(-0.17, 1.44, -0.08), (0.17, 1.44, -0.08), (-0.17, 1.44, 0.08), (0.17, 1.44, 0.08),
                (-0.14, 1.18, -0.07), (0.14, 1.18, -0.07), (-0.14, 1.18, 0.08), (0.14, 1.18, 0.08)], smooth=True),
          JACKET, B["chest"], 0, 3)
    m.add(hull([(-0.08, 1.43, 0.085), (0.08, 1.43, 0.085), (-0.10, 1.20, 0.085), (0.10, 1.20, 0.085),
                (-0.08, 1.43, 0.06), (0.08, 1.43, 0.06), (-0.10, 1.20, 0.06), (0.10, 1.20, 0.06)]), VEST, B["chest"], 1, 3)
    m.add(hull([(-0.03, 1.43, 0.095), (0.03, 1.43, 0.095), (-0.03, 1.21, 0.095), (0.03, 1.21, 0.095),
                (-0.03, 1.43, 0.07), (0.03, 1.43, 0.07), (-0.03, 1.21, 0.07), (0.03, 1.21, 0.07)]), TOP, B["chest"], 2, 3)
    m.add(hull([(-0.13, 1.20, -0.07), (0.13, 1.20, -0.07), (-0.13, 1.20, 0.08), (0.13, 1.20, 0.08),
                (-0.12, 0.98, -0.06), (0.12, 0.98, -0.06), (-0.12, 0.98, 0.07), (0.12, 0.98, 0.07)], smooth=True),
          TOP, B["spine"], 0, 3)
    m.add(hull([(-0.13, 1.00, -0.07), (0.13, 1.00, -0.07), (-0.13, 1.00, 0.08), (0.13, 1.00, 0.08),
                (-0.12, 0.86, -0.07), (0.12, 0.86, -0.07), (-0.12, 0.86, 0.07), (0.12, 0.86, 0.07)]), PANTS, B["hips"], 0, 3)
    for s, x in (("L", -1), ("R", 1)):
        UA, FA, H = B[f"upperarm.{s}"], B[f"forearm.{s}"], B[f"hand.{s}"]
        TH, SH, FT = B[f"thigh.{s}"], B[f"shin.{s}"], B[f"foot.{s}"]
        m.add(cylinder(0.055, 0.27, segs=6, r2=0.05).turn(rz=x * 168).move(x * 0.19, 1.41, 0), JACKET, UA, 0, 3)
        m.add(cylinder(0.05, 0.25, segs=6, r2=0.04).turn(rz=x * 172).move(x * 0.24, 1.15, 0.0), JACKET2, FA, 0, 3)
        m.add(ellipsoid(0.04, 0.055, 0.035, segs=6, rings=4).move(x * 0.275, 0.87, 0.025), GLOVE, H, 0, 3)
        m.add(cylinder(0.07, 0.42, segs=6, r2=0.055).turn(rz=180).move(x * 0.10, 0.91, 0.0), PANTS, TH, 0, 3)
        m.add(cylinder(0.055, 0.42, segs=6, r2=0.045).turn(rz=180).move(x * 0.11, 0.50, 0.01), PANTS, SH, 0, 3)
        m.add(facing(quad((x * 0.168, 0.85, -0.01), (x * 0.168, 0.85, 0.02), (x * 0.155, 0.12, 0.02),
                          (x * 0.155, 0.12, -0.01)), (x, 0, 0)), STRIPE, TH, 2, 3)
        m.add(hull([(x * 0.11 - 0.05, 0.0, -0.06), (x * 0.11 + 0.05, 0.0, -0.06), (x * 0.11 - 0.05, 0.0, 0.18),
                    (x * 0.11 + 0.05, 0.0, 0.18), (x * 0.11 - 0.045, 0.10, -0.05), (x * 0.11 + 0.045, 0.10, -0.05),
                    (x * 0.11 - 0.04, 0.07, 0.12), (x * 0.11 + 0.04, 0.07, 0.12)]), SHOE, FT, 0, 3)
        m.add(box(0.105, 0.02, 0.25).move(x * 0.11, 0.01, 0.06), SOLE, FT, 2, 3)
    # the light gun in the right hand
    g = B["hand.R"]
    m.add(box(0.04, 0.05, 0.16, bevel=0.01).move(0.28, 0.86, 0.11), GUN_W, g, 1, 3)
    m.add(box(0.03, 0.07, 0.035).move(0.28, 0.82, 0.06), GUN_P, g, 1, 3)
    m.add(facing(strip((0.28, 0.865, 0.191), 0.02, 0.02), (0, 0, 1)), GUN_GLOW, g, 2, 3)
    return m.weld()


# ------------------------------------------------------------------ first person

def fp_skeleton():
    """the mech's cannons seen from the cockpit: the camera is at the origin
    looking along +z"""
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.2, 0))
    for s, x in (("L", -1), ("R", 1)):
        sk.bone(f"arm.{s}", "root", (x * 0.80, -0.55, 0.30), (x * 0.95, -0.70, 0.55))
        sk.bone(f"gun.{s}", f"arm.{s}", (x * 0.95, -0.70, 0.55), (x * 0.95, -0.70, 1.75))
    return sk


def fp_mesh(sk, name="rally_fp"):
    m = Mesh(name)
    B = sk.index
    for s, x in (("L", -1), ("R", 1)):
        A, G = B[f"arm.{s}"], B[f"gun.{s}"]
        m.add(box(0.46, 0.44, 0.50, bevel=0.06).move(x * 0.95, -0.66, 0.38), WHITE, A, 0, 3)
        m.add(cylinder(0.16, 0.30, segs=8, axis="x").move(x * 0.95 - (0.15 if x > 0 else 0.15) * 0 - 0.15, -0.52, 0.30),
              DARK, A, 0, 3)
        m.add(box(0.42, 0.40, 1.20, bevel=0.06).move(x * 0.95, -0.72, 1.15), WHITE, G, 0, 3)
        m.add(box(0.30, 0.28, 0.06).move(x * 0.95, -0.72, 1.76), BLACK, G, 0, 3)
        for y in (-0.66, -0.78):
            m.add(facing(strip((x * 0.95, y, 1.795), 0.22, 0.03), (0, 0, 1)), GLOW, G, 0, 3)
        for dx in (-0.07, 0.07):
            m.add(cylinder(0.065, 0.18, segs=8, axis="z").move(x * 0.95 + dx, -0.72, 1.76), DARK, G, 0, 3)
        # the orange strip on top, the black grille on the inner side
        m.add(facing(quad((x * 0.85, -0.515, 0.70), (x * 1.05, -0.515, 0.70), (x * 1.05, -0.515, 1.60),
                          (x * 0.85, -0.515, 1.60)), (0, 1, 0)), ORANGE, G, 0, 3)
        m.add(facing(quad((x * 0.735, -0.62, 0.80), (x * 0.735, -0.62, 1.50), (x * 0.735, -0.80, 1.50),
                          (x * 0.735, -0.80, 0.80)), (-x, 0, 0)), BLACK, G, 0, 3)
    return m.weld()


def pfp_skeleton():
    sk = Skeleton()
    sk.bone("root", None, (0, 0, 0), (0, 0.1, 0))
    sk.bone("hand.R", "root", (0.16, -0.20, 0.22), (0.17, -0.19, 0.40))
    sk.bone("hand.L", "root", (-0.18, -0.26, 0.26), (-0.12, -0.22, 0.38))
    return sk


def pfp_mesh(sk, name="rally_pfp"):
    m = Mesh(name)
    R, L = sk.index["hand.R"], sk.index["hand.L"]
    m.add(cylinder(0.045, 0.30, segs=8, r2=0.04, axis="z").move(0.17, -0.24, -0.05), JACKET2, R, 0, 3)
    m.add(ellipsoid(0.05, 0.045, 0.06, segs=8, rings=5).move(0.17, -0.22, 0.25), GLOVE, R, 0, 3)
    m.add(box(0.05, 0.065, 0.24, bevel=0.012).move(0.17, -0.16, 0.36), GUN_W, R, 0, 3)
    m.add(box(0.035, 0.09, 0.05).move(0.17, -0.22, 0.30), GUN_P, R, 0, 3)
    m.add(box(0.055, 0.02, 0.12).move(0.17, -0.12, 0.38), GUN_P, R, 0, 3)
    m.add(facing(strip((0.17, -0.16, 0.481), 0.03, 0.03), (0, 0, 1)), GUN_GLOW, R, 0, 3)
    m.add(cylinder(0.045, 0.30, segs=8, r2=0.04, axis="z").move(-0.20, -0.30, -0.02), JACKET2, L, 0, 3)
    m.add(ellipsoid(0.05, 0.045, 0.06, segs=8, rings=5).move(-0.19, -0.27, 0.28), GLOVE, L, 0, 3)
    return m.weld()


# ------------------------------------------------------------------ animation

def _sm(t):
    """smoothstep 0..1"""
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def mech_legs(sk, phase, stride, lift, bob, side_phase=0.5, crouch=0.0):
    """leg pose of the mech at a phase of its step cycle (0..1): each foot
    on the ground half the cycle (moving back by `stride`), in the air the
    other half; the hock reached by inverse kinematics"""
    pose = {}
    hip_y = HIP_Y + bob - crouch
    l1 = math.hypot(HIP_Y - KNEE[0], KNEE[1])
    l2 = math.hypot(KNEE[0] - HOCK[0], KNEE[1] - HOCK[1])
    rest_t = (KNEE[0] - HIP_Y, KNEE[1])
    rest_s = (HOCK[0] - KNEE[0], HOCK[1] - KNEE[1])
    for s, off in (("L", 0.0), ("R", side_phase)):
        p = (phase + off) % 1.0
        if p < 0.5:                       # on the ground, sliding back
            u = p / 0.5
            z = stride / 2 - stride * u
            y = TOE[0]
            foot_rx = 0.0
        else:                             # in the air, forward
            u = (p - 0.5) / 0.5
            z = -stride / 2 + stride * _sm(u)
            y = TOE[0] + lift * math.sin(math.pi * u)
            foot_rx = 25 * math.sin(math.pi * u)
        toe = (0, y, TOE[1] + z)
        hock = (0, toe[1] + (HOCK[0] - TOE[0]), toe[2] + (HOCK[1] - TOE[1]))
        th, sh = leg_ik((0, hip_y, 0), hock, l1, l2, rest_t, rest_s)
        pose[f"thigh.{s}"] = (th, 0, 0)
        pose[f"shin.{s}"] = (sh, 0, 0)
        pose[f"foot.{s}"] = (foot_rx - th - sh, 0, 0)
    pose["root"] = (0, 0, 0, 0, bob - crouch, 0)
    return pose


def mech_clips(sk):
    clips = []
    still = mech_legs(sk, 0.25, 0.0, 0.0, 0.0)          # both feet under the hips

    def idle(t):
        k = math.sin(2 * math.pi * t / 2.4)
        p = mech_legs(sk, 0.25, 0.0, 0.0, -0.015 + 0.015 * k)
        p.update({"body": (1.5 * k, 0, 0), "arm.L": (-2 * k, 0, 1), "arm.R": (-2 * k, 0, -1),
                  "gun.L": (1.5 * k, 0, 0), "gun.R": (1.5 * k, 0, 0)})
        return p
    clips.append(sample(idle, 2.4, 8, name="idle"))

    def walker(period, stride, lift, bobk, sway, armk, name, lean=0):
        def f(t):
            ph = t / period
            bob = -0.05 + bobk * math.cos(4 * math.pi * ph)
            p = mech_legs(sk, ph, stride, lift, bob)
            p["body"] = (lean + 2 * math.cos(4 * math.pi * ph), 4 * math.sin(2 * math.pi * ph),
                         sway * math.sin(2 * math.pi * ph))
            p["hips"] = (0, -5 * math.sin(2 * math.pi * ph), 0)
            p["arm.L"] = (armk * math.sin(2 * math.pi * ph), 0, 0)
            p["arm.R"] = (-armk * math.sin(2 * math.pi * ph), 0, 0)
            return p
        return sample(f, period, 12, name=name)
    clips.append(walker(0.95, 1.10, 0.28, 0.035, 2.5, 6, "walk"))
    clips.append(walker(0.56, 1.55, 0.42, 0.06, 3.5, 9, "run", lean=6))

    def fire(t):
        p = {}
        for s, at in (("L", 0.0), ("R", 0.15)):
            u = ((t - at) % 0.3) / 0.15
            k = max(0.0, 1 - u) if u < 1 else 0.0
            p[f"gun.{s}"] = (-3 * k, 0, 0, 0, 0.02 * k, -0.14 * k)
        p["body"] = (-1.5, 0, 0)
        return p
    clips.append(sample(fire, 0.3, 6, name="fire", mode=0))

    def boost(t):
        k = math.sin(2 * math.pi * t)
        p = {"root": (0, 0, 0, 0, 0.25, 0), "hips": (28 + 2 * k, 0, 0), "body": (-6, 0, 2 * k),
             "arm.L": (-25, 0, -8), "arm.R": (-25, 0, 8), "gun.L": (-10, 0, 0), "gun.R": (-10, 0, 0)}
        for s in "LR":
            p[f"thigh.{s}"] = (32 + 3 * k, 0, 0)
            p[f"shin.{s}"] = (-55, 0, 0)
            p[f"foot.{s}"] = (60, 0, 0)
        return p
    clips.append(sample(boost, 1.0, 4, name="boost"))

    def matrix(t):
        k = math.sin(2 * math.pi * t * 3)
        return {"body": (-4, 0, 0), "arm.L": (-38 + k, -18, -12), "arm.R": (-38 - k, 18, 12),
                "gun.L": (-22, 14, 0), "gun.R": (-22, -14, 0)}
    clips.append(sample(matrix, 1.0, 6, name="matrix"))

    def missiles(t):
        o = _sm(t / 0.12) * (1 - _sm((t - 0.5) / 0.15))
        sh = 1.5 * math.sin(t * 60) * o
        return {"pod.L": (-58 * o, 0, 0), "pod.R": (-58 * o, 0, 0), "body": (-3 * o + sh, 0, 0)}
    clips.append(sample(missiles, 0.65, 13, loop=False, name="missiles"))

    def jump(t):
        u = t / 0.45
        c = 0.35 * math.sin(math.pi * min(1, u * 1.6)) if u < 0.62 else 0.0
        p = mech_legs(sk, 0.25, 0.0, 0.0, 0.0, crouch=c)
        tuck = _sm((u - 0.55) / 0.45)
        for s in "LR":
            th, sh, ft = p[f"thigh.{s}"][0], p[f"shin.{s}"][0], p[f"foot.{s}"][0]
            p[f"thigh.{s}"] = (th - 20 * tuck, 0, 0)
            p[f"shin.{s}"] = (sh + 30 * tuck, 0, 0)
            p[f"foot.{s}"] = (ft - 10 * tuck, 0, 0)
        p["arm.L"] = (-15 * tuck, 0, -10 * tuck)
        p["arm.R"] = (-15 * tuck, 0, 10 * tuck)
        return p
    clips.append(sample(jump, 0.45, 9, loop=False, name="jump"))

    def air(t):
        k = math.sin(2 * math.pi * t)
        p = dict(still)
        for s, sg in (("L", 1), ("R", -1)):
            p[f"thigh.{s}"] = (-20 + 5 * k * sg, 0, 0)
            p[f"shin.{s}"] = (30, 0, 0)
            p[f"foot.{s}"] = (-10, 0, 0)
        p["arm.L"] = (-15, 0, -12)
        p["arm.R"] = (-15, 0, 12)
        p["body"] = (-3, 0, 0)
        return p
    clips.append(sample(air, 1.0, 4, name="air"))

    def land(t):
        u = t / 0.35
        c = 0.30 * math.sin(math.pi * min(1, u)) * (1 - u * 0.3)
        p = mech_legs(sk, 0.25, 0.0, 0.0, 0.0, crouch=max(0, c))
        p["body"] = (4 * math.sin(math.pi * u), 0, 0)
        return p
    clips.append(sample(land, 0.35, 7, loop=False, name="land"))

    def crouch(t):
        p = mech_legs(sk, 0.25, 0.0, 0.0, 0.0, crouch=0.32)
        p["body"] = (3, 0, 0)
        return p
    clips.append(sample(crouch, 1.0, 2, name="crouch"))

    def hit(t):
        k = math.sin(math.pi * t / 0.25) * (1 - t / 0.25)
        return {"body": (8 * k, 0, 4 * k), "canopy": (0, 0, 0)}
    clips.append(sample(hit, 0.25, 5, loop=False, name="hit"))

    def eject(t):
        o = _sm(t / 0.35)
        sl = _sm((t - 0.3) / 0.8)
        p = mech_legs(sk, 0.25, 0.0, 0.0, 0.0, crouch=0.35 * sl)
        p["canopy"] = (-75 * o, 0, 0)
        p["body"] = (12 * sl, 0, 0)
        p["arm.L"] = (25 * sl, 0, -15 * sl)
        p["arm.R"] = (25 * sl, 0, 15 * sl)
        return p
    clips.append(sample(eject, 1.2, 12, loop=False, name="eject"))

    def selfd(t):
        sh = math.sin(t * 2 * math.pi * 6)
        p = mech_legs(sk, 0.25, 0.0, 0.0, 0.0, crouch=0.25)
        p["canopy"] = (-75, 0, 0)
        p["body"] = (10 + 2 * sh, 2 * sh, 3 * sh)
        p["arm.L"] = (30, 0, -20)
        p["arm.R"] = (30, 0, 20)
        p["pod.L"] = (-40, 0, 0)
        p["pod.R"] = (-40, 0, 0)
        return p
    clips.append(sample(selfd, 0.5, 6, name="selfdestruct"))

    def death(t):
        u = _sm(t / 1.1)
        p = mech_legs(sk, 0.25, 0.0, 0.0, 0.0, crouch=0.75 * u)
        p["hips"] = (22 * u, 0, 6 * u)
        p["body"] = (18 * u, 0, -8 * u)
        p["arm.L"] = (40 * u, 0, -25 * u)
        p["arm.R"] = (40 * u, 0, 30 * u)
        p["gun.L"] = (20 * u, 0, 0)
        p["gun.R"] = (25 * u, 0, 0)
        return p
    clips.append(sample(death, 1.1, 11, loop=False, name="death"))

    def call(t):
        u = t / 0.8
        c = 0.45 * (1 - _sm(u * 1.3)) if u < 0.8 else 0.0
        p = mech_legs(sk, 0.25, 0.0, 0.0, 0.0, crouch=c)
        p["body"] = (10 * (1 - _sm(u)), 0, 0)
        p["arm.L"] = (20 * (1 - _sm(u)), 0, -25 * (1 - _sm(u)))
        p["arm.R"] = (20 * (1 - _sm(u)), 0, 25 * (1 - _sm(u)))
        return p
    clips.append(sample(call, 0.8, 10, loop=False, name="callmech"))

    def victory(t):
        u = t / 2.6
        up = _sm(u / 0.2) * (1 - _sm((u - 0.85) / 0.15))
        hop = 0.25 * max(0.0, math.sin(math.pi * (u - 0.3) / 0.2)) if 0.3 < u < 0.5 else 0.0
        p = mech_legs(sk, 0.25, 0.0, 0.0, hop - 0.08 * up)
        p["arm.L"] = (-70 * up, 0, -25 * up)
        p["arm.R"] = (-70 * up, 0, 25 * up)
        p["gun.L"] = (-30 * up, 0, 0)
        p["gun.R"] = (-30 * up, 0, 0)
        p["body"] = (-8 * up, 20 * math.sin(2 * math.pi * u) * up, 0)
        p["pod.L"] = (-50 * up, 0, 0)
        p["pod.R"] = (-50 * up, 0, 0)
        return p
    clips.append(sample(victory, 2.6, 26, loop=False, name="victory"))
    return clips


def human_legs(phase, stride, lift, bob, hip_y=0.90, knee=(0.49, 0.01), ankle=(0.08, -0.02)):
    """the pilot's legs (knee in front) at a phase of the run"""
    pose = {}
    l1 = math.hypot(hip_y - knee[0], knee[1])
    l2 = math.hypot(knee[0] - ankle[0], knee[1] - ankle[1])
    for s, off in (("L", 0.0), ("R", 0.5)):
        p = (phase + off) % 1.0
        if p < 0.45:
            u = p / 0.45
            z = stride / 2 - stride * u
            y = ankle[0]
            frx = 0
        else:
            u = (p - 0.45) / 0.55
            z = -stride / 2 + stride * _sm(u)
            y = ankle[0] + lift * math.sin(math.pi * u) ** 0.8
            frx = 30 * math.sin(math.pi * u)
        th, sh = leg_ik((0, hip_y + bob, 0), (0, y, ankle[1] + z), l1, l2,
                        (knee[0] - hip_y, knee[1]), (ankle[0] - knee[0], ankle[1] - knee[1]))
        pose[f"thigh.{s}"] = (th, 0, 0)
        pose[f"shin.{s}"] = (sh, 0, 0)
        pose[f"foot.{s}"] = (frx - th - sh, 0, 0)
    pose["root"] = (0, 0, 0, 0, bob, 0)
    return pose


AIM_R = {"upperarm.R": (-80, -10, 8), "forearm.R": (-8, 0, 0), "hand.R": (5, 0, 0)}


def pilot_clips(sk):
    clips = []
    rest_arms = {"upperarm.L": (0, 0, -10), "upperarm.R": (0, 0, 10), "forearm.L": (-12, 0, 0), "forearm.R": (-12, 0, 0)}

    def idle(t):
        k = math.sin(2 * math.pi * t / 2.0)
        p = human_legs(0.0, 0.0, 0.0, -0.01 + 0.006 * k)
        p.update(rest_arms)
        p["chest"] = (1.5 * k, 0, 0)
        p["head"] = (-1.5 * k, 3 * math.sin(math.pi * t / 2.0), 0)
        return p
    clips.append(sample(idle, 2.0, 8, name="idle"))

    def run(t):
        ph = t / 0.62
        p = human_legs(ph, 0.95, 0.30, -0.04 + 0.035 * math.cos(4 * math.pi * ph))
        sw = math.sin(2 * math.pi * ph)
        p["spine"] = (8, 0, 0)
        p["chest"] = (0, 10 * sw, 0)
        p["hips"] = (0, -8 * sw, 0)
        p["upperarm.L"] = (-40 * sw, 0, -12)
        p["upperarm.R"] = (40 * sw, 0, 12)
        p["forearm.L"] = (-70, 0, 0)
        p["forearm.R"] = (-70, 0, 0)
        return p
    clips.append(sample(run, 0.62, 12, name="run"))

    def aim(t):
        p = dict(AIM_R)
        p["chest"] = (0, 8, 0)
        return p
    clips.append(sample(aim, 1.0, 2, name="aim"))

    def fire(t):
        u = (t % 0.14) / 0.14
        k = max(0.0, 1 - u * 2)
        p = dict(AIM_R)
        p["chest"] = (0, 8, 0)
        p["forearm.R"] = (-8 - 14 * k, 0, 0)
        p["hand.R"] = (5 - 10 * k, 0, 0)
        return p
    clips.append(sample(fire, 0.14, 4, name="fire", mode=0))

    def jump(t):
        u = t / 0.35
        p = human_legs(0.0, 0.0, 0.0, -0.12 * math.sin(math.pi * min(1, u * 1.4)))
        p.update({"upperarm.L": (-50 * u, 0, -30), "upperarm.R": (-50 * u, 0, 30)})
        return p
    clips.append(sample(jump, 0.35, 7, loop=False, name="jump"))

    def air(t):
        k = math.sin(2 * math.pi * t)
        return {"thigh.L": (-45 + 5 * k, 0, 0), "shin.L": (70, 0, 0), "thigh.R": (-20 - 5 * k, 0, 0),
                "shin.R": (40, 0, 0), "upperarm.L": (-30, 0, -40), "upperarm.R": (-30, 0, 40),
                "forearm.L": (-30, 0, 0), "forearm.R": (-30, 0, 0)}
    clips.append(sample(air, 1.0, 4, name="air"))

    def land(t):
        u = t / 0.3
        p = human_legs(0.0, 0.0, 0.0, -0.15 * math.sin(math.pi * u))
        p.update(rest_arms)
        return p
    clips.append(sample(land, 0.3, 6, loop=False, name="land"))

    def eject(t):
        u = _sm(t / 0.3)
        return {"spine": (-35 * u, 0, 0), "chest": (-25 * u, 0, 0), "head": (-20 * u, 0, 0),
                "thigh.L": (-110 * u, 0, 0), "thigh.R": (-110 * u, 0, 0), "shin.L": (130 * u, 0, 0),
                "shin.R": (130 * u, 0, 0), "upperarm.L": (-60 * u, 0, -20), "upperarm.R": (-60 * u, 0, 20),
                "forearm.L": (-100 * u, 0, 0), "forearm.R": (-100 * u, 0, 0)}
    clips.append(sample(eject, 0.9, 9, loop=False, name="eject"))

    def call(t):
        u = _sm(t / 0.25) * (1 - _sm((t - 0.85) / 0.25))
        p = human_legs(0.0, 0.0, 0.0, -0.01)
        p.update(rest_arms)
        p["upperarm.L"] = (-170 * u, 0, -10 - 10 * u)
        p["forearm.L"] = (-5 * u, 0, 0)
        p["head"] = (-25 * u, 0, 0)
        p["chest"] = (-5 * u, 0, 0)
        return p
    clips.append(sample(call, 1.1, 11, loop=False, name="call"))

    def hit(t):
        k = math.sin(math.pi * t / 0.25) * (1 - t / 0.25)
        return {"chest": (-12 * k, 0, 6 * k), "head": (-10 * k, 0, 0)}
    clips.append(sample(hit, 0.25, 5, loop=False, name="hit"))

    def death(t):
        u = _sm(t / 0.9)
        p = human_legs(0.0, 0.0, 0.0, -0.75 * u)
        p["hips"] = (-80 * u, 0, 0)
        p["chest"] = (-10 * u, 0, 0)
        p["head"] = (-20 * u, 0, 0)
        for s in "LR":
            p[f"thigh.{s}"] = (-75 * u + p[f"thigh.{s}"][0] * (1 - u), 0, 0)
            p[f"shin.{s}"] = (20 * u, 0, 0)
        p["upperarm.L"] = (-100 * u, 0, -40 * u)
        p["upperarm.R"] = (-100 * u, 0, 40 * u)
        return p
    clips.append(sample(death, 0.9, 9, loop=False, name="death"))

    def victory(t):
        u = t / 2.4
        up = _sm(u / 0.15) * (1 - _sm((u - 0.85) / 0.15))
        p = human_legs(0.0, 0.0, 0.0, -0.01)
        p.update(rest_arms)
        wave = 15 * math.sin(2 * math.pi * u * 4)
        p["upperarm.R"] = (-160 * up, 0, 20 * up + wave * up)
        p["forearm.R"] = (-20 * up, 0, 0)
        p["upperarm.L"] = (-20 * up, 0, -60 * up)
        p["forearm.L"] = (-110 * up, 0, 0)
        p["chest"] = (0, -15 * up, 6 * math.sin(2 * math.pi * u) * up)
        p["head"] = (0, 10 * up, 8 * up)
        p["hips"] = (0, 0, -5 * up)
        return p
    clips.append(sample(victory, 2.4, 24, loop=False, name="victory"))
    return clips


def fp_clips(sk):
    clips = []

    def idle(t):
        k = math.sin(2 * math.pi * t / 2.0)
        return {"root": (0.6 * k, 0, 0, 0, 0.01 * k, 0)}
    clips.append(sample(idle, 2.0, 8, name="idle"))

    def walk(t):
        ph = t / 0.56
        return {"root": (0, 1.5 * math.sin(2 * math.pi * ph), 1.0 * math.sin(2 * math.pi * ph),
                         0.02 * math.sin(2 * math.pi * ph), 0.03 * math.cos(4 * math.pi * ph), 0)}
    clips.append(sample(walk, 0.56, 8, name="walk"))

    def fire(t):
        p = {}
        for s, at in (("L", 0.0), ("R", 0.15)):
            u = ((t - at) % 0.3) / 0.15
            k = max(0.0, 1 - u) if u < 1 else 0.0
            p[f"gun.{s}"] = (-2.5 * k, 0, 0, 0, 0.015 * k, -0.12 * k)
        return p
    clips.append(sample(fire, 0.3, 6, name="fire", mode=0))

    def boost(t):
        k = math.sin(2 * math.pi * t * 2)
        return {"arm.L": (25, 15, 0, 0, -0.10, -0.15), "arm.R": (25, -15, 0, 0, -0.10, -0.15),
                "root": (0, 0, 1.5 * k)}
    clips.append(sample(boost, 1.0, 4, name="boost"))

    def matrix(t):
        k = math.sin(2 * math.pi * t * 4)
        return {"arm.L": (-12 + k, -22, 0, 0.15, 0.12, 0), "arm.R": (-12 - k, 22, 0, -0.15, 0.12, 0)}
    clips.append(sample(matrix, 1.0, 8, name="matrix"))

    def missiles(t):
        o = _sm(t / 0.1) * (1 - _sm((t - 0.5) / 0.15))
        return {"root": (-1.2 * o * math.sin(t * 70), 0, 0)}
    clips.append(sample(missiles, 0.65, 13, loop=False, name="missiles"))

    def eject(t):
        u = _sm(t / 0.5)
        return {"arm.L": (40 * u, 20 * u, 0, -0.2 * u, -0.6 * u, -0.3 * u),
                "arm.R": (40 * u, -20 * u, 0, 0.2 * u, -0.6 * u, -0.3 * u)}
    clips.append(sample(eject, 0.5, 6, loop=False, name="eject"))

    def selfd(t):
        sh = math.sin(t * 2 * math.pi * 8)
        return {"root": (1.5 * sh, 0, 1.0 * sh, 0, -0.05, 0), "arm.L": (15, 0, 0), "arm.R": (15, 0, 0)}
    clips.append(sample(selfd, 0.5, 8, name="selfdestruct"))

    def raise_(t):
        u = 1 - _sm(t / 0.35)
        return {"root": (25 * u, 0, 0, 0, -0.5 * u, 0)}
    clips.append(sample(raise_, 0.35, 5, loop=False, name="raise"))
    return clips


def pfp_clips(sk):
    clips = []

    def idle(t):
        k = math.sin(2 * math.pi * t / 2.0)
        return {"root": (0.8 * k, 0, 0, 0, 0.004 * k, 0)}
    clips.append(sample(idle, 2.0, 8, name="idle"))

    def run(t):
        ph = t / 0.62
        return {"root": (0, 2 * math.sin(2 * math.pi * ph), 0,
                         0.012 * math.sin(2 * math.pi * ph), 0.012 * math.cos(4 * math.pi * ph), 0)}
    clips.append(sample(run, 0.62, 8, name="run"))

    def fire(t):
        u = (t % 0.14) / 0.14
        k = max(0.0, 1 - u * 2)
        return {"hand.R": (-9 * k, 0, 0, 0, 0.008 * k, -0.035 * k)}
    clips.append(sample(fire, 0.14, 4, name="fire", mode=0))

    def call(t):
        u = _sm(t / 0.25) * (1 - _sm((t - 0.85) / 0.25))
        return {"hand.L": (-70 * u, 0, 10 * u, 0.05 * u, 0.25 * u, 0.1 * u)}
    clips.append(sample(call, 1.1, 11, loop=False, name="call"))

    def raise_(t):
        u = 1 - _sm(t / 0.3)
        return {"root": (30 * u, 0, 0, 0, -0.3 * u, 0)}
    clips.append(sample(raise_, 0.3, 5, loop=False, name="raise"))
    return clips


def build():
    """-> [(Mesh, Skeleton, clips)] of Rally's models"""
    out = []
    sk = mech_skeleton()
    out.append((mech_mesh(sk), sk, mech_clips(sk)))
    sk = pilot_skeleton()
    out.append((pilot_mesh(sk), sk, pilot_clips(sk)))
    sk = fp_skeleton()
    out.append((fp_mesh(sk), sk, fp_clips(sk)))
    sk = pfp_skeleton()
    out.append((pfp_mesh(sk), sk, pfp_clips(sk)))
    return out
