#!/usr/bin/env python3
"""
mkbm.py - packs a native bm cartridge (.bm). Standard library only.

  mkbm.py -o game.bm --lua main.lua [--sheet sheet.png [--sheet8]]
           [--map [name=]map.csv ...] [--flags flags.csv] [--sprites zones.txt]
           [--audio bank.json|bank.bmau] [--models models.glb|pack.bm]
           [--title "My game"] [--author me]
           [--res 640x360|480x270|320x180|256x256|360x360]
           [--targets pi,rgb30 [--input pad,keyboard] [--aspect wide] [--needs 3d] [--min-ram MiB]
            [--min-spec N] [--min-width PX]]   the compatibility block (header bytes 120..127)

sheet.png: 8-bit RGB or RGBA PNG (non-interlaced); size multiple of 8 recommended.
--sheet8:  store the sheet with a palette and runs (at most 256 colours): big
           sheets of sprites become a fraction of the size.
map.csv:   one row of comma-separated sprite indices per line (0 = empty).
           Given again, each --map is one more layer of the map (up to 8,
           all of the first one's size; drawn in this order, the first at the
           back), named by "name=" before the file ("main" without).
--flags:   the 8 flags of the sheet's 8x8 cells (fget/fset): numbers 0-255
           separated by commas, spaces or lines, for cells 0, 1, 2... in
           order, and "n=flags" for cell n (then the next number is cell
           n+1); "#" starts a comment.
--sprites: named zones of the sheet (zspr, zone): one per line "name x y w h
           [frames [fps]]" in sheet pixels (the frames are the w x h boxes to
           the right of the first); "#" starts a comment. Under a zone, its
           hitboxes and hurtboxes (zboxes, the BOXES section): "hurt|hit|body
           frame x y w h" or "box kind frame x y w h" (kind 3-255, the
           game's), frame 1-16 or * (every frame), x and y from the frame's
           top-left corner (they may stick out).
--audio:   the sound bank (sounds, sound effects, music): JSON or binary,
           see scripts/bmaudio.py; sfx() and music() play it.
--models:  3D models for model(): a .glb (its texture is the sprite
           sheet, used as the sheet when there is no --sheet) or a .bm made
           with bm Studio / bm Animator on the console: its models, their
           skeletons and animations (ANIM), and its sheet when there is no
           --sheet.
Format: see src/bm/bm.h.
"""
import argparse
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bmaudio  # noqa: E402
import bmmesh  # noqa: E402

# 6 is the sound bank; MESH and ANIM were 6 and 7 in the first bm Studio
# files (bmmesh.cart_sections reads those as 8 and 9)
SEC_LUA, SEC_SHEET, SEC_MAP, SEC_COVER, SEC_SHEET8, SEC_AUDIO = 1, 2, 3, 4, 5, 6
SEC_MESH, SEC_ANIM = bmmesh.SEC_MESH, bmmesh.SEC_ANIM
SEC_SPRITES, SEC_LAYERS, SEC_FLAGS, SEC_BOXES = 11, 12, 13, 14


SEC_INFO = 10
INFO_MAX = 16 * 1024
RESOLUTIONS = ["640x360", "480x270", "320x180", "256x256", "360x360", "720x720"]
TYPES = {"game": 0, "app": 1, "utility": 2}   # bm.info "type:", bits 2-3 of the u16 at 18 (src/bm/project.h)


def read_info(path):
    """bm.info: (the INFO section text, UTF-8 <= 16 KiB; its top-level keys)."""
    text = "".join(ln for ln in open(path, encoding="utf-8").read().splitlines(True)
                   if not ln.lstrip().startswith("#"))       # comments stay in the file
    raw = text.encode("utf-8")
    if len(raw) > INFO_MAX:
        raise SystemExit(f"{path}: INFO over {INFO_MAX} bytes")
    keys = {}
    for line in text.split("\n"):
        s = line.strip()
        if s.startswith("["):
            break                       # the blocks of the parts start here
        k, sep, v = s.partition(":")
        if sep and not s.startswith("#"):
            keys[k.strip()] = v.strip()
    return raw, keys


def read_png(path, data=None):
    if data is None:
        data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG")
    pos, idat, w = 8, b"", None
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise SystemExit(f"{path}: need 8-bit RGB/RGBA, non-interlaced PNG")
            bpp = 3 if ctype == 2 else 4
        elif typ == b"IDAT":
            idat += body
    raw = zlib.decompress(idat)
    stride = w * bpp
    out, prev = bytearray(), bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        for x in range(w):
            px = line[x * bpp:(x + 1) * bpp]
            out += px if bpp == 4 else px + b"\xff"
        prev = line
    return w, h, bytes(out)


