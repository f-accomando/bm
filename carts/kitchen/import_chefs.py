#!/usr/bin/env python3
"""Chaos Kitchen: imports the chef models (models/chef1-4.glb, low poly, one
texture each) into the game.

For each chef it drops what the model holds in its hands and the pieces that
make the eyes look hollow, puts clean eyes on the face instead, splits the
model into body, two legs and two arms (the game animates them around the
hips and the shoulders), lowers raised arms, scales it to the chef's height
and re-bakes its texture into a 128x128 atlas.

Writes src/16_chef_models.lua (geometry, texture coordinates in the sheet,
the rig) and models/chefs.png (the four atlases side by side, pasted into
the sheet at y = 128 by mkassets.py). Both are in git, so `make` does not
need this script or its libraries (Pillow and numpy).

  python3 carts/kitchen/import_chefs.py [--preview DIR]
"""
import argparse
import io
import json
import os
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ATLAS = 128                     # texture size of one chef, in sheet pixels
SHEET_Y = 128                   # where the atlases go in the sheet

# Selectors, in the model's own coordinates (y up, the face towards +z):
#   piece(x, y, z)          the connected piece nearest to that point
#   box(x0, x1, y0, y1, z0, z1)   triangles whose centre is inside
#   piece_box(p, b)         the triangles of piece p inside box b
#   colour(rgb, tol, b)     textured triangles inside box b whose texture is
#                           within tol of that colour (wood, a knife blade)
#   part(name, b)           triangles already given to "arm" or "leg", in box b
def piece(x, y, z): return ("piece", (x, y, z))
def box(*b): return ("box", b)
def piece_box(p, b): return ("piece_box", p[1], b[1])
def colour(rgb, tol, b): return ("colour", rgb, tol, b[1])
def part(name, b=box(-9, 9, -9, 9, -9, 9)): return ("part", name, b[1])

# Posing, in the model's own coordinates, before anything else moves:
#   bend(selector, pivot, axis, degrees, towards)
# turns the vertices of the selected triangles around the axis through pivot;
# only those on the `towards` side of the pivot, fully from `blend` away (so
# an elbow bends and the shoulder stays put). towards None: all of them.
def bend(sel, pivot, axis, degrees, towards=None, blend=0.03):
    return (sel, pivot, axis, degrees, towards, blend)

EYE = 0xF8F4EC
PUPIL = 0x1C1418

