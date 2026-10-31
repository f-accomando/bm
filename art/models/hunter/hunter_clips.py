"""hunter_clips: the Hunter's clips, generated from a few readable numbers (12 frames/s, linear, a key on every frame).

Every clip is a function of a parameter dict (the "params" below); a small solver turns the parameters of one frame
into one rotation per bone: the legs and arms by two-bone IK (feet and wrists are given as points), the weapon
hand by a direction, the body by degrees. The T pose is the rest pose, so the skeleton needs no bind pose.

Frames are IN PLACE: the game moves the character, the clips only move the body (small lunges return to 0).
Axes of the finished model: +x = the character's left (.L), +y up, forward = -z.

Params (all optional, missing = READY):
  hips [lean, yawL, bankL]  deg, world rotation of the pelvis      hmove [left, up, fwd] metres
  spine chest neck head [lean, yawL, bankL] deg, local            (lean + = forward, yawL + = turn left, bankL + = tilt left)
  fL fR [out, up, fwd, pitch, yawL]   ankle on the ground plane, from the point under the pelvis (pitch + = toe up)
  wL wR [out, up, fwd]                ankle from the hip joint in the pelvis frame (rest = [0, -0.83, 0]); fw = blend 0..1
  hL hR [out, up, fwd]                wrist from the shoulder joint in the chest frame (rest = [0.54, 0, 0])
  dL dR [fwd, up, left]               direction of the hand (= the weapon tip) in the chest frame;  rL rR roll in deg
  gL gR                               grip 0..1 (fingers and thumb)       eL eR  [out, up, fwd]  elbow hint
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from hunter_build import BONES, expand_bones  # noqa: E402
from bm_core import animgen, skin  # noqa: E402
from bm_core.skin import quat_mul, quat_norm  # noqa: E402

FPS = 12
RIG = expand_bones(BONES, "hunter")
NAMES = [b.name for b in RIG.bones]
IDX = {n: i for i, n in enumerate(NAMES)}
NB = len(NAMES)


def vsub(a, b): return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
def vadd(a, b): return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
def vmul(a, k): return [a[0] * k, a[1] * k, a[2] * k]
def vdot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def vlen(a): return math.sqrt(vdot(a, a))
def vunit(a):
    n = vlen(a) or 1.0
    return [a[0] / n, a[1] / n, a[2] / n]
def vcross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def qconj(q): return [-q[0], -q[1], -q[2], q[3]]
def qmul(a, b): return quat_mul(a, b)


def qaxis(axis, deg):
    a = vunit(axis)
    h = math.radians(deg) / 2
    s = math.sin(h)
    return [a[0] * s, a[1] * s, a[2] * s, math.cos(h)]


def qrot(q, v):
    p = qmul(qmul(q, [v[0], v[1], v[2], 0.0]), qconj(q))
    return [p[0], p[1], p[2]]


def qarc(a, b):
    """shortest rotation taking unit a to unit b"""
    a, b = vunit(a), vunit(b)
    d = vdot(a, b)
    if d > 0.999999:
        return [0.0, 0.0, 0.0, 1.0]
    if d < -0.999999:
        ax = vcross(a, [1, 0, 0])
        if vlen(ax) < 1e-3:
            ax = vcross(a, [0, 1, 0])
        return qaxis(ax, 180)
    c = vcross(a, b)
    return quat_norm([c[0], c[1], c[2], 1 + d])


def mat_rot_to_q(m):
    r = [[m[0], m[1], m[2]], [m[4], m[5], m[6]], [m[8], m[9], m[10]]]
    t = r[0][0] + r[1][1] + r[2][2]
    if t > 0:
        s = math.sqrt(t + 1) * 2
        q = [(r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, 0.25 * s]
    elif r[0][0] > r[1][1] and r[0][0] > r[2][2]:
        s = math.sqrt(1 + r[0][0] - r[1][1] - r[2][2]) * 2
        q = [0.25 * s, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s]
    elif r[1][1] > r[2][2]:
        s = math.sqrt(1 + r[1][1] - r[0][0] - r[2][2]) * 2
        q = [(r[0][1] + r[1][0]) / s, 0.25 * s, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s]
    else:
        s = math.sqrt(1 + r[2][2] - r[0][0] - r[1][1]) * 2
        q = [(r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, 0.25 * s, (r[1][0] - r[0][1]) / s]
    return quat_norm(q)


def euler(lean=0.0, yawl=0.0, bankl=0.0):
    """body angles -> q: lean + forward (x rot is -lean), yawL + toward +x (y rot is -yawL), bankL + toward +x (z rot is -bankL)"""
    return qmul(qaxis([0, 0, 1], -bankl), qmul(qaxis([0, 1, 0], -yawl), qaxis([1, 0, 0], -lean)))


def apply(m, p):
    return [m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3], m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7],
            m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11]]


def rot_only(m, v):
    return [m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[4] * v[0] + m[5] * v[1] + m[6] * v[2], m[8] * v[0] + m[9] * v[1] + m[10] * v[2]]


def rest_dir(name):
    b = RIG.bones[IDX[name]]
    return vunit(vsub(b.tail, b.head))


def seg_len(name):
    b = RIG.bones[IDX[name]]
    return vlen(vsub(b.tail, b.head))


class Pose:
    def __init__(self):
        self.q = [[0.0, 0.0, 0.0, 1.0] for _ in range(NB)]
        self.t = [[0.0, 0.0, 0.0] for _ in range(NB)]
        self.m = skin.skin(RIG, self.q, self.t)

    def refresh(self):
        self.m = skin.skin(RIG, self.q, self.t)

    def mat(self, name): return self.m[IDX[name]]

    def wrot(self, name):
        return mat_rot_to_q(self.m[IDX[name]])

    def head_w(self, name):
        return apply(self.m[IDX[name]], RIG.bones[IDX[name]].head)

    def set_local(self, name, q):
        self.q[IDX[name]] = quat_norm(q)
        self.refresh()

    def set_world(self, name, wq):
        """world rotation of a bone (its rest frame is the world frame): q_local = parent_world^-1 * wq"""
        b = RIG.bones[IDX[name]]
        pw = self.wrot(NAMES[b.parent]) if b.parent >= 0 else [0.0, 0.0, 0.0, 1.0]
        self.q[IDX[name]] = quat_norm(qmul(qconj(pw), wq))
        self.refresh()

    def set_dir(self, name, d, roll=0.0):
        w = qarc(rest_dir(name), d)
        if roll:
            w = qmul(qaxis(d, roll), w)
        self.set_world(name, w)


def ik2(root, target, l1, l2, hint):
    """two-bone IK: (mid, end) points; the middle joint bends toward `hint`"""
    d = vsub(target, root)
    dist = vlen(d)
    dist = max(abs(l1 - l2) + 1e-3, min(l1 + l2 - 1e-3, dist))
    dd = vunit(d)
    a = (l1 * l1 - l2 * l2 + dist * dist) / (2 * dist)
    h = math.sqrt(max(0.0, l1 * l1 - a * a))
    p = vsub(hint, vmul(dd, vdot(hint, dd)))
    if vlen(p) < 1e-4:
        p = [0.0, 0.0, -1.0]
    p = vunit(p)
    mid = vadd(root, vadd(vmul(dd, a), vmul(p, h)))
    end = vadd(root, vmul(dd, dist))
    return mid, end


READY = dict(
    hips=[3, 0, 0], hmove=[0, -0.045, 0], spine=[4, 0, 0], chest=[3, 0, 0], neck=[-3, 0, 0], head=[-4, 0, 0],
    fL=[0.15, 0.09, 0.12, 0, 4], fR=[0.15, 0.09, -0.12, 0, -8],
    wL=[0, -0.8, 0], wR=[0, -0.8, 0], fw=0.0,
    hL=[0.04, -0.20, 0.36], hR=[0.02, -0.27, 0.32],
    dL=[1.0, 0.02, 0.0], dR=[1.0, -0.25, -0.1], rL=0, rR=0,
    gL=1.0, gR=1.0, eL=[0.7, -1.0, 0.2], eR=[0.7, -1.0, 0.2],
)


def solve(par):
    """params dict (complete) -> (q list, move list) for the 24 bones"""
    P = Pose()
    g = lambda k: par[k]                                            # noqa: E731
    hm = g("hmove")
    P.t[IDX["hips"]] = [hm[0], hm[1], -hm[2]]
    h = g("hips")
    P.q[IDX["hips"]] = euler(*h)
    P.refresh()
    for nm in ("spine", "chest", "neck", "head"):
        P.q[IDX[nm]] = euler(*g(nm))
        P.refresh()
    Wc = P.wrot("chest")
    Mc = P.mat("chest")
    Wh = P.wrot("hips")
    Mh = P.mat("hips")
    for side, sg in (("L", 1), ("R", -1)):
        up_, fo_, ha_ = "upper." + side, "fore." + side, "hand." + side
        sh_rest = RIG.bones[IDX[up_]].head
        off = g("h" + side)
        tgt = apply(Mc, vadd(sh_rest, [sg * off[0], off[1], -off[2]]))
        sh_w = apply(Mc, sh_rest)
        eo = g("e" + side)
        hint = qrot(Wc, [sg * eo[0], eo[1], -eo[2]])
        mid, end = ik2(sh_w, tgt, seg_len(up_), seg_len(fo_), hint)
        P.set_dir(up_, vsub(mid, sh_w))
        P.set_dir(fo_, vsub(end, mid))
        dv = g("d" + side)
        dw = qrot(Wc, [dv[2], dv[1], -dv[0]])
        P.set_dir(ha_, dw, g("r" + side))
        gr = g("g" + side)
        P.set_local("fing." + side, qaxis([0, 0, 1], -sg * gr * 85))
        P.set_local("thumb." + side, qaxis([0, 0, 1], -sg * gr * 35))
    w = g("fw")
    G = [hm[0], 0.0, -hm[2]]
    for side, sg in (("L", 1), ("R", -1)):
        th_, sh_, ft_ = "thigh." + side, "shin." + side, "foot." + side
        hip_rest = RIG.bones[IDX[th_]].head
        hip_w = apply(Mh, hip_rest)
        f = g("f" + side)
        tg = [G[0] + sg * f[0], f[1], G[2] - f[2]]
        o = g("w" + side)
        th = apply(Mh, vadd(hip_rest, [sg * o[0], o[1], -o[2]]))
        tgt = vadd(vmul(tg, 1 - w), vmul(th, w))
        hint = qrot(Wh, [sg * 0.12, 0.0, -1.0])
        knee, ank = ik2(hip_w, tgt, seg_len(th_), seg_len(sh_), hint)
        P.set_dir(th_, vsub(knee, hip_w))
        P.set_dir(sh_, vsub(ank, knee))
        fq_g = qmul(qaxis([0, 1, 0], -f[4]), qaxis([1, 0, 0], f[3]))
        fq_h = qmul(Wh, qaxis([1, 0, 0], f[3]))
        fq = fq_g if w < 0.5 else fq_h
        P.set_world(ft_, fq)
    # coat skirt: follows the thighs (forward/back swing about the pelvis frame), the back panel the mean
    sw = {}
    for side in ("L", "R"):
        d = vunit(vsub(P.m and apply(P.mat("shin." + side), RIG.bones[IDX["shin." + side]].head), P.head_w("thigh." + side)))
        dl = qrot(qconj(Wh), d)
        fwd = math.degrees(math.atan2(-dl[2], -dl[1]))
        out = math.degrees(math.atan2(dl[0] * (1 if side == "L" else -1), -dl[1]))
        sw[side] = (fwd, out)
    for side, sg in (("L", 1), ("R", -1)):
        fwd, out = sw[side]
        a = max(-38.0, min(38.0, 0.62 * fwd))
        b = max(0.0, min(25.0, 0.5 * out))
        P.set_local("skirt." + side, qmul(qaxis([0, 0, 1], -sg * b), qaxis([1, 0, 0], a)))
    meanf = 0.5 * (sw["L"][0] + sw["R"][0])
    P.set_local("skirt_b", qaxis([1, 0, 0], max(-30.0, min(30.0, 0.35 * meanf + 0.25 * abs(sw["L"][0] - sw["R"][0])))))
    return P.q, P.t


# ------------------------------------------------------------------ keying

def merge(base, over):
    d = {k: (list(v) if isinstance(v, list) else v) for k, v in base.items()}
    for k, v in over.items():
        d[k] = list(v) if isinstance(v, list) else v
    return d


def lerp(a, b, u):
    if isinstance(a, list):
        return [x + (y - x) * u for x, y in zip(a, b)]
    return a + (b - a) * u


def ease(kind, u):
    if kind == "smooth":
        return u * u * (3 - 2 * u)
    if kind == "in":
        return u * u
    if kind == "out":
        return u * (2 - u)
    return u


def from_keys(keys, frames, loop=False, base=None):
    """keys: [(frame, params_override, ease_into_this_key)]; parameters between keys are eased, missing ones inherit READY"""
    base = base or READY
    ks = [(f, merge(base, p), e) for f, p, e in keys]
    def fn(f):
        if loop:
            f = f % frames
        if f <= ks[0][0]:
            return ks[0][1]
        for (f0, p0, _), (f1, p1, e1) in zip(ks, ks[1:] + ([(ks[0][0] + frames, ks[0][1], ks[0][2])] if loop else [])):
            if f0 <= f <= f1:
                u = ease(e1, (f - f0) / float(f1 - f0)) if f1 > f0 else 1.0
                return {k: lerp(p0[k], p1[k], u) for k in p0}
        return ks[-1][1]
    return fn


def make_clip(name, frames, fn, loop=False):
    keys = []
    for f in range(frames):
        q, t = solve(fn(f))
        pose = {}
        for i, n in enumerate(NAMES):
            e = {"q": [round(x, 6) for x in q[i]]}
            if any(abs(x) > 1e-9 for x in t[i]):
                e["move"] = [round(x, 6) for x in t[i]]
            pose[n] = e
        keys.append({"frame": f, "pose": pose})
    return {"name": name, "loop": loop, "frames": frames, "curve": "linear", "keys": keys}


# ------------------------------------------------------------------ the clips

def cyc(p):
    return p - math.floor(p)


def walk_fn(frames=12, stride=0.20, lift=0.075, sway=0.02):
    def fn(f):
        p = f / float(frames)
        d = merge(READY, {})
        for side, ph in (("L", p), ("R", cyc(p + 0.5))):
            if ph < 0.5:                                         # stance: the foot slides back under the body
                u = ph / 0.5
                fwd = stride - 2 * stride * u
                up = 0.09
                pitch = 8 * (1 - u) - 22 * max(0.0, (u - 0.7) / 0.3) ** 2 + 0
            else:                                                # swing
                u = (ph - 0.5) / 0.5
                fwd = -stride + 2 * stride * (u * u * (3 - 2 * u))
                up = 0.09 + lift * math.sin(math.pi * u)
                pitch = -22 * (1 - u) + 8 * u
            d["f" + side] = [0.115, up, fwd, pitch, 0]
        d["hmove"] = [sway * math.sin(2 * math.pi * p), -0.045 - 0.022 * math.cos(4 * math.pi * p), 0]
        sw = math.sin(2 * math.pi * p)
        d["hips"] = [3, -5 * sw, 0]
        d["spine"] = [4, 7 * sw, 0]
        d["chest"] = [3, 3 * sw, 0]
        d["head"] = [-4, -6 * sw, 0]
        d["hL"] = [0.05, -0.19, 0.36 - 0.12 * sw]
        d["hR"] = [0.03, -0.26, 0.33 + 0.12 * sw]
        return d
    return fn


def run_fn(frames=8, stride=0.34, lift=0.17):
    def fn(f):
        p = f / float(frames)
        d = merge(READY, {})
        for side, ph in (("L", p), ("R", cyc(p + 0.5))):
            if ph < 0.38:
                u = ph / 0.38
                fwd = stride - 2 * stride * u * 0.92
                up = 0.09
                pitch = 5 - 35 * max(0.0, (u - 0.6) / 0.4) ** 2
            else:
                u = (ph - 0.38) / 0.62
                fwd = -stride * 0.84 + (stride * 1.84) * (u * u * (3 - 2 * u))
                up = 0.09 + lift * math.sin(math.pi * u) ** 0.8
                pitch = -30 * (1 - u) + 5 * u
            d["f" + side] = [0.10, up, fwd, pitch, 0]
        d["hmove"] = [0.015 * math.sin(2 * math.pi * p), -0.09 + 0.05 * abs(math.sin(2 * math.pi * p + math.pi / 2)) - 0.02, -0.0]
        sw = math.sin(2 * math.pi * p)
        d["hips"] = [10, -6 * sw, 0]
        d["spine"] = [6, 10 * sw, 0]
        d["chest"] = [4, 4 * sw, 0]
        d["head"] = [-12, -8 * sw, 0]
        d["hL"] = [0.0, -0.10, 0.22 - 0.26 * sw]
        d["hR"] = [-0.02, -0.12, 0.22 + 0.26 * sw]
        d["dR"] = [1.0, -0.1, -0.05]
        d["eL"] = [0.2, -1.0, 0.5]
        d["eR"] = [0.2, -1.0, 0.5]
        return d
    return fn


def idle_fn(frames=24):
    def fn(f):
        p = f / float(frames)
        b = math.sin(2 * math.pi * p)
        d = merge(READY, {})
        d["hmove"] = [0, -0.045 + 0.008 * b, 0]
        d["chest"] = [3 + 1.5 * b, 0, 0]
        d["head"] = [-4 - 1.0 * b, 0, 0]
        d["hL"] = [0.04, -0.20 + 0.01 * b, 0.36]
        d["hR"] = [0.02, -0.27 + 0.01 * b, 0.32]
        return d
    return fn


def clips():
    out = []
    out.append(make_clip("idle", 24, idle_fn(24), True))
    out.append(make_clip("walk", 12, walk_fn(), True))
    out.append(make_clip("run", 8, run_fn(), True))

    # ---- pistol shot (left hand): raise, fire (kick), hold, lower
    ks = [
        (0, {}, "lin"),
        (3, dict(hips=[1, 10, 0], spine=[2, 12, 0], chest=[2, 10, 0], head=[-2, 6, 0],
                 hL=[0.02, 0.0, 0.50], dL=[1.0, 0.03, 0.0], eL=[0.7, -1.0, 0.0],
                 hR=[0.05, -0.18, 0.25], fL=[0.14, 0.09, 0.18, 0, 12], fR=[0.18, 0.09, -0.16, 0, -20]), "smooth"),
        (5, dict(hips=[0, 10, 0], hmove=[0, -0.05, 0.05], spine=[-4, 12, 0], chest=[-6, 10, 0], head=[-2, 6, 0],
                 hL=[0.02, 0.07, 0.46], dL=[1.0, 0.30, 0.0], eL=[0.7, -1.0, 0.0],
                 hR=[0.05, -0.18, 0.25], fL=[0.14, 0.09, 0.18, 0, 12], fR=[0.18, 0.09, -0.16, 0, -20]), "out"),
        (8, dict(hips=[1, 10, 0], spine=[2, 12, 0], chest=[2, 10, 0], head=[-2, 6, 0],
                 hL=[0.02, 0.01, 0.50], dL=[1.0, 0.05, 0.0], eL=[0.7, -1.0, 0.0],
                 hR=[0.05, -0.18, 0.25], fL=[0.14, 0.09, 0.18, 0, 12], fR=[0.18, 0.09, -0.16, 0, -20]), "smooth"),
        (13, {}, "smooth"),
    ]
    out.append(make_clip("gun", 14, from_keys(ks, 14), False))

    # ---- saw, closed: three-hit combo, then the charged attack. Right hand = saw. dR = tip direction (fwd, up, left)
    CL = dict(eR=[0.7, -1.0, 0.0])
    def saw_keys(sets, frames):
        return from_keys(sets, frames)

    # hit 1: right to left horizontal slash (wind up to the right, cut across, follow through)
    c1 = [
        (0, {}, "lin"),
        (4, dict(hips=[2, -22, 0], hmove=[-0.03, -0.07, -0.02], spine=[3, -18, 0], chest=[2, -14, 0], head=[-3, 14, 0],
                 hR=[0.34, -0.02, 0.16], dR=[0.55, -0.1, -0.8], rR=-10, hL=[0.10, -0.25, 0.22],
                 fL=[0.20, 0.09, 0.14, 0, 0], fR=[0.20, 0.09, -0.20, 0, 20], **CL), "smooth"),
        (7, dict(hips=[5, 28, 0], hmove=[0.02, -0.10, 0.20], spine=[5, 22, 0], chest=[3, 16, 0], head=[-3, -14, 0],
                 hR=[-0.30, -0.10, 0.50], dR=[0.45, -0.1, 0.9], rR=10, hL=[0.20, -0.20, 0.10],
                 fL=[0.22, 0.09, 0.30, 0, 30], fR=[0.20, 0.09, -0.10, 0, -10], **CL), "in"),
        (9, dict(hips=[5, 32, 0], hmove=[0.02, -0.10, 0.22], spine=[5, 24, 0], chest=[3, 18, 0], head=[-3, -16, 0],
                 hR=[-0.36, -0.12, 0.44], dR=[0.30, -0.1, 1.0], rR=14, hL=[0.20, -0.20, 0.10],
                 fL=[0.22, 0.09, 0.30, 0, 34], fR=[0.20, 0.09, -0.10, 0, -10], **CL), "out"),
        (15, {}, "smooth"),
    ]
    out.append(make_clip("saw_c1", 16, from_keys(c1, 16), False))

    # hit 2: backhand left to right, a little lower
    c2 = [
        (0, {}, "lin"),
        (3, dict(hips=[3, 26, 0], hmove=[0.03, -0.08, 0.0], spine=[3, 18, 0], chest=[2, 14, 0], head=[-3, -12, 0],
                 hR=[-0.28, -0.20, 0.34], dR=[0.40, -0.2, 0.9], rR=20, hL=[0.12, -0.22, 0.30],
                 fL=[0.20, 0.09, 0.22, 0, 25], fR=[0.20, 0.09, -0.14, 0, -10], **CL), "smooth"),
        (6, dict(hips=[7, -32, 0], hmove=[-0.02, -0.12, 0.22], spine=[6, -24, 0], chest=[3, -18, 0], head=[-3, 16, 0],
                 hR=[0.40, -0.18, 0.40], dR=[0.40, -0.2, -0.9], rR=-15, hL=[0.18, -0.25, 0.16],
                 fL=[0.20, 0.09, 0.08, 0, -10], fR=[0.20, 0.09, -0.30, 0, 22], **CL), "in"),
        (8, dict(hips=[7, -36, 0], hmove=[-0.02, -0.12, 0.24], spine=[6, -26, 0], chest=[3, -20, 0], head=[-3, 18, 0],
                 hR=[0.46, -0.20, 0.30], dR=[0.25, -0.2, -1.0], rR=-20, hL=[0.18, -0.25, 0.16],
                 fL=[0.20, 0.09, 0.08, 0, -10], fR=[0.20, 0.09, -0.30, 0, 24], **CL), "out"),
        (14, {}, "smooth"),
    ]
    out.append(make_clip("saw_c2", 15, from_keys(c2, 15), False))

    # hit 3: overhead diagonal slam, body drops forward
    c3 = [
        (0, {}, "lin"),
        (4, dict(hips=[-4, 6, 0], hmove=[0, -0.02, -0.04], spine=[-8, 6, 0], chest=[-8, 4, 0], head=[2, 0, 0],
                 hR=[0.14, 0.44, 0.06], dR=[-0.1, 1.0, -0.2], rR=0, hL=[0.14, 0.12, 0.28],
                 fL=[0.15, 0.09, 0.10, 0, 0], fR=[0.17, 0.09, -0.20, 0, -10], **CL), "smooth"),
        (7, dict(hips=[22, -6, 0], hmove=[0, -0.18, 0.30], spine=[14, -4, 0], chest=[12, -3, 0], head=[-10, 0, 0],
                 hR=[0.08, -0.48, 0.52], dR=[0.75, -0.65, 0.0], rR=0, hL=[0.14, -0.18, 0.22],
                 fL=[0.15, 0.09, 0.36, 0, 0], fR=[0.15, 0.09, -0.22, 0, -4], **CL), "in"),
        (9, dict(hips=[24, -6, 0], hmove=[0, -0.20, 0.32], spine=[16, -4, 0], chest=[12, -3, 0], head=[-12, 0, 0],
                 hR=[0.08, -0.52, 0.50], dR=[0.65, -0.75, 0.0], rR=0, hL=[0.14, -0.18, 0.22],
                 fL=[0.15, 0.09, 0.36, 0, 0], fR=[0.15, 0.09, -0.22, 0, -4], **CL), "out"),
        (15, dict(hmove=[0, -0.12, 0.10]), "smooth"),
        (19, {}, "smooth"),
    ]
    out.append(make_clip("saw_c3", 20, from_keys(c3, 20), False))

    # charged attack: crouch and pull the saw behind, shake while charging, release in a big overhead cut
    ch = [
        (0, {}, "lin"),
        (6, dict(hips=[-8, 14, 0], hmove=[0, -0.20, -0.06], spine=[-10, 16, 0], chest=[-8, 12, 0], head=[2, -8, 0],
                 hR=[0.40, 0.30, -0.18], dR=[-0.5, 0.8, -0.5], rR=-20, hL=[0.12, -0.10, 0.30],
                 fL=[0.20, 0.09, 0.20, 0, 14], fR=[0.24, 0.09, -0.30, 0, -22], wL=[0.1, -0.7, 0.0], **CL), "smooth"),
        (9, dict(hips=[-9, 15, 0], hmove=[0.01, -0.21, -0.06], spine=[-11, 17, 0], chest=[-8, 12, 0], head=[2, -8, 0],
                 hR=[0.42, 0.32, -0.20], dR=[-0.5, 0.8, -0.5], rR=-20, hL=[0.12, -0.10, 0.30],
                 fL=[0.20, 0.09, 0.20, 0, 14], fR=[0.24, 0.09, -0.30, 0, -22], **CL), "lin"),
        (11, dict(hips=[-8, 14, 0], hmove=[-0.01, -0.20, -0.06], spine=[-10, 16, 0], chest=[-8, 12, 0], head=[2, -8, 0],
                 hR=[0.40, 0.30, -0.18], dR=[-0.5, 0.8, -0.5], rR=-20, hL=[0.12, -0.10, 0.30],
                 fL=[0.20, 0.09, 0.20, 0, 14], fR=[0.24, 0.09, -0.30, 0, -22], **CL), "lin"),
        (14, dict(hips=[4, -14, 0], hmove=[0, -0.10, 0.16], spine=[2, -10, 0], chest=[0, -6, 0], head=[-4, 8, 0],
                 hR=[0.12, 0.50, 0.14], dR=[0.2, 1.0, 0.0], rR=0, hL=[0.12, -0.05, 0.30],
                 fL=[0.20, 0.09, 0.30, 0, -6], fR=[0.20, 0.09, -0.12, 0, 10], **CL), "in"),
        (17, dict(hips=[30, 0, 0], hmove=[0, -0.28, 0.40], spine=[18, 0, 0], chest=[14, 0, 0], head=[-14, 0, 0],
                 hR=[0.04, -0.52, 0.56], dR=[0.7, -0.7, 0.0], rR=0, hL=[0.14, -0.15, 0.20],
                 fL=[0.16, 0.09, 0.46, 0, 0], fR=[0.15, 0.09, -0.26, 0, -4], **CL), "out"),
        (20, dict(hips=[30, 0, 0], hmove=[0, -0.28, 0.40], spine=[18, 0, 0], chest=[14, 0, 0], head=[-14, 0, 0],
                 hR=[0.04, -0.52, 0.56], dR=[0.7, -0.7, 0.0], rR=0, hL=[0.14, -0.15, 0.20],
                 fL=[0.16, 0.09, 0.46, 0, 0], fR=[0.15, 0.09, -0.26, 0, -4], **CL), "lin"),
        (25, {}, "smooth"),
    ]
    out.append(make_clip("saw_c_charge", 26, from_keys(ch, 26), False))

    # ---- saw, open: longer arcs, more body, heavier recovery. Same three hits + charge, wider
    OP = dict(eR=[0.8, -1.0, 0.1])
    OREADY = dict(hR=[0.10, -0.26, 0.30], dR=[1.0, -0.15, -0.15])
    o1 = [
        (0, OREADY, "lin"),
        (5, dict(hips=[2, -34, 0], hmove=[-0.05, -0.09, -0.04], spine=[3, -26, 0], chest=[2, -20, 0], head=[-3, 20, 0],
                 hR=[0.52, 0.06, 0.04], dR=[0.35, 0.05, -0.95], rR=-15, hL=[0.20, -0.20, 0.10],
                 fL=[0.22, 0.09, 0.12, 0, -4], fR=[0.28, 0.09, -0.22, 0, 30], **OP), "smooth"),
        (9, dict(hips=[5, 40, 0], hmove=[0.04, -0.13, 0.26], spine=[6, 30, 0], chest=[3, 22, 0], head=[-3, -20, 0],
                 hR=[-0.40, -0.08, 0.56], dR=[0.35, -0.05, 0.95], rR=12, hL=[0.24, -0.15, 0.04],
                 fL=[0.24, 0.09, 0.34, 0, 40], fR=[0.22, 0.09, -0.10, 0, -14], **OP), "in"),
        (11, dict(hips=[5, 46, 0], hmove=[0.04, -0.13, 0.28], spine=[6, 34, 0], chest=[3, 24, 0], head=[-3, -22, 0],
                 hR=[-0.46, -0.10, 0.46], dR=[0.15, -0.05, 1.0], rR=16, hL=[0.24, -0.15, 0.04],
                 fL=[0.24, 0.09, 0.34, 0, 44], fR=[0.22, 0.09, -0.10, 0, -14], **OP), "out"),
        (18, OREADY, "smooth"),
    ]
    out.append(make_clip("saw_o1", 19, from_keys(o1, 19), False))
    o2 = [
        (0, OREADY, "lin"),
        (4, dict(hips=[3, 36, 0], hmove=[0.05, -0.09, 0.0], spine=[3, 26, 0], chest=[2, 18, 0], head=[-3, -16, 0],
                 hR=[-0.40, -0.22, 0.34], dR=[0.30, -0.25, 0.95], rR=24, hL=[0.14, -0.22, 0.30],
                 fL=[0.22, 0.09, 0.26, 0, 32], fR=[0.22, 0.09, -0.14, 0, -12], **OP), "smooth"),
        (8, dict(hips=[8, -42, 0], hmove=[-0.04, -0.14, 0.26], spine=[7, -30, 0], chest=[3, -22, 0], head=[-3, 20, 0],
                 hR=[0.54, -0.20, 0.34], dR=[0.30, -0.2, -0.95], rR=-20, hL=[0.20, -0.25, 0.14],
                 fL=[0.22, 0.09, 0.08, 0, -12], fR=[0.24, 0.09, -0.34, 0, 26], **OP), "in"),
        (10, dict(hips=[8, -48, 0], hmove=[-0.04, -0.14, 0.28], spine=[7, -34, 0], chest=[3, -24, 0], head=[-3, 22, 0],
                 hR=[0.58, -0.22, 0.24], dR=[0.15, -0.2, -1.0], rR=-24, hL=[0.20, -0.25, 0.14],
                 fL=[0.22, 0.09, 0.08, 0, -12], fR=[0.24, 0.09, -0.34, 0, 30], **OP), "out"),
        (17, OREADY, "smooth"),
    ]
    out.append(make_clip("saw_o2", 18, from_keys(o2, 18), False))
    o3 = [
        (0, OREADY, "lin"),
        (5, dict(hips=[-6, 8, 0], hmove=[0, -0.02, -0.06], spine=[-10, 8, 0], chest=[-10, 6, 0], head=[3, 0, 0],
                 hR=[0.12, 0.50, -0.02], dR=[-0.25, 1.0, -0.2], rR=0, hL=[0.14, 0.14, 0.26],
                 fL=[0.15, 0.09, 0.10, 0, 0], fR=[0.18, 0.09, -0.24, 0, -10], **OP), "smooth"),
        (9, dict(hips=[26, -8, 0], hmove=[0, -0.22, 0.34], spine=[16, -6, 0], chest=[14, -4, 0], head=[-12, 0, 0],
                 hR=[0.06, -0.52, 0.54], dR=[0.8, -0.6, 0.0], rR=0, hL=[0.14, -0.18, 0.22],
                 fL=[0.16, 0.09, 0.40, 0, 0], fR=[0.15, 0.09, -0.26, 0, -4], **OP), "in"),
        (12, dict(hips=[28, -8, 0], hmove=[0, -0.24, 0.36], spine=[18, -6, 0], chest=[14, -4, 0], head=[-14, 0, 0],
                 hR=[0.06, -0.54, 0.50], dR=[0.65, -0.75, 0.0], rR=0, hL=[0.14, -0.18, 0.22],
                 fL=[0.16, 0.09, 0.40, 0, 0], fR=[0.15, 0.09, -0.26, 0, -4], **OP), "out"),
        (20, dict(hmove=[0, -0.12, 0.12]), "smooth"),
        (23, OREADY, "smooth"),
    ]
    out.append(make_clip("saw_o3", 24, from_keys(o3, 24), False))
    och = [
        (0, OREADY, "lin"),
        (8, dict(hips=[-6, 62, 0], hmove=[0, -0.22, -0.04], spine=[-6, 40, 0], chest=[-4, 30, 0], head=[2, -26, 0],
                 hR=[0.58, -0.04, -0.20], dR=[-0.4, 0.2, -0.95], rR=-35, hL=[0.20, -0.05, 0.20],
                 fL=[0.24, 0.09, 0.22, 0, 40], fR=[0.28, 0.09, -0.28, 0, -50], **OP), "smooth"),
        (12, dict(hips=[-6, 66, 0], hmove=[0.01, -0.23, -0.04], spine=[-6, 42, 0], chest=[-4, 32, 0], head=[2, -26, 0],
                 hR=[0.60, -0.02, -0.22], dR=[-0.4, 0.2, -0.95], rR=-35, hL=[0.20, -0.05, 0.20],
                 fL=[0.24, 0.09, 0.22, 0, 40], fR=[0.28, 0.09, -0.28, 0, -50], **OP), "lin"),
        (16, dict(hips=[-6, 62, 0], hmove=[-0.01, -0.22, -0.04], spine=[-6, 40, 0], chest=[-4, 30, 0], head=[2, -26, 0],
                 hR=[0.58, -0.04, -0.20], dR=[-0.4, 0.2, -0.95], rR=-35, hL=[0.20, -0.05, 0.20],
                 fL=[0.24, 0.09, 0.22, 0, 40], fR=[0.28, 0.09, -0.28, 0, -50], **OP), "lin"),
        (20, dict(hips=[10, -10, 0], hmove=[0, -0.16, 0.20], spine=[6, -8, 0], chest=[2, -6, 0], head=[-6, 6, 0],
                 hR=[0.20, 0.52, 0.10], dR=[0.15, 1.0, 0.0], rR=0, hL=[0.12, 0.0, 0.30],
                 fL=[0.20, 0.09, 0.32, 0, -6], fR=[0.20, 0.09, -0.12, 0, 10], **OP), "in"),
        (23, dict(hips=[34, 0, 0], hmove=[0, -0.30, 0.46], spine=[20, 0, 0], chest=[14, 0, 0], head=[-16, 0, 0],
                 hR=[0.04, -0.54, 0.58], dR=[0.75, -0.65, 0.0], rR=0, hL=[0.14, -0.15, 0.20],
                 fL=[0.16, 0.09, 0.50, 0, 0], fR=[0.15, 0.09, -0.28, 0, -4], **OP), "out"),
        (27, dict(hips=[34, 0, 0], hmove=[0, -0.30, 0.46], spine=[20, 0, 0], chest=[14, 0, 0], head=[-16, 0, 0],
                 hR=[0.04, -0.54, 0.58], dR=[0.75, -0.65, 0.0], rR=0, hL=[0.14, -0.15, 0.20],
                 fL=[0.16, 0.09, 0.50, 0, 0], fR=[0.15, 0.09, -0.28, 0, -4], **OP), "lin"),
        (32, OREADY, "smooth"),
    ]
    out.append(make_clip("saw_o_charge", 33, from_keys(och, 33), False))

    # ---- backstep: crouch, hop back (lean back, knees up), land, recover. In place.
    AIR = dict(hips=[-14, 0, 0], hmove=[0, 0.22, 0], spine=[-8, 0, 0], chest=[-6, 0, 0], head=[4, 0, 0],
               fL=[0.14, 0.40, 0.16, 18, 0], fR=[0.14, 0.34, 0.10, 18, 0], hL=[0.35, 0.05, 0.12], hR=[0.38, 0.02, 0.10],
               dL=[1.0, 0.2, 0.3], dR=[1.0, 0.2, -0.3])
    bs = [
        (0, {}, "lin"),
        (3, dict(hips=[16, 0, 0], hmove=[0, -0.26, 0.04], spine=[10, 0, 0], chest=[8, 0, 0], head=[-8, 0, 0],
                 fL=[0.18, 0.09, 0.10, 0, 0], fR=[0.18, 0.09, -0.10, 0, 0], hL=[0.10, -0.25, 0.30], hR=[0.10, -0.28, 0.28]), "smooth"),
        (6, AIR, "out"),
        (8, merge(AIR, dict(hmove=[0, 0.26, 0])), "smooth"),
        (11, dict(hips=[10, 0, 0], hmove=[0, -0.20, 0.0], spine=[8, 0, 0], chest=[6, 0, 0], head=[-6, 0, 0],
                  fL=[0.18, 0.09, 0.10, 0, 0], fR=[0.18, 0.09, -0.10, 0, 0], hL=[0.12, -0.25, 0.28], hR=[0.10, -0.28, 0.26]), "in"),
        (15, {}, "smooth"),
    ]
    out.append(make_clip("backstep", 16, from_keys(bs, 16), False))

    # ---- side dodges: bank into the dodge, the near foot steps out, the far foot drags in, recover
    def dodge(sign, name):
        s = sign                                         # +1 left, -1 right
        near, far = ("L", "R") if s > 0 else ("R", "L")
        def foot(side, out_, up, fwd):
            return {"f" + side: [out_, up, fwd, 0, 0]}
        k1 = dict(hips=[4, 0, 12 * s], hmove=[0.10 * s, -0.20, 0.0], spine=[4, 0, 6 * s], chest=[4, 0, 6 * s], head=[-4, 0, -8 * s],
                  hL=[0.22, -0.16, 0.26], hR=[0.22, -0.16, 0.26])
        k1.update(foot(near, 0.30, 0.09, 0.02)); k1.update(foot(far, 0.12, 0.09, -0.08))
        k2 = dict(hips=[5, 0, 18 * s], hmove=[0.30 * s, -0.14, 0.0], spine=[4, 0, 8 * s], chest=[4, 0, 10 * s], head=[-4, 0, -14 * s],
                  hL=[0.45, -0.05, 0.20], hR=[0.45, -0.05, 0.20])
        k2.update(foot(near, 0.50, 0.20, 0.0)); k2.update(foot(far, 0.10, 0.22, -0.10))
        k3 = dict(hips=[5, 0, 12 * s], hmove=[0.46 * s, -0.22, 0.0], spine=[4, 0, 6 * s], chest=[4, 0, 8 * s], head=[-4, 0, -10 * s],
                  hL=[0.30, -0.12, 0.26], hR=[0.30, -0.12, 0.26])
        k3.update(foot(near, 0.34, 0.09, 0.04)); k3.update(foot(far, -0.04, 0.09, -0.06))
        k4 = dict(hips=[4, 0, 4 * s], hmove=[0.22 * s, -0.12, 0.0], spine=[4, 0, 2 * s], chest=[3, 0, 3 * s],
                  fL=[0.15, 0.09, 0.0, 0, 0], fR=[0.15, 0.09, -0.06, 0, 0])
        ks = [(0, {}, "lin"), (2, k1, "smooth"), (4, k2, "out"), (6, k3, "in"), (9, k4, "smooth"), (11, {}, "smooth")]
        # the hips move is a slide that returns to the origin so the clip stays in place
        return make_clip(name, 12, from_keys(ks, 12), False)
    out.append(dodge(1, "dodge_l"))
    out.append(dodge(-1, "dodge_r"))

    # ---- forward roll: crouch, tuck, the pelvis turns a full 360 degrees, stand up. Feet in the pelvis frame while tucked
    TUCK = dict(fw=1.0, wL=[0.10, -0.26, 0.10], wR=[0.10, -0.26, 0.10], spine=[40, 0, 0], chest=[30, 0, 0], neck=[20, 0, 0], head=[30, 0, 0],
                hL=[0.02, -0.34, 0.22], hR=[0.02, -0.34, 0.22], dL=[0.6, -0.5, 0.0], dR=[0.6, -0.5, 0.0], eL=[0.8, -0.5, 0.6], eR=[0.8, -0.5, 0.6])
    rl = [
        (0, {}, "lin"),
        (2, dict(hips=[22, 0, 0], hmove=[0, -0.30, 0.10], spine=[24, 0, 0], chest=[18, 0, 0], head=[-4, 0, 0],
                 fL=[0.16, 0.09, 0.14, 0, 0], fR=[0.16, 0.09, -0.04, 0, 0], hL=[0.06, -0.30, 0.34], hR=[0.06, -0.30, 0.34]), "smooth"),
        (4, merge(TUCK, dict(hips=[70, 0, 0], hmove=[0, -0.36, 0.20])), "out"),
        (6, merge(TUCK, dict(hips=[160, 0, 0], hmove=[0, -0.44, 0.24])), "lin"),
        (8, merge(TUCK, dict(hips=[250, 0, 0], hmove=[0, -0.40, 0.20])), "lin"),
        (10, merge(TUCK, dict(hips=[330, 0, 0], hmove=[0, -0.34, 0.12])), "lin"),
        (12, dict(hips=[372, 0, 0], hmove=[0, -0.30, 0.06], spine=[24, 0, 0], chest=[16, 0, 0], head=[-4, 0, 0], fw=0.4,
                  fL=[0.16, 0.09, 0.12, 0, 0], fR=[0.16, 0.09, -0.06, 0, 0], wL=[0.1, -0.5, 0.05], wR=[0.1, -0.5, 0.05],
                  hL=[0.10, -0.30, 0.30], hR=[0.10, -0.30, 0.30]), "in"),
        (15, dict(hips=[363, 0, 0]), "smooth"),
    ]
    out.append(make_clip("roll", 16, from_keys(rl, 16), False))

    # ---- the fourth hit of each combo. Closed: a rising backhand from low right to high left, a step in.
    c4 = [
        (0, {}, "lin"),
        (3, dict(hips=[8, -24, 0], hmove=[-0.03, -0.14, -0.04], spine=[10, -18, 0], chest=[6, -12, 0], head=[-6, 12, 0],
                 hR=[0.34, -0.42, 0.12], dR=[0.5, -0.75, -0.45], rR=-25, hL=[0.14, -0.22, 0.26],
                 fL=[0.20, 0.09, 0.10, 0, 0], fR=[0.22, 0.09, -0.24, 0, 20], **CL), "smooth"),
        (7, dict(hips=[-2, 26, 0], hmove=[0.02, -0.06, 0.22], spine=[-8, 20, 0], chest=[-8, 14, 0], head=[2, -14, 0],
                 hR=[-0.18, 0.30, 0.48], dR=[0.35, 0.85, 0.40], rR=10, hL=[0.22, -0.15, 0.08],
                 fL=[0.22, 0.09, 0.32, 0, 30], fR=[0.20, 0.09, -0.08, 0, -10], **CL), "in"),
        (9, dict(hips=[-4, 30, 0], hmove=[0.02, -0.05, 0.24], spine=[-10, 22, 0], chest=[-10, 16, 0], head=[4, -16, 0],
                 hR=[-0.26, 0.44, 0.36], dR=[0.05, 0.95, 0.30], rR=14, hL=[0.22, -0.15, 0.08],
                 fL=[0.22, 0.09, 0.32, 0, 32], fR=[0.20, 0.09, -0.08, 0, -10], **CL), "out"),
        (13, dict(hips=[2, 10, 0], hmove=[0, -0.08, 0.12], spine=[2, 6, 0], chest=[0, 4, 0], head=[-4, -4, 0],
                  hR=[0.0, 0.10, 0.40], dR=[0.6, 0.5, 0.0], hL=[0.12, -0.22, 0.26],
                  fL=[0.18, 0.09, 0.20, 0, 10], fR=[0.18, 0.09, -0.12, 0, -8], **CL), "smooth"),
        (17, {}, "smooth"),
    ]
    out.append(make_clip("saw_c4", 18, from_keys(c4, 18), False))
    # Open: a full turn, the long blade out at arm's length (the pelvis turns 360 degrees, the clip ends facing front again)
    o4 = [
        (0, OREADY, "lin"),
        (5, dict(hips=[4, -42, 0], hmove=[-0.04, -0.12, -0.06], spine=[4, -28, 0], chest=[2, -18, 0], head=[-3, 24, 0],
                 hR=[0.52, -0.08, -0.08], dR=[0.2, -0.05, -0.97], rR=-20, hL=[0.22, -0.18, 0.08],
                 fL=[0.24, 0.09, 0.10, 0, -6], fR=[0.28, 0.09, -0.26, 0, 34], **OP), "smooth"),
        (8, dict(hips=[6, 70, 0], hmove=[0.02, -0.12, 0.10], spine=[5, 20, 0], chest=[3, 12, 0], head=[-3, -10, 0],
                 hR=[-0.20, -0.06, 0.58], dR=[0.55, -0.05, 0.82], rR=10, hL=[0.24, -0.15, 0.04],
                 fL=[0.24, 0.09, 0.30, 0, 40], fR=[0.22, 0.09, -0.12, 0, -14], **OP), "in"),
        (11, dict(hips=[6, 190, 0], hmove=[0.0, -0.14, 0.18], spine=[5, 16, 0], chest=[3, 10, 0], head=[-3, -10, 0],
                  hR=[-0.52, -0.06, 0.22], dR=[-0.15, -0.05, 0.98], rR=10, hL=[0.24, -0.15, 0.04],
                  fL=[0.24, 0.09, 0.30, 0, 40], fR=[0.22, 0.09, -0.12, 0, -14], **OP), "lin"),
        (14, dict(hips=[6, 300, 0], hmove=[0.0, -0.14, 0.24], spine=[5, 16, 0], chest=[3, 10, 0], head=[-3, -10, 0],
                  hR=[-0.10, -0.06, 0.54], dR=[0.85, -0.05, 0.50], rR=10, hL=[0.24, -0.15, 0.04],
                  fL=[0.24, 0.09, 0.30, 0, 40], fR=[0.22, 0.09, -0.12, 0, -14], **OP), "lin"),
        (17, dict(hips=[9, 398, 0], hmove=[0.03, -0.16, 0.32], spine=[7, 30, 0], chest=[4, 20, 0], head=[-3, -22, 0],
                  hR=[-0.46, -0.10, 0.46], dR=[0.15, -0.05, 1.0], rR=16, hL=[0.24, -0.15, 0.04],
                  fL=[0.24, 0.09, 0.34, 0, 44], fR=[0.22, 0.09, -0.10, 0, -14], **OP), "out"),
        (21, dict(hips=[6, 372, 0], hmove=[0.0, -0.12, 0.16], spine=[4, 10, 0], chest=[2, 6, 0], head=[-3, -8, 0],
                  hR=[-0.10, -0.20, 0.40], dR=[0.8, -0.2, 0.5], hL=[0.14, -0.20, 0.20],
                  fL=[0.22, 0.09, 0.22, 0, 20], fR=[0.22, 0.09, -0.12, 0, -8], **OP), "smooth"),
        (25, merge(OREADY, dict(hips=[3, 360, 0])), "smooth"),
    ]
    out.append(make_clip("saw_o4", 26, from_keys(o4, 26), False))

    # ---- the transform attack: a wide sweep from across the body out to the right with the arm at full length; the game
    # changes the saw's form in the middle of it (frame 6), so the blade opens or closes inside the cut
    tr = [
        (0, {}, "lin"),
        (3, dict(hips=[4, 32, 0], hmove=[0.03, -0.08, 0.0], spine=[4, 22, 0], chest=[2, 16, 0], head=[-3, -14, 0],
                 hR=[-0.32, -0.16, 0.40], dR=[0.30, -0.25, 0.92], rR=24, hL=[0.14, -0.22, 0.30],
                 fL=[0.22, 0.09, 0.24, 0, 30], fR=[0.22, 0.09, -0.14, 0, -12], **OP), "smooth"),
        (7, dict(hips=[6, -40, 0], hmove=[-0.04, -0.12, 0.18], spine=[6, -28, 0], chest=[3, -20, 0], head=[-3, 18, 0],
                 hR=[0.54, -0.06, 0.34], dR=[0.42, 0.05, -0.90], rR=-30, hL=[0.20, -0.25, 0.14],
                 fL=[0.22, 0.09, 0.08, 0, -12], fR=[0.24, 0.09, -0.34, 0, 26], **OP), "in"),
        (9, dict(hips=[6, -46, 0], hmove=[-0.04, -0.12, 0.20], spine=[6, -32, 0], chest=[3, -22, 0], head=[-3, 20, 0],
                 hR=[0.58, 0.0, 0.24], dR=[0.28, 0.10, -0.95], rR=-34, hL=[0.20, -0.25, 0.14],
                 fL=[0.22, 0.09, 0.08, 0, -12], fR=[0.24, 0.09, -0.34, 0, 30], **OP), "out"),
        (15, OREADY, "smooth"),
    ]
    out.append(make_clip("transform", 16, from_keys(tr, 16), False))

    # ---- a flinch when hit, and the death: the knees give, the body falls on its back
    hu = [
        (0, {}, "lin"),
        (2, dict(hips=[-8, 0, 0], hmove=[0, -0.05, -0.10], spine=[-10, 0, 4], chest=[-8, 0, 2], head=[10, 0, -6],
                 hL=[0.26, -0.14, 0.18], hR=[0.22, -0.18, 0.18], dR=[0.8, -0.4, -0.3],
                 fL=[0.18, 0.09, 0.06, 0, 0], fR=[0.18, 0.09, -0.16, 0, 0]), "out"),
        (5, dict(hips=[-3, 0, 0], hmove=[0, -0.07, -0.05], spine=[2, 0, 2], head=[-4, 0, -3]), "smooth"),
        (8, {}, "smooth"),
    ]
    out.append(make_clip("hurt", 9, from_keys(hu, 9), False))
    de = [
        (0, {}, "lin"),
        (3, dict(hips=[-2, 0, 0], hmove=[0, -0.22, -0.05], spine=[6, 0, 0], head=[-6, 0, 0], fL=[0.14, 0.09, 0.0, 0, 0], fR=[0.14, 0.09, 0.0, 0, 0],
                 wL=[0.1, -0.5, 0.25], wR=[0.1, -0.5, 0.25], hL=[0.12, -0.30, 0.28], hR=[0.10, -0.32, 0.26]), "smooth"),
        (8, dict(hips=[-48, 0, 6], hmove=[0, -0.55, -0.35], spine=[-8, 0, 0], chest=[-6, 0, 0], neck=[0, 0, 0], head=[10, 0, 0], fw=1.0,
                 wL=[0.12, -0.7, 0.2], wR=[0.14, -0.7, 0.1], hL=[0.3, -0.2, -0.1], hR=[0.3, -0.2, -0.1], dL=[0.5, -0.5, 0.5], dR=[0.5, -0.5, 0.5]), "in"),
        (13, dict(hips=[-88, 0, 4], hmove=[0, -0.78, -0.55], spine=[0, 0, 0], chest=[0, 0, 0], neck=[0, 0, 0], head=[18, 0, 0], fw=1.0,
                  wL=[0.14, -0.78, 0.1], wR=[0.16, -0.76, 0.0], hL=[0.5, -0.1, -0.1], hR=[0.5, -0.1, -0.1], dL=[0.2, -0.3, 1.0], dR=[0.2, -0.3, -1.0],
                  gL=0.3, gR=0.3), "out"),
        (21, dict(hips=[-90, 0, 4], hmove=[0, -0.80, -0.55], spine=[0, 0, 0], chest=[0, 0, 0], neck=[0, 0, 0], head=[18, 0, 0], fw=1.0,
                  wL=[0.14, -0.78, 0.1], wR=[0.16, -0.76, 0.0], hL=[0.5, -0.1, -0.1], hR=[0.5, -0.1, -0.1], dL=[0.2, -0.3, 1.0], dR=[0.2, -0.3, -1.0],
                  gL=0.3, gR=0.3), "lin"),
    ]
    out.append(make_clip("death", 22, from_keys(de, 22), False))
    return out


if __name__ == "__main__":
    cs = clips()
    for c in cs:
        print(c["name"], c["frames"], "frames")
