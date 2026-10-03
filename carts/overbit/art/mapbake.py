"""
mapbake.py - a Map (mapgeo.py) made ready for the console:

1. the faces nobody can see go: those against a solid box or under the
   ground;
2. the big faces are cut into smaller triangles (the light is baked at the
   corners, so the corners must not be too far apart);
3. the light is baked at every corner of every face: the low sun with soft
   shadows (a few rays), the sky through the gaps around (ambient
   occlusion: rays over the half sphere; the blocked ones bring back a warm
   bounce), the lamps of the map with their shadows;
4. the triangles are cut into chunks of the ground (squares of `chunk`
   metres): models of the MESH section with the "lit" flag, which the game
   draws only when the camera can see their box.

The result: the models, and the data of the map for the Lua code (chunks,
collision boxes, marks) as Lua source.
"""
import math
import os

import numpy as np

from geo import cross, length, norm, sub
import mapgeo


class Light:
    """the light of a map: the sun (towards it), colours, strengths"""

    def __init__(self, sun_dir, sun_rgb, sun_k, zenith, horizon, sky_k, bounce, bounce_k, ground=0.0):
        self.sun = np.array(norm(sun_dir))
        self.sun_rgb = np.array(sun_rgb) * sun_k
        self.zenith, self.horizon = np.array(zenith), np.array(horizon)
        self.sky_k = sky_k
        self.bounce = np.array(bounce) * bounce_k
        self.ground = ground


# ------------------------------------------------------------------ 1. hidden faces

def drop_hidden(tris, solids, eps=0.02):
    if not tris:
        return tris
    lo = np.array([s[0] for s in solids]) if solids else np.zeros((0, 3))
    hi = np.array([s[1] for s in solids]) if solids else np.zeros((0, 3))
    keep = []
    B = 4096
    for s in range(0, len(tris), B):
        chunk = tris[s:s + B]
        P = np.array([[t[0], t[1], t[2]] for t in chunk])           # (n, 3, 3)
        n = np.array([norm(cross(sub(t[1], t[0]), sub(t[2], t[0]))) for t in chunk])
        c = P.mean(axis=1)
        pts = np.stack([c] + [P[:, k] * 0.94 + c * 0.06 for k in range(3)], axis=1)   # (n, 4, 3)
        pts = pts + n[:, None, :] * eps
        under = (pts[:, :, 1] < -1e-4).all(axis=1)
        inside = np.zeros(len(chunk), bool)
        if len(lo):
            q = pts.reshape(-1, 3)
            ins = np.zeros(len(q), bool)
            for b0 in range(0, len(lo), 512):
                l, h = lo[b0:b0 + 512], hi[b0:b0 + 512]
                ins |= ((q[:, None, :] > l[None] + 1e-4) & (q[:, None, :] < h[None] - 1e-4)).all(axis=2).any(axis=1)
            inside = ins.reshape(-1, 4).all(axis=1)
        for t, u, i in zip(chunk, under, inside):
            if not (u or i):
                keep.append(t)
    return keep


# ------------------------------------------------------------------ 2. smaller triangles

def subdivide(tris, max_edge):
    out = []
    stack = list(tris)
    while stack:
        t = stack.pop()
        a, b, c, na, nb, nc, m = t
        e = [length(sub(b, a)), length(sub(c, b)), length(sub(a, c))]
        k = max(range(3), key=lambda i: e[i])
        lim = max_edge * (2.5 if m.glow else 1.0) * (5.0 if m.thin else 1.0)
        if e[k] <= lim:
            out.append(t)
            continue
        # cut the longest edge in two
        P, N = [a, b, c], [na, nb, nc]
        i, j, o = k, (k + 1) % 3, (k + 2) % 3
        mid = tuple((P[i][q] + P[j][q]) / 2 for q in range(3))
        nm = norm(tuple(N[i][q] + N[j][q] for q in range(3)))
        stack.append((P[i], mid, P[o], N[i], nm, N[o], m))
        stack.append((mid, P[j], P[o], nm, N[j], N[o], m))
    return out


# ------------------------------------------------------------------ 3. light

