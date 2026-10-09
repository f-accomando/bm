#!/usr/bin/env python3
"""
bmres.py - resource files (docs/RISORSE.md): one resource out of a .bm
cartridge, in the same container with the "BMRES" magic and no code
(layout in src/bm/bm.h). Standard library only, like mkbm.py.

  bmres.py list FILE...
  bmres.py extract GAME.bm -o OUT.bmm --model NAME [--model NAME ...]
  bmres.py extract GAME.bm -o OUT.bmi [--sprite NAME ... | --rect X,Y,W,H --name N
                                       [--frames N --fps N]]   (none: the whole sheet)
  bmres.py extract GAME.bm -o OUT.bms [--sfx N|NAME ...] [--song N|NAME ...] [--sound N|NAME ...]
                                       (none: the whole sound bank)
  bmres.py extract GAME.bm -o OUT.bmt              the map, with the tiles it uses
  bmres.py extract GAME.bm -o OUT.bmc              the palette of the sheet
  bmres.py extract GAME.bm -o OUT.bmk              every resource of the game
  bmres.py add GAME.bm FILE... [-o OUT.bm]         integrates resource files (copies them in)
  bmres.py convert IN OUT [--tiles PNG] [--name N]
        .png -> .bmi   .bmi -> .png   .glb -> .bmm   .json -> .bms   .bms -> .json
        .bm (a sound pack) -> .bms   .hex/.gpl -> .bmc   .bmc -> .hex/.gpl
        .csv -> .bmt (--tiles: the sheet its numbers refer to)   .bmt -> .csv (+ the tiles .png)
  bmres.py info FILE [--item TYPE NAME] [--name ..] [--author ..] [--license ..]
        [--version ..] [--tags ..] [--desc ..]   writes INFO (of the file or of a part)

The kind of a new resource file comes from the extension of its name. The
options --name, --author, --license, --version, --tags and --desc also work
with extract and convert.
"""
import argparse
import hashlib
import json
import math
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bmaudio  # noqa: E402
import bmmesh  # noqa: E402
import mkbm  # noqa: E402

MAGIC_RES = b"BMRES\0\0\0"
MAGIC_CART = (b"BMCART\0\0", b"BM33CART")
SEC_LUA, SEC_SHEET, SEC_MAP, SEC_COVER, SEC_SHEET8, SEC_AUDIO = 1, 2, 3, 4, 5, 6
SEC_MESH, SEC_ANIM, SEC_INFO, SEC_SPRITES, SEC_LAYERS, SEC_FLAGS, SEC_BOXES = 8, 9, 10, 11, 12, 13, 14
SEC_NAMES = {SEC_LUA: "LUA", SEC_SHEET: "SHEET", SEC_MAP: "MAP", SEC_COVER: "COVER", SEC_SHEET8: "SHEET8",
             SEC_AUDIO: "AUDIO", SEC_MESH: "MESH", SEC_ANIM: "ANIM", SEC_INFO: "INFO", SEC_SPRITES: "SPRITES",
             SEC_LAYERS: "LAYERS", SEC_FLAGS: "FLAGS", SEC_BOXES: "BOXES"}
KNOWN = set(SEC_NAMES) | {7}

CART, MODEL, IMAGE, SOUND, MAP, PALETTE, KIT = range(7)
KIND_NAMES = {CART: "cartridge", MODEL: "models", IMAGE: "image", SOUND: "sounds", MAP: "map",
              PALETTE: "palette", KIT: "kit"}
EXTENSIONS = {".bmm": MODEL, ".bmi": IMAGE, ".bms": SOUND, ".bmt": MAP, ".bmc": PALETTE, ".bmk": KIT}
SHEETS = {SEC_SHEET, SEC_SHEET8}
ALLOWED = {
    MODEL: {SEC_INFO, SEC_MESH, SEC_ANIM} | SHEETS,
    IMAGE: {SEC_INFO, SEC_SPRITES, SEC_BOXES, SEC_FLAGS} | SHEETS,
    SOUND: {SEC_INFO, SEC_AUDIO},
    MAP: {SEC_INFO, SEC_MAP, SEC_LAYERS, SEC_FLAGS} | SHEETS,
    PALETTE: {SEC_INFO, SEC_SHEET8},
    KIT: {SEC_INFO, SEC_MESH, SEC_ANIM, SEC_SPRITES, SEC_BOXES, SEC_AUDIO, SEC_MAP, SEC_LAYERS, SEC_FLAGS} | SHEETS,
}

INFO_MAX = 16 * 1024
SPRITES_MAX, SPRITE_SIZE, FRAMES_MAX = 1024, 28, 16
BOXES_MAX, BOX_SIZE = 4096, 28
BOX_KINDS = {0: "hurt", 1: "hit", 2: "body"}
SHEET_MAX, CELL = 4096, 8
NAME_LEN = 16
TEXTURED = bmmesh.TEXTURED
INFO_TYPES = ("model", "sprite", "sound", "sfx", "song", "map", "palette")


class ResError(Exception):
    pass


def kind_of_path(path):
    k = EXTENSIONS.get(os.path.splitext(path)[1].lower())
    if k is None:
        raise ResError(f"{path}: the extension must be one of {', '.join(EXTENSIONS)}")
    return k


# ------------------------------------------------------------------ files

class File:
    """a .bm cartridge (kind CART) or a resource file: header fields and
    the sections in file order, as [type, body]"""

    def __init__(self, kind, name="", author="", sections=None, header=None):
        self.kind, self.name, self.author = kind, name, author
        self.sections = sections or []
        self.header = header            # a cartridge's 128 bytes (resolution, ...)

    def get(self, typ):
        for t, b in self.sections:
            if t == typ:
                return b
        return None

    def put(self, typ, body):
        """replaces the section (where it is), or adds it at the end"""
        for s in self.sections:
            if s[0] == typ:
                s[1] = body
                return
        self.sections.append([typ, body])

    def drop(self, typ):
        self.sections = [s for s in self.sections if s[0] != typ]

    def sheet_type(self):
        return SEC_SHEET8 if self.get(SEC_SHEET8) is not None else SEC_SHEET if self.get(SEC_SHEET) is not None else None


def _cstr(b):
    return b.split(b"\0")[0].decode("utf-8", "replace")


def _fixed(s, n):
    b = s.encode("utf-8")[:n - 1]
    while b:                                    # not a cut UTF-8 sequence at the end
        try:
            b.decode("utf-8")
            break
        except UnicodeDecodeError:
            b = b[:-1]
    return b.ljust(n, b"\0")


def load(data, where="file"):
    """bytes -> File; ResError if it is not a well-formed .bm or resource file"""
    if len(data) < 128:
        raise ResError(f"{where}: too short")
    magic = data[:8]
    if magic == MAGIC_RES:
        kind = struct.unpack_from("<H", data, 12)[0]
        if kind not in ALLOWED:
            raise ResError(f"{where}: unknown kind of resource file ({kind})")
    elif magic in MAGIC_CART:
        kind = CART
    else:
        raise ResError(f"{where}: not a .bm or a resource file")
    version, hsize = struct.unpack_from("<HH", data, 8)
    if version != 1 or hsize != 128:
        raise ResError(f"{where}: unsupported version")
    count = data[17]
    crc = struct.unpack_from("<I", data, 20)[0]
    if zlib.crc32(data[128:]) & 0xFFFFFFFF != crc:
        raise ResError(f"{where}: CRC mismatch")
    if 128 + 16 * count > len(data):
        raise ResError(f"{where}: truncated section table")
    sections = []
    for i in range(count):
        typ, off, size, _ = struct.unpack_from("<IIII", data, 128 + 16 * i)
        if off + size > len(data):
            raise ResError(f"{where}: section out of bounds")
        body = data[off:off + size]
        if typ == SEC_AUDIO and body[:4] != b"BMAU":     # MESH of the first bm Studio files
            typ = SEC_MESH
        elif typ == 7:
            typ = SEC_ANIM
        sections.append([typ, body])
    f = File(kind, _cstr(data[24:72]), _cstr(data[72:104]), sections, bytes(data[:128]) if kind == CART else None)
    check(f, where)
    return f


