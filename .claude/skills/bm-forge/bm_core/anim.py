"""ANIM section (type 9): skeletons and clips of bm Animator.

Layout: src/bm/bm.h:61-76, limits bm.h:179-183; checker rig_walk /
bm_anim_check (src/bm/format.c:245-320). Reference: references/FORMATS.md
"ANIM section". Semantics (playback) are not ported here: runtime.c pose_at /
skin (src/bm/runtime.c:2230-2321).
"""
import struct
from dataclasses import dataclass, field
from typing import List

from .binary import NAME_FIELD, Reader, f32, finite_in, name_bytes, pad4, read_name
from .errors import need
from . import mesh as meshmod

BONES_MAX = 64          # bm.h:179
CLIPS_MAX = 255         # bm.h:180
KEYS_MAX = 1024         # bm.h:181
BONE_SIZE = 44          # bm.h:182
POSE_SIZE = 28          # bm.h:183
HEAD = 8                # u16 rigs, u16 0, u32 0 (src/bm/format.c:237)
RIG_HEAD = NAME_FIELD + 8
CLIP_HEAD = NAME_FIELD + 8
MODES = {0: "linear", 1: "smooth", 2: "step"}      # src/bm/format.c:277, bm.h:61-76
LOOP = 1                # clip flags bit 0
FIN = 1e9               # fin32, src/bm/format.c:241
LENGTH_MAX = 1e6        # src/bm/format.c:277


@dataclass
class Bone:
    """44 bytes: name, parent (-1 or an earlier index), u16 0, head[3], tail[3]
    (rest pose) (src/bm/format.c:255-263)."""
    name: str
    parent: int = -1
    head: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    tail: List[float] = field(default_factory=lambda: [0.0, 1.0, 0.0])

    def to_dict(self):
        return {"name": self.name, "parent": self.parent, "head": list(self.head), "tail": list(self.tail)}

    @classmethod
    def from_dict(cls, d):
        return cls(d["name"], int(d.get("parent", -1)), [float(x) for x in d["head"]],
                   [float(x) for x in d["tail"]])


@dataclass
class Pose:
    """one bone in one key, 28 bytes: q (x,y,z,w) relative to the parent, t translation"""
    q: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0, 1.0])
    t: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])

    def to_dict(self):
        return {"q": list(self.q), "t": list(self.t)}

    @classmethod
    def from_dict(cls, d):
        return cls([float(x) for x in d["q"]], [float(x) for x in d["t"]])


@dataclass
class Key:
    """f32 time (s) then one Pose per bone (src/bm/format.c:286-296)"""
    time: float
    pose: List[Pose] = field(default_factory=list)

    def to_dict(self):
        return {"time": self.time, "pose": [p.to_dict() for p in self.pose]}

    @classmethod
    def from_dict(cls, d):
        return cls(float(d["time"]), [Pose.from_dict(p) for p in d["pose"]])


@dataclass
class Clip:
    """head 24 B: name, u16 keys, u8 mode (0 linear, 1 smooth, 2 step), u8 flags
    (bit 0 loop; other bits kept), f32 length (src/bm/format.c:275-278)."""
    name: str
    length: float
    keys: List[Key] = field(default_factory=list)
    mode: int = 1
    flags: int = LOOP

    @property
    def loop(self):
        return bool(self.flags & LOOP)

    def to_dict(self):
        return {"name": self.name, "length": self.length, "mode": self.mode, "flags": self.flags,
                "keys": [k.to_dict() for k in self.keys]}

    @classmethod
    def from_dict(cls, d):
        flags = int(d["flags"]) if "flags" in d else (LOOP if d.get("loop", True) else 0)
        return cls(d["name"], float(d["length"]), [Key.from_dict(k) for k in d["keys"]],
                   int(d.get("mode", 1)), flags)


@dataclass
class Rig:
    """a skeleton for the MESH model of the same name: bones, the bone of each
    vertex (len == the model's nverts, src/bm/runtime.c:2161), clips."""
    model: str
    bones: List[Bone] = field(default_factory=list)
    vbones: List[int] = field(default_factory=list)
    clips: List[Clip] = field(default_factory=list)

    def to_dict(self):
        return {"model": self.model, "bones": [b.to_dict() for b in self.bones], "vbones": list(self.vbones),
                "clips": [c.to_dict() for c in self.clips]}

    @classmethod
    def from_dict(cls, d):
        return cls(d["model"], [Bone.from_dict(b) for b in d["bones"]], [int(x) for x in d["vbones"]],
                   [Clip.from_dict(c) for c in d["clips"]])