def _hits(O, D, maxd, lo, hi):
    """for each ray (O, D unit), is a box hit before maxd? (bool array)"""
    M = len(O)
    hit = np.zeros(M, bool)
    if not len(lo):
        return hit
    inv = 1.0 / np.where(np.abs(D) < 1e-9, 1e-9, D)
    for b0 in range(0, len(lo), 256):
        l, h = lo[b0:b0 + 256], hi[b0:b0 + 256]
        t1 = (l[None] - O[:, None, :]) * inv[:, None, :]
        t2 = (h[None] - O[:, None, :]) * inv[:, None, :]
        tn = np.minimum(t1, t2).max(axis=2)
        tf = np.maximum(t1, t2).min(axis=2)
        hit |= ((tf >= np.maximum(tn, 0.0)) & (tn < maxd[:, None])).any(axis=1)
    return hit


def _ground_hit(O, D, maxd):
    with np.errstate(divide="ignore", invalid="ignore"):
        t = np.where(D[:, 1] < -1e-6, -O[:, 1] / D[:, 1], np.inf)
    return t < maxd


def _hemisphere(n_rays, seed=1):
    """cosine-weighted directions around +y (a fixed spiral)"""
    out = []
    ga = math.pi * (3 - math.sqrt(5))
    for i in range(n_rays):
        r = math.sqrt((i + 0.5) / n_rays)
        a = i * ga
        out.append((r * math.cos(a), math.sqrt(max(0.0, 1 - r * r)), r * math.sin(a)))
    return np.array(out)


def _frame(n):
    """two tangents for each normal (rows)"""
    up = np.where(np.abs(n[:, 1:2]) < 0.9, np.array([[0.0, 1.0, 0.0]]), np.array([[1.0, 0.0, 0.0]]))
    t = np.cross(up, n)
    t /= np.linalg.norm(t, axis=1, keepdims=True)
    b = np.cross(n, t)
    return t, b


def bake(tris, boxes, lamps, light, verbose=False, **kw):
    """the light at each corner of a list of triangles: an (n, 3, 3) array"""
    P = np.array([[t[0], t[1], t[2]] for t in tris], float).reshape(-1, 3)
    N = np.array([[t[3], t[4], t[5]] for t in tris], float).reshape(-1, 3)
    glow = np.repeat(np.array([t[6].glow for t in tris]), 3)
    L = bake_points(P, N, boxes, lamps, light, **kw)
    L[glow] = 1.0
    if verbose:
        print(f"  baked {len(P)} corners, {len(boxes)} boxes, {len(lamps)} lamps")
    return L.reshape(len(tris), 3, 3)


