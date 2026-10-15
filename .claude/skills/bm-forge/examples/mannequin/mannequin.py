"""An articulated artist's mannequin (male) with a continuous `detail` knob: the reference for how much
detail bm-forge can hold and for polygon-reduction tests (references/DETAIL.md).

detail 1 is a plain low-poly figure (~1000 triangles); the knob raises the sides of every tube and the rings
along it, and switches features on when they can be seen: ball joints (>= 1.0), separate fingers and toes
(>= 1.6), face relief, abs, pecs, ears (they appear by themselves once the sides are enough to show them).
Each part is its own loft (a mannequin is made of parts); every vertex has one bone, as the format wants
(src/bm/format.c:266-269). Built facing +z, mirrored to -z at the end (bm Studio's recipes), .L on +x.

  python mannequin.py DETAIL OUT.bm [mitten]   e.g.  python mannequin.py 1.0 /abs/path/man1.bm  (mitten: no fingers or toes; lowface: a small face below detail 1.2; noface)
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(HERE, "..", "charkit"))

from bm_core import anim as A, export, mesh as M, skelgen  # noqa: E402
from charkit import Skin, add, cross, dot, frame_ring, mul, sub, unit  # noqa: E402

FLAT = 0x08000000
SKIN, DARK, LIGHT = 0x9EA3A8, 0x6F757B, 0xB4B9BE
LIPS, EYE, PUPIL, MOUTH, BROW = 0x8A6C68, 0xE6E6E6, 0x202020, 0x3A3030, 0x7A7F85

BONES = {"bones": [
    {"name": "hips", "parent": None, "head": [0, 0.92, 0], "tail": [0, 1.08, 0]},
    {"name": "spine", "parent": "hips", "head": [0, 1.08, 0], "tail": [0, 1.50, 0]},
    {"name": "head", "parent": "spine", "connect": True, "length": 0.30, "direction": [0, 1, 0]},
    {"name": "arm.{S}", "parent": "spine", "mirror": True, "head": [0.21, 1.44, 0], "tail": [0.33, 1.15, 0.01]},
    {"name": "forearm.{S}", "parent": "arm.{S}", "mirror": True, "connect": True, "tail": [0.35, 0.90, 0.04]},
    {"name": "hand.{S}", "parent": "forearm.{S}", "mirror": True, "connect": True, "tail": [0.36, 0.72, 0.05]},
    {"name": "leg.{S}", "parent": "hips", "mirror": True, "head": [0.09, 0.92, 0], "tail": [0.095, 0.50, 0.01]},
    {"name": "shin.{S}", "parent": "leg.{S}", "mirror": True, "connect": True, "tail": [0.095, 0.09, 0]},
    {"name": "foot.{S}", "parent": "shin.{S}", "mirror": True, "connect": True, "tail": [0.095, 0.03, 0.17]}]}


def g(x, w):
    """a bump: 1 at x = 0, falling off with width w"""
    return math.exp(-(x / w) ** 2)


def angdiff(a, b):
    d = (a - b + math.pi) % (2 * math.pi) - math.pi
    return d


class Builder:
    def __init__(self, detail):
        self.d = detail
        self.s = Skin()
        self.sides = max(5, int(round(6 * detail)))
        self.rings = max(2, int(round(2 + 3 * detail)))

    def loft(self, a, b, rad, bone, col, n=None, rings=None, ref=(1, 0, 0), caps=True, bump=None):
        """a tube from a to b; rad(t) -> (ru, rv) semi-axes; bump(t, th) -> multiplier"""
        s = self.s
        n = n or self.sides
        rings = rings or self.rings
        d = unit(sub(b, a))
        u = sub(ref, mul(d, dot(ref, d)))
        if dot(u, u) < 1e-6:
            u = sub((0, 0, 1), mul(d, d[2]))
        u = unit(u)
        v = cross(u, d)
        want = (0, 0, 1) if abs(d[2]) < 0.7 else (0, 1, 0)   # theta = 90 degrees is the front (+z), or up for a tube along z
        if dot(v, want) < 0:
            v = mul(v, -1)
        ids = []
        for i in range(rings):
            t = i / (rings - 1)
            c = add(a, mul(sub(b, a), t))
            ru, rv = rad(t)
            pts = []
            for k in range(n):
                th = 2 * math.pi * k / n
                m = bump(t, th) if bump else 1.0
                pts.append(add(c, add(mul(u, ru * m * math.cos(th)), mul(v, rv * m * math.sin(th)))))
            ids.append(s.ring(pts, bone))
        for i in range(rings - 1):
            s.bridge(ids[i], ids[i + 1], col)
        if caps:
            s.cap(ids[0], col)
            s.cap(ids[-1], col)
        return ids

    def blob(self, c, rx, ry, rz, bone, col, n=None, rings=None):
        """an ellipsoid (axes along x, y, z): a muscle belly, a bone end, an eyeball"""
        n = n or max(6, self.sides // 2)
        rr = rings or max(4, self.rings // 2 + 1)
        a, b = add(c, (0, -ry, 0)), add(c, (0, ry, 0))
        self.loft(a, b, lambda t: (rx * math.sin(math.pi * min(max(t, 0.03), 0.97)),
                                   rz * math.sin(math.pi * min(max(t, 0.03), 0.97))), bone, col, n=n, rings=rr)

    def path_tube(self, pts, radii, bone, col, n=None, ref=(1, 0, 0)):
        """a tube along a polyline (a lip, a brow, a clavicle, a tendon); radii: one per point"""
        s = self.s
        n = n or max(5, self.sides // 3)
        ids = []
        for i, p in enumerate(pts):
            t = sub(pts[min(i + 1, len(pts) - 1)], pts[max(i - 1, 0)])
            ids.append(s.ring(frame_ring(p, t, radii[i], n, ref), bone))
        for i in range(len(ids) - 1):
            s.bridge(ids[i], ids[i + 1], col)
        s.cap(ids[0], col)
        s.cap(ids[-1], col)

    def ball(self, c, r, bone, col):
        """a ball joint: a short vertical loft shaped like a sphere"""
        n = max(5, self.sides // 2 * 2)
        rr = max(3, self.rings // 2 + 1)
        a, b = add(c, (0, -r, 0)), add(c, (0, r, 0))
        self.loft(a, b, lambda t: (r * math.sin(math.pi * min(max(t, 0.02), 0.98)),) * 2, bone, col, n=n, rings=rr)


def build(detail, fingers=True, lowface=False, noface=False):
    """fingers=False: mittens and one-piece feet at any detail (the hybrid: detail where it is worth it).
    lowface=True: below detail 1.2, still a face (nose, eyes, brows, lips) at a small budget.
    noface=True: no face parts at all. The face parts are loose pieces: Skin.face_range = (v0, v1, f0, f1) says where
    they are in the vertex and face lists (a model made of a reduced body and a face built apart)."""
    B = Builder(detail)
    d = detail
    s = B.s

    # --- torso: pelvis, abdomen, chest, neck, head (one bone each)
    B.loft((0, 0.92, 0), (0, 1.08, 0), lambda t: (0.17 - 0.02 * t, 0.115), "hips", SKIN,
           bump=lambda t, th: 1 + 0.06 * g(angdiff(th, -math.pi / 2), 0.9) * g(t - 0.3, 0.4))     # glutes
    B.loft((0, 1.08, 0), (0, 1.28, 0), lambda t: (0.145 + 0.015 * t, 0.095), "spine", SKIN)
    B.loft((0, 1.28, 0), (0, 1.50, 0), lambda t: (0.17 + 0.03 * math.sin(math.pi * min(t * 1.2, 1)), 0.115 - 0.01 * t),
           "spine", SKIN)
    B.loft((0, 1.49, 0), (0, 1.62, 0.005), lambda t: (0.05, 0.05), "head", SKIN, n=max(5, B.sides * 2 // 3), rings=3)

    if d >= 1.2:       # chest and belly: pecs, abs, obliques, lats, clavicles, trapezius, iliac lines
        for sx in (1, -1):
            B.blob((sx * 0.075, 1.40, 0.078), 0.076, 0.052, 0.042, "spine", SKIN, n=10, rings=7)                     # pectoral
            B.blob((sx * 0.105, 1.36, -0.07), 0.065, 0.085, 0.042, "spine", SKIN)                    # latissimus
            B.path_tube([(sx * 0.02, 1.488, 0.07), (sx * 0.10, 1.490, 0.062), (sx * 0.19, 1.468, 0.03)],
                        (0.009, 0.009, 0.010), "spine", LIGHT)                                       # clavicle
            B.path_tube([(sx * 0.03, 1.50, -0.05), (sx * 0.10, 1.488, -0.045), (sx * 0.17, 1.462, -0.02)],
                        (0.016, 0.018, 0.015), "spine", SKIN)                                        # trapezius
            B.path_tube([(sx * 0.125, 1.20, 0.05), (sx * 0.135, 1.12, 0.045), (sx * 0.125, 1.06, 0.06)],
                        (0.018, 0.020, 0.018), "spine", SKIN)                                        # oblique
            B.path_tube([(sx * 0.115, 1.045, 0.07), (sx * 0.07, 0.985, 0.095), (sx * 0.02, 0.945, 0.10)],
                        (0.008, 0.008, 0.008), "hips", DARK)                                         # iliac line
            B.blob((sx * 0.075, 0.975, -0.095), 0.07, 0.07, 0.045, "hips", SKIN)                     # glute
            for row, y in enumerate((1.255, 1.205, 1.155, 1.105)):
                B.blob((sx * 0.03, y, 0.083 - 0.003 * (row == 3)), 0.027, 0.021, 0.016, "spine", SKIN, n=6, rings=4)   # abs
        B.path_tube([(0, 1.30, 0.093), (0, 1.12, 0.09)], (0.0035, 0.0035), "spine", DARK, n=4)       # linea alba
        B.path_tube([(0, 1.489, 0.07), (0, 1.455, 0.088), (0, 1.405, 0.082)], (0.007, 0.008, 0.008), "spine", DARK, n=5)   # sternum

    def head_rad(t):
        pts = [(0.0, 0.030, 0.040), (0.08, 0.052, 0.060), (0.2, 0.068, 0.078), (0.45, 0.086, 0.100), (0.7, 0.090, 0.106),
               (0.88, 0.072, 0.088), (0.97, 0.038, 0.046), (1.0, 0.012, 0.014)]
        for (t0, a0, b0), (t1, a1, b1) in zip(pts, pts[1:]):
            if t <= t1:
                k = (t - t0) / (t1 - t0)
                return a0 + (a1 - a0) * k, b0 + (b1 - b0) * k
        return pts[-1][1], pts[-1][2]

    def head_z(x, y):
        """z of the face surface (build frame, +z ahead) at (x, y): the head loft with its bumps at that angle"""
        t = min(max((y - 1.575) / 0.225, 0.01), 0.99)
        ru, rv = head_rad(t)
        th = math.acos(min(max(x / ru, -0.999), 0.999))
        return 0.005 * t + rv * skull(t, th) * math.sin(th)

    def skull(t, th):
        front = angdiff(th, math.pi / 2)
        m = 1.0
        m += 0.05 * g(front, 0.70) * g(t - 0.64, 0.05)                        # brow ridge
        m += 0.08 * g(front, 0.28) * g(t - 0.12, 0.07)                        # chin
        m -= 0.07 * g(angdiff(th, math.radians(62)), 0.14) * g(t - 0.58, 0.05)   # eye sockets
        m -= 0.07 * g(angdiff(th, math.radians(118)), 0.14) * g(t - 0.58, 0.05)
        m += 0.05 * (g(angdiff(th, math.radians(35)), 0.25) + g(angdiff(th, math.radians(145)), 0.25)) * g(t - 0.38, 0.09)  # cheekbones
        m -= 0.04 * (g(angdiff(th, math.radians(30)), 0.20) + g(angdiff(th, math.radians(150)), 0.20)) * g(t - 0.22, 0.07)  # hollow cheeks
        return m
    B.loft((0, 1.575, 0.0), (0, 1.80, 0.005), head_rad, "head", LIGHT, n=max(B.sides + 4, 8),
           rings=max(B.rings * 2 + 4, 10), bump=skull)

    full = d >= 1.2
    fv0, ff0 = len(s.v), len(s.f)
    if (full or lowface) and not noface:       # the face: nose, eyes, brows, lips, ears; the neck's cords. All placed on the head's surface.
        def on(x, y, off=0.0):
            return (x, y, head_z(x, y) + off)
        B.path_tube([on(0, 1.716, 0.002), on(0, 1.692, 0.007), on(0, 1.668, 0.014)], (0.010, 0.0115, 0.013), "head", LIGHT)   # nose bridge
        B.blob(on(0, 1.660, 0.019), 0.0155, 0.0155, 0.0155, "head", LIGHT)                              # tip
        for sx in (1, -1):
            if full:
                B.blob(on(sx * 0.017, 1.655, 0.009), 0.009, 0.009, 0.009, "head", LIGHT, n=6, rings=4)        # nostril wing
            if full:
                B.blob(on(sx * 0.036, 1.703, 0.000), 0.0135, 0.0105, 0.0125, "head", EYE)                 # eyeball
                B.blob(on(sx * 0.036, 1.703, 0.011), 0.0062, 0.0062, 0.0035, "head", PUPIL, n=6, rings=4)   # pupil
            else:
                B.blob(on(sx * 0.036, 1.703, 0.002), 0.012, 0.010, 0.010, "head", PUPIL, n=6, rings=3)    # one dark eye
            B.path_tube([on(sx * 0.014, 1.730, 0.003), on(sx * 0.036, 1.7355, 0.003), on(sx * 0.060, 1.727, 0.002)],
                        (0.0045, 0.0055, 0.0045), "head", BROW, n=5)                                      # brow
            if full:
                B.path_tube([on(sx * 0.022, 1.7185, 0.004), on(sx * 0.036, 1.7215, 0.0055), on(sx * 0.051, 1.7155, 0.003)],
                            (0.0028, 0.0032, 0.0028), "head", DARK, n=4)                                  # upper lid
                B.blob((sx * 0.094, 1.690, -0.002), 0.0085, 0.030, 0.021, "head", SKIN)                   # ear
                B.path_tube([(sx * 0.04, 1.572, -0.045), (sx * 0.03, 1.535, 0.0), (sx * 0.016, 1.49, 0.045)],
                            (0.010, 0.0115, 0.010), "spine", SKIN, n=6)                                   # sternocleidomastoid
        B.path_tube([on(-0.032, 1.6255, 0.004), on(-0.016, 1.630, 0.007), on(0.0, 1.632, 0.008), on(0.016, 1.630, 0.007),
                     on(0.032, 1.6255, 0.004)], (0.0058, 0.0070, 0.0078, 0.0070, 0.0058), "head", LIPS, n=6)   # upper lip
        B.path_tube([on(-0.026, 1.6135, 0.003), on(0.0, 1.6095, 0.006), on(0.026, 1.6135, 0.003)],
                    (0.0060, 0.0085, 0.0060), "head", LIPS, n=6)                                          # lower lip
        if full:
            B.path_tube([on(-0.035, 1.6195, 0.002), on(0.0, 1.6195, 0.005), on(0.035, 1.6195, 0.002)], (0.0016, 0.0016, 0.0016),
                        "head", MOUTH, n=4)                                                               # mouth line
            B.blob((0, 1.548, 0.047), 0.0095, 0.0125, 0.0095, "head", SKIN, n=6, rings=4)                # Adam's apple

    s.face_range = (fv0, len(s.v), ff0, len(s.f))

    # --- arms and legs, both sides
    for sx, S in ((1, "L"), (-1, "R")):
        arm, fore, hand = f"arm.{S}", f"forearm.{S}", f"hand.{S}"
        leg, shin, foot = f"leg.{S}", f"shin.{S}", f"foot.{S}"
        sh, el, wr = (sx * 0.21, 1.44, 0.0), (sx * 0.33, 1.15, 0.012), (sx * 0.35, 0.90, 0.04)
        if d >= 1.0:
            B.blob((sx * 0.222, 1.435, 0.0), 0.052, 0.062, 0.052, arm, SKIN)                 # deltoid
        def biceps(t, th):
            return 1 + 0.14 * g(angdiff(th, math.pi / 2), 0.7) * g(t - 0.4, 0.3) + 0.08 * g(angdiff(th, -math.pi / 2), 0.7) * g(t - 0.45, 0.3)
        B.loft(sh, el, lambda t: (0.048 - 0.01 * t, 0.045 - 0.008 * t), arm, SKIN, bump=biceps)
        if d >= 1.0:
            B.blob(el, 0.040, 0.040, 0.040, fore, DARK)                                       # elbow
        def fore_bump(t, th):
            return 1 + 0.16 * g(t - 0.2, 0.25) + 0.05 * g(angdiff(th, math.pi / 2), 0.8) * g(t - 0.3, 0.3)
        B.loft(el, wr, lambda t: (0.038 - 0.012 * t, 0.034 - 0.010 * t), fore, SKIN, bump=fore_bump)
        if d >= 1.4:
            B.blob(wr, 0.0245, 0.016, 0.0185, fore, DARK, n=6, rings=4)                       # wrist
        palm_end = (sx * 0.355, 0.82, 0.048)
        B.loft(wr, palm_end, lambda t: (0.026 + 0.012 * math.sin(math.pi * t), 0.013 + 0.005 * t), hand, SKIN,
               n=max(5, B.sides // 2), rings=3)
        if d >= 1.6 and fingers:
            for i, off in enumerate((-0.022, -0.007, 0.007, 0.022)):
                lens = (0.026, 0.021, 0.017) if i not in (1, 2) else (0.03, 0.024, 0.019)
                p = (palm_end[0] + sx * off, palm_end[1], palm_end[2] + 0.006)
                for li, ln in enumerate(lens):
                    q = (p[0], p[1] - ln * math.cos(0.35 * li), p[2] + ln * math.sin(0.5 * li) * 0.8)
                    B.loft(p, q, lambda t, li=li: (0.0085 - 0.0012 * li,) * 2, hand, SKIN, n=max(4, B.sides // 3),
                           rings=2, ref=(1, 0, 0))
                    if li < 2:
                        B.blob(q, 0.0072 - 0.001 * li, 0.0072 - 0.001 * li, 0.0072 - 0.001 * li, hand, DARK, n=5, rings=3)
                    p = q
            p = (palm_end[0] + sx * 0.03, 0.85, 0.05)                                         # thumb
            for li, ln in enumerate((0.026, 0.02)):
                q = (p[0] + sx * 0.012, p[1] - ln * 0.85, p[2] + 0.012)
                B.loft(p, q, lambda t, li=li: (0.0095 - 0.0012 * li,) * 2, hand, SKIN, n=max(4, B.sides // 3), rings=2)
                p = q
        else:
            B.loft(palm_end, (sx * 0.357, 0.745, 0.05), lambda t: (0.034 - 0.02 * t, 0.014 - 0.006 * t), hand, SKIN,
                   n=max(5, B.sides // 2), rings=2)

        hip, kn, an = (sx * 0.09, 0.92, 0.0), (sx * 0.095, 0.50, 0.012), (sx * 0.095, 0.09, 0.0)
        def quad(t, th):
            return 1 + 0.12 * g(angdiff(th, math.pi / 2), 0.7) * g(t - 0.35, 0.3) + 0.06 * g(angdiff(th, -math.pi / 2), 0.7) * g(t - 0.4, 0.3)
        B.loft(hip, kn, lambda t: (0.078 - 0.026 * t, 0.075 - 0.024 * t), leg, SKIN, bump=quad)
        if d >= 1.0:
            B.blob(kn, 0.050, 0.050, 0.050, shin, DARK)                                       # knee
        if d >= 1.4:
            B.blob((sx * 0.095, 0.50, 0.052), 0.026, 0.030, 0.014, shin, LIGHT, n=7, rings=4)  # patella
        def calf(t, th):
            return 1 + 0.18 * g(angdiff(th, -math.pi / 2), 0.7) * g(t - 0.25, 0.25) + 0.05 * g(angdiff(th, math.pi / 2), 0.6) * g(t - 0.3, 0.3)
        B.loft(kn, an, lambda t: (0.047 - 0.017 * t, 0.045 - 0.016 * t), shin, SKIN, bump=calf)
        if d >= 1.4:
            for off in (-0.02, 0.02):
                B.blob((sx * 0.095 + off, 0.385, -0.044), 0.021, 0.07, 0.026, shin, SKIN, n=6, rings=5)   # gastrocnemius heads
        if d >= 1.0:
            B.blob(an, 0.032, 0.032, 0.032, foot, DARK)                                       # ankle
        if d >= 1.4:
            for off in (-0.040, 0.036):
                B.blob((sx * 0.095 + off, 0.075, 0.0), 0.009, 0.01, 0.009, foot, LIGHT, n=5, rings=3)    # malleoli
        heel, toe = (sx * 0.095, 0.045, -0.05), (sx * 0.095, 0.022, 0.14)
        B.loft(heel, toe, lambda t: (0.038 - 0.006 * t, 0.034 - 0.014 * t), foot, SKIN, ref=(1, 0, 0),
               n=max(5, B.sides // 2), rings=max(3, B.rings // 2 + 1))
        if d >= 1.6 and fingers:
            for i, off in enumerate((-0.022, -0.011, 0.0, 0.011, 0.021)):
                ln = 0.032 - 0.004 * abs(i - 1)
                a0 = (toe[0] + sx * off, 0.022, toe[2] - 0.005)
                B.loft(a0, (a0[0], 0.016, a0[2] + ln), lambda t: (0.0085 - 0.0015 * t,) * 2, foot, SKIN,
                       n=max(4, B.sides // 3), rings=2)
        else:
            B.loft((toe[0], 0.022, toe[2] - 0.01), (toe[0], 0.016, toe[2] + 0.03), lambda t: (0.036 - 0.01 * t, 0.014),
                   foot, SKIN, n=max(5, B.sides // 2), rings=2)

    # --- face -z, bind
    s.v = [(x, y, -z) for x, y, z in s.v]
    s.f = [(a, c, b, col) for a, b, c, col in s.f]
    bones = []
    for b in BONES["bones"]:
        for side, sx in (("L", 1), ("R", -1)) if b.get("mirror") else ((None, 1),):
            nm = b["name"].replace("{S}", side) if side else b["name"]
            dd = {"name": nm, "parent": (b["parent"].replace("{S}", side) if side else b["parent"]) if b["parent"] else None}
            for key in ("head", "tail"):
                if key in b:
                    x, y, z = b[key]
                    dd[key] = [x * sx, y, -z]
            for key in ("connect", "length", "direction"):
                if key in b:
                    dd[key] = b[key]
            bones.append(dd)
    rig = skelgen.skeleton({"model": "mannequin", "bones": bones})
    idx = {b.name: i for i, b in enumerate(rig.bones)}
    model = M.Model("mannequin", [list(v) for v in s.v], [M.Face(a, b, c, col | FLAT) for a, b, c, col in s.f], 0)
    big = len(model.verts) > 4096 or len(model.faces) > 16384          # over the tools' limits: only the kernel's
    rig = A.Rig(rig.model, [A.Bone(b.name, b.parent, list(b.head), list(b.tail)) for b in rig.bones],
                [idx[b] for b in s.bone], [])
    A.validate_rig(rig, kernel_limits=big)
    return model, rig, s


if __name__ == "__main__":
    det = float(sys.argv[1])
    model, rig, _ = build(det, fingers="mitten" not in sys.argv[3:], lowface="lowface" in sys.argv[3:], noface="noface" in sys.argv[3:])
    print(f"detail {det}: {len(model.verts)} verts, {len(model.faces)} tris, {len(rig.bones)} bones", file=sys.stderr)
    if len(sys.argv) > 2:
        big = len(model.verts) > 4096 or len(model.faces) > 16384          # over the tools' limits: the kernel's
        export.export_models(sys.argv[2], M.Mesh([model]), anim=A.Anim([rig]), title="Mannequin", kernel_limits=big)
        print("wrote", sys.argv[2])
