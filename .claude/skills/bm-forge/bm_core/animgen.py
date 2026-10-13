"""Clips (ANIM keys) from a simple keyframe description.

What the format and the runtime can play (src/bm/format.c:275-296,
src/bm/runtime.c:2230-2271, ported in skin.pose_at):
- one interpolation MODE per clip (u8: 0 linear, 1 smooth = smoothstep
  u*u*(3-2u), 2 step), not per key;
- a key = f32 time in seconds + one pose (q rotation relative to the parent,
  t translation) for EVERY bone; quaternions slerp the short way
  (src/bm/runtime.c:2209-2223), translations lerp;
- loop flag (clip flags bit 0): t mod length, the last key blends into the
  first across the wrap; else clamp.
bm Animator's constraints mirrored here (carts/animator/main.lua): keys sit
on a 12 frames/s grid (snap_t :831, T.FPS src/script/bm3d.lua:32); one key
per frame (key_at :815, set_key :819-823); keys within 0..length (:1005); a
length is whole frames and >= max(1/12 s, last key) (:1029-1034); a new clip
is smooth and looping (:912-913); at most 255 clips (:909); mirror = the
.L/.R bone's pose with q (x,-y,-z,w), t (-x,y,z) (mirror_pose :951-961).

Description (dict or JSON file; unknown keys refused):

    {"name": "walk", "loop": true, "frames": 12,     # or "length": seconds (snapped)
     "curve": "smooth",                               # default curve of every key
     "keys": [
       {"frame": 0, "pose": {"leg.L": {"rot": [25, 0, 0]}, "hips": {"move": [0, 0.04, 0]}}},
       {"frame": 6, "mirror": 0},                     # the pose of frame 0, left <-> right
       {"time": 0.75, "curve": "ease-in", "pose": {...}}]}

A bone entry: one of "rot" [x, y, z] degrees (R = Rz Ry Rx: x first, then
y, then z, in the parent's frame), "q" [x, y, z, w] (unit within 1e-3),
"axis" + "angle" (degrees); plus "move" [x, y, z] (the pose t, added to the
bone head in the parent's frame, src/bm/runtime.c:2273). Missing rotation =
identity, missing move = 0. A bone not named in a key takes, at that key,
its own track's value (interpolated between the keys that do name it, with
their curves); a bone named in no key stays at rest (identity, as bm
Animator's rest_pose and bmmesh's missing poses, scripts/bmmesh.py:115-120).

Curves: "linear", "smooth" (alias "ease-in-out"), "step" are the runtime's
modes; "ease-in" (u*u) and "ease-out" (1-(1-u)^2) are not, nor are mixed
curves in one clip. The curve of a key applies from that key to the next
(for a loop the last key's curve covers the wrap). When the clip is not one
native mode, or is "smooth" and a bone must be filled between its own keys
(smoothstep over a split segment is not the same motion), the clip is BAKED:
one key on every frame (12/s), mode 0 linear. Baked clips match the intended
curve exactly on the 12 fps frames; between frames (the console draws at up
to 60 fps) the motion is linear. linear/step clips with filled bones stay
native and exact (a split slerp arc is the same arc; a held step stays held).

Ranges (ValidationError on anything else):
- frame: integer >= 0; time: finite seconds, snapped to round(t*12)/12;
  length 1..12e6 frames (0 < length < 1e6 s, src/bm/format.c:277);
- keys after baking: 1..1024 per clip (src/bm/bm.h:181); clips <= 255 (bm.h:180);
- move: finite in (-1e6, 1e6), the MESH vertex range (src/bm/format.c:157;
  the ANIM check alone allows |v| < 1e9, format.c:241: strict);
- rot / angle: finite degrees in (-1e6, 1e6); between two consecutive keys of
  one bone (and across the wrap of a loop) no "rot" axis may change by 180
  degrees or more: slerp takes the short way (src/bm/runtime.c:2209-2223),
  so the runtime would turn the other way; add a key in between;
- q: 4 finite numbers, | |q| - 1 | <= 1e-3 (the runtime normalises,
  runtime.c:2201-2206; a far-from-unit q is a mistake, strict), stored unit.
"""
import json
import math

