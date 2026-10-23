"""Skeletal playback: the pose of a clip at a time and the bone matrices.

Port of src/bm/runtime.c:2198-2321 (quat_norm, quat_slerp, pose_at, quat_mul,
skin) and of the call in l_animate (src/bm/runtime.c:2369-2409). bm Animator
(carts/animator/main.lua:772-812 `ease` / `sample`, Q.slerp in
src/script/bm3d.lua:112-125) does the same sums; see
references/OPEN_QUESTIONS.md "Preview" for the small differences found.

Arithmetic is in Python doubles, the kernel uses f32: results agree to about
1e-6 (NON VERIFICATO against a run of the kernel). Key times, poses and bone
points are already f32 values (decoded from the file).

A bone matrix is 12 floats, rows of a 3x4 [R | t] (row-major, as s->mat in
runtime.c:2300-2317): p' = R p + t. Rigid skinning: each vertex follows the
matrix of its bone (runtime.c:2319-2320, r3d moves it while drawing).
"""
import math
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

from .errors import ValidationError, need

IDENTITY = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)


def quat_norm(q):
    """runtime.c:2201-2206: unit length, identity if ~0"""
    l = math.sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3])
    if l < 1e-12:
        return [0.0, 0.0, 0.0, 1.0]
    return [q[0] / l, q[1] / l, q[2] / l, q[3] / l]


def quat_slerp(a, b, u):
    """the shorter way from a to b (runtime.c:2209-2223; same as
    src/script/bm3d.lua:112-125)"""
    d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]
    s = 1.0
    if d < 0:
        d, s = -d, -1.0
    if d > 0.9995:
        k0, k1 = 1 - u, u
    else:
        th = math.acos(min(1.0, d))
        sn = math.sin(th)
        k0, k1 = math.sin((1 - u) * th) / sn, math.sin(u * th) / sn
    return quat_norm([a[k] * k0 + s * b[k] * k1 for k in range(4)])


def quat_mul(a, b):
    """runtime.c:2274-2281 (bm3d.lua Q.mul, 96-101)"""
    return [a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
            a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
            a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]]


def _key(clip, k):
    p = clip.keys[k].pose
    return [list(x.q) for x in p], [list(x.t) for x in p]


def pose_at(clip, t, nbones):
    """(q[nb][4], tr[nb][3]) of `clip` at time t seconds. Port of pose_at
    (runtime.c:2230-2271): loop -> t mod length (>= 0), else clamp to
    0..length; one key or before the first (not looping) -> key 0; past the
    last (not looping) -> the last key; looping outside [first, last) blends
    last -> first across the wrap; u linear, smoothstep u*u*(3-2u) (mode 1),
    0 (mode 2 step); slerp + linear translation."""
    need(len(clip.keys) >= 1, "a clip with no keys", f"clip {clip.name!r}")
    L = clip.length if clip.length > 0 else 1.0
    if clip.loop:
        t = math.fmod(t, L)
        if t < 0:
            t += L
    else:
        t = 0.0 if t < 0 else L if t > L else t
    n = len(clip.keys)
    first, last = clip.keys[0].time, clip.keys[n - 1].time
    if n == 1 or (not clip.loop and t < first):
        return _key(clip, 0)
    if t < first or t >= last:
        if not clip.loop:
            return _key(clip, n - 1)
        ka, kb = n - 1, 0
        ta = last - L if t < first else last
        tb = first if t < first else first + L
    else:
        ka = 0
        while ka + 1 < n and clip.keys[ka + 1].time <= t:
            ka += 1
        kb = ka + 1
        ta, tb = clip.keys[ka].time, clip.keys[kb].time
    u = (t - ta) / (tb - ta) if tb > ta else 0.0
    if clip.mode == 2:
        u = 0.0
    elif clip.mode == 1:
        u = u * u * (3 - 2 * u)
    elif clip.mode == 3:
        u = u * u
    elif clip.mode == 4:
        u = u * (2 - u)
    qa, ta_ = _key(clip, ka)
    qb, tb_ = _key(clip, kb)
    q = [quat_slerp(qa[i], qb[i], u) for i in range(nbones)]
    tr = [[ta_[i][k] + (tb_[i][k] - ta_[i][k]) * u for k in range(3)] for i in range(nbones)]
    return q, tr


def rest_pose(nbones):
    """animate(m) with no clip: identity rotations, no move (runtime.c:2374-2379)"""
    return [[0.0, 0.0, 0.0, 1.0] for _ in range(nbones)], [[0.0, 0.0, 0.0] for _ in range(nbones)]


def skin(rig, q, tr):
    """the bone matrices M[i] = M[parent] * T(head + t) * R(q) * T(-head)
    (runtime.c:2273, 2283-2318; bone_turn is not modelled: it is a runtime
    call, not file data). -> list of 12-tuples"""
    mats = []
    for i, b in enumerate(rig.bones):
        x, y, z, w = quat_norm(q[i])
        r = (1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
             2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
             2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y))
        h = b.head
        local = []
        for row in range(3):
            r0, r1, r2 = r[row * 3], r[row * 3 + 1], r[row * 3 + 2]
            local += [r0, r1, r2, h[row] + tr[i][row] - (r0 * h[0] + r1 * h[1] + r2 * h[2])]
        if b.parent < 0:
            mats.append(tuple(local))
            continue
        a = mats[b.parent]
        m = []
        for row in range(3):
            a0, a1, a2, a3 = a[row * 4], a[row * 4 + 1], a[row * 4 + 2], a[row * 4 + 3]
            m += [a0 * local[col] + a1 * local[4 + col] + a2 * local[8 + col] for col in range(3)]
            m.append(a0 * local[3] + a1 * local[7] + a2 * local[11] + a3)
        mats.append(tuple(m))
    return mats


