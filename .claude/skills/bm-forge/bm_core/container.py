"""The container: 128-byte header, section table, CRC-32, for `.bm` cartridges,
`.bme` projects and `BMRES` resource files.

Spec src/bm/bm.h:1-19, parser parse() src/bm/format.c:525-758, Python
writers scripts/mkbm.py:pack, scripts/bmmesh.py:rewrite_cart, scripts/bmres.py:dump.
Reference: references/FORMATS.md "Container", "Compatibility block", "BMRES", ".bme".

Rules of bm-forge on top of the C parser (strict choices, see
references/OPEN_QUESTIONS.md): duplicate known sections are refused (C keeps
the last), a broken INFO/SPRITES/BOXES is refused also in a cartridge (C drops
it), a section must not overlap the header or the table, a rig must fit its
MESH model. The header `targets` (pi, rgb30) says which kernels a `.bm` is for.
"""
import os
import struct
import zlib
from dataclasses import dataclass, field
from typing import List, Optional

from . import anim as animmod
from . import mesh as meshmod
from . import sections as S
from .errors import ValidationError, need

HEADER_SIZE = 128                               # bm.h:139
MAGIC_CART = b"BMCART\0\0"                      # bm.h:4-5, written by every writer
MAGIC_OLD = b"BM33CART"                         # still read (src/bm/format.c:8-10)
MAGIC_RES = b"BMRES\0\0\0"                      # src/bm/format.c:411-414
FMT_RGB565 = 1                                  # bm.h:140
FLAG_PROJECT = 1                                # src/bm/project.h:15
FLAG_COMPAT = 16                                # flag bit 4: the compatibility block is present (docs/BM-FORMAT.md)
COMPAT_AT = 120                                 # compatibility block: bytes 120..127
TARGETS = {"pi": 1, "rgb30": 2}                 # byte 120, BM_TARGET_*
RESOLUTIONS = {(640, 360), (480, 270), (320, 180), (256, 256), (360, 360)}   # src/bm/format.c:552-555

# BMRES kinds, bm.h:167-168; extensions scripts/bmres.py:54
RES_MODEL, RES_IMAGE, RES_SOUND, RES_MAP, RES_PALETTE, RES_KIT = 1, 2, 3, 4, 5, 6
RES_EXT = {".bmm": RES_MODEL, ".bmi": RES_IMAGE, ".bms": RES_SOUND, ".bmt": RES_MAP, ".bmc": RES_PALETTE,
           ".bmk": RES_KIT}
_SHEETS = {S.SHEET, S.SHEET8}
RES_ALLOWED = {                                  # src/bm/format.c:450-462, scripts/bmres.py:56 ALLOWED
    RES_MODEL: {S.INFO, S.COVER, S.MESH, S.ANIM} | _SHEETS,          # COVER: a picture inside the file (2026-10-29)
    RES_IMAGE: {S.INFO, S.COVER, S.SPRITES, S.FLAGS, S.BOXES} | _SHEETS,
    RES_SOUND: {S.INFO, S.AUDIO},
    RES_MAP: {S.INFO, S.COVER, S.MAP, S.LAYERS, S.FLAGS} | _SHEETS,
    RES_PALETTE: {S.INFO, S.SHEET8},
    RES_KIT: {S.INFO, S.COVER, S.MESH, S.ANIM, S.SPRITES, S.AUDIO, S.MAP, S.LAYERS, S.FLAGS, S.BOXES} | _SHEETS,
}


def crc32(data):
    """zlib CRC-32 (src/lib/crc32.c:27,54-57; scripts/mkbm.py:crc32)"""
    return zlib.crc32(data) & 0xFFFFFFFF


def _cstr(raw):
    """display text of a char[N] header field; never refused (the raw header
    is kept, so no byte is lost). mkbm cuts the title at 47 bytes and may cut
    a UTF-8 character (scripts/mkbm.py:413): decoded with replacement."""
    return raw.split(b"\0", 1)[0].decode("utf-8", errors="replace")


