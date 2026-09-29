#!/usr/bin/env python3
"""Titan Clash: the VANGUARD robot, pre-rendered into pixel-art sprites.

The robot is a 3D model made of simple solids (tapered boxes with chamfered
edges, cylinders, extruded profiles) hung on a skeleton. Each animation frame
is a pose of the skeleton, rendered orthographically at 1:1 pixel size and
turned into pixel art: flat facets shaded in a few hand-picked tones per
material, dark outlines on the silhouette and wherever a part stands in front
of another, emissive visor and vents.

Everything that can change on the robot is its own layer, drawn over the
base in a fixed order: heavy armour plates, the near shoulder armour (intact
or cracked, for the light and the heavy armour), the arm guns, the sword
(on the back, or in the hand). A layer holds only the pixels its parts show
in front of the base, so base + layers is exactly the robot with those parts.

Needs numpy and Pillow; the outputs (sprites.png and src/05_frames.lua) are
in git, so `make` does not.

  python3 carts/titan/mkrobot.py [--preview DIR] [--only NAME]
"""
import argparse
import math
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------- materials
# tone ramps, darkest to lightest (5 tones); "glow" materials ignore light

def hexc(s):
    s = s.lstrip("#")
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


MAT = {
    # steel-teal armour plates
    "armor":  [hexc(c) for c in ("#132029", "#1f3a47", "#2f5a66", "#4f8189", "#8bbcbc", "#cfeee6")],
    # hazard orange trim
    "accent": [hexc(c) for c in ("#2e1409", "#6a2a10", "#b14c16", "#e77a24", "#ffb25a", "#ffe0a8")],
    # the dark inner frame, joints and pistons
    "frame":  [hexc(c) for c in ("#0c0e12", "#171b21", "#252b33", "#3a424c", "#5b6570", "#8a95a0")],
    # bright machined metal (weapons, pistons)
    "metal":  [hexc(c) for c in ("#181c22", "#2e343c", "#4c5560", "#76828e", "#adb9c4", "#e6eef4")],
    # cracked armour: darker, burnt
    "broken": [hexc(c) for c in ("#0f1418", "#1a262c", "#26383e", "#37525a", "#4f7070", "#6e9090")],
    # yellow warning paint
    "yellow": [hexc(c) for c in ("#2a2206", "#5c4a0c", "#a08214", "#dcb828", "#fbe468", "#fff6c0")],
}
GLOW = {
    "visor": [hexc(c) for c in ("#0c4a5a", "#1a9ab4", "#5ae6ff", "#d6ffff")],
    "vent": [hexc(c) for c in ("#5a1600", "#c24a00", "#ff9a1a", "#fff09a")],
    "blade": [hexc(c) for c in ("#1a5a8a", "#48b8f0", "#b0f4ff", "#ffffff")],
    "crack": [hexc(c) for c in ("#5a1600", "#e05000", "#ffb020", "#fff4b0")],
}
OUTLINE = hexc("#07080c")
# the second player's colours (as the alternate colours of the arcade
# fighters): crimson armour, gold trim, a green visor
ALT = {
    "armor":  [hexc(c) for c in ("#1e0a10", "#3e121c", "#6a1c28", "#9c2c34", "#d05a4e", "#ffa88e")],
    "accent": [hexc(c) for c in ("#2a1c04", "#5e420a", "#a07814", "#d8b030", "#f8dc6c", "#fff4c0")],
    "broken": [hexc(c) for c in ("#140a0c", "#261016", "#3a1820", "#56242c", "#703038", "#904848")],
    "visor":  [hexc(c) for c in ("#0c4a1e", "#1aa44a", "#6aff8e", "#e0ffe6")],
}
MATS = list(MAT) + list(GLOW)
MAT_ID = {m: i + 1 for i, m in enumerate(MATS)}

# ---------------------------------------------------------------- solids
# each returns (vertices (n,3), triangles (m,3)); faces point outwards


class Solid:
    def __init__(self, v, t, mat):
        self.v = np.asarray(v, float)
        self.t = np.asarray(t, int)
        self.mat = [mat] * len(self.t) if isinstance(mat, str) else list(mat)


def _outward(v, tris):
    """turn every triangle to face away from the solid's centre"""
    c = v.mean(axis=0)
    out = []
    for a, b, d in tris:
        n = np.cross(v[b] - v[a], v[d] - v[a])
        if n @ ((v[a] + v[b] + v[d]) / 3 - c) < 0:
            b, d = d, b
        out.append((a, b, d))
    return out


def section(cx, cz, w, d, ch):
    """a rectangle w x d around (cx, cz) with its corners cut by ch (octagon)"""
    hw, hd = w / 2, d / 2
    if ch <= 0:
        return [(cx - hw, cz - hd), (cx + hw, cz - hd), (cx + hw, cz + hd), (cx - hw, cz + hd)]
    c = min(ch, hw * 0.9, hd * 0.9)
    return [(cx - hw + c, cz - hd), (cx + hw - c, cz - hd), (cx + hw, cz - hd + c), (cx + hw, cz + hd - c),
            (cx + hw - c, cz + hd), (cx - hw + c, cz + hd), (cx - hw, cz + hd - c), (cx - hw, cz - hd + c)]


def loft(y0, y1, bot, top, mat, ch=0.0, cap_mat=None):
    """a solid between two horizontal sections: bot/top = (cx, cz, w, d)"""
    s0 = section(*bot, ch)
    s1 = section(*top, ch)
    n = len(s0)
    v = [(x, y0, z) for x, z in s0] + [(x, y1, z) for x, z in s1]
    t, mats = [], []
    for i in range(n):
        j = (i + 1) % n
        t += [(i, j, n + j), (i, n + j, n + i)]
        mats += [mat, mat]
    for i in range(1, n - 1):
        t += [(0, i, i + 1), (n, n + i, n + i + 1)]
        mats += [mat, cap_mat or mat]
    v = np.array(v, float)
    return Solid(v, _outward(v, t), mats)


def box(x0, x1, y0, y1, z0, z1, mat, ch=0.0, top=None):
    return loft(y0, y1, ((x0 + x1) / 2, (z0 + z1) / 2, x1 - x0, z1 - z0),
                ((x0 + x1) / 2, (z0 + z1) / 2, x1 - x0, z1 - z0), mat, ch, top)


def cyl(p0, p1, r, mat, n=8, r1=None):
    """a cylinder (or cone) from p0 to p1"""
    p0, p1 = np.asarray(p0, float), np.asarray(p1, float)
    ax = p1 - p0
    ax /= np.linalg.norm(ax)
    u = np.cross(ax, [0, 0, 1.0]) if abs(ax[2]) < 0.9 else np.cross(ax, [1.0, 0, 0])
    u /= np.linalg.norm(u)
    w = np.cross(ax, u)
    r1 = r if r1 is None else r1
    v = []
    for k in range(n):
        a = 2 * math.pi * (k + 0.5) / n
        d = u * math.cos(a) + w * math.sin(a)
        v.append(p0 + d * r)
    for k in range(n):
        a = 2 * math.pi * (k + 0.5) / n
        d = u * math.cos(a) + w * math.sin(a)
        v.append(p1 + d * r1)
    t = []
    for i in range(n):
        j = (i + 1) % n
        t += [(i, j, n + j), (i, n + j, n + i)]
    for i in range(1, n - 1):
        t += [(0, i, i + 1), (n, n + i, n + i + 1)]
    v = np.array(v, float)
    return Solid(v, _outward(v, t), mat)