from . import anim as animmod
from . import skelgen
from .binary import name_bytes
from .errors import ValidationError, need
from .skin import quat_mul, quat_norm, quat_slerp

FPS = 12                         # src/script/bm3d.lua:32, carts/animator/main.lua:831
NATIVE = {"linear": 0, "smooth": 1, "step": 2}          # src/bm/format.c:277, bm3d.lua:33
CURVES = ("linear", "smooth", "ease-in-out", "step", "ease-in", "ease-out")
ALIAS = {"ease-in-out": "smooth"}
MOVE_LIMIT = 1e6                 # src/bm/format.c:157
ANGLE_LIMIT = 1e6
Q_TOL = 1e-3
FRAMES_MAX = int(animmod.LENGTH_MAX * FPS) - 1          # length < 1e6 s (src/bm/format.c:277)
CLIP_KEYS = {"name", "loop", "length", "frames", "curve", "keys", "comment"}
KEY_KEYS = {"frame", "time", "curve", "pose", "mirror", "comment"}
BONE_KEYS = {"rot", "q", "axis", "angle", "move"}
CLOSE = 1e-5                     # a closing loop key must equal the first key this closely


def ease(curve, u):
    """the curve of a segment at u in 0..1: smooth / step as runtime.c:2260-2264
    (bm Animator ease, main.lua:772-776); ease-in / ease-out are bm-forge's"""
    if curve == "step":
        return 0.0
    if curve == "smooth":
        return u * u * (3 - 2 * u)
    if curve == "ease-in":
        return u * u
    if curve == "ease-out":
        return 1 - (1 - u) * (1 - u)
    return u


def euler_quat(rx, ry, rz):
    """degrees -> unit q (x, y, z, w) of R = Rz Ry Rx (x applied first)"""
    def axis(k, deg):
        h = math.radians(deg) / 2
        q = [0.0, 0.0, 0.0, math.cos(h)]
        q[k] = math.sin(h)
        return q
    return quat_norm(quat_mul(axis(2, rz), quat_mul(axis(1, ry), axis(0, rx))))


def _num(x, limit, what, where):
    need(isinstance(x, (int, float)) and not isinstance(x, bool), f"{what} {x!r} is not a number", where)
    need(math.isfinite(x) and -limit < x < limit, f"{what} {x!r} not finite in (-{limit:g}, {limit:g})", where)
    return float(x)


def _vec(v, n, limit, what, where):
    need(isinstance(v, (list, tuple)) and len(v) == n, f"{what} must be {n} numbers, not {v!r}", where)
    return [_num(x, limit, what, where) for x in v]


class _BP:
    """one bone at one key: q, t, and the Euler degrees it came from (or None)"""
    __slots__ = ("q", "t", "rot")

    def __init__(self, q, t, rot):
        self.q, self.t, self.rot = q, t, rot

    def mirrored(self):
        """bm Animator mirror_pose (carts/animator/main.lua:958): q (x,-y,-z,w), t (-x,y,z);
        Euler (a,-b,-c) is the same reflection of R = Rz Ry Rx"""
        rot = None if self.rot is None else [self.rot[0], -self.rot[1], -self.rot[2]]
        return _BP([self.q[0], -self.q[1], -self.q[2], self.q[3]], [-self.t[0], self.t[1], self.t[2]], rot)


