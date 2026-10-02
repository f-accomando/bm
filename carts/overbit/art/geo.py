"""
geo.py - the modelling kit of Overbit's heroes: low-poly parts built from
primitives (bevelled boxes, convex hulls, cylinders, ellipsoids, lathes),
each part painted with a material and tied to one bone of the skeleton.

Conventions (as r3d.c and bm Studio): y up, a character faces +z, its
right side is +x (so the left arm, "L", is at -x). A face shows from the
side where its corners turn clockwise; its normal is (b - a) x (c - a), and
it points out of the part. Units: metres.

A Mesh keeps vertices, triangles and per-vertex bones; smooth parts share
their vertices (Gouraud makes them look round), hard parts do not (every
face has its own corners: flat panels with sharp edges).
"""
import math

# material bits of a face colour (src/bm/r3d.h)
TEXTURED = 0x80000000
EMISSIVE = 0x40000000
GLOSSY = 0x20000000
SCREEN = 0x10000000
FLAT = 0x08000000


def lod_bits(lo=0, hi=3):
    """the face shows at levels of detail lo..hi (0 the lowest): every
    level, lo..3 (a detail) or 0..hi (a coarse stand-in)"""
    if lo == 0 and hi == 3:
        return 0
    if hi == 3:
        return (lo & 3) << 24
    if lo == 0:
        return (hi + 1) << 24 | 1 << 26
    raise ValueError(f"levels of detail {lo}..{hi}: only 0..3, k..3 or 0..k")


class Mat:
    """a colour and how it reacts to light"""

    def __init__(self, rgb, glossy=False, emissive=False, screen=False):
        self.rgb = rgb
        self.flags = (GLOSSY if glossy else 0) | (EMISSIVE if emissive else 0) | (SCREEN if screen else 0)

    def colour(self, lo=0, hi=3):
        return self.rgb | self.flags | lod_bits(lo, hi)


# ------------------------------------------------------------------ vectors

def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, k): return (a[0] * k, a[1] * k, a[2] * k)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def length(a): return math.sqrt(dot(a, a))


def norm(a):
    n = length(a)
    return mul(a, 1 / n) if n > 1e-12 else (0.0, 0.0, 0.0)


def lerp(a, b, t): return add(a, mul(sub(b, a), t))


