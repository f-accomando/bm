"""A plain artist's mannequin (male) as ONE continuous skin, rigged: base shapes only, with a continuous `detail` knob.
The reference for how much detail bm-forge can hold and for polygon-reduction tests (references/DETAIL.md).

Pelvis, waist, ribcage, neck and head are one loft of rings (a column). The arms start from a patch of column
vertices at the shoulder, the legs from the two halves of the pelvis ring, and run on without a break to the hands
and the feet (mitten hands with a pyramid thumb, a foot made of horizontal slices): every joint is a ring shared by
the two segments, so nothing is detached and a bent joint stretches the skin. The same technique as charkit.
Skinning is the format's own, one bone per vertex (src/bm/format.c:266-269); a ring shared by two segments belongs to
the distal bone. Radii follow smooth curves through a few anatomical stations; `detail` raises the sides of the column
(4 * round(4 * detail), at least 12) and the rings along it. Built facing +z, mirrored to -z at the end, .L on +x.

  python mannequin.py DETAIL OUT.bm            e.g.  python mannequin.py 1.0 /abs/path/man1.bm
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(HERE, "..", "charkit"))

from bm_core import anim as A, export, mesh as M, skelgen  # noqa: E402
from charkit import Skin, add, align, ellipse  # noqa: E402

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

# the column: (y, half width, half depth); the head is an egg with a jaw, the neck a thin stalk
COLUMN = [(0.92, 0.172, 0.110), (0.98, 0.174, 0.114), (1.04, 0.166, 0.113), (1.10, 0.142, 0.100), (1.17, 0.130, 0.092),
          (1.26, 0.146, 0.098), (1.36, 0.174, 0.110), (1.43, 0.192, 0.108), (1.48, 0.170, 0.094), (1.52, 0.080, 0.066),
          (1.55, 0.052, 0.054), (1.59, 0.050, 0.054), (1.62, 0.060, 0.070), (1.66, 0.078, 0.090), (1.72, 0.090, 0.104),
          (1.77, 0.082, 0.098), (1.80, 0.052, 0.062), (1.82, 0.014, 0.016)]
# the arm, from the shoulder to the fingertip by s = 0..1 along y (1.45 .. 0.72): upper arm, forearm, mitten hand
ARM = [(0.0, 0.052, 0.050), (0.05, 0.060, 0.058), (0.18, 0.048, 0.046), (0.32, 0.040, 0.040), (0.40, 0.037, 0.037),
       (0.47, 0.040, 0.038), (0.62, 0.032, 0.030), (0.74, 0.025, 0.019), (0.78, 0.034, 0.017), (0.88, 0.036, 0.016),
       (0.96, 0.028, 0.013), (1.0, 0.012, 0.008)]
# the leg, from the hip to the ankle by s = 0..1 along y (0.90 .. 0.09): thigh, calf
LEG = [(0.0, 0.080, 0.086), (0.08, 0.082, 0.084), (0.30, 0.071, 0.071), (0.49, 0.050, 0.050), (0.60, 0.052, 0.050),
       (0.80, 0.040, 0.038), (1.0, 0.030, 0.030)]
FOOT = [(0.055, 0.036, 0.052, 0.015), (0.040, 0.043, 0.090, 0.050), (0.028, 0.046, 0.108, 0.075), (0.020, 0.034, 0.112, 0.092)]


def curve(stations):
    """stations [(t, a, b)] -> f(t) -> (a, b): a cubic through them with no overshoot (monotone Hermite)"""
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


def path_xz(points):
    """f(y) -> (x, z) along a polyline of (x, y, z) points that descends in y"""
    def f(y):
        for (x0, y0, z0), (x1, y1, z1) in zip(points, points[1:]):
            if y >= y1 - 1e-9:
                k = (y0 - y) / (y0 - y1) if y0 != y1 else 0.0
                return x0 + (x1 - x0) * k, z0 + (z1 - z0) * k
        return points[-1][0], points[-1][2]
    return f


def build(detail):
    s = Skin()
    d = detail
    n = 4 * max(3, int(round(4 * d)))                 # sides of the column
    w = n // 4 if (n // 4) % 2 == 1 else n // 4 - 1   # width of the shoulder patch (odd: it is centred on a vertex)
    n_arm = 2 * w
    n_leg = n // 2 + 1
    dy = 0.07 / max(d, 0.6)
    col = curve(COLUMN)

    # --- the column: pelvis (hips), waist and ribcage (spine), neck and head
    y0, y1 = COLUMN[0][0], COLUMN[-1][0]
    m = max(8, int(round((y1 - y0) / dy)))
    ys = [y0 + (y1 - y0) * i / m for i in range(m + 1)]
    R = []
    for y in ys:
        ru, rv = col(y)
        bone = "hips" if y < 1.075 else ("spine" if y < 1.495 else "head")
        R.append(s.ring(ellipse(y, ru, rv, n, cz=0.005 if y > 1.5 else 0.0), bone))
    for i in range(m):
        s.bridge(R[i], R[i + 1], LIGHT if ys[i] > 1.56 else SKIN)
    s.cap(R[-1], LIGHT)
    centre = lambda c: (c if c < n else c - n)                                           # noqa: E731

    # --- arms: a patch of 2 x w column vertices at the shoulder, then rings down to the fingertip
    arm = curve(ARM)
    i_sh = max(i for i in range(m) if ys[i] <= 1.44)
    for sx, S, c in ((1, "L", 0), (-1, "R", n // 2)):
        ks = [centre(c + j) for j in range(-(w - 1) // 2, (w - 1) // 2 + 1)]
        prev = [R[i_sh][k] for k in ks] + [R[i_sh + 1][k] for k in reversed(ks)]
        ya, yb = ys[i_sh + 1] - 0.005, 0.72                         # the arm starts just under the shoulder patch
        pts = [(sx * 0.205, ya, 0.0), (sx * 0.325, 1.16, 0.012), (sx * 0.35, 0.91, 0.04), (sx * 0.358, 0.72, 0.058)]
        xz = path_xz(pts)
        m_arm = max(6, int(round((ya - yb) / (dy * 0.9))))
        rings = []
        for i in range(1, m_arm + 1):
            y = ya - (ya - yb) * i / m_arm
            sp = (ya - y) / (ya - yb)
            ru, rv = arm(sp)
            x, z = xz(y)
            bone = f"arm.{S}" if sp < 0.395 else (f"forearm.{S}" if sp < 0.735 else f"hand.{S}")
            ids = s.ring(align(s, prev, ellipse(y, ru, rv, n_arm, cx=x, cz=z)), bone)
            s.bridge(prev, ids, LIGHT if sp >= 0.735 else SKIN)
            prev = ids
            rings.append((sp, ids))
        s.cap(prev, LIGHT)
        # thumb: a pyramid on the palm, towards the body and a little forward
        palm = next(ids for sp, ids in rings if sp >= 0.80)
        before = rings[[k for k, (sp, _) in enumerate(rings) if sp >= 0.80][0] - 1][1]
        j = min(range(n_arm), key=lambda q: (s.v[palm[q]][0] * sx) + 0.5 * s.v[palm[q]][2])      # the inner-front corner
        base = [palm[j], palm[(j + 1) % n_arm], before[(j + 1) % n_arm], before[j]]
        cx_, cy_, cz_ = s.centre(base)
        s.tent(base, (cx_ - sx * 0.018, cy_ - 0.045, cz_ + 0.014), LIGHT, f"hand.{S}")

    # --- legs: half of the pelvis ring (an arc closed by a chord), then rings down to the ankle and the foot
    leg = curve(LEG)
    for sx, S, c in ((1, "L", 0), (-1, "R", n // 2)):
        prev = [R[0][centre(c + j)] for j in range(-n // 4, n // 4 + 1)]
        pts = [(sx * 0.088, 0.90, 0.0), (sx * 0.093, 0.50, 0.012), (sx * 0.095, 0.09, 0.0)]
        xz = path_xz(pts)
        ya, yb = 0.90, 0.09
        m_leg = max(6, int(round((ya - yb) / (dy * 1.2))))
        for i in range(m_leg + 1):
            y = ya - (ya - yb) * i / m_leg
            sp = (ya - y) / (ya - yb)
            ru, rv = leg(sp)
            x, z = xz(y)
            bone = f"leg.{S}" if sp < 0.49 else (f"foot.{S}" if i == m_leg else f"shin.{S}")
            pts_e = align(s, prev, ellipse(y, ru, rv, n_leg, cx=x, cz=z))
            if i == 0:           # a ring half way between the pelvis arc and the thigh: two gentle steps, no slivers
                mid = s.ring([((s.v[p][0] + q[0]) / 2, (s.v[p][1] + q[1]) / 2, (s.v[p][2] + q[2]) / 2)
                              for p, q in zip(prev, pts_e)], f"leg.{S}")
                s.bridge(prev, mid, SKIN)
                prev = mid
            ids = s.ring(pts_e, bone)
            s.bridge(prev, ids, SKIN)
            prev = ids
        for y, ru, rv, cz in FOOT:                                                  # the foot: horizontal slices
            ids = s.ring(align(s, prev, ellipse(y, ru, rv, n_leg, cx=sx * 0.095, cz=cz)), f"foot.{S}")
            s.bridge(prev, ids, LIGHT)
            prev = ids
        s.cap(prev, LIGHT, inside=(sx * 0.095, 0.05, 0.05))

    # --- face -z, bind
    s.v = [(x, y, -z) for x, y, z in s.v]
    s.f = [(a, c, b, col_) for a, b, c, col_ in s.f]
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
    model = M.Model("mannequin", [list(v) for v in s.v], [M.Face(a, b, c, col_ | FLAT) for a, b, c, col_ in s.f], 0)
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