def _bone_pose(spec, where):
    need(isinstance(spec, dict), "a bone pose is a JSON object", where)
    extra = set(spec) - BONE_KEYS
    need(not extra, f"unknown keys {sorted(extra)} (allowed: {sorted(BONE_KEYS)})", where)
    kinds = [k for k in ("rot", "q", "axis") if k in spec]
    need(len(kinds) <= 1, f"{kinds}: give one rotation (rot, q or axis+angle)", where)
    need(("axis" in spec) == ("angle" in spec), "'axis' and 'angle' go together", where)
    rot = None
    if "rot" in spec:
        rot = _vec(spec["rot"], 3, ANGLE_LIMIT, "rot (degrees)", where)
        q = euler_quat(*rot)
    elif "q" in spec:
        q = _vec(spec["q"], 4, 1e9, "q", where)
        n = math.sqrt(sum(x * x for x in q))
        need(abs(n - 1) <= Q_TOL, f"q {spec['q']!r} has length {n:.6g}, not 1 (within {Q_TOL})", where)
        q = quat_norm(q)
    elif "axis" in spec:
        ax = _vec(spec["axis"], 3, 1e9, "axis", where)
        n = math.sqrt(sum(x * x for x in ax))
        need(n > 1e-9, "axis is zero", where)
        h = math.radians(_num(spec["angle"], ANGLE_LIMIT, "angle (degrees)", where)) / 2
        q = quat_norm([ax[0] / n * math.sin(h), ax[1] / n * math.sin(h), ax[2] / n * math.sin(h), math.cos(h)])
    else:
        q = [0.0, 0.0, 0.0, 1.0]
    t = _vec(spec["move"], 3, MOVE_LIMIT, "move", where) if "move" in spec else [0.0, 0.0, 0.0]
    return _BP(q, t, rot)


def _curve(c, where):
    need(c in CURVES, f"curve {c!r}: one of {list(CURVES)}", where)
    return ALIAS.get(c, c)


def _frame_of(k, where, snapped):
    has_f, has_t = "frame" in k, "time" in k
    need(has_f != has_t, "give either 'frame' or 'time'", where)
    if has_f:
        f = k["frame"]
        need(isinstance(f, int) and not isinstance(f, bool) and 0 <= f <= FRAMES_MAX,
             f"frame {f!r} must be an integer 0..{FRAMES_MAX}", where)
        return f
    t = _num(k["time"], animmod.LENGTH_MAX, "time (s)", where)
    need(t >= 0, f"time {t} < 0", where)
    f = int(round(t * FPS))
    if abs(f - t * FPS) > 1e-6:
        snapped.append((t, f / FPS))
    return f


def _eval(track, f, loop, F):
    """a bone's own track at frame f: port of skin.pose_at's search (runtime.c:2230-2271)
    with each segment's curve -> (q, t)"""
    n = len(track)
    if n == 1:
        e = track[0][1]
        return e.q, e.t
    if loop:
        f = math.fmod(f, F)
        if f < 0:
            f += F
    first, last = track[0][0], track[-1][0]
    if not loop and f <= first:
        return track[0][1].q, track[0][1].t
    if not loop and f >= last:
        return track[-1][1].q, track[-1][1].t
    if first <= f < last:
        a = 0
        while a + 1 < n and track[a + 1][0] <= f:
            a += 1
        b = a + 1
        ta, tb = track[a][0], track[b][0]
    else:
        a, b = n - 1, 0
        ta = last - F if f < first else last
        tb = first if f < first else first + F
    u = (f - ta) / (tb - ta) if tb > ta else 0.0
    u = ease(track[a][2], u)
    ea, eb = track[a][1], track[b][1]
    return quat_slerp(ea.q, eb.q, u), [ea.t[k] + (eb.t[k] - ea.t[k]) * u for k in range(3)]


def _check_turns(bone, track, loop, where):
    pairs = list(zip(track, track[1:]))
    if loop and len(track) > 1:
        pairs.append((track[-1], track[0]))
    for (fa, a, _), (fb, b, _) in pairs:
        if a.rot is None or b.rot is None:
            continue
        for ax in range(3):
            d = abs(b.rot[ax] - a.rot[ax])
            need(d < 180, f"bone {bone!r} rot {'xyz'[ax]} goes {a.rot[ax]:g} -> {b.rot[ax]:g} degrees between "
                 f"frames {fa} and {fb}: 180 or more, slerp would turn the short way "
                 "(src/bm/runtime.c:2209-2223); add a key in between", where)


def load(path_or_dict):
    if isinstance(path_or_dict, dict):
        return path_or_dict
    with open(path_or_dict, encoding="utf-8") as f:
        return json.load(f)