# Which model is which chef (Data.CHEFS order: Basil, Bun, Noodle, Pepper);
# height: top of the head or hat, in tiles (as Data.CHEFS).
SLAB = box(-0.3, 0.3, -0.31, 0.0, 0.08, 0.4)          # chef 2: in front of the belly
CHEFS = [
    dict(src="chef1.glb", height=1.35,
         drop=[piece(-0.10, -0.36, 0.20),              # the wooden spoon
               piece(-0.04, 0.42, -0.04)],             # a spike on the hat
         eyes=[piece(-0.06, 0.01, 0.17), piece(0.08, 0.01, 0.17)],   # eyes that stick out
         eye=(0.05, 0.055), pupil=(0.026, 0.034),
         legs=[piece(0.10, -0.42, 0.07), piece(-0.07, -0.42, 0.07)],
         arms=[piece(-0.13, -0.27, 0.02),
               piece_box(piece(0.05, -0.26, 0.05), box(0.11, 1, -1, -0.08, -1, 1))],
         pose=[]),
    dict(src="chef2.glb", height=1.05,
         drop=[piece(-0.04, -0.11, 0.26),              # the knife's blade
               colour((145, 85, 65), 45, SLAB),        # the cutting board
               colour((125, 110, 120), 40, SLAB),      # the knife
               box(-0.06, 0.02, -0.16, -0.12, 0.14, 0.2)],   # the board's last face
         eyes=[], eye=None, pupil=None,
         legs=[box(-1, 1, -1, -0.36, -1, 1)],
         arms=[box(0.12, 1, -0.24, -0.03, -1, 1), box(-1, -0.12, -0.24, -0.03, -1, 1)],
         pose=[bend(part("arm", box(0, 1, -1, 1, -1, 1)), (0.2, -0.13, 0.08), (1, 0, 0), 80),
               bend(part("arm", box(-1, 0, -1, 1, -1, 1)), (-0.2, -0.13, 0.08), (1, 0, 0), 80)]),
    dict(src="chef3.glb", height=1.65,
         drop=[piece(-0.16, -0.31, 0.15), piece(-0.19, -0.31, 0.16)],   # the lettuce
         eyes=[piece(0.07, 0.07, 0.16), piece(-0.08, 0.06, 0.17)],     # slivers in hollow eyes
         eye=(0.075, 0.08), pupil=(0.032, 0.042),
         legs=[piece(0.0, -0.40, 0.06), piece(-0.12, -0.47, 0.09)],
         arms=[piece(0.20, -0.16, 0.16), piece(-0.16, -0.22, 0.07)],
         pose=[bend(part("arm", box(0, 1, -1, 1, -1, 1)), (0.07, -0.13, 0.13), (0, 0, 1), -70, (1, 0, 0)),
               bend(part("arm", box(-1, 0, -1, 1, -1, 1)), (-0.05, -0.18, 0.07), (0, 0, 1), 45, (-1, 0, 0))]),
    dict(src="chef4.glb", height=0.92,
         drop=[piece(0.2, 0.25, -0.15), piece(-0.2, 0.08, 0.05)],   # the pan and its handle
         eyes=[], eye=None, pupil=None,
         legs=[box(-1, 1, -1, -0.33, -1, 1)],
         arms=[box(0.18, 1, -0.4, -0.02, -1, 1), box(-1, -0.18, -0.2, 0.14, -1, 1)],
         # the forearm went up to hold the pan's handle: down it comes
         pose=[bend(part("arm", box(-1, 0, -1, 1, -1, 1)), (-0.29, -0.12, 0.05), (0, 0, 1), 152, (-0.86, 0.51, 0))]),
]

# ---------------------------------------------------------------- reading

def load_glb(path):
    d = open(path, "rb").read()
    n, _ = struct.unpack("<II", d[12:20])
    g = json.loads(d[20:20 + n])
    binoff = 20 + n + 8

    def acc(i):
        a = g["accessors"][i]
        bv = g["bufferViews"][a["bufferView"]]
        off = binoff + bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        comps = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a["componentType"]]
        size = np.dtype(dt).itemsize * comps
        stride = bv.get("byteStride") or size
        raw = np.frombuffer(d, np.uint8, (a["count"] - 1) * stride + size, off)
        rows = np.lib.stride_tricks.as_strided(raw, (a["count"], size), (stride, 1))
        return np.frombuffer(rows.tobytes(), dt).reshape(a["count"], comps)

    prim = g["meshes"][0]["primitives"][0]
    pos = acc(prim["attributes"]["POSITION"]).astype(np.float64)
    uv = acc(prim["attributes"]["TEXCOORD_0"]).astype(np.float64)
    idx = acc(prim["indices"]).astype(np.int64).reshape(-1, 3)
    node = g["nodes"][0] if g.get("nodes") else {}
    if "matrix" in node:
        m = np.array(node["matrix"]).reshape(4, 4).T
        pos = (np.c_[pos, np.ones(len(pos))] @ m.T)[:, :3]
    im = g["images"][0]
    bv = g["bufferViews"][im["bufferView"]]
    start = binoff + bv.get("byteOffset", 0)
    tex = Image.open(io.BytesIO(d[start:start + bv["byteLength"]])).convert("RGB")
    return pos, uv, idx, tex


