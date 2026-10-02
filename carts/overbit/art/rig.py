"""
rig.py - skeletons and animations of Overbit's heroes, written as the ANIM
section of a .bm (src/bm/bm.h, the format of bm Animator).

A bone turns around its head: M = M_parent * T(head + t) * R(q) * T(-head),
with q relative to the parent (runtime.c animate(), sdk/studio/js/rig.js).
Poses are written in degrees: {bone: (rx, ry, rz)} or (rx, ry, rz, tx, ty,
tz); the angles turn x, then y, then z, in the parent's frame. With y up and
the character facing +z: rx > 0 swings a hanging arm or leg backwards, ry
turns to the left... seen from above, counterclockwise (from +z towards
+x), rz > 0 leans towards +y from +x (the right arm goes up).
"""
import math
import struct

NAME_LEN = 16


def quat_euler(rx, ry, rz):
    """degrees, x then y then z -> (x, y, z, w)"""
    def q_axis(axis, deg):
        h = math.radians(deg) / 2
        s = math.sin(h)
        return (s if axis == 0 else 0.0, s if axis == 1 else 0.0, s if axis == 2 else 0.0, math.cos(h))
    return qmul(q_axis(2, rz), qmul(q_axis(1, ry), q_axis(0, rx)))


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qmat(q):
    x, y, z, w = q
    return [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
            2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
            2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]


class Skeleton:
    def __init__(self):
        self.bones = []          # (name, parent index, head, tail)
        self.index = {}

    def bone(self, name, parent, head, tail):
        if len(self.bones) >= 64:
            raise ValueError("at most 64 bones")
        p = -1 if parent is None else self.index[parent]
        self.index[name] = len(self.bones)
        self.bones.append((name, p, tuple(head), tuple(tail)))
        return self.index[name]

    def __getitem__(self, name):
        return self.index[name]

    def matrices(self, pose):
        """pose -> per bone (3x3 rotation, translation): where the rest
        vertices of each bone go (for checks and inverse kinematics)"""
        out = []
        for name, parent, head, _ in self.bones:
            v = pose.get(name, (0, 0, 0))
            r = qmat(quat_euler(*v[:3]))
            t = v[3:6] if len(v) >= 6 else (0, 0, 0)
            # local = T(head + t) R T(-head)
            lt = [head[k] + t[k] - sum(r[k * 3 + j] * head[j] for j in range(3)) for k in range(3)]
            if parent < 0:
                out.append((r, lt))
                continue
            pr, pt = out[parent]
            rr = [sum(pr[i * 3 + k] * r[k * 3 + j] for k in range(3)) for i in range(3) for j in range(3)]
            tt = [sum(pr[i * 3 + k] * lt[k] for k in range(3)) + pt[i] for i in range(3)]
            out.append((rr, tt))
        return out

    def place(self, pose, bone, p):
        """a rest point carried by a bone in a pose"""
        r, t = self.matrices(pose)[self.index[bone]]
        return tuple(sum(r[i * 3 + j] * p[j] for j in range(3)) + t[i] for i in range(3))


class Clip:
    """keys: [(time, pose)]; mode 0 linear, 1 smooth, 2 step"""

    def __init__(self, name, length, keys, loop=True, mode=1):
        self.name, self.length, self.keys, self.loop, self.mode = name, length, keys, loop, mode


def sample(fn, length, n, loop=True, mode=1, name="clip"):
    """a clip from a function t (0..length) -> pose, n keys (a loop does not
    repeat the first key at the end)"""
    keys = []
    for i in range(n + (0 if loop else 1)):
        t = length * i / n
        keys.append((t, fn(t)))
    return Clip(name, length, keys, loop, mode)


def leg_ik(hip, target, l1, l2, rest_thigh, rest_shin, knee_forward=True):
    """two bones in the y-z plane (rotations about x): the thigh from hip,
    the shin to `target` (the ankle). rest_thigh, rest_shin: their directions
    at rest (y, z). Returns (thigh rx, shin rx relative to the thigh), degrees."""
    def ang(dy, dz):          # rotation about x that takes "straight down" to (dy, dz)
        return math.degrees(math.atan2(-dz, -dy))
    dy, dz = target[1] - hip[1], target[2] - hip[2]
    d = math.hypot(dy, dz)
    d = max(1e-4, min(d, (l1 + l2) * 0.9999))
    cos_a = (l1 * l1 + d * d - l2 * l2) / (2 * l1 * d)
    a = math.degrees(math.acos(max(-1, min(1, cos_a))))
    base = ang(dy, dz)
    # the knee in front (+z): the thigh turns towards -rx... a forward knee
    # means the thigh points more forward than the hip-ankle line
    thigh = base - a if knee_forward else base + a
    # knee position, then the shin direction
    kt = math.radians(thigh)
    knee = (hip[1] - l1 * math.cos(kt), hip[2] - l1 * math.sin(kt))
    sd = (target[1] - knee[0], target[2] - knee[1])
    shin = ang(*sd)
    t0, s0 = ang(*rest_thigh), ang(*rest_shin)
    return thigh - t0, (shin - s0) - (thigh - t0)


def encode(rigs):
    """rigs: [(model name, Skeleton, vertex bones, [Clip])] -> ANIM body"""
    out = bytearray(struct.pack("<HHI", len(rigs), 0, 0))
    for model, sk, vbones, clips in rigs:
        nb = len(sk.bones)
        out += model.encode()[:NAME_LEN - 1].ljust(NAME_LEN, b"\0")
        out += struct.pack("<HHHH", nb, len(clips), len(vbones), 0)
        for name, parent, head, tail in sk.bones:
            out += name.encode()[:NAME_LEN - 1].ljust(NAME_LEN, b"\0")
            out += struct.pack("<hH6f", parent, 0, *head, *tail)
        vb = bytes(vbones)
        out += vb + b"\0" * ((-len(vb)) % 4)
        for c in clips:
            if not 1 <= len(c.keys) <= 1024:
                raise ValueError(f"{model}/{c.name}: 1 to 1024 keys")
            out += c.name.encode()[:NAME_LEN - 1].ljust(NAME_LEN, b"\0")
            out += struct.pack("<HBBf", len(c.keys), c.mode, 1 if c.loop else 0, c.length)
            last = -1
            for t, pose in c.keys:
                t = max(0.0, min(c.length, t))
                if t < last:
                    raise ValueError(f"{model}/{c.name}: key times must rise")
                last = t
                out += struct.pack("<f", t)
                for name, _, _, _ in sk.bones:
                    v = pose.get(name, (0, 0, 0))
                    q = quat_euler(*v[:3])
                    tr = v[3:6] if len(v) >= 6 else (0, 0, 0)
                    out += struct.pack("<7f", *q, *tr)
    return bytes(out)


def mix(a, b, k):
    """two poses blended (angles linearly: fine for authoring keys)"""
    out = {}
    for name in set(a) | set(b):
        va = list(a.get(name, (0, 0, 0)))
        vb = list(b.get(name, (0, 0, 0)))
        n = max(len(va), len(vb))
        va += [0] * (n - len(va))
        vb += [0] * (n - len(vb))
        out[name] = tuple(x + (y - x) * k for x, y in zip(va, vb))
    return out


def over(base, top):
    """pose `top` replacing the bones it names in `base`"""
    out = dict(base)
    out.update(top)
    return out
