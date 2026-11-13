#!/usr/bin/env python3
"""Draws the 88x88 covers of the emulators (bm/nes, bm/snes, bm/psx /cover.png).

Retro 1990s pixel art: a 44x44 grid scaled 2x, a small palette, dark outlines.
Original artwork. Run `python3 core/scripts/mkemucovers.py` (or with names: `nes snes psx`).
"""
import os, struct, sys, zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
N, S = 44, 2
FONT = {  # 3x5 glyphs
    "N": ["1001", "1101", "1011", "1001", "1001"], "E": ["111", "100", "110", "100", "111"],
    "S": ["111", "100", "111", "001", "111"], "P": ["110", "101", "110", "100", "100"],
    "X": ["101", "101", "010", "101", "101"],
}
OUT = (16, 12, 24)


class Pix:
    def __init__(self, bg):
        self.p = [[bg] * N for _ in range(N)]

    def set(self, x, y, c):
        if 0 <= x < N and 0 <= y < N:
            self.p[y][x] = c

    def rect(self, x, y, w, h, c):
        for j in range(y, y + h):
            for i in range(x, x + w):
                self.set(i, j, c)

    def box(self, x, y, w, h, fill, hi=None, lo=None):
        """A filled box with a 1-pixel dark outline and an optional bevel."""
        self.rect(x, y, w, h, OUT)
        self.rect(x + 1, y + 1, w - 2, h - 2, fill)
        if hi:
            self.rect(x + 1, y + 1, w - 2, 1, hi); self.rect(x + 1, y + 1, 1, h - 2, hi)
        if lo:
            self.rect(x + 1, y + h - 2, w - 2, 1, lo); self.rect(x + w - 2, y + 1, 1, h - 2, lo)

    def disc(self, cx, cy, r, c, outline=True):
        for y in range(N):
            for x in range(N):
                d = (x - cx) ** 2 + (y - cy) ** 2
                if d <= r * r:
                    self.set(x, y, c)
                elif outline and d <= (r + 1) ** 2:
                    self.set(x, y, OUT)

    def text(self, s, x, y, c, shadow=OUT):
        for ch in s:
            for j, row in enumerate(FONT[ch]):
                for i, b in enumerate(row):
                    if b == "1":
                        self.set(x + i + 1, y + j + 1, shadow)
                        self.set(x + i, y + j, c)
            x += len(FONT[ch][0]) + 1

    def ctext(self, s, y, c):
        self.text(s, (N - sum(len(FONT[ch][0]) + 1 for ch in s) + 1) // 2, y, c)

    def save(self, path):
        raw = b""
        for y in range(N * S):
            raw += b"\x00" + b"".join(bytes(self.p[y // S][x // S]) + b"\xff" for x in range(N * S))
        def chunk(t, d):
            return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
        open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", N * S, N * S, 8, 6, 0, 0, 0))
                               + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def bg_stripes(p, c0, c1):
    for y in range(N):
        for x in range(N):
            p.set(x, y, c0 if ((x + y) // 3) % 2 else c1)


def nes():
    """A grey cartridge; its label shows a sunset over green hills."""
    p = Pix(0)
    bg_stripes(p, (52, 24, 30), (64, 30, 36))
    p.box(7, 4, 30, 37, (150, 150, 158), (196, 196, 204), (100, 100, 110))     # the shell
    for y in range(7, 13, 2):
        p.rect(10, y, 24, 1, (110, 110, 120))                                      # the grip ridges
    p.box(10, 14, 24, 18, (60, 120, 220))                                          # the label
    for y, c in ((15, (250, 140, 60)), (16, (250, 140, 60)), (17, (250, 110, 70)), (18, (220, 80, 90))):
        p.rect(11, y, 22, 1, c)                                                    # the sunset sky
    p.disc(26, 21, 3, (255, 220, 80), outline=False)                               # the sun
    for x in range(11, 33):                                                        # the hills
        h1 = 24 - (2 if 13 <= x <= 19 else 1 if x in (12, 20) else 0)
        h2 = 26 - (2 if 23 <= x <= 30 else 1 if x in (22, 31) else 0)
        for y in range(h1, 31):
            p.set(x, y, (40, 150, 60))
        for y in range(h2, 31):
            p.set(x, y, (24, 104, 48))
    p.rect(11, 24 - 2, 1, 0, OUT)
    p.rect(9, 34, 26, 7, (210, 40, 50))                                            # the red title band
    p.rect(9, 34, 26, 1, (240, 90, 90))
    p.ctext("NES", 35, (255, 255, 255))
    return p


def snes():
    """A grey pad with the four coloured buttons over a purple night."""
    p = Pix(0)
    for y in range(N):
        c = (40 + y, 24 + y // 2, 80 + y)
        p.rect(0, y, N, 1, c)
    for x, y in ((4, 3), (13, 7), (36, 4), (40, 11), (30, 9), (8, 12), (21, 3)):
        p.set(x, y, (255, 255, 220))                                               # the stars
    p.ctext("SNES", 4, (255, 230, 120))
    p.disc(11, 25, 9, (186, 186, 196))                                             # the two grips
    p.disc(32, 25, 9, (186, 186, 196))
    p.rect(11, 16, 22, 19, (186, 186, 196))
    p.rect(11, 15, 22, 1, OUT); p.rect(13, 35, 18, 1, OUT)
    p.rect(13, 34, 18, 1, (150, 150, 160))
    p.rect(9, 21, 7, 3, (40, 40, 48)); p.rect(11, 19, 3, 7, (40, 40, 48))         # the cross
    p.set(12, 22, (80, 80, 92))
    for (x, y), c in (((32, 18), (90, 70, 200)), ((37, 23), (60, 170, 80)),
                      ((27, 23), (230, 200, 40)), ((32, 28), (210, 50, 60))):
        p.disc(x, y, 2, c)                                                         # X A Y B
        p.set(x - 1, y - 1, (255, 255, 255))
    p.rect(18, 26, 3, 1, (100, 100, 112)); p.rect(23, 26, 3, 1, (100, 100, 112))   # select start
    p.rect(10, 38, 24, 2, (90, 70, 200)); p.rect(10, 40, 24, 1, (210, 50, 60))
    return p


def psx():
    """A rainbow compact disc and the four face symbols on a dark grid."""
    p = Pix((20, 20, 30))
    for i in range(0, N, 4):
        p.rect(i, 0, 1, N, (30, 30, 44)); p.rect(0, i, N, 1, (30, 30, 44))
    p.disc(22, 19, 14, (200, 205, 215))                                            # the disc
    import math
    ring = [(240, 80, 90), (250, 170, 60), (240, 230, 90), (90, 210, 120), (80, 160, 240), (170, 100, 230)]
    for y in range(N):
        for x in range(N):
            dx, dy = x - 22, y - 19
            r = math.hypot(dx, dy)
            a = math.degrees(math.atan2(dy, dx)) % 360
            if 6.5 < r < 12.5 and (200 <= a <= 250 or 20 <= a <= 70):
                p.set(x, y, ring[min(5, int((r - 6.5)))])                           # the shine
    p.disc(22, 19, 4, (120, 120, 132))
    p.disc(22, 19, 1, (20, 20, 30), outline=False)
    # the face symbols
    g, r, b, k = (60, 200, 140), (240, 80, 90), (100, 150, 240), (240, 130, 200)
    for j in range(5):                                                             # triangle
        p.set(5 - j + 0, 33 + j, g); p.set(5 + j, 33 + j, g)
    p.rect(1, 37, 9, 1, g)
    for (dx, dy) in ((-2, -1), (-2, 0), (-2, 1), (2, -1), (2, 0), (2, 1), (-1, -2), (0, -2), (1, -2), (-1, 2), (0, 2), (1, 2)):
        p.set(16 + dx, 35 + dy, r)                                                 # circle
    for j in range(5):                                                             # cross
        p.set(25 + j, 33 + j, b); p.set(29 - j, 33 + j, b)
    p.rect(35, 33, 5, 1, k); p.rect(35, 37, 5, 1, k); p.rect(35, 33, 1, 5, k); p.rect(39, 33, 1, 5, k)  # square
    p.rect(0, 38, N, 6, (12, 12, 18))
    p.ctext("PSX", 39, (230, 230, 240))
    return p


COVERS = {"nes": nes, "snes": snes, "psx": psx}

if __name__ == "__main__":
    for name in sys.argv[1:] or COVERS:
        COVERS[name]().save(os.path.join(ROOT, "bm", name, "cover.png"))
        print("wrote bm/%s/cover.png" % name)