def check(f, where="file"):
    """the sections of a File: allowed for its kind, and readable"""
    types = [t for t, _ in f.sections]
    if f.kind != CART:
        if SEC_LUA in types:
            raise ResError(f"{where}: a resource file never holds code")
        for t in types:
            if t in KNOWN and t not in ALLOWED[f.kind]:
                raise ResError(f"{where}: a {KIND_NAMES[f.kind]} file has no {SEC_NAMES.get(t, t)} section")
        need = {MODEL: SEC_MESH, SOUND: SEC_AUDIO, MAP: SEC_MAP, PALETTE: SEC_SHEET8}.get(f.kind)
        if need and need not in types:
            raise ResError(f"{where}: a {KIND_NAMES[f.kind]} file needs a {SEC_NAMES[need]} section")
        if f.kind in (IMAGE, MAP) and not SHEETS & set(types):
            raise ResError(f"{where}: a {KIND_NAMES[f.kind]} file needs a sheet")
        if f.kind == KIT and not set(types) - {SEC_INFO}:
            raise ResError(f"{where}: an empty kit")
    for t in set(types):
        if types.count(t) > 1 and t in KNOWN:
            raise ResError(f"{where}: two {SEC_NAMES.get(t, t)} sections")
    if SEC_SHEET in types and SEC_SHEET8 in types:
        raise ResError(f"{where}: two sheets")
    try:
        sheet = sheet_get(f)
        if f.get(SEC_MESH) is not None:
            mesh_parse(f.get(SEC_MESH))
        if f.get(SEC_ANIM) is not None:
            anim_parse(f.get(SEC_ANIM))
        if f.get(SEC_AUDIO) is not None:
            bmaudio.unpack(f.get(SEC_AUDIO))
        if f.get(SEC_MAP) is not None:
            map_get(f)
            layers_get(f)
        elif f.get(SEC_LAYERS) is not None:
            raise ResError("map layers without a map")
        if f.get(SEC_FLAGS) is not None:
            flags_get(f)
        if f.get(SEC_INFO) is not None:
            info_parse(f.get(SEC_INFO))
        zones = []
        if f.get(SEC_SPRITES) is not None:
            zones = sprites_decode(f.get(SEC_SPRITES))
            for z in zones:
                if sheet and (z["x"] + z["w"] * z["frames"] > sheet[0] or z["y"] + z["h"] > sheet[1]):
                    raise ResError(f"zone {z['name']} is out of the sheet")
        if f.get(SEC_BOXES) is not None:
            frames = {z["name"]: z["frames"] for z in zones}
            for b in boxes_decode(f.get(SEC_BOXES)):
                if b["zone"] not in frames or b["frame"] > frames[b["zone"]]:
                    raise ResError(f"a box of zone {b['zone']!r} frame {b['frame']}, not in SPRITES")
    except ResError as e:
        raise ResError(f"{where}: {e}") from None
    except (struct.error, IndexError, ValueError, UnicodeDecodeError) as e:
        raise ResError(f"{where}: broken data ({e})") from None
    if f.kind == PALETTE:
        w, h = struct.unpack_from("<HH", f.get(SEC_SHEET8), 0)
        if h != 1:
            raise ResError(f"{where}: a palette is a sheet of N x 1 pixels")


def dump(f):
    """File -> bytes"""
    check(f)
    table, bodies = b"", b""
    offset = 128 + 16 * len(f.sections)
    for typ, body in f.sections:
        table += struct.pack("<IIII", typ, offset + len(bodies), len(body), 0)
        bodies += body + b"\0" * ((-len(body)) % 4)
    after = table + bodies
    if f.kind == CART:
        header = bytearray(f.header)
        header[0:8] = b"BMCART\0\0"
    else:
        header = bytearray(128)
        header[0:8] = MAGIC_RES
        struct.pack_into("<HHH", header, 8, 1, 128, f.kind)
    header[17] = len(f.sections)
    struct.pack_into("<I", header, 20, zlib.crc32(after) & 0xFFFFFFFF)
    header[24:72] = _fixed(f.name, 48)
    header[72:104] = _fixed(f.author, 32)
    return bytes(header) + after


def read(path):
    return load(open(path, "rb").read(), path)


def write(path, f):
    data = dump(f)
    if path.lower().endswith(".bme") and data[:8] == b"BMCART\0\0":
        data = bytearray(data)              # a project: bit 0 of the u16 at 18 (src/bm/project.h)
        struct.pack_into("<H", data, 18, struct.unpack_from("<H", data, 18)[0] | 1)
        data = bytes(data)
    open(path, "wb").write(data)
    return data


# ------------------------------------------------------------------ INFO

def info_parse(body):
    """INFO text -> {"file": [(key, value)], "items": [[type, name, [(key, value)]]]}"""
    if len(body) > INFO_MAX:
        raise ResError(f"INFO over {INFO_MAX} bytes")
    text = body.decode("utf-8")
    info = {"file": [], "items": []}
    cur = info["file"]
    for line in text.split("\n"):
        line = line.rstrip("\r")
        s = line.strip()
        if not s:
            continue
        if s.startswith("[") and s.endswith("]"):
            typ, _, name = s[1:-1].strip().partition(" ")
            item = [typ, name.strip(), []]
            info["items"].append(item)
            cur = item[2]
            continue
        key, sep, value = s.partition(":")
        if sep:
            cur.append((key.strip(), value.strip()))
    return info


def info_dump(info):
    lines = [f"{k}: {v}" for k, v in info["file"]]
    for typ, name, kv in info["items"]:
        if lines:
            lines.append("")
        lines.append(f"[{typ} {name}]")
        lines += [f"{k}: {v}" for k, v in kv]
    body = ("\n".join(lines) + "\n").encode("utf-8") if lines else b""
    if len(body) > INFO_MAX:
        raise ResError(f"INFO over {INFO_MAX} bytes")
    return body


def info_of(f):
    body = f.get(SEC_INFO)
    return info_parse(body) if body else {"file": [], "items": []}


def kv_set(kv, key, value):
    for i, (k, _) in enumerate(kv):
        if k == key:
            kv[i] = (key, value)
            return
    kv.append((key, value))


def kv_get(kv, key):
    for k, v in kv:
        if k == key:
            return v
    return None


def info_item(info, typ, name, create=False):
    for item in info["items"]:
        if item[0] == typ and item[1] == name:
            return item
    if create:
        item = [typ, name, []]
        info["items"].append(item)
        return item
    return None


def info_store(f, info):
    if info["file"] or info["items"]:
        f.put(SEC_INFO, info_dump(info))
    else:
        f.drop(SEC_INFO)


# ------------------------------------------------------------------ SPRITES

def sprites_decode(body):
    n = struct.unpack_from("<H", body, 0)[0]
    if not 1 <= n <= SPRITES_MAX or len(body) != 4 + SPRITE_SIZE * n:
        raise ResError("bad SPRITES section")
    zones, seen = [], set()
    for i in range(n):
        o = 4 + SPRITE_SIZE * i
        name = _cstr(body[o:o + NAME_LEN])
        x, y, w, h, frames, fps = struct.unpack_from("<4HBB", body, o + NAME_LEN)
        if not name or name in seen or not w or not h or not 1 <= frames <= FRAMES_MAX:
            raise ResError(f"bad zone {name!r} in SPRITES")
        seen.add(name)
        zones.append({"name": name, "x": x, "y": y, "w": w, "h": h, "frames": frames, "fps": fps})
    return zones


def sprites_encode(zones):
    if not 1 <= len(zones) <= SPRITES_MAX:
        raise ResError(f"1 to {SPRITES_MAX} zones, not {len(zones)}")
    out = bytearray(struct.pack("<HH", len(zones), 0))
    for z in zones:
        out += _fixed(z["name"], NAME_LEN) + struct.pack("<4HBBH", z["x"], z["y"], z["w"], z["h"],
                                                         z.get("frames", 1), z.get("fps", 0), 0)
    return bytes(out)


def boxes_decode(body):
    """BOXES -> [{zone, frame (0: every frame), kind (0 hurt, 1 hit, 2 body, 3.. the game's), x, y, w, h}]"""
    n = struct.unpack_from("<H", body, 0)[0]
    if not 1 <= n <= BOXES_MAX or len(body) != 4 + BOX_SIZE * n:
        raise ResError("bad BOXES section")
    out = []
    for i in range(n):
        o = 4 + BOX_SIZE * i
        zone = _cstr(body[o:o + NAME_LEN])
        frame, kind, x, y, w, h = struct.unpack_from("<BBhhHH", body, o + NAME_LEN)
        if not zone or frame > FRAMES_MAX or not w or not h:
            raise ResError(f"bad box of {zone!r} in BOXES")
        out.append({"zone": zone, "frame": frame, "kind": kind, "x": x, "y": y, "w": w, "h": h})
    return out


def boxes_encode(boxes):
    if not 1 <= len(boxes) <= BOXES_MAX:
        raise ResError(f"1 to {BOXES_MAX} boxes, not {len(boxes)}")
    out = bytearray(struct.pack("<HH", len(boxes), 0))
    for b in boxes:
        out += _fixed(b["zone"], NAME_LEN) + struct.pack("<BBhhHHH", b["frame"], b["kind"], b["x"], b["y"],
                                                         b["w"], b["h"], 0)
    return bytes(out)


def zone_rect(z):
    """the pixels of a zone with all its frames: x, y, w, h"""
    return z["x"], z["y"], z["w"] * z["frames"], z["h"]


# ------------------------------------------------------------------ sheets

CLEAR = b"\0\0\0\0"


