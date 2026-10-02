"""
mapgeo.py - the pieces of Overbit's maps: boxes, prisms, ramps, stairs,
any part of geo.py, the paved ground, lamps and markers, gathered in a Map.

A map is triangles with a material (colour, glowing, see-through) and the
normal of each corner (smooth parts round their corners), plus boxes: the
solid ones are the collision world (and hide the faces against them), the
"occluders" throw shadows when the light is baked (mapbake.py).

Units: metres, y up; the ground is y = 0.
"""
import math

from geo import Part, add, cross, dot, length, lod_bits, norm, sub

TEXTURED = 0x80000000
EMISSIVE = 0x40000000
SCREEN = 0x10000000


class Mat:
    """a material: colour 0xRRGGBB; glow: it lights itself (neon, lamps,
    sky); see: see-through (screen-door glass); detail: a small thing drawn
    only near (levels of detail 2 and 3: the far chunks leave it out);
    thin: long thin things (cornices, kerbs, railings) not cut into smaller
    triangles for the light (their light changes little across them)"""

    def __init__(self, rgb, glow=False, see=False, detail=False, thin=False):
        self.rgb, self.glow, self.see, self.detail, self.thin = rgb, glow, see, detail, thin

    def bits(self):
        return (self.rgb | (EMISSIVE if self.glow else 0) | (SCREEN if self.see else 0) |
                (lod_bits(2, 3) if self.detail else 0))

    def shade(self, k):
        """the same material, darker or lighter"""
        r, g, b = (self.rgb >> 16) & 255, (self.rgb >> 8) & 255, self.rgb & 255
        c = lambda v: max(0, min(255, int(v * k)))      # noqa: E731
        return Mat(c(r) << 16 | c(g) << 8 | c(b), self.glow, self.see, self.detail, self.thin)

    def small(self):
        """the same material for small things (drawn only near)"""
        return Mat(self.rgb, self.glow, self.see, True, self.thin)


class Tex:
    """a picture of the map's atlas (maptex.py) on a flat quad: a window, a
    door, a sign. The light is baked once for the whole quad; glow: lit by
    itself (signs); detail: drawn only near"""

    def __init__(self, tile, glow=False, detail=False):
        self.tile, self.glow, self.detail = tile, glow, detail

    def bits(self):
        return TEXTURED | (lod_bits(2, 3) if self.detail else 0)