def bake_points(P, N, boxes, lamps, light, ao_rays=12, ao_dist=4.5, sun_rays=4, cell=8.0):
    """the light at points P with normals N (arrays (m, 3)): (m, 3) rgb"""
    lo = np.array([b[0] for b in boxes], float) if boxes else np.zeros((0, 3))
    hi = np.array([b[1] for b in boxes], float) if boxes else np.zeros((0, 3))
    O = P + N * 0.03
    L = np.zeros_like(P)
    if not len(P):
        return L
    keys = np.floor(P[:, [0, 2]] / cell).astype(int)
    cells = {}
    for i, k in enumerate(map(tuple, keys)):
        cells.setdefault(k, []).append(i)
    hemi = _hemisphere(ao_rays)
    sun = light.sun
    st, sb = _frame(sun[None])
    jit = [(0, 0)] + [(math.cos(a) * 0.026, math.sin(a) * 0.026) for a in np.linspace(0, 2 * math.pi, max(1, sun_rays - 1), endpoint=False)]
    sun_dirs = np.array([norm(tuple(sun + st[0] * u + sb[0] * v)) for u, v in jit[:sun_rays]])
    for k, idx in cells.items():
        idx = np.array(idx)
        if len(lo):
            c0 = np.array([k[0] * cell, k[1] * cell]) - ao_dist - 0.5
            c1 = np.array([(k[0] + 1) * cell, (k[1] + 1) * cell]) + ao_dist + 0.5
            near = ((hi[:, [0, 2]] >= c0) & (lo[:, [0, 2]] <= c1)).all(axis=1)
            nlo, nhi = lo[near], hi[near]
        else:
            nlo, nhi = lo, hi
        o, nn = O[idx], N[idx]
        m = len(idx)
        t, b = _frame(nn)
        sky = np.zeros((m, 3))
        for d in hemi:
            D = t * d[0] + nn * d[1] + b * d[2]
            D /= np.linalg.norm(D, axis=1, keepdims=True)
            maxd = np.full(m, ao_dist)
            blocked = _hits(o, D, maxd, nlo, nhi) | _ground_hit(o, D, maxd)
            up = np.clip(D[:, 1], -1, 1)[:, None]
            sky_c = light.horizon + (light.zenith - light.horizon) * np.clip(up, 0, 1)
            sky_c = np.where(up < 0, light.bounce * 0.9, sky_c)            # from below: the warm ground
            sky += np.where(blocked[:, None], light.bounce * 0.35, sky_c)
        sky *= light.sky_k / len(hemi)
        ndl = np.clip(nn @ sun, 0, 1)
        vis = np.zeros(m)
        lit = ndl > 0
        if lit.any():
            for D in sun_dirs:
                Dm = np.repeat(D[None], lit.sum(), axis=0)
                maxd = np.full(lit.sum(), 80.0)
                vis[lit] += ~_hits(o[lit], Dm, maxd, lo, hi)
            vis /= len(sun_dirs)
        L[idx] = sky + (ndl * vis)[:, None] * light.sun_rgb[None]
    for pos, rgb, radius, kk in lamps:
        p = np.array(pos)
        d = p[None] - O
        dist = np.linalg.norm(d, axis=1)
        sel = dist < radius
        if not sel.any():
            continue
        D = d[sel] / np.maximum(dist[sel], 1e-6)[:, None]
        ndl = np.clip((N[sel] * D).sum(axis=1), 0, 1)
        fall = (1 - dist[sel] / radius) ** 2
        near = ((hi >= p - radius) & (lo <= p + radius)).all(axis=1) if len(lo) else np.zeros(0, bool)
        shadow = _hits(O[sel], D, dist[sel] - 0.15, lo[near], hi[near]) if len(lo) else np.zeros(sel.sum(), bool)
        L[sel] += (kk * ndl * fall * ~shadow)[:, None] * np.array(rgb)[None]
    return L