def _fixed(text, n, what):
    """str -> char[n] with at least one NUL, never cutting (bmres cuts without
    breaking UTF-8, scripts/bmres.py:122-130 _fixed; bm-forge refuses)"""
    b = text.encode("utf-8")
    need(len(b) <= n - 1 and b"\0" not in b, f"{what} {text!r} is {len(b)} bytes, at most {n - 1}", "header")
    return b.ljust(n, b"\0")


@dataclass
class Header:
    """The 128 bytes (bm.h:1-19). For a BMRES file `kind` replaces width
    (offset 12) and height is 0. `raw` keeps the bytes read: a rewrite keeps
    every byte it does not change (like scripts/bmmesh.py:rewrite_cart)."""
    magic: bytes = MAGIC_CART
    width: int = 640
    height: int = 360
    kind: int = 0
    pixel_format: int = FMT_RGB565
    flags: int = 0
    title: str = ""
    author: str = ""
    built_from: str = ""
    version: int = 1
    header_size: int = HEADER_SIZE
    crc: int = 0
    raw: Optional[bytes] = None
    compat: Optional[bytes] = None      # 8 bytes for 120..127; None keeps the bytes of raw

    @property
    def is_res(self):
        return self.magic == MAGIC_RES

    @property
    def project(self):
        return bool(self.flags & FLAG_PROJECT)

    def to_dict(self):
        return {"magic": self.magic.decode("latin-1").rstrip("\0"), "width": self.width, "height": self.height,
                "kind": self.kind, "pixel_format": self.pixel_format, "flags": self.flags, "title": self.title,
                "author": self.author, "built_from": self.built_from}

    @classmethod
    def from_dict(cls, d):
        return cls(d.get("magic", "BMCART").encode("latin-1").ljust(8, b"\0"), int(d.get("width", 640)),
                   int(d.get("height", 360)), int(d.get("kind", 0)), int(d.get("pixel_format", 1)),
                   int(d.get("flags", 0)), d.get("title", ""), d.get("author", ""), d.get("built_from", ""))


@dataclass
class Section:
    """One table entry {u32 type, u32 offset, u32 size, u32 reserved} and its
    body (unpadded). `type` is the canonical one (legacy 6-as-MESH -> 8, 7 -> 9,
    scripts/bmmesh.py:cart_sections); `stored_type` is what the table said."""
    type: int
    body: bytes
    stored_type: Optional[int] = None
    reserved: int = 0

    def to_dict(self):
        return {"type": self.type, "name": S.sec_name(self.type), "size": len(self.body)}


@dataclass
class Cart:
    header: Header = field(default_factory=Header)
    sections: List[Section] = field(default_factory=list)

    def get(self, typ):
        for s in self.sections:
            if s.type == typ:
                return s.body
        return None

    def put(self, typ, body):
        """replace in place, or append at the end (bm_rewrite_with order,
        src/bm/format.c:891-938)"""
        for s in self.sections:
            if s.type == typ:
                s.body, s.stored_type = body, None
                return
        self.sections.append(Section(typ, body))

    def drop(self, typ):
        self.sections = [s for s in self.sections if s.type != typ]

    def mesh(self, kernel_limits=False):
        b = self.get(S.MESH)
        return meshmod.decode(b, kernel_limits) if b is not None else None

    def anim(self, kernel_limits=False):
        b = self.get(S.ANIM)
        return animmod.decode(b, kernel_limits) if b is not None else None

    def set_mesh(self, mesh, kernel_limits=False, keep_long_names=False):
        self.put(S.MESH, meshmod.encode(mesh, kernel_limits, keep_long_names))

    def set_anim(self, anim, kernel_limits=False, keep_long_names=False):
        """an Anim with 0 rigs drops the section (bm-forge rule; bm Mesh writes
        none too, carts/mesh/main.lua:468)"""
        if anim is None or not anim.rigs:
            self.drop(S.ANIM)
        else:
            self.put(S.ANIM, animmod.encode(anim, kernel_limits, keep_long_names))

    def to_dict(self):
        return {"header": self.header.to_dict(), "sections": [s.to_dict() for s in self.sections]}


# ------------------------------------------------------------------ reading