def _norm(p):
    return bytes(p) if p[3] >= 128 else CLEAR


def sheet8_palette(body):
    ncol = struct.unpack_from("<H", body, 4)[0]
    return [bytes(body[8 + 4 * i:12 + 4 * i]) for i in range(ncol)]


def sheet_get(f):
    """the sheet of a File -> (w, h, rgba bytearray) or None"""
    s8 = f.get(SEC_SHEET8)
    if s8 is not None:
        w, h, ncol = struct.unpack_from("<HHH", s8, 0)
        if not w or not h or w > SHEET_MAX or h > SHEET_MAX or not 1 <= ncol <= 256 or 8 + 4 * ncol > len(s8):
            raise ResError("bad sheet size")
        pal = sheet8_palette(s8)
        out, q, n = bytearray(), 8 + 4 * ncol, w * h
        while len(out) < 4 * n:
            if q >= len(s8):
                raise ResError("bad sheet data")
            t = s8[q]
            q += 1
            run = t + 1 if t < 128 else t - 126
            idx = s8[q:q + run] if t < 128 else bytes([s8[q]]) * run
            if len(idx) < run or len(out) + 4 * run > 4 * n or (idx and max(idx) >= ncol):
                raise ResError("bad sheet data")
            for k in idx:
                out += pal[k]
            q += run if t < 128 else 1
        if q != len(s8):
            raise ResError("bad sheet data")
        return w, h, out
    s = f.get(SEC_SHEET)
    if s is not None:
        w, h = struct.unpack_from("<HH", s, 0)
        if not w or not h or w > SHEET_MAX or h > SHEET_MAX or len(s) != 4 + 4 * w * h:
            raise ResError("bad sheet size")
        return w, h, bytearray(s[4:])
    return None


def sheet8_encode(w, h, rgba, seed=()):
    """a SHEET8 body with the colours of `seed` first (in their order), then
    the others as they appear; None over 256 colours. The runs are those of
    mkbm.sheet8 (and bm Studio's core.js)."""
    pal, index, clear = [], {}, None
    for c in seed:
        c = bytes(c)
        if c[3] < 128:
            if clear is None:
                clear = len(pal)
                pal.append(c)
            continue
        if c not in index and len(pal) < 256:
            index[c] = len(pal)
            pal.append(c)
    idx = bytearray(w * h)
    for i in range(w * h):
        p = rgba[4 * i:4 * i + 4]
        if p[3] < 128:
            if clear is None:
                if len(pal) == 256:
                    return None
                clear = len(pal)
                pal.append(CLEAR)
            idx[i] = clear
            continue
        p = bytes(p)
        k = index.get(p)
        if k is None:
            if len(pal) == 256:
                return None
            k = index[p] = len(pal)
            pal.append(p)
        idx[i] = k
    out = bytearray(struct.pack("<HHHH", w, h, len(pal), 0) + b"".join(pal))
    i, n, lit = 0, len(idx), bytearray()

    def flush():
        while lit:
            chunk = lit[:128]
            out.append(len(chunk) - 1)
            out.extend(chunk)
            del lit[:128]
    while i < n:
        j = i
        while j < n and j - i < 129 and idx[j] == idx[i]:
            j += 1
        if j - i >= 3:
            flush()
            out.append(j - i + 126)
            out.append(idx[i])
            i = j
        else:
            lit.append(idx[i])
            i += 1
    flush()
    return bytes(out)


def sheet_set(f, w, h, rgba, seed=None):
    """the sheet of a File becomes this picture: SHEET8 (with the colours of
    the old SHEET8, or `seed`, first) when it has at most 256 colours, else
    SHEET; in the place of the old one"""
    if seed is None:
        old = f.get(SEC_SHEET8)
        seed = sheet8_palette(old) if old is not None else ()
    body = sheet8_encode(w, h, rgba, seed)
    if body is not None:
        sheet_put(f, SEC_SHEET8, body)
    else:
        sheet_put(f, SEC_SHEET, struct.pack("<HH", w, h) + bytes(rgba))


def sheet_put(f, typ, body):
    """this sheet section in the place of the old one (or at the end)"""
    pos = next((i for i, s in enumerate(f.sections) if s[0] in SHEETS), len(f.sections))
    f.sections = [s for s in f.sections if s[0] not in SHEETS]
    f.sections.insert(pos, [typ, body])


def blit(src, sw, sx, sy, dst, dw, dx, dy, w, h):
    for y in range(h):
        a = 4 * ((sy + y) * sw + sx)
        b = 4 * ((dy + y) * dw + dx)
        dst[b:b + 4 * w] = src[a:a + 4 * w]


def blank(w, h):
    return bytearray(4 * w * h)