def refine(tris, boxes, lamps, light, coarse=6.0, rounds=0, contrast=0.3, min_edge=1.1, verbose=False):
    """triangles cut where the light changes: start with edges up to
    `coarse` metres, bake, then cut the longest edge of every triangle whose
    corners differ by more than `contrast` (and is longer than `min_edge`),
    bake the new corners, again `rounds` times. An edge is cut in every
    triangle that has it (no T-junctions: no cracks between them)."""
    tris = subdivide(tris, coarse)
    if verbose:
        print(f"  {len(tris)} triangles with edges up to {coarse} m")
    # vertices by position; triangles as (ia, ib, ic, na, nb, nc, mat)
    vid, verts = {}, []

    def V(p):
        key = (round(p[0], 4), round(p[1], 4), round(p[2], 4))
        if key not in vid:
            vid[key] = len(verts)
            verts.append(key)
        return vid[key]
    T = [[V(t[0]), V(t[1]), V(t[2]), t[3], t[4], t[5], t[6]] for t in tris]
    light_of = {}                                   # (tri id, corner) -> rgb

    def bake_tris(ids):
        pts, nrm, keys = [], [], []
        for i in ids:
            t = T[i]
            for k in range(3):
                if (i, k) not in light_of:
                    pts.append(verts[t[k]])
                    nrm.append(t[3 + k])
                    keys.append((i, k))
        if not pts:
            return
        Lp = bake_points(np.array(pts, float), np.array(nrm, float), boxes, lamps, light)
        for key, l, (i, k) in zip(keys, Lp, keys):
            light_of[(i, k)] = l if not T[i][6].glow else np.ones(3)

    bake_tris(range(len(T)))
    for rnd in range(rounds):
        # the edges to cut
        edges = {}
        for i, t in enumerate(T):
            for k in range(3):
                e = tuple(sorted((t[k], t[(k + 1) % 3])))
                edges.setdefault(e, []).append(i)
        cut = set()
        for i, t in enumerate(T):
            if t[6].glow:
                continue
            ls = np.array([light_of[(i, k)] for k in range(3)])
            if (ls.max(axis=0) - ls.min(axis=0)).max() <= contrast:
                continue
            P = [verts[t[k]] for k in range(3)]
            el = [math.dist(P[k], P[(k + 1) % 3]) for k in range(3)]
            k = max(range(3), key=lambda q: el[q])
            if el[k] > min_edge:
                cut.add(tuple(sorted((t[k], t[(k + 1) % 3]))))
        if not cut:
            break
        # cut: every triangle with a cut edge is split on one of them (the
        # longest cut edge it has); the rest of its cut edges wait a round
        new_ids = []
        for i in range(len(T)):
            t = T[i]
            best = None
            for k in range(3):
                e = tuple(sorted((t[k], t[(k + 1) % 3])))
                if e in cut:
                    l = math.dist(verts[e[0]], verts[e[1]])
                    if best is None or l > best[1]:
                        best = (k, l)
            if best is None:
                continue
            k = best[0]
            a, b, o = k, (k + 1) % 3, (k + 2) % 3
            pa, pb = verts[t[a]], verts[t[b]]
            m = V(tuple((pa[q] + pb[q]) / 2 for q in range(3)))
            na, nb, no = t[3 + a], t[3 + b], t[3 + o]
            nm = norm(tuple(na[q] + nb[q] for q in range(3)))
            mat = t[6]
            la, lb, lo_ = light_of.pop((i, a)), light_of.pop((i, b)), light_of.pop((i, o))
            light_of.pop((i, 0), None), light_of.pop((i, 1), None), light_of.pop((i, 2), None)
            T[i] = [t[a], m, t[o], na, nm, no, mat]
            light_of[(i, 0)], light_of[(i, 2)] = la, lo_
            j = len(T)
            T.append([m, t[b], t[o], nm, nb, no, mat])
            light_of[(j, 1)], light_of[(j, 2)] = lb, lo_
            new_ids += [i, j]
        # a triangle split on another edge than one cut by its neighbour
        # leaves a T: cut it again next round (its edge is still marked)
        bake_tris(new_ids)
        if verbose:
            print(f"  round {rnd + 1}: {len(cut)} edges cut, {len(T)} triangles")
    # conforming: any edge still with a vertex in its middle? (a T-junction)
    # split those triangles too, until there is none
    for _ in range(8):
        mids = {}
        tset = set()
        for i, t in enumerate(T):
            for k in range(3):
                tset.add(tuple(sorted((t[k], t[(k + 1) % 3]))))
        bad = []
        for i, t in enumerate(T):
            for k in range(3):
                a, b = t[k], t[(k + 1) % 3]
                pa, pb = verts[a], verts[b]
                key = (round((pa[0] + pb[0]) / 2, 4), round((pa[1] + pb[1]) / 2, 4), round((pa[2] + pb[2]) / 2, 4))
                if key in vid:
                    m = vid[key]
                    if (tuple(sorted((a, m))) in tset) and (tuple(sorted((m, b))) in tset):
                        bad.append((i, k, m))
                        break
        if not bad:
            break
        new_ids = []
        for i, k, m in bad:
            t = T[i]
            a, b, o = k, (k + 1) % 3, (k + 2) % 3
            na, nb, no = t[3 + a], t[3 + b], t[3 + o]
            nm = norm(tuple(na[q] + nb[q] for q in range(3)))
            mat = t[6]
            la, lb, lo_ = light_of.pop((i, a)), light_of.pop((i, b)), light_of.pop((i, o))
            T[i] = [t[a], m, t[o], na, nm, no, mat]
            light_of[(i, 0)], light_of[(i, 2)] = la, lo_
            j = len(T)
            T.append([m, t[b], t[o], nm, nb, no, mat])
            light_of[(j, 1)], light_of[(j, 2)] = lb, lo_
            new_ids += [i, j]
        bake_tris(new_ids)
    out = [(verts[t[0]], verts[t[1]], verts[t[2]], t[3], t[4], t[5], t[6]) for t in T]
    L = np.array([[light_of[(i, k)] for k in range(3)] for i in range(len(T))])
    if verbose:
        print(f"  {len(out)} triangles after refining")
    return out, L


# ------------------------------------------------------------------ 4. chunks

