"""charkit: a rigged, continuous, flat-shaded low-poly character from a data table (stdlib + bm_core).

A character is a JSON "spec": a palette, a column of rings (pelvis -> neck -> head), feature tables
(eyes, ears, hair, hat, arms, legs, tail, decals) and a skeleton. One shared vertex table, so limbs, ears,
eyes, tail and fingers GROW OUT of vertices they share with the part they hang on: nothing is detached.
Skinning is the format's own: one bone per vertex (src/bm/format.c:266-269); a ring shared by two segments
belongs to the distal bone. The model is built facing +z and mirrored at the end to face -z, .L on +x,
as bm Studio's recipes (src/ai/mesh_chars.c). Schema and the shared parts: references/CHARACTERS.md.

  python charkit.py presets/rango.json OUT.bm      (absolute OUT path; then bm_forge.py check / preview / anim)
"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", ".."))

from bm_core import anim as A, export, mesh as M, skelgen  # noqa: E402

FLAT = 0x08000000
N = 12                      # sides of the column rings; sector k is between vertex k and k+1; 3 = straight ahead


def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def mul(a, k): return (a[0] * k, a[1] * k, a[2] * k)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def unit(a):
    n = math.sqrt(dot(a, a)) or 1.0
    return (a[0] / n, a[1] / n, a[2] / n)


class Skin:
    """one shared vertex table; every vertex has a bone name; faces are (a, b, c, colour)"""

    def __init__(self):
        self.v, self.bone, self.f = [], [], []

    def vert(self, p, bone):
        self.v.append(tuple(p))
        self.bone.append(bone)
        return len(self.v) - 1

    def ring(self, pts, bone):
        return [self.vert(p, bone) for p in pts]

    def centre(self, ids):
        return mul(tuple(map(sum, zip(*[self.v[i] for i in ids]))), 1.0 / len(ids))

    def tri(self, a, b, c, col, inside):
        """a face turned so its normal (b-a)x(c-a) points away from `inside`, a point inside the part"""
        n = cross(sub(self.v[b], self.v[a]), sub(self.v[c], self.v[a]))
        mid = mul(add(add(self.v[a], self.v[b]), self.v[c]), 1 / 3)
        if dot(n, sub(mid, inside)) < 0:
            b, c = c, b
        self.f.append((a, b, c, col))

    def quad(self, a, b, c, d, col, inside):
        self.tri(a, b, c, col, inside)
        self.tri(a, c, d, col, inside)

    def bridge(self, r0, r1, col):
        """faces between two rings of the same size; col is a colour or a list (one per sector)"""
        n = len(r0)
        inside = mul(add(self.centre(r0), self.centre(r1)), 0.5)
        for k in range(n):
            c = col[k] if isinstance(col, (list, tuple)) else col
            j = (k + 1) % n
            self.quad(r0[k], r0[j], r1[j], r1[k], c, inside)

    def cap(self, r, col, apex=None, bone=None, inside=None):
        c = self.vert(apex, bone) if apex is not None else self.vert(self.centre(r), self.bone[r[0]])
        inside = inside if inside is not None else self.centre(r + [c])
        for k in range(len(r)):
            self.tri(r[k], r[(k + 1) % len(r)], c, col, inside)
        return c

    def tent(self, base, apex, col, bone):
        """a pyramid on `base` vertices (shared with the part it grows from)"""
        t = self.vert(apex, bone)
        inside = self.centre(base + [t])
        for k in range(len(base)):
            self.tri(base[k], base[(k + 1) % len(base)], t, col, inside)
        return t


def ellipse(cy, rx, rz, n=12, off=0.0, cx=0.0, cz=0.0):
    return [(cx + rx * math.cos(off + 2 * math.pi * k / n), cy, cz + rz * math.sin(off + 2 * math.pi * k / n))
            for k in range(n)]


def frame_ring(c, t, r, n=6, ref=(0.0, 0.0, 1.0)):
    """a ring of n points round centre c, in the plane perpendicular to direction t (radius r)"""
    t = unit(t)
    u = sub(ref, mul(t, dot(ref, t)))
    if dot(u, u) < 1e-6:
        u = sub((1.0, 0.0, 0.0), mul(t, t[0]))
    u = unit(u)
    v = cross(t, u)
    return [add(c, add(mul(u, r * math.cos(2 * math.pi * k / n)), mul(v, r * math.sin(2 * math.pi * k / n))))
            for k in range(n)]


def align(skin, given, new):
    """order `new` (points) so it follows the vertex ids `given` (best cyclic shift and direction)"""
    n = len(given)
    best = None
    for d in (1, -1):
        for s in range(n):
            cost = sum(sum((x - y) ** 2 for x, y in zip(skin.v[given[i]], new[(d * i + s) % n])) for i in range(n))
            if best is None or cost < best[0]:
                best = (cost, d, s)
    _, d, s = best
    return [new[(d * i + s) % n] for i in range(n)]


def disc(s, c, n, ru, rv, h, col, bone):
    """a flat 8-sided decal lying on a surface with normal n, raised by h at its middle"""
    u = unit(cross(n, (0, 1, 0)))
    v = cross(n, u)
    ring = [add(add(c, mul(n, 0.004)), add(mul(u, ru * math.cos(a)), mul(v, rv * math.sin(a))))
            for a in [2 * math.pi * i / 8 for i in range(8)]]
    ids = s.ring(ring, bone)
    s.cap(ids, col, apex=add(c, mul(n, h)), bone=bone)


def build(spec):
    """spec (dict) -> (anim-ready Model, Rig, Skin). See references/CHARACTERS.md for the schema."""
    pal = {k: int(v.lstrip("#"), 16) for k, v in spec["palette"].items()}
    P = lambda k: pal[k]                                                     # noqa: E731
    idx = spec["idx"]
    s = Skin()

    # ---- the column
    R = [s.ring(ellipse(r[0], r[1], r[2], N, math.radians(r[4]) if len(r) > 4 else 0.0), r[3]) for r in spec["column"]]
    for pu in spec.get("push", []):
        for k, d in pu["dz"].items():
            x, y, z = s.v[R[pu["ring"]][int(k)]]
            s.v[R[pu["ring"]][int(k)]] = (x, y, z + d)
    for i, sg in enumerate(spec["segments"]):
        col = [P(sg["base"])] * N
        for k in (2, 3):
            if "front" in sg:
                col[k] = P(sg["front"])
        for k in (8, 9):
            if "back" in sg:
                col[k] = P(sg["back"])
        for k, c in sg.get("sides", {}).items():
            col[int(k)] = P(c)
        s.bridge(R[i], R[i + 1], col)
    tc = spec["top_cap"]
    s.cap(R[-1], P(tc["colour"]), apex=tuple(tc["apex"]), bone=spec["column"][-1][3])

    ring_c = lambda i: s.centre(R[i])                                        # noqa: E731
    HEADC = ring_c(idx["eye_lo"])

    # ---- tents: pyramids on named head/body vertices (nose, horns, crests), apex = vertex `at` + offset
    for t in spec.get("tents", []):
        base = [R[r][k] for r, k in t["base"]]
        ax = s.v[base[t.get("at", 0)]]
        s.tent(base, add(ax, tuple(t["off"])), P(t["colour"]), t.get("bone", "head"))

    # ---- eyes
    ey = spec.get("eyes")
    if ey:
        lo, hi = R[idx["eye_lo"]], R[idx["eye_hi"]]
        if ey["mode"] == "front":
            for k in ey["sectors"]:
                ids = [lo[k], lo[k + 1], hi[k + 1], hi[k]]
                c = s.centre(ids)
                nq = unit(sub(c, HEADC))
                n = unit((nq[0] * 0.45, nq[1] * 0.6, nq[2] + 0.75))
                c = add(c, mul(n, ey["lift"]))
                sx = 1 if c[0] > 0 else -1
                disc(s, c, n, ey["ru"], ey["rv"], 0.012, P(ey["white"]), "head")
                disc(s, add(c, add(mul(n, 0.010), (-sx * 0.012, 0.0, 0.0))), n, ey["ru"] * 0.5, ey["rv"] * 0.6, 0.010,
                     P(ey["pupil"]), "head")
        else:                                                                # turret: a short limb with a pupil on the tip
            d0 = ey.get("dir", [0.7, 0.35, 0.55])
            for sx, ks in ((1, (11, 0, 1)), (-1, (7, 6, 5))):
                prev = [lo[ks[0]], lo[ks[1]], lo[ks[2]], hi[ks[2]], hi[ks[1]], hi[ks[0]]]
                c0 = s.centre(prev)
                dv = unit((sx * d0[0], d0[1], d0[2]))
                last = c0
                for dist, r, col in ey["rings"]:
                    last = add(c0, mul(dv, dist))
                    ids = s.ring(align(s, prev, frame_ring(last, dv, r, 6, ref=(0, 1, 0))), "head")
                    s.bridge(prev, ids, P(col))
                    prev = ids
                s.cap(prev, P(ey["pupil"]), apex=add(last, mul(dv, 0.012)), bone="head")

    # ---- ears (tents on a patch of head vertices)
    for e in spec.get("ears", []):
        lo, hi = R[e["lo"]], R[e["hi"]]
        for sx, ks in ((1, (11, 0, 1)), (-1, (7, 6, 5))):
            base = [lo[ks[0]], lo[ks[1]], lo[ks[2]], hi[ks[2]], hi[ks[1]], hi[ks[0]]]
            ax, ay, az = e["apex"]
            t = s.vert((sx * ax, ay, az), "head")
            inside = s.centre(base + [t])
            for q in range(6):
                a_, b_ = base[q], base[(q + 1) % 6]
                inner = s.v[a_][2] > 0 and s.v[b_][2] > 0
                s.tri(a_, b_, t, P(e["inner"]) if inner else P(e["colour"]), inside)

    # ---- hair spikes (tents on crown quads)
    for hs in spec.get("hair", []):
        lo, hi = R[hs["lo"]], R[hs["hi"]]
        for k, h in hs["spikes"]:
            base = [lo[k], lo[(k + 1) % N], hi[(k + 1) % N], hi[k]]
            c = s.centre(base)
            s.tent(base, (c[0], c[1] + h, c[2] - 0.05), P(hs["colour"]), "head")

    # ---- hat: brim and crown grow from the head ring `ring`
    ht = spec.get("hat")
    if ht:
        prev = R[ht["ring"]]
        cc = s.centre(prev)
        for dy, rs, col in ht["rings"]:
            y0 = cc[1] + dy
            pts = [(cc[0] + (s.v[v][0] - cc[0]) * rs, y0, cc[2] + (s.v[v][2] - cc[2]) * rs) for v in R[ht["ring"]]]
            ids = s.ring(pts, "head")
            s.bridge(prev, ids, P(col))
            prev = ids
        s.cap(prev, P(ht["top"]), bone="head")

    # ---- arms: first ring = patch of torso vertices
    def names(tpl, side):
        return tpl.replace("{S}", side) if side else tpl

    pa = spec["arms"]
    for sx, side, ks in ((1, "L", (11, 0, 1)), (-1, "R", (7, 6, 5))):
        lo, hi = R[pa["lo"]], R[pa["hi"]]
        prev = [lo[ks[0]], lo[ks[1]], lo[ks[2]], hi[ks[2]], hi[ks[1]], hi[ks[0]]]
        rings = []
        for y, x, r, bone, col in pa["rings"]:
            pts = ellipse(y, r, r, 6, 0, cx=sx * x, cz=pa.get("z", 0.04) if y < pa.get("z_below", 0.9) else 0.0)
            ids = s.ring(align(s, prev, pts), names(bone, side))
            s.bridge(prev, ids, P(col))
            prev = ids
            rings.append(ids)
        palm, knuckle = rings[-2], rings[-1]
        hb = names(pa["rings"][-1][3], side)
        s.cap(knuckle, P(pa["rings"][-1][4]))
        for j in pa["fingers"]:
            base = [palm[j], palm[(j + 1) % 6], knuckle[(j + 1) % 6], knuckle[j]]
            c = s.centre(base)
            s.tent(base, (c[0] + sx * pa.get("spread", 0.01), c[1] - pa["finger_len"], c[2] + 0.02),
                   P(pa["rings"][-1][4]), hb)

    # ---- legs: first ring = the half of the pelvis ring on that side (an open arc closed by a chord)
    pl = spec["legs"]
    arcs = {1: (9, 10, 11, 0, 1, 2), -1: (3, 4, 5, 6, 7, 8)}
    for sx, side in ((1, "L"), (-1, "R")):
        prev = [R[0][k] for k in arcs[sx]]
        cx = sx * pl["x"]
        for y, r, bone, col in pl["rings"]:
            ids = s.ring(align(s, prev, ellipse(y, r, r, 6, 0, cx=cx)), names(bone, side))
            s.bridge(prev, ids, P(col))
            prev = ids
        fb = names(pl["foot_bone"], side)
        fr = []
        for y, rx, rz, cz, col in pl["foot"]:
            ids = s.ring(align(s, prev, ellipse(y, rx, rz, 6, 0, cx=cx, cz=cz)), fb)
            s.bridge(prev, ids, P(col))
            prev = ids
            fr.append(ids)
        s.cap(prev, P(pl["foot"][-1][4]), inside=(cx, 0.1, 0.1))
        for j, ln, dx in pl.get("toes", []):
            base = [fr[-1][j], fr[-1][(j + 1) % 6], fr[-2][(j + 1) % 6], fr[-2][j]]
            c = s.centre(base)
            s.tent(base, (c[0] + sx * dx, c[1] - 0.01, c[2] + ln), P(pl["toe_colour"]), fb)
    s.quad(R[0][9], R[0][2], R[0][3], R[0][8], P(pl["crotch"]), (0, 0.7, 0))

    # ---- tail: grows from a patch of pelvis/waist vertices at the back
    tl = spec.get("tail")
    if tl:
        lo, hi = R[tl["lo"]], R[tl["hi"]]
        ks = tl["ks"]
        prev = [lo[ks[0]], lo[ks[1]], lo[ks[2]], hi[ks[2]], hi[ks[1]], hi[ks[0]]]
        pts = [(0.0, tl["from"][0], tl["from"][1])] + [(0.0, p[0], p[1]) for p in tl["path"]]
        for i, (y, z, r, col, bone) in enumerate(tl["path"]):
            tdir = sub((0.0, y, z), pts[i])
            ids = s.ring(align(s, prev, frame_ring((0.0, y, z), tdir, r, 6, ref=(1, 0, 0))), bone)
            s.bridge(prev, ids, P(col))
            prev = ids
        s.cap(prev, P(tl["path"][-1][3]), bone=tl["path"][-1][4])

    # ---- decals (flowers, badge): flat discs on the quad (ring a, ring a+1, sector k)
    for a, k, colkey, rad in spec.get("decals", []):
        ids = [R[a][k], R[a][k + 1], R[a + 1][k + 1], R[a + 1][k]]
        c = s.centre(ids)
        n = unit(cross(sub(s.v[ids[1]], s.v[ids[0]]), sub(s.v[ids[3]], s.v[ids[0]])))
        if dot(n, sub(c, (0, c[1], 0))) < 0:
            n = mul(n, -1)
        disc(s, add(c, mul(n, 0.006)), n, rad, rad, 0.008, P(colkey), spec["column"][a][3])

    # ---- face -z, like bm Studio's recipes: mirror z, then turn every face back
    s.v = [(x, y, -z) for x, y, z in s.v]
    s.f = [(a, c, b, col) for a, b, c, col in s.f]

    # ---- skeleton and bind (one bone per vertex)
    bones = []
    for b in spec["bones"]:
        for side, sx in (("L", 1), ("R", -1)) if b.get("mirror") else ((None, 1),):
            d = {"name": names(b["name"], side) if side else b["name"], "parent": names(b["parent"], side) if b["parent"] else None}
            for key in ("head", "tail"):
                if key in b:
                    x, y, z = b[key]
                    d[key] = [x * sx, y, -z]
            for key in ("connect", "length", "direction"):
                if key in b:
                    d[key] = b[key]
            bones.append(d)
    rig = skelgen.skeleton({"model": spec["name"], "bones": bones})
    bidx = {b.name: i for i, b in enumerate(rig.bones)}
    model = M.Model(spec["name"], [list(v) for v in s.v], [M.Face(a, b, c, col | FLAT) for a, b, c, col in s.f], 0)
    rig = skelgen.bind(model, rig, vbones=[bidx[b] for b in s.bone])
    return model, rig, s


def main(argv):
    spec = json.load(open(argv[1], encoding="utf-8"))
    model, rig, _ = build(spec)
    print(f"{spec['name']}: {len(model.verts)} verts, {len(model.faces)} tris, {len(rig.bones)} bones", file=sys.stderr)
    if len(argv) > 2:
        export.export_models(argv[2], M.Mesh([model]), anim=A.Anim([rig]), title=spec.get("title", spec["name"]))
        print("wrote", argv[2])


if __name__ == "__main__":
    main(sys.argv)
