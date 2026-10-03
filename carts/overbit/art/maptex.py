"""
maptex.py - the textures of Overbit's maps: one sprite sheet (the atlas)
with the windows of the facades (shutters open or closed, lit at night or
dark, with a balcony, arched), doors, a shop window and the signs (the
pizzeria's neon, the holo board), drawn here pixel by pixel, at about 32
pixels a metre. Few colours: the sheet is stored with a palette (SHEET8).

Atlas.tile(name) -> (x, y, w, h) in sheet pixels; Atlas.image (RGBA).
"""
import math
import os
import re

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 512, 256

# the console's 8x16 font (src/gfx/font8x16.c) for the lettering
_FONT = None


def font():
    global _FONT
    if _FONT is None:
        src = open(os.path.join(HERE, "..", "..", "..", "src", "gfx", "font8x16.c")).read()
        rows = re.findall(r"\{((?:\s*0x[0-9a-f]{2},?){16})\s*\}", src)
        _FONT = [[int(v, 16) for v in re.findall(r"0x([0-9a-f]{2})", r)] for r in rows]
    return _FONT


def text(d, x, y, s, rgb, scale=1, glow=None):
    """the console's font, `scale` times; glow: a darker halo"""
    f = font()
    for i, ch in enumerate(s):
        g = f[ord(ch)]
        for row in range(16):
            bits = g[row]
            for col in range(8):
                if bits & (0x80 >> col):
                    px, py = x + (i * 8 + col) * scale, y + row * scale
                    if glow:
                        d.rectangle([px - 1, py - 1, px + scale, py + scale], fill=glow)
        for row in range(16):
            bits = g[row]
            for col in range(8):
                if bits & (0x80 >> col):
                    px, py = x + (i * 8 + col) * scale, y + row * scale
                    d.rectangle([px, py, px + scale - 1, py + scale - 1], fill=rgb)


def rgb(h, a=255):
    return ((h >> 16) & 255, (h >> 8) & 255, h & 255, a)


STONE = rgb(0xE6DCC6)
STONE2 = rgb(0xC9BDA4)
SHUT = rgb(0x3F6E52)
SHUT2 = rgb(0x2F5640)
SHUT3 = rgb(0x5A8A6A)
GLASS = rgb(0x2A3340)
GLASS2 = rgb(0x3E4C5E)
GLASS3 = rgb(0x8EA6BE)
WARM = rgb(0xFFD9A0)
WARM2 = rgb(0xE8A860)
CURTAIN = rgb(0xF0E6D0)
IRON = rgb(0x26282C)
WOOD = rgb(0x6A4A30)
WOOD2 = rgb(0x523822)
CLEAR = (0, 0, 0, 0)


class Atlas:
    def __init__(self):
        self.image = Image.new("RGBA", (W, H), CLEAR)
        self.d = ImageDraw.Draw(self.image)
        self.tiles = {}
        self.x, self.y, self.row_h = 0, 0, 0

    def place(self, name, w, h):
        if self.x + w > W:
            self.x, self.y, self.row_h = 0, self.y + self.row_h + 1, 0
        if self.y + h > H:
            raise ValueError("the atlas is full")
        t = (self.x, self.y, w, h)
        self.tiles[name] = t
        self.x += w + 1
        self.row_h = max(self.row_h, h)
        return t

    def tile(self, name):
        return self.tiles[name]


