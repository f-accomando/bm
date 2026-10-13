"""MESH section (type 8): the 3D models of a cartridge.

Layout: src/bm/bm.h:36-60, limits bm.h:174-178; reader/checker bm_mesh_check
(src/bm/format.c:136-168). Reference: references/FORMATS.md "MESH section".

The data model keeps every stored bit so that decode -> encode gives the same
bytes: the whole u32 colour (bit 31 textured, bits 24..30 material, never
dropped), the u16 at face offset 6 (`extra`: light of a lit textured face),
the raw u16 texture corners (`uv8`: sheet pixels x 8, or the corner lights of a
lit untextured face) and the model flags (bit 0 = lit).
"""
import struct
from dataclasses import dataclass, field
from typing import List

from .binary import NAME_FIELD, Reader, finite_in, name_bytes, read_name
from .errors import ValidationError, need

TEXTURED = 0x80000000           # bm.h:48-51, scripts/bmmesh.py:25
MATERIAL_MASK = 0x7F000000      # bits 24..30, src/bm/r3d.h:74-90
LIT = 1                         # BM_MODEL_LIT, bm.h:238
MODELS_MAX = 256                # BM_MODELS_MAX, bm.h:177
KERNEL_VERTS = KERNEL_FACES = 65535     # bm.h:175-176 (M39, bm3d 5.0)
TOOL_VERTS, TOOL_FACES = 4096, 16384    # src/script/bm3d.lua:34 (bm Studio / bm Animator)
FACE_SIZE = 24                  # BM_MESH_FACE, bm.h:178
HEAD = 8                        # u16 models, u16 inset, u32 reserved (src/bm/format.c:133)
MODEL_HEAD = NAME_FIELD + 8     # src/bm/format.c:134
COORD_LIMIT = 1e6               # src/bm/format.c:157


def limits(kernel_limits=False):
    """(max vertices, max faces): the tools' 4096/16384 by default
    (src/script/bm3d.lua:34), the kernel's 65535 with kernel_limits=True
    (src/bm/bm.h:175-176)"""
    return (KERNEL_VERTS, KERNEL_FACES) if kernel_limits else (TOOL_VERTS, TOOL_FACES)


@dataclass
class Face:
    """One triangle, 24 bytes (bm.h:46-60). a, b, c are 0-based, clockwise seen
    from the visible side (bm.h:46-47). colour is the raw u32. uv8 are the six
    raw u16 (u0,v0,u1,v1,u2,v2) = sheet pixels x 8 (/8 on read,
    src/bm/format.c:209-210). extra is the u16 at offset 6 (reserved; the light
    of a textured face in a lit model, bm.h:58-60)."""
    a: int
    b: int
    c: int
    colour: int = 0xFFFFFF
    uv8: List[int] = field(default_factory=lambda: [0] * 6)
    extra: int = 0

    @property
    def textured(self):
        return bool(self.colour & TEXTURED)

    @property
    def material(self):
        """bits 24..30 of the colour (src/bm/r3d.h:74-90)"""
        return (self.colour & MATERIAL_MASK) >> 24

    def uv_px(self):
        """texture corners in sheet pixels (bm_model_face, src/bm/format.c:203-211)"""
        return tuple(t / 8 for t in self.uv8)

    def light(self, lit):
        """the 9 bytes of baked light (r,g,b per corner, 128 = unchanged) of a face of
        a lit model; port of bm_model_face_light (src/bm/format.c:213-233)"""
        if not lit:
            return None
        if self.textured:
            c = self.extra
            r, g, b = (c >> 11) * 256 // 31, (c >> 5 & 63) * 256 // 63, (c & 31) * 256 // 31
            return tuple(min(255, v) for v in (r, g, b)) * 3
        out = []
        for k in range(3):
            u, v = self.uv8[k * 2], self.uv8[k * 2 + 1]
            out += [u & 255, u >> 8 & 255, v & 255]
        return tuple(out)

    @classmethod
    def from_px(cls, a, b, c, colour, uv=None, extra=0):
        """corners in sheet pixels -> uv8 = round(t*8); bmmesh.encode clamps to
        0..65535 (scripts/bmmesh.py:64 encode), bm-forge refuses out-of-range."""
        uv8 = []
        for t in (uv or (0,) * 6):
            q = round(t * 8)
            need(0 <= q <= 65535, f"texture corner {t} out of 0..8191.875 sheet px", "face")
            uv8.append(q)
        need(len(uv8) == 6, "a face has 6 texture coordinates", "face")
        return cls(a, b, c, colour, uv8, extra)

    @classmethod
    def lit_colour(cls, a, b, c, colour, light9):
        """an untextured face of a lit model with corner lights (r,g,b) x 3, 128 = 1;
        port of scripts/bmmesh.py:58-62 encode"""
        need(not colour & TEXTURED, "lit_colour is for untextured faces", "face")
        L = [int(x) for x in light9]
        need(len(L) == 9 and all(0 <= x <= 255 for x in L), "9 light bytes 0..255", "face")
        q = []
        for k in range(3):
            q += [L[k * 3] | L[k * 3 + 1] << 8, L[k * 3 + 2]]
        return cls(a, b, c, colour, q, 0)

    @staticmethod
    def light565(light9):
        """one light for a textured face of a lit model, RGB 5-6-5, top = 2x;
        port of scripts/bmmesh.py:65-70 encode"""
        lr, lg, lb = (sum(light9[k * 3 + ch] for k in range(3)) / 3 for ch in range(3))
        return (min(31, round(lr / 256 * 31)) << 11 | min(63, round(lg / 256 * 63)) << 5 |
                min(31, round(lb / 256 * 31)))

    def to_dict(self):
        return {"a": self.a, "b": self.b, "c": self.c, "colour": self.colour, "uv8": list(self.uv8),
                "extra": self.extra}

    @classmethod
    def from_dict(cls, d):
        return cls(int(d["a"]), int(d["b"]), int(d["c"]), int(d.get("colour", 0xFFFFFF)),
                   [int(x) for x in d.get("uv8", [0] * 6)], int(d.get("extra", 0)))