def read_map(path):
    rows = [[int(v) for v in line.replace(" ", "").split(",") if v != ""]
            for line in open(path) if line.strip() and not line.startswith("#")]
    w = max(len(r) for r in rows)
    cells = b"".join(struct.pack("<H", r[x] if x < len(r) else 0) for r in rows for x in range(w))
    return w, len(rows), cells


LAYERS_MAX, LAYER_NAME = 8, 16


def read_layers(specs):
    """--map arguments ("file.csv" or "name=file.csv") -> [(name, w, h,
    cells)]: the layers, all of the first one's size (a smaller CSV is
    padded with empty cells)"""
    out = []
    for spec in specs:
        name, path = spec.split("=", 1) if "=" in spec and not os.path.exists(spec) else ("main", spec)
        name = name or "main"
        if len(name.encode()) > LAYER_NAME:
            raise SystemExit(f"--map {spec}: a layer's name is at most {LAYER_NAME} bytes")
        if any(name == n for n, *_ in out):
            raise SystemExit(f"--map {spec}: two layers called {name!r}")
        w, h, cells = read_map(path)
        if out:
            w0, h0 = out[0][1], out[0][2]
            if w > w0 or h > h0:
                raise SystemExit(f"--map {spec}: {w}x{h}, bigger than the first layer ({w0}x{h0})")
            rows = [cells[y * w * 2:(y + 1) * w * 2].ljust(w0 * 2, b"\0") for y in range(h)]
            cells = b"".join(rows).ljust(w0 * h0 * 2, b"\0")
            w, h = w0, h0
        out.append((name, w, h, cells))
    if len(out) > LAYERS_MAX:
        raise SystemExit(f"--map: at most {LAYERS_MAX} layers")
    return out


def layers_section(layers):
    """the LAYERS section (src/bm/bm.h) of 2..8 layers, or None for one
    layer called "main" (the MAP section alone)"""
    if len(layers) == 1 and layers[0][0] == "main":
        return None
    w, h = layers[0][1], layers[0][2]
    out = struct.pack("<HHHH", w, h, len(layers), 0)
    out += b"".join(n.encode().ljust(LAYER_NAME, b"\0") for n, *_ in layers)
    return out + b"".join(c for *_, c in layers[1:])


def read_flags(path):
    """--flags file -> {cell: flags}"""
    flags, n = {}, 0
    for line in open(path):
        for tok in line.split("#", 1)[0].replace(",", " ").split():
            if "=" in tok:
                k, v = tok.split("=", 1)
                n = int(k, 0)
                tok = v
            v = int(tok, 0)
            if not 0 <= v <= 255 or n < 0:
                raise SystemExit(f"{path}: flags are 0-255 ({tok})")
            if v:
                flags[n] = v
            n += 1
    return flags