def pad8(w, h, rgba):
    """a picture grown to multiples of 8 (transparent pixels on the right and below)"""
    W, H = -(-w // CELL) * CELL, -(-h // CELL) * CELL
    if (W, H) == (w, h):
        return w, h, bytearray(rgba)
    out = blank(W, H)
    blit(rgba, w, 0, 0, out, W, 0, 0, w, h)
    return W, H, out


# ------------------------------------------------------------------ MESH and ANIM, as raw records

def mesh_parse(body):
    """MESH body -> (inset u16, [(name, record bytes)])"""
    count, inset, _ = struct.unpack_from("<HHI", body, 0)
    if not 1 <= count <= bmmesh.MAX_MODELS:
        raise ResError("bad MESH section")
    off, out = 8, []
    for _ in range(count):
        nv, nf, _ = struct.unpack_from("<HHI", body, off + NAME_LEN)
        size = NAME_LEN + 8 + 12 * nv + 24 * nf
        if not nv or not nf or off + size > len(body):
            raise ResError("bad MESH section")
        rec = bytes(body[off:off + size])
        for i in range(nf):
            a, b, c = struct.unpack_from("<3H", rec, NAME_LEN + 8 + 12 * nv + 24 * i)
            if max(a, b, c) >= nv:
                raise ResError("bad MESH section")
        out.append((_cstr(rec[:NAME_LEN]), rec))
        off += size
    if off != len(body):
        raise ResError("bad MESH section")
    return inset, out


def mesh_build(inset, records):
    if not 1 <= len(records) <= bmmesh.MAX_MODELS:
        raise ResError(f"1 to {bmmesh.MAX_MODELS} models, not {len(records)}")
    return struct.pack("<HHI", len(records), inset, 0) + b"".join(rec for _, rec in records)


def rec_faces(rec):
    """the faces of a model record: [(offset of its uv in rec, colour, (u0, v0, u1, v1, u2, v2) in sheet pixels x 8)]"""
    nv, nf = struct.unpack_from("<HH", rec, NAME_LEN)
    base = NAME_LEN + 8 + 12 * nv
    out = []
    for i in range(nf):
        o = base + 24 * i
        colour = struct.unpack_from("<I", rec, o + 8)[0]
        out.append((o + 12, colour, struct.unpack_from("<6H", rec, o + 12)))
    return out


def rec_rename(rec, name):
    return _fixed(name, NAME_LEN) + rec[NAME_LEN:]


def rec_move_uv(rec, moves):
    """moves: {uv offset in rec: (dx, dy) in sheet pixels} -> the record with those corners moved"""
    out = bytearray(rec)
    for o, (dx, dy) in moves.items():
        uv = list(struct.unpack_from("<6H", out, o))
        for k in range(3):
            uv[2 * k] = max(0, min(65535, uv[2 * k] + dx * 8))
            uv[2 * k + 1] = max(0, min(65535, uv[2 * k + 1] + dy * 8))
        struct.pack_into("<6H", out, o, *uv)
    return bytes(out)


def anim_parse(body):
    """ANIM body -> [(model name, rig bytes)]"""
    n = struct.unpack_from("<H", body, 0)[0]
    off, out = 8, []
    for _ in range(n):
        start = off
        nb, nc, nv, _ = struct.unpack_from("<HHHH", body, off + NAME_LEN)
        if not 1 <= nb <= 64:
            raise ResError("bad ANIM section")
        off += NAME_LEN + 8 + 44 * nb + ((nv + 3) & ~3)
        for _ in range(nc):
            nk = struct.unpack_from("<H", body, off + NAME_LEN)[0]
            off += NAME_LEN + 8 + nk * (4 + 28 * nb)
        if off > len(body):
            raise ResError("bad ANIM section")
        out.append((_cstr(body[start:start + NAME_LEN]), bytes(body[start:off])))
    if off != len(body):
        raise ResError("bad ANIM section")
    return out


def anim_build(rigs):
    return struct.pack("<HHI", len(rigs), 0, 0) + b"".join(r for _, r in rigs)


# ------------------------------------------------------------------ MAP

def map_get(f):
    body = f.get(SEC_MAP)
    w, h = struct.unpack_from("<HH", body, 0)
    if not w or not h or len(body) != 4 + 2 * w * h:
        raise ResError("bad map size")
    return w, h, list(struct.unpack_from(f"<{w * h}H", body, 4))


def map_body(w, h, cells):
    return struct.pack(f"<HH{w * h}H", w, h, *cells)


LAYERS_MAX = 8


def layers_get(f):
    """the map's layers: [(name, cells)], layer 1 (MAP) first; "main"
    without LAYERS"""
    mw, mh, cells = map_get(f)
    body = f.get(SEC_LAYERS)
    if body is None:
        return [("main", cells)]
    w, h, n = struct.unpack_from("<HHH", body, 0)
    if (w, h) != (mw, mh) or not 2 <= n <= LAYERS_MAX or len(body) != 8 + n * NAME_LEN + (n - 1) * w * h * 2:
        raise ResError("bad map layers (LAYERS)")
    names = [_cstr(body[8 + i * NAME_LEN:8 + (i + 1) * NAME_LEN]) for i in range(n)]
    if not all(names) or len(set(names)) != n:
        raise ResError("map layers without a name, or two with the same")
    out, q = [(names[0], cells)], 8 + n * NAME_LEN
    for i in range(1, n):
        out.append((names[i], list(struct.unpack_from(f"<{w * h}H", body, q))))
        q += w * h * 2
    return out


def layers_body(w, h, layers):
    """the LAYERS section of [(name, cells)], or None for one layer "main\""""
    if len(layers) == 1 and layers[0][0] == "main":
        return None
    out = struct.pack("<HHHH", w, h, len(layers), 0) + b"".join(n.encode()[:NAME_LEN].ljust(NAME_LEN, b"\0") for n, _ in layers)
    return out + b"".join(struct.pack(f"<{w * h}H", *c) for _, c in layers[1:])


def map_put(f, w, h, layers):
    """MAP and LAYERS of a file from [(name, cells)]"""
    f.put(SEC_MAP, map_body(w, h, layers[0][1]))
    more = layers_body(w, h, layers)
    f.drop(SEC_LAYERS)
    if more:
        f.sections.append([SEC_LAYERS, more])


def flags_get(f):
    """FLAGS -> {(cx, cy): flags} by the cell's place"""
    body = f.get(SEC_FLAGS)
    if body is None:
        return {}
    per, rows = struct.unpack_from("<HH", body, 0)
    if not per or len(body) != 4 + per * rows:
        raise ResError("bad tile flags (FLAGS)")
    return {(i % per, i // per): v for i, v in enumerate(body[4:]) if v}


def flags_put(f, flags, sw):
    """FLAGS of {(cx, cy): flags} for a sheet sw pixels wide (none: away)"""
    f.drop(SEC_FLAGS)
    per = max(sw // CELL, 1)
    flags = {k: v for k, v in flags.items() if v and k[0] < per}
    if not flags:
        return
    rows = max(cy for _, cy in flags) + 1
    body = bytearray(per * rows)
    for (cx, cy), v in flags.items():
        body[cy * per + cx] = v
    f.sections.append([SEC_FLAGS, struct.pack("<HH", per, rows) + bytes(body)])


# ------------------------------------------------------------------ islands of the sheet

def uv_cells(uv, sw, sh):
    """the cells under the bounding box of a face's texture corners: (cx, cy, cw, ch)"""
    us = [t / 8 for t in uv[0::2]]
    vs = [t / 8 for t in uv[1::2]]
    x0, x1 = max(0, math.floor(min(us) / CELL)), max(math.floor(min(us) / CELL) + 1, math.ceil(max(us) / CELL))
    y0, y1 = max(0, math.floor(min(vs) / CELL)), max(math.floor(min(vs) / CELL) + 1, math.ceil(max(vs) / CELL))
    x1, y1 = min(x1, sw // CELL), min(y1, sh // CELL)
    if x1 <= x0 or y1 <= y0:
        return None
    return (x0, y0, x1 - x0, y1 - y0)


def px_cells(x, y, w, h):
    x0, y0 = x // CELL, y // CELL
    return (x0, y0, -(-(x + w) // CELL) - x0, -(-(y + h) // CELL) - y0)


def merge_rects(rects):
    """rectangles of cells -> islands: the overlapping ones joined into
    their bounding box, until none overlaps; in the order of their first
    rectangle"""
    isl = [list(r) for r in rects]
    changed = True
    while changed:
        changed = False
        out = []
        for r in isl:
            for o in out:
                if r[0] < o[0] + o[2] and o[0] < r[0] + r[2] and r[1] < o[1] + o[3] and o[1] < r[1] + r[3]:
                    x0, y0 = min(r[0], o[0]), min(r[1], o[1])
                    o[2], o[3] = max(r[0] + r[2], o[0] + o[2]) - x0, max(r[1] + r[3], o[1] + o[3]) - y0
                    o[0], o[1] = x0, y0
                    changed = True
                    break
            else:
                out.append(r)
        isl = out
    return [tuple(r) for r in isl]


def island_of(islands, rect):
    for i, r in enumerate(islands):
        if r[0] <= rect[0] and r[1] <= rect[1] and rect[0] + rect[2] <= r[0] + r[2] and rect[1] + rect[3] <= r[1] + r[3]:
            return i
    raise ResError("internal: a rectangle outside the islands")


def shelf_pack(islands, first_cell=0):
    """places islands (cx, cy, cw, ch) on a new sheet, tallest first, on
    shelves: -> (width in cells, height in cells, [(nx, ny) per island])"""
    if not islands:
        return 0, 0, []
    area = sum(r[2] * r[3] for r in islands)
    W = max(max(r[2] for r in islands), min(SHEET_MAX // CELL, max(16, math.ceil(math.sqrt(area)))))
    order = sorted(range(len(islands)), key=lambda i: (-islands[i][3], -islands[i][2], i))
    pos = [None] * len(islands)
    x, y, shelf = first_cell, 0, 0
    for i in order:
        cw, ch = islands[i][2], islands[i][3]
        if x + cw > W:
            x, y, shelf = 0, y + shelf, 0
        pos[i] = (x, y)
        x += cw
        shelf = max(shelf, ch)
    H = y + shelf
    if H * CELL > SHEET_MAX:
        raise ResError("the textures do not fit in a 4096 x 4096 sheet")
    return W, H, pos


def used_cells(f, sw, sh):
    """the cells of a sheet that faces, zones or the map use"""
    used = set()
    if f.get(SEC_MESH) is not None:
        for _, rec in mesh_parse(f.get(SEC_MESH))[1]:
            for _, colour, uv in rec_faces(rec):
                if colour & TEXTURED:
                    r = uv_cells(uv, sw, sh)
                    if r:
                        used |= {(r[0] + i, r[1] + j) for i in range(r[2]) for j in range(r[3])}
    if f.get(SEC_SPRITES) is not None:
        for z in sprites_decode(f.get(SEC_SPRITES)):
            r = px_cells(*zone_rect(z))
            used |= {(r[0] + i, r[1] + j) for i in range(r[2]) for j in range(r[3])}
    if f.get(SEC_MAP) is not None:
        per = sw // CELL
        for _, cells in layers_get(f):
            for n in cells:
                if n:
                    used.add((n % per, n // per))
    return used


class Sheet:
    """a cartridge's sheet while resources go in: the free cells (clear and
    not used by faces, zones or the map; never cell 0, the map's empty tile)"""

    def __init__(self, f):
        self.w, self.h, self.rgba = sheet_get(f)
        taken = used_cells(f, self.w, self.h)
        taken.add((0, 0))
        self.busy = []
        self._rows(0, taken)

    def _rows(self, cy0, taken=()):
        """the busy flags of the cell rows from cy0 down"""
        W, H = self.w // CELL, self.h // CELL
        alpha = self.rgba[3::4]
        for cy in range(cy0, H):
            row = []
            for cx in range(W):
                busy = (cx, cy) in taken
                for y in range(CELL):
                    if busy:
                        break
                    a = (cy * CELL + y) * self.w + cx * CELL
                    busy = max(alpha[a:a + CELL]) >= 128
                row.append(1 if busy else 0)
            self.busy.append(row)
        self._sums()

    def _sums(self):
        W = self.w // CELL
        self.sat = [[0] * (W + 1)]
        for row in self.busy:
            acc, line, up = 0, [0], self.sat[-1]
            for cx in range(W):
                acc += row[cx]
                line.append(up[cx + 1] + acc)
            self.sat.append(line)

    def _empty(self, cx, cy, cw, ch):
        s = self.sat
        return s[cy + ch][cx + cw] - s[cy][cx + cw] - s[cy + ch][cx] + s[cy][cx] == 0

    def place(self, cw, ch):
        """the first free cw x ch cells (row by row): -> (cx, cy); the sheet
        grows down (never wider: the numbers of the sprites stay) when needed"""
        W = self.w // CELL
        if cw > W:
            raise ResError(f"a piece {cw * CELL} pixels wide does not fit in a sheet {self.w} wide")
        while True:
            H = self.h // CELL
            for cy in range(H - ch + 1):
                for cx in range(W - cw + 1):
                    if self._empty(cx, cy, cw, ch):
                        for j in range(ch):
                            for i in range(cw):
                                self.busy[cy + j][cx + i] = 1
                        self._sums()
                        return cx, cy
            if self.h >= SHEET_MAX:
                raise ResError("the sheet is full (4096 pixels high)")
            h0, old = self.h // CELL, self.h
            self.h = min(SHEET_MAX, (h0 + ch) * CELL)
            self.rgba += blank(self.w, self.h - old)
            self._rows(h0)


# ------------------------------------------------------------------ extraction

def _models_info(src_info, out_info, typ, names):
    for n in names:
        it = info_item(src_info, typ, n)
        if it:
            out_info["items"].append([typ, n, list(it[2])])


def _sheet_islands(src, rects, first_cell=0):
    """copies the islands of these rectangles out of src's sheet onto a new
    packed sheet: -> (w, h, rgba, islands, new positions)"""
    sw, sh, rgba = sheet_get(src)
    islands = merge_rects(rects)
    W, H, pos = shelf_pack(islands, first_cell)
    out = blank(W * CELL, H * CELL)
    for (cx, cy, cw, ch), (nx, ny) in zip(islands, pos):
        blit(rgba, sw, cx * CELL, cy * CELL, out, W * CELL, nx * CELL, ny * CELL, cw * CELL, ch * CELL)
    return W * CELL, H * CELL, out, islands, pos


def extract_models(src, names):
    body = src.get(SEC_MESH)
    if body is None:
        raise ResError("the cartridge has no 3D models")
    inset, recs = mesh_parse(body)
    have = {n: r for n, r in recs}
    if not names:
        names = [n for n, _ in recs]
    for n in names:
        if n not in have:
            raise ResError(f"no model {n!r} (there are: {', '.join(have)})")
    out = File(MODEL)
    picked = [(n, have[n]) for n in dict.fromkeys(names)]
    sheet = sheet_get(src)
    rects = []
    if sheet:
        for _, rec in picked:
            for _, colour, uv in rec_faces(rec):
                if colour & TEXTURED:
                    r = uv_cells(uv, sheet[0], sheet[1])
                    if r:
                        rects.append(r)
    if rects:
        w, h, rgba, islands, pos = _sheet_islands(src, rects)
        moved = []
        for n, rec in picked:
            moves = {}
            for o, colour, uv in rec_faces(rec):
                if colour & TEXTURED:
                    r = uv_cells(uv, sheet[0], sheet[1])
                    if r:
                        i = island_of(islands, r)
                        moves[o] = ((pos[i][0] - islands[i][0]) * CELL, (pos[i][1] - islands[i][1]) * CELL)
            moved.append((n, rec_move_uv(rec, moves)))
        picked = moved
    out.sections.append([SEC_MESH, mesh_build(inset, picked)])
    if src.get(SEC_ANIM) is not None:
        rigs = [r for r in anim_parse(src.get(SEC_ANIM)) if r[0] in dict(picked)]
        if rigs:
            out.sections.append([SEC_ANIM, anim_build(rigs)])
    if rects:
        sheet_set(out, w, h, rgba, seed=())
    info = {"file": [], "items": []}
    _models_info(info_of(src), info, "model", [n for n, _ in picked])
    info_store(out, info)
    out.name = names[0] if len(picked) == 1 else src.name
    out.author = src.author
    return out


def extract_image(src, sprites=(), rect=None, name=None, frames=1, fps=0):
    sheet = sheet_get(src)
    if not sheet:
        raise ResError("the cartridge has no sprite sheet")
    sw, sh, rgba = sheet
    zones = sprites_decode(src.get(SEC_SPRITES)) if src.get(SEC_SPRITES) is not None else []
    out = File(IMAGE, author=src.author)
    info = {"file": [], "items": []}
    if not sprites and rect is None:            # the whole sheet, with its zones
        out.sections.append([src.sheet_type(), src.get(src.sheet_type())])
        if zones:
            out.sections.append([SEC_SPRITES, src.get(SEC_SPRITES)])
            if src.get(SEC_BOXES) is not None:
                out.sections.append([SEC_BOXES, src.get(SEC_BOXES)])
            _models_info(info_of(src), info, "sprite", [z["name"] for z in zones])
        out.name = src.name
        info_store(out, info)
        return out
    if rect is not None:
        x, y, w, h = rect
        if not name:
            raise ResError("--rect needs --name")
        pick = [{"name": name, "x": x, "y": y, "w": w // frames, "h": h, "frames": frames, "fps": fps}]
    else:
        have = {z["name"]: z for z in zones}
        for n in sprites:
            if n not in have:
                raise ResError(f"no sprite {n!r} in SPRITES" + (f" (there are: {', '.join(have)})" if have else ""))
        pick = [dict(have[n]) for n in dict.fromkeys(sprites)]
    for z in pick:
        x, y, w, h = zone_rect(z)
        if x + w > sw or y + h > sh or w < 1 or h < 1:
            raise ResError(f"{z['name']}: out of the sheet ({sw}x{sh})")
    rects = [px_cells(*zone_rect(z)) for z in pick]
    W, H, out_rgba, islands, pos = _sheet_islands(src, rects)
    for z, r in zip(pick, rects):
        i = island_of(islands, r)
        z["x"] += (pos[i][0] - islands[i][0]) * CELL
        z["y"] += (pos[i][1] - islands[i][1]) * CELL
    sheet_set(out, W, H, out_rgba, seed=())
    out.sections.append([SEC_SPRITES, sprites_encode(pick)])
    names = {z["name"] for z in pick}
    boxes = [b for b in boxes_decode(src.get(SEC_BOXES)) if b["zone"] in names] \
        if src.get(SEC_BOXES) is not None and rect is None else []
    if boxes:                                    # the hitboxes and hurtboxes go with their zones
        out.sections.append([SEC_BOXES, boxes_encode(boxes)])
    _models_info(info_of(src), info, "sprite", [z["name"] for z in pick])
    info_store(out, info)
    out.name = pick[0]["name"] if len(pick) == 1 else src.name
    return out


def _pick_index(items, keys, what):
    out = []
    for k in keys:
        if str(k).isdigit():
            i = int(k)
        else:
            i = next((j for j, x in enumerate(items) if x.get("name") == k), None)
        if i is None or not 0 <= i < len(items):
            raise ResError(f"no {what} {k!r}")
        out.append(i)
    return list(dict.fromkeys(out))


def _step_sound(st):
    """bmaudio step text -> the sound it plays, or None"""
    note, sound, _, _ = bmaudio.parse_step(st)
    return sound if 0 < note < 128 else None


def _step_remap(st, smap):
    note, sound, vol, fx = bmaudio.parse_step(st)
    if 0 < note < 128 and sound in smap:
        sound = smap[sound]
    return bmaudio.step_text(note, sound, vol, fx)


def extract_sounds(src, sfx=(), songs=(), sounds=()):
    body = src.get(SEC_AUDIO)
    if body is None:
        raise ResError("the cartridge has no sound bank")
    out = File(SOUND, name=src.name, author=src.author)
    info = {"file": [], "items": []}
    sinfo = info_of(src)
    if not (sfx or songs or sounds):
        out.sections.append([SEC_AUDIO, body])
        bank = bmaudio.unpack(body)
        for typ, lst in (("sound", bank["sounds"]), ("sfx", bank["sfx"]), ("song", bank["songs"])):
            _models_info(sinfo, info, typ, [x["name"] for x in lst])
        info_store(out, info)
        return out
    bank = bmaudio.unpack(body)
    xi = _pick_index(bank["sfx"], sfx, "sound effect")
    gi = _pick_index(bank["songs"], songs, "song")
    si = _pick_index(bank["sounds"], sounds, "sound")
    pats = []
    for g in gi:
        for p in bank["songs"][g]["order"]:
            if p not in pats and p < len(bank["patterns"]):
                pats.append(p)
    used = list(si)
    for x in xi:
        for st in bank["sfx"][x]["steps"]:
            s = _step_sound(st)
            if s is not None and s not in used and s < len(bank["sounds"]):
                used.append(s)
    for p in pats:
        for steps in bank["patterns"][p]["tracks"].values():
            for st in steps:
                s = _step_sound(st)
                if s is not None and s not in used and s < len(bank["sounds"]):
                    used.append(s)
    used.sort()
    smap = {s: i for i, s in enumerate(used)}
    pmap = {p: i for i, p in enumerate(sorted(pats))}
    new = {"sounds": [bank["sounds"][s] for s in used], "sfx": [], "patterns": [], "songs": []}
    for x in xi:
        e = dict(bank["sfx"][x])
        e["steps"] = [_step_remap(st, smap) for st in e["steps"]]
        new["sfx"].append(e)
    for p in sorted(pats):
        e = {"steps": bank["patterns"][p]["steps"],
             "tracks": {t: [_step_remap(st, smap) for st in v] for t, v in bank["patterns"][p]["tracks"].items()}}
        new["patterns"].append(e)
    for g in gi:
        e = dict(bank["songs"][g])
        e["order"] = [pmap[p] for p in e["order"] if p in pmap]
        if e["loop"] is not None:
            e["loop"] = min(e["loop"], len(e["order"]) - 1)
        new["songs"].append(e)
    out.sections.append([SEC_AUDIO, bmaudio.pack(new)])
    for typ, lst in (("sound", new["sounds"]), ("sfx", new["sfx"]), ("song", new["songs"])):
        _models_info(sinfo, info, typ, [x["name"] for x in lst])
    info_store(out, info)
    names = [x["name"] for x in new["sfx"] + new["songs"]]
    if len(names) == 1 and names[0]:
        out.name = names[0]
    return out


def extract_map(src):
    if src.get(SEC_MAP) is None:
        raise ResError("the cartridge has no map")
    sheet = sheet_get(src)
    if not sheet:
        raise ResError("the cartridge has a map but no sheet")
    sw, sh, rgba = sheet
    mw, mh, _ = map_get(src)
    layers = layers_get(src)
    flags = flags_get(src)
    per, ncells = sw // CELL, (sw // CELL) * (sh // CELL)
    tiles = sorted({n for _, cells in layers for n in cells if 0 < n < ncells})
    PER = 16                                     # the tiles' sheet: 128 pixels wide, from cell 1
    tmap = {n: i + 1 for i, n in enumerate(tiles)}
    rows = -(-(len(tiles) + 1) // PER)
    out_rgba = blank(PER * CELL, rows * CELL)
    for n, m in tmap.items():
        blit(rgba, sw, n % per * CELL, n // per * CELL, out_rgba, PER * CELL, m % PER * CELL, m // PER * CELL, CELL, CELL)
    out = File(MAP, name=src.name, author=src.author)
    map_put(out, mw, mh, [(name, [tmap.get(n, 0) for n in cells]) for name, cells in layers])
    sheet_set(out, PER * CELL, rows * CELL, out_rgba, seed=())
    flags_put(out, {(m % PER, m // PER): flags.get((n % per, n // per), 0) for n, m in tmap.items()}, PER * CELL)
    info = {"file": [], "items": []}
    _models_info(info_of(src), info, "map", ["map"])
    info_store(out, info)
    return out


def palette_colours(f):
    """the opaque colours of a sheet's palette (SHEET8), or of its pixels (SHEET, up to 256)"""
    s8 = f.get(SEC_SHEET8)
    if s8 is not None:
        return [c for c in sheet8_palette(s8) if c[3] >= 128]
    sheet = sheet_get(f)
    if not sheet:
        raise ResError("no sheet, no palette")
    seen = []
    rgba = sheet[2]
    for i in range(0, len(rgba), 4):
        p = bytes(rgba[i:i + 4])
        if p[3] >= 128 and p not in seen:
            seen.append(p)
            if len(seen) > 256:
                raise ResError("the sheet has more than 256 colours: no palette")
    return seen


def palette_file(colours, name="", author=""):
    colours = list(dict.fromkeys(bytes(c) for c in colours))
    if not 1 <= len(colours) <= 256:
        raise ResError("a palette has 1 to 256 colours")
    f = File(PALETTE, name=name, author=author)
    rgba = bytearray(b"".join(colours))
    f.sections.append([SEC_SHEET8, sheet8_encode(len(colours), 1, rgba, colours)])
    return f


def extract_palette(src):
    out = palette_file(palette_colours(src), src.name, src.author)
    info = {"file": [], "items": []}
    _models_info(info_of(src), info, "palette", ["palette"])
    info_store(out, info)
    return out


def extract_kit(src):
    out = File(KIT, name=src.name, author=src.author)
    for t, b in src.sections:
        if t in ALLOWED[KIT]:
            out.sections.append([t, b])
    if not [t for t, _ in out.sections if t != SEC_INFO]:
        raise ResError("the cartridge has no resources")
    return out


# ------------------------------------------------------------------ integration

def _unique(name, taken, limit=NAME_LEN - 1):
    if name not in taken:
        return name
    for k in range(2, 10000):
        tail = str(k)
        base = name.encode("utf-8")[:limit - len(tail)].decode("utf-8", "ignore")
        if base + tail not in taken:
            return base + tail
    raise ResError("no free name")


def _merge_audio(dst_body, src_body):
    """the sounds of src added to dst: identical sounds reused, the rest in
    new slots (as bm Sound's Import from...); -> (body, {kind: [(old name, new name)]})"""
    if dst_body is None:
        bank = bmaudio.unpack(src_body)
        return src_body, {"sound": bank["sounds"], "sfx": bank["sfx"], "song": bank["songs"]}
    dst, src = bmaudio.unpack(dst_body), bmaudio.unpack(src_body)
    n0 = len(dst["sounds"])
    smap = {}
    for i, s in enumerate(src["sounds"]):
        j = next((k for k, d in enumerate(dst["sounds"]) if d == s), None)
        if j is None:
            if len(dst["sounds"]) >= bmaudio.LIMITS["sounds"]:
                raise ResError("no free sound slots")
            j = len(dst["sounds"])
            dst["sounds"].append(s)
        smap[i] = j
    pmap = {}
    for i, p in enumerate(src["patterns"]):
        if len(dst["patterns"]) >= bmaudio.LIMITS["patterns"]:
            raise ResError("no free pattern slots")
        pmap[i] = len(dst["patterns"])
        dst["patterns"].append({"steps": p["steps"],
                                "tracks": {t: [_step_remap(st, smap) for st in v] for t, v in p["tracks"].items()}})
    new_sfx, new_songs = [], []
    for x in src["sfx"]:
        if len(dst["sfx"]) >= bmaudio.LIMITS["sfx"]:
            raise ResError("no free sound effect slots")
        e = dict(x)
        e["steps"] = [_step_remap(st, smap) for st in x["steps"]]
        dst["sfx"].append(e)
        new_sfx.append(e)
    for g in src["songs"]:
        if len(dst["songs"]) >= bmaudio.LIMITS["songs"]:
            raise ResError("no free song slots")
        e = dict(g)
        e["order"] = [pmap[p] for p in g["order"] if p in pmap]
        dst["songs"].append(e)
        new_songs.append(e)
    added_sounds = dst["sounds"][n0:]
    return bmaudio.pack(dst), {"sound": added_sounds, "sfx": new_sfx, "song": new_songs}


def integrate(cart, res, origin=None):
    """copies the resource file `res` into the cartridge `cart` (a File,
    changed in place): -> a list of what went in, as "type name" strings"""
    if cart.kind != CART:
        raise ResError("resources go into a .bm cartridge")
    if res.kind == CART:
        raise ResError("that is a cartridge, not a resource file")
    added = []
    rinfo, cinfo = info_of(res), info_of(cart)
    file_kv = [(k, v) for k, v in rinfo["file"] if k in ("author", "license", "tags", "desc", "version")]
    if not kv_get(file_kv, "author") and res.author:
        file_kv.insert(0, ("author", res.author))

    def note(typ, old, new):
        added.append(f"{typ} {new}")
        it = info_item(rinfo, typ, old)
        kv = list(it[2]) if it else list(file_kv)
        if origin:
            kv_set(kv, "origin", origin)
        if kv:
            item = info_item(cinfo, typ, new, create=True)
            for k, v in kv:
                kv_set(item[2], k, v)

    if res.kind == PALETTE:
        # its colours first in the palette of the sheet (bm Pixel's palette)
        colours = [c for c in sheet8_palette(res.get(SEC_SHEET8)) if c[3] >= 128]
        old = cart.get(SEC_SHEET8)
        seed = list(dict.fromkeys(colours + (sheet8_palette(old) if old is not None else [])))
        sheet = sheet_get(cart) or (CELL, CELL, blank(CELL, CELL))
        body = sheet8_encode(sheet[0], sheet[1], sheet[2], seed)
        if body is None:
            raise ResError("the sheet and the palette have more than 256 colours together")
        sheet_put(cart, SEC_SHEET8, body)
        note("palette", "palette", res.name or "palette")
        info_store(cart, cinfo)
        return added

    # the res sheet: islands (what faces, zones and tiles use together) placed in the cart's sheet
    rsheet = sheet_get(res)
    rects, offs = [], {}
    rmesh = mesh_parse(res.get(SEC_MESH)) if res.get(SEC_MESH) is not None else None
    rzones = sprites_decode(res.get(SEC_SPRITES)) if res.get(SEC_SPRITES) is not None else []
    rmap = map_get(res) if res.get(SEC_MAP) is not None else None
    rlayers = layers_get(res) if rmap else []
    rflags = flags_get(res)
    if rmap and cart.get(SEC_MAP) is not None:
        raise ResError("the cartridge has a map already")
    if rsheet:
        rw, rh = rsheet[0], rsheet[1]
        if rmesh:
            for _, rec in rmesh[1]:
                for _, colour, uv in rec_faces(rec):
                    if colour & TEXTURED:
                        r = uv_cells(uv, rw, rh)
                        if r:
                            rects.append(r)
        rects += [px_cells(*zone_rect(z)) for z in rzones]
        if rmap:
            per = rw // CELL
            used = sorted({n for _, cells in rlayers for n in cells})
            rects += [(n % per, n // per, 1, 1) for n in used if 0 < n < per * (rh // CELL)]
    islands = merge_rects(rects)
    csheet = sheet_get(cart)
    if islands and not csheet:
        cart.put(res.sheet_type(), res.get(res.sheet_type()))      # the res sheet becomes the sheet
        offs = {i: (0, 0) for i in range(len(islands))}
        cw_cells = rsheet[0] // CELL
    elif islands:
        sh = Sheet(cart)
        for i, (cx, cy, w, h) in enumerate(islands):
            nx, ny = sh.place(w, h)
            blit(rsheet[2], rsheet[0], cx * CELL, cy * CELL, sh.rgba, sh.w, nx * CELL, ny * CELL, w * CELL, h * CELL)
            offs[i] = ((nx - cx) * CELL, (ny - cy) * CELL)
        sheet_set(cart, sh.w, sh.h, sh.rgba)
        cw_cells = sh.w // CELL
    else:
        cw_cells = csheet[0] // CELL if csheet else 0

    def off_of(rect):
        return offs[island_of(islands, rect)]

    if rmesh:
        cmesh = mesh_parse(cart.get(SEC_MESH)) if cart.get(SEC_MESH) is not None else (rmesh[0], [])
        taken = {n for n, _ in cmesh[1]}
        renames, recs = {}, list(cmesh[1])
        for n, rec in rmesh[1]:
            new = _unique(n, taken)
            taken.add(new)
            renames[n] = new
            moves = {}
            if rsheet:
                for o, colour, uv in rec_faces(rec):
                    if colour & TEXTURED:
                        r = uv_cells(uv, rsheet[0], rsheet[1])
                        if r:
                            moves[o] = off_of(r)
            recs.append((new, rec_move_uv(rec_rename(rec, new) if new != n else rec, moves)))
            note("model", n, new)
        cart.put(SEC_MESH, mesh_build(cmesh[0], recs))
        if res.get(SEC_ANIM) is not None:
            rigs = anim_parse(cart.get(SEC_ANIM)) if cart.get(SEC_ANIM) is not None else []
            for n, rig in anim_parse(res.get(SEC_ANIM)):
                if n in renames:
                    rigs.append((renames[n], rec_rename(rig, renames[n])))
            cart.put(SEC_ANIM, anim_build(rigs))
    if rzones:
        czones = sprites_decode(cart.get(SEC_SPRITES)) if cart.get(SEC_SPRITES) is not None else []
        taken = {z["name"] for z in czones}
        znames = {}
        for z in rzones:
            z = dict(z)
            dx, dy = off_of(px_cells(*zone_rect(z)))
            old = z["name"]
            z["name"] = _unique(old, taken)
            znames[old] = z["name"]
            taken.add(z["name"])
            z["x"] += dx
            z["y"] += dy
            czones.append(z)
            note("sprite", old, z["name"])
        cart.put(SEC_SPRITES, sprites_encode(czones))
        if res.get(SEC_BOXES) is not None:      # their boxes, by the zones' new names
            cboxes = boxes_decode(cart.get(SEC_BOXES)) if cart.get(SEC_BOXES) is not None else []
            for b in boxes_decode(res.get(SEC_BOXES)):
                if b["zone"] in znames:
                    cboxes.append(dict(b, zone=znames[b["zone"]]))
            cart.put(SEC_BOXES, boxes_encode(cboxes))
    if rmap:
        mw, mh, _ = rmap
        per = rsheet[0] // CELL
        layers = []
        for name, cells in rlayers:
            out = []
            for n in cells:
                if 0 < n < per * (rsheet[1] // CELL):
                    dx, dy = off_of((n % per, n // per, 1, 1))
                    out.append((n // per + dy // CELL) * cw_cells + n % per + dx // CELL)
                else:
                    out.append(0)
            layers.append((name, out))
        map_put(cart, mw, mh, layers)
        note("map", "map", "map")
    if rflags and islands:                       # the flags go with their cells
        cflags = flags_get(cart)
        for (cx, cy), v in rflags.items():
            for i, r in enumerate(islands):
                if r[0] <= cx < r[0] + r[2] and r[1] <= cy < r[1] + r[3]:
                    dx, dy = offs[i]
                    cflags[(cx + dx // CELL, cy + dy // CELL)] = v
                    break
        flags_put(cart, cflags, cw_cells * CELL)
    if res.get(SEC_AUDIO) is not None:
        body, new = _merge_audio(cart.get(SEC_AUDIO), res.get(SEC_AUDIO))
        cart.put(SEC_AUDIO, body)
        for typ in ("sound", "sfx", "song"):
            for x in new[typ]:
                note(typ, x["name"], x["name"])
    info_store(cart, cinfo)
    return added


# ------------------------------------------------------------------ conversions

def png_write(path, w, h, rgba):
    raw = b"".join(b"\0" + bytes(rgba[4 * w * y:4 * w * (y + 1)]) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def read_hex(path):
    out = []
    for line in open(path, encoding="utf-8"):
        s = line.strip().lstrip("#")
        if len(s) == 6:
            out.append(bytes.fromhex(s) + b"\xff")
    return out


def read_gpl(path):
    out = []
    for line in open(path, encoding="utf-8"):
        p = line.split()
        if len(p) >= 3 and all(t.isdigit() for t in p[:3]):
            out.append(bytes(int(t) & 255 for t in p[:3]) + b"\xff")
    return out


def convert(src, dst, tiles=None, name=None):
    """-> the File written (or None for the PC formats)"""
    a, b = os.path.splitext(src)[1].lower(), os.path.splitext(dst)[1].lower()
    stem = os.path.splitext(os.path.basename(src))[0]
    if a == ".png" and b == ".bmi":
        w, h, rgba = pad8(*mkbm.read_png(src))
        f = File(IMAGE, name=name or stem)
        sheet_set(f, w, h, rgba, seed=())
        return f
    if a == ".bmi" and b == ".png":
        w, h, rgba = sheet_get(read(src))
        png_write(dst, w, h, rgba)
        return None
    if a == ".glb" and b == ".bmm":
        data = open(src, "rb").read()
        models, inset = bmmesh.models_from_file(src)
        f = File(MODEL, name=name or stem)
        f.sections.append([SEC_MESH, bmmesh.encode(models, 0.25 if inset is None else inset)])
        png = bmmesh.glb_image(data)
        if png:
            w, h, rgba = pad8(*mkbm.read_png(src, png))
            sheet_set(f, w, h, rgba, seed=())
        return f
    if a == ".json" and b == ".bms":
        f = File(SOUND, name=name or stem)
        f.sections.append([SEC_AUDIO, bmaudio.pack(json.loads(open(src, encoding="utf-8").read()))])
        return f
    if a == ".bms" and b == ".json":
        open(dst, "w", encoding="utf-8").write(bmaudio.dumps(bmaudio.unpack(read(src).get(SEC_AUDIO))))
        return None
    if a == ".bm" and b == ".bms":
        c = read(src)
        return extract_sounds(c)
    if a in (".hex", ".gpl") and b == ".bmc":
        cols = read_hex(src) if a == ".hex" else read_gpl(src)
        return palette_file(list(dict.fromkeys(cols)), name or stem)
    if a == ".bmc" and b in (".hex", ".gpl"):
        f = read(src)
        cols = [c for c in sheet8_palette(f.get(SEC_SHEET8)) if c[3] >= 128]
        with open(dst, "w", encoding="utf-8") as o:
            if b == ".gpl":
                o.write(f"GIMP Palette\nName: {f.name or stem}\nColumns: 8\n#\n")
                o.writelines(f"{c[0]:3d} {c[1]:3d} {c[2]:3d}\t{c[:3].hex()}\n" for c in cols)
            else:
                o.writelines(c[:3].hex() + "\n" for c in cols)
        return None
    if a == ".csv" and b == ".bmt":
        if not tiles:
            raise ResError("a map from .csv needs --tiles SHEET.png (the sheet its numbers refer to)")
        mw, mh, cells = mkbm.read_map(src)
        tmp = File(CART, sections=[[SEC_LUA, b""], [SEC_MAP, struct.pack("<HH", mw, mh) + cells]],
                   header=bytes(128))
        tw, th, trgba = pad8(*mkbm.read_png(tiles))
        sheet_set(tmp, tw, th, trgba, seed=())
        f = extract_map(tmp)
        f.name = name or stem
        return f
    if a == ".bmt" and b == ".csv":
        f = read(src)
        mw, mh, _ = map_get(f)
        stem_dst = os.path.splitext(dst)[0]
        for i, (lname, cells) in enumerate(layers_get(f)):     # layer 1 in dst, the others beside it
            with open(dst if i == 0 else f"{stem_dst}_{lname}.csv", "w", encoding="utf-8") as o:
                for y in range(mh):
                    o.write(",".join(str(n) for n in cells[y * mw:(y + 1) * mw]) + "\n")
        w, h, rgba = sheet_get(f)
        png_write(stem_dst + ".png", w, h, rgba)
        flags = flags_get(f)
        if flags:                                # as mkbm.py --flags reads them
            with open(stem_dst + "_flags.csv", "w", encoding="utf-8") as o:
                o.write("# the flags of the tiles: cell=flags\n")
                o.write(" ".join(f"{cy * (w // CELL) + cx}={v}" for (cx, cy), v in sorted(flags.items(),
                                                                                         key=lambda e: e[0][::-1])))
                o.write("\n")
        return None
    raise ResError(f"no conversion from {a} to {b}")


# ------------------------------------------------------------------ list

def describe(f):
    lines = [f"{KIND_NAMES[f.kind]}: {f.name or '(no name)'}" + (f", by {f.author}" if f.author else "")]
    sheet = sheet_get(f)
    for t, b in f.sections:
        nm = SEC_NAMES.get(t, f"type {t}")
        if t == SEC_MESH:
            inset, recs = mesh_parse(b)
            rigs = {n for n, _ in anim_parse(f.get(SEC_ANIM))} if f.get(SEC_ANIM) is not None else set()
            lines.append(f"  MESH: {len(recs)} models")
            for n, rec in recs:
                nv, nf = struct.unpack_from("<HH", rec, NAME_LEN)
                tex = sum(1 for _, c, _ in rec_faces(rec) if c & TEXTURED)
                lines.append(f"    {n:16s} {nv} vertices, {nf} faces" + (f", {tex} textured" if tex else "")
                             + (", animated" if n in rigs else ""))
        elif t == SEC_ANIM:
            lines.append(f"  ANIM: {len(anim_parse(b))} skeletons")
        elif t in SHEETS:
            ncol = f"{struct.unpack_from('<H', b, 4)[0]} colours, " if t == SEC_SHEET8 else ""
            lines.append(f"  {nm}: {sheet[0]}x{sheet[1]}, {ncol}{len(b)} bytes")
        elif t == SEC_SPRITES:
            zones = sprites_decode(b)
            lines.append(f"  SPRITES: {len(zones)} zones")
            for z in zones:
                fr = f", {z['frames']} frames at {z['fps']} fps" if z["frames"] > 1 else ""
                lines.append(f"    {z['name']:16s} {z['x']},{z['y']} {z['w']}x{z['h']}{fr}")
        elif t == SEC_BOXES:
            boxes = boxes_decode(b)
            kinds = {}
            for x in boxes:
                k = BOX_KINDS.get(x["kind"], f"kind {x['kind']}")
                kinds[k] = kinds.get(k, 0) + 1
            lines.append(f"  BOXES: {len(boxes)} boxes (" + ", ".join(f"{n} {k}" for k, n in kinds.items()) + ")")
        elif t == SEC_AUDIO:
            bank = bmaudio.unpack(b)
            lines.append(f"  AUDIO: {len(bank['sounds'])} sounds, {len(bank['sfx'])} sound effects, "
                         f"{len(bank['patterns'])} patterns, {len(bank['songs'])} songs")
            for typ, lst in (("sfx", bank["sfx"]), ("song", bank["songs"])):
                if lst:
                    lines.append(f"    {typ}: " + ", ".join(f"{i} {x['name']}" for i, x in enumerate(lst)))
        elif t == SEC_MAP:
            mw, mh, _ = map_get(f)
            lines.append(f"  MAP: {mw}x{mh}")
        elif t == SEC_LAYERS:
            lines.append("  LAYERS: " + ", ".join(n for n, _ in layers_get(f)))
        elif t == SEC_FLAGS:
            fl = flags_get(f)
            lines.append(f"  FLAGS: {len(fl)} cells with flags")
        elif t == SEC_INFO:
            info = info_parse(b)
            lines.append(f"  INFO: {len(info['file'])} lines, {len(info['items'])} parts")
            for k, v in info["file"]:
                lines.append(f"    {k}: {v}")
            for typ, n, kv in info["items"]:
                lines.append(f"    [{typ} {n}] " + "; ".join(f"{k}: {v}" for k, v in kv))
        elif t == SEC_LUA:
            lines.append(f"  LUA: {len(b)} bytes")
        else:
            lines.append(f"  {nm}: {len(b)} bytes")
    return "\n".join(lines)


# ------------------------------------------------------------------ command line

INFO_KEYS = ("name", "author", "license", "version", "tags", "desc")


def apply_info(f, a, item=None):
    """the INFO options of the command line, onto the file (or a part of it)"""
    vals = [(k, getattr(a, k)) for k in INFO_KEYS if getattr(a, k, None) is not None]
    if not vals:
        return
    info = info_of(f)
    kv = info["file"] if item is None else info_item(info, item[0], item[1], create=True)[2]
    for k, v in vals:
        if item is None and k == "name":
            f.name = v
        if item is None and k == "author":
            f.author = v
        kv_set(kv, k, v)
    info_store(f, info)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def info_opts(p):
        for k in INFO_KEYS:
            p.add_argument("--" + k)
    p = sub.add_parser("list")
    p.add_argument("files", nargs="+")
    p = sub.add_parser("extract")
    p.add_argument("cart")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("--model", action="append", default=[])
    p.add_argument("--sprite", action="append", default=[])
    p.add_argument("--rect")
    p.add_argument("--frames", type=int, default=1)
    p.add_argument("--fps", type=int, default=0)
    p.add_argument("--sfx", action="append", default=[])
    p.add_argument("--song", action="append", default=[])
    p.add_argument("--sound", action="append", default=[])
    info_opts(p)
    p = sub.add_parser("add")
    p.add_argument("cart")
    p.add_argument("files", nargs="+")
    p.add_argument("-o", "--output")
    p = sub.add_parser("convert")
    p.add_argument("src")
    p.add_argument("dst")
    p.add_argument("--tiles")
    info_opts(p)
    p = sub.add_parser("info")
    p.add_argument("file")
    p.add_argument("--item", nargs=2, metavar=("TYPE", "NAME"))
    info_opts(p)
    a = ap.parse_args(argv)
    try:
        if a.cmd == "list":
            for path in a.files:
                print(describe(read(path)))
        elif a.cmd == "extract":
            src = read(a.cart)
            kind = kind_of_path(a.output)
            if kind == MODEL:
                out = extract_models(src, a.model)
            elif kind == IMAGE:
                rect = tuple(int(v) for v in a.rect.split(",")) if a.rect else None
                out = extract_image(src, a.sprite, rect, a.name, a.frames, a.fps)
            elif kind == SOUND:
                out = extract_sounds(src, a.sfx, a.song, a.sound)
            elif kind == MAP:
                out = extract_map(src)
            elif kind == PALETTE:
                out = extract_palette(src)
            else:
                out = extract_kit(src)
            if a.name is not None and kind == IMAGE and a.rect:
                a.name = None                       # the zone's name, not the file's
            apply_info(out, a)
            data = write(a.output, out)
            print(f"{a.output}: {len(data)} bytes, {KIND_NAMES[kind]} '{out.name}'")
        elif a.cmd == "add":
            cart = read(a.cart)
            for path in a.files:
                data = open(path, "rb").read()
                res = load(data, path)
                for what in integrate(cart, res, hashlib.sha256(data).hexdigest()):
                    print(f"{path}: {what}")
            out = a.output or a.cart
            if not a.output and os.path.splitext(a.cart)[1].lower() in (".bm", ".b16"):
                # a game is read only: the resources go into its editable copy
                out = os.path.splitext(a.cart)[0] + ".bme"
                print(f"{a.cart} is a game, read only: its editable copy is {out}")
            data = write(out, cart)
            print(f"{out}: {len(data)} bytes")
        elif a.cmd == "convert":
            f = convert(a.src, a.dst, a.tiles, a.name)
            if f is not None:
                kind_of_path(a.dst)
                apply_info(f, a)
                data = write(a.dst, f)
                print(f"{a.dst}: {len(data)} bytes, {KIND_NAMES[f.kind]} '{f.name}'")
            else:
                print(f"{a.dst}: written")
        elif a.cmd == "info":
            f = read(a.file)
            apply_info(f, a, tuple(a.item) if a.item else None)
            write(a.file, f)
            print(describe(f))
    except ResError as e:
        print(f"bmres: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