@dataclass
class Model:
    """One model (bm.h:36-45): name (1..15 bytes UTF-8 on write), vertices
    (x, y, z) f32 with y up, faces, flags (u32; bit 0 = lit, every other bit kept)."""
    name: str
    verts: List[List[float]] = field(default_factory=list)
    faces: List[Face] = field(default_factory=list)
    flags: int = 0

    @property
    def lit(self):
        return bool(self.flags & LIT)

    def to_dict(self):
        return {"name": self.name, "flags": self.flags, "verts": [list(v) for v in self.verts],
                "faces": [f.to_dict() for f in self.faces]}

    @classmethod
    def from_dict(cls, d):
        flags = int(d.get("flags", 0))
        if d.get("lit"):
            flags |= LIT
        return cls(d["name"], [[float(x) for x in v] for v in d["verts"]],
                   [Face.from_dict(f) for f in d["faces"]], flags)


@dataclass
class Mesh:
    """A MESH section: inset (u16, 1/256 sheet px, bm_mesh_inset src/bm/format.c:192-195;
    bmmesh default 0.25 px = 64, scripts/bmmesh.py:30) and 1..256 models."""
    models: List[Model] = field(default_factory=list)
    inset: int = 64

    def get(self, name):
        for m in self.models:
            if m.name == name:
                return m
        return None

    def to_dict(self):
        return {"inset": self.inset, "models": [m.to_dict() for m in self.models]}

    @classmethod
    def from_dict(cls, d):
        return cls([Model.from_dict(m) for m in d["models"]], int(d.get("inset", 64)))


def validate_model(m, kernel_limits=False, keep_long_names=False, where=None):
    """the checks of bm_mesh_check on one model (src/bm/format.c:144-165) plus the
    bm-forge rules: tool limits by default, name 1..15 bytes, every field in range."""
    w = where or f"MESH model {m.name!r}"
    name_bytes(m.name, w, "model name", allow_full=keep_long_names)
    mv, mf = limits(kernel_limits)
    nv, nf = len(m.verts), len(m.faces)
    hint = "" if kernel_limits else " (tool limit, src/script/bm3d.lua:34; kernel_limits=True allows 65535)"
    need(1 <= nv <= mv, f"{nv} vertices, 1 to {mv}{hint}", w)
    need(1 <= nf <= mf, f"{nf} faces, 1 to {mf}{hint}", w)
    need(0 <= m.flags <= 0xFFFFFFFF, "flags out of u32", w)
    for i, v in enumerate(m.verts):
        need(len(v) == 3, f"vertex {i} has {len(v)} coordinates", w)
        need(all(finite_in(x, COORD_LIMIT) for x in v),
             f"vertex {i} {tuple(v)} not finite in (-1e6, 1e6) (src/bm/format.c:157)", w)
    for i, f in enumerate(m.faces):
        for k in (f.a, f.b, f.c):
            need(isinstance(k, int) and 0 <= k < nv, f"face {i}: vertex index {k} out of 0..{nv - 1}", w)
        need(0 <= f.colour <= 0xFFFFFFFF, f"face {i}: colour out of u32", w)
        need(0 <= f.extra <= 0xFFFF, f"face {i}: extra out of u16", w)
        need(len(f.uv8) == 6 and all(isinstance(t, int) and 0 <= t <= 0xFFFF for t in f.uv8),
             f"face {i}: uv8 must be 6 integers 0..65535", w)


