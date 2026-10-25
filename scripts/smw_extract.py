#!/usr/bin/env python3
"""Reads a Super Mario World ROM that you own and extracts its data on your own machine.

    python3 scripts/smw_extract.py info ROM.sfc
    python3 scripts/smw_extract.py gfx ROM.sfc OUTDIR     # every graphics file as a PNG sheet

Only the Europe Rev 1 dump is accepted (SHA-1 below). The ROM and the output are personal
files: they stay out of git (.gitignore) and out of the Market. Stdlib only."""
import hashlib, struct, sys, zlib

SHA1 = {"46bf36be1c3a2ce9de7581323370bd2d891ad5a1": "Super Mario World (Europe) Rev 1"}
GFX_TABLE, GFX_COUNT = 0xB9A6, 0x32   # lo[50], hi[50], bank[50]; 24-bit LoROM addresses


def off(a):
    """LoROM address to file offset."""
    return ((a >> 16) & 0x7F) * 0x8000 + (a & 0x7FFF)


def load(path):
    d = open(path, "rb").read()
    if len(d) == 0x80200:          # copier header
        d = d[0x200:]
    h = hashlib.sha1(d).hexdigest()
    if h not in SHA1:
        sys.exit("unknown ROM (SHA-1 %s): only %s is supported" % (h, "; ".join(SHA1.values())))
    return d


def lz2(src, p):
    """LC_LZ2, the compression of the graphics. Repeat offsets are little endian."""
    out = bytearray()
    while src[p] != 0xFF:
        h = src[p]; p += 1
        c, n = h >> 5, (h & 31) + 1
        if c == 7:
            c, n = (h >> 2) & 7, (((h & 3) << 8) | src[p]) + 1; p += 1
        if c == 0:
            out += src[p:p + n]; p += n
        elif c == 1:
            out += bytes([src[p]]) * n; p += 1
        elif c == 2:
            out += bytes(src[p + (i & 1)] for i in range(n)); p += 2
        elif c == 3:
            out += bytes((src[p] + i) & 255 for i in range(n)); p += 1
        elif c == 4:
            o = src[p] | (src[p + 1] << 8); p += 2
            for i in range(n):
                out.append(out[o + i])
        else:
            raise ValueError("bad LZ2 command %d at %#x" % (c, p))
    return bytes(out)


def gfx_file(d, i):
    a = d[off(GFX_TABLE) + i] | d[off(GFX_TABLE + GFX_COUNT) + i] << 8 \
        | d[off(GFX_TABLE + 2 * GFX_COUNT) + i] << 16
    return lz2(d, off(a))


def tiles(data, bpp):
    """SNES planar tiles to a list of 8x8 index rows. 2bpp: 16 bytes a tile, 3bpp: 24, 4bpp: 32."""
    size = 8 * bpp
    res = []
    for t in range(len(data) // size):
        b = data[t * size:(t + 1) * size]
        tile = []
        for y in range(8):
            row = []
            for x in range(8):
                v = 0
                for pl in range(bpp):
                    # planes 0,1 interleaved per row; 3bpp's plane 2 follows; 4bpp's 2,3 follow
                    o = (pl & 1) + 2 * y + (16 if pl >= 2 else 0) if bpp == 4 or pl < 2 else 16 + y
                    v |= ((b[o] >> (7 - x)) & 1) << pl
                row.append(v)
            tile.append(row)
        res.append(tile)
    return res


def png(path, w, h, rows):
    """Writes an 8-bit grayscale PNG from rows of bytes."""
    raw = b"".join(b"\0" + bytes(r) for r in rows)
    def chunk(t, c):
        return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c))
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def sheet(ts, bpp, cols=16):
    levels = (1 << bpp) - 1
    rows = []
    for r in range((len(ts) + cols - 1) // cols):
        for y in range(8):
            line = []
            for c in range(cols):
                t = ts[r * cols + c] if r * cols + c < len(ts) else None
                line += [(t[y][x] * 255 // levels if t else 0) for x in range(8)]
            rows.append(line)
    return cols * 8, len(rows), rows


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("info", "gfx"):
        sys.exit(__doc__)
    d = load(sys.argv[2])
    if sys.argv[1] == "info":
        for i in range(GFX_COUNT):
            print("GFX%02X %6d bytes" % (i, len(gfx_file(d, i))))
        return
    import os
    out = sys.argv[3]
    os.makedirs(out, exist_ok=True)
    for i in range(GFX_COUNT):
        g = gfx_file(d, i)
        for bpp in (2, 3):
            w, h, rows = sheet(tiles(g, bpp), bpp)
            png("%s/gfx%02X_%dbpp.png" % (out, i, bpp), w, h, rows)


if __name__ == "__main__":
    main()
