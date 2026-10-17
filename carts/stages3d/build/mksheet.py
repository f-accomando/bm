#!/usr/bin/env python3
"""The sprite sheet of Stages 3D: carts/stages3d/assets/sheet.png, 64x32, one colour per 8x8 cell.
  cell 1 green (stage 1), 2 sand (stage 2), 3 grey (stage 3), 4 red (apple and goal); the rest empty."""
import os
import struct
import zlib

W, H = 64, 32
CELLS = {1: 0x40A060, 2: 0xD0B070, 3: 0x808890, 4: 0xE03030}


def main():
    raw = bytearray()
    for y in range(H):
        raw.append(0)
        for x in range(W):
            c = CELLS.get(y // 8 * (W // 8) + x // 8)
            raw += bytes((c >> 16, c >> 8 & 255, c & 255, 255)) if c is not None else b"\0\0\0\0"

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "assets", "sheet.png")
    with open(out, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    main()