@dataclass
class Anim:
    rigs: List[Rig] = field(default_factory=list)

    def get(self, model):
        for r in self.rigs:
            if r.model == model:
                return r
        return None

    def to_dict(self):
        return {"rigs": [r.to_dict() for r in self.rigs]}

    @classmethod
    def from_dict(cls, d):
        return cls([Rig.from_dict(r) for r in d["rigs"]])


def _unique(names, what, where):
    seen = set()
    for n in names:
        key = n.encode("utf-8")
        need(key not in seen, f"{what} name {n!r} twice", where)
        seen.add(key)


def validate_rig(rig, kernel_limits=False, keep_long_names=False):
    """the checks of rig_walk (src/bm/format.c:245-299, deep) plus bm-forge rules:
    unique bone and clip names, bone/clip names not empty (C does not check
    them: strict choice), names <= 15 bytes on write, vertex count within the
    tool limits by default."""
    w = f"ANIM rig {rig.model!r}"
    name_bytes(rig.model, w, "rig model name", allow_full=keep_long_names)
    nb, nc, nv = len(rig.bones), len(rig.clips), len(rig.vbones)
    mv, _ = meshmod.limits(kernel_limits)
    need(1 <= nb <= BONES_MAX, f"{nb} bones, 1 to {BONES_MAX}", w)
    need(nc <= CLIPS_MAX, f"{nc} clips, 0 to {CLIPS_MAX}", w)
    need(1 <= nv <= mv, f"{nv} vertices, 1 to {mv}", w)
    for i, b in enumerate(rig.bones):
        bw = f"{w} bone {i}"
        name_bytes(b.name, bw, "bone name", allow_full=keep_long_names)
        need(isinstance(b.parent, int) and -1 <= b.parent < i,
             f"parent {b.parent}: -1 or an earlier bone (src/bm/format.c:257-259)", bw)
        need(len(b.head) == 3 and len(b.tail) == 3, "head and tail are 3 floats", bw)
        need(all(finite_in(x, FIN) for x in list(b.head) + list(b.tail)), "head/tail not finite (|v| < 1e9)", bw)
    _unique([b.name for b in rig.bones], "bone", w)
    for i, v in enumerate(rig.vbones):
        need(isinstance(v, int) and 0 <= v < nb, f"vertex {i} on bone {v}, 0 to {nb - 1}", w)
    for ci, c in enumerate(rig.clips):
        cw = f"{w} clip {c.name!r}"
        name_bytes(c.name, cw, "clip name", allow_full=keep_long_names)
        need(1 <= len(c.keys) <= KEYS_MAX, f"{len(c.keys)} keys, 1 to {KEYS_MAX}", cw)
        need(c.mode in MODES, f"mode {c.mode}, 0 linear / 1 smooth / 2 step", cw)
        need(0 <= c.flags <= 255, "flags out of u8", cw)
        need(finite_in(c.length, LENGTH_MAX) and f32(c.length) > 0, f"length {c.length}: 0 < length < 1e6", cw)
        top = f32(f32(c.length) + f32(1e-4))
        prev = 0.0
        for ki, k in enumerate(c.keys):
            t = f32(k.time) if finite_in(k.time, FIN) else float("nan")
            need(prev <= t <= top, f"key {ki} time {k.time}: not decreasing, 0..length "
                 "(src/bm/format.c:290)", cw)
            prev = t
            need(len(k.pose) == nb, f"key {ki} has {len(k.pose)} poses, the rig has {nb} bones", cw)
            for p in k.pose:
                need(len(p.q) == 4 and len(p.t) == 3, f"key {ki}: a pose is q[4] + t[3]", cw)
                need(all(finite_in(x, FIN) for x in list(p.q) + list(p.t)), f"key {ki}: pose not finite", cw)
    _unique([c.name for c in rig.clips], "clip", w)


def validate(anim, kernel_limits=False, keep_long_names=False):
    """every rig, rig model names unique in the section (strict: C takes the
    first match, src/bm/format.c:346-350)"""
    need(len(anim.rigs) <= 0xFFFF, "too many rigs", "ANIM")
    for r in anim.rigs:
        validate_rig(r, kernel_limits, keep_long_names)
    _unique([r.model for r in anim.rigs], "rig model", "ANIM")


def validate_links(anim, mesh):
    """each rig must move a MESH model of its name with as many vertices
    (src/bm/runtime.c:2161; the kernel skips a rig that does not fit, bm-forge
    refuses it). A rig with no MESH model is refused too (strict; NON VERIFICATO
    whether a rig may move a mesh built in Lua code)."""
    for r in anim.rigs:
        w = f"ANIM rig {r.model!r}"
        m = mesh.get(r.model) if mesh else None
        need(m is not None, "no MESH model of this name", w)
        need(len(r.vbones) == len(m.verts),
             f"rig has {len(r.vbones)} vertices, model has {len(m.verts)} (src/bm/runtime.c:2161)", w)


