#!/usr/bin/env python3
"""The sprite sheet of the game API test (make test-gameapi): 64x32, one
colour per 8x8 cell, written as a PNG with the standard library.
  cell 1 red (a wall: flag 0), 2 green (a platform: flag 1), 3 blue (a
  ladder: flag 2), 4 yellow (no flags), 8-10 the zone "coin" (cyan, white,
  magenta), 16-17 and 24-25 the zone "big" (red top left, white the rest)."""
import struct
import sys
import zlib

W, H = 64, 32
CELLS = {1: 0xFF0000, 2: 0x00FF00, 3: 0x0000FF, 4: 0xFFFF00, 8: 0x00FFFF, 9: 0xFFFFFF, 10: 0xFF00FF,
         16: 0xFF0000, 17: 0xFFFFFF, 24: 0xFFFFFF, 25: 0xFFFFFF}


def main(out):
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        for x in range(W):
            c = CELLS.get(y // 8 * (W // 8) + x // 8)
            raw += bytes((c >> 16, c >> 8 & 255, c & 255, 255)) if c is not None else b"\0\0\0\0"

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    with open(out, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))


if __name__ == "__main__":
    main(sys.argv[1])
