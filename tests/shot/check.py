#!/usr/bin/env python3
"""tests/shot/check.py DIR: the PNGs of bm/shots (DIR = the SD's folder) as tests/shot/cart.lua makes
them: the names, a valid PNG (CRCs, zlib, Adler), the size, and the pixels the cart drew."""
import os
import struct
import sys
import zlib


def read_png(path):
    d = open(path, "rb").read()
    assert d[:8] == b"\x89PNG\r\n\x1a\n", "signature"
    pos, chunks = 8, []
    while pos < len(d):
        n, typ = struct.unpack(">I4s", d[pos:pos + 8])
        body = d[pos + 8:pos + 8 + n]
        crc, = struct.unpack(">I", d[pos + 8 + n:pos + 12 + n])
        assert zlib.crc32(typ + body) == crc, f"CRC of {typ}"
        chunks.append((typ, body))
        pos += 12 + n
    assert [c[0] for c in chunks] == [b"IHDR", b"IDAT", b"IEND"], chunks
    w, h, depth, ctype, _, _, _ = struct.unpack(">IIBBBBB", chunks[0][1])
    assert (depth, ctype) == (8, 2), "8 bits RGB"
    raw = zlib.decompress(chunks[1][1])                 # checks the Adler-32 too
    row = 3 * w + 1
    assert len(raw) == row * h, "size of the data"
    assert all(raw[y * row] == 0 for y in range(h)), "filter 0"
    return w, h, lambda x, y: tuple(raw[y * row + 1 + 3 * x:y * row + 4 + 3 * x])


def main():
    shots = os.path.join(sys.argv[1], "bm", "shots")
    names = sorted(os.listdir(shots))
    # the cart's title is "shot test" (the first four letters: SHOT); shot("ab!c") keeps the letters: ABC
    want = ["ABC0001.PNG", "SHOT0001.PNG", "SHOT0002.PNG"]
    ok = names == want
    print(f"shot: names {names} {'ok' if ok else 'WRONG, want ' + str(want)}")
    assert ok
    img = {}
    for n in want:
        w, h, img[n] = read_png(os.path.join(shots, n))
        assert (w, h) == (640, 360), (n, w, h)
        assert img[n](20, 20) != img[n](0, 0), "the rectangle at 10,10 is there"
        print(f"shot: {n} {w}x{h} ok")
    # the moving rectangle starts at x 12 in frame 3 and reaches x 40; at 24 in frame 6, to x 64
    a, b = img["SHOT0001.PNG"], img["SHOT0002.PNG"]
    assert a(14, 60) != a(0, 0) and b(14, 60) == b(0, 0), "frame 3"
    assert b(60, 60) != b(0, 0) and a(60, 60) == a(0, 0), "frame 6"
    print("shot: 3/3 checks passed")


main()