def extrude(poly, z0, z1, mat, axis="z", side_mat=None):
    """a convex polygon extruded: axis z: poly in (x, y); axis x: poly in (z, y)"""
    n = len(poly)
    v = []
    for d in (z0, z1):
        for a, b in poly:
            v.append((a, b, d) if axis == "z" else (d, b, a))
    t, mats = [], []
    for i in range(n):
        j = (i + 1) % n
        t += [(i, j, n + j), (i, n + j, n + i)]
        mats += [side_mat or mat] * 2
    for i in range(1, n - 1):
        t += [(0, i, i + 1), (n, n + i, n + i + 1)]
        mats += [mat, mat]
    v = np.array(v, float)
    return Solid(v, _outward(v, t), mats)


def mirror_z(s):
    v = s.v.copy()
    v[:, 2] *= -1
    return Solid(v, [(a, c, b) for a, b, c in s.t], s.mat)

# ---------------------------------------------------------------- the robot
# Units: the robot stands about 10 tall, facing +x; +z is towards the viewer
# (the "near" side, n), -z the far side (f). Bones hang their parts from the
# joint at their origin; limbs point down (-y) in the rest pose.

BONES = {
    # name: (parent, joint position in the parent's frame)
    "pelvis": (None, (0.0, 5.1, 0.0)),
    "waist": ("pelvis", (0.0, 0.45, 0.0)),
    "chest": ("waist", (0.0, 0.62, 0.0)),
    "head": ("chest", (0.3, 2.15, 0.0)),
}
for s_, z_ in (("n", 1.0), ("f", -1.0)):
    BONES.update({
        "arm_" + s_: ("chest", (0.0, 1.7, 1.95 * z_)),
        "fore_" + s_: ("arm_" + s_, (0.0, -1.6, 0.0)),
        "hand_" + s_: ("fore_" + s_, (0.0, -1.75, 0.0)),
        "thigh_" + s_: ("pelvis", (0.0, -0.3, 0.85 * z_)),
        "shin_" + s_: ("thigh_" + s_, (0.0, -2.2, 0.0)),
        "foot_" + s_: ("shin_" + s_, (0.0, -2.1, 0.0)),
    })

PARTS = []      # (bone, layer, Solid)


def part(bone, layer, *solids):
    for s in solids:
        PARTS.append((bone, layer, s))


def limb_side(s):
    """the same geometry for both sides: near parts face +z, far ones -z"""
    return (lambda x: x) if s == "n" else mirror_z


def moved(s, R=None, t=(0, 0, 0)):
    """a solid turned by R and moved by t"""
    v = s.v @ (R.T if R is not None else np.eye(3)) + np.asarray(t, float)
    return Solid(v, s.t, s.mat)


def zs(side, *solids):
    """near-side solids, mirrored for the far side"""
    f = limb_side(side)
    return [f(x) for x in solids]


