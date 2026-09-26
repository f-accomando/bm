#!/usr/bin/env python3
"""
mkb33.py - packs a native bm33 cartridge (.b33). Standard library only.

  mkb33.py -o game.b33 --lua main.lua [--sheet sheet.png] [--map map.csv]
           [--title "My game"] [--author me] [--res 640x360|320x180]

sheet.png: 8-bit RGB or RGBA PNG (non-interlaced); size multiple of 8 recommended.
map.csv:   one row of comma-separated sprite indices per line (0 = empty).
Format: see src/b33/b33.h.
"""
import argparse
import struct
import sys
import zlib

SEC_LUA, SEC_SHEET, SEC_MAP = 1, 2, 3


def read_png(path):
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


def pack(lua, sheet=None, map_=None, title="", author="", res=(640, 360)):
    sections = [(SEC_LUA, lua)]
    if sheet:
        w, h, rgba = sheet
        sections.append((SEC_SHEET, struct.pack("<HH", w, h) + rgba))
    if map_:
        w, h, cells = map_
        sections.append((SEC_MAP, struct.pack("<HH", w, h) + cells))

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
    header[0:8] = b"BM33CART"
    struct.pack_into("<HHHHBBHI", header, 8, 1, 128, res[0], res[1], 1, len(sections), 0, crc32(after))
    header[24:24 + 47] = title.encode()[:47].ljust(47, b"\0")
    header[72:72 + 31] = author.encode()[:31].ljust(31, b"\0")
    return bytes(header) + after


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--lua", required=True)
    ap.add_argument("--sheet")
    ap.add_argument("--map")
    ap.add_argument("--title", default="")
    ap.add_argument("--author", default="")
    ap.add_argument("--res", default="640x360", choices=["640x360", "320x180"])
    a = ap.parse_args()
    lua = open(a.lua, "rb").read()
    sheet = read_png(a.sheet) if a.sheet else None
    map_ = read_map(a.map) if a.map else None
    res = tuple(int(v) for v in a.res.split("x"))
    data = pack(lua, sheet, map_, a.title, a.author, res)
    open(a.output, "wb").write(data)
    print(f"{a.output}: {len(data)} bytes ({a.title or 'untitled'}, {a.res})")


if __name__ == "__main__":
    sys.exit(main())