def rot_x(p, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return (p[0], p[1] * c - p[2] * s, p[1] * s + p[2] * c)


def rot_y(p, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return (p[0] * c + p[2] * s, p[1], -p[0] * s + p[2] * c)


def rot_z(p, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return (p[0] * c - p[1] * s, p[0] * s + p[1] * c, p[2])


def turn(p, rx=0, ry=0, rz=0):
    """x, then y, then z (degrees)"""
    return rot_z(rot_y(rot_x(p, rx), ry), rz)


# ------------------------------------------------------------------ parts

class Part:
    """a primitive before it joins a mesh: points, faces (index tuples,
    outward and clockwise), smooth or hard"""

    def __init__(self, pts, faces, smooth=False):
        self.pts = [tuple(float(c) for c in p) for p in pts]
        self.faces = [tuple(f) for f in faces]
        self.smooth = smooth

    def copy(self):
        return Part(list(self.pts), list(self.faces), self.smooth)

    def map(self, fn):
        self.pts = [fn(p) for p in self.pts]
        return self

    def move(self, x=0, y=0, z=0):
        return self.map(lambda p: (p[0] + x, p[1] + y, p[2] + z))

    def scale(self, sx, sy=None, sz=None):
        sy = sx if sy is None else sy
        sz = sx if sz is None else sz
        flip = sx * sy * sz < 0
        self.map(lambda p: (p[0] * sx, p[1] * sy, p[2] * sz))
        if flip:
            self.faces = [(f[0], f[2], f[1]) for f in self.faces]
        return self

    def turn(self, rx=0, ry=0, rz=0):
        return self.map(lambda p: turn(p, rx, ry, rz))

    def mirror_x(self):
        return self.scale(-1, 1, 1)

    def centre(self):
        n = len(self.pts)
        return tuple(sum(p[i] for p in self.pts) / n for i in range(3))

    def orient_out(self):
        """flips the faces whose normal points to the middle (convex parts)"""
        c = self.centre()
        out = []
        for f in self.faces:
            a, b, d = (self.pts[i] for i in f[:3])
            n = cross(sub(b, a), sub(d, a))
            mid = mul(add(add(a, b), d), 1 / 3)
            out.append(f if dot(n, sub(mid, c)) >= 0 else tuple(reversed(f)))
        self.faces = out
        return self


def tris(faces):
    """polygons (clockwise from outside) -> triangles (a fan)"""
    out = []
    for f in faces:
        for i in range(1, len(f) - 1):
            out.append((f[0], f[i], f[i + 1]))
    return out


def box(sx, sy, sz, bevel=0.0):
    """a box centred on the origin, sx x sy x sz; with `bevel` its edges are
    cut at 45 degrees (a hard-surface look)"""
    hx, hy, hz = sx / 2, sy / 2, sz / 2
    if bevel <= 0:
        pts = [(x, y, z) for x in (-hx, hx) for y in (-hy, hy) for z in (-hz, hz)]
        return hull(pts)
    b = min(bevel, hx * 0.9, hy * 0.9, hz * 0.9)
    pts = []
    for x in (-1, 1):
        for y in (-1, 1):
            for z in (-1, 1):
                pts += [(x * hx, y * (hy - b), z * (hz - b)), (x * (hx - b), y * hy, z * (hz - b)),
                        (x * (hx - b), y * (hy - b), z * hz)]
    return hull(pts)


def wedge(w, h, d, top_d=None, top_w=None, top_dz=0.0):
    """a tapered box: bottom w x d, top top_w x top_d moved top_dz in z"""
    top_d = d if top_d is None else top_d
    top_w = w if top_w is None else top_w
    pts = []
    for x in (-1, 1):
        for z in (-1, 1):
            pts.append((x * w / 2, 0, z * d / 2))
            pts.append((x * top_w / 2, h, z * top_d / 2 + top_dz))
    return hull(pts)


def cylinder(r, h, segs=8, r2=None, smooth=True, caps=True, axis="y"):
    """from y = 0 to y = h (radius r, then r2 at the top), round"""
    r2 = r if r2 is None else r2
    pts, faces = [], []
    for i in range(segs):
        a = 2 * math.pi * i / segs
        pts.append((r * math.cos(a), 0, r * math.sin(a)))
        pts.append((r2 * math.cos(a), h, r2 * math.sin(a)))
    for i in range(segs):
        j = (i + 1) % segs
        b0, t0, b1, t1 = 2 * i, 2 * i + 1, 2 * j, 2 * j + 1
        faces += [(b0, t0, t1), (b0, t1, b1)]
    p = Part(pts, faces, smooth)
    p.orient_out()
    if caps:
        # caps are flat: their own corners (a hard edge round the rim)
        cap = Part([q for q in pts], [], False)
        bottom = [2 * i for i in range(segs)]
        top = [2 * i + 1 for i in range(segs)]
        cap.faces = tris([bottom]) + tris([top])
        cap.orient_out()
        p = join(p, cap)
    if axis == "x":
        p.turn(rz=-90)
    elif axis == "z":
        p.turn(rx=90)
    return p


def ellipsoid(rx, ry, rz, segs=10, rings=6, smooth=True):
    pts, faces = [(0, ry, 0)], []
    for k in range(1, rings):
        t = math.pi * k / rings
        for i in range(segs):
            a = 2 * math.pi * i / segs
            pts.append((rx * math.sin(t) * math.cos(a), ry * math.cos(t), rz * math.sin(t) * math.sin(a)))
    pts.append((0, -ry, 0))
    last = len(pts) - 1
    for i in range(segs):
        faces.append((0, 1 + (i + 1) % segs, 1 + i))
    for k in range(rings - 2):
        for i in range(segs):
            a, b = 1 + k * segs + i, 1 + k * segs + (i + 1) % segs
            faces += [(a, b, b + segs), (a, b + segs, a + segs)]
    base = 1 + (rings - 2) * segs
    for i in range(segs):
        faces.append((last, base + i, base + (i + 1) % segs))
    p = Part(pts, faces, smooth)
    return p.orient_out()


def lathe(profile, segs=10, smooth=True, close_top=True, close_bottom=True):
    """a profile [(radius, y), ...] from bottom to top, turned around y;
    the ends closed by flat caps"""
    pts, side = [], []
    n = len(profile)
    for r, y in profile:
        for i in range(segs):
            a = 2 * math.pi * i / segs
            pts.append((r * math.cos(a), y, r * math.sin(a)))
    for k in range(n - 1):
        for i in range(segs):
            a, b = k * segs + i, k * segs + (i + 1) % segs
            side += [(a, a + segs, b + segs), (a, b + segs, b)]

    def outward(f, want):
        p0, p1, p2 = (pts[i] for i in f)
        nn = cross(sub(p1, p0), sub(p2, p0))
        return f if dot(nn, want) >= 0 else (f[0], f[2], f[1])

    faces = []
    for f in side:
        mid = mul(add(add(pts[f[0]], pts[f[1]]), pts[f[2]]), 1 / 3)
        faces.append(outward(f, (mid[0], 0, mid[2])))
    part = Part(pts, faces, smooth)
    caps = []
    if close_bottom and profile[0][0] > 1e-6:
        ring = list(range(segs))
        caps += [outward(t, (0, -1, 0)) for t in tris([ring])]
    if close_top and profile[-1][0] > 1e-6:
        ring = list(range((n - 1) * segs, n * segs))
        caps += [outward(t, (0, 1, 0)) for t in tris([ring])]
    if caps:
        part = join(part, Part(pts, caps, False))
        part.smooth = smooth
    return part


def hull(points, smooth=False):
    """the convex hull of a few points (incremental, fine for < 100)"""
    pts = [tuple(float(c) for c in p) for p in points]
    # remove duplicates
    uniq = []
    for p in pts:
        if all(length(sub(p, q)) > 1e-7 for q in uniq):
            uniq.append(p)
    pts = uniq
    n = len(pts)
    if n < 4:
        raise ValueError("hull: at least 4 distinct points")
    # a first tetrahedron that is not flat
    i0 = 0
    i1 = max(range(n), key=lambda i: length(sub(pts[i], pts[i0])))
    i2 = max(range(n), key=lambda i: length(cross(sub(pts[i1], pts[i0]), sub(pts[i], pts[i0]))))
    nrm = cross(sub(pts[i1], pts[i0]), sub(pts[i2], pts[i0]))
    i3 = max(range(n), key=lambda i: abs(dot(nrm, sub(pts[i], pts[i0]))))
    if abs(dot(nrm, sub(pts[i3], pts[i0]))) < 1e-12:
        raise ValueError("hull: the points are flat")
    centre = mul(add(add(pts[i0], pts[i1]), add(pts[i2], pts[i3])), 0.25)

    def oriented(a, b, c):
        nn = cross(sub(pts[b], pts[a]), sub(pts[c], pts[a]))
        return (a, b, c) if dot(nn, sub(pts[a], centre)) > 0 else (a, c, b)

    faces = [oriented(i0, i1, i2), oriented(i0, i1, i3), oriented(i0, i2, i3), oriented(i1, i2, i3)]
    for i in range(n):
        if i in (i0, i1, i2, i3):
            continue
        p = pts[i]
        visible = []
        for f in faces:
            a, b, c = (pts[k] for k in f)
            nn = cross(sub(b, a), sub(c, a))
            if dot(nn, sub(p, a)) > 1e-9 * max(1.0, length(nn)):
                visible.append(f)
        if not visible:
            continue
        edges = {}
        for f in visible:
            for e in ((f[0], f[1]), (f[1], f[2]), (f[2], f[0])):
                edges[e] = edges.get(e, 0) + 1
        horizon = [e for e in edges if (e[1], e[0]) not in edges]
        faces = [f for f in faces if f not in visible]
        for a, b in horizon:
            faces.append((a, b, i))
    used = sorted({k for f in faces for k in f})
    remap = {k: j for j, k in enumerate(used)}
    return Part([pts[k] for k in used], [tuple(remap[k] for k in f) for f in faces], smooth)


def join(*parts):
    """parts -> one part (the first one's smoothness)"""
    out = Part([], [], parts[0].smooth)
    for p in parts:
        base = len(out.pts)
        out.pts += p.pts
        out.faces += [tuple(k + base for k in f) for f in p.faces]
    return out


# ------------------------------------------------------------------ mesh

class Mesh:
    """the model: parts with a material, a bone and levels of detail"""

    def __init__(self, name):
        self.name = name
        self.verts = []          # (x, y, z)
        self.vbone = []          # bone index of each vertex
        self.vhard = []          # the vertex belongs to one flat face only
        self.faces = []          # (a, b, c, colour)

    def add(self, part, mat, bone=0, lo=0, hi=3):
        colour = mat.colour(lo, hi)
        if part.smooth:
            base = len(self.verts)
            self.verts += part.pts
            self.vbone += [bone] * len(part.pts)
            self.vhard += [False] * len(part.pts)
            for f in part.faces:
                for t in tris([f]):
                    self.faces.append((t[0] + base, t[1] + base, t[2] + base, colour))
        else:
            # flat faces: lit by their own plane, so their corners can be
            # shared (weld) with no effect on the look
            for f in part.faces:
                for t in tris([f]):
                    base = len(self.verts)
                    self.verts += [part.pts[k] for k in t]
                    self.vbone += [bone] * 3
                    self.vhard += [True] * 3
                    self.faces.append((base, base + 1, base + 2, colour | FLAT))
        return self

    def weld(self, eps=1e-5):
        """merges the corners of flat faces that are in the same place and
        on the same bone: fewer vertices to move, the same look (flat faces
        take their light from their own plane)"""
        key_of, remap, verts, vbone, vhard = {}, [], [], [], []
        for i, p in enumerate(self.verts):
            if self.vhard[i]:
                key = (round(p[0] / eps), round(p[1] / eps), round(p[2] / eps), self.vbone[i])
            else:
                key = ("smooth", i)
            if key not in key_of:
                key_of[key] = len(verts)
                verts.append(p)
                vbone.append(self.vbone[i])
                vhard.append(self.vhard[i])
            remap.append(key_of[key])
        self.verts, self.vbone, self.vhard = verts, vbone, vhard
        self.faces = [(remap[a], remap[b], remap[c], col) for a, b, c, col in self.faces]
        return self

    def stats(self, detail=3):
        def shows(c):
            k = c >> 24 & 3
            return detail < k if c >> 26 & 1 else detail >= k
        f = [x for x in self.faces if shows(x[3])]
        used = {k for x in f for k in x[:3]}
        return len(used), len(f)

    def model(self):
        """for bmmesh.encode"""
        return {"name": self.name, "verts": self.verts,
                "faces": [(a, b, c, col, (0,) * 6) for a, b, c, col in self.faces]}