def build_robot():
    PARTS.clear()
    # --- pelvis: core, crotch plate, split front skirt, back skirt, hip axle
    part("pelvis", "base",
         box(-0.6, 0.6, -0.55, 0.35, -0.8, 0.8, "frame", 0.15),
         loft(-0.7, 0.3, (0.45, 0, 0.45, 0.6), (0.5, 0, 0.75, 1.0), "armor", 0.12),
         box(0.78, 0.9, -0.45, -0.05, -0.18, 0.18, "yellow", 0.04),
         extrude([(0.5, 0.3), (0.98, 0.18), (0.88, -0.62), (0.52, -0.5)], 0.22, 0.78, "armor"),
         extrude([(0.5, 0.3), (0.98, 0.18), (0.88, -0.62), (0.52, -0.5)], -0.78, -0.22, "armor"),
         extrude([(0.9, 0.1), (0.97, 0.08), (0.9, -0.45), (0.84, -0.4)], 0.3, 0.7, "accent"),
         extrude([(-0.5, 0.3), (-1.0, 0.1), (-0.9, -0.6), (-0.45, -0.5)], -0.7, 0.7, "armor"),
         cyl((0, -0.3, -1.05), (0, -0.3, 1.05), 0.34, "frame", 8))
    # --- waist: the frame, two pistons and a cable bundle
    part("waist", "base",
         loft(-0.1, 0.75, (0, 0, 0.95, 1.1), (0, 0, 1.2, 1.5), "frame", 0.22),
         cyl((0.45, 0.0, 0.45), (0.45, 0.72, 0.5), 0.1, "metal", 6),
         cyl((0.45, 0.0, -0.45), (0.45, 0.72, -0.5), 0.1, "metal", 6),
         cyl((-0.5, -0.05, 0.0), (-0.45, 0.75, 0.0), 0.16, "frame", 6))
    # --- chest: abdomen plates, the big armoured chest with a chevron, vents,
    #     collar, and the booster pack with its stacks and nozzles
    part("chest", "base",
         loft(0.0, 0.85, (0.0, 0, 1.45, 2.0), (0.1, 0, 2.0, 2.8), "armor", 0.35),
         loft(0.85, 1.9, (0.1, 0, 2.0, 2.8), (0.0, 0, 1.85, 3.2), "armor", 0.5),
         loft(1.9, 2.2, (0.0, 0, 1.85, 3.0), (-0.1, 0, 1.25, 1.9), "armor", 0.35),
         box(0.62, 0.84, 0.02, 0.28, -0.55, 0.55, "armor", 0.06),               # abdomen segments
         box(0.7, 0.93, 0.32, 0.6, -0.62, 0.62, "armor", 0.06),
         extrude([(0.2, 1.85), (1.3, 1.55), (1.2, 0.98), (0.2, 1.2)], 1.02, 1.12, "accent", axis="x"),   # chevron
         extrude([(-0.2, 1.85), (-0.2, 1.2), (-1.2, 0.98), (-1.3, 1.55)], 1.02, 1.12, "accent", axis="x"),
         box(1.0, 1.16, 1.0, 1.45, -0.15, 0.15, "vent"),                          # chest intake
         box(-0.7, 0.7, 1.0, 1.75, 1.5, 1.62, "armor", 0.1),                        # flank panels
         box(-0.7, 0.7, 1.0, 1.75, -1.62, -1.5, "armor", 0.1),
         box(0.98, 1.12, 0.62, 0.92, -0.95, -0.35, "frame", 0.04),               # side grills
         box(0.98, 1.12, 0.62, 0.92, 0.35, 0.95, "frame", 0.04),
         loft(2.05, 2.4, (0.2, 0, 0.9, 1.1), (0.25, 0, 0.75, 0.9), "frame", 0.15),   # collar
         box(-1.75, -0.9, 0.55, 2.05, -1.05, 1.05, "frame", 0.22),               # booster pack
         box(-1.85, -1.1, 1.45, 2.45, -0.95, -0.3, "armor", 0.14),              # its stacks
         box(-1.85, -1.1, 1.45, 2.45, 0.3, 0.95, "armor", 0.14),
         box(-1.9, -1.8, 1.6, 2.3, 0.4, 0.85, "accent", 0.03),
         cyl((-1.35, 0.6, -0.6), (-1.5, 0.05, -0.65), 0.3, "metal", 8, 0.4),     # nozzles
         cyl((-1.35, 0.6, 0.6), (-1.5, 0.05, 0.65), 0.3, "metal", 8, 0.4),
         box(-1.55, -1.35, -0.02, 0.1, 0.45, 0.85, "vent"))
    # --- head: helmet, visor, face mask with slits, crest, sensor ears
    part("head", "base",
         loft(0.0, 0.32, (0.0, 0, 0.6, 0.65), (0.05, 0, 0.62, 0.72), "frame", 0.1),
         loft(0.28, 0.98, (0.12, 0, 1.05, 0.98), (0.02, 0, 1.0, 1.02), "armor", 0.24),
         loft(0.98, 1.22, (0.0, 0, 0.95, 0.98), (-0.08, 0, 0.62, 0.66), "armor", 0.2),
         box(0.5, 0.68, 0.58, 0.76, -0.44, 0.44, "visor"),
         box(0.5, 0.66, 0.3, 0.52, -0.28, 0.28, "frame", 0.05),
         box(0.64, 0.68, 0.33, 0.48, -0.2, -0.14, "armor"),
         box(0.64, 0.68, 0.33, 0.48, -0.03, 0.03, "armor"),
         box(0.64, 0.68, 0.33, 0.48, 0.14, 0.2, "armor"),
         extrude([(0.35, 0.98), (0.62, 1.1), (-0.35, 1.72), (-0.42, 1.52)], -0.07, 0.07, "accent"),
         extrude([(-0.3, 1.58), (-0.36, 1.72), (-0.62, 1.88), (-0.58, 1.72)], -0.05, 0.05, "yellow"),
         box(0.45, 0.72, 0.76, 0.86, -0.5, 0.5, "armor", 0.05),                    # brow
         box(0.3, 0.62, 0.25, 0.58, 0.44, 0.56, "armor", 0.06),                     # cheek guards
         box(0.3, 0.62, 0.25, 0.58, -0.56, -0.44, "armor", 0.06),
         cyl((0.0, 0.85, 0.55), (-0.75, 1.35, 0.72), 0.05, "metal", 4),            # antennae
         cyl((0.0, 0.85, -0.55), (-0.75, 1.35, -0.72), 0.05, "metal", 4),
         cyl((0.0, 0.62, 0.48), (0.0, 0.62, 0.68), 0.18, "frame", 8),
         cyl((0.0, 0.62, -0.48), (0.0, 0.62, -0.68), 0.18, "frame", 8),
         box(0.1, 0.22, 0.55, 0.7, 0.66, 0.72, "visor"),
         box(0.1, 0.22, 0.55, 0.7, -0.72, -0.66, "visor"))
    for s in ("n", "f"):
        # shoulder ball, upper arm (frame and sleeve), elbow
        part("arm_" + s, "base", *zs(s,
             cyl((0, 0.0, -0.3), (0, 0.0, 0.35), 0.5, "frame", 8),
             loft(-1.45, -0.2, (0, 0, 0.6, 0.62), (0, 0, 0.7, 0.72), "frame", 0.16),
             loft(-1.15, -0.35, (0.02, 0.06, 0.8, 0.78), (0.02, 0.06, 0.92, 0.88), "armor", 0.22),
             box(-0.42, 0.48, -0.95, -0.55, 0.42, 0.5, "frame", 0.04),
             box(-0.3, 0.36, -0.9, -0.62, 0.49, 0.53, "armor", 0.03)))
        # the gauntlet: a big armoured forearm with a band and a vent
        part("fore_" + s, "base", *zs(s,
             cyl((0, 0, -0.38), (0, 0, 0.38), 0.34, "frame", 8),
             loft(-1.65, -0.18, (0.1, 0, 0.95, 0.95), (0.0, 0, 1.05, 1.02), "armor", 0.28),
             box(-0.52, 0.72, -1.5, -1.28, -0.52, 0.52, "accent", 0.06),
             box(-0.4, 0.4, -1.0, -0.55, 0.5, 0.57, "vent"),
             box(-0.36, 0.36, -1.72, -1.55, -0.4, 0.4, "frame", 0.05)))
        # the fist: knuckle plate and thumb
        part("hand_" + s, "base", *zs(s,
             loft(-0.8, -0.05, (0.08, 0, 0.72, 0.62), (0.02, 0, 0.62, 0.58), "frame", 0.14),
             box(0.2, 0.46, -0.72, -0.2, -0.32, 0.32, "metal", 0.07),
             box(0.2, 0.48, -0.66, -0.52, -0.3, 0.3, "frame"),
             box(-0.1, 0.3, -0.45, -0.1, 0.3, 0.46, "metal", 0.05)))
        # thigh: axle, frame, front armour, side plate
        part("thigh_" + s, "base", *zs(s,
             cyl((0, 0, -0.35), (0, 0, 0.35), 0.44, "frame", 8),
             loft(-2.15, -0.1, (0.05, 0, 0.72, 0.78), (0, 0, 0.85, 0.9), "frame", 0.2),
             loft(-1.8, -0.35, (0.22, 0.04, 0.62, 0.9), (0.18, 0.04, 0.78, 1.0), "armor", 0.2),
             box(-0.35, 0.3, -1.4, -0.55, 0.46, 0.54, "armor", 0.05),
             box(0.55, 0.62, -1.5, -0.7, -0.25, 0.25, "armor", 0.04)))
        # shin: knee axle, the heavy flared armour, knee guard, vents, calf nozzle
        part("shin_" + s, "base", *zs(s,
             cyl((0, 0, -0.42), (0, 0, 0.42), 0.42, "frame", 8),
             loft(-2.05, -0.2, (0.02, 0, 1.3, 1.2), (0.1, 0, 1.0, 1.02), "armor", 0.32),
             extrude([(0.3, 0.42), (0.82, 0.1), (0.78, -0.72), (0.42, -0.9)], -0.4, 0.4, "accent"),
             box(0.55, 0.7, -1.55, -1.0, -0.32, 0.32, "vent"),
             box(0.62, 0.72, -1.62, -0.92, -0.38, 0.38, "frame", 0.02),
             box(-0.25, 0.45, -1.45, -0.45, 0.56, 0.66, "armor", 0.08),
             box(-0.12, 0.3, -1.32, -1.18, 0.64, 0.68, "frame"),
             cyl((-0.62, -1.2, 0.0), (-0.78, -1.75, 0.0), 0.2, "metal", 6, 0.26)))
        # foot: ankle, the sole, toe cap, heel
        part("foot_" + s, "base", *zs(s,
             cyl((0, 0, -0.38), (0, 0, 0.38), 0.3, "frame", 8),
             extrude([(-0.8, -0.2), (0.35, -0.15), (1.3, -0.45), (1.25, -0.72), (-0.85, -0.72)],
                     -0.55, 0.55, "armor"),
             extrude([(0.4, -0.18), (0.95, -0.3), (0.85, -0.55), (0.3, -0.45)], -0.35, 0.35, "accent"),
             box(-0.95, -0.55, -0.72, -0.3, -0.45, 0.45, "frame", 0.08),
             box(-0.85, 1.3, -0.8, -0.7, -0.5, 0.5, "frame")))

    # --- heavy armour: thick breastplate, flank plates, side skirts,
    #     thigh and shin plates, forearm plates, a brow plate
    part("chest", "heavy",
         loft(0.9, 1.95, (0.12, 0, 2.25, 3.05), (0.02, 0, 2.1, 3.4), "armor", 0.55),
         extrude([(0.25, 1.95), (1.45, 1.65), (1.35, 1.05), (0.25, 1.25)], 1.14, 1.26, "accent", axis="x"),
         extrude([(-0.25, 1.95), (-0.25, 1.25), (-1.35, 1.05), (-1.45, 1.65)], 1.14, 1.26, "accent", axis="x"),
         box(1.12, 1.3, 1.05, 1.5, -0.16, 0.16, "vent"),
         box(-0.6, 0.6, 0.2, 1.0, 1.35, 1.55, "armor", 0.12),
         box(-0.6, 0.6, 0.2, 1.0, -1.55, -1.35, "armor", 0.12))
    part("head", "heavy",
         loft(0.9, 1.05, (0.25, 0, 0.75, 1.12), (0.25, 0, 0.85, 1.12), "armor", 0.12))
    for s in ("n", "f"):
        part("pelvis", "heavy", *zs(s,
             extrude([(0.6, 0.35), (-0.6, 0.35), (-0.75, -1.05), (0.75, -1.05)], 1.0, 1.18, "armor", axis="z"),
             box(-0.3, 0.3, -0.9, -0.65, 1.14, 1.22, "accent", 0.03)))
        part("thigh_" + s, "heavy", *zs(s,
             loft(-1.75, -0.3, (0.08, 0.1, 1.0, 1.0), (0.05, 0.1, 1.1, 1.1), "armor", 0.26)))
        part("shin_" + s, "heavy", *zs(s,
             loft(-2.0, -0.55, (0.1, 0, 1.52, 1.38), (0.2, 0, 1.18, 1.2), "armor", 0.36),
             box(0.72, 0.84, -1.7, -1.1, -0.3, 0.3, "vent")))
        part("fore_" + s, "heavy", *zs(s,
             loft(-1.35, -0.3, (0.12, 0.05, 1.2, 1.15), (0.08, 0.05, 1.25, 1.2), "armor", 0.3),
             box(-0.58, 0.8, -1.2, -1.0, -0.6, 0.6, "accent", 0.06)))

    # --- shoulder armour: light (a compact cap) and heavy (a big pauldron).
    #     The near one can break: its own layer; the far one goes with the rest.
    def cap():
        return [loft(-0.35, 0.55, (0.0, 0.25, 1.35, 0.9), (-0.05, 0.1, 1.1, 0.75), "armor", 0.28),
                box(-0.45, 0.45, 0.05, 0.3, 0.68, 0.76, "accent", 0.04)]

    def pauldron():
        return [loft(-0.75, 0.62, (0.0, 0.35, 1.9, 1.3), (-0.08, 0.15, 1.55, 1.0), "armor", 0.38),
                loft(0.62, 0.85, (-0.08, 0.15, 1.55, 1.0), (-0.1, 0.05, 1.1, 0.7), "armor", 0.28),
                box(-0.7, 0.72, -0.35, 0.0, 0.98, 1.06, "accent", 0.05),
                box(-0.35, 0.35, 0.2, 0.45, 0.94, 1.0, "yellow", 0.03)]
    def cracked(solids, cracks):
        out = []
        for x in solids:
            out.append(Solid(x.v, x.t, ["broken" if m in ("armor", "accent", "yellow") else m for m in x.mat]))
        for (x0, y0), (x1, y1), z in cracks:
            d = np.array([x1 - x0, y1 - y0])
            n = np.array([-d[1], d[0]]) / (np.linalg.norm(d) + 1e-9) * 0.06
            out.append(extrude([(x0 - n[0], y0 - n[1]), (x1 - n[0], y1 - n[1]), (x1 + n[0], y1 + n[1]),
                                (x0 + n[0], y0 + n[1])], z - 0.03, z + 0.03, "crack"))
        return out
    part("arm_n", "shoulder", *cap())
    part("arm_n", "shoulder_d", *cracked(cap(), [((-0.4, 0.4), (0.1, 0.05), 0.72), ((0.1, 0.05), (0.45, -0.2), 0.72)]))
    part("arm_n", "shoulder_h", *pauldron())
    part("arm_n", "shoulder_hd", *cracked(pauldron(), [((-0.7, 0.5), (-0.1, 0.1), 1.0), ((-0.1, 0.1), (0.2, -0.5), 1.0),
                                                      ((-0.1, 0.1), (0.6, 0.35), 1.0)]))
    part("arm_f", "base", *[mirror_z(x) for x in cap()])
    part("arm_f", "heavy", *[mirror_z(x) for x in pauldron()])

    # --- guns: a twin-barrel pod on each gauntlet, barrels past the fist
    for s in ("n", "f"):
        part("fore_" + s, "guns", *zs(s,
             box(-0.35, 0.45, -1.35, -0.25, 0.55, 1.05, "metal", 0.12),
             box(-0.3, 0.35, -1.0, -0.45, 1.02, 1.12, "frame", 0.04),
             cyl((0.2, -1.2, 0.72), (0.2, -2.75, 0.72), 0.13, "frame", 6),
             cyl((0.2, -1.2, 0.92), (0.2, -2.75, 0.92), 0.13, "frame", 6),
             cyl((0.2, -2.45, 0.82), (0.2, -2.65, 0.82), 0.28, "metal", 8),
             box(-0.45, -0.1, -0.9, -0.4, 0.62, 0.98, "yellow", 0.04)))

    # --- the sword: a long blade with an energy edge; on the back, or in the
    #     near hand (grip across the fist, the blade forward)
    def sword():
        blade = extrude([(0.45, -0.36), (5.4, -0.36), (6.1, 0.02), (5.4, 0.46), (0.45, 0.46)], -0.1, 0.1, "metal")
        edge = extrude([(0.45, -0.44), (5.4, -0.44), (6.0, -0.02), (5.4, -0.32), (0.45, -0.34)], -0.06, 0.06, "blade")
        fuller = extrude([(0.9, -0.02), (4.6, -0.02), (4.6, 0.12), (0.9, 0.12)], -0.13, 0.13, "frame")
        guard = box(0.25, 0.5, -0.65, 0.72, -0.3, 0.3, "accent", 0.08)
        grip = cyl((-0.8, 0.04, 0), (0.25, 0.04, 0), 0.16, "frame", 6)
        pommel = cyl((-1.05, 0.04, 0), (-0.8, 0.04, 0), 0.24, "metal", 6)
        return [blade, edge, fuller, guard, grip, pommel]
    back = rot(0, 0, -128)
    part("chest", "sword_back", *[moved(x, back, (-1.45, 2.55, -1.3)) for x in sword()])
    inhand = rot(0, 0, 0)
    part("hand_n", "sword_hand", *[moved(x, inhand, (0.25, -0.42, 0.0)) for x in sword()])