def flags_section(flags, sheet_w):
    """the FLAGS section for a sheet sheet_w pixels wide, or None"""
    if not flags:
        return None
    per = max(sheet_w // 8, 1)
    rows = max(flags) // per + 1
    body = bytearray(per * rows)
    for k, v in flags.items():
        body[k] = v
    return struct.pack("<HH", per, rows) + bytes(body)


SPRITES_MAX, ZONE_NAME = 1024, 16


BOX_KINDS = {"hurt": 0, "hit": 1, "body": 2}
BOXES_MAX = 4096


def read_sprite_file(path, sheet_w=None, sheet_h=None):
    """--sprites file -> (the SPRITES section, the BOXES section or None)"""
    zones, boxes = [], []
    for i, line in enumerate(open(path), 1):
        f = line.split("#", 1)[0].split()
        if not f:
            continue
        if f[0] in BOX_KINDS or f[0] == "box":
            if not zones:
                raise SystemExit(f"{path}:{i}: a box goes under its zone")
            if f[0] == "box":
                if len(f) != 7 or not f[1].isdigit() or not 3 <= int(f[1]) <= 255:
                    raise SystemExit(f"{path}:{i}: box kind frame x y w h (kind 3-255)")
                kind, f = int(f[1]), f[1:]
            else:
                kind = BOX_KINDS[f[0]]
            if len(f) != 6:
                raise SystemExit(f"{path}:{i}: {f[0]} frame x y w h (frame 1-16 or *)")
            zname, frames = zones[-1][0], zones[-1][5]
            fr = 0 if f[1] == "*" else int(f[1])
            x, y, w, h = map(int, f[2:6])
            if not 0 <= fr <= frames or w < 1 or h < 1 or not -32768 <= x <= 32767 or not -32768 <= y <= 32767:
                raise SystemExit(f"{path}:{i}: frame 1-{frames} or *, w and h >= 1")
            boxes.append((zname, fr, kind, x, y, w, h))
            continue
        slices, down = 0, False
        if f[0] in ("stack", "stackv"):         # stack name x y w h n: n slices of w x h side by side;
            down = f[0] == "stackv"             # stackv: one under the other (bit 0 of byte 27)
            if len(f) != 7 or not f[6].isdigit() or not 2 <= int(f[6]) <= 255:
                raise SystemExit(f"{path}:{i}: {f[0]} name x y w h n (n 2-255)")
            slices, f = int(f[6]), f[1:6]
        if len(f) < 5 or len(f) > 7:
            raise SystemExit(f"{path}:{i}: name x y w h [frames [fps]]")
        name, (x, y, w, h), frames, fps = f[0], map(int, f[1:5]), int(f[5]) if len(f) > 5 else 1, \
            int(f[6]) if len(f) > 6 else 0
        if len(name.encode()) > ZONE_NAME or any(z[0] == name for z in zones):
            raise SystemExit(f"{path}:{i}: {name!r}: names are unique, at most {ZONE_NAME} bytes")
        if w < 1 or h < 1 or not 1 <= frames <= 16 or not 0 <= fps <= 255 or x < 0 or y < 0:
            raise SystemExit(f"{path}:{i}: w, h >= 1, frames 1-16, fps 0-255")
        if sheet_w and (x + w * (frames if down else max(frames, slices)) > sheet_w
                        or y + h * (slices if down else 1) > sheet_h):
            raise SystemExit(f"{path}:{i}: {name!r} is out of the sheet ({sheet_w}x{sheet_h})")
        zones.append((name, x, y, w, h, frames, fps, slices | 0x100 if down else slices))
    if not 1 <= len(zones) <= SPRITES_MAX:
        raise SystemExit(f"{path}: 1 to {SPRITES_MAX} zones")
    if len(boxes) > BOXES_MAX:
        raise SystemExit(f"{path}: at most {BOXES_MAX} boxes")
    sprites = struct.pack("<HH", len(zones), 0) + b"".join(
        n.encode().ljust(ZONE_NAME, b"\0") + struct.pack("<HHHHBBH", x, y, w, h, fr, fps, sl)
        for n, x, y, w, h, fr, fps, sl in zones)
    return sprites, boxes_section(boxes)


def boxes_section(boxes):
    """[(zone, frame, kind, x, y, w, h)] -> the BOXES section (None if empty)"""
    if not boxes:
        return None
    return struct.pack("<HH", len(boxes), 0) + b"".join(
        z.encode().ljust(ZONE_NAME, b"\0") + struct.pack("<BBhhHHH", fr, k, x, y, w, h, 0)
        for z, fr, k, x, y, w, h in boxes)


def read_sprites(path, sheet_w=None, sheet_h=None):
    """--sprites file -> the SPRITES section"""
    return read_sprite_file(path, sheet_w, sheet_h)[0]


def crc32(b):
    return zlib.crc32(b) & 0xFFFFFFFF


COVER = 88                                  # the menu's card (src/kernel/menu_ui.h MENU_CARD)


def box_scale(w, rgba, cx, cy, cw, ch, dw, dh):
    """the box (cx, cy, cw, ch) of an RGBA picture w wide -> dw x dh RGBA,
    each pixel the average of what falls on it"""
    out = bytearray()
    for y in range(dh):
        sy0 = cy + y * ch // dh
        sy1 = max(cy + (y + 1) * ch // dh, sy0 + 1)
        for x in range(dw):
            sx0 = cx + x * cw // dw
            sx1 = max(cx + (x + 1) * cw // dw, sx0 + 1)
            acc, n = [0, 0, 0, 0], 0
            for sy in range(sy0, sy1):
                row = sy * w * 4
                for sx in range(sx0, sx1):
                    i = row + sx * 4
                    for k in range(4):
                        acc[k] += rgba[i + k]
                    n += 1
            out += bytes(v // n for v in acc)
    return out


def make_cover(img):
    """Any picture -> the COVER: 88x88 RGBA, square as the menu's cards
    (2026-10-04; 128x80 before). A (nearly) square picture is scaled; any
    other shape stays whole, as wide (or as high) as the card, over a
    blurred, darker copy of its middle that fills the rest (as the menu
    does with the covers of before)."""
    w, h, rgba = img
    if w * 10 >= h * 9 and h * 10 >= w * 9:
        return COVER, COVER, bytes(box_scale(w, rgba, 0, 0, w, h, COVER, COVER))
    c = min(w, h)
    lo = box_scale(w, rgba, (w - c) // 2, (h - c) // 2, c, c, 8, 8)
    out = bytearray(COVER * COVER * 4)
    for y in range(COVER):                  # the 8x8 blocks, stretched smooth, darker
        v = max((y + 0.5) * 8 / COVER - 0.5, 0)
        j0 = int(v)
        j1, fy = min(j0 + 1, 7), v - j0
        for x in range(COVER):
            u = max((x + 0.5) * 8 / COVER - 0.5, 0)
            i0 = int(u)
            i1, fx = min(i0 + 1, 7), u - i0
            o = (y * COVER + x) * 4
            for k in range(3):
                a, b = lo[(j0 * 8 + i0) * 4 + k], lo[(j0 * 8 + i1) * 4 + k]
                cc, d = lo[(j1 * 8 + i0) * 4 + k], lo[(j1 * 8 + i1) * 4 + k]
                top, bot = a + (b - a) * fx, cc + (d - cc) * fx
                out[o + k] = int((top + (bot - top) * fy) * 0.45)
            out[o + 3] = 255
    fw, fh = (COVER, COVER * h // w) if w >= h else (COVER * w // h, COVER)
    pic = box_scale(w, rgba, 0, 0, w, h, fw, fh)
    x0, y0 = (COVER - fw) // 2, (COVER - fh) // 2
    for y in range(fh):
        o = ((y0 + y) * COVER + x0) * 4
        out[o:o + fw * 4] = pic[y * fw * 4:(y + 1) * fw * 4]
    return COVER, COVER, bytes(out)


def sheet8(w, h, rgba):
    """a SHEET8 section body: palette and runs (see src/bm/bm.h)"""
    pal, index = [], {}
    idx = bytearray(w * h)
    for i in range(w * h):
        p = rgba[i * 4:i * 4 + 4]
        key = bytes(p) if p[3] >= 128 else b"\0\0\0\0"
        k = index.get(key)
        if k is None:
            if len(pal) == 256:
                raise SystemExit("--sheet8: the sheet has more than 256 colours")
            k = index[key] = len(pal)
            pal.append(key)
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


def sheet8_decode(body):
    """a SHEET8 section body -> (w, h, rgba)"""
    w, h, ncol = struct.unpack_from("<HHH", body, 0)
    pal = [body[8 + i * 4:12 + i * 4] for i in range(ncol)]
    out, q, n = bytearray(), 8 + ncol * 4, w * h
    while len(out) < n * 4:
        t = body[q]
        q += 1
        if t < 128:
            for k in range(t + 1):
                out += pal[body[q + k]]
            q += t + 1
        else:
            out += pal[body[q]] * (t - 126)
            q += 1
    return w, h, bytes(out[:n * 4])


def pack(lua, sheet=None, map_=None, title="", author="", res=(640, 360), cover=None, sheet_packed=False,
         audio=None, mesh=None, extra=(), layers=None, flags=None, sprites=None, boxes=None):
    """map_: (w, h, cells) of one layer, or layers: [(name, w, h, cells)];
    flags: {cell: flags} of the sheet's cells; sprites: a SPRITES section,
    boxes: a BOXES section (its zones' hitboxes and hurtboxes)"""
    if layers:
        map_ = layers[0][1:]
    sections = []
    if cover:                               # first: the menu reads only the start
        w, h, rgba = cover
        sections.append((SEC_COVER, struct.pack("<HH", w, h) + rgba))
    sections.append((SEC_LUA, lua))
    if sheet:
        w, h, rgba = sheet
        if sheet_packed:
            sections.append((SEC_SHEET8, sheet8(w, h, rgba)))
        else:
            sections.append((SEC_SHEET, struct.pack("<HH", w, h) + rgba))
    if map_:
        w, h, cells = map_
        sections.append((SEC_MAP, struct.pack("<HH", w, h) + cells))
        more = layers_section(layers) if layers else None
        if more:
            sections.append((SEC_LAYERS, more))
    if flags:
        sections.append((SEC_FLAGS, flags_section(flags, sheet[0] if sheet else 256)))
    if sprites:
        sections.append((SEC_SPRITES, sprites))
    if boxes:
        sections.append((SEC_BOXES, boxes))
    if audio:
        sections.append((SEC_AUDIO, audio))
    if mesh:
        sections.append((SEC_MESH, mesh))
    sections.extend(extra)

    table_size = 16 * len(sections)
    offset = 128 + table_size
    table, bodies = b"", b""
    for typ, data in sections:
        table += struct.pack("<IIII", typ, offset + len(bodies), len(data), 0)
        bodies += data
        pad = (-len(bodies)) % 4
        bodies += b"\0" * pad
    after = table + bodies

    header = bytearray(128)
    header[0:8] = b"BMCART\x00\x00"
    struct.pack_into("<HHHHBBHI", header, 8, 1, 128, res[0], res[1], 1, len(sections), 0, crc32(after))
    header[24:24 + 47] = title.encode()[:47].ljust(47, b"\0")
    header[72:72 + 31] = author.encode()[:31].ljust(31, b"\0")
    return bytes(header) + after


def mark_project(data):
    """a project (.bme), the file the tools change: bit 0 of the u16 at 18
    (a .bm is a game, read only for them; src/bm/project.h)"""
    out = bytearray(data)
    struct.pack_into("<H", out, 18, (struct.unpack_from("<H", out, 18)[0] & ~2) | 1)
    return bytes(out)


def mark_b16(data):
    """a .b16 cartridge: bit 1 of the u16 at 18, square screen"""
    out = bytearray(data)
    struct.pack_into("<H", out, 18, (struct.unpack_from("<H", out, 18)[0] & ~1) | 2)
    w, h = struct.unpack_from("<HH", out, 12)
    if w != h:
        struct.pack_into("<HH", out, 12, 360, 360)
    return bytes(out)


COMPAT_AT = 120                     # the compatibility block, bytes 120..127; flag bit 4 (src/bm/bm.h)
FLAG_COMPAT = 16
COMPAT_BITS = {
    "targets": {"pi": 1, "rgb30": 2},
    "input": {"pad": 1, "keyboard": 2, "text": 4, "mouse": 8, "mouse_optional": 16},
    "aspect": {"wide": 1, "square": 2, "any": 4, "safe_area": 8},
    "needs": {"3d": 1, "audio": 2, "net": 4, "mouse_hw": 8},
}


def compat_bits(kind, text):
    """'pad,keyboard' -> the bits of COMPAT_BITS[kind]; SystemExit on an unknown name"""
    bits = 0
    for name in (t.strip().lower() for t in text.split(",") if t.strip()):
        if name not in COMPAT_BITS[kind]:
            raise SystemExit(f"--{kind}: {name!r} is not one of {', '.join(COMPAT_BITS[kind])}")
        bits |= COMPAT_BITS[kind][name]
    return bits


def mark_compat(data, targets, input_=0, aspect=0, needs=0, min_ram=0, min_spec=0, min_w16=0):
    """the compatibility block: bytes 120..127 and flag bit 4 (docs/BM-FORMAT.md). targets is a bit set
    (1 Pi, 2 RGB30) and must not be 0; the other bytes are 0 when a cartridge does not declare them"""
    if not targets & 3:
        raise SystemExit("compatibility block: targets must name pi, rgb30 or both")
    if not 0 <= min_ram <= 0xFFFF or not 0 <= min_spec <= 255 or not 0 <= min_w16 <= 255:
        raise SystemExit("compatibility block: min RAM is 0..65535 MiB, min spec 0..255, min width 0..4080")
    out = bytearray(data)
    struct.pack_into("<H", out, 18, struct.unpack_from("<H", out, 18)[0] | FLAG_COMPAT)
    struct.pack_into("<BBBBHBB", out, COMPAT_AT, targets & 3, input_ & 255, aspect & 255, needs & 255,
                     min_ram, min_spec, min_w16)
    return bytes(out)


def mark_type(data, name):
    """bits 2-3 of the u16 at 18: the type of bm.info (game 0, app 1, utility 2; src/bm/project.h)"""
    out = bytearray(data)
    struct.pack_into("<H", out, 18, (struct.unpack_from("<H", out, 18)[0] & ~12) | (TYPES[name] << 2))
    return bytes(out)


B16_LUA_CHECKS = [
    (r"(?<![\w])math\.random", "math.random: not the same game on every machine"),
    (r"(?<![\w.:])time\(", "time(): no clock; count the ticks instead"),
    (r"(?<![\w.:])save\(", "save(): saves are not designed yet"),
    (r"(?<![\w.:])ls\(", "ls() and files: no file system"),
    (r"(?<![\w.:])cart_\w+\(", "cart_*: the tools' functions, not a game's"),
    (r"(?<![\w.:])udp_\w+\(", "udp: no network from the system"),
    (r"(?<![\w.:])require(?![\w])", "require: not for a .b16"),
    (r"(?<![\w.:])load\(", "load(): not for a .b16"),
    (r"(?<![\w.:])screen\(", "screen(): one machine, one resolution"),
]


def lua_code_only(text):
    """`text` with the comments and the insides of the strings blanked, line
    numbers kept (so a "--" or a name inside a string is not code)"""
    out, i, n = [], 0, len(text)

    def blank(chunk):
        return "".join(c if c == "\n" else " " for c in chunk)
    while i < n:
        c = text[i]
        m = None
        if c == "-" and text.startswith("--", i):
            m = re.match(r"--\[(=*)\[", text[i:])
            if m:
                end = text.find("]" + m.group(1) + "]", i + len(m.group(0)))
                end = n if end < 0 else end + len(m.group(1)) + 2
            else:
                end = text.find("\n", i)
                end = n if end < 0 else end
            out.append(blank(text[i:end]))
            i = end
        elif c == "[" and (m := re.match(r"\[(=*)\[", text[i:])):
            end = text.find("]" + m.group(1) + "]", i + len(m.group(0)))
            end = n if end < 0 else end + len(m.group(1)) + 2
            out.append(blank(text[i:end]))
            i = end
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            end = min(j + 1, n)             # j is the closing quote, the line end or n
            closed = end <= n and text[end - 1] == c and end - 1 > i
            out.append(c + blank(text[i + 1:end - 1 if closed else end]) + (c if closed else ""))
            i = end
        else:
            out.append(c)
            i += 1
    return "".join(out)


def b16_lint(data, label):
    """checks of a .b16 cartridge: raises SystemExit for a sheet of
    more than 256 colours, prints a warning (first occurrence only) for
    each system call of B16_LUA_CHECKS in its Lua"""
    secs = bmmesh.cart_sections(data)
    types = {t for t, _ in secs}
    sheet = next((body for t, body in secs if t == SEC_SHEET), None)
    if sheet is not None and SEC_SHEET8 not in types:
        w, h = struct.unpack_from("<HH", sheet, 0)
        rgba = sheet[4:]
        colours = set()
        for i in range(min(w * h, len(rgba) // 4)):
            p = rgba[i * 4:i * 4 + 4]
            colours.add(bytes(p) if p[3] >= 128 else b"\0\0\0\0")
            if len(colours) > 256:
                raise SystemExit(f"{label}: a .b16 sheet has more than 256 colours (use --sheet8)")
    lua = b"\n".join(body for t, body in secs if t == SEC_LUA)
    if lua:
        lines = lua_code_only(lua.decode("utf-8", errors="replace")).split("\n")
        for pattern, msg in B16_LUA_CHECKS:
            rx = re.compile(pattern)
            for n, line in enumerate(lines, 1):
                if rx.search(line):
                    print(f"warning: {label}:{n}: {msg}", file=sys.stderr)
                    break


def export_from(src, a, out):
    raw = bytearray(open(src, "rb").read())
    if len(raw) < 128 or raw[:8] not in (b"BMCART\x00\x00", b"BM33CART"):
        raise SystemExit(f"{src}: not a bm cartridge")
    w, h = struct.unpack_from("<HH", raw, 12)
    title = raw[24:72].split(b"\0", 1)[0].decode("utf-8", "replace")
    author = raw[72:104].split(b"\0", 1)[0].decode("utf-8", "replace")
    is_b16 = a.b16 or out.lower().endswith(".b16")
    if a.res_set:
        w, h = (int(v) for v in a.res.split("x"))
        struct.pack_into("<HH", raw, 12, w, h)
    elif is_b16 and w != h:
        w, h = 360, 360
        struct.pack_into("<HH", raw, 12, w, h)
    if a.title:
        title = a.title
        raw[24:24 + 48] = title.encode("utf-8")[:47].ljust(48, b"\0")
    if a.author:
        author = a.author
        raw[72:72 + 32] = author.encode("utf-8")[:31].ljust(32, b"\0")
    flags = struct.unpack_from("<H", raw, 18)[0] & ~(1 | 2)
    if is_b16:
        flags |= 2
    struct.pack_into("<H", raw, 18, flags)
    raw[104:120] = b"\0" * 16               # built from; the compatibility block at 120..127 stays
    if a.sheet8:
        secs = list(bmmesh.cart_sections(bytes(raw)))
        if any(t == SEC_SHEET for t, _ in secs) and not any(t == SEC_SHEET8 for t, _ in secs):
            new_secs = []
            for t, payload in secs:
                if t == SEC_SHEET:
                    sw, sh = struct.unpack_from("<HH", payload, 0)
                    from bmres import sheet8_encode      # lazy: bmres imports this module
                    enc = sheet8_encode(sw, sh, payload[4:])
                    new_secs.append((SEC_SHEET8, enc) if enc else (t, payload))
                else:
                    new_secs.append((t, payload))
            table, bodies = b"", b""
            offset = 128 + 16 * len(new_secs)
            for t, payload in new_secs:
                table += struct.pack("<IIII", t, offset + len(bodies), len(payload), 0)
                bodies += payload
                bodies += b"\0" * ((-len(bodies)) % 4)
            after = table + bodies
            raw[17] = len(new_secs)
            struct.pack_into("<I", raw, 20, crc32(after))
            raw = raw[:128] + after
    data = bytes(raw)
    if out.lower().endswith(".bme"):
        data = mark_project(data)
    return data, title, f"{w}x{h}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--output", help="output cartridge (.bm, .bme, or .b16)")
    ap.add_argument("--from", dest="src", help="source .bm or .bme to export or repack (e.g. to .b16)")
    ap.add_argument("--b16", action="store_true", help="export a .b16 edition (360x360 header)")
    ap.add_argument("--targets", help="compatibility block: the kernels the cartridge runs on, pi,rgb30 "
                                      "(flag bit 4, bytes 120..127). Needed by the options below")
    ap.add_argument("--input", default="", help="pad,keyboard,text,mouse,mouse_optional")
    ap.add_argument("--aspect", default="", help="wide,square,any,safe_area")
    ap.add_argument("--needs", default="", help="3d,audio,net,mouse_hw")
    ap.add_argument("--min-ram", type=int, default=0, help="minimum RAM in MiB")
    ap.add_argument("--min-spec", type=int, default=0, help="minimum spec revision (BM_SPEC_REV)")
    ap.add_argument("--min-width", type=int, default=0, help="minimum screen width in pixels (stored in units of 16)")
    ap.add_argument("--lua")
    ap.add_argument("--sheet")
    ap.add_argument("--sheet8", action="store_true", help="store the sheet with a palette and runs")
    ap.add_argument("--map", action="append", default=[],
                    help="a layer of the map: [name=]map.csv (again: one more layer)")
    ap.add_argument("--flags", help="the flags of the sheet's cells (fget/fset)")
    ap.add_argument("--sprites", help="named zones of the sheet: name x y w h [frames [fps]] per line, "
                                      "their boxes under them: hurt|hit|body frame x y w h")
    ap.add_argument("--audio", help="sound bank: .json (scripts/bmaudio.py) or .bmau")
    ap.add_argument("--cover", help="picture for the menu (PNG, any size: 88x88; not square: fitted)")
    ap.add_argument("--models", help="3D models: a .glb from bm Studio, or a .bm with models")
    ap.add_argument("--uv-inset", type=float, help="texture inset of the models, sheet pixels (default 0.25)")
    ap.add_argument("--title", default="")
    ap.add_argument("--author", default="")
    ap.add_argument("--info", help="bm.info of the project (INFO section, docs/PROJECT-LAYOUT.md): "
                                   "key: value lines; name, author and resolution are the defaults of "
                                   "--title, --author and --res")
    ap.add_argument("--res", default=None, choices=RESOLUTIONS)
    a = ap.parse_args()
    info_body, info = read_info(a.info) if a.info else (None, None)
    if info is not None:
        if a.src and not a.lua:
            ap.error("--info goes with --lua, not with --from")
        a.title = a.title or info.get("name", "")
        a.author = a.author or info.get("author", "")
        a.res = a.res or info.get("resolution")
        if a.res and a.res not in RESOLUTIONS:
            ap.error(f"{a.info}: resolution {a.res} is not one of {', '.join(RESOLUTIONS)}")
    a.res_set = a.res is not None
    if not a.res:
        a.res = "360x360" if a.b16 else "640x360"
    if not a.output and a.src and a.b16:
        base, _ = os.path.splitext(a.src)
        a.output = base + ".b16"
    if not a.output or (not a.lua and not a.src):
        ap.error("specify -o and --lua (or --from SRC [--b16] [-o OUT])")
    if a.src and not a.lua:
        data, title, res_str = export_from(a.src, a, a.output)
    else:
        lua = open(a.lua, "rb").read()
        sheet = read_png(a.sheet) if a.sheet else None
        layers = read_layers(a.map) if a.map else None
        flags = read_flags(a.flags) if a.flags else None
        res = tuple(int(v) for v in a.res.split("x"))
        cover = make_cover(read_png(a.cover)) if a.cover else None
        audio = bmaudio.load(a.audio) if a.audio else None
        mesh, extra = None, []
        if info is not None:
            extra.append((SEC_INFO, info_body))
        if a.models and a.models.endswith(".bm"):
            secs = dict(bmmesh.cart_sections(open(a.models, "rb").read()))
            if SEC_MESH not in secs:
                raise SystemExit(f"{a.models}: no 3D models in it")
            mesh = bytearray(secs[SEC_MESH])
            if a.uv_inset is not None:
                struct.pack_into("<H", mesh, 2, max(0, min(65535, round(a.uv_inset * 256))))
            mesh = bytes(mesh)
            if SEC_ANIM in secs:
                extra.append((SEC_ANIM, secs[SEC_ANIM]))
            if not sheet and SEC_SHEET8 in secs:
                sheet, a.sheet8 = sheet8_decode(secs[SEC_SHEET8]), True
            elif not sheet and SEC_SHEET in secs:
                w, h = struct.unpack_from("<HH", secs[SEC_SHEET], 0)
                sheet = (w, h, secs[SEC_SHEET][4:])
        elif a.models:
            if not sheet and a.models.endswith(".glb"):
                png = bmmesh.glb_image(open(a.models, "rb").read())
                if png:
                    sheet = read_png(a.models, png)
                    w, h, rgba = sheet
                    colours = {bytes(rgba[i:i + 4]) if rgba[i + 3] >= 128 else b"" for i in range(0, len(rgba), 4)}
                    a.sheet8 = a.sheet8 or len(colours) <= 256     # smaller, as bm Studio saves it
            models, inset = bmmesh.models_from_file(a.models)
            inset = a.uv_inset if a.uv_inset is not None else (inset if inset is not None else 0.25)
            mesh = bmmesh.encode(models, inset)
        sprites, boxes = read_sprite_file(a.sprites, *(sheet[:2] if sheet else (256, 256))) if a.sprites \
            else (None, None)
        data = pack(lua, sheet, None, a.title, a.author, res, cover, a.sheet8, audio, mesh, extra, layers, flags,
                    sprites, boxes)
        if a.output.lower().endswith(".bme"):
            data = mark_project(data)
        elif a.b16 or a.output.lower().endswith(".b16"):
            data = mark_b16(data)
        if info and info.get("type"):
            if info["type"] not in TYPES:
                raise SystemExit(f"{a.info}: type: {info['type']!r} must be game, app or utility")
            data = mark_type(data, info["type"])
        title, res_str = a.title, a.res
    if a.b16 or a.output.lower().endswith(".b16"):
        b16_lint(data, a.output)
    # no size limit here: the Market takes 100 MiB a file (scripts/mkmarket.py), the console loads what fits in RAM
    if a.targets:
        data = mark_compat(data, compat_bits("targets", a.targets), compat_bits("input", a.input),
                           compat_bits("aspect", a.aspect), compat_bits("needs", a.needs), a.min_ram,
                           a.min_spec, (a.min_width + 15) // 16)
    elif a.input or a.aspect or a.needs or a.min_ram or a.min_spec or a.min_width:
        ap.error("--input, --aspect, --needs and the minimums go with --targets")
    if a.output and os.path.dirname(a.output):
        os.makedirs(os.path.dirname(os.path.abspath(a.output)), exist_ok=True)
    open(a.output, "wb").write(data)
    print(f"{a.output}: {len(data)} bytes ({title or 'untitled'}, {res_str})")


if __name__ == "__main__":
    sys.exit(main())
