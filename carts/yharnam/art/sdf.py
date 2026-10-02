"""A tiny SDF renderer that turns simple 3D models into 16-bit pixel art.

Models are unions of primitives (signed distance functions) placed on the
bones of a skeleton. They are ray-marched with an orthographic camera that
looks down at ELEV degrees (the 3/4 top-down view of the game), 3x3 samples
per pixel, then turned into pixel art: cel shading on the ramps of each
material, contour lines where a part passes in front of another, lone pixels
merged into their neighbours, a dark outline. Everything the game shows in
3D (the hunter, lamps, statues, braziers...) goes through here, so they all
share one camera and one light.
"""
import math
import numpy as np

D = math.radians
# Bump when a change here changes the pictures: the caches of rendered frames
# (mkassets.py) are keyed on it, not on this file.
SDF_VERSION = 1
ELEV = D(36)                                   # the camera looks down at this angle
INK = (24, 16, 32)                             # outline


def nrm(v):
    v = np.asarray(v, float)
    return v / np.linalg.norm(v)


LIGHT_W = nrm([-0.85, -0.55, 0.75])            # from the upper left of the screen, towards the viewer
RIM_W = nrm([0.7, 0.3, 0.4])                   # a cold edge light from the right, behind
# the creatures drawn for one side and mirrored for the other are lit the
# same on both: from above, a little towards the viewer, an edge light behind
LIGHT_TOP = nrm([0.0, -0.5, 0.86])
RIM_TOP = nrm([0.0, 0.8, 0.45])


