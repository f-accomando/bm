#!/usr/bin/env python3
"""Chaos Kitchen: draws the sprite sheet (sheet.png, 512x256) and the menu
cover (cover.png, 128x80) in code, reproducibly. Standard library only.

    python3 carts/kitchen/mkassets.py

Sheet layout (the numbers used by src/41_hud.lua):
  16x16 icons, 32 per row: ingredients 0-22 (the order of 10_ingredients.lua),
  the same chopped 32-54, symbols from 64 (see SYMBOLS below)
  8x8 badges at y = 64: chop boil fry bake blend tick cross fire
  32x32 chef portraits at y = 80: Basil, Bun, Noodle, Pepper
  32x32 kitchen textures at x = 128 + 32 * i, y = 80 (i: KITCHEN_TEX below,
  the order of Mesh.TEX in src/21_kitchen_mesh.lua)
  128x128 texture of each chef's 3D model at y = 128, side by side: copied
  from models/chefs.png (made by import_chefs.py)
The ingredient colours are read from src/10_ingredients.lua.
"""
import math
import os
import re
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
SW, SH = 512, 256


def rgb(c, a=255):
    return (c >> 16 & 255, c >> 8 & 255, c & 255, a)


def shade(c, k):
    r, g, b = c >> 16 & 255, c >> 8 & 255, c & 255
    return (min(255, int(r * k)) << 16) | (min(255, int(g * k)) << 8) | min(255, int(b * k))


def mix(c1, c2, t):
    a, b = rgb(c1), rgb(c2)
    return (int(a[0] + (b[0] - a[0]) * t) << 16) | (int(a[1] + (b[1] - a[1]) * t) << 8) | int(a[2] + (b[2] - a[2]) * t)


def load_font():
    src = open(os.path.join(ROOT, "src", "gfx", "font8x16.c")).read()
    rows = re.findall(r"\{ (0x[0-9a-f]{2}(?:, 0x[0-9a-f]{2}){15}) \}", src)
    return [bytes(int(b, 16) for b in r.split(", ")) for r in rows]


FONT = load_font()


class Img:
    def __init__(self, w, h, bg=None):
        self.w, self.h = w, h
        self.px = [(0, 0, 0, 0) if bg is None else rgb(bg)] * (w * h)
        self.ox = self.oy = 0           # origin of the current cell
        self.cw = self.ch = 0           # its size (clip)

    def cell(self, x, y, w, h):
        self.ox, self.oy, self.cw, self.ch = x, y, w, h

    def set(self, x, y, c):
        x, y = int(x), int(y)
        if self.cw and not (0 <= x < self.cw and 0 <= y < self.ch):
            return
        x, y = x + self.ox, y + self.oy
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y * self.w + x] = rgb(c) if isinstance(c, int) else c

    def get(self, x, y):
        x, y = x + self.ox, y + self.oy
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.px[y * self.w + x]
        return (0, 0, 0, 0)

    def rect(self, x, y, w, h, c):
        for yy in range(int(y), int(y + h)):
            for xx in range(int(x), int(x + w)):
                self.set(xx, yy, c)

    def ellipse(self, cx, cy, rx, ry, c):
        for yy in range(int(cy - ry - 1), int(cy + ry + 2)):
            for xx in range(int(cx - rx - 1), int(cx + rx + 2)):
                dx, dy = (xx + 0.5 - cx) / rx, (yy + 0.5 - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    self.set(xx, yy, c)

    def circle(self, cx, cy, r, c):
        self.ellipse(cx, cy, r, r, c)

    def poly(self, pts, c):
        ys = [p[1] for p in pts]
        for yy in range(int(min(ys)), int(max(ys)) + 1):
            y = yy + 0.5
            xs = []
            n = len(pts)
            for i in range(n):
                x0, y0 = pts[i]
                x1, y1 = pts[(i + 1) % n]
                if (y0 <= y < y1) or (y1 <= y < y0):
                    xs.append(x0 + (y - y0) * (x1 - x0) / (y1 - y0))
            xs.sort()
            for i in range(0, len(xs) - 1, 2):
                for xx in range(int(math.ceil(xs[i] - 0.5)), int(math.ceil(xs[i + 1] - 0.5))):
                    self.set(xx, yy, c)

    def line(self, x0, y0, x1, y1, c, w=1):
        n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
        for i in range(n + 1):
            t = i / n
            x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
            if w == 1:
                self.set(round(x), round(y), c)
            else:
                self.circle(x, y, w / 2, c)

    def text(self, s, x, y, c, scale=1):
        for i, ch in enumerate(s):
            g = FONT[ord(ch)]
            for r in range(16):
                bits = g[r]
                for b in range(8):
                    if bits & (0x80 >> b):
                        self.rect(x + (i * 8 + b) * scale, y + r * scale, scale, scale, c)

    def outline(self, c=0x1A1418):
        """a 1-pixel dark edge around everything opaque in the current cell"""
        edge = []
        for y in range(self.ch):
            for x in range(self.cw):
                if self.get(x, y)[3]:
                    continue
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < self.cw and 0 <= yy < self.ch and self.get(xx, yy)[3] and self.get(xx, yy) != rgb(c):
                        edge.append((x, y))
                        break
        for x, y in edge:
            self.set(x, y, c)


def write_png(path, w, h, px, alpha=True):
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            p = px[y * w + x]
            raw.extend(p if alpha else p[:3])

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6 if alpha else 2, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def ingredients():
    src = open(os.path.join(HERE, "src", "10_ingredients.lua")).read()
    out = []
    for m in re.finditer(r'\{ id = "(\w+)",.*?c1 = 0x([0-9A-Fa-f]{6}), c2 = 0x([0-9A-Fa-f]{6}), shape = "(\w+)"', src):
        out.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)))
    return out