def parse(data, allow_res=True, kernel_limits=False, deep=True):
    """bytes -> Cart, checked. Port of parse() (src/bm/format.c:525-758) and
    scripts/bmmesh.py:cart_sections. deep=False skips the per-section and
    cross-section checks (only header, CRC, table)."""
    data = bytes(data)
    need(len(data) >= HEADER_SIZE, f"{len(data)} bytes, shorter than the {HEADER_SIZE}-byte header", "header")
    magic = data[:8]
    is_res = magic == MAGIC_RES
    need(magic in (MAGIC_CART, MAGIC_OLD) or (allow_res and is_res),
         f"bad magic {magic!r}: not a .bm" + (" or a resource file" if allow_res else " cartridge"), "header")
    version, hsize, w, h, pixfmt, count, flags, crc = struct.unpack_from("<HHHHBBHI", data, 8)
    need(version == 1 and hsize == HEADER_SIZE, f"unsupported version {version} / header size {hsize}", "header")
    actual = crc32(data[HEADER_SIZE:])
    need(actual == crc, f"CRC mismatch: header {crc:#010x}, data {actual:#010x}", "header")
    hd = Header(magic, w if not is_res else 0, h if not is_res else 0, w if is_res else 0, pixfmt, flags,
                _cstr(data[24:72]), _cstr(data[72:104]), _cstr(data[104:120]), version, hsize, crc,
                data[:HEADER_SIZE])
    if is_res:
        need(RES_MODEL <= w <= RES_KIT, f"unknown kind {w} of resource file", "header")
    else:
        need((w, h) in RESOLUTIONS, f"resolution {w}x{h}: 640x360, 480x270, 320x180, 256x256 or 360x360",
             "header")
        need(pixfmt == FMT_RGB565, f"pixel format {pixfmt}: only 1 (RGB565)", "header")
    table_end = HEADER_SIZE + count * 16
    need(table_end <= len(data), f"truncated section table ({count} entries)", "table")
    secs = []
    for i in range(count):
        typ, off, size, res = struct.unpack_from("<IIII", data, HEADER_SIZE + i * 16)
        need(off + size <= len(data), f"section {i} ({S.sec_name(typ)}) out of bounds: {off}+{size} > {len(data)}",
             "table")
        need(off >= table_end or size == 0, f"section {i} ({S.sec_name(typ)}) overlaps the header or the table",
             "table")
        body = data[off:off + size]
        canon = typ
        if typ == S.AUDIO and not S.is_audio(body):
            canon = S.MESH                       # legacy (src/bm/format.c:673-676)
        elif typ == S.OLD_ANIM:
            canon = S.ANIM
        secs.append(Section(canon, body, typ, res))
    cart = Cart(hd, secs)
    if deep:
        validate(cart, kernel_limits)
    return cart