def chunks(tris, light, name, size, lit_scale=128):
    """triangles -> lit models by square of the ground; returns
    [(model dict, bbox)]"""
    groups = {}
    for t, l in zip(tris, light):
        c = [(t[0][q] + t[1][q] + t[2][q]) / 3 for q in range(3)]
        k = (math.floor(c[0] / size), math.floor(c[2] / size))
        groups.setdefault(k, []).append((t, l))
    out = []
    for i, (k, items) in enumerate(sorted(groups.items())):
        # a model can hold 4096 corners and 16384 faces: split if needed
        for part in range(0, len(items), 5000):
            sub_items = items[part:part + 5000]
            verts, index, faces = [], {}, []
            lo, hi = [1e9] * 3, [-1e9] * 3
            # the pictures first: drawn before the walls behind them, they win
            # where the depth is the same far away
            sub_items.sort(key=lambda it: len(it[0]) <= 7)
            for t, l in sub_items:
                ids = []
                for q in range(3):
                    p = tuple(round(v, 4) for v in t[q])
                    if p not in index:
                        index[p] = len(verts)
                        verts.append(p)
                    ids.append(index[p])
                    for a in range(3):
                        lo[a], hi[a] = min(lo[a], p[a]), max(hi[a], p[a])
                rgb = [max(0, min(255, int(round(x * lit_scale)))) for x in l.reshape(-1)]
                faces.append((ids[0], ids[1], ids[2], t[6].bits(), t[7] if len(t) > 7 else None, rgb))
            if len(verts) > 4096:
                raise ValueError(f"{name}: a chunk with {len(verts)} corners: make the chunks smaller")
            nm = f"{name}{len(out):02d}"
            out.append(({"name": nm, "verts": verts, "faces": faces, "lit": True}, (lo, hi)))
    return out


# ------------------------------------------------------------------ 5. what can be seen from where

def pvs(mp, models, tool, cell=4.0, heights=(0.6, 1.8, 4.4), max_y=8.0, verbose=False):
    """the potentially visible sets: the map's ground in squares of `cell`
    metres; from points in each square (at a few heights) the C tool
    (tools/mappvs.c) draws the chunks in colours that are their numbers and
    keeps the ones that show. Each square's set is joined with its four
    neighbours' (no holes at the borders). Returns the data for the Lua code."""
    import subprocess
    import tempfile
    import bmmesh
    import mkbm
    near = [(m, bb) for m, bb in models if "far" not in m["name"]]
    names = [m["name"] for m, _ in near]
    x0 = math.floor(min(bb[0][0] for _, bb in near) / cell) * cell
    z0 = math.floor(min(bb[0][2] for _, bb in near) / cell) * cell
    x1 = math.ceil(max(bb[1][0] for _, bb in near) / cell) * cell
    z1 = math.ceil(max(bb[1][2] for _, bb in near) / cell) * cell
    nx, nz = int(round((x1 - x0) / cell)), int(round((z1 - z0) / cell))
    lo = np.array([b[0] for b in mp.solids]) if mp.solids else np.zeros((0, 3))
    hi = np.array([b[1] for b in mp.solids]) if mp.solids else np.zeros((0, 3))

    def inside(p):
        if not len(lo):
            return False
        q = np.array(p)
        return bool(((q > lo - 0.15) & (q < hi + 0.15)).all(axis=1).any())
    lines = []
    for j in range(nz):
        for i in range(nx):
            c = j * nx + i
            for u in (0.28, 0.72):
                for v in (0.28, 0.72):
                    for h in heights:
                        x, z = x0 + (i + u) * cell, z0 + (j + v) * cell
                        # on top of whatever is under (a step, a platform)
                        y = h
                        for b0, b1 in mp.solids:
                            if b0[0] <= x <= b1[0] and b0[2] <= z <= b1[2] and b1[1] <= 4.5 and b1[1] > 0.05:
                                y = max(y, b1[1] + h)
                        if not inside((x, y, z)):
                            lines.append(f"{c} {x:.3f} {y:.3f} {z:.3f}")
    with tempfile.TemporaryDirectory() as td:
        mesh = bmmesh.encode([m for m, _ in near], inset=0)
        cart = os.path.join(td, "map.bm")
        with open(cart, "wb") as f:
            f.write(mkbm.pack(b"-- map\n", title="map", author="bm", res=(320, 180), mesh=mesh))
        with open(os.path.join(td, "in.txt"), "w") as f:
            f.write(f"chunks {len(names)}\n" + "\n".join(names) + f"\nsamples {len(lines)}\n" + "\n".join(lines) + "\n")
        subprocess.run([tool, cart, os.path.join(td, "in.txt"), os.path.join(td, "out.txt")], check=True)
        sets = {}
        for line in open(os.path.join(td, "out.txt")):
            v = [int(t) for t in line.split()]
            sets[v[0]] = set(v[1:])
    out = {}
    for j in range(nz):
        for i in range(nx):
            c = j * nx + i
            if c not in sets:
                continue
            u = set(sets[c])
            for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                k = (j + dj) * nx + (i + di)
                if 0 <= i + di < nx and 0 <= j + dj < nz and k in sets:
                    u |= sets[k]
            out[c] = sorted(u)
    if verbose:
        avg = sum(len(v) for v in out.values()) / max(1, len(out))
        print(f"  visibility: {nx} x {nz} squares, {len(lines)} points, {avg:.1f} of {len(names)} chunks seen on average")
    return {"x0": x0, "z0": z0, "cell": cell, "nx": nx, "nz": nz, "max_y": max_y, "sets": out}


