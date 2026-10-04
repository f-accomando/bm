#!/usr/bin/env python3
"""
mkbm.py - packs a native bm cartridge (.bm). Standard library only.

  mkbm.py -o game.bm --lua main.lua [--sheet sheet.png [--sheet8]] [--map map.csv]
           [--audio bank.json|bank.bmau] [--models models.glb|pack.bm]
           [--title "My game"] [--author me]
           [--res 640x360|480x270|320x180|256x256]

sheet.png: 8-bit RGB or RGBA PNG (non-interlaced); size multiple of 8 recommended.
--sheet8:  store the sheet with a palette and runs (at most 256 colours): big
           sheets of sprites become a fraction of the size.
map.csv:   one row of comma-separated sprite indices per line (0 = empty).
--audio:   the sound bank (sounds, sound effects, music): JSON or binary,
           see scripts/bmaudio.py; sfx() and music() play it.
--models:  3D models for model(): a .glb exported by bm Studio (sdk/studio;
           its texture is the sprite sheet, used as the sheet when there is
           no --sheet) or a .bm made with bm Studio / bm Animator: its models,
           their skeletons and animations (ANIM), and its sheet when there is
           no --sheet.
Format: see src/bm/bm.h.
"""
import argparse
import os
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
         audio=None, mesh=None, extra=()):
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--lua", required=True)
    ap.add_argument("--sheet")
    ap.add_argument("--sheet8", action="store_true", help="store the sheet with a palette and runs")
    ap.add_argument("--map")
    ap.add_argument("--audio", help="sound bank: .json (scripts/bmaudio.py) or .bmau")
    ap.add_argument("--cover", help="picture for the menu (PNG, any size: 88x88; not square: fitted)")
    ap.add_argument("--models", help="3D models: a .glb from bm Studio, or a .bm with models")
    ap.add_argument("--uv-inset", type=float, help="texture inset of the models, sheet pixels (default 0.25)")
    ap.add_argument("--title", default="")
    ap.add_argument("--author", default="")
    ap.add_argument("--res", default="640x360", choices=["640x360", "480x270", "320x180", "256x256"])
    a = ap.parse_args()
    lua = open(a.lua, "rb").read()
    sheet = read_png(a.sheet) if a.sheet else None
    map_ = read_map(a.map) if a.map else None
    res = tuple(int(v) for v in a.res.split("x"))
    cover = make_cover(read_png(a.cover)) if a.cover else None
    audio = bmaudio.load(a.audio) if a.audio else None
    mesh, extra = None, []
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
    data = pack(lua, sheet, map_, a.title, a.author, res, cover, a.sheet8, audio, mesh, extra)
    open(a.output, "wb").write(data)
    print(f"{a.output}: {len(data)} bytes ({a.title or 'untitled'}, {a.res})")


if __name__ == "__main__":
    sys.exit(main())
