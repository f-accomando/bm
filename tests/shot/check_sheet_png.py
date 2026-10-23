#!/usr/bin/env python3
"""tests/shot/check_sheet_png.py LOG: the "png:" lines tests/shot/sheet_png.lua logs. Decodes the PNG
bytes (signature, CRCs, zlib, Adler, colour type 6) and checks the RGBA pixels, the sizes, the order of
the strips and the refusals."""
import struct
import sys
import zlib


def decode(hexs):
    d = bytes.fromhex(hexs)
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
    assert (depth, ctype) == (8, 6), "8 bits RGBA"
    raw = zlib.decompress(chunks[1][1])
    row = 4 * w + 1
    assert len(raw) == row * h, "size of the data"
    assert all(raw[y * row] == 0 for y in range(h)), "filter 0"
    return w, h, [[tuple(raw[y * row + 1 + 4 * x:y * row + 5 + 4 * x]) for x in range(w)] for y in range(h)]


R, G, B, W, K = (255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255,) * 4, (0, 0, 0, 255)
T = (0, 0, 0, 0)          # a transparent pixel: alpha 0 (its colour is not checked)


def same(got, want, what):
    for y, (gr, wr) in enumerate(zip(got, want)):
        for x, (g, w) in enumerate(zip(gr, wr)):
            ok = g[3] == 0 if w[3] == 0 else g == w
            assert ok, f"{what}: pixel {x},{y} is {g}, expected {w}"


def main():
    res = {}
    seen_end = False
    for line in open(sys.argv[1], errors="replace"):
        if not line.startswith("png: "):
            continue
        parts = line.split()
        if parts[1] == "end":
            seen_end = True
        elif parts[2] == "nil":
            res[parts[1]] = ("nil", " ".join(parts[3:]))
        else:
            res[parts[1]] = ("png", parts[2])
    assert seen_end, "the cart did not finish"

    w, h, px = decode(res["one"][1])
    assert (w, h) == (4, 2), (w, h)
    same(px, [[R, G, B, T], [W, K, R, T]], "one")

    w, h, px = decode(res["list"][1])
    assert (w, h) == (7, 2), (w, h)
    # strips side by side, in the order given: {8,8,2,2} {10,8,2,2} {20,20,3,2}
    same(px, [[R, G, B, T, B, B, B], [W, K, R, T, G, G, G]], "list")

    for name, word in (("outside", "outside"), ("height", "height"), ("toowide", "4096")):
        kind, why = res[name]
        assert kind == "nil" and word in why, (name, res[name])
    w, h, px = decode(res["widest"][1])
    assert (w, h) == (4096, 8), (w, h)
    assert all(p[3] == 0 for r in px for p in r), "a new sheet is transparent"
    print("sheet_png: ok")


main()