# ------------------------------------------------------------------ 6. where the bots can walk

def navgraph(mp, reach=17.0, step=0.25, climb=0.5, body=(0.35, 1.7), side=0.45, verbose=False):
    """the links between the map's nav nodes: b is a neighbour of a when a
    body can walk from a to b in a straight line: no solid box in the way
    (on the line and `side` metres to each side, from `body` over the floor),
    the floor never rising more than `climb` between two samples (stairs and
    ramps are small steps; a drop is fine: some links go one way only)."""
    nodes = [p for _, p in mp.nav]
    lo = np.array([b[0] for b in mp.solids], float)
    hi = np.array([b[1] for b in mp.solids], float)

    def floor_at(x, z, y):
        """the top of what is under (x, z), no higher than y + climb"""
        inside = (lo[:, 0] <= x) & (x <= hi[:, 0]) & (lo[:, 2] <= z) & (z <= hi[:, 2]) & (hi[:, 1] <= y + climb)
        return max(0.0, hi[inside, 1].max()) if inside.any() else 0.0

    def free(x, y, z):
        q = np.array([x, y, z])
        ins = ((q > lo) & (q < hi)).all(axis=1)
        return not ins.any()

    def walk(a, b):
        ax, ay, az = a
        bx, by, bz = b
        d = math.hypot(bx - ax, bz - az)
        if d < 1e-6 or d > reach:
            return False
        n = max(2, int(d / step))
        ux, uz = (bx - ax) / d, (bz - az) / d
        y = ay
        for i in range(1, n + 1):
            t = i / n
            x, z = ax + (bx - ax) * t, az + (bz - az) * t
            f = floor_at(x, z, y)
            if f > y + climb:
                return False
            y = f
            for off in (-side, 0.0, side):
                px, pz = x - uz * off, z + ux * off
                for h in body:
                    if not free(px, y + h, pz):
                        return False
        return abs(y - by) < 0.6
    links = [[] for _ in nodes]
    for i, a in enumerate(nodes):
        for j, b in enumerate(nodes):
            if i != j and walk(a, b):
                links[i].append(j)
    if verbose:
        lone = [mp.nav[i][0] for i, l in enumerate(links) if not l]
        print(f"  nav: {len(nodes)} nodes, {sum(len(l) for l in links)} links" + (f", alone: {lone}" if lone else ""))
    return {"names": [n for n, _ in mp.nav], "nodes": nodes, "links": links}


# ------------------------------------------------------------------ all together