S = Img(SW, SH)


def icon_cell(n):
    S.cell((n % 32) * 16, (n // 32) * 16, 16, 16)


# ---------------------------------------------------------------- ingredients

def hl(x, y):
    S.set(x, y, 0xFFFFFF)


def draw_raw(ing, c1, c2):
    if ing == "tomato":
        S.circle(8, 9.5, 6, c1)
        S.circle(6.5, 8, 1.5, shade(c1, 1.25))
        for x, y in ((6, 3), (7, 4), (8, 3), (9, 4), (10, 3)):
            S.set(x, y, c2)
        hl(5, 7)
    elif ing == "onion":
        S.poly([(8, 1.5), (13.5, 9), (12, 14), (4, 14), (2.5, 9)], c1)
        S.ellipse(8, 10, 5.5, 4.5, c1)
        S.line(8, 4, 8, 13, shade(c1, 0.8))
        S.line(5.5, 7, 5, 13, shade(c1, 0.85))
        S.line(10.5, 7, 11, 13, shade(c1, 0.85))
        S.set(8, 1, 0x80A040)
    elif ing == "potato":
        S.ellipse(8, 9, 7, 5, c1)
        for x, y in ((5, 7), (10, 9), (7, 11), (11, 6)):
            S.set(x, y, shade(c1, 0.7))
        hl(5, 6)
    elif ing == "carrot":
        S.poly([(3, 13), (6, 6), (11, 3.5), (12.5, 5), (10, 10)], c1)
        for x, y in ((11, 2), (12, 1), (13, 3), (14, 2), (12, 3)):
            S.set(x, y, c2)
        S.line(6, 9, 8, 8, shade(c1, 0.8))
        S.line(8, 6, 10, 5, shade(c1, 0.8))
    elif ing == "mushroom":
        S.ellipse(8, 7.5, 7, 4.5, c1)
        S.rect(1, 8, 14, 3, (0, 0, 0, 0))
        S.rect(6, 8, 4, 6, c2)
        S.ellipse(8, 7.5, 6.5, 3.5, c1)
        S.rect(2, 8, 12, 1, shade(c1, 0.8))
        hl(5, 5)
    elif ing == "lettuce":
        S.circle(8, 9, 6.5, c1)
        S.circle(8, 9, 4, c2)
        S.line(8, 5, 8, 13, c1)
        S.line(5, 8, 11, 11, shade(c2, 0.85))
    elif ing == "cucumber":
        S.line(3, 12, 12, 3, c1, 5)
        for x, y in ((5, 9), (8, 7), (10, 5), (7, 10)):
            S.set(x, y, c2)
    elif ing == "cheese":
        S.poly([(2, 12), (14, 12), (14, 7), (4, 5)], c1)
        S.poly([(4, 5), (14, 7), (13, 5)], c2)
        S.circle(7, 9, 1.2, shade(c1, 0.8))
        S.circle(11, 10, 1, shade(c1, 0.8))
    elif ing == "bread":
        S.ellipse(8, 8, 7, 5, c1)
        S.rect(1, 8, 14, 5, c1)
        S.line(4, 6, 6, 8, c2)
        S.line(7, 5, 9, 7, c2)
        S.line(10, 5, 12, 7, c2)
    elif ing == "rice":
        S.poly([(2, 8), (14, 8), (12, 14), (4, 14)], 0x4A7AC8)
        S.ellipse(8, 8, 6, 3, c1)
        for x, y in ((6, 6), (9, 6), (7, 7), (10, 7)):
            S.set(x, y, 0xD8D0C0)
    elif ing == "pasta":
        for i in range(5):
            S.line(3 + i * 2, 14, 7 + i * 1.2, 2, c1)
        S.rect(3, 7, 10, 2, c2)
    elif ing == "egg":
        S.ellipse(8, 9, 5, 6, c1)
        hl(6, 6)
        S.set(6, 7, 0xFFFFFF)
    elif ing == "flour":
        S.poly([(3, 5), (13, 5), (14, 14), (2, 14)], c1)
        S.rect(5, 2, 6, 3, c2)
        S.rect(6, 8, 4, 1, c2)              # an F on the sack
        S.rect(6, 8, 1, 4, c2)
        S.rect(6, 10, 3, 1, c2)
    elif ing == "milk":
        S.rect(5, 5, 6, 10, c1)
        S.poly([(5, 5), (11, 5), (9, 2), (7, 2)], c1)
        S.rect(7, 1, 2, 2, c2)
        S.rect(5, 8, 6, 4, c2)
    elif ing == "butter":
        S.poly([(2, 9), (10, 12), (14, 9), (6, 6)], c2)
        S.poly([(2, 9), (10, 12), (10, 14), (2, 11)], c1)
        S.poly([(10, 12), (14, 9), (14, 11), (10, 14)], shade(c1, 0.85))
    elif ing == "chicken":
        S.ellipse(9, 7, 5, 4.5, c1)
        S.line(6, 10, 3, 13, 0xF8F0E8, 2)
        S.circle(2.5, 13.5, 1.3, 0xF8F0E8)
        hl(8, 5)
    elif ing == "beef":
        S.ellipse(8, 9, 7, 5, c1)
        S.line(3, 8, 12, 10, c2)
        S.circle(11, 8, 1.5, c2)
    elif ing == "fish":
        S.ellipse(7, 8, 6, 3.5, c1)
        S.poly([(12, 8), (15, 4), (15, 12)], c1)
        S.ellipse(7, 9, 5, 1.5, c2)
        S.set(4, 7, 0x101010)
    elif ing == "shrimp":
        for i in range(8):
            a = math.pi * (0.1 + i / 7 * 1.3)
            S.circle(8 + math.cos(a) * 4.5, 9 - math.sin(a) * 4.5, 2 - i * 0.12, c1 if i % 2 else c2)
    elif ing == "sausage":
        S.line(3, 11, 13, 5, c1, 5)
        S.line(5, 9, 10, 6, c2)
    elif ing == "herbs":
        S.line(8, 15, 8, 5, 0x207020)
        for x, y, r in ((5, 6, 2.5), (11, 6, 2.5), (8, 3, 2.5), (5, 11, 2), (11, 11, 2)):
            S.circle(x, y, r, c1)
        S.set(8, 3, c2)
    elif ing == "fruit":
        S.circle(8, 9.5, 6, c1)
        S.ellipse(10, 3.5, 2.5, 1.5, c2)
        S.line(8, 3, 8, 5, 0x6A4A20)
        hl(5, 7)
    elif ing == "chocolate":
        S.rect(3, 3, 10, 11, c1)
        for x in (3, 8):
            for y in (3, 7, 11):
                S.rect(x + 1, y + 1, 3, 2, c2)
    S.outline()


def draw_chopped(ing, c1, c2, look):
    if look in ("slices", "rings"):
        for cx, cy in ((5, 6), (10, 8), (6, 11)):
            S.circle(cx, cy, 3.3, c1)
            S.circle(cx, cy, 2.2, c2)
            if look == "rings":
                S.circle(cx, cy, 1, c1)
    elif look == "leaves":
        S.poly([(2, 6), (8, 3), (7, 9)], c1)
        S.poly([(8, 8), (14, 6), (12, 13)], c1)
        S.poly([(3, 10), (8, 11), (5, 14)], c2)
    elif look == "patty":
        S.ellipse(8, 9, 6.5, 4, c1)
        S.ellipse(8, 8, 5, 2.5, c2)
    elif look == "fillet":
        S.poly([(2, 7), (13, 5), (14, 10), (3, 12)], c2)
        S.line(3, 9, 13, 7, c1)
    elif look == "dough":
        S.ellipse(8, 10, 6.5, 4.5, c1)
        S.set(6, 8, 0xFFFFFF)
        S.set(10, 9, 0xFFFFFF)
    else:
        for x, y in ((2, 4), (9, 3), (5, 9), (10, 10)):
            S.rect(x, y, 4, 4, c2)
            S.rect(x, y, 4, 1, c1)
            S.rect(x + 3, y, 1, 4, shade(c2, 0.8))
    S.outline()


CHOP_LOOK = {}


def load_chop_look():
    src = open(os.path.join(HERE, "src", "10_ingredients.lua")).read()
    block = src[src.index("Data.CHOP_LOOK"):]
    for m in re.finditer(r'(\w+) = "(\w+)"', block):
        CHOP_LOOK[m.group(1)] = m.group(2)


# ---------------------------------------------------------------- symbols

SYMBOLS = [
    "knife", "pot", "pan", "oven", "blender", "plate", "dplate", "bowl", "burnt", "ext", "coin", "star",
    "star0", "heart", "heart0", "clock", "fire", "drop", "rat", "duck", "ghost", "tornado", "steam", "lock",
    "check", "cross", "arrow", "bA", "bB", "bX", "bY", "register",
    "up", "snow", "wrench", "heart2", "trophy", "skull", "bell", "speed", "shield", "box", "knife2", "book",
]


def symbol(name):
    if name == "knife" or name == "knife2":
        S.poly([(2, 12), (11, 3), (13, 5), (4, 14)], 0xD8E0E8)
        S.line(10, 6, 14, 2, 0x7A4A2A, 3)
    elif name == "pot":
        S.rect(3, 6, 10, 8, 0xA0A8B0)
        S.rect(1, 7, 2, 2, 0x505860)
        S.rect(13, 7, 2, 2, 0x505860)
        S.rect(3, 5, 10, 2, 0x707880)
        for x in (5, 8, 11):
            S.line(x, 1, x, 3, 0xE0E0E0)
    elif name == "pan":
        S.ellipse(7, 10, 6, 3, 0x3A3A40)
        S.ellipse(7, 9.5, 4.5, 2, 0x26262A)
        S.line(12, 9, 15, 7, 0x6A3A2A, 2)
    elif name == "oven":
        S.rect(2, 3, 12, 11, 0x8A4A3A)
        S.rect(4, 6, 8, 6, 0x2A2420)
        S.rect(5, 8, 6, 3, 0xF08030)
    elif name == "blender":
        S.poly([(4, 2), (12, 2), (11, 10), (5, 10)], 0xB8E0F0)
        S.rect(4, 10, 8, 4, 0xD04050)
    elif name == "plate":
        S.ellipse(8, 9, 7, 4, 0xF4F4F8)
        S.ellipse(8, 9, 4.5, 2.5, 0xE0E0E8)
    elif name == "dplate":
        S.ellipse(8, 9, 7, 4, 0xB0A890)
        S.circle(6, 9, 1.5, 0x7A6A40)
        S.circle(10, 8, 1, 0x7A6A40)
    elif name == "bowl":
        S.poly([(2, 7), (14, 7), (11, 13), (5, 13)], 0xF0F0F8)
        S.ellipse(8, 7, 6, 2, 0xE08040)
    elif name == "burnt":
        S.ellipse(8, 11, 6, 3.5, 0x201A18)
        S.circle(6, 5, 2, 0x606060)
        S.circle(9, 3, 1.5, 0x808080)
    elif name == "ext":
        S.rect(6, 4, 5, 11, 0xE02020)
        S.rect(7, 2, 3, 2, 0x303030)
        S.line(10, 3, 14, 5, 0x303030)
    elif name == "coin":
        S.circle(8, 8, 7, 0xF0B020)
        S.circle(8, 8, 5, 0xFFD850)
        S.rect(7, 4, 2, 8, 0xC08010)
    elif name in ("star", "star0"):
        pts = []
        for i in range(10):
            a = -math.pi / 2 + i * math.pi / 5
            r = 7 if i % 2 == 0 else 3
            pts.append((8 + math.cos(a) * r, 8.5 + math.sin(a) * r))
        S.poly(pts, 0xFFD030 if name == "star" else 0x5A5468)
    elif name in ("heart", "heart0", "heart2"):
        c = 0xF03050 if name != "heart0" else 0x5A5468
        S.circle(5, 6, 3.5, c)
        S.circle(11, 6, 3.5, c)
        S.poly([(1.6, 7), (14.4, 7), (8, 14)], c)
    elif name == "clock":
        S.circle(8, 8, 7, 0xF0F0F0)
        S.line(8, 8, 8, 3, 0x202020)
        S.line(8, 8, 11, 9, 0x202020)
    elif name == "fire":
        S.poly([(8, 1), (13, 8), (12, 14), (4, 14), (3, 8), (6, 6)], 0xFF6010)
        S.poly([(8, 6), (11, 11), (10, 14), (6, 14), (5, 11)], 0xFFD040)
    elif name == "drop":
        S.poly([(8, 1), (13, 10), (3, 10)], 0x40A0F0)
        S.circle(8, 10, 5, 0x40A0F0)
        hl(6, 9)
    elif name == "rat":
        S.ellipse(8, 10, 6, 3.5, 0x807870)
        S.circle(3, 8, 2.5, 0x807870)
        S.circle(4, 5.5, 1.5, 0xE0A0A0)
        S.line(13, 11, 15, 8, 0xE0A0A0)
        S.set(2, 8, 0x101010)
    elif name == "duck":
        S.ellipse(9, 10, 6, 4, 0xF8F0D0)
        S.circle(5, 5, 3, 0xF8F0D0)
        S.poly([(2, 5), (0, 6), (2, 7)], 0xF08020)
        S.set(4, 4, 0x101010)
    elif name == "ghost":
        S.circle(8, 7, 6, 0xF0F0FF)
        S.rect(2, 7, 12, 6, 0xF0F0FF)
        for x in (2, 6, 10):
            S.poly([(x, 13), (x + 4, 13), (x + 2, 15)], 0xF0F0FF)
        S.rect(5, 6, 2, 3, 0x202040)
        S.rect(9, 6, 2, 3, 0x202040)
    elif name == "tornado":
        for i in range(6):
            S.ellipse(8 + (i % 2), 3 + i * 2.2, 7 - i, 1.4, 0xA0A8B8)
    elif name == "steam":
        for x in (4, 8, 12):
            for y in range(2, 14):
                S.set(x + round(math.sin(y * 0.8) * 1.5), y, 0xE0E8F0)
    elif name == "lock":
        S.rect(3, 7, 10, 8, 0xE0B040)
        S.rect(5, 2, 6, 6, (0, 0, 0, 0))
        for y in range(2, 8):
            S.set(5, y, 0xB0B0B0)
            S.set(10, y, 0xB0B0B0)
        S.rect(5, 2, 6, 1, 0xB0B0B0)
        S.rect(7, 10, 2, 3, 0x604010)
    elif name == "check":
        S.line(2, 8, 6, 12, 0x30D050, 3)
        S.line(6, 12, 14, 3, 0x30D050, 3)
    elif name == "cross":
        S.line(3, 3, 13, 13, 0xF04040, 3)
        S.line(13, 3, 3, 13, 0xF04040, 3)
    elif name == "arrow":
        S.poly([(8, 2), (14, 9), (10, 9), (10, 14), (6, 14), (6, 9), (2, 9)], 0xFFFFFF)
    elif name in ("bA", "bB", "bX", "bY"):
        col = {"bA": 0x40C050, "bB": 0xE04040, "bX": 0x4080E0, "bY": 0xE0C030}[name]
        S.circle(8, 8, 7.5, col)
        S.text(name[1], 4, 0, 0xFFFFFF)
    elif name == "register":
        S.rect(2, 6, 12, 8, 0xD8A030)
        S.rect(4, 2, 8, 4, 0xB08020)
        S.rect(5, 3, 6, 2, 0x30C060)
    elif name == "up":
        S.poly([(8, 1), (15, 8), (11, 8), (11, 15), (5, 15), (5, 8), (1, 8)], 0x40E070)
    elif name == "snow":
        for a in range(3):
            ang = a * math.pi / 3
            S.line(8 - math.cos(ang) * 6, 8 - math.sin(ang) * 6, 8 + math.cos(ang) * 6, 8 + math.sin(ang) * 6, 0xD8F0FF)
    elif name == "wrench":
        S.line(3, 13, 11, 5, 0xA0A8B0, 3)
        S.circle(11.5, 4.5, 3, 0xA0A8B0)
        S.circle(12.5, 3.5, 1.2, (0, 0, 0, 0))
    elif name == "trophy":
        S.poly([(3, 2), (13, 2), (11, 9), (5, 9)], 0xF0C030)
        S.rect(7, 9, 2, 3, 0xF0C030)
        S.rect(4, 12, 8, 3, 0xB08020)
    elif name == "skull":
        S.circle(8, 7, 6, 0xF0F0E8)
        S.rect(5, 11, 6, 4, 0xF0F0E8)
        S.circle(5.5, 7, 1.5, 0x202020)
        S.circle(10.5, 7, 1.5, 0x202020)
    elif name == "bell":
        S.poly([(8, 2), (12, 6), (13, 12), (3, 12), (4, 6)], 0xF0C030)
        S.rect(2, 12, 12, 2, 0xD0A020)
    elif name == "speed":
        S.poly([(9, 1), (4, 9), (8, 9), (6, 15), (12, 6), (8, 6)], 0xFFE040)
    elif name == "shield":
        S.poly([(2, 2), (14, 2), (14, 8), (8, 15), (2, 8)], 0x4080E0)
        S.poly([(5, 4), (11, 4), (11, 8), (8, 12), (5, 8)], 0x80B0F0)
    elif name == "box":
        S.rect(2, 4, 12, 10, 0xC08A50)
        S.rect(2, 4, 12, 2, 0x9A6A3A)
        S.rect(7, 4, 2, 10, 0x9A6A3A)
    elif name == "book":
        S.rect(2, 2, 12, 12, 0xC04040)
        S.rect(4, 3, 9, 10, 0xF8F0E0)
        S.rect(2, 2, 2, 12, 0x802020)
    S.outline()


MINIS = ["chop", "boil", "fry", "bake", "blend", "tick", "cross", "fire"]


def mini(i, name):
    S.cell(i * 8, 64, 8, 8)
    S.rect(0, 0, 8, 8, 0x1A1418)
    if name == "chop":
        S.line(1, 6, 5, 2, 0xE0E8F0)
        S.line(5, 2, 7, 1, 0xA06030)
    elif name == "boil":
        S.rect(1, 3, 6, 4, 0xA0A8B0)
        S.rect(2, 1, 1, 2, 0xE0E0E0)
        S.rect(5, 1, 1, 2, 0xE0E0E0)
    elif name == "fry":
        S.rect(1, 4, 5, 2, 0x707078)
        S.rect(6, 3, 2, 1, 0xA06030)
        S.rect(2, 2, 1, 1, 0xFF8030)
        S.rect(4, 1, 1, 2, 0xFF8030)
    elif name == "bake":
        S.rect(1, 1, 6, 6, 0xB05A40)
        S.rect(2, 3, 4, 3, 0xF08030)
    elif name == "blend":
        S.rect(2, 1, 4, 4, 0xB8E0F0)
        S.rect(2, 5, 4, 2, 0xD04050)
    elif name == "tick":
        S.rect(0, 0, 8, 8, 0x208030)
        S.line(1, 4, 3, 6, 0xFFFFFF)
        S.line(3, 6, 6, 1, 0xFFFFFF)
    elif name == "cross":
        S.rect(0, 0, 8, 8, 0xA02020)
        S.line(1, 1, 6, 6, 0xFFFFFF)
        S.line(6, 1, 1, 6, 0xFFFFFF)
    elif name == "fire":
        S.poly([(4, 0), (7, 5), (6, 8), (2, 8), (1, 5)], 0xFF6010)
        S.rect(3, 5, 2, 2, 0xFFD040)


# ---------------------------------------------------------------- portraits

SKIN = 0xF4C8A0


def portrait(i):
    S.cell(i * 32, 80, 32, 32)
    bg = [0xE8605A, 0xF0B040, 0x50C080, 0x5A8AF0][i]
    S.rect(0, 0, 32, 32, bg)
    S.rect(0, 0, 32, 1, 0xFFFFFF)
    if i == 0:      # Basil: tall toque, determined
        S.circle(16, 20, 8, SKIN)
        S.rect(10, 1, 12, 9, 0xFFFFFF)
        S.ellipse(16, 4, 9, 4, 0xFFFFFF)
        S.rect(9, 10, 14, 2, 0xF0F0F0)
        S.rect(12, 18, 2, 2, 0x1C1C24)
        S.rect(18, 18, 2, 2, 0x1C1C24)
        S.line(11, 16, 14, 17, 0x4A2A18)
        S.line(21, 16, 18, 17, 0x4A2A18)
        S.rect(14, 24, 4, 1, 0x8A3A30)
        S.rect(10, 29, 12, 3, 0xE03A30)
    elif i == 1:    # Bun: round, rosy, beret
        S.circle(16, 20, 10, SKIN)
        S.ellipse(15, 8, 9, 3, 0xE04830)
        S.rect(15, 4, 2, 2, 0xE04830)
        S.line(10, 18, 13, 18, 0x1C1C24)
        S.line(19, 18, 22, 18, 0x1C1C24)
        S.circle(9, 22, 2, 0xF08080)
        S.circle(23, 22, 2, 0xF08080)
        S.ellipse(16, 25, 4, 2, 0x8A2A20)
    elif i == 2:    # Noodle: glasses, headband, ponytail
        S.ellipse(16, 18, 7, 9, SKIN)
        S.rect(8, 9, 16, 3, 0x30B060)
        S.rect(7, 4, 18, 5, 0x2A1A10)
        S.rect(24, 6, 5, 3, 0x2A1A10)
        S.rect(10, 16, 5, 4, 0x80D0FF)
        S.rect(17, 16, 5, 4, 0x80D0FF)
        S.rect(15, 17, 2, 1, 0x1C1C24)
        S.rect(12, 17, 2, 2, 0x1C1C24)
        S.rect(19, 17, 2, 2, 0x1C1C24)
        S.rect(14, 24, 4, 1, 0x8A3A30)
    else:           # Pepper: moustache, flat cap, stern
        S.circle(16, 19, 9, SKIN)
        S.rect(7, 6, 18, 5, 0x3A70E0)
        S.rect(10, 10, 16, 2, 0x2A50B0)
        S.rect(11, 16, 4, 2, 0x1C1C24)
        S.rect(18, 16, 4, 2, 0x1C1C24)
        S.rect(10, 14, 6, 2, 0x2A2020)
        S.rect(17, 14, 6, 2, 0x2A2020)
        S.rect(9, 22, 14, 3, 0x241810)
    S.outline(0x1A1418)


# ---------------------------------------------------------------- kitchen textures

# 32x32 each, in the game's cartoon style: flat colours, no noise, a dark
# outline around every shape and one light edge on top
INK = 0x3A2418                          # outlines (warm dark brown)


def box_ink(x, y, w, h, fill, light=None, ink=INK):
    """a flat rectangle with a 1-pixel outline and a light top edge"""
    S.rect(x, y, w, h, ink)
    S.rect(x + 1, y + 1, w - 2, h - 2, fill)
    if light is not None:
        S.rect(x + 1, y + 1, w - 2, 1, light)


def tex_wood_top():
    """butcher block: three wide planks, flat, outlined only at the edge"""
    S.rect(0, 0, 32, 32, INK)
    cols = [0xF2C478, 0xE8B468, 0xF2C478]
    for i, y in enumerate((1, 11, 21)):
        S.rect(1, y, 30, 10 if i < 2 else 10, cols[i])
        S.rect(1, y, 30, 1, 0xFFE0A8)                   # light edge of each plank
        if i:
            S.rect(1, y - 1, 30, 1, 0xC88A48)           # soft joint
    S.rect(8, 5, 6, 1, 0xD89C50)                        # two knots, simple dashes
    S.rect(19, 26, 5, 1, 0xD89C50)


def tex_cabinet():
    """the front of a counter: a door with a round-cornered panel and a knob"""
    S.rect(0, 0, 32, 32, 0xC8925A)
    S.rect(0, 0, 32, 3, INK)                            # the shadow under the top
    box_ink(2, 4, 28, 27, 0xE0AA6C, 0xF6CC90)          # the door
    box_ink(7, 10, 18, 15, 0xD49A5C)                    # the panel
    S.circle(16, 7, 2, INK)                             # the knob
    S.circle(16, 7, 1.2, 0xF8E8C8)


def tex_stove_top():
    """a cartoon cooker: grey plate, a black ring burner, a blue flame hint"""
    box_ink(0, 0, 32, 32, 0x707A88, 0x98A4B4)
    S.circle(16, 16, 12, INK)
    S.circle(16, 16, 11, 0x2A2C34)
    S.circle(16, 16, 7, 0x4A4E5A)
    S.circle(16, 16, 4, INK)
    S.circle(16, 16, 3, 0x48A0F0)                       # the pilot light
    for a in range(4):                                  # the grate's arms
        ang = a * math.pi / 2 + math.pi / 4
        S.line(16 + math.cos(ang) * 5, 16 + math.sin(ang) * 5,
               16 + math.cos(ang) * 12, 16 + math.sin(ang) * 12, INK, 2)


def tex_stove_front():
    """the cooker's front: two big round knobs"""
    box_ink(0, 0, 32, 32, 0x5A6272, 0x7A8494)
    for kx in (9, 23):
        S.circle(kx, 12, 5, INK)
        S.circle(kx, 12, 4, 0xF0F0F0)
        S.rect(kx - 1, 8, 2, 4, INK)                   # the pointer
    box_ink(5, 21, 22, 6, 0x3A404C)                     # a vent


def tex_oven():
    """a red oven door with a round glowing window and a white handle"""
    box_ink(0, 0, 32, 32, 0xD8503C, 0xF07860)
    box_ink(4, 5, 24, 4, 0xF8F4EC)                      # the handle
    S.ellipse(16, 19, 11, 9, INK)                        # the window
    S.ellipse(16, 19, 10, 8, 0xFFA030)
    S.ellipse(16, 21, 7, 5, 0xFFD060)                   # the warm glow
    S.rect(10, 14, 5, 1, 0xFFF0C0)                      # a shine on the glass


def tex_crate():
    """a crate: two flat slats and a cross brace, thick outlines"""
    S.rect(0, 0, 32, 32, INK)
    S.rect(1, 1, 30, 30, 0xE09A50)
    box_ink(0, 0, 32, 5, 0xC87C38, 0xF0B070)            # top rail
    box_ink(0, 27, 32, 5, 0xC87C38, 0xF0B070)           # bottom rail
    S.line(3, 24, 28, 7, INK, 3)                        # the brace
    S.line(3, 24, 28, 7, 0xC87C38, 1)
    for x, y in ((3, 2), (28, 2), (3, 29), (28, 29)):  # nail heads
        S.set(x, y, INK)


def tex_sink():
    """a light steel top with a rounded blue basin"""
    box_ink(0, 0, 32, 32, 0xC8D4E0, 0xF0F6FC)
    S.rect(5, 6, 22, 21, INK)                           # the basin
    S.rect(6, 7, 20, 19, 0x6AB0E8)                      # water-blue floor
    S.rect(6, 7, 20, 3, 0x4A88C8)                       # its back wall in shade
    S.circle(16, 19, 2, INK)                            # the drain
    S.rect(8, 12, 4, 1, 0xC8E8FF)                       # a shine


def tex_steel():
    """the pass: plain light steel with two shine stripes"""
    box_ink(0, 0, 32, 32, 0xD0D8E4, 0xF8FCFF)
    S.line(6, 26, 14, 6, 0xF8FCFF, 2)
    S.line(12, 28, 18, 14, 0xF8FCFF, 1)


def tex_belt():
    """a conveyor from above: dark belt with bold ribs, yellow side rails"""
    S.rect(0, 0, 32, 32, 0x3A3E48)
    for y in range(0, 32, 8):                           # the ribs (it runs along y)
        S.rect(3, y, 26, 3, INK)
    for x0 in (0, 29):
        box_ink(x0, 0, 3, 32, 0xF0C030)


KITCHEN_TEX = [tex_wood_top, tex_cabinet, tex_stove_top, tex_stove_front, tex_oven,
               tex_crate, tex_sink, tex_steel, tex_belt]


# ---------------------------------------------------------------- cover

def cover():
    C = Img(128, 80, 0x6AB8D8)
    for y in range(80):
        t = y / 79
        for x in range(128):
            C.set(x, y, mix(0x5AA8E0, 0xF0E0C0, t))
    # a checked floor in perspective
    for y in range(44, 80):
        for x in range(128):
            depth = 1 / (y - 36)
            u = int((x - 64) * depth * 8 + 100)
            v = int(depth * 60)
            C.set(x, y, 0xEADCC0 if (u + v) % 2 else 0xD6C29E)
    # a pot with steam
    C.rect(48, 44, 32, 20, 0xA0A8B0)
    C.rect(46, 42, 36, 4, 0x707880)
    C.ellipse(64, 44, 15, 3, 0xE85030)
    for i, x in enumerate((54, 64, 74)):
        for y in range(28, 40):
            C.set(x + round(math.sin(y * 0.7 + i) * 2), y, 0xFFFFFF)
    # four little chef heads
    for i, (x, c) in enumerate(((14, 0xFFFFFF), (34, 0xE04830), (100, 0x30B060), (118, 0x3A70E0))):
        C.circle(x, 58, 6, SKIN)
        C.rect(x - 5, 49, 10, 4, c)
        C.rect(x - 3, 57, 2, 2, 0x1C1C24)
        C.rect(x + 1, 57, 2, 2, 0x1C1C24)
    for dx, dy in ((1, 1), (-1, 0), (1, 0), (0, -1), (0, 1)):
        C.text("CHAOS", 24 + dx, 2 + dy, 0x401008, 1)
        C.text("KITCHEN", 36 + dx, 14 + dy, 0x401008, 1)
    C.text("CHAOS", 24, 2, 0xFFD040)
    C.text("KITCHEN", 36, 14, 0xFF6040)
    write_png(os.path.join(HERE, "cover.png"), 128, 80, C.px, alpha=False)


def main():
    load_chop_look()
    for i, (ing, c1, c2, shp) in enumerate(ingredients()):
        icon_cell(i)
        draw_raw(ing, c1, c2)
        icon_cell(i + 32)
        draw_chopped(ing, c1, c2, CHOP_LOOK.get(ing, "cubes"))
    for i, name in enumerate(SYMBOLS):
        icon_cell(64 + i)
        symbol(name)
    for i, name in enumerate(MINIS):
        mini(i, name)
    for i in range(4):
        portrait(i)
    for i, fn in enumerate(KITCHEN_TEX):
        S.cell(128 + 32 * i, 80, 32, 32)
        fn()
    S.cell(0, 0, 0, 0)
    # the chefs' model textures
    tex = os.path.join(HERE, "models", "chefs.png")
    if os.path.exists(tex):
        import sys
        sys.path.insert(0, os.path.join(ROOT, "scripts"))
        from mkbm import read_png
        w, h, rgba = read_png(tex)
        for y in range(min(h, SH - 128)):
            for x in range(min(w, SW)):
                i = (y * w + x) * 4
                S.px[(128 + y) * SW + x] = tuple(rgba[i:i + 4])
    write_png(os.path.join(HERE, "sheet.png"), SW, SH, S.px)
    cover()


if __name__ == "__main__":
    main()