def apply(m, p):
    """p' = M p (3x4)"""
    x, y, z = p
    return (m[0] * x + m[1] * y + m[2] * z + m[3],
            m[4] * x + m[5] * y + m[6] * z + m[7],
            m[8] * x + m[9] * y + m[10] * z + m[11])


def rotate(m, n):
    """the 3x3 part of M on a direction (the normal of a face, r3d.c:2171-2175)"""
    x, y, z = n
    return (m[0] * x + m[1] * y + m[2] * z, m[4] * x + m[5] * y + m[6] * z, m[8] * x + m[9] * y + m[10] * z)


def skin_verts(model, rig, mats):
    """the vertices of `model` moved by their bones (rig.vbones)"""
    return [apply(mats[rig.vbones[i]], v) for i, v in enumerate(model.verts)]


def find_clip(rig, clip):
    """a clip by name or by number from 1 (find_clip, runtime.c:2332-2345)"""
    if isinstance(clip, int) and not isinstance(clip, bool):
        need(1 <= clip <= len(rig.clips), f"no animation number {clip}", f"rig {rig.model!r}")
        return rig.clips[clip - 1]
    for c in rig.clips:
        if c.name == clip:
            return c
    raise ValidationError(f"no animation {clip!r}", f"rig {rig.model!r}")


def check_link(model, rig):
    """a rig moves a model only with the same name and vertex count (runtime.c:2161)"""
    w = f"rig {rig.model!r}"
    need(rig.model == model.name, f"rig is for model {rig.model!r}, not {model.name!r}", w)
    need(len(rig.vbones) == len(model.verts),
         f"rig has {len(rig.vbones)} vertices, model has {len(model.verts)} (src/bm/runtime.c:2161)", w)
    need(all(0 <= b < len(rig.bones) for b in rig.vbones), "a vertex on a bone the rig does not have", w)
    for c in rig.clips:
        for k in c.keys:
            need(len(k.pose) == len(rig.bones), f"clip {c.name!r}: a key without one pose per bone", w)


@dataclass
class Frame:
    """one sampled instant of a clip: time (s), pose (q, t per bone), bone
    matrices (12 floats each), posed bone heads/tails, and, with a model, the
    bounds (lo, hi) of its skinned vertices"""
    t: float
    q: List[List[float]]
    tr: List[List[float]]
    mats: List[Tuple[float, ...]]
    heads: List[Tuple[float, float, float]]
    tails: List[Tuple[float, float, float]]
    bounds: Optional[Tuple[Tuple[float, float, float], Tuple[float, float, float]]] = None
    verts: Optional[list] = field(default=None, repr=False)

    def to_dict(self):
        return {"t": self.t, "q": self.q, "tr": self.tr, "mats": [list(m) for m in self.mats],
                "heads": [list(h) for h in self.heads], "tails": [list(t) for t in self.tails],
                "bounds": [list(self.bounds[0]), list(self.bounds[1])] if self.bounds else None}


def frame_times(clip, fps=12, include_end=False):
    """t = i / fps for i in 0..n-1, n = max(1, round(length * fps)); with
    include_end one more frame at t = length (for a looping clip it equals
    frame 0, pose_at wraps). bm Animator's timeline snaps to 12 frames/s
    (src/script/bm3d.lua:32)."""
    if not (isinstance(fps, (int, float)) and fps > 0 and math.isfinite(fps)):
        raise ValueError(f"fps must be a positive number, not {fps!r}")
    n = max(1, int(round(clip.length * fps)))
    ts = [i / fps for i in range(n)]
    if include_end:
        ts.append(clip.length)
    return ts


def playback(rig, clip, fps=12, model=None, include_end=False, times=None):
    """the frames of a clip, no image: list of Frame. clip = name or number
    (from 1). With a model (linked as by runtime.c:2161) each Frame also has
    the skinned vertices and their bounds."""
    if model is not None:
        check_link(model, rig)
    c = find_clip(rig, clip)
    nb = len(rig.bones)
    out = []
    for t in (times if times is not None else frame_times(c, fps, include_end)):
        q, tr = pose_at(c, t, nb)
        mats = skin(rig, q, tr)
        heads = [apply(mats[i], b.head) for i, b in enumerate(rig.bones)]
        tails = [apply(mats[i], b.tail) for i, b in enumerate(rig.bones)]
        fr = Frame(t, q, tr, mats, heads, tails)
        if model is not None:
            vs = skin_verts(model, rig, mats)
            fr.verts = vs
            fr.bounds = (tuple(min(v[k] for v in vs) for k in range(3)),
                         tuple(max(v[k] for v in vs) for k in range(3)))
        out.append(fr)
    return out
