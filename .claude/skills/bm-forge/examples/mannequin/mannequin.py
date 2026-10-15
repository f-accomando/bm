"""An articulated artist's mannequin (male) with a continuous `detail` knob: the reference for how much
detail bm-forge can hold and for polygon-reduction tests (references/DETAIL.md).

detail 1 is a plain low-poly figure (~1000 triangles); the knob raises the sides of every tube and the rings
along it, and switches features on when they can be seen: ball joints (>= 1.0), separate fingers and toes
(>= 1.6), face relief, abs, pecs, ears (they appear by themselves once the sides are enough to show them).
Each part is its own loft (a mannequin is made of parts); every vertex has one bone, as the format wants
(src/bm/format.c:266-269). Built facing +z, mirrored to -z at the end (bm Studio's recipes), .L on +x.

  python mannequin.py DETAIL OUT.bm [mitten]   e.g.  python mannequin.py 1.0 /abs/path/man1.bm  (mitten: no fingers or toes)
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(HERE, "..", "charkit"))

from bm_core import anim as A, export, mesh as M, skelgen  # noqa: E402
from charkit import Skin, add, cross, dot, mul, sub, unit  # noqa: E402

FLAT = 0x08000000
SKIN, DARK, LIGHT = 0x9EA3A8, 0x6F757B, 0xB4B9BE

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
        v = cross(d, u)
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

    def ball(self, c, r, bone, col):
        """a ball joint: a short vertical loft shaped like a sphere"""
        n = max(5, self.sides // 2 * 2)
        rr = max(3, self.rings // 2 + 1)
        a, b = add(c, (0, -r, 0)), add(c, (0, r, 0))
        self.loft(a, b, lambda t: (r * math.sin(math.pi * min(max(t, 0.02), 0.98)),) * 2, bone, col, n=n, rings=rr)


def build(detail, fingers=True):
    """fingers=False: mittens and one-piece feet at any detail (the hybrid: detail where it is worth it)"""
    B = Builder(detail)
    d = detail
    s = B.s

    # --- torso: pelvis, abdomen, chest, neck, head (one bone each)
    B.loft((0, 0.92, 0), (0, 1.08, 0), lambda t: (0.17 - 0.02 * t, 0.115), "hips", SKIN,
           bump=lambda t, th: 1 + 0.06 * g(angdiff(th, -math.pi / 2), 0.9) * g(t - 0.3, 0.4))     # glutes
    def abs_(t, th):
        front = max(0.0, math.sin(th))
        return 1 + 0.06 * front * (0.5 + 0.5 * math.cos(4 * th)) * g(t - 0.5, 0.5)
    B.loft((0, 1.08, 0), (0, 1.28, 0), lambda t: (0.145 + 0.015 * t, 0.095), "spine", SKIN, bump=abs_)

    def pecs(t, th):
        lobe = g(angdiff(th, math.radians(62)), 0.28) + g(angdiff(th, math.radians(118)), 0.28)
        return 1 + 0.11 * lobe * g(t - 0.45, 0.3) + 0.05 * g(angdiff(th, -math.pi / 2), 0.9) * g(t - 0.6, 0.4)
    B.loft((0, 1.28, 0), (0, 1.50, 0), lambda t: (0.17 + 0.03 * math.sin(math.pi * min(t * 1.2, 1)), 0.115 - 0.01 * t),
           "spine", SKIN, bump=pecs)
    B.loft((0, 1.49, 0), (0, 1.62, 0.005), lambda t: (0.05, 0.05), "head", SKIN, n=max(5, B.sides * 2 // 3), rings=3)

    def face(t, th):
        front = angdiff(th, math.pi / 2)
        m = 1.0
        m += 0.22 * g(front, 0.20) * g(t - 0.46, 0.09)                        # nose
        m += 0.07 * g(front, 0.70) * g(t - 0.64, 0.05)                        # brow ridge
        m += 0.09 * g(front, 0.28) * g(t - 0.12, 0.07)                        # chin
        m += 0.05 * g(front, 0.45) * g(t - 0.30, 0.05)                        # lips
        m -= 0.09 * g(angdiff(th, math.radians(62)), 0.14) * g(t - 0.58, 0.05)   # eye sockets
        m -= 0.09 * g(angdiff(th, math.radians(118)), 0.14) * g(t - 0.58, 0.05)
        m += 0.16 * (g(angdiff(th, 0), 0.20) + g(angdiff(th, math.pi), 0.20)) * g(t - 0.5, 0.11)   # ears
        return m

    def head_rad(t):
        pts = [(0.0, 0.030, 0.040), (0.08, 0.052, 0.060), (0.2, 0.068, 0.078), (0.45, 0.086, 0.100), (0.7, 0.090, 0.106),
               (0.88, 0.072, 0.088), (0.97, 0.038, 0.046), (1.0, 0.012, 0.014)]
        for (t0, a0, b0), (t1, a1, b1) in zip(pts, pts[1:]):
            if t <= t1:
                k = (t - t0) / (t1 - t0)
                return a0 + (a1 - a0) * k, b0 + (b1 - b0) * k
        return pts[-1][1], pts[-1][2]
    B.loft((0, 1.575, 0.0), (0, 1.80, 0.005), head_rad, "head", LIGHT, n=max(B.sides, 6), rings=max(B.rings * 2 + 2, 8),
           bump=face)

    # --- arms and legs, both sides
    for sx, S in ((1, "L"), (-1, "R")):
        arm, fore, hand = f"arm.{S}", f"forearm.{S}", f"hand.{S}"
        leg, shin, foot = f"leg.{S}", f"shin.{S}", f"foot.{S}"
        sh, el, wr = (sx * 0.21, 1.44, 0.0), (sx * 0.33, 1.15, 0.012), (sx * 0.35, 0.90, 0.04)
        if d >= 1.0:
            B.ball(sh, 0.055, arm, DARK)                                                    # shoulder
        def biceps(t, th):
            return 1 + 0.12 * g(angdiff(th, math.pi / 2), 0.7) * g(t - 0.4, 0.3)
        B.loft(sh, el, lambda t: (0.048 - 0.01 * t, 0.045 - 0.008 * t), arm, SKIN, bump=biceps)
        if d >= 1.0:
            B.ball(el, 0.04, fore, DARK)                                                    # elbow
        def fore_bump(t, th):
            return 1 + 0.12 * g(t - 0.2, 0.25)
        B.loft(el, wr, lambda t: (0.038 - 0.012 * t, 0.034 - 0.01 * t), fore, SKIN, bump=fore_bump)
        # hand: palm, then fingers (or one mitten when the detail is low)
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
                    p = q
            p = (palm_end[0] + sx * 0.03, 0.85, 0.05)                                       # thumb
            for li, ln in enumerate((0.026, 0.02)):
                q = (p[0] + sx * 0.012, p[1] - ln * 0.85, p[2] + 0.012)
                B.loft(p, q, lambda t, li=li: (0.0095 - 0.0012 * li,) * 2, hand, SKIN, n=max(4, B.sides // 3), rings=2)
                p = q
        else:
            B.loft(palm_end, (sx * 0.357, 0.745, 0.05), lambda t: (0.034 - 0.02 * t, 0.014 - 0.006 * t), hand, SKIN,
                   n=max(5, B.sides // 2), rings=2)

        hip, kn, an = (sx * 0.09, 0.92, 0.0), (sx * 0.095, 0.50, 0.012), (sx * 0.095, 0.09, 0.0)
        def quad(t, th):
            return 1 + 0.10 * g(angdiff(th, math.pi / 2), 0.7) * g(t - 0.35, 0.3)
        B.loft(hip, kn, lambda t: (0.078 - 0.026 * t, 0.075 - 0.024 * t), leg, SKIN, bump=quad)
        if d >= 1.0:
            B.ball(kn, 0.05, shin, DARK)                                                    # knee
        def calf(t, th):
            return 1 + 0.16 * g(angdiff(th, -math.pi / 2), 0.7) * g(t - 0.25, 0.25)
        B.loft(kn, an, lambda t: (0.047 - 0.017 * t, 0.045 - 0.016 * t), shin, SKIN, bump=calf)
        if d >= 1.0:
            B.ball(an, 0.032, foot, DARK)                                                   # ankle
        # foot: a loft along z, flat; toes when the detail allows
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
    model, rig, _ = build(det, fingers="mitten" not in sys.argv[3:])
    print(f"detail {det}: {len(model.verts)} verts, {len(model.faces)} tris, {len(rig.bones)} bones", file=sys.stderr)
    if len(sys.argv) > 2:
        big = len(model.verts) > 4096 or len(model.faces) > 16384          # over the tools' limits: the kernel's
        export.export_models(sys.argv[2], M.Mesh([model]), anim=A.Anim([rig]), title="Mannequin", kernel_limits=big)
        print("wrote", sys.argv[2])