def validate(mesh, kernel_limits=False, keep_long_names=False):
    """a whole MESH: 1..256 models (bm.h:177), names unique (bm.h:41; bm_mesh_check
    does not check it, bmmesh.encode does, scripts/bmmesh.py:35-40), inset u16."""
    need(1 <= len(mesh.models) <= MODELS_MAX, f"{len(mesh.models)} models, 1 to {MODELS_MAX}", "MESH")
    need(0 <= mesh.inset <= 0xFFFF, "inset out of u16", "MESH")
    seen = set()
    for m in mesh.models:
        validate_model(m, kernel_limits, keep_long_names)
        key = m.name.encode("utf-8")
        need(key not in seen, f"model name {m.name!r} twice", "MESH")
        seen.add(key)


def encode(mesh, kernel_limits=False, keep_long_names=False):
    """Mesh -> MESH section body. Port of scripts/bmmesh.py:encode (head
    "<HHI", per model name + "<HHI" + "<3f" x nv + "<4HI6H" x nf) with the
    stored bits kept (no clamping, no cutting: a bad value is a ValidationError)."""
    validate(mesh, kernel_limits, keep_long_names)
    out = bytearray(struct.pack("<HHI", len(mesh.models), mesh.inset, 0))
    for m in mesh.models:
        out += name_bytes(m.name, "MESH", "model name", allow_full=keep_long_names)
        out += struct.pack("<HHI", len(m.verts), len(m.faces), m.flags)
        for v in m.verts:
            out += struct.pack("<3f", *v)
        for f in m.faces:
            out += struct.pack("<4HI6H", f.a, f.b, f.c, f.extra, f.colour, *f.uv8)
    return bytes(out)


def decode(body, kernel_limits=False):
    """MESH section body -> Mesh, checked. Port of scripts/bmmesh.py:decode plus
    the checks of bm_mesh_check (src/bm/format.c:136-168): exact size, names not
    empty, counts, finite coordinates, indices. Extra bm-forge checks: unique
    names, reserved u32 of the head = 0 (strict, NON VERIFICATO that no tool
    writes there), tool limits unless kernel_limits."""
    r = Reader(body, "MESH")
    count, inset, reserved = r.unpack("HHI", "MESH head")
    need(1 <= count <= MODELS_MAX, f"{count} models, 1 to {MODELS_MAX}", "MESH")
    need(reserved == 0, f"reserved u32 of the MESH head is {reserved:#x}, not 0", "MESH")
    mv, mf = limits(kernel_limits)
    models = []
    for i in range(count):
        raw = r.take(NAME_FIELD, f"model {i} name")
        need(raw[0] != 0, "empty model name (src/bm/format.c:149)", f"MESH model {i}")
        name = read_name(raw, f"MESH model {i}", "model name")
        w = f"MESH model {name!r}"
        nv, nf, flags = r.unpack("HHI", "model head")
        need(1 <= nv <= mv, f"{nv} vertices, 1 to {mv}", w)
        need(1 <= nf <= mf, f"{nf} faces, 1 to {mf}", w)
        need(r.left() >= nv * 12 + nf * FACE_SIZE, "model runs past the end of the section", w)
        vs = struct.unpack_from(f"<{nv * 3}f", r.take(nv * 12, "vertices"))
        verts = [list(vs[k:k + 3]) for k in range(0, nv * 3, 3)]
        faces = []
        fb = r.take(nf * FACE_SIZE, "faces")
        for k in range(nf):
            a, b, c, extra, colour, *uv = struct.unpack_from("<4HI6H", fb, k * FACE_SIZE)
            faces.append(Face(a, b, c, colour, list(uv), extra))
        models.append(Model(name, verts, faces, flags))
    need(r.left() == 0, f"{r.left()} bytes after the last model (src/bm/format.c:167)", "MESH")
    mesh = Mesh(models, inset)
    validate(mesh, kernel_limits, keep_long_names=True)
    return mesh