def encode(anim, kernel_limits=False, keep_long_names=False):
    """Anim -> ANIM section body. Port of scripts/bmmesh.py:encode_anim (same
    layout: "<HHI", rig "<HHHH", bone "<hH6f", u8 per vertex padded with zeros
    to 4, clip "<HBBf", keys "<f" + "<7f" per bone), but every pose must be
    given (bmmesh fills missing ones with identity) and nothing is cut."""
    validate(anim, kernel_limits, keep_long_names)
    out = bytearray(struct.pack("<HHI", len(anim.rigs), 0, 0))
    for r in anim.rigs:
        out += name_bytes(r.model, "ANIM", allow_full=keep_long_names)
        out += struct.pack("<HHHH", len(r.bones), len(r.clips), len(r.vbones), 0)
        for b in r.bones:
            out += name_bytes(b.name, "ANIM", allow_full=keep_long_names)
            out += struct.pack("<hH6f", b.parent, 0, *b.head, *b.tail)
        out += bytes(r.vbones) + pad4(len(r.vbones))
        for c in r.clips:
            out += name_bytes(c.name, "ANIM", allow_full=keep_long_names)
            out += struct.pack("<HBBf", len(c.keys), c.mode, c.flags, c.length)
            for k in c.keys:
                out += struct.pack("<f", k.time)
                for p in k.pose:
                    out += struct.pack("<7f", *p.q, *p.t)
    return bytes(out)


def decode(body, kernel_limits=False):
    """ANIM section body -> Anim, checked. Port of scripts/bmmesh.py:decode_anim
    with the checks of rig_walk / bm_anim_check (src/bm/format.c:245-320):
    exact size, counts, parents, finite numbers, key times. Extra bm-forge
    checks: reserved fields 0 (head u16+u32, rig u16, bone u16; strict), unique
    names, tool limits unless kernel_limits. Zero rigs is valid (C has no lower
    bound, src/bm/format.c:306-320)."""
    r = Reader(body, "ANIM")
    n, z16, z32 = r.unpack("HHI", "ANIM head")
    need(z16 == 0 and z32 == 0, "reserved fields of the ANIM head are not 0", "ANIM")
    rigs = []
    for ri in range(n):
        raw = r.take(NAME_FIELD, f"rig {ri} name")
        need(raw[0] != 0, "empty rig model name (src/bm/format.c:247)", f"ANIM rig {ri}")
        name = read_name(raw, f"ANIM rig {ri}", "rig model name")
        w = f"ANIM rig {name!r}"
        nb, nc, nv, res = r.unpack("HHHH", "rig head")
        need(res == 0, "reserved u16 of the rig head is not 0", w)
        need(1 <= nb <= BONES_MAX, f"{nb} bones, 1 to {BONES_MAX}", w)
        need(nc <= CLIPS_MAX, f"{nc} clips, 0 to {CLIPS_MAX}", w)
        need(nv >= 1, "0 vertices", w)
        bones = []
        for bi in range(nb):
            bname = read_name(r.take(NAME_FIELD, "bone name"), f"{w} bone {bi}", "bone name")
            parent, z, *hv = r.unpack("hH6f", "bone")
            need(z == 0, "reserved u16 of a bone is not 0", f"{w} bone {bi}")
            bones.append(Bone(bname, parent, hv[:3], hv[3:]))
        vb = list(r.take(nv, "vertex bones"))
        r.take((-nv) % 4, "vertex bones padding")
        clips = []
        for ci in range(nc):
            cname = read_name(r.take(NAME_FIELD, "clip name"), f"{w} clip {ci}", "clip name")
            nk, mode, flags, length = r.unpack("HBBf", "clip head")
            cw = f"{w} clip {cname!r}"
            need(1 <= nk <= KEYS_MAX, f"{nk} keys, 1 to {KEYS_MAX}", cw)
            need(r.left() >= nk * (4 + nb * POSE_SIZE), "keys run past the end of the section", cw)
            keys = []
            for _ in range(nk):
                t = r.unpack("f", "key time")[0]
                vals = struct.unpack(f"<{nb * 7}f", r.take(nb * POSE_SIZE, "key poses"))
                keys.append(Key(t, [Pose(list(vals[k:k + 4]), list(vals[k + 4:k + 7]))
                                    for k in range(0, nb * 7, 7)]))
            clips.append(Clip(cname, length, keys, mode, flags))
        rigs.append(Rig(name, bones, vb, clips))
    need(r.left() == 0, f"{r.left()} bytes after the last rig (src/bm/format.c:319)", "ANIM")
    anim = Anim(rigs)
    validate(anim, kernel_limits, keep_long_names=True)
    return anim