def validate(cart, kernel_limits=False):
    """the checks of parse() after the table (src/bm/format.c:565-757) on a Cart"""
    hd = cart.header
    kind = hd.kind if hd.is_res else 0
    seen, info = set(), {}
    for i, s in enumerate(cart.sections):
        w = f"section {i} ({S.sec_name(s.type)})"
        if s.type in S.KNOWN:
            need(s.type not in seen, f"{S.sec_name(s.type)} twice (bm-forge refuses; C keeps the last)", w)
            seen.add(s.type)
        if kind and s.type in S.KNOWN:
            need(s.type != S.LUA, "a resource file never holds code (src/bm/format.c:572)", w)
            need(s.type in RES_ALLOWED[kind], "a section this kind of resource file does not have", w)
        if s.type == S.MESH:
            info[S.MESH] = meshmod.decode(s.body, kernel_limits)
        elif s.type == S.ANIM:
            info[S.ANIM] = animmod.decode(s.body, kernel_limits)
        elif s.type in S.SIMPLE:
            info[s.type] = S.SIMPLE[s.type](s.body, w)
    need(not (S.SHEET in info and S.SHEET8 in info), "two sheets, SHEET and SHEET8 (src/bm/format.c:698)")
    if S.LAYERS in info:
        lay, mp = info[S.LAYERS], info.get(S.MAP)
        need(mp and (lay["w"], lay["h"]) == (mp["w"], mp["h"]), "LAYERS do not fit the MAP (src/bm/format.c:700)")
    sheet = info.get(S.SHEET) or info.get(S.SHEET8)
    zones = {}
    if S.SPRITES in info:
        sw, sh = (sheet["w"], sheet["h"]) if sheet else (0, 0)
        for z in info[S.SPRITES]["zones"]:
            need(z["x"] + z["w"] * max(z["frames"], z.get("slices", 0)) <= sw and z["y"] + z["h"] <= sh,
                 f"sprite zone {z['name']!r} out of the sheet (src/bm/format.c:704-714)")
            zones[z["name"]] = z["frames"]
    if S.BOXES in info:
        for b in info[S.BOXES]["boxes"]:
            need(b["zone"] in zones and b["frame"] <= zones[b["zone"]],
                 f"box of zone {b['zone']!r} frame {b['frame']} the sheet does not have (src/bm/format.c:716-732)")
    if S.ANIM in info:
        animmod.validate_links(info[S.ANIM], info.get(S.MESH))
    need_one = {0: (S.LUA,), RES_MODEL: (S.MESH,), RES_IMAGE: (S.SHEET, S.SHEET8), RES_SOUND: (S.AUDIO,),
                RES_PALETTE: (S.SHEET8,), RES_KIT: (S.MESH, S.SHEET, S.SHEET8, S.AUDIO, S.MAP)}
    if kind == RES_MAP:
        need(S.MAP in info and sheet, "a map file without MAP and its tiles (src/bm/format.c:747-749)")
    else:
        need(any(t in info for t in need_one[kind]),
             "no Lua section" if not kind else f"resource kind {kind} without its sections (src/bm/format.c:733-756)")
    if kind == RES_PALETTE:
        need(info[S.SHEET8]["h"] == 1, "a palette is a SHEET8 of N x 1 pixels (src/bm/format.c:750-752)")
    return info


# ------------------------------------------------------------------ writing

def _header_bytes(hd, count, crc):
    """the 128 bytes: start from hd.raw (kept bytes) or zeros, then write each
    field whose value differs from what raw says (so an unchanged header stays
    byte-identical, as in scripts/bmmesh.py:rewrite_cart)"""
    out = bytearray(hd.raw if hd.raw else bytes(HEADER_SIZE))
    fresh = hd.raw is None
    need(len(out) == HEADER_SIZE, "raw header is not 128 bytes", "header")
    need(hd.magic in (MAGIC_CART, MAGIC_OLD, MAGIC_RES), f"bad magic {hd.magic!r}", "header")
    if fresh and hd.magic == MAGIC_OLD:
        hd.magic = MAGIC_CART                   # writers always write BMCART (src/bm/format.c:953)
    out[0:8] = hd.magic
    if hd.is_res:
        need(RES_MODEL <= hd.kind <= RES_KIT, f"unknown resource kind {hd.kind}", "header")
        struct.pack_into("<HHHHB", out, 8, 1, HEADER_SIZE, hd.kind, 0, 0)
    else:
        need((hd.width, hd.height) in RESOLUTIONS, f"resolution {hd.width}x{hd.height} not accepted", "header")
        need(hd.pixel_format == FMT_RGB565, "pixel format must be 1 (RGB565)", "header")
        struct.pack_into("<HHHHB", out, 8, 1, HEADER_SIZE, hd.width, hd.height, hd.pixel_format)
    need(0 <= count <= 255, f"{count} sections, at most 255", "table")
    out[17] = count
    need(0 <= hd.flags <= 0xFFFF, "flags out of u16", "header")
    struct.pack_into("<HI", out, 18, hd.flags, crc)
    if hd.compat is not None:
        need(len(hd.compat) == 8 and hd.compat[0] & 3, "compatibility block: 8 bytes, targets names pi, rgb30 or both",
             "header")
        out[COMPAT_AT:COMPAT_AT + 8] = hd.compat
        struct.pack_into("<H", out, 18, hd.flags | FLAG_COMPAT)
    for off, n, text, what in ((24, 48, hd.title, "title"), (72, 32, hd.author, "author"),
                               (104, 16, hd.built_from, "built-from name")):
        if fresh or _cstr(bytes(out[off:off + n])) != text:
            out[off:off + n] = _fixed(text, n, what)
    return bytes(out)


