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


# --------------------------------------------------------------------------------------------- NES

def nes():
    """A grey cartridge; its label shows a sunset, mountains and a castle on a hill."""
    c = Canvas()
    c.dither_v(0, W, [(30, 14, 40), (70, 20, 50), (120, 30, 50)])
    c.stars(STARS)
    c.title("NES", 3, (255, 240, 240), (240, 70, 70), (110, 20, 30))
    # the shell
    x, y, w, h = 17, 22, 54, 64
    shell = rrect(x, y, w, h, 3)
    c.body(shell, (150, 150, 160), (205, 205, 214), (96, 96, 108), depth=2)
    # the label: a recessed frame
    lx, ly, lw, lh = x + 6, y + 6, w - 12, 34
    c.fill(rect(lx - 1, ly - 1, lw + 2, lh + 2), (90, 90, 100))
    c.fill(rect(lx, ly, lw, lh), INK)
    sx, sy, sw, sh = lx + 1, ly + 1, lw - 2, lh - 2
    c.dither_v(sy, sy + 20, [(40, 30, 110), (190, 60, 120), (250, 150, 70)], sx, sx + sw)
    sun_x, sun_y = sx + sw // 2 + 6, sy + 16
    for (px, py) in circle(sun_x, sun_y, 7):
        if sy <= py < sy + 22 and sx <= px < sx + sw and not (py > sun_y and (py - sun_y) % 3 == 0):
            c.set(px, py, mix((255, 240, 120), (255, 120, 60), (py - sun_y + 7) / 14))
    def ridge(base, peaks, col, cap=None):
        for px in range(sx, sx + sw):
            top = base
            for (pxc, ph, pw) in peaks:
                top = min(top, base - ph + abs(px - pxc) * ph // pw)
            for py in range(max(top, sy), sy + sh):
                c.set(px, py, col)
            if cap and top < base - 3:
                for py in range(top, min(top + 2, sy + sh)):
                    c.set(px, py, cap)
    ridge(sy + 22, [(sx + 6, 10, 8), (sx + 18, 8, 7), (sx + 34, 11, 9)], (70, 50, 120), (220, 210, 240))
    ridge(sy + 26, [(sx + 10, 6, 10), (sx + 28, 5, 8)], (40, 90, 110))
    # the castle on the front hill
    hill = lambda px: sy + 25 + abs(px - (sx + 12)) // 4
    for px in range(sx, sx + sw):
        for py in range(hill(px), sy + sh):
            c.set(px, py, (40, 140, 70) if py - hill(px) < 2 else (24, 96, 52))
    cx0 = sx + 6
    for (bx, by, bw, bh) in ((cx0, sy + 17, 12, 8), (cx0 - 1, sy + 13, 4, 12), (cx0 + 9, sy + 13, 4, 12), (cx0 + 4, sy + 10, 4, 8)):
        for px in range(bx, bx + bw):
            for py in range(by, by + bh):
                c.set(px, py, (34, 24, 50))
        for px in range(bx, bx + bw, 2):
            c.set(px, by - 1, (34, 24, 50))
    for (wx, wy) in ((cx0 + 5, sy + 13), (cx0 + 1, sy + 16), (cx0 + 10, sy + 16), (cx0 + 5, sy + 20)):
        c.set(wx, wy, (255, 220, 90)); c.set(wx, wy + 1, (255, 180, 60))
    c.set(cx0 + 6, sy + 7, (240, 60, 60)); c.set(cx0 + 7, sy + 7, (240, 60, 60)); c.set(cx0 + 6, sy + 8, (60, 40, 60))
    # the grip ridges and the arrow
    for i in range(5):
        ry = ly + lh + 4 + i * 3
        c.fill(rect(x + 8, ry, w - 16, 1), (100, 100, 112))
        c.fill(rect(x + 8, ry + 1, w - 16, 1), (190, 190, 200))
    ax, ay = x + w // 2, y + h - 6
    for i in range(3):
        c.fill(rect(ax - 2 + i, ay - 2 + i, 5 - 2 * i, 1), (110, 110, 122))
    return c


# -------------------------------------------------------------------------------------------- SNES

def snes():
    """A grey pad with four coloured buttons over a purple night and a 90s grid floor."""
    c = Canvas()
    c.dither_v(0, 58, [(16, 10, 40), (60, 24, 100), (150, 50, 140)])
    c.stars(STARS[:8])
    # the sunset sun behind the grid
    for (px, py) in circle(44, 58, 16):
        if py < 58 and not (py > 47 and (py - 47) % 3 == 0):
            c.set(px, py, mix((255, 220, 100), (250, 80, 140), (py - 42) / 16))
    c.dither_v(58, W, [(40, 10, 60), (20, 6, 36)])
    for gy in (59, 61, 64, 68, 73, 79, 86):                       # the grid: rows closer near you
        c.fill(rect(0, gy, W, 1), (230, 60, 200))
    for k in range(-8, 9):
        for py in range(58, W):
            px = int(44 + k * 5 * (1 + (py - 58) / 6))
            c.set(px, py, (230, 60, 200))
    c.title("SNES", 3, (255, 250, 220), (250, 190, 40), (120, 60, 20))
    # the pad
    pad = circle(24, 54, 15) | circle(64, 54, 15) | rect(24, 40, 40, 29)
    c.body(pad, (196, 196, 206), (236, 236, 244), (130, 130, 144), depth=2)
    # the dark well under the buttons
    well = circle(64, 54, 12)
    c.body(well, (120, 110, 150), (90, 80, 120), (160, 150, 190), outline=None)
    # the cross
    cross = rect(15, 51, 18, 6) | rect(21, 45, 6, 18)
    c.body(cross, (50, 50, 60), (90, 90, 104), (24, 24, 30))
    c.fill(circle(24, 54, 1.2), (36, 36, 44))
    # the four buttons
    for (bx, by, base) in ((64, 46, (110, 90, 220)), (72, 54, (60, 190, 90)), (56, 54, (240, 200, 50)), (64, 62, (220, 50, 60))):
        b = circle(bx, by, 3.4)
        c.body(b, base, mix(base, (255, 255, 255), 0.45), mix(base, (0, 0, 0), 0.4))
        c.set(bx - 1, by - 2, (255, 255, 255))
    # select and start
    for sxp in (37, 46):
        pill = rrect(sxp, 61, 7, 3, 1)
        c.body(pill, (110, 110, 124), (150, 150, 164), (70, 70, 84))
    # the logo dots
    for i, col in enumerate(((220, 50, 60), (240, 200, 50), (60, 190, 90), (110, 90, 220))):
        c.fill(rect(38 + i * 3, 47, 2, 2), col)
    return c


# --------------------------------------------------------------------------------------------- PSX

def psx():
    """A silver compact disc with an iridescent shine, over a dark field of face symbols."""
    c = Canvas()
    c.dither_v(0, W, [(14, 14, 24), (30, 30, 52), (14, 14, 24)])
    cols = ((60, 200, 140), (240, 80, 90), (100, 150, 240), (240, 130, 200))
    def sym(kind, x, y, col):
        if kind == 0:
            for j in range(5):
                c.set(x + 2 - j // 2, y + j, col); c.set(x + 2 + j // 2, y + j, col)
            c.fill(rect(x, y + 4, 5, 1), col)
        elif kind == 1:
            for (dx, dy) in ((1, 0), (2, 0), (3, 0), (0, 1), (4, 1), (0, 2), (4, 2), (0, 3), (4, 3), (1, 4), (2, 4), (3, 4)):
                c.set(x + dx, y + dy, col)
        elif kind == 2:
            for j in range(5):
                c.set(x + j, y + j, col); c.set(x + 4 - j, y + j, col)
        else:
            c.fill(rect(x, y, 5, 1) | rect(x, y + 4, 5, 1) | rect(x, y, 1, 5) | rect(x + 4, y, 1, 5), col)
    for gy in range(0, W, 11):                                 # the faint pattern
        for gx in range(0, W, 11):
            k = (gx // 11 + gy // 11) % 4
            sym(k, gx + 3 + (gy // 11 % 2) * 5 % 11, gy + 3, mix(cols[k], (14, 14, 24), 0.78))
    # the disc
    cx, cy, R = 44, 52, 31
    disc = circle(cx, cy, R)
    hole = circle(cx, cy, 4)
    hub = circle(cx, cy, 10)
    c.fill(outline_of(disc), INK)
    rainbow = [(250, 90, 110), (255, 170, 80), (250, 240, 120), (120, 230, 150), (100, 190, 250), (170, 130, 250)]
    for (x, y) in disc - hole:
        dx, dy = x - cx, y - cy
        r = math.hypot(dx, dy)
        a = math.atan2(dy, dx)
        if r <= 10.5:
            v = 150 if r > 9 else 210 if r > 6 else 180
            col = (v, v, v + 10)
        else:
            # silver from a light top-left to a darker bottom-right
            t = (1 - math.cos(a + math.pi * 0.75)) / 2
            col = mix((236, 238, 246), (140, 144, 164), t)
            # two iridescent bands, opposite each other
            for centre in (-2.2, 0.95):
                d = abs((a - centre + math.pi) % (2 * math.pi) - math.pi)
                if d < 0.5:
                    band = rainbow[min(5, int((r - 11) / (R - 11) * 6))]
                    col = mix(col, band, 0.75 if d < 0.3 else 0.4)
            if abs(r - R) < 1.2:
                col = mix(col, (255, 255, 255), 0.5)
        c.set(x, y, col)
    c.fill(outline_of(hole) & disc, (90, 90, 104))
    c.fill(outline_of(hub) & disc - hub, (110, 110, 126))
    for (x, y) in ((cx - 18, cy - 20), (cx - 17, cy - 21), (cx - 19, cy - 19), (cx - 21, cy - 14), (cx - 22, cy - 12)):
        c.set(x, y, (255, 255, 255))                           # the glint
    for (x, y) in ((cx - 19, cy - 21), (cx - 18, cy - 22), (cx - 20, cy - 20)):
        c.set(x, y, (255, 255, 255))
    c.title("PSX", 3, (240, 244, 255), (130, 150, 210), (40, 40, 80))
    return c


COVERS = {"nes": nes, "snes": snes, "psx": psx}

if __name__ == "__main__":
    for name in sys.argv[1:] or COVERS:
        COVERS[name]().save(os.path.join(ROOT, "bm", name, "cover.png"))
        print("wrote bm/%s/cover.png" % name)