def rx(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def ry(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def rz(a):
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


# ---------------------------------------------------------------- materials

class Mat:
    def __init__(self, i, name, ramp, cuts, weight=1.0, detail=False, keep=False,
                 noline=False, metal=False, glow=False):
        self.i, self.name, self.ramp, self.cuts = i, name, [tuple(c) for c in ramp], cuts
        self.weight, self.detail, self.keep, self.noline = weight, detail, keep, noline
        self.metal, self.glow = metal, glow


MATS = []
BYNAME = {}


def material(name, ramp, cuts, **kw):
    """ramp: colours dark -> light; cuts: light thresholds between them"""
    assert len(cuts) == len(ramp) - 1, name
    for c in ramp:
        assert all(v % 8 == 0 for v in c), (name, c)
    m = Mat(len(MATS), name, ramp, cuts, **kw)
    MATS.append(m)
    BYNAME[name] = m
    return m.i


# ---------------------------------------------------------------- SDFs

def sd_ellipsoid(q, r):
    r = np.asarray(r, float)
    k0 = np.sqrt(((q / r) ** 2).sum(1))
    k1 = np.sqrt(((q / (r * r)) ** 2).sum(1))
    return k0 * (k0 - 1.0) / np.maximum(k1, 1e-9)


def sd_sphere(q, c, r):
    return np.sqrt(((q - np.asarray(c, float)) ** 2).sum(1)) - r


def sd_roundcone(q, a, b, r1, r2):
    a = np.asarray(a, float)
    b = np.asarray(b, float)
    ba = b - a
    l2 = ba @ ba
    rr = r1 - r2
    a2 = l2 - rr * rr
    il2 = 1.0 / l2
    pa = q - a
    y = pa @ ba
    z = y - l2
    x = pa * l2 - y[:, None] * ba
    x2 = (x * x).sum(1)
    y2 = y * y * l2
    z2 = z * z * l2
    k = np.sign(rr) * rr * rr * x2
    rb = np.sqrt(x2 + z2) * il2 - r2
    ra = np.sqrt(x2 + y2) * il2 - r1
    rm = (np.sqrt(x2 * a2 * il2) + y * rr) * il2 - r1
    return np.where(np.sign(z) * a2 * z2 > k, rb, np.where(np.sign(y) * a2 * y2 < k, ra, rm))


def sd_capsule(q, a, b, r):
    return sd_roundcone(q, a, b, r, r)


def sd_cone(q, zb, zt, rb, rt):
    """capped cone along z: radius rb at z = zb, rt at z = zt (zb < zt)"""
    h = (zt - zb) / 2
    qx = np.sqrt(q[:, 0] ** 2 + q[:, 1] ** 2)
    qy = q[:, 2] - (zt + zb) / 2
    k1x, k1y = rt, h
    k2x, k2y = rt - rb, 2 * h
    cax = qx - np.minimum(qx, np.where(qy < 0, rb, rt))
    cay = np.abs(qy) - h
    tt = np.clip(((k1x - qx) * k2x + (k1y - qy) * k2y) / (k2x * k2x + k2y * k2y), 0, 1)
    cbx = qx - k1x + k2x * tt
    cby = qy - k1y + k2y * tt
    s = np.where((cbx < 0) & (cay < 0), -1.0, 1.0)
    return s * np.sqrt(np.minimum(cax * cax + cay * cay, cbx * cbx + cby * cby))


def sd_cyl(q, c, r, zb, zt):
    """vertical cylinder at (cx, cy), z from zb to zt"""
    dx = np.sqrt((q[:, 0] - c[0]) ** 2 + (q[:, 1] - c[1]) ** 2) - r
    dz = np.maximum(zb - q[:, 2], q[:, 2] - zt)
    return np.minimum(np.maximum(dx, dz), 0) + np.sqrt(np.maximum(dx, 0) ** 2 + np.maximum(dz, 0) ** 2)


def sd_box(q, b, r=0.0):
    d = np.abs(q) - (np.asarray(b, float) - r)
    return np.sqrt((np.maximum(d, 0) ** 2).sum(1)) + np.minimum(d.max(1), 0) - r


def sd_boxat(q, c, b, r=0.0):
    return sd_box(q - np.asarray(c, float), b, r)


def sd_torus_z(q, c, R, r):
    """torus around the vertical axis through c"""
    p = q - np.asarray(c, float)
    qx = np.sqrt(p[:, 0] ** 2 + p[:, 1] ** 2) - R
    return np.sqrt(qx * qx + p[:, 2] ** 2) - r


def sd_tri(px, py, r):
    """equilateral triangle in 2D, a corner towards +y"""
    k = math.sqrt(3.0)
    px = np.abs(px) - r
    py = py + r / k
    m = px + k * py > 0
    nx = np.where(m, (px - k * py) / 2, px)
    ny = np.where(m, (-k * px - py) / 2, py)
    nx = nx - np.clip(nx, -2 * r, 0)
    return -np.sqrt(nx * nx + ny * ny) * np.sign(ny)


def hash2(a, b):
    v = np.sin(a * 12.9898 + b * 78.233) * 43758.5453
    return v - np.floor(v)


def jag(ang, n, amp, seed=0.0):
    """ragged hem: a few uneven teeth around a circle"""
    s = ang * n / (2 * math.pi) + seed
    i = np.floor(s)
    f = s - i
    h = hash2(i, seed * 3.1 + 1.7)
    tooth = np.abs(f - 0.5) * 2
    return amp * (0.35 + 0.65 * h) * (1 - tooth) - amp * 0.2


# ---------------------------------------------------------------- skeleton

class Skel:
    def __init__(self):
        self.b = {'root': (np.eye(3), np.zeros(3))}

    def add(self, name, parent, off, R):
        off = np.asarray(off, float)
        Rp, tp = self.b[parent]
        self.b[name] = (Rp @ R, tp + Rp @ off)

    def pt(self, name, p):
        R, t = self.b[name]
        return t + R @ np.asarray(p, float)


class Prim:
    __slots__ = ('bone', 'f', 'mat', 'matf', 'group', 'R', 't', 'tex', '_scaled')

    def __init__(self, bone, f, mat, group, matf=None, tex=None):
        self.bone, self.f, self.mat, self.group, self.matf = bone, f, mat, group, matf
        self.tex = tex              # q -> light offset (texture: joints, grain...)
        self._scaled = False


GROUPS = {}


def gid(name):
    return GROUPS.setdefault(name, len(GROUPS))


class Model:
    """primitives on a skeleton; add(bone, f, mat, group, matf)"""

    def __init__(self, skel=None, scale=1.0):
        self.S = skel or Skel()
        self.prims = []
        self.decals = []            # (point, colour)
        self.scale = scale          # the model is built in its own units, drawn this much bigger

    def add(self, bone, f, mat, group='body', matf=None, tex=None):
        self.prims.append(Prim(bone, f, mat, gid(group), matf, tex))

    def finish(self):
        k = self.scale
        for pr in self.prims:
            R, t = self.S.b[pr.bone]
            pr.R, pr.t = R, t * k
            if k != 1.0 and not pr._scaled:
                pr._scaled = True
                f = pr.f
                pr.f = (lambda f: lambda q: f(q / k) * k)(f)
                if pr.matf is not None:
                    mf = pr.matf
                    pr.matf = (lambda mf: lambda q: mf(q / k))(mf)
                if pr.tex is not None:
                    tf = pr.tex
                    pr.tex = (lambda tf: lambda q: tf(q / k))(tf)
        return self.prims


# ---------------------------------------------------------------- render

def scene_d(Pts, prims):
    d = np.full(len(Pts), 1e9)
    for pr in prims:
        d = np.minimum(d, pr.f((Pts - pr.t) @ pr.R))
    return d


def scene_info(Pts, prims):
    Dm = np.stack([pr.f((Pts - pr.t) @ pr.R) for pr in prims])
    k = Dm.argmin(0)
    m = np.zeros(len(Pts), int)
    g = np.zeros(len(Pts), int)
    tx = np.zeros(len(Pts))
    for i, pr in enumerate(prims):
        sel = k == i
        if not sel.any():
            continue
        g[sel] = pr.group
        q = (Pts[sel] - pr.t) @ pr.R
        if pr.matf is None:
            m[sel] = pr.mat
        else:
            m[sel] = pr.matf(q)
        if pr.tex is not None:
            tx[sel] = pr.tex(q)
    return m, g, tx


def normals(Pts, prims, e=0.06):
    ks = np.array([[1, -1, -1], [-1, -1, 1], [-1, 1, -1], [1, 1, 1]], float)
    n = np.zeros_like(Pts)
    for k in ks:
        n += k * scene_d(Pts + k * e, prims)[:, None]
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)


def march(O, d, prims, t0, t1, steps=180, eps=0.012, fac=0.7):
    t = t0.copy()
    hit = np.zeros(len(O), bool)
    idx = np.nonzero(t0 < t1)[0]
    for _ in range(steps):
        if len(idx) == 0:
            break
        Pts = O[idx] + d * t[idx, None]
        dist = scene_d(Pts, prims)
        h = dist < eps
        hit[idx[h]] = True
        t[idx[~h]] += np.maximum(dist[~h] * fac, 0.004)
        keep = (~h) & (t[idx] < t1[idx])
        idx = idx[keep]
    return hit, t


def soft_shadow(Pts, l, prims, tmax=40.0, k=10.0, steps=56):
    res = np.ones(len(Pts))
    t = np.full(len(Pts), 0.35)
    for _ in range(steps):
        dist = scene_d(Pts + l * t[:, None], prims)
        res = np.minimum(res, k * dist / t)
        t += np.clip(dist, 0.15, 2.0)
        if (t > tmax).all():
            break
    return np.clip(res, 0, 1)


def ambient_occ(Pts, n, prims):
    occ = np.zeros(len(Pts))
    sca = 1.0
    for i in range(1, 6):
        h = 0.5 * i
        dd = scene_d(Pts + n * h, prims)
        occ += (h - dd) * sca
        sca *= 0.6
    return np.clip(1 - 0.22 * occ, 0, 1)


# ---------------------------------------------------------------- culling
# Each primitive gets a bounding sphere (found by sampling its distance on two
# grids); a ray, a shadow ray or a shading point then only evaluates the
# primitives whose sphere it can reach. The picture is the same, much sooner.

def prim_sphere(pr, C, Rb):
    """a sphere (centre in model space, radius) holding primitive pr, which
    lies inside the model's bounding sphere (C, Rb); None if it is empty"""
    cl = (np.asarray(C, float) - pr.t) @ pr.R
    lo, hi = cl - Rb, cl + Rb
    for n in (14, 12):
        g = [np.linspace(lo[k], hi[k], n) for k in range(3)]
        Q = np.stack(np.meshgrid(*g, indexing='ij'), -1).reshape(-1, 3)
        step = float((hi - lo).max()) / (n - 1)
        near = Q[pr.f(Q) < step * 1.05 + 0.3]
        if len(near) == 0:
            return None
        lo, hi = near.min(0) - step, near.max(0) + step
    c = (lo + hi) / 2
    return pr.t + pr.R @ c, float(np.linalg.norm(hi - lo)) / 2


def scene_d_cull(Pts, prims, masks):
    """scene_d, each primitive only where its mask says it can matter"""
    d = np.full(len(Pts), 1e9)
    for pr, mk in zip(prims, masks):
        if mk is None:
            d = np.minimum(d, pr.f((Pts - pr.t) @ pr.R))
            continue
        s = np.nonzero(mk)[0]
        if len(s) == 0:
            continue
        d[s] = np.minimum(d[s], pr.f((Pts[s] - pr.t) @ pr.R))
    return d


def near_masks(Pts, spheres, pad):
    return [np.ones(len(Pts), bool) if sp is None else
            ((Pts - sp[0]) ** 2).sum(1) < (sp[1] + pad) ** 2 for sp in spheres]


def march_cull(O, d, prims, t0, t1, masks, steps=180, eps=0.012, fac=0.7):
    t = t0.copy()
    hit = np.zeros(len(O), bool)
    idx = np.nonzero(t0 < t1)[0]
    for _ in range(steps):
        if len(idx) == 0:
            break
        Pts = O[idx] + d * t[idx, None]
        dist = scene_d_cull(Pts, prims, [m[idx] for m in masks])
        h = dist < eps
        hit[idx[h]] = True
        t[idx[~h]] += np.maximum(dist[~h] * fac, 0.004)
        keep = (~h) & (t[idx] < t1[idx])
        idx = idx[keep]
    return hit, t


def soft_shadow_cull(Pts, l, prims, spheres, tmax=40.0, k=10.0, steps=56):
    # the primitives near each shadow ray, a segment from the point towards the light
    masks = []
    for sp in spheres:
        if sp is None:
            masks.append(np.ones(len(Pts), bool))
            continue
        cp = sp[0] - Pts
        tt = np.clip(cp @ l, 0.0, tmax)
        q = cp - tt[:, None] * l
        masks.append((q * q).sum(1) < (sp[1] + 3.0) ** 2)
    res = np.ones(len(Pts))
    t = np.full(len(Pts), 0.35)
    for _ in range(steps):
        dist = scene_d_cull(Pts + l * t[:, None], prims, masks)
        res = np.minimum(res, k * dist / t)
        t += np.clip(dist, 0.15, 2.0)
        if (t > tmax).all():
            break
    return np.clip(res, 0, 1)


def camera(facing=(0, -1)):
    """character -> world rotation for a model facing `facing` on the map,
    and the camera vectors (right, up, view) in model space"""
    F = np.array([facing[0], facing[1], 0.0])
    F /= np.linalg.norm(F)
    Z = np.array([0, 0, 1.0])
    M = np.stack([np.cross(F, Z), F, Z], 1)
    rW = np.array([1.0, 0, 0])
    uW = np.array([0, math.sin(ELEV), math.cos(ELEV)])
    dW = np.array([0, math.cos(ELEV), -math.sin(ELEV)])
    return M, M.T @ rW, M.T @ uW, M.T @ dW


def render(model, W, H, CX, CY, facing=(0, -1), bound=((0, 0, 29), 33.0), ss=3, ground_ao=False,
           light=LIGHT_W, rim_dir=RIM_W, cull=True):
    """-> per pixel (material, group, light, depth), and the decals"""
    prims = model.finish()
    if cull:
        return render_cull(model, prims, W, H, CX, CY, facing, bound, ss, light, rim_dir)
    M, r, u, d = camera(facing)
    l = M.T @ light
    xs = (np.arange(W * ss) + 0.5) / ss - CX
    ys = CY - (np.arange(H * ss) + 0.5) / ss
    X, Y = np.meshgrid(xs, ys)
    X, Y = X.ravel(), Y.ravel()
    O = X[:, None] * r + Y[:, None] * u - 200.0 * d
    C = np.asarray(bound[0], float)
    Rb = bound[1]
    oc = O - C
    b = oc @ d
    c = (oc * oc).sum(1) - Rb * Rb
    disc = b * b - c
    ok = disc > 0
    sq = np.sqrt(np.maximum(disc, 0))
    t0 = np.where(ok, -b - sq, 1e9)
    t1 = np.where(ok, -b + sq, -1e9)
    hit, t = march(O, d, prims, t0, t1)
    N = len(O)
    mat_ = np.full(N, -1)
    grp = np.full(N, -1)
    lum = np.zeros(N)
    dep = np.full(N, 1e9)
    hi = np.nonzero(hit)[0]
    if len(hi):
        Pts = O[hi] + d * t[hi, None]
        n = normals(Pts, prims)
        m, g, tx = scene_info(Pts, prims)
        sh = soft_shadow(Pts + n * 0.08, l, prims)
        ao = ambient_occ(Pts, n, prims)
        ndl = n @ l
        diff = np.clip((ndl + 0.25) / 1.25, 0, 1)
        L = ao * (0.22 + 0.78 * diff * (0.3 + 0.7 * sh)) + tx
        rim = np.clip(1 - np.abs(n @ (-d)), 0, 1) ** 3 * np.clip(n @ (M.T @ rim_dir), 0, 1)
        L = L + 0.35 * rim
        hvec = nrm(l - d)
        spec = np.clip(n @ hvec, 0, 1) ** 20
        metal = np.array([MATS[k].metal for k in m]) if len(m) else np.zeros(0, bool)
        L = np.where(metal, 0.25 + 0.55 * diff + 0.9 * spec, L)
        glow = np.array([MATS[k].glow for k in m]) if len(m) else np.zeros(0, bool)
        L = np.where(glow, 0.5 + 0.5 * np.clip(n @ (-d), 0, 1), L)
        mat_[hi], grp[hi], lum[hi], dep[hi] = m, g, np.clip(L, 0, 1), t[hi]
    img = downsample(mat_.reshape(H * ss, W * ss), grp.reshape(H * ss, W * ss),
                     lum.reshape(H * ss, W * ss), dep.reshape(H * ss, W * ss), ss)
    proj = []
    for pt, colour in model.decals:
        pc = M.T @ (np.asarray(pt, float) * model.scale)
        proj.append((pc @ r + CX, CY - pc @ u, pc @ d + 200.0, colour))
    return img, proj


def render_cull(model, prims, W, H, CX, CY, facing, bound, ss, light, rim_dir):
    """render() with every primitive limited to the rays and points that can
    reach its bounding sphere"""
    M, r, u, d = camera(facing)
    l = M.T @ light
    xs = (np.arange(W * ss) + 0.5) / ss - CX
    ys = CY - (np.arange(H * ss) + 0.5) / ss
    X, Y = np.meshgrid(xs, ys)
    X, Y = X.ravel(), Y.ravel()
    O = X[:, None] * r + Y[:, None] * u - 200.0 * d
    C = np.asarray(bound[0], float)
    Rb = bound[1]
    oc = O - C
    b = oc @ d
    c = (oc * oc).sum(1) - Rb * Rb
    disc = b * b - c
    ok = disc > 0
    sq = np.sqrt(np.maximum(disc, 0))
    t0 = np.where(ok, -b - sq, 1e9)
    t1 = np.where(ok, -b + sq, -1e9)
    spheres = [prim_sphere(pr, C, Rb) for pr in prims]
    keep = [i for i, sp in enumerate(spheres) if sp is not None]
    prims = [prims[i] for i in keep]
    spheres = [spheres[i] for i in keep]
    # the rays (parallel: a point on the screen) that pass near each sphere
    rmasks = [(X - sp[0] @ r) ** 2 + (Y - sp[0] @ u) ** 2 < (sp[1] + 0.05) ** 2 for sp in spheres]
    hit, t = march_cull(O, d, prims, t0, t1, rmasks)
    N = len(O)
    mat_ = np.full(N, -1)
    grp = np.full(N, -1)
    lum = np.zeros(N)
    dep = np.full(N, 1e9)
    hi = np.nonzero(hit)[0]
    if len(hi):
        Pts = O[hi] + d * t[hi, None]
        nm = near_masks(Pts, spheres, 0.6)
        e = 0.06
        ks = np.array([[1, -1, -1], [-1, -1, 1], [-1, 1, -1], [1, 1, 1]], float)
        n = np.zeros_like(Pts)
        for kk in ks:
            n += kk * scene_d_cull(Pts + kk * e, prims, nm)[:, None]
        n = n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
        # material, group, texture: the nearest primitive
        Dm = np.full((len(prims), len(Pts)), 1e9)
        for i, (pr, mk) in enumerate(zip(prims, nm)):
            s = np.nonzero(mk)[0]
            if len(s):
                Dm[i, s] = pr.f((Pts[s] - pr.t) @ pr.R)
        kmin = Dm.argmin(0)
        m = np.zeros(len(Pts), int)
        g = np.zeros(len(Pts), int)
        tx = np.zeros(len(Pts))
        for i, pr in enumerate(prims):
            sel = kmin == i
            if not sel.any():
                continue
            g[sel] = pr.group
            q = (Pts[sel] - pr.t) @ pr.R
            m[sel] = pr.mat if pr.matf is None else pr.matf(q)
            if pr.tex is not None:
                tx[sel] = pr.tex(q)
        sh = soft_shadow_cull(Pts + n * 0.08, l, prims, spheres)
        am = near_masks(Pts, spheres, 3.2)
        occ = np.zeros(len(Pts))
        sca = 1.0
        for i in range(1, 6):
            hh = 0.5 * i
            dd = scene_d_cull(Pts + n * hh, prims, am)
            occ += (hh - dd) * sca
            sca *= 0.6
        ao = np.clip(1 - 0.22 * occ, 0, 1)
        ndl = n @ l
        diff = np.clip((ndl + 0.25) / 1.25, 0, 1)
        L = ao * (0.22 + 0.78 * diff * (0.3 + 0.7 * sh)) + tx
        rim = np.clip(1 - np.abs(n @ (-d)), 0, 1) ** 3 * np.clip(n @ (M.T @ rim_dir), 0, 1)
        L = L + 0.35 * rim
        hvec = nrm(l - d)
        spec = np.clip(n @ hvec, 0, 1) ** 20
        metal = np.array([MATS[k].metal for k in m])
        L = np.where(metal, 0.25 + 0.55 * diff + 0.9 * spec, L)
        glow = np.array([MATS[k].glow for k in m])
        L = np.where(glow, 0.5 + 0.5 * np.clip(n @ (-d), 0, 1), L)
        mat_[hi], grp[hi], lum[hi], dep[hi] = m, g, np.clip(L, 0, 1), t[hi]
    img = downsample(mat_.reshape(H * ss, W * ss), grp.reshape(H * ss, W * ss),
                     lum.reshape(H * ss, W * ss), dep.reshape(H * ss, W * ss), ss)
    proj = []
    for pt, colour in model.decals:
        pc = M.T @ (np.asarray(pt, float) * model.scale)
        proj.append((pc @ r + CX, CY - pc @ u, pc @ d + 200.0, colour))
    return img, proj


def downsample(mat_, grp, lum, dep, ss):
    H, W = mat_.shape[0] // ss, mat_.shape[1] // ss
    out_m = np.full((H, W), -1)
    out_g = np.full((H, W), -1)
    out_l = np.zeros((H, W))
    out_d = np.full((H, W), 1e9)
    for j in range(H):
        for i in range(W):
            mm = mat_[j * ss:(j + 1) * ss, i * ss:(i + 1) * ss].ravel()
            hitm = mm >= 0
            cnt = hitm.sum()
            if cnt == 0:
                continue
            ms = mm[hitm]
            best, bw = -1, -1
            for m in np.unique(ms):
                w = (ms == m).sum() * MATS[m].weight
                if w > bw:
                    best, bw = m, w
            nb = (ms == best).sum()
            if cnt < ss * ss * 0.45 and not (MATS[best].detail and nb >= 2):
                continue
            sel = mm == best
            out_m[j, i] = best
            out_l[j, i] = lum[j * ss:(j + 1) * ss, i * ss:(i + 1) * ss].ravel()[sel].mean()
            out_d[j, i] = dep[j * ss:(j + 1) * ss, i * ss:(i + 1) * ss].ravel()[sel].min()
            gg = grp[j * ss:(j + 1) * ss, i * ss:(i + 1) * ss].ravel()[sel]
            out_g[j, i] = np.bincount(gg).argmax()
    return out_m, out_g, out_l, out_d


def tone(m, L):
    cuts = MATS[m].cuts
    k = 0
    while k < len(cuts) and L >= cuts[k]:
        k += 1
    return k


def despeckle(m, T, maxsize=2, rounds=2):
    H, W = m.shape
    for _ in range(rounds):
        seen = np.zeros((H, W), bool)
        T2 = T.copy()
        for j in range(H):
            for i in range(W):
                if m[j, i] < 0 or seen[j, i]:
                    continue
                mm, tt = m[j, i], T[j, i]
                stack, cl = [(j, i)], []
                seen[j, i] = True
                while stack:
                    a, b = stack.pop()
                    cl.append((a, b))
                    for c, d in ((a - 1, b), (a + 1, b), (a, b - 1), (a, b + 1)):
                        if 0 <= c < H and 0 <= d < W and not seen[c, d] and m[c, d] == mm and T[c, d] == tt:
                            seen[c, d] = True
                            stack.append((c, d))
                if len(cl) > maxsize or MATS[mm].keep:
                    continue
                votes = {}
                for a, b in cl:
                    for c, d in ((a - 1, b), (a + 1, b), (a, b - 1), (a, b + 1)):
                        if 0 <= c < H and 0 <= d < W and m[c, d] == mm and T[c, d] != tt:
                            votes[T[c, d]] = votes.get(T[c, d], 0) + 1
                if votes:
                    nt = max(votes, key=votes.get)
                    for a, b in cl:
                        T2[a, b] = nt
        T = T2
    return T


def pixelize(img, proj, lines=True, outline=INK, line_jump=1.6):
    """-> RGBA uint8 (H, W, 4)"""
    m, g, L, dep = img
    H, W = m.shape
    T = np.full((H, W), -1)
    for j in range(H):
        for i in range(W):
            if m[j, i] >= 0:
                T[j, i] = tone(m[j, i], L[j, i])
    T = despeckle(m, T)
    out = np.zeros((H, W, 4), np.uint8)
    ramp_k = np.full((H, W), -1)
    for j in range(H):
        for i in range(W):
            if m[j, i] >= 0:
                ramp_k[j, i] = T[j, i]
    # contour lines where a part passes in front of another
    if lines:
        line = np.zeros((H, W), bool)
        for j in range(H):
            for i in range(W):
                if m[j, i] < 0 or MATS[m[j, i]].noline:
                    continue
                for a, b in ((j - 1, i), (j + 1, i), (j, i - 1), (j, i + 1)):
                    if 0 <= a < H and 0 <= b < W and m[a, b] >= 0:
                        jump = dep[j, i] - dep[a, b]
                        if (jump > line_jump and g[a, b] != g[j, i]) or jump > 3.5:
                            line[j, i] = True
    for j in range(H):
        for i in range(W):
            if m[j, i] < 0:
                continue
            mt = MATS[m[j, i]]
            k = ramp_k[j, i]
            c = mt.ramp[k]
            if lines and line[j, i]:
                c = outline if k <= 1 else mt.ramp[0]
            out[j, i, :3] = c
            out[j, i, 3] = 255
    for sx, sy, tz, colour in proj:
        i, j = int(math.floor(sx)), int(math.floor(sy))
        if 0 <= i < W and 0 <= j < H and m[j, i] >= 0 and tz <= dep[j, i] + 0.9:
            out[j, i, :3] = colour
    if outline is not None:
        solid = out[:, :, 3] > 0
        ol = np.zeros_like(solid)
        ol[1:, :] |= solid[:-1, :]
        ol[:-1, :] |= solid[1:, :]
        ol[:, 1:] |= solid[:, :-1]
        ol[:, :-1] |= solid[:, 1:]
        sel = ol & ~solid
        out[sel, :3] = outline
        out[sel, 3] = 255
    return out


def crop(img):
    """-> cropped image, (x0, y0) of the crop in img"""
    ys, xs = np.nonzero(img[:, :, 3] > 0)
    if len(xs) == 0:
        return img[:1, :1], (0, 0)
    return img[ys.min():ys.max() + 1, xs.min():xs.max() + 1], (int(xs.min()), int(ys.min()))


# ---------------------------------------------------------------- textures (light offsets)

def tex_noise(amp=0.07, f=1.3, seed=0.0):
    def t(q):
        return amp * (hash2(np.floor(q[:, 0] * f) + seed, np.floor(q[:, 1] * f) * 7.1 + np.floor(q[:, 2] * f) * 3.3)
                      * 2 - 1)
    return t


def tex_blocks(h=3.0, w=5.0, joint=0.22, amp=0.08, seed=0.0):
    """ashlar: courses of height h, blocks of length w, dark joints"""
    def t(q):
        z = q[:, 2]
        row = np.floor(z / h)
        u = q[:, 0] + q[:, 1] * 0.97 + (row % 2) * w * 0.5
        col = np.floor(u / w)
        fz, fu = (z / h) % 1, (u / w) % 1
        j = (fz < 0.14) | (fu < 0.08)
        blk = hash2(row + seed, col) * 2 - 1
        return np.where(j, -joint, amp * blk) + 0.03 * (hash2(np.floor(q[:, 0] * 2), np.floor(z * 2) + 9) - 0.5)
    return t


def tex_planks(axis=0, width=2.2, amp=0.08, seed=0.0):
    def t(q):
        u = q[:, axis]
        k = np.floor(u / width)
        gap = ((u / width) % 1) < 0.12
        along = q[:, 1 - axis if axis < 2 else 0]
        grain = 0.05 * np.sin(along * 3.1 + k * 7.0 + np.sin(along * 0.7) * 2)
        return np.where(gap, -0.22, amp * (hash2(k + seed, 2.0) * 2 - 1) + grain)
    return t


def tex_bark(amp=0.12):
    def t(q):
        a = np.arctan2(q[:, 1], q[:, 0])
        return amp * np.sign(np.sin(a * 7 + q[:, 2] * 0.35 + np.sin(q[:, 2] * 0.9) * 1.5)) * 0.5 - 0.02
    return t