def build(cart, check=True, kernel_limits=False):
    """Cart -> bytes: table then bodies one after the other, each padded with
    zeros to 4 (src/bm/format.c:939-952, scripts/mkbm.py:400-407,
    scripts/bmmesh.py:rewrite_cart); CRC of everything after the header.
    Legacy types are written as 8/9 (bm.h:77-79). check=True validates first."""
    if check:
        validate(cart, kernel_limits)
    table, bodies = bytearray(), bytearray()
    start = HEADER_SIZE + 16 * len(cart.sections)
    for s in cart.sections:
        need(0 <= s.type <= 0xFFFFFFFF, "section type out of u32", "table")
        table += struct.pack("<IIII", s.type, start + len(bodies), len(s.body), 0)
        bodies += s.body + b"\0" * ((-len(s.body)) % 4)
    after = bytes(table + bodies)
    return _header_bytes(cart.header, len(cart.sections), crc32(after)) + after


def new_cart(lua, width=640, height=360, title="", author=""):
    """a fresh cartridge with a LUA section (mkbm.pack order: LUA first when no cover)"""
    return Cart(Header(MAGIC_CART, width, height, 0, FMT_RGB565, 0, title, author), [Section(S.LUA, lua)])


# ------------------------------------------------------------------ editions and files

def edition_of(path):
    """'bm' (game), 'bme' (project), 'res' (BMRES) by extension
    (src/bm/project.c:19-27)"""
    ext = os.path.splitext(path)[1].lower()
    if ext in (".bm", ".bme"):
        return ext[1:]
    if ext in RES_EXT:
        return "res"
    raise ValidationError(f"unknown extension {ext!r}: .bm, .bme or a resource file", path)


def edition_of_cart(cart):
    """'bm' for a cartridge, 'res' for BMRES"""
    return "res" if cart.header.is_res else "bm"


def compat_block(targets, input_=0, aspect=0, needs=0, min_ram=0, min_spec=0, min_width16=0):
    """the 8 bytes of the compatibility block (docs/BM-FORMAT.md, bytes 120..127).
    targets: 'pi', 'rgb30', 'pi,rgb30' (or an iterable of those names, or a bit set 1..3)."""
    if isinstance(targets, str):
        targets = [t.strip() for t in targets.split(",") if t.strip()]
    if not isinstance(targets, int):
        bits = 0
        for t in targets:
            need(t in TARGETS, f"unknown target {t!r}: pi or rgb30", "targets")
            bits |= TARGETS[t]
        targets = bits
    need(1 <= targets <= 3, "targets must name pi, rgb30 or both", "targets")
    need(0 <= min_ram <= 0xFFFF, "min_ram out of u16", "targets")
    return struct.pack("<BBBBHBB", targets, input_ & 255, aspect & 255, needs & 255, min_ram, min_spec & 255,
                       min_width16 & 255)


def check_path(path, cart):
    """the file name must match the bytes: .bm -> a cartridge resolution,
    .bme -> project bit set, .bm -> bit clear (src/bm/project.c:19-27),
    resource extension -> its kind (bm-forge strict:
    C lets the kind win, docs/RISORSE.md:133-134)"""
    ed = edition_of(path)
    hd = cart.header
    if ed == "res":
        need(hd.is_res, "a resource extension on a cartridge", path)
        want = RES_EXT[os.path.splitext(path)[1].lower()]
        need(hd.kind == want, f"extension says kind {want}, header says {hd.kind}", path)
        return
    need(not hd.is_res, "a BMRES file with a cartridge extension", path)
    if ed == "bm":
        need((hd.width, hd.height) in RESOLUTIONS, f"a .bm is not {hd.width}x{hd.height}", path)
    need(hd.project == (ed == "bme"), "project bit (u16 at 18, bit 0) must be set exactly for .bme", path)


def read_file(path, kernel_limits=False):
    """parse a file and check its name against its bytes"""
    with open(path, "rb") as f:
        cart = parse(f.read(), True, kernel_limits)
    check_path(path, cart)
    return cart


def write_file(path, cart, kernel_limits=False):
    """check the name against the bytes, build and write"""
    check_path(path, cart)
    data = build(cart, True, kernel_limits)
    with open(path, "wb") as f:
        f.write(data)
    return data
