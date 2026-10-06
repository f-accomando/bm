#!/usr/bin/env python3
"""Draws the 128x80 cover pictures of the demo games (carts/<game>/cover.png).

The covers are printed on the cartridges in the menu. They are drawn here
in code (no image editor needed) with the console font; run the script again
after changing it: `python3 scripts/mkcovers.py` (or with names: only those,
`python3 scripts/mkcovers.py sound`).
"""
import os
import re
import struct
import zlib

W, H = 128, 80
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def load_font():
    src = open(os.path.join(ROOT, "src", "gfx", "font8x16.c")).read()
    rows = re.findall(r"\{ (0x[0-9a-f]{2}(?:, 0x[0-9a-f]{2}){15}) \}", src)
    return [bytes(int(b, 16) for b in r.split(", ")) for r in rows]


FONT = load_font()


def rgb(c):
    return (c >> 16 & 255, c >> 8 & 255, c & 255)


class Canvas:
    def __init__(self, bg=0):
        self.px = [rgb(bg)] * (W * H)

    def set(self, x, y, c):
        x, y = int(x), int(y)
        if 0 <= x < W and 0 <= y < H:
            self.px[y * W + x] = rgb(c) if isinstance(c, int) else c

    def rect(self, x, y, w, h, c):
        for yy in range(int(y), int(y + h)):
            for xx in range(int(x), int(x + w)):
                self.set(xx, yy, c)

    def vgradient(self, y0, y1, c0, c1):
        a, b = rgb(c0), rgb(c1)
        for y in range(y0, y1):
            t = (y - y0) / max(1, y1 - y0 - 1)
            col = tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
            for x in range(W):
                self.set(x, y, col)

    def circle(self, cx, cy, r, c):
        for y in range(int(cy - r), int(cy + r) + 1):
            for x in range(int(cx - r), int(cx + r) + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                    self.set(x, y, c)

    def tri(self, a, b, c, col):
        xs = [a[0], b[0], c[0]]
        ys = [a[1], b[1], c[1]]

        def edge(p, q, x, y):
            return (q[0] - p[0]) * (y - p[1]) - (q[1] - p[1]) * (x - p[0])
        area = edge(a, b, *c)
        if area == 0:
            return
        for y in range(int(min(ys)), int(max(ys)) + 1):
            for x in range(int(min(xs)), int(max(xs)) + 1):
                px, py = x + 0.5, y + 0.5
                w0, w1, w2 = edge(b, c, px, py), edge(c, a, px, py), edge(a, b, px, py)
                if (w0 >= 0 and w1 >= 0 and w2 >= 0) or (w0 <= 0 and w1 <= 0 and w2 <= 0):
                    self.set(x, y, col)

    def text(self, s, x, y, c, scale=1, outline=None, shadow=None):
        def glyphs(dx, dy, col):
            for i, ch in enumerate(s.encode("cp437")):
                g = FONT[ch]
                for row in range(16):
                    for bit in range(8):
                        if g[row] & (0x80 >> bit):
                            self.rect(x + dx + (i * 8 + bit) * scale, y + dy + row * scale, scale, scale, col)
        if shadow is not None:
            glyphs(scale, scale, shadow)
        if outline is not None:
            for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, 1), (-1, 1), (1, -1)):
                glyphs(dx, dy, outline)
        glyphs(0, 0, c)

    def ctext(self, s, y, c, scale=1, **kw):
        self.text(s, (W - len(s) * 8 * scale) // 2, y, c, scale, **kw)

    def art(self, rows, palette, x, y, scale=1):
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch in palette:
                    self.rect(x + i * scale, y + j * scale, scale, scale, palette[ch])

    def save(self, path):
        raw = b"".join(b"\0" + bytes(v for p in self.px[y * W:(y + 1) * W] for v in p) for y in range(H))

        def chunk(t, d):
            return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def pong():
    c = Canvas()
    c.vgradient(0, H, 0x0A1428, 0x1C2C48)
    for y in range(26, 80, 8):
        c.rect(63, y, 2, 4, 0x405070)
    c.rect(10, 38, 5, 22, 0xF0F0F0)
    c.rect(113, 30, 5, 22, 0xF0F0F0)
    for i, (x, y) in enumerate(((30, 60), (42, 55), (54, 50), (66, 45))):
        g = 60 + i * 50
        c.rect(x, y, 5, 5, (g, g, g // 2))
    c.rect(78, 40, 6, 6, 0xFFC040)
    c.ctext("PONG", 2, 0xFFC040, scale=2, outline=0x000000)
    return c


def snake():
    c = Canvas(0x0C1A10)
    for y in range(0, H, 8):
        for x in range(0, W, 8):
            if (x // 8 + y // 8) % 2 == 0:
                c.rect(x, y, 8, 8, 0x122818)
    path = [(1, 8), (2, 8), (3, 8), (4, 8), (5, 8), (5, 7), (5, 6), (6, 6), (7, 6), (8, 6),
            (9, 6), (9, 7), (9, 8), (10, 8), (11, 8), (12, 8), (12, 7)]
    for i, (gx, gy) in enumerate(path):
        col = 0xB8F070 if i == len(path) - 1 else 0x6CC04A
        c.rect(gx * 8 + 1, gy * 8 + 1, 6, 6, col)
    hx, hy = path[-1]
    c.rect(hx * 8 + 2, hy * 8 + 2, 1, 1, 0x000000)
    c.rect(hx * 8 + 5, hy * 8 + 2, 1, 1, 0x000000)
    c.circle(12 * 8 + 3.5, 4 * 8 + 3.5, 3.5, 0xE84040)
    c.rect(12 * 8 + 4, 4 * 8 - 1, 2, 2, 0x6CC04A)
    c.ctext("SNAKE", 2, 0xFFD050, scale=2, outline=0x000000)
    return c


SHIP = [".......11.......", "......1221......", "......1221......", ".....122221.....",
        ".....123321.....", "....12333321....", "...1223333221...", "..122223322221..",
        ".12222222222221.", "1222222222222221", "1221122222211221", "121..122221..121",
        "11...122221...11", ".....1.44.1.....", "......4554......", ".......55......."]
ALIEN = ["....6......6....", ".....6....6.....", "....66666666....", "...6677667766...",
         "..667777777766..", ".66777877877766.", "6667777777777666", "6.667777777766.6",
         "6..6666666666..6", "...66..66..66...", "..66...66...66..", ".66....66....66."]
PAL = {"1": 0x2050A0, "2": 0x70A8F0, "3": 0xE0F0FF, "4": 0xFF8020, "5": 0xFFE060,
       "6": 0x30A040, "7": 0x70E060, "8": 0x000000}


def shooter():
    c = Canvas(0x05060C)
    import random
    rnd = random.Random(7)
    for _ in range(90):
        v = rnd.choice((0x404860, 0x8090B0, 0xFFFFFF))
        c.set(rnd.randrange(W), rnd.randrange(H), v)
    for i in range(4):
        c.art(ALIEN, PAL, 8 + i * 30, 36)
    c.rect(62, 50, 2, 8, 0xFFE060)
    c.rect(66, 56, 2, 5, 0xFFE060)
    c.art(SHIP, PAL, 56, 62)
    c.ctext("STAR", 0, 0xFFD050, scale=1, outline=0x000000)
    c.ctext("SHOOTER", 14, 0xFFD050, scale=2 if 7 * 16 <= W else 1, outline=0x000000)
    return c


def astrowing():
    c = Canvas()
    c.vgradient(0, 44, 0x0E1848, 0x3050A0)
    c.rect(0, 44, W, 3, 0x8098C8)
    c.vgradient(47, H, 0x2E6A3A, 0x1C4A26)
    for i in range(-6, 7):                  # ground lines converging to the horizon
        x0 = 64 + i * 40
        for t in range(0, 34):
            y = 47 + t
            x = 64 + (x0 - 64) * t / 33
            c.set(x, y, 0x3C8048)
    # the fighter, seen from behind, banking
    body = [(64, 50), (56, 64), (72, 64)]
    c.tri((64, 46), (58, 63), (70, 63), 0xF0F0F8)
    c.tri((64, 52), (58, 63), (70, 63), 0x5070E0)
    c.tri((58, 60), (22, 70), (60, 64), 0x3060D0)
    c.tri((70, 60), (106, 54), (68, 64), 0x3060D0)
    c.tri((22, 70), (20, 58), (26, 68), 0xE0E0E8)
    c.tri((106, 54), (106, 42), (101, 55), 0xE0E0E8)
    c.circle(64, 64, 2.5, 0xFF9020)
    _ = body
    # enemies and a ring in the distance
    for x, y in ((30, 34), (92, 30), (76, 38)):
        c.tri((x, y), (x - 5, y - 3), (x + 5, y - 3), 0xE04040)
    for a in range(0, 360, 12):
        import math
        c.set(100 + 6 * math.cos(math.radians(a)), 40 + 6 * math.sin(math.radians(a)), 0xFFD040)
    c.ctext("ASTRO", 0, 0xFFD050, scale=1, outline=0x000000)
    c.ctext("WING", 12, 0xFFD050, scale=2, outline=0x000000)
    return c


def hunt():
    import math
    c = Canvas()
    c.vgradient(0, H, 0x06040A, 0x1A0C10)
    c.circle(98, 22, 11, 0xE8D8B0)           # a pale moon, veiled
    c.circle(102, 19, 10, 0x0C080E)
    # the cathedral: towers and spires in black
    for x0, w, h in ((14, 10, 44), (26, 20, 34), (48, 10, 50), (60, 18, 30), (80, 8, 40)):
        c.rect(x0, H - h, w, h, 0x000000)
        c.tri((x0, H - h), (x0 + w, H - h), (x0 + w / 2, H - h - 14), 0x000000)
    for x in (18, 52, 83):                    # lit windows
        c.rect(x, H - 30, 2, 5, 0xFFA040)
    # the hunter: hat, coat, the cleaver raised
    c.tri((104, 44), (122, 44), (113, 38), 0x100808)
    c.rect(108, 44, 10, 4, 0x100808)
    c.tri((104, 48), (122, 48), (113, 78), 0x160C0C)
    c.rect(111, 52, 4, 3, 0x801010)
    for i in range(10):
        c.set(120 + i // 2, 46 - i, 0xC8C8D0)
    c.ctext("HUNTER'S", 2, 0xB01818, scale=1, outline=0x000000)
    c.ctext("NIGHT", 18, 0xC01818, scale=2, outline=0x000000)
    _ = math
    return c


def editor():
    c = Canvas()
    c.vgradient(0, H, 0x1C2030, 0x2A3048)
    # a sprite grid on the left, a code page on the right
    for j in range(8):
        for i in range(8):
            v = (i * 3 + j * 5) % 7
            col = (0xFFC050, 0x3060D0, 0x70A8F0, 0xE04040, 0x6CC04A, 0x14161E, 0xE0E4F0)[v]
            c.rect(8 + i * 6, 30 + j * 6, 5, 5, col)
    for k, (w, col) in enumerate(((30, 0xFF7AB0), (44, 0x70D0FF), (22, 0x90E070), (38, 0xE0E4F0), (18, 0xFFB060), (34, 0x70D0FF))):
        c.rect(64, 32 + k * 7, 6, 3, 0x707890)
        c.rect(74, 32 + k * 7, w, 3, col)
    c.tri((96, 70), (118, 48), (122, 52), 0xFFC050)       # a pencil
    c.tri((96, 70), (100, 66), (104, 70), 0xE0C8A0)
    c.ctext("SDK", 4, 0xFFC050, scale=2, outline=0x000000)
    return c


def sound():
    import math
    c = Canvas()
    c.vgradient(0, H, 0x15171C, 0x23272F)
    # pads of a step sequencer, some lit
    lit = {(0, 0): 0xF2701D, (2, 0): 0xF2701D, (1, 1): 0xF2C230, (3, 1): 0xF2C230, (0, 2): 0x35C46E,
           (3, 2): 0x35C46E, (1, 3): 0x3C8CE7, (2, 3): 0x3C8CE7, (3, 3): 0xA472F2}
    for j in range(4):
        for i in range(4):
            col = lit.get((i, j), 0x343A46)
            c.rect(10 + i * 13, 30 + j * 12, 10, 9, col)
    # a wave going through
    prev = None
    for x in range(64, 122):
        t = (x - 64) / 58
        y = 54 - math.sin(t * math.pi * 4) * 14 * (1 - t * 0.6)
        if prev:
            c.rect(x, min(prev, y), 1, abs(y - prev) + 2, 0xF2701D)
        prev = y
    c.ctext("SOUND", 4, 0xF2701D, scale=2, outline=0x000000)
    return c


def nano8():
    c = Canvas()
    c.vgradient(0, H, 0x101420, 0x241a38)
    # a little 128x128 screen, its 16 colours in a band, a sprite on a hill
    pal = (0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8,
           0xFF004D, 0xFFA300, 0xFFEC27, 0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA)
    c.rect(36, 26, 56, 52, 0x39425C)
    c.rect(38, 28, 52, 48, 0x1D2B53)
    for i in range(16):
        c.rect(38 + (i % 8) * 6.5, 28 + (i // 8) * 4, 6.5, 4, pal[i])
    for y in range(60, 76):                 # the hill, inside the screen
        for x in range(38, 90):
            d = (x - 64) ** 2 + (y - 92) ** 2
            if d <= 27 * 27:
                c.set(x, y, 0x00E436)
            elif d <= 28 * 28:
                c.set(x, y, 0x008751)
    hero = ["..88..", ".8888.", "877778", ".7117.", ".7777.", ".8..8."]
    c.art(hero, {"8": 0xFF004D, "7": 0xFFCCAA, "1": 0x000000}, 58, 54, 1)
    for x, y in ((44, 40), (82, 44), (70, 38)):
        c.set(x, y, 0xFFF1E8)
    c.text("nano", 8, 2, 0xE8ECF4, scale=2, outline=0x000000)
    c.text("8", 8 + 4 * 16, 2, 0xFF4F78, scale=2, outline=0x000000)
    c.rect(100, 34, 18, 26, 0x39425C)       # a cart beside the screen
    c.rect(102, 36, 14, 12, 0xFF77A8)
    c.rect(102, 50, 14, 2, 0x1C2233)
    c.rect(102, 54, 14, 2, 0x1C2233)
    return c


def typing():
    c = Canvas()
    c.vgradient(0, H, 0x0E1220, 0x1A2238)
    # a cross with its four consonants, the buttons with their colours
    cx, cy = 30, 52
    c.rect(cx - 6, cy - 20, 12, 40, 0x0A0D14)
    c.rect(cx - 20, cy - 6, 40, 12, 0x0A0D14)
    for (dx, dy, ch) in ((0, -15, "t"), (15, 0, "n"), (0, 15, "r"), (-15, 0, "s")):
        c.rect(cx + dx - 6, cy + dy - 7, 12, 14, 0x2A3550)
        c.text(ch, cx + dx - 4, cy + dy - 8, 0xFFFFFF)
    fx = 98
    for (dx, dy, col) in ((0, -13, 0x5EE0A0), (13, 0, 0xFF6E6E), (0, 13, 0x7AA8FF), (-13, 0, 0xF08CD8)):
        c.circle(fx + dx, cy + dy, 5, col)
    # the text being written: a syllable turning, the rest of the word
    c.text("ciao", 46, 24, 0xFFFFFF)
    c.rect(46, 40, 32, 1, 0x8FD0FF)
    c.rect(79, 25, 2, 14, 0xFFC050)
    c.ctext("TYPE", 2, 0xFFC050, scale=2, outline=0x000000)
    return c


def write():
    c = Canvas()
    c.vgradient(0, H, 0x1C2234, 0x2E3A58)
    # a page with its title, lines of text (a word in bold, one underlined)
    c.rect(37, 39, 52, 40, 0x10131C)
    c.rect(34, 36, 52, 40, 0xFFFFFF)
    c.rect(46, 40, 28, 3, 0x15233F)
    for i, w in enumerate((44, 40, 44, 28, 36)):
        c.rect(38, 47 + i * 5, w, 2, 0x8A93A6)
    c.rect(38, 52, 11, 2, 0x1C1F26)
    c.rect(54, 63, 14, 1, 0x1A5FD8)
    c.rect(67, 66, 2, 5, 0x1A5FD8)          # the cursor
    # a pen
    c.tri((106, 38), (114, 46), (94, 66), 0xFFB84A)
    c.tri((94, 66), (114, 46), (99, 71), 0xE0962A)
    c.tri((94, 66), (99, 71), (89, 75), 0x1C1F26)
    c.ctext("WRITE", 2, 0xFFB84A, scale=2, outline=0x000000)
    return c


COVERS = (("pong", pong), ("snake", snake), ("shooter", shooter), ("astrowing", astrowing),
          ("hunt", hunt), ("editor", editor), ("sound", sound), ("nano8", nano8), ("typing", typing),
          ("write", write))


def main():
    import sys
    only = sys.argv[1:]                     # names: draw only those covers
    for name, fn in COVERS:
        if only and name not in only:
            continue
        path = os.path.join(ROOT, "carts", name, "cover.png")
        fn().save(path)
        print(path)


if __name__ == "__main__":
    main()