class Map:
    def __init__(self, name, atlas=None):
        self.name = name
        self.atlas = atlas      # maptex.Atlas: the pictures of the decals
        self.tris = []          # (p0, p1, p2, n0, n1, n2, mat)
        self.decals = []        # (p0, p1, p2, n, n, n, Tex, uv): textured, over the walls
        self.solids = []        # (lo, hi): collision, hides faces, shadows
        self.occluders = []     # (lo, hi): only shadows (thin things, foliage)
        self.lamps = []         # (pos, rgb (floats), radius, k)
        self.marks = {}         # name -> data (spawns, the point...)
        self.far = []           # far scenery: (Part, Mat) drawn as is, not baked
        self.nav = []           # (name, (x, y, z)): the places the bots walk between (mapbake.navgraph)

    # ---------------------------------------------------------- triangles

    def tri(self, a, b, c, mat, n=None, normals=None):
        """one triangle, turned to face n (if given)"""
        fn = cross(sub(b, a), sub(c, a))
        if length(fn) < 1e-9:
            return
        if n is not None and dot(fn, n) < 0:
            b, c = c, b
            if normals:
                normals = (normals[0], normals[2], normals[1])
            fn = (-fn[0], -fn[1], -fn[2])
        fn = norm(fn)
        self.tris.append((a, b, c) + (normals or (fn, fn, fn)) + (mat,))

    def quad(self, a, b, c, d, mat, n=None):
        self.tri(a, b, c, mat, n)
        self.tri(a, c, d, mat, n)

    def part(self, p, mat, solid=False, occlude=True):
        """a Part of geo.py (already placed): its triangles; smooth parts keep
        the average normal at each corner"""
        vn = None
        if p.smooth:
            acc = [(0.0, 0.0, 0.0)] * len(p.pts)
            for f in p.faces:
                a, b, c = (p.pts[i] for i in f[:3])
                n = cross(sub(b, a), sub(c, a))
                for i in f[:3]:
                    acc[i] = add(acc[i], n)
            vn = [norm(v) if length(v) > 1e-12 else (0, 1, 0) for v in acc]
        for f in p.faces:
            for k in range(1, len(f) - 1):
                i0, i1, i2 = f[0], f[k], f[k + 1]
                a, b, c = p.pts[i0], p.pts[i1], p.pts[i2]
                if vn:
                    self.tri(a, b, c, mat, None, (vn[i0], vn[i1], vn[i2]))
                else:
                    self.tri(a, b, c, mat)
        lo = tuple(min(q[i] for q in p.pts) for i in range(3))
        hi = tuple(max(q[i] for q in p.pts) for i in range(3))
        if solid:
            self.solids.append((lo, hi))
        elif occlude:
            self.occluders.append((lo, hi))
        return lo, hi

    def decal(self, c, n, w, h, tex, up=(0, 1, 0)):
        """a picture of the atlas, w x h metres, centred on c, facing n (a
        little in front of the wall it is on); its texture upright along `up`"""
        n = norm(n)
        r = cross(n, up)                       # the picture's right, seen from the front
        r = norm(r)
        u = cross(r, n)
        P = lambda i, j: tuple(c[k] + r[k] * w * i + u[k] * h * j for k in range(3))   # noqa: E731
        self.tex_quad(P(-0.5, -0.5), P(-0.5, 0.5), P(0.5, 0.5), P(0.5, -0.5), tex)

    def tex_quad(self, bl, tl, tr, br, tex):
        """a picture of the atlas on any flat quad: its corners bottom left,
        top left, top right, bottom right seen from the side it faces"""
        n = norm(cross(sub(tl, bl), sub(tr, bl)))
        x, y, tw, th = self.atlas.tile(tex.tile)
        # half a texel in from the edges: the next picture never bleeds in
        u0, u1, v0, v1 = x + 0.5, x + tw - 0.5, y + 0.5, y + th - 0.5
        self.decals.append((bl, tl, tr, n, n, n, tex, (u0, v1, u0, v0, u1, v0)))
        self.decals.append((bl, tr, br, n, n, n, tex, (u0, v1, u1, v0, u1, v1)))

    # ---------------------------------------------------------- boxes and the like

    def box(self, x0, y0, z0, x1, y1, z1, mat, top=None, solid=True, occlude=True, sides=None):
        """an upright box; top: another material for the top; sides: which
        faces (a string of "tbnsew": top, bottom, north +z, south -z, east +x,
        west -x; default all)"""
        if x0 > x1: x0, x1 = x1, x0
        if y0 > y1: y0, y1 = y1, y0
        if z0 > z1: z0, z1 = z1, z0
        top = top or mat
        sides = sides or "tbnsew"
        P = lambda x, y, z: (x, y, z)          # noqa: E731
        if "t" in sides:
            self.quad(P(x0, y1, z0), P(x0, y1, z1), P(x1, y1, z1), P(x1, y1, z0), top, (0, 1, 0))
        if "b" in sides and y0 > 0.001:
            self.quad(P(x0, y0, z0), P(x1, y0, z0), P(x1, y0, z1), P(x0, y0, z1), mat, (0, -1, 0))
        if "s" in sides:
            self.quad(P(x0, y0, z0), P(x0, y1, z0), P(x1, y1, z0), P(x1, y0, z0), mat, (0, 0, -1))
        if "n" in sides:
            self.quad(P(x0, y0, z1), P(x1, y0, z1), P(x1, y1, z1), P(x0, y1, z1), mat, (0, 0, 1))
        if "w" in sides:
            self.quad(P(x0, y0, z0), P(x0, y0, z1), P(x0, y1, z1), P(x0, y1, z0), mat, (-1, 0, 0))
        if "e" in sides:
            self.quad(P(x1, y0, z0), P(x1, y1, z0), P(x1, y1, z1), P(x1, y0, z1), mat, (1, 0, 0))
        lohi = ((x0, y0, z0), (x1, y1, z1))
        if solid:
            self.solids.append(lohi)
        elif occlude:
            self.occluders.append(lohi)
        return lohi

    def ramp(self, x0, z0, x1, z1, y0, y1, up, mat, side=None, steps=None):
        """a ramp over the rectangle x0..x1, z0..z1 rising from y0 to y1
        towards `up` ("n", "s", "e", "w"); its collision is a stair of
        steps no higher than 0.3 m (the bodies step up 0.45 m)"""
        side = side or mat.shade(0.85)
        if x0 > x1: x0, x1 = x1, x0
        if z0 > z1: z0, z1 = z1, z0

        def h(x, z):
            if up == "n": return y0 + (y1 - y0) * (z - z0) / (z1 - z0)
            if up == "s": return y0 + (y1 - y0) * (z1 - z) / (z1 - z0)
            if up == "e": return y0 + (y1 - y0) * (x - x0) / (x1 - x0)
            return y0 + (y1 - y0) * (x1 - x) / (x1 - x0)
        a, b, c, d = (x0, h(x0, z0), z0), (x0, h(x0, z1), z1), (x1, h(x1, z1), z1), (x1, h(x1, z0), z0)
        self.quad(a, b, c, d, mat, (0, 1, 0))
        # the sides down to y0 (triangles and the high end)
        for p, q, n in (((x0, z0), (x0, z1), (-1, 0, 0)), ((x1, z0), (x1, z1), (1, 0, 0)),
                        ((x0, z0), (x1, z0), (0, 0, -1)), ((x0, z1), (x1, z1), (0, 0, 1))):
            hp, hq = h(*p), h(*q)
            if hp - y0 < 1e-4 and hq - y0 < 1e-4:
                continue
            self.quad((p[0], y0, p[1]), (p[0], hp, p[1]), (q[0], hq, q[1]), (q[0], y0, q[1]), side, n)
        n = steps or max(1, math.ceil((y1 - y0) / 0.3))
        for i in range(n):
            t0, t1 = i / n, (i + 1) / n
            top = y0 + (y1 - y0) * (i + 0.5) / n
            if up == "n": lo, hi = (x0, y0, z0 + (z1 - z0) * t0), (x1, top, z0 + (z1 - z0) * t1)
            elif up == "s": lo, hi = (x0, y0, z1 - (z1 - z0) * t1), (x1, top, z1 - (z1 - z0) * t0)
            elif up == "e": lo, hi = (x0 + (x1 - x0) * t0, y0, z0), (x0 + (x1 - x0) * t1, top, z1)
            else: lo, hi = (x1 - (x1 - x0) * t1, y0, z0), (x1 - (x1 - x0) * t0, top, z1)
            self.solids.append((lo, hi))

    def stairs(self, x0, z0, x1, z1, y0, y1, up, mat, n=None, riser=None):
        """real steps (each a box), rising towards `up`"""
        n = n or max(1, round((y1 - y0) / 0.25))
        riser = riser or mat.shade(0.8)
        for i in range(n):
            t0, t1 = i / n, (i + 1) / n
            y = y0 + (y1 - y0) * (i + 1) / n
            if up == "n": self.box(x0, y0, z0 + (z1 - z0) * t0, x1, y, z0 + (z1 - z0) * t1, riser, top=mat)
            elif up == "s": self.box(x0, y0, z1 - (z1 - z0) * t1, x1, y, z1 - (z1 - z0) * t0, riser, top=mat)
            elif up == "e": self.box(x0 + (x1 - x0) * t0, y0, z0, x0 + (x1 - x0) * t1, y, z1, riser, top=mat)
            else: self.box(x1 - (x1 - x0) * t1, y0, z0, x1 - (x1 - x0) * t0, y, z1, riser, top=mat)

    def prism(self, footprint, y0, y1, mat, top=None, solid=True):
        """an upright prism over a convex footprint [(x, z), ...]"""
        top = top or mat
        cx = sum(p[0] for p in footprint) / len(footprint)
        cz = sum(p[1] for p in footprint) / len(footprint)
        n = len(footprint)
        for i in range(n):
            (ax, az), (bx, bz) = footprint[i], footprint[(i + 1) % n]
            out = (((ax + bx) / 2 - cx), 0, ((az + bz) / 2 - cz))
            self.quad((ax, y0, az), (ax, y1, az), (bx, y1, bz), (bx, y0, bz), mat, out)
        for i in range(1, n - 1):
            self.tri((footprint[0][0], y1, footprint[0][1]), (footprint[i][0], y1, footprint[i][1]),
                     (footprint[i + 1][0], y1, footprint[i + 1][1]), top, (0, 1, 0))
            if y0 > 0.001:
                self.tri((footprint[0][0], y0, footprint[0][1]), (footprint[i][0], y0, footprint[i][1]),
                         (footprint[i + 1][0], y0, footprint[i + 1][1]), mat, (0, -1, 0))
        lo = (min(p[0] for p in footprint), y0, min(p[1] for p in footprint))
        hi = (max(p[0] for p in footprint), y1, max(p[1] for p in footprint))
        (self.solids if solid else self.occluders).append((lo, hi))

    def ground(self, x0, z0, x1, z1, cell, colour, y=0.0):
        """paving: a grid of quads, colour(i, j, x, z) -> Mat for each cell"""
        nx, nz = max(1, round((x1 - x0) / cell)), max(1, round((z1 - z0) / cell))
        dx, dz = (x1 - x0) / nx, (z1 - z0) / nz
        for i in range(nx):
            for j in range(nz):
                ax, az = x0 + i * dx, z0 + j * dz
                m = colour(i, j, ax + dx / 2, az + dz / 2)
                if m is None:
                    continue
                self.quad((ax, y, az), (ax, y, az + dz), (ax + dx, y, az + dz), (ax + dx, y, az), m, (0, 1, 0))

    # ---------------------------------------------------------- light and marks

    def lamp(self, pos, rgb, radius=6.0, k=1.0):
        """a light baked into the world (a street lamp, a neon sign)"""
        self.lamps.append((tuple(pos), tuple(c / 255 for c in ((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255)),
                           radius, k))

    def mark(self, name, value):
        self.marks[name] = value

    def nav_node(self, name, x, z, y=0.0):
        """a place on the ground for the bots' paths (links found by mapbake)"""
        self.nav.append((name, (x, y, z)))

    def far_part(self, p, mat):
        self.far.append((p, mat))

    # ---------------------------------------------------------- symmetry

    def mirrored(self, fn):
        """calls fn(s) for s = -1 and 1: the two halves of a symmetric map
        (s = -1 the west side, of team 1)"""
        for s in (-1, 1):
            fn(s)