def clip(desc, rig, report=None):
    """description (dict or JSON path) + the rig it animates -> anim.Clip.
    report (a dict, optional) receives: mode, baked, reason, keys, frames,
    snapped [(time asked, time used)]."""
    d = load(desc)
    need(isinstance(d, dict), "a clip description is a JSON object", "clip")
    need("name" in d, "no 'name'", "clip")
    name = d["name"]
    name_bytes(name, "clip", "clip name")
    w = f"clip {name!r}"
    extra = set(d) - CLIP_KEYS
    need(not extra, f"unknown keys {sorted(extra)} (allowed: {sorted(CLIP_KEYS)})", w)
    loop = d.get("loop", True)                     # new clips loop (carts/animator/main.lua:912)
    need(isinstance(loop, bool), "'loop' is true or false", w)
    default = _curve(d.get("curve", "smooth"), w)  # new clips are smooth (main.lua:912)
    keys = d.get("keys")
    need(isinstance(keys, list) and keys, "'keys' must be a non-empty list", w)
    bones = {b.name: i for i, b in enumerate(rig.bones)}
    snapped = []
    raw = []
    for i, k in enumerate(keys):
        kw = f"{w} key {i}"
        need(isinstance(k, dict), "a key is a JSON object", kw)
        extra = set(k) - KEY_KEYS
        need(not extra, f"unknown keys {sorted(extra)} (allowed: {sorted(KEY_KEYS)})", kw)
        f = _frame_of(k, kw, snapped)
        raw.append((f, k, _curve(k.get("curve", default), kw), kw))
    raw.sort(key=lambda r: r[0])
    for (fa, _, _, _), (fb, _, _, kw) in zip(raw, raw[1:]):
        need(fa != fb, f"two keys on frame {fa} (bm Animator keeps one key per frame, "
             "carts/animator/main.lua:815-823)", kw)
    last = raw[-1][0]
    has_len, has_fr = "length" in d, "frames" in d
    need(not (has_len and has_fr), "give 'length' or 'frames', not both", w)
    if has_fr:
        F = d["frames"]
        need(isinstance(F, int) and not isinstance(F, bool) and 1 <= F <= FRAMES_MAX,
             f"frames {F!r} must be an integer 1..{FRAMES_MAX}", w)
    elif has_len:
        L = _num(d["length"], animmod.LENGTH_MAX, "length (s)", w)
        F = int(round(L * FPS))
        if abs(F - L * FPS) > 1e-6:
            snapped.append((L, F / FPS))
        need(F >= 1, f"length {L} s is under one frame (1/{FPS} s, carts/animator/main.lua:1032)", w)
    else:
        need(not loop, "a looping clip needs 'frames' or 'length'", w)
        F = max(1, last)
    need(last <= F, f"a key on frame {last}, past the length of {F} frames (carts/animator/main.lua:1005)", w)
    # poses: plain keys first, then mirrors of plain keys
    poses = {}
    byframe = {f: k for f, k, _, _ in raw}
    for f, k, _, kw in raw:
        if "mirror" in k:
            continue
        need("pose" in k, "a key needs 'pose' or 'mirror'", kw)
        poses[f] = _parse_pose(k["pose"], bones, kw)
    for f, k, _, kw in raw:
        if "mirror" not in k:
            continue
        src = k["mirror"]
        need(isinstance(src, int) and not isinstance(src, bool) and src in byframe,
             f"mirror {src!r}: the frame of another key", kw)
        need("mirror" not in byframe[src], f"mirror {src}: that key is a mirror itself", kw)
        out = {}
        for bn, bp in poses[src].items():
            other = skelgen.mirror_name(bn)
            tgt = other if other is not None else bn
            need(tgt in bones, f"mirror of bone {bn!r} is {tgt!r}, not in the rig", kw)
            out[tgt] = bp.mirrored()
        out.update(_parse_pose(k.get("pose", {}), bones, kw))
        poses[f] = out
    frames = [f for f, _, _, _ in raw]
    curves = {f: c for f, _, c, _ in raw}
    tracks = {}
    for f in frames:
        for bn, bp in poses[f].items():
            tracks.setdefault(bn, []).append((f, bp, curves[f]))
    for bn, tr in tracks.items():
        _check_turns(bn, tr, loop, w)
    if loop and last == F and 0 in poses:
        for bn in set(poses[F]) | set(poses[0]):
            a = poses[F].get(bn)
            b = poses[0].get(bn)
            qa, ta = (a.q, a.t) if a else ([0, 0, 0, 1], [0, 0, 0])
            qb, tb = (b.q, b.t) if b else ([0, 0, 0, 1], [0, 0, 0])
            dq = abs(abs(sum(qa[k] * qb[k] for k in range(4))) - 1)
            need(dq < CLOSE and all(abs(ta[k] - tb[k]) < CLOSE for k in range(3)),
                 f"looping clip: the key on the last frame ({F}) must equal frame 0 (bone {bn!r} differs); "
                 "drop it, the loop already blends back to frame 0", w)
    kinds = set(curves.values())
    full = all(set(poses[f]) == set(tracks) for f in frames)
    if len(kinds) > 1:
        reason = f"mixed curves {sorted(kinds)}: the format has one mode per clip"
    elif next(iter(kinds)) not in NATIVE:
        reason = f"curve {next(iter(kinds))!r} is not a runtime mode (linear, smooth, step)"
    elif next(iter(kinds)) == "smooth" and not full:
        reason = "smooth with bones filled between their own keys"
    else:
        reason = None
    baked = reason is not None
    if baked:
        out_frames = list(range(F)) if loop else list(range(max(last, 0) + 1))
        mode = 0
    else:
        out_frames = frames
        mode = NATIVE[next(iter(kinds))]
    need(len(out_frames) <= animmod.KEYS_MAX,
         f"{len(out_frames)} keys{' after baking' if baked else ''}, at most {animmod.KEYS_MAX} "
         f"(src/bm/bm.h:181){': ' + reason + ' -> one key per frame' if baked else ''}", w)
    nb = len(rig.bones)
    out = []
    for f in out_frames:
        pose = [animmod.Pose() for _ in range(nb)]
        for bn, tr in tracks.items():
            q, t = _eval(tr, f, loop, F)
            pose[bones[bn]] = animmod.Pose(list(q), list(t))
        out.append(animmod.Key(f / FPS, pose))
    c = animmod.Clip(name, F / FPS, out, mode, animmod.LOOP if loop else 0)
    if report is not None:
        report.update({"mode": mode, "baked": baked, "reason": reason, "keys": len(out), "frames": F,
                       "snapped": snapped})
    return c