# ---------------------------------------------------------------- posing

def rot(rx, ry, rz):
    """degrees; applied x, then y, then z"""
    a, b, c = math.radians(rx), math.radians(ry), math.radians(rz)
    Rx = np.array([[1, 0, 0], [0, math.cos(a), -math.sin(a)], [0, math.sin(a), math.cos(a)]])
    Ry = np.array([[math.cos(b), 0, math.sin(b)], [0, 1, 0], [-math.sin(b), 0, math.cos(b)]])
    Rz = np.array([[math.cos(c), -math.sin(c), 0], [math.sin(c), math.cos(c), 0], [0, 0, 1]])
    return Rz @ Ry @ Rx


def pose_bones(pose):
    """world matrices (R, t) of every bone; pose[bone] = rz or (rz, rx, ry);
    pose['root'] = (dx, dy)"""
    out = {}
    root = pose.get("root", (0, 0))

    def get(name):
        if name in out:
            return out[name]
        parent, j = BONES[name]
        p = pose.get(name, 0)
        rz, rx, ry = (p, 0, 0) if not isinstance(p, tuple) else (p + (0, 0))[:3]
        R = rot(rx, ry, rz)
        if parent is None:
            t = np.array(j, float) + np.array([root[0], root[1], 0.0])
            out[name] = (R, t)
        else:
            PR, pt = get(parent)
            out[name] = (PR @ R, pt + PR @ np.array(j, float))
        return out[name]
    for b in BONES:
        get(b)
    return out

