"""A plain artist's mannequin (male): base shapes only, with a continuous `detail` knob. The reference for how much
detail bm-forge can hold and for polygon-reduction tests (references/DETAIL.md).

No face, no muscle relief, no ball joints: only the natural forms (egg head, neck, a torso that narrows at the waist,
tapering limbs with a gentle calf and deltoid, mitten hands with a thumb, simple feet), each a loft of elliptical rings
whose radii follow a smooth curve through a few anatomical stations. `detail` raises the sides of every ring and the
rings along each part (sides = 6 * detail, rings along a part grow with its length). Every part is its own loft and
every vertex has one bone, as the format wants (src/bm/format.c:266-269). Built facing +z, mirrored to -z at the end
(bm Studio's recipes), .L on +x. Angle 90 degrees of a ring is the front of the part.

  python mannequin.py DETAIL OUT.bm            e.g.  python mannequin.py 1.0 /abs/path/man1.bm
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
SKIN, LIGHT = 0x9EA3A8, 0xB4B9BE

BONES = {"bones": [
    {"name": "hips", "parent": None, "head": [0, 0.92, 0], "tail": [0, 1.08, 0]},
    {"name": "spine", "parent": "hips", "head": [0, 1.08, 0], "tail": [0, 1.50, 0]},
    {"name": "head", "parent": "spine", "connect": True, "length": 0.30, "direction": [0, 1, 0]},
    {"name": "arm.{S}", "parent": "spine", "mirror": True, "head": [0.21, 1.44, 0], "tail": [0.325, 1.16, 0.012]},
    {"name": "forearm.{S}", "parent": "arm.{S}", "mirror": True, "connect": True, "tail": [0.35, 0.91, 0.04]},
    {"name": "hand.{S}", "parent": "forearm.{S}", "mirror": True, "connect": True, "tail": [0.358, 0.72, 0.058]},
    {"name": "leg.{S}", "parent": "hips", "mirror": True, "head": [0.09, 0.92, 0], "tail": [0.095, 0.50, 0.012]},
    {"name": "shin.{S}", "parent": "leg.{S}", "mirror": True, "connect": True, "tail": [0.095, 0.09, 0]},
    {"name": "foot.{S}", "parent": "shin.{S}", "mirror": True, "connect": True, "tail": [0.095, 0.03, 0.17]}]}


def curve(stations):
    """stations [(t, ru, rv)] -> f(t) -> (ru, rv): a cubic through them with no overshoot (monotone Hermite)"""
    n = len(stations)
    ts = [s[0] for s in stations]

    def slopes(k):
        ys = [s[k] for s in stations]
        d = [(ys[i + 1] - ys[i]) / (ts[i + 1] - ts[i]) for i in range(n - 1)]
        m = [d[0]] + [0.0 if d[i - 1] * d[i] <= 0 else (d[i - 1] + d[i]) / 2 for i in range(1, n - 1)] + [d[-1]]
        return ys, m

    data = [slopes(1), slopes(2)]

    def f(t):
        t = min(max(t, ts[0]), ts[-1])
        i = max(j for j in range(n - 1) if ts[j] <= t) if t < ts[-1] else n - 2
        h = ts[i + 1] - ts[i]
        u = (t - ts[i]) / h
        out = []
        for ys, m in data:
            h00, h10, h01, h11 = 2 * u**3 - 3 * u**2 + 1, u**3 - 2 * u**2 + u, -2 * u**3 + 3 * u**2, u**3 - u**2
            out.append(h00 * ys[i] + h10 * h * m[i] + h01 * ys[i + 1] + h11 * h * m[i + 1])
        return out[0], out[1]
    return f


def g(x, w):
    return math.exp(-(x / w) ** 2)


def angdiff(a, b):
    return (a - b + math.pi) % (2 * math.pi) - math.pi


class Builder:
    def __init__(self, detail):
        self.d = detail
        self.s = Skin()
        self.sides = max(5, int(round(6 * detail)))

    def rings_for(self, length):
        return max(3, int(round((1.6 + 2.6 * self.d) * (length / 0.3) ** 0.55)))

    def loft(self, a, b, rad, bone, col, bump=None, sides=None, rings=None, ref=(1, 0, 0)):
        """a tube from a to b; rad(t) -> (ru, rv) semi-axes (ru: the side, rv: front-back); bump(t, th) -> factor"""
        s = self.s
        n = sides or self.sides
        length = math.dist(a, b)
        rings = rings or self.rings_for(length)
        d = unit(sub(b, a))
        u = sub(ref, mul(d, dot(ref, d)))
        if dot(u, u) < 1e-6:
            u = sub((0, 0, 1), mul(d, d[2]))
        u = unit(u)
        v = cross(u, d)
        want = (0, 0, 1) if abs(d[2]) < 0.7 else (0, 1, 0)       # angle 90 degrees is the front (+z), or up along z
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
        s.cap(ids[0], col)
        s.cap(ids[-1], col)


def build(detail):
    B = Builder(detail)
    s = B.s

    # --- torso: pelvis and abdomen (hips), ribcage and shoulders (spine); one curve, split at y = 1.08
    torso = curve([(0.92, 0.150, 0.105), (0.98, 0.168, 0.113), (1.04, 0.166, 0.113), (1.10, 0.142, 0.100),
                   (1.17, 0.130, 0.092), (1.26, 0.146, 0.098), (1.36, 0.174, 0.110), (1.43, 0.192, 0.108),
                   (1.48, 0.170, 0.094), (1.52, 0.085, 0.066)])

    def part(y0, y1):
        return lambda t: torso(y0 + (y1 - y0) * t)
    B.loft((0, 0.92, 0), (0, 1.08, 0), part(0.92, 1.08), "hips", SKIN,
           bump=lambda t, th: 1 + 0.05 * g(angdiff(th, -math.pi / 2), 0.9) * g(t - 0.35, 0.4))        # a little seat
    B.loft((0, 1.08, 0), (0, 1.52, 0.004), part(1.08, 1.52), "spine", SKIN)

    # --- neck and head: an egg with a jaw
    B.loft((0, 1.50, 0), (0, 1.60, 0.006), curve([(0, 0.056, 0.058), (1, 0.050, 0.052)]), "head", SKIN)
    B.loft((0, 1.575, 0.0), (0, 1.80, 0.005),
           curve([(0.0, 0.032, 0.042), (0.08, 0.054, 0.064), (0.2, 0.070, 0.082), (0.45, 0.086, 0.100), (0.7, 0.090, 0.106),
                  (0.88, 0.072, 0.088), (0.97, 0.038, 0.046), (1.0, 0.012, 0.014)]), "head", LIGHT,
           sides=max(B.sides, 8), rings=max(B.rings_for(0.225) + 2, 8))

    # --- arms and legs, both sides
    upper = curve([(0.0, 0.052, 0.050), (0.12, 0.060, 0.058), (0.45, 0.048, 0.046), (0.8, 0.040, 0.040), (1.0, 0.037, 0.037)])
    fore = curve([(0.0, 0.037, 0.036), (0.2, 0.040, 0.038), (0.6, 0.032, 0.030), (1.0, 0.025, 0.020)])
    palm = curve([(0.0, 0.025, 0.018), (0.15, 0.034, 0.017), (0.55, 0.036, 0.016), (0.85, 0.028, 0.013), (1.0, 0.012, 0.008)])
    thigh = curve([(0.0, 0.082, 0.082), (0.15, 0.084, 0.080), (0.5, 0.068, 0.066), (1.0, 0.050, 0.050)])
    calf = curve([(0.0, 0.050, 0.050), (0.18, 0.052, 0.050), (0.55, 0.040, 0.038), (1.0, 0.030, 0.030)])
    foot = curve([(0.0, 0.030, 0.033), (0.25, 0.040, 0.034), (0.65, 0.046, 0.026), (1.0, 0.030, 0.012)])
    for sx, S in ((1, "L"), (-1, "R")):
        arm, fore_b, hand = f"arm.{S}", f"forearm.{S}", f"hand.{S}"
        leg, shin, foot_b = f"leg.{S}", f"shin.{S}", f"foot.{S}"
        sh, el, wr, tip = (sx * 0.215, 1.45, 0.0), (sx * 0.325, 1.16, 0.012), (sx * 0.35, 0.91, 0.04), (sx * 0.358, 0.72, 0.058)
        B.loft(sh, el, upper, arm, SKIN)
        B.loft(el, wr, fore, fore_b, SKIN)
        B.loft(wr, tip, palm, hand, LIGHT)
        B.loft((sx * 0.335, 0.865, 0.052), (sx * 0.305, 0.80, 0.078), lambda t: (0.010 - 0.003 * t,) * 2, hand, LIGHT,
               sides=max(5, B.sides // 2), rings=3)                                                   # thumb
        hip, kn, an = (sx * 0.09, 0.95, 0.0), (sx * 0.095, 0.50, 0.012), (sx * 0.095, 0.09, 0.0)
        B.loft(hip, kn, thigh, leg, SKIN,
               bump=lambda t, th: 1 + 0.06 * g(angdiff(th, math.pi / 2), 0.8) * g(t - 0.35, 0.3))     # the thigh's front
        B.loft(kn, an, calf, shin, SKIN,
               bump=lambda t, th: 1 + 0.10 * g(angdiff(th, -math.pi / 2), 0.8) * g(t - 0.25, 0.25))   # the calf, behind
        B.loft((sx * 0.095, 0.050, -0.055), (sx * 0.095, 0.022, 0.165), foot, foot_b, LIGHT)

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
    model, rig, _ = build(det)
    print(f"detail {det}: {len(model.verts)} verts, {len(model.faces)} tris, {len(rig.bones)} bones", file=sys.stderr)
    if len(sys.argv) > 2:
        big = len(model.verts) > 4096 or len(model.faces) > 16384
        export.export_models(sys.argv[2], M.Mesh([model]), anim=A.Anim([rig]), title="Mannequin", kernel_limits=big)
        print("wrote", sys.argv[2])
