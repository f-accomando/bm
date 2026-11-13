#!/usr/bin/env python3
"""Draws the 88x88 covers of the emulators (bm/nes, bm/snes, bm/psx /cover.png).

Retro 1990s pixel art at the native 88x88: every shape is a pixel mask, shaded with a bevel
(light top-left, dark bottom-right), a 1-pixel dark outline and ordered dithering on the gradients.
Original artwork. Run `python3 core/scripts/mkemucovers.py` (or with names: `nes snes psx`).
"""
import math, os, sys

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
W = 88
INK = (18, 12, 28)
BAYER = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))
FONT = {
    "N": ("10001", "11001", "11101", "10111", "10011", "10001", "10001"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "X": ("10001", "10001", "01010", "00100", "01010", "10001", "10001"),
}


class Canvas:
    def __init__(self):
        self.im = Image.new("RGBA", (W, W), INK + (255,))
        self.px = self.im.load()

    def set(self, x, y, c):
        if 0 <= x < W and 0 <= y < W:
            self.px[x, y] = tuple(c[:3]) + (255,)

    def get(self, x, y):
        return self.px[x, y][:3]

    def dither_v(self, y0, y1, stops, x0=0, x1=W):
        """A vertical gradient through the colour stops, dithered (no smooth blends: real pixel steps)."""
        stops = [mix(stops[min(i // 4, len(stops) - 2)], stops[min(i // 4, len(stops) - 2) + 1], (i % 4) / 4)
                 for i in range(4 * (len(stops) - 1))] + [stops[-1]]
        n = len(stops) - 1
        for y in range(y0, y1):
            t = (y - y0) / max(1, y1 - y0 - 1) * n
            i = min(int(t), n - 1)
            f = t - i
            for x in range(x0, x1):
                self.set(x, y, stops[i + 1] if f * 16 > BAYER[y & 3][x & 3] + 0.5 else stops[i])

    def fill(self, mask, c):
        for (x, y) in mask:
            self.set(x, y, c)

    def body(self, mask, base, hi, lo, outline=INK, depth=1):
        """Fills a shape with a bevel and an outline."""
        self.fill(outline_of(mask), outline) if outline else None
        for (x, y) in mask:
            c = base
            if any((x - d, y) not in mask or (x, y - d) not in mask for d in range(1, depth + 1)):
                c = hi
            if any((x + d, y) not in mask or (x, y + d) not in mask for d in range(1, depth + 1)):
                c = lo
            self.set(x, y, c)

    def title(self, text, y, top, bottom, shade):
        """Bold 2x title, gradient fill, outline and a drop shadow."""
        w = len(text) * 12 - 2
        x0 = (W - w) // 2
        mask = set()
        for k, ch in enumerate(text):
            for j, row in enumerate(FONT[ch]):
                for i, b in enumerate(row):
                    if b == "1":
                        for dx in (0, 1):
                            for dy in (0, 1):
                                mask.add((x0 + k * 12 + i * 2 + dx, y + j * 2 + dy))
        self.fill(outline_of({(x + 1, y + 2) for x, y in mask} | mask), INK)
        self.fill({(x + 1, y + 2) for x, y in mask} - mask, shade)
        for (x, yy) in mask:
            t = yy - y
            self.set(x, yy, top if t < 6 else mix(top, bottom, 0.5) if t < 8 else bottom)

    def stars(self, pts):
        for i, (x, y) in enumerate(pts):
            if i % 3 == 0:
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                    self.set(x + dx, y + dy, (150, 140, 220))
                self.set(x, y, (255, 255, 255))
            else:
                self.set(x, y, (200, 200, 255) if i % 2 else (255, 230, 180))

    def save(self, path):
        self.im.save(path)


def outline_of(mask):
    out = set()
    for (x, y) in mask:
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if (x + dx, y + dy) not in mask:
                    out.add((x + dx, y + dy))
    return out


def rect(x, y, w, h):
    return {(i, j) for i in range(x, x + w) for j in range(y, y + h)}


def rrect(x, y, w, h, r=2):
    m = rect(x, y, w, h)
    for i in range(r):
        for j in range(r - i):
            for (cx, cy) in ((x + i, y + j), (x + w - 1 - i, y + j), (x + i, y + h - 1 - j), (x + w - 1 - i, y + h - 1 - j)):
                m.discard((cx, cy))
    return m


def circle(cx, cy, r):
    return {(x, y) for x in range(int(cx - r - 1), int(cx + r + 2)) for y in range(int(cy - r - 1), int(cy + r + 2))
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r + r * 0.6}


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


STARS = [(6, 24), (80, 20), (12, 40), (78, 44), (4, 60), (84, 66), (18, 80), (70, 84), (40, 22), (60, 26), (26, 28)]


# ------------------------------------------------------------------------------------------ the pads
# Every cover has the same layout: the title on top, the console's own pad in the middle (same place
# and size class), a themed background behind. The pads are drawn from the real shapes.

def symbol(c, kind, x, y, col):
    """A 5x5 PlayStation face symbol, its top-left corner at (x, y)."""
    if kind == 0:                                                   # triangle
        for j in range(5):
            c.set(x + 2 - j // 2, y + j, col); c.set(x + 2 + j // 2, y + j, col)
        c.fill(rect(x, y + 4, 5, 1), col)
    elif kind == 1:                                                 # circle
        for (dx, dy) in ((1, 0), (2, 0), (3, 0), (0, 1), (4, 1), (0, 2), (4, 2), (0, 3), (4, 3), (1, 4), (2, 4), (3, 4)):
            c.set(x + dx, y + dy, col)
    elif kind == 2:                                                 # cross
        for j in range(5):
            c.set(x + j, y + j, col); c.set(x + 4 - j, y + j, col)
    else:                                                           # square
        c.fill(rect(x, y, 5, 1) | rect(x, y + 4, 5, 1) | rect(x, y, 1, 5) | rect(x + 4, y, 1, 5), col)


def dpad(c, cx, cy, arm=9, th=6, base=(50, 50, 60)):
    cross = rect(cx - arm, cy - th // 2, 2 * arm, th) | rect(cx - th // 2, cy - arm, th, 2 * arm)
    c.body(cross, base, mix(base, (255, 255, 255), 0.35), mix(base, (0, 0, 0), 0.5))
    c.fill(circle(cx, cy, 1.2), mix(base, (0, 0, 0), 0.4))


def ball(c, x, y, r, base):
    c.body(circle(x, y, r), base, mix(base, (255, 255, 255), 0.45), mix(base, (0, 0, 0), 0.4))
    c.set(x - 1, y - r + 1, (255, 255, 255))


def pill(c, x, y, w=7, base=(110, 110, 124)):
    c.body(rrect(x, y, w, 3, 1), base, mix(base, (255, 255, 255), 0.35), mix(base, (0, 0, 0), 0.4))


def pad_nes(c):
    """The Famicom/NES pad: a flat grey slab, a black panel, the cross, select and start, two red buttons."""
    body = rrect(11, 40, 66, 30, 3)
    c.body(body, (176, 176, 186), (226, 226, 234), (110, 110, 124), depth=2)
    panel = rrect(14, 43, 60, 24, 2)
    c.body(panel, (34, 30, 42), (70, 66, 82), (14, 10, 20), outline=None)
    dpad(c, 26, 55, 9, 6, (74, 72, 88))
    for sx in (40, 49):
        pill(c, sx, 57, 7, (120, 118, 134))
        c.fill(rect(sx + 1, 62, 5, 1), (220, 50, 60))               # the red label strokes
    ball(c, 59, 56, 4, (214, 40, 54))
    ball(c, 68, 56, 4, (214, 40, 54))
    c.fill(rect(14, 45, 20, 1) | rect(40, 45, 30, 1), (190, 40, 50))  # the red stripes on the panel


def pad_snes(c):
    """The Super Famicom/SNES pad: a rounded grey peanut, the cross, four coloured buttons, select and start."""
    pad = circle(24, 56, 14) | circle(64, 56, 14) | rect(24, 42, 40, 28)
    c.body(pad, (198, 198, 208), (238, 238, 246), (130, 130, 144), depth=2)
    c.body(circle(64, 56, 11), (120, 110, 150), (90, 80, 120), (160, 150, 190), outline=None)
    dpad(c, 24, 56, 9, 6, (50, 50, 60))
    for (bx, by, base) in ((64, 48, (110, 90, 220)), (72, 56, (60, 190, 90)), (56, 56, (240, 200, 50)), (64, 64, (220, 50, 60))):
        ball(c, bx, by, 3, base)
    for sx in (37, 46):
        pill(c, sx, 62, 7)
    for i, col in enumerate(((220, 50, 60), (240, 200, 50), (60, 190, 90), (110, 90, 220))):
        c.fill(rect(38 + i * 3, 49, 2, 2), col)


def pad_psx(c):
    """The PlayStation pad: grey body with two grips, the cross, four symbol buttons and two analog sticks."""
    pad = rrect(11, 41, 66, 22, 6) | circle(20, 60, 9) | circle(68, 60, 9)
    c.body(pad, (176, 178, 188), (226, 228, 236), (106, 108, 124), depth=2)
    dpad(c, 25, 52, 8, 5, (56, 58, 70))
    cols = ((60, 200, 140), (240, 80, 90), (100, 150, 240), (240, 130, 200))
    for k, (bx, by) in enumerate(((63, 46), (70, 52), (63, 58), (56, 52))):
        c.body(circle(bx, by, 3.4), (70, 72, 86), (104, 108, 124), (36, 38, 48))
        symbol(c, (0, 1, 2, 3)[k], bx - 2, by - 2, cols[k])
    for sx in (36, 52):
        c.body(circle(sx, 59, 4.4), (62, 64, 78), (110, 114, 130), (30, 32, 42))
        c.fill(circle(sx, 59, 1.6), (32, 34, 44))
    pill(c, 40, 47, 4, (120, 124, 140)); pill(c, 46, 47, 4, (120, 124, 140))


# --------------------------------------------------------------------------------------------- NES

def nes():
    """Red sunset, striped sun, castle and hills; the NES pad in front."""
    c = Canvas()
    c.dither_v(0, W, [(34, 12, 40), (110, 24, 60), (240, 110, 60)])
    c.stars(STARS[:8])
    for (px, py) in circle(44, 50, 22):
        if not (py > 40 and (py - 40) % 3 == 0):
            c.set(px, py, mix((255, 240, 130), (250, 90, 70), (py - 28) / 44))
    def ridge(base, peaks, col, cap=None):
        for px in range(W):
            top = base
            for (pxc, ph, pw) in peaks:
                top = min(top, base - ph + abs(px - pxc) * ph // pw)
            for py in range(max(top, 0), W):
                c.set(px, py, col)
            if cap and top < base - 3:
                for py in range(top, top + 2):
                    c.set(px, py, cap)
    ridge(64, [(8, 20, 14), (78, 24, 16), (60, 12, 14)], (70, 36, 96), (240, 220, 240))
    ridge(74, [(20, 8, 18), (66, 8, 18)], (30, 80, 90))
    for px in range(W):
        for py in range(78 + abs(px - 44) // 12, W):
            c.set(px, py, (40, 140, 70) if py < 80 + abs(px - 44) // 12 else (22, 92, 52))
    c.title("NES", 3, (255, 240, 240), (240, 70, 70), (110, 20, 30))
    pad_nes(c)
    return c


# -------------------------------------------------------------------------------------------- SNES

def snes():
    """Purple night, a sun and a synthwave grid floor; the SNES pad in front."""
    c = Canvas()
    c.dither_v(0, 58, [(16, 10, 40), (60, 24, 100), (150, 50, 140)])
    c.stars(STARS[:8])
    for (px, py) in circle(44, 40, 17):
        if not (py > 40 and (py - 40) % 3 == 0):
            c.set(px, py, mix((255, 220, 100), (250, 80, 140), (py - 23) / 34))
    c.dither_v(58, W, [(40, 10, 60), (20, 6, 36)])
    for gy in (59, 61, 64, 68, 73, 79, 86):
        c.fill(rect(0, gy, W, 1), (230, 60, 200))
    for k in range(-8, 9):
        for py in range(58, W):
            c.set(int(44 + k * 5 * (1 + (py - 58) / 6)), py, (230, 60, 200))
    c.title("SNES", 3, (255, 250, 220), (250, 190, 40), (120, 60, 20))
    pad_snes(c)
    return c


# --------------------------------------------------------------------------------------------- PSX

def psx():
    """A silver disc with iridescent bands over a dark field of face symbols; the PlayStation pad in front."""
    c = Canvas()
    c.dither_v(0, W, [(14, 14, 24), (30, 34, 70), (14, 14, 24)])
    cols = ((60, 200, 140), (240, 80, 90), (100, 150, 240), (240, 130, 200))
    for gy in range(0, W, 11):
        for gx in range(0, W, 11):
            k = (gx // 11 + gy // 11) % 4
            symbol(c, k, gx + 3 + (gy // 11 % 2) * 5 % 11, gy + 3, mix(cols[k], (14, 14, 24), 0.8))
    cx, cy, R = 44, 54, 31
    disc, hole, hub = circle(cx, cy, R), circle(cx, cy, 4), circle(cx, cy, 10)
    c.fill(outline_of(disc), INK)
    rainbow = [(250, 90, 110), (255, 170, 80), (250, 240, 120), (120, 230, 150), (100, 190, 250), (170, 130, 250)]
    for (x, y) in disc - hole:
        dx, dy = x - cx, y - cy
        r, a = math.hypot(dx, dy), math.atan2(dy, dx)
        if r <= 10.5:
            v = 150 if r > 9 else 210 if r > 6 else 180
            col = (v, v, v + 10)
        else:
            col = mix((236, 238, 246), (140, 144, 164), (1 - math.cos(a + math.pi * 0.75)) / 2)
            for centre in (-2.2, 0.95):
                d = abs((a - centre + math.pi) % (2 * math.pi) - math.pi)
                if d < 0.5:
                    col = mix(col, rainbow[min(5, int((r - 11) / (R - 11) * 6))], 0.75 if d < 0.3 else 0.4)
            if abs(r - R) < 1.2:
                col = mix(col, (255, 255, 255), 0.5)
        c.set(x, y, col)
    c.title("PSX", 3, (240, 244, 255), (130, 150, 210), (40, 40, 80))
    pad_psx(c)
    return c


COVERS = {"nes": nes, "snes": snes, "psx": psx}

if __name__ == "__main__":
    for name in sys.argv[1:] or COVERS:
        COVERS[name]().save(os.path.join(ROOT, "bm", name, "cover.png"))
        print("wrote bm/%s/cover.png" % name)