def build(mp, light, chunk=8.0, max_edge=6.0, verbose=True, pvs_tool=None):
    tris = drop_hidden(mp.tris, mp.solids)
    if verbose:
        print(f"{mp.name}: {len(mp.tris)} triangles, {len(mp.tris) - len(tris)} hidden")
    tris, L = refine(tris, mp.solids + mp.occluders, mp.lamps, light, coarse=max_edge, verbose=verbose)
    if mp.decals:
        # the pictures: one light for each, at its middle
        D = mp.decals
        C = np.array([[(t[0][q] + t[1][q] + t[2][q]) / 3 for q in range(3)] for t in D], float)
        Ld = bake_points(C, np.array([t[3] for t in D], float), mp.solids + mp.occluders, mp.lamps, light)
        Ld[np.array([t[6].glow for t in D])] = 1.0
        # the two triangles of a quad: the same light (the one of the first)
        for i in range(1, len(D)):
            if D[i][6] is D[i - 1][6] and D[i][0] == D[i - 1][0]:
                Ld[i] = Ld[i - 1]
        tris = list(tris) + D
        L = np.concatenate([L, np.repeat(Ld[:, None, :], 3, axis=1)])
        if verbose:
            print(f"  {len(D) // 2} pictures")
    models = chunks(tris, L, mp.name, chunk)
    # the far scenery: lit by the sky and the sun, no shadows, one model
    far = []
    for p, m in mp.far:
        tmp = mapgeo.Map("far")
        tmp.part(p, m)
        far += tmp.tris
    if far:
        Lf = bake(far, [], [], light, ao_rays=4, sun_rays=1)
        fm = chunks(far, Lf, mp.name + "far", 1e6)
        models += fm
    if verbose:
        print(f"  {len(models)} models, {sum(len(m['faces']) for m, _ in models)} faces")
    vis = pvs(mp, models, pvs_tool, verbose=verbose) if pvs_tool else None
    nav = navgraph(mp, verbose=verbose) if mp.nav else None
    return models, lua_data(mp, models, vis, nav)


def lua_data(mp, models, vis=None, nav=None):
    """the map for the Lua code: World.MAP = { chunks, boxes, marks, pvs, nav }"""
    out = ["-- generated by carts/overbit/art/mapbake.py: do not edit", "World.MAP = {",
           f'  name = "{mp.name}",', "  chunks = {"]
    for m, (lo, hi) in models:
        far = "far" in m["name"]
        out.append(f'    {{ "{m["name"]}", {lo[0]:.2f}, {lo[1]:.2f}, {lo[2]:.2f}, {hi[0]:.2f}, {hi[1]:.2f}, {hi[2]:.2f}'
                   f'{", far = true" if far else ""} }},')
    out.append("  },")
    out.append("  boxes = {")
    for lo, hi in mp.solids:
        out.append("    " + ", ".join(f"{v:.3f}".rstrip("0").rstrip(".") for v in (*lo, *hi)) + ",")
    out.append("  },")
    out.append("  marks = {")
    for k, v in sorted(mp.marks.items()):
        out.append(f"    {k} = {to_lua(v)},")
    out.append("  },")
    if vis:
        # each square's set as a string: one byte per chunk seen (its number + 1)
        out.append(f"  pvs = {{ x0 = {vis['x0']}, z0 = {vis['z0']}, cell = {vis['cell']}, nx = {vis['nx']}, "
                   f"nz = {vis['nz']}, max_y = {vis['max_y']},")
        out.append("    sets = {")
        for c, ids in sorted(vis["sets"].items()):
            bs = "".join(f"\\{i + 1}" for i in ids)
            out.append(f'      [{c + 1}] = "{bs}",')
        out.append("    },")
        out.append("  },")
    if nav:
        # nodes: x, y, z in a row; links: the neighbours of each node (1-based)
        out.append("  nav = {")
        out.append("    names = { " + ", ".join(f'"{n}"' for n in nav["names"]) + " },")
        out.append("    nodes = { " + ", ".join(f"{to_lua(float(v))}" for p in nav["nodes"] for v in p) + " },")
        out.append("    links = {")
        for l in nav["links"]:
            out.append("      { " + ", ".join(str(j + 1) for j in l) + " },")
        out.append("    },")
        out.append("  },")
    out.append("}")
    return "\n".join(out) + "\n"


def to_lua(v):
    if isinstance(v, dict):
        return "{ " + ", ".join(f"{k} = {to_lua(x)}" for k, x in v.items()) + " }"
    if isinstance(v, (list, tuple)):
        return "{ " + ", ".join(to_lua(x) for x in v) + " }"
    if isinstance(v, str):
        return f'"{v}"'
    if isinstance(v, float):
        return f"{v:.3f}".rstrip("0").rstrip(".")
    return str(v)