# ---------------------------------------------------------------- rendering

SCALE = 18.0                        # pixels per unit
VIEW = rot(-9, -52, 0)            # 3/4 view: turned to the viewer, seen a little from above
LIGHT = np.array([-0.45, 0.72, 0.55])
LIGHT /= np.linalg.norm(LIGHT)
CANVAS = 440                        # render box (pixels); the robot's feet at the centre bottom
FLOOR_Y = 340                       # canvas row of y = 0


def render(pose, layers, start=None):
    """rasterise the parts on the given layers (over the buffers `start`, if
    given); returns buffers: depth, normal (camera space), material id,
    layer index, solid index"""
    bones = pose_bones(pose)
    W = H = CANVAS
    if start is not None:
        depth, normal, matid, layer, solid = (b.copy() for b in start)
    else:
        depth = np.full((H, W), -1e9)
        normal = np.zeros((H, W, 3))
        matid = np.zeros((H, W), int)
        layer = np.full((H, W), -1, int)
        solid = np.full((H, W), -1, int)
    for si, (bone, lay, s) in enumerate(PARTS):
        if lay not in layers:
            continue
        R, t = bones[bone]
        v = (s.v @ R.T + t) @ VIEW.T
        px = W / 2 + v[:, 0] * SCALE
        py = FLOOR_Y - v[:, 1] * SCALE
        pz = v[:, 2]
        for ti, (a, b, c) in enumerate(s.t):
            xs = np.array([px[a], px[b], px[c]])
            ys = np.array([py[a], py[b], py[c]])
            area = (xs[1] - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (ys[1] - ys[0])
            if area >= 0:           # back face (screen y down)
                continue
            n = np.cross(v[b] - v[a], v[c] - v[a])
            n /= np.linalg.norm(n) + 1e-12
            x0, x1 = max(0, int(xs.min())), min(W - 1, int(xs.max()) + 1)
            y0, y1 = max(0, int(ys.min())), min(H - 1, int(ys.max()) + 1)
            if x1 < x0 or y1 < y0:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            w0 = ((xs[1] - gx) * (ys[2] - gy) - (xs[2] - gx) * (ys[1] - gy)) / area
            w1 = ((xs[2] - gx) * (ys[0] - gy) - (xs[0] - gx) * (ys[2] - gy)) / area
            w2 = 1 - w0 - w1
            m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not m.any():
                continue
            zz = w0 * pz[a] + w1 * pz[b] + w2 * pz[c]
            sub = depth[y0:y1 + 1, x0:x1 + 1]
            m &= zz > sub
            if not m.any():
                continue
            sub[m] = zz[m]
            normal[y0:y1 + 1, x0:x1 + 1][m] = n
            matid[y0:y1 + 1, x0:x1 + 1][m] = MAT_ID[s.mat[ti]]
            layer[y0:y1 + 1, x0:x1 + 1][m] = LAYERS.index(lay)
            solid[y0:y1 + 1, x0:x1 + 1][m] = si
    return depth, normal, matid, layer, solid


LAYERS = ["base", "heavy", "shoulder", "shoulder_d", "shoulder_h", "shoulder_hd", "guns", "sword_back",
          "sword_hand"]


def stylise(depth, normal, matid, layer, solid):
    """pixel art: tones, outlines; returns RGBA and the layer of every pixel"""
    H, W = matid.shape
    img = np.zeros((H, W, 4), np.uint8)
    lay = layer.copy()
    inside = matid > 0
    # tones
    d = normal @ LIGHT
    dark = np.zeros((H, W, 3), np.uint8)      # each pixel's line colour
    for name, i in MAT_ID.items():
        m = matid == i
        if not m.any():
            continue
        if name in MAT:
            ramp = MAT[name]
            # 6 tones: deep shadow .. highlight
            k = np.digitize(d[m], [-0.3, 0.05, 0.35, 0.62, 0.86])
            # faces turned to the screen edge catch a rim light
            rim = (normal[m][:, 2] < 0.3) & (normal[m][:, 1] > -0.1) & (k < 3)
            k = np.where(rim, k + 1, k)
            cols = np.array(ramp)[k]
            dark[m] = ramp[0]
        else:
            ramp = GLOW[name]
            k = np.digitize(d[m], [-0.2, 0.3, 0.8])
            cols = np.array(ramp)[np.clip(k + 1, 0, 3)]
            dark[m] = ramp[0]
        img[m, :3] = cols
        img[m, 3] = 255
    # lines: a pixel next to another solid clearly in front of it takes the
    # line colour (black on the silhouette, the deepest tone of the covered
    # part inside) and the front part's layer
    out = img.copy()
    olay = lay.copy()
    for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
        nd = np.roll(np.roll(depth, dy, 0), dx, 1)
        nin = np.roll(np.roll(inside, dy, 0), dx, 1)
        nl = np.roll(np.roll(lay, dy, 0), dx, 1)
        ns = np.roll(np.roll(solid, dy, 0), dx, 1)
        edge = nin & ~inside
        over = nin & inside & (ns != solid) & (nd > depth + 0.12)
        out[edge, :3] = OUTLINE
        out[edge, 3] = 255
        olay[edge] = nl[edge]
        out[over, :3] = np.where((nd[over] > depth[over] + 0.9)[:, None], OUTLINE, dark[over])
        olay[over] = nl[over]
    return out, olay


def recolor(img):
    """the same picture in the second player's colours"""
    out = img.copy()
    key = (img[:, :, 0].astype(np.int32) << 16) | (img[:, :, 1].astype(np.int32) << 8) | img[:, :, 2]
    for name, ramp in ALT.items():
        src = MAT[name] if name in MAT else GLOW[name]
        for a, b in zip(src, ramp):
            m = (key == ((a[0] << 16) | (a[1] << 8) | a[2])) & (img[:, :, 3] > 0)
            out[m, :3] = b[:3]
    return out


def frame_image(pose, layers=("base",)):
    return stylise(*render(pose, layers))

# ---------------------------------------------------------------- poses

def ground(pose):
    """the pose moved up or down so its lowest point touches y = 0"""
    bones = pose_bones(pose)
    low = 1e9
    for bone, lay, sol in PARTS:
        if lay != "base":
            continue
        R, t = bones[bone]
        low = min(low, (sol.v @ R.T + t)[:, 1].min())
    p = dict(pose)
    dx, dy = pose.get("root", (0, 0))
    p["root"] = (dx, dy - low)
    return p


def P(base, **kw):
    """a pose: `base` with some bones changed"""
    p = dict(base)
    p.update(kw)
    return p


GUARD = {
    "root": (0.0, 0.0),
    "pelvis": (-6, 0, 14),
    "waist": 4, "chest": (-4, 0, -18), "head": -8,
    "arm_n": (62, -10), "fore_n": 88, "hand_n": -15,
    "arm_f": (70, 6), "fore_f": 105, "hand_f": -10,
    "thigh_n": 30, "shin_n": -38, "foot_n": 8,
    "thigh_f": -22, "shin_f": -24, "foot_f": 40,
}
CROUCH = P(GUARD, pelvis=(-8, 0, 18), chest=(-10, 0, -16), head=-4,
           thigh_n=78, shin_n=-118, foot_n=38, thigh_f=18, shin_f=-122, foot_f=100,
           arm_n=(55, -10), fore_n=95, arm_f=(62, 6), fore_f=110)


def walk(i):
    ph = i / 6 * 2 * math.pi
    tn = 6 + 26 * math.sin(ph)
    tf = 6 + 26 * math.sin(ph + math.pi)
    sn = -12 - 34 * max(0.0, math.sin(ph + math.pi / 2))
    sf = -12 - 34 * max(0.0, math.sin(ph + 3 * math.pi / 2))
    return P(GUARD, pelvis=(-4, 0, 8 + 3 * math.sin(2 * ph)), thigh_n=tn, shin_n=sn, foot_n=-(tn + sn) * 0.8,
             thigh_f=tf, shin_f=sf, foot_f=-(tf + sf) * 0.8,
             arm_n=(58 - 8 * math.sin(ph), -10), arm_f=(66 + 8 * math.sin(ph), 6))


# Animations: name -> list of (pose, active part or None). The active part is
# what hits (a fist, a foot, the blade): its box is the frame's hitbox.
def animations():
    A = {}
    A["idle"] = [(P(GUARD, root=(0, -0.06 * k), chest=(-4, 0, -18 + 2 * k), arm_n=(62 - 2 * k, -10),
                    arm_f=(70 - 2 * k, 6)), None) for k in (0, 1, 2, 1)]
    A["walk"] = [(walk(i), None) for i in range(6)]
    A["crouch"] = [(CROUCH, None)]
    A["jump"] = [(P(GUARD, thigh_n=62, shin_n=-105, foot_n=30, thigh_f=40, shin_f=-100, foot_f=45,
                    arm_n=(95, -20), fore_n=70, arm_f=(100, 10), fore_f=80, chest=(-4, 0, -8)), None),
                 (P(GUARD, thigh_n=85, shin_n=-125, foot_n=40, thigh_f=70, shin_f=-130, foot_f=50,
                    chest=(-4, 0, -20), head=0), None),
                 (P(GUARD, thigh_n=30, shin_n=-30, foot_n=-10, thigh_f=-5, shin_f=-40, foot_f=20,
                    arm_n=(80, -25), fore_n=60, arm_f=(40, 10), fore_f=70), None)]
    A["dash"] = [(P(GUARD, pelvis=(-28, 0, -8), chest=(-4, 0, -26), head=22,
                    arm_n=(-25, -15), fore_n=40, arm_f=(-35, 10), fore_f=45,
                    thigh_n=45, shin_n=-70, foot_n=10, thigh_f=-40, shin_f=-20, foot_f=40), None),
                 (P(GUARD, pelvis=(-32, 0, -10), chest=(-4, 0, -28), head=24,
                    arm_n=(-30, -15), fore_n=35, arm_f=(-40, 10), fore_f=40,
                    thigh_n=50, shin_n=-80, foot_n=15, thigh_f=-45, shin_f=-25, foot_f=45), None)]
    A["block"] = [(P(GUARD, pelvis=(-4, 0, 4), chest=(-6, 0, -2), head=-12,
                     arm_n=(78, -30), fore_n=120, hand_n=0, arm_f=(88, 25), fore_f=115), None)]
    A["cblock"] = [(P(CROUCH, chest=(-6, 0, -6), head=-10, arm_n=(78, -30), fore_n=122, arm_f=(88, 25),
                      fore_f=118), None)]
    # punches and kicks
    A["lp"] = [(P(GUARD, arm_n=(40, -10), fore_n=110), None),
               (P(GUARD, chest=(-4, -4, -24), arm_n=(92, -4), fore_n=4, hand_n=0), "hand_n"),
               (P(GUARD, arm_n=(55, -10), fore_n=90), None)]
    A["hp"] = [(P(GUARD, chest=(-4, 18, -12), arm_f=(15, 10), fore_f=125), None),
               (P(GUARD, pelvis=(-6, -10, 8), chest=(-4, -30, -26), head=0, arm_f=(92, 2), fore_f=2, hand_f=0,
                  arm_n=(35, -20), fore_n=100, thigh_n=38, shin_n=-30, thigh_f=-30, shin_f=-8, foot_f=40),
                "hand_f"),
               (P(GUARD, pelvis=(-6, -12, 8), chest=(-4, -34, -28), head=0, arm_f=(95, 2), fore_f=0, hand_f=0,
                  arm_n=(30, -20), fore_n=100, thigh_n=40, shin_n=-30, thigh_f=-32, shin_f=-8, foot_f=40),
                "hand_f"),
               (P(GUARD, chest=(-4, -8, -18), arm_f=(60, 6), fore_f=95), None)]
    A["lk"] = [(P(GUARD, thigh_n=82, shin_n=-105, foot_n=20, thigh_f=-8, shin_f=-14, chest=(-4, 0, -6)), None),
               (P(GUARD, thigh_n=82, shin_n=-6, foot_n=-40, thigh_f=-10, shin_f=-8, chest=(-4, 0, 2),
                  pelvis=(-6, 0, 20)), "foot_n"),
               (P(GUARD, thigh_n=70, shin_n=-95, foot_n=15, thigh_f=-8, shin_f=-14)), None]
    A["lk"] = [A["lk"][0], A["lk"][1], (A["lk"][2], None)]
    A["hk"] = [(P(GUARD, pelvis=(-6, 25, 10), chest=(-4, 20, -10), thigh_f=70, shin_f=-120, foot_f=30,
                  thigh_n=10, shin_n=-20, foot_n=10), None),
               (P(GUARD, pelvis=(-6, 30, 38), chest=(-4, 10, -22), head=-20, thigh_f=108, shin_f=-4, foot_f=-50,
                  thigh_n=-10, shin_n=-10, foot_n=20, arm_n=(40, -30), fore_n=80, arm_f=(20, 20), fore_f=90),
                "foot_f"),
               (P(GUARD, pelvis=(-6, 32, 40), chest=(-4, 10, -24), head=-22, thigh_f=112, shin_f=-2, foot_f=-50,
                  thigh_n=-12, shin_n=-10, foot_n=22, arm_n=(40, -30), fore_n=80, arm_f=(20, 20), fore_f=90),
                "foot_f"),
               (P(GUARD, pelvis=(-6, 10, 18), thigh_f=60, shin_f=-100, foot_f=30, thigh_n=15, shin_n=-25), None)]
    # crouching attacks
    A["clp"] = [(P(CROUCH, arm_n=(40, -10), fore_n=110), None),
                (P(CROUCH, chest=(-4, -4, -22), arm_n=(92, -4), fore_n=4, hand_n=0), "hand_n")]
    A["chp"] = [(P(CROUCH, arm_f=(10, 10), fore_f=130, chest=(-4, 10, -24)), None),
                (P(CROUCH, thigh_n=45, shin_n=-70, thigh_f=5, shin_f=-60, foot_f=50, chest=(-4, -20, 2),
                   arm_f=(150, 5), fore_f=40, hand_f=0, head=10), "hand_f"),
                (P(GUARD, thigh_n=20, shin_n=-15, thigh_f=-10, shin_f=-10, chest=(-4, -25, 12), head=15,
                   arm_f=(172, 5), fore_f=15, hand_f=0), "hand_f")]
    A["clk"] = [(P(CROUCH, thigh_n=75, shin_n=-40, foot_n=-20, chest=(-4, 0, -4)), "foot_n"),
                (P(CROUCH, thigh_n=78, shin_n=-20, foot_n=-40, chest=(-4, 0, 0)), "foot_n")]
    A["chk"] = [(P(CROUCH, pelvis=(-8, 20, 10), thigh_f=40, shin_f=-120, chest=(-4, 10, -24)), None),
                (P(CROUCH, pelvis=(-8, 30, 25), chest=(-4, 10, -30), thigh_n=100, shin_n=-150, foot_n=50,
                   thigh_f=88, shin_f=-2, foot_f=-60, arm_n=(20, -30), fore_n=60, arm_f=(10, 30), fore_f=60),
                 "foot_f"),
                (P(CROUCH, pelvis=(-8, 15, 18), thigh_f=60, shin_f=-100, chest=(-4, 5, -20)), None)]
    # in the air
    A["jhk"] = [(P(GUARD, thigh_n=75, shin_n=-120, thigh_f=70, shin_f=-125, foot_f=30, chest=(-4, 0, -12)), None),
                (P(GUARD, thigh_n=55, shin_n=-2, foot_n=-50, thigh_f=70, shin_f=-120, foot_f=30,
                   chest=(-4, 0, 6), pelvis=(-6, 0, 28), arm_n=(20, -30), fore_n=80), "foot_n")]
    A["jhp"] = [(P(GUARD, thigh_n=80, shin_n=-120, thigh_f=60, shin_f=-110, arm_f=(10, 10), fore_f=130), None),
                (P(GUARD, thigh_n=80, shin_n=-120, thigh_f=60, shin_f=-110, chest=(-4, -20, -30),
                   arm_f=(55, 0), fore_f=0, hand_f=0), "hand_f")]
    # getting hit, falling, getting up, winning
    A["hit"] = [(P(GUARD, pelvis=(-6, 0, 2), chest=(-4, 0, 12), head=20, arm_n=(25, -30), fore_n=60,
                   arm_f=(35, 20), fore_f=70), None),
                (P(GUARD, pelvis=(-6, 0, -4), chest=(-4, 0, 22), head=28, arm_n=(10, -40), fore_n=40,
                   arm_f=(20, 25), fore_f=55, thigh_n=15, shin_n=-20), None)]
    A["chit"] = [(P(CROUCH, chest=(-4, 0, 10), head=18, arm_n=(30, -30), fore_n=60), None)]
    A["down"] = [(P(GUARD, pelvis=(50, 0, 10), chest=(-4, 0, 10), head=20, arm_n=(-40, -40), fore_n=30,
                    arm_f=(-30, 30), fore_f=40, thigh_n=60, shin_n=-40, thigh_f=40, shin_f=-30), None),
                 (P(GUARD, pelvis=(86, 0, 6), chest=(-4, 0, 4), head=10, arm_n=(-80, -20), fore_n=20,
                    arm_f=(-70, 20), fore_f=25, thigh_n=10, shin_n=-15, foot_n=20, thigh_f=20, shin_f=-40,
                    foot_f=20), None),
                 (P(GUARD, pelvis=(-6, 0, 10), chest=(-4, 0, -20), thigh_n=95, shin_n=-100, foot_n=5,
                    thigh_f=-10, shin_f=-135, foot_f=60, arm_n=(40, -20), fore_n=60, arm_f=(10, 20), fore_f=40),
                  None)]
    A["win"] = [(P(GUARD, pelvis=(-4, 0, 0), chest=(-4, 0, 4), head=12, arm_n=(172, -10), fore_n=18, hand_n=0,
                   arm_f=(10, 25), fore_f=60, thigh_n=12, shin_n=-8, thigh_f=-10, shin_f=-6, foot_f=15), None),
                (P(GUARD, pelvis=(-4, 0, 0), chest=(-4, 0, 6), head=16, arm_n=(178, -8), fore_n=8, hand_n=0,
                   arm_f=(10, 25), fore_f=60, thigh_n=12, shin_n=-8, thigh_f=-10, shin_f=-6, foot_f=15), None)]
    # weapons: the sword slash (the sword goes from the back to the hand) and
    # the guns' burst
    def blade(pose, want):
        """the hand angle that points the blade at `want` degrees on the
        screen (0: forward, 90: up), found by trying them all"""
        best, bd = (0, 0), 1e9
        PR, _ = pose_bones(P(pose, hand_n=0))["fore_n"]
        for hy in range(-90, 91, 10):
            for h in range(-180, 180, 4):
                R = PR @ rot(0, hy, h)
                d = VIEW @ (R @ np.array([1.0, 0, 0]))
                ang = math.degrees(math.atan2(d[1], d[0]))
                err = abs((ang - want + 180) % 360 - 180) + 120 * (1 - math.hypot(d[0], d[1]))
                if err < bd:
                    best, bd = (h, 0, hy), err
        return P(pose, hand_n=best)
    A["slash"] = [(P(GUARD, chest=(-4, 10, -10), arm_n=(165, 25), fore_n=70, hand_n=0), None),
                  (blade(P(GUARD, chest=(-4, 5, 5), head=5, arm_n=(172, -10), fore_n=35), 125), None),
                  (blade(P(GUARD, pelvis=(-6, -10, 8), chest=(-4, -15, -20), arm_n=(115, -10), fore_n=10,
                           thigh_n=40, shin_n=-40, thigh_f=-30, shin_f=-10), 20), "sword_hand"),
                  (blade(P(GUARD, pelvis=(-6, -15, 12), chest=(-4, -25, -32), arm_n=(58, -10), fore_n=5,
                           thigh_n=45, shin_n=-45, thigh_f=-32, shin_f=-8), -30), "sword_hand"),
                  (blade(P(GUARD, chest=(-4, -5, -20), arm_n=(40, -10), fore_n=60), -50), None)]
    A["fire"] = [(P(GUARD, pelvis=(-6, 0, 6), chest=(-4, -10, -8), head=-4, arm_n=(90, -8), fore_n=2, hand_n=0,
                    arm_f=(88, 8), fore_f=2, hand_f=0, thigh_n=28, shin_n=-24, thigh_f=-24, shin_f=-14), None),
                 (P(GUARD, pelvis=(-6, 0, 4), chest=(-4, -10, -4), head=-2, arm_n=(95, -8), fore_n=6, hand_n=0,
                    arm_f=(93, 8), fore_f=6, hand_f=0, thigh_n=28, shin_n=-24, thigh_f=-24, shin_f=-14), None),
                 (P(GUARD, pelvis=(-6, 0, 2), chest=(-4, -10, -2), head=0, arm_n=(99, -8), fore_n=9, hand_n=0,
                    arm_f=(97, 8), fore_f=9, hand_f=0, thigh_n=28, shin_n=-24, thigh_f=-24, shin_f=-14), None)]
    return A


WEAPON_ANIMS = {"slash": "sword", "fire": "guns"}


def layer_image(bufs_base, pose, lay):
    """the pixels layer `lay` shows over the base (RGBA, transparent elsewhere)"""
    img, lmap = stylise(*render(pose, (lay,), bufs_base))
    out = np.zeros_like(img)
    mine = lmap == LAYERS.index(lay)
    out[mine] = img[mine]
    return out


def boxes(pose, parts_bones):
    """screen box (x0, y0, x1, y1 around the anchor) of the solids of these bones"""
    bones = pose_bones(pose)
    pts = []
    for bone, lay, sol in PARTS:
        if bone in parts_bones and lay in ("base", "sword_hand"):
            if lay == "sword_hand" and "sword_hand" not in parts_bones:
                continue
            R, t = bones[bone]
            v = (sol.v @ R.T + t) @ VIEW.T
            pts.append(v)
    if not pts:
        return None
    v = np.concatenate(pts)
    xs, ys = v[:, 0] * SCALE, -v[:, 1] * SCALE
    return (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)


def frames():
    """every frame of every animation: base image, layer images, boxes"""
    out = []
    anims = animations()
    for name, seq in anims.items():
        for k, (pose, active) in enumerate(seq):
            pose = ground(pose)
            base = render(pose, ("base",))
            f = {"anim": name, "k": k, "layers": {}}
            f["layers"]["base"] = stylise(*base)[0]
            weapon = WEAPON_ANIMS.get(name)
            in_hand = name == "slash" and k >= 1
            for lay in ("heavy", "shoulder", "shoulder_d", "shoulder_h", "shoulder_hd", "guns"):
                f["layers"][lay] = layer_image(base, pose, lay)
            f["layers"]["sword"] = layer_image(base, pose, "sword_hand" if in_hand else "sword_back")
            f["sword_front"] = in_hand
            # on the ground nothing goes below the street (the sword on the
            # back of a robot lying down)
            if name not in ("jump", "jhp", "jhk") and not (name == "down" and k == 0):
                for img in f["layers"].values():
                    img[FLOOR_Y + 2:] = 0
            # hurt boxes: upper body and lower body; the hit box: the active part
            f["hurt"] = [boxes(pose, {"chest", "head", "waist", "arm_n", "arm_f"}),
                         boxes(pose, {"pelvis", "thigh_n", "thigh_f", "shin_n", "shin_f", "foot_n", "foot_f"})]
            if active == "hand_n":
                f["hit"] = boxes(pose, {"hand_n"})
            elif active == "hand_f":
                f["hit"] = boxes(pose, {"hand_f"})
            elif active == "foot_n":
                f["hit"] = boxes(pose, {"foot_n", "shin_n"})
            elif active == "foot_f":
                f["hit"] = boxes(pose, {"foot_f", "shin_f"})
            elif active == "sword_hand":
                f["hit"] = boxes(pose, {"hand_n", "sword_hand"})
            else:
                f["hit"] = None
            f["weapon"] = weapon
            out.append(f)
    return out


def contact_sheet(fr, config, path):
    """all frames in one picture, with this configuration drawn the game's way"""
    cols = 10
    cw, ch = 200, 230
    rows = (len(fr) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * cw, rows * ch), (58, 76, 96, 255))
    for i, f in enumerate(fr):
        L = f["layers"]
        img = L["base"].copy()
        order = [lay for lay in config if lay != "sword"]
        if "sword" in config and not f["sword_front"]:
            order = ["sword"] + order
        if "sword" in config and f["sword_front"]:
            order = order + ["sword"]
        for lay in order:
            m = L[lay][:, :, 3] > 0
            img[m] = L[lay][m]
        im = Image.fromarray(img, "RGBA").crop((CANVAS // 2 - 100, FLOOR_Y - 215, CANVAS // 2 + 100, FLOOR_Y + 15))
        sheet.alpha_composite(im, ((i % cols) * cw, (i // cols) * ch))
    sheet.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preview", default=None)
    a = ap.parse_args()
    build_robot()
    outdir = a.preview or os.path.join(HERE, "preview")
    os.makedirs(outdir, exist_ok=True)
    fr = frames()
    contact_sheet(fr, ["shoulder", "guns"], os.path.join(outdir, "light_guns.png"))
    contact_sheet(fr, ["heavy", "shoulder_h", "sword"], os.path.join(outdir, "heavy_sword.png"))


if __name__ == "__main__":
    main()