def pieces(pos, idx):
    """connected piece of every triangle (vertices welded by position)"""
    key = {}
    wid = np.array([key.setdefault(tuple(np.round(p, 4)), len(key)) for p in pos])
    parent = list(range(len(key)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for t in idx:
        a, b, c = wid[t]
        for x, y in ((a, b), (b, c)):
            ra, rb = find(x), find(y)
            if ra != rb:
                parent[ra] = rb
    roots = {}
    return np.array([roots.setdefault(find(wid[t[0]]), len(roots)) for t in idx])

# ---------------------------------------------------------------- the model

class Model:
    def __init__(self, path):
        pos, uv, idx, self.tex = load_glb(path)
        self.P = pos[idx]                       # (T, 3 corners, xyz)
        self.UV = uv[idx]                       # (T, 3, uv) in the source texture
        self.piece = pieces(pos, idx)
        src = np.asarray(self.tex, dtype=np.float64)
        h, w = src.shape[:2]
        self.tcol = np.array([np.mean([src[min(h - 1, int(v * h)), min(w - 1, int(u * w))]
                                       for u, v in (np.array(k) @ self.UV[t] for k in
                                                    ((1 / 3, 1 / 3, 1 / 3), (.6, .2, .2), (.2, .6, .2), (.2, .2, .6)))],
                                      axis=0) for t in range(len(idx))])
        self.col = [None] * len(idx)            # None: textured, else a flat colour
        self.part = ["body"] * len(idx)
        self.alive = np.ones(len(idx), bool)

    def centres(self):
        return self.P.mean(axis=1)

    def inside(self, b, whole=False):
        """triangles with their centre in box b (whole: all three corners)"""
        x0, x1, y0, y1, z0, z1 = b
        if whole:
            p = self.P
            ok = (p[:, :, 0] >= x0) & (p[:, :, 0] <= x1) & (p[:, :, 1] >= y0) & (p[:, :, 1] <= y1) & \
                 (p[:, :, 2] >= z0) & (p[:, :, 2] <= z1)
            return ok.all(axis=1)
        c = self.centres()
        return (c[:, 0] >= x0) & (c[:, 0] <= x1) & (c[:, 1] >= y0) & (c[:, 1] <= y1) & \
               (c[:, 2] >= z0) & (c[:, 2] <= z1)

    def select(self, sel, whole=False):
        """whole: boxes take only triangles entirely inside (limbs never take
        the big triangles of the chest)"""
        kind = sel[0]
        c = self.centres()
        if kind == "box":
            return self.inside(sel[1], whole)
        if kind == "colour":
            near = np.linalg.norm(self.tcol - np.array(sel[1]), axis=1) <= sel[2]
            return near & self.inside(sel[3]) & np.array([k is None for k in self.col])
        if kind == "part":
            return np.array([p == sel[1] for p in self.part]) & self.inside(sel[2])
        p = np.array(sel[1])
        best, bd = None, 1e9
        for k in set(self.piece[self.alive].tolist()):
            m = (self.piece == k) & self.alive
            d = np.linalg.norm(c[m].mean(axis=0) - p)
            if d < bd:
                best, bd = k, d
        m = (self.piece == best) & self.alive
        if kind == "piece_box":
            m &= self.inside(sel[2], whole)
        return m

    def keep(self, m):
        self.P, self.UV, self.piece, self.tcol = self.P[m], self.UV[m], self.piece[m], self.tcol[m]
        self.col = [c for c, k in zip(self.col, m) if k]
        self.part = [p for p, k in zip(self.part, m) if k]
        self.alive = self.alive[m]

    def add(self, tris, color):
        for t in tris:
            self.P = np.concatenate([self.P, np.array(t)[None]], axis=0)
            self.UV = np.concatenate([self.UV, np.zeros((1, 3, 2))], axis=0)
            self.piece = np.append(self.piece, -1)
            self.tcol = np.concatenate([self.tcol, np.zeros((1, 3))], axis=0)
            self.col.append(color)
            self.part.append("body")
            self.alive = np.append(self.alive, True)


def rot_axis(axis, a):
    """rotation matrix: angle a (radians) around a unit axis"""
    x, y, z = axis
    c, s, t = np.cos(a), np.sin(a), 1 - np.cos(a)
    return np.array([[t * x * x + c, t * x * y - s * z, t * x * z + s * y],
                     [t * x * y + s * z, t * y * y + c, t * y * z - s * x],
                     [t * x * z - s * y, t * y * z + s * x, t * z * z + c]])


def do_bend(m, op):
    sel, pivot, axis, degrees, towards, blend = op
    tris = m.select(sel)
    pivot = np.array(pivot, float)
    axis = np.array(axis, float) / np.linalg.norm(axis)
    for i in np.nonzero(tris)[0]:
        for k in range(3):
            v = m.P[i, k]
            if towards is None:
                w = 1.0
            else:
                d = np.array(towards, float) / np.linalg.norm(towards)
                w = float(np.clip((v - pivot) @ d / blend, 0, 1))
            if w > 0:
                m.P[i, k] = rot_axis(axis, np.radians(degrees) * w) @ (v - pivot) + pivot


def ray_down_z(P, x, y):
    """the front-most point of triangles P hit by the line (x, y, z), and the
    normal there (towards +z)"""
    best = None
    for t in P:
        a, b, c = t
        # barycentric in the xy plane
        d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(d) < 1e-12:
            continue
        w0 = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / d
        w1 = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / d
        w2 = 1 - w0 - w1
        if min(w0, w1, w2) < -1e-6:
            continue
        z = w0 * a[2] + w1 * b[2] + w2 * c[2]
        n = np.cross(b - a, c - a)
        n /= np.linalg.norm(n) + 1e-12
        if n[2] < 0:
            n = -n
        if best is None or z > best[0]:
            best = (z, n)
    return best


def decal(centre, normal, w, h, lift):
    """two triangles of a w x h quad on the surface, facing `normal`"""
    n = normal / np.linalg.norm(normal)
    up = np.array([0.0, 1.0, 0.0])
    up = up - n * (up @ n)
    up /= np.linalg.norm(up)
    right = np.cross(up, n)
    c = centre + n * lift
    p = [c - right * w / 2 - up * h / 2, c + right * w / 2 - up * h / 2,
         c + right * w / 2 + up * h / 2, c - right * w / 2 + up * h / 2]
    tris = [[p[0], p[1], p[2]], [p[0], p[2], p[3]]]
    # outwards (right-hand rule) along n
    out = []
    for t in tris:
        if np.cross(t[1] - t[0], t[2] - t[0]) @ n < 0:
            t = [t[0], t[2], t[1]]
        out.append(t)
    return out


def build(spec, raw=False):
    m = Model(os.path.join(HERE, "models", spec["src"]))
    # 1. what goes: things in the hands, broken bits, the old eyes
    eye_at = []
    for s in spec["eyes"]:
        sel = m.select(s)
        eye_at.append(m.centres()[sel].mean(axis=0))
        m.keep(~sel)
    for s in spec["drop"]:
        m.keep(~m.select(s))
    # 2. legs and arms (split left and right later)
    for s in spec["legs"]:
        sel = m.select(s)
        for i in np.nonzero(sel)[0]:
            m.part[i] = "leg"
    for s in spec["arms"]:
        sel = m.select(s, whole=True) & np.array([p == "body" for p in m.part])
        for i in np.nonzero(sel)[0]:
            m.part[i] = "arm"
    # arms that were up or holding something come down
    for op in spec["pose"]:
        do_bend(m, op)
    if raw:
        return m
    # 3. the game's x runs the other way (its screen is a mirror of glTF's):
    #    mirror, and keep the faces turned outwards
    m.P[:, :, 0] *= -1
    m.P = m.P[:, [0, 2, 1], :]
    m.UV = m.UV[:, [0, 2, 1], :]
    eye_at = [np.array([-e[0], e[1], e[2]]) for e in eye_at]
    # 4. feet on the floor, the given height, centred on the legs
    lo, hi = m.P[:, :, 1].min(), m.P[:, :, 1].max()
    k = spec["height"] / (hi - lo)
    legs = np.array([p == "leg" for p in m.part])
    cx = m.P[legs][:, :, 0].mean() if legs.any() else 0.0
    cz = m.P[legs][:, :, 2].mean() if legs.any() else 0.0
    shift = np.array([cx, lo, cz])
    m.P = (m.P - shift) * k
    eye_at = [(e - shift) * k for e in eye_at]
    # 5. sides
    c = m.centres()
    for i, p in enumerate(m.part):
        if p in ("leg", "arm"):
            m.part[i] = p + ("L" if c[i, 0] < 0 else "R")
    # 6. pivots: legs at their top, arms at their top (the shoulder)
    piv = {}
    for part in ("legL", "legR", "armL", "armR"):
        sel = np.array([p == part for p in m.part])
        if not sel.any():
            continue
        v = m.P[sel].reshape(-1, 3)
        top = v[:, 1].max()
        near = v[v[:, 1] >= top - 0.2 * (top - v[:, 1].min())]
        piv[part] = np.array([np.median(near[:, 0]), top if part.startswith("leg") else top - 0.03,
                              np.median(near[:, 2])])
    piv.update({k2: np.array(v2) for k2, v2 in spec.get("pivot", {}).items()})
    # 7. new eyes on the face
    if spec["eye"] or spec["pupil"]:
        body = np.array([p == "body" and c is None for p, c in zip(m.part, m.col)])
        face = m.P[body]
        for e in eye_at:
            hit = ray_down_z(face, e[0], e[1])
            if not hit:
                print(f"{spec['src']}: no face under the eye at {e}", file=sys.stderr)
                continue
            z, n = hit
            at = np.array([e[0], e[1], z])
            if spec["eye"]:
                m.add(decal(at, n, *spec["eye"], 0.004), EYE)
            if spec["pupil"]:
                m.add(decal(at, n, *spec["pupil"], 0.008), PUPIL)
    return m, piv

# ---------------------------------------------------------------- texture

def bake(models):
    """each chef's textured triangles into its own ATLAS x ATLAS square, two
    triangles per square cell (one on each side of the diagonal)"""
    sheet = Image.new("RGBA", (ATLAS * len(models), ATLAS), (0, 0, 0, 0))
    out = []
    for ci, (m, _) in enumerate(models):
        src = np.asarray(m.tex, dtype=np.float64)
        sh, sw = src.shape[:2]
        tex = [i for i, c in enumerate(m.col) if c is None]
        area = np.array([np.linalg.norm(np.cross(m.P[i, 1] - m.P[i, 0], m.P[i, 2] - m.P[i, 0])) / 2 for i in tex])
        # detail painted in the source counts too (faces, buttons)
        src_area = np.array([abs(np.cross(np.r_[m.UV[i, 1] - m.UV[i, 0], 0], np.r_[m.UV[i, 2] - m.UV[i, 0], 0])[2]) / 2
                             for i in tex])
        weight = np.sqrt(area / area.sum() + src_area / src_area.sum())
        order = np.argsort(-weight)

        def layout(kk):
            sizes = [int(np.clip(round(kk * weight[j]), 4, 28)) for j in order]
            cells = []
            for a in range(0, len(order), 2):
                s = max(sizes[a:a + 2])
                cells.append((s, [order[a]] + ([order[a + 1]] if a + 1 < len(order) else [])))
            # shelves
            x = y = rowh = 0
            placed = []
            for s, tris in cells:
                if x + s > ATLAS:
                    x, y, rowh = 0, y + rowh, 0
                if y + s > ATLAS:
                    return None
                placed.append((x, y, s, tris))
                x += s
                rowh = max(rowh, s)
            return placed
        lo_k, hi_k = 1.0, 400.0
        for _ in range(30):
            mid = (lo_k + hi_k) / 2
            if layout(mid):
                lo_k = mid
            else:
                hi_k = mid
        placed = layout(lo_k)
        atlas = np.zeros((ATLAS, ATLAS, 3))
        uvs = {}
        for x, y, s, tris in placed:
            for half, j in enumerate(tris):
                i = tex[j]
                if half == 0:       # upper left of the diagonal
                    corners = np.array([[x + 0.5, y + 0.5], [x + s - 1.5, y + 0.5], [x + 0.5, y + s - 1.5]])
                    texels = [(a, b) for a in range(s) for b in range(s) if a + b <= s - 1]
                else:               # lower right, one texel away
                    corners = np.array([[x + s - 0.5, y + s - 0.5], [x + 2.5, y + s - 0.5], [x + s - 0.5, y + 2.5]])
                    texels = [(a, b) for a in range(s) for b in range(s) if a + b >= s]
                uvs[i] = corners
                A = np.array([[corners[0, 0], corners[1, 0], corners[2, 0]],
                              [corners[0, 1], corners[1, 1], corners[2, 1]], [1, 1, 1]])
                Ainv = np.linalg.inv(A)
                for a, b in texels:
                    acc = np.zeros(3)
                    for sy in (0.2, 0.4, 0.6, 0.8):
                        for sx in (0.2, 0.4, 0.6, 0.8):
                            w = Ainv @ np.array([x + a + sx, y + b + sy, 1.0])
                            w = np.clip(w, 0, None)
                            w /= w.sum()
                            su, sv = w @ m.UV[i]
                            acc += src[min(sh - 1, max(0, int(sv * sh))), min(sw - 1, max(0, int(su * sw)))]
                    atlas[y + b, x + a] = acc / 16
        img = Image.fromarray(np.clip(atlas, 0, 255).astype(np.uint8)).convert("RGBA")
        sheet.paste(img, (ci * ATLAS, 0))
        out.append({i: uv + np.array([ci * ATLAS, SHEET_Y]) for i, uv in uvs.items()})
    return sheet, out

# ---------------------------------------------------------------- writing

def lua_part(m, sel, origin, uvs):
    """one part as mesh data around `origin`: vertices welded by position"""
    v, f, uv, key = [], [], [], {}
    for i in np.nonzero(sel)[0]:
        ids = []
        for corner in m.P[i]:
            p = tuple(np.round(corner - origin, 3))
            if p not in key:
                key[p] = len(key) + 1
                v.extend(p)
            ids.append(key[p])
        col = -1 if m.col[i] is None else m.col[i]
        f.extend(ids + [col])
        if m.col[i] is None:
            uv.extend(np.round(uvs[i].ravel(), 1))
        else:
            uv.extend([0] * 6)

    def nums(a):
        return ",".join(("%d" % x) if float(x).is_integer() else ("%g" % x) for x in a)
    return "{ v = {%s},\n      f = {%s},\n      uv = {%s} }" % (nums(v), nums(f), nums(uv))


def write_lua(models, uvs, path):
    lines = ["-- The chefs' 3D models, made by import_chefs.py from models/chef1-4.glb",
             "-- (do not edit by hand). Per chef: the parts as mesh data (vertices, faces",
             "-- with colour -1 for textured, texture coordinates in the sheet) and the rig:",
             "-- hip height, pivots of the legs and arms around the hips' centre.",
             "Data.CHEF_MODEL = {"]
    for (m, piv), uv in zip(models, uvs):
        legs = [piv[p] for p in ("legL", "legR") if p in piv]
        hip = np.mean([p[1] for p in legs]) if legs else 0.4
        origin = np.array([0.0, hip, 0.0])
        parts = np.array(m.part)
        top = m.P[:, :, 1].max()
        body = parts == "body"
        bv = m.P[body].reshape(-1, 3)
        chest = bv[(bv[:, 1] > hip) & (bv[:, 1] < hip + 0.35 * (top - hip))]
        rig = {"hip": hip, "top": top, "width": float(chest[:, 2].max()) if len(chest) else 0.2}
        for p in ("legL", "legR", "armL", "armR"):
            if p in piv:
                rig[p] = piv[p] - origin
        arms = [p for p in ("armL", "armR") if p in piv]
        if arms:
            rig["sh"] = np.mean([rig[p][1] for p in arms])
            rig["shw"] = np.mean([abs(rig[p][0]) for p in arms])
            reach = [piv[p][1] - m.P[parts == p][:, :, 1].min() for p in arms]
            rig["hand"] = float(np.mean(reach))
        if legs:
            rig["hipw"] = np.mean([abs(rig[p][0]) for p in ("legL", "legR") if p in rig])
        lines.append("  {")
        lines.append("    rig = { %s }," % ", ".join(
            ("%s = {%.3f, %.3f, %.3f}" % (k, *v)) if isinstance(v, np.ndarray) else ("%s = %.3f" % (k, v))
            for k, v in rig.items()))
        lines.append("    body = " + lua_part(m, body, origin, uv) + ",")
        for p in ("legL", "legR", "armL", "armR"):
            if p in piv:
                lines.append("    %s = %s," % (p, lua_part(m, parts == p, piv[p], uv)))
        lines.append("  },")
    lines.append("}")
    open(path, "w").write("\n".join(lines) + "\n")

# ---------------------------------------------------------------- previews

def raster(tris, cols, size, view, scale=None, centre=(0, 0.7), pitch=0.98):
    """flat or textured triangles (cols: RGB tuples or (atlas, uv) pairs) seen
    from the game camera (pitch 0.98) or from the side (pitch 0); view = yaw"""
    img = np.full((size, size, 3), 48.0)
    zb = np.full((size, size), 1e9)
    yaw = view
    cy, sy = np.cos(yaw), np.sin(yaw)
    cp, sp = np.cos(pitch), np.sin(pitch)
    k = scale or size / 1.9
    for t, c in zip(tris, cols):
        p = np.array(t)
        x = p[:, 0] * cy + p[:, 2] * sy
        z = -p[:, 0] * sy + p[:, 2] * cy
        y = p[:, 1] - centre[1]
        y2 = y * cp + z * sp
        z2 = -y * sp + z * cp
        xs, ys = size / 2 + x * k, size / 2 - y2 * k
        area = (xs[1] - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (ys[1] - ys[0])
        if area <= 0:           # back face (screen y down, as in r3d.c)
            continue
        n = np.cross(p[1] - p[0], p[2] - p[0])
        n /= np.linalg.norm(n) + 1e-12
        light = np.array([-0.45, 0.85, -0.35])
        light /= np.linalg.norm(light)
        shade = 0.45 + 0.55 * max(0, n @ light)
        x0, x1 = int(max(0, xs.min())), int(min(size - 1, xs.max() + 1))
        y0, y1 = int(max(0, ys.min())), int(min(size - 1, ys.max() + 1))
        if x1 < x0 or y1 < y0:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        w0 = ((xs[1] - gx) * (ys[2] - gy) - (xs[2] - gx) * (ys[1] - gy)) / area
        w1 = ((xs[2] - gx) * (ys[0] - gy) - (xs[0] - gx) * (ys[2] - gy)) / area
        w2 = 1 - w0 - w1
        mk = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        zz = w0 * z2[0] + w1 * z2[1] + w2 * z2[2]
        sub = zb[y0:y1 + 1, x0:x1 + 1]
        mk &= zz < sub
        if not mk.any():
            continue
        if isinstance(c, tuple) and len(c) == 2:
            atlas, uv = c
            u = w0 * uv[0, 0] + w1 * uv[1, 0] + w2 * uv[2, 0]
            v = w0 * uv[0, 1] + w1 * uv[1, 1] + w2 * uv[2, 1]
            col = atlas[np.clip(v.astype(int), 0, atlas.shape[0] - 1), np.clip(u.astype(int), 0, atlas.shape[1] - 1)]
            col = col[:, :, :3] * shade
        else:
            col = np.broadcast_to(np.array(c, float) * shade, mk.shape + (3,))
        blk = img[y0:y1 + 1, x0:x1 + 1]
        blk[mk] = col[mk]
        sub[mk] = zz[mk]
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))


PART_COL = {"body": (200, 200, 200), "legL": (80, 120, 240), "legR": (80, 200, 90),
            "armL": (240, 200, 60), "armR": (230, 80, 80)}


def preview(models, uvs, sheet, outdir):
    os.makedirs(outdir, exist_ok=True)
    full = np.asarray(sheet, dtype=np.float64)
    rows = []
    for ci, ((m, piv), uv) in enumerate(zip(models, uvs)):
        cells = []
        mid = (0, m.P[:, :, 1].max() / 2)
        # parts, from the front and the side
        for yaw in (np.pi, np.pi / 2):
            cols = [PART_COL[p] if c is None else tuple((c >> s) & 255 for s in (16, 8, 0))
                    for p, c in zip(m.part, m.col)]
            cells.append(raster(m.P, cols, 300, yaw, centre=mid, pitch=0.0))
        # textured, as the game draws it (atlas coordinates)
        cols = []
        for i, c in enumerate(m.col):
            if c is None:
                cols.append((full, uv[i] - np.array([0, SHEET_Y])))
            else:
                cols.append(tuple((c >> s) & 255 for s in (16, 8, 0)))
        cells.append(raster(m.P, cols, 300, np.pi, centre=mid, pitch=0.0))
        for yaw in (np.pi, np.pi + 0.8, 0.0):
            cells.append(raster(m.P, cols, 300, yaw, centre=mid))
        # carrying and walking: arms forward, one leg forward
        posed = m.P.copy()
        for p, (ax, ang) in {"armL": ((1, 0, 0), -1.35), "armR": ((1, 0, 0), -1.35),
                             "legL": ((1, 0, 0), -0.6), "legR": ((1, 0, 0), 0.6)}.items():
            if p in piv:
                sel = np.array([q == p for q in m.part])
                R = rot_axis(np.array(ax, float), ang)
                posed[sel] = (posed[sel] - piv[p]) @ R.T + piv[p]
        cells.append(raster(posed, cols, 300, np.pi + 1.2, centre=mid, pitch=0.3))
        # game size (about 70 px)
        small = raster(m.P, cols, 100, np.pi, centre=mid).resize((300, 300), Image.NEAREST)
        cells.append(small)
        row = Image.new("RGB", (300 * len(cells), 300))
        for i, c in enumerate(cells):
            row.paste(c, (i * 300, 0))
        row.save(os.path.join(outdir, f"chef{ci + 1}.png"))
    sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(os.path.join(outdir, "atlas.png"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preview", help="directory for preview images")
    a = ap.parse_args()
    models = [build(s) for s in CHEFS]
    for (m, piv), s in zip(models, CHEFS):
        parts = {p: m.part.count(p) for p in sorted(set(m.part))}
        print(f"{s['src']}: {len(m.part)} triangles {parts}")
    sheet, uvs = bake(models)
    sheet.save(os.path.join(HERE, "models", "chefs.png"))
    write_lua(models, uvs, os.path.join(HERE, "src", "16_chef_models.lua"))
    if a.preview:
        preview(models, uvs, sheet, a.preview)


if __name__ == "__main__":
    main()