def _parse_pose(p, bones, where):
    need(isinstance(p, dict), "'pose' is an object {bone name: bone pose}", where)
    out = {}
    for bn, spec in p.items():
        need(bn in bones, f"bone {bn!r} is not in the rig", where)
        out[bn] = _bone_pose(spec, f"{where} bone {bn!r}")
    return out


def add_clips(rig, descs, reports=None):
    """a copy of rig with the clips of `descs` (dicts or JSON paths) appended;
    at most 255 clips (src/bm/bm.h:180, carts/animator/main.lua:909), names unique.
    -> anim.Rig, checked by anim.validate_rig"""
    clips = list(rig.clips)
    for dsc in descs:
        rep = {}
        clips.append(clip(dsc, rig, rep))
        if reports is not None:
            reports.append(rep)
    w = f"rig {rig.model!r}"
    need(len(clips) <= animmod.CLIPS_MAX, f"{len(clips)} clips, at most {animmod.CLIPS_MAX} (src/bm/bm.h:180)", w)
    seen = set()
    for c in clips:
        need(c.name not in seen, f"clip name {c.name!r} twice", w)
        seen.add(c.name)
    out = animmod.Rig(rig.model, list(rig.bones), list(rig.vbones), clips)
    if out.vbones:
        animmod.validate_rig(out)
    return out


def write_anim(cart, rigs, mesh=None):
    """put an ANIM section (and the MESH, if given) into a Cart; every rig must
    be bound to a model of the MESH (anim.validate_links). -> cart"""
    a = animmod.Anim(list(rigs))
    if mesh is not None:
        cart.set_mesh(mesh)
    animmod.validate_links(a, mesh if mesh is not None else cart.mesh())
    cart.set_anim(a)
    return cart


__all__ = ["FPS", "CURVES", "NATIVE", "ease", "euler_quat", "clip", "add_clips", "write_anim", "ValidationError"]