def window(a, name, w=48, h=64, mode="open", lit=False, balcony=False, arch=False):
    """a window of the facades: a stone frame and sill, the glass (dark with
    a reflection, or warm and lit with curtains), green louvred shutters
    open to the sides or closed, a balcony railing in front"""
    x, y, _, _ = a.place(name, w, h)
    d = a.d
    sw = 11                                     # shutter width
    gx0, gx1 = x + sw + 2, x + w - sw - 3       # the glass between the shutters
    gy0, gy1 = y + 6, y + h - 7
    # the frame and the sill
    d.rectangle([gx0 - 2, gy0 - 3, gx1 + 2, gy1 + 2], fill=STONE)
    d.rectangle([gx0 - 4, gy1 + 2, gx1 + 4, gy1 + 5], fill=STONE2)
    if arch:
        d.pieslice([gx0 - 2, gy0 - 10, gx1 + 2, gy0 + 12], 180, 360, fill=STONE)
    # the glass
    if mode == "closed":
        for yy in range(gy0, gy1 + 1, 3):
            d.line([gx0, yy, gx1, yy], fill=SHUT)
            d.line([gx0, yy + 1, gx1, yy + 1], fill=SHUT2)
            d.line([gx0, yy + 2, gx1, yy + 2], fill=SHUT3)
        d.line([(gx0 + gx1) // 2, gy0, (gx0 + gx1) // 2, gy1], fill=SHUT2)
    else:
        top = gy0 + (6 if arch else 0)
        if lit:
            d.rectangle([gx0, gy0, gx1, gy1], fill=WARM)
            d.rectangle([gx0, gy0, gx0 + 4, gy1], fill=CURTAIN)
            d.rectangle([gx1 - 4, gy0, gx1, gy1], fill=CURTAIN)
            d.rectangle([gx0 + 5, gy1 - 8, gx1 - 5, gy1], fill=WARM2)
        else:
            d.rectangle([gx0, gy0, gx1, gy1], fill=GLASS)
            d.polygon([(gx0 + 3, gy1), (gx0 + 9, gy0 + 4), (gx0 + 13, gy0 + 4), (gx0 + 7, gy1)], fill=GLASS2)
            d.line([gx0 + 12, gy0 + 6, gx0 + 8, gy0 + 16], fill=GLASS3)
        # the cross of the window frame
        d.line([(gx0 + gx1) // 2, gy0, (gx0 + gx1) // 2, gy1], fill=STONE)
        d.line([gx0, (gy0 + gy1) // 2 - 4, gx1, (gy0 + gy1) // 2 - 4], fill=STONE)
        if arch:
            d.pieslice([gx0, gy0 - 8, gx1, gy0 + 10], 180, 360, fill=GLASS if not lit else WARM)
        # the shutters, open against the wall
        for sx0 in (x + 1, x + w - sw - 1):
            d.rectangle([sx0, gy0 - 2, sx0 + sw - 1, gy1 + 1], fill=SHUT2)
            for yy in range(gy0, gy1, 3):
                d.line([sx0 + 1, yy, sx0 + sw - 2, yy], fill=SHUT3)
                d.line([sx0 + 1, yy + 1, sx0 + sw - 2, yy + 1], fill=SHUT)
    if balcony:
        # a railing of black iron over the lower half, curls on top
        by0 = y + h - 22
        d.rectangle([x, by0, x + w - 1, by0 + 1], fill=IRON)
        d.rectangle([x, y + h - 3, x + w - 1, y + h - 1], fill=IRON)
        for xx in range(x + 1, x + w, 4):
            d.line([xx, by0, xx, y + h - 3], fill=IRON)
        for xx in range(x + 4, x + w - 4, 8):
            d.arc([xx - 3, by0 + 2, xx + 3, by0 + 8], 0, 360, fill=IRON)


def door(a, name, w=48, h=80):
    x, y, _, _ = a.place(name, w, h)
    d = a.d
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=STONE)
    d.rectangle([x + 4, y + 6, x + w - 5, y + h - 1], fill=WOOD)
    d.pieslice([x + 4, y + 2, x + w - 5, y + 20], 180, 360, fill=WOOD2)
    d.line([x + w // 2, y + 10, x + w // 2, y + h - 1], fill=WOOD2)
    for yy in (y + 26, y + 50):
        d.rectangle([x + 8, yy, x + w // 2 - 4, yy + 18], outline=WOOD2)
        d.rectangle([x + w // 2 + 4, yy, x + w - 9, yy + 18], outline=WOOD2)
    d.rectangle([x + w // 2 - 4, y + 44, x + w // 2 - 3, y + 47], fill=rgb(0xD8B048))
    d.rectangle([x + w // 2 + 2, y + 44, x + w // 2 + 3, y + 47], fill=rgb(0xD8B048))


def shop(a, name, w=96, h=72):
    """a shop window lit warm, goods on shelves, the frame dark"""
    x, y, _, _ = a.place(name, w, h)
    d = a.d
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=rgb(0x2A2A2E))
    d.rectangle([x + 3, y + 3, x + w - 4, y + h - 10], fill=WARM)
    for i, yy in enumerate((y + 20, y + 38)):
        d.rectangle([x + 4, yy, x + w - 5, yy + 1], fill=WOOD)
        for k in range(8):
            c = [rgb(0xE85A3A), rgb(0x5A9A3A), rgb(0xF2D25A), rgb(0xE8E0D0)][(k + i) % 4]
            d.rectangle([x + 8 + k * 11, yy - 8, x + 14 + k * 11, yy - 1], fill=c)
    d.rectangle([x + w // 2 - 1, y + 3, x + w // 2, y + h - 10], fill=rgb(0x2A2A2E))
    d.rectangle([x, y + h - 9, x + w - 1, y + h - 1], fill=STONE2)


def sign(a, name, words, fg, bg, w, h, scale=1):
    x, y, _, _ = a.place(name, w, h)
    d = a.d
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=bg)
    tw = len(words) * 8 * scale
    text(d, x + (w - tw) // 2, y + (h - 16 * scale) // 2, words, fg, scale,
         glow=tuple(max(0, c // 3) for c in fg[:3]) + (255,))


def holo(a, name, w=144, h=76):
    """the holo board: the city's name over a skyline, cyan on dark blue"""
    x, y, _, _ = a.place(name, w, h)
    d = a.d
    d.rectangle([x, y, x + w - 1, y + h - 1], fill=rgb(0x10283A))
    for yy in range(y, y + h, 4):
        d.line([x, yy, x + w - 1, yy], fill=rgb(0x163648))
    c = rgb(0x5FE8FF)
    # Vesuvius and the bay in lines
    d.line([x + 8, y + h - 14, x + 50, y + 34, x + 64, y + 30, x + 78, y + 36, x + 120, y + h - 14], fill=c)
    d.line([x + 6, y + h - 10, x + w - 6, y + h - 10], fill=c)
    text(d, x + (w - 9 * 8 * 1) // 2, y + 6, "PARTENOPE", rgb(0xCFFBFF), 1)
    d.rectangle([x, y, x + w - 1, y + h - 1], outline=c)


def awning(a, name, w=64, h=24):
    """the stripes of an awning, red and white, its edge cut in scallops"""
    x, y, _, _ = a.place(name, w, h)
    d = a.d
    for i in range(0, w, 8):
        d.rectangle([x + i, y, x + i + 7, y + h - 6], fill=rgb(0xC8443A) if (i // 8) % 2 == 0 else rgb(0xF0EAE0))
        d.pieslice([x + i, y + h - 10, x + i + 7, y + h - 1], 0, 180,
                   fill=rgb(0xA8382F) if (i // 8) % 2 == 0 else rgb(0xD8D0C4))
    d.line([x, y + h - 6, x + w - 1, y + h - 6], fill=rgb(0x8A2A22))


def washing(a, name, w=128, h=32):
    """clothes on a line: shirts, sheets, socks; nothing round them"""
    x, y, _, _ = a.place(name, w, h)
    d = a.d
    cols = [rgb(0xF2F2EE), rgb(0xE85A5A), rgb(0x5A9AE8), rgb(0xF2D25A), rgb(0x8AD87A), rgb(0xF0B8D0)]
    d.line([x, y + 2, x + w - 1, y + 2], fill=IRON)
    xx, k = x + 3, 0
    while xx < x + w - 12:
        c = cols[(k * 5 + 1) % len(cols)]
        dark = tuple(int(v * 0.8) for v in c[:3]) + (255,)
        kind = (k * 7 + 3) % 4
        if kind == 0:                                   # a shirt
            d.rectangle([xx + 3, y + 3, xx + 16, y + 22], fill=c)
            d.rectangle([xx, y + 3, xx + 19, y + 8], fill=c)
            d.line([xx + 10, y + 3, xx + 10, y + 22], fill=dark)
            xx += 24
        elif kind == 1:                                 # a sheet
            d.rectangle([xx, y + 3, xx + 26, y + 29], fill=c)
            d.line([xx + 9, y + 3, xx + 9, y + 29], fill=dark)
            d.line([xx + 18, y + 3, xx + 18, y + 29], fill=dark)
            xx += 31
        elif kind == 2:                                 # trousers
            d.rectangle([xx, y + 3, xx + 12, y + 8], fill=c)
            d.rectangle([xx, y + 8, xx + 5, y + 25], fill=c)
            d.rectangle([xx + 7, y + 8, xx + 12, y + 25], fill=c)
            xx += 17
        else:                                           # socks
            for q in range(2):
                d.rectangle([xx + q * 6, y + 3, xx + q * 6 + 3, y + 12], fill=c)
                d.rectangle([xx + q * 6, y + 12, xx + q * 6 + 5, y + 14], fill=c)
            xx += 15
        k += 1


def build():
    a = Atlas()
    window(a, "win", mode="open")
    window(a, "win_lit", mode="open", lit=True)
    window(a, "win_shut", mode="closed")
    window(a, "win_bal", mode="open", balcony=True)
    window(a, "win_bal_lit", mode="open", lit=True, balcony=True)
    window(a, "win_arch", mode="open", arch=True)
    window(a, "win_shut_bal", mode="closed", balcony=True)
    door(a, "door")
    shop(a, "shop")
    sign(a, "pizza", "PIZZERIA BIT", rgb(0xFF4A3A), rgb(0x1A1414), 104, 22)
    sign(a, "gelato", "GELATO", rgb(0xFFD04A), rgb(0x2A2014), 56, 20)
    sign(a, "bar", "CAFFE'", rgb(0x5FE8FF), rgb(0x14202A), 56, 20)
    holo(a, "holo")
    awning(a, "awning")
    washing(a, "washing")
    return a


def final(a):
    """the atlas with at most 256 colours (for SHEET8), alpha 0 or 255"""
    img = a.image
    alpha = img.getchannel("A")
    q = img.convert("RGB").quantize(colors=255, method=Image.Quantize.MEDIANCUT).convert("RGB")
    return Image.merge("RGBA", (*q.split(), alpha.point(lambda v: 255 if v >= 128 else 0)))


def save(a, path):
    out = final(a)
    out.save(path)
    return out


def sheet(a):
    """(w, h, rgba bytes) for mkbm.pack"""
    img = final(a)
    return img.width, img.height, img.tobytes()


if __name__ == "__main__":
    import sys
    a = build()
    save(a, sys.argv[1] if len(sys.argv) > 1 else "/tmp/maptex.png")
    for k, v in a.tiles.items():
        print(k, v)
