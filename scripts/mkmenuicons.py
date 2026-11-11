#!/usr/bin/env python3
"""The menu icons (flat, no text): a coloured disc with a white glyph.

    python3 scripts/mkmenuicons.py          # art/icons/<name>_<1080|720|360>.png and art/icons/sheet.png

One master is drawn at 1080p (192 x 192, 4x oversampled) and scaled to 720p (128) and 360p (64), the
menu's 3:2:1 steps. Icons: market (an SD card), nes, snes, psx (controllers), bm (the chip), settings (a gear).
The SD card has the bottom-left corner cut and the other three corners slightly rounded.
"""
import math
import os

from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "art", "icons")
U = 192                                     # the design box, in 1080p pixels
SS = 4                                      # oversampling of the master
SIZES = {"1080": 192, "720": 128, "360": 64}

WHITE = (250, 250, 252, 255)
PAD = (236, 236, 241, 255)
DARK = (44, 44, 52, 255)
GREY = (205, 205, 214, 255)

DISCS = {
    "market": (242, 69, 79),
    "nes": (76, 154, 255),
    "snes": (111, 207, 138),
    "psx": (50, 190, 204),
    "bm": (251, 194, 78),
    "settings": (155, 123, 238),
}


class Pen:
    def __init__(self):
        self.im = Image.new("RGBA", (U * SS, U * SS), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    @staticmethod
    def p(v):
        return [(x * SS, y * SS) for x, y in v]

    def rect(self, x0, y0, x1, y1, r, fill, corners=(True, True, True, True)):
        self.d.rounded_rectangle([x0 * SS, y0 * SS, x1 * SS - 1, y1 * SS - 1], r * SS, fill=fill, corners=corners)

    def circle(self, cx, cy, r, fill):
        self.d.ellipse([(cx - r) * SS, (cy - r) * SS, (cx + r) * SS - 1, (cy + r) * SS - 1], fill=fill)

    def poly(self, pts, fill):
        self.d.polygon(self.p(pts), fill=fill)

    def line(self, pts, w, fill):                       # a polyline with round joints and ends
        self.d.line(self.p(pts), fill=fill, width=int(w * SS))
        for x, y in pts:
            self.circle(x, y, w / 2, fill)

    def plus(self, cx, cy, arm, th, fill):
        self.rect(cx - arm, cy - th / 2, cx + arm, cy + th / 2, th / 3, fill)
        self.rect(cx - th / 2, cy - arm, cx + th / 2, cy + arm, th / 3, fill)

    def pill(self, cx, cy, w, h, fill):
        self.rect(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, h / 2, fill)

    def ring(self, cx, cy, r, w, fill):
        self.d.ellipse([(cx - r) * SS, (cy - r) * SS, (cx + r) * SS - 1, (cy + r) * SS - 1], outline=fill, width=int(w * SS))


def disc(pen, name):
    pen.circle(U / 2, U / 2, U / 2, DISCS[name] + (255,))


def cable(pen):
    pen.line([(84, 80), (84, 66), (96, 58), (108, 58), (108, 36)], 7, DARK)


def market(pen):
    c = DISCS["market"] + (255,)
    pen.rect(56, 36, 136, 156, 9, WHITE, corners=(True, True, True, False))        # slightly rounded; the bottom-left is cut
    pen.poly([(56, 124), (56, 157), (89, 157)], c)                                  # the cut: the disc shows through
    pen.poly([(56, 124), (88, 156), (56, 156)], c)
    pen.poly([(80, 112), (112, 112), (96, 130)], c)                                 # the arrow of the card


def nes(pen):
    cable(pen)
    pen.rect(25, 78, 167, 142, 8, PAD)
    pen.plus(52, 110, 14, 10, DARK)
    pen.rect(78, 90, 118, 102, 3, GREY)
    pen.line([(82, 94), (114, 94)], 1.6, (170, 170, 182, 255))
    pen.line([(82, 98), (114, 98)], 1.6, (170, 170, 182, 255))
    pen.pill(87, 116, 15, 7, DARK)
    pen.pill(109, 116, 15, 7, DARK)
    for cx in (133, 153):
        pen.circle(cx, 113, 10, DARK)
        pen.circle(cx, 113, 7.5, (232, 40, 58, 255))


def snes(pen):
    cable(pen)
    pen.rect(30, 82, 162, 132, 22, PAD)
    pen.circle(50, 112, 23, PAD)
    pen.circle(142, 112, 23, PAD)
    pen.plus(52, 106, 13, 9, DARK)
    for dx, dy, col in ((0, -12, (47, 107, 255)), (-12, 0, (47, 199, 122)), (12, 0, (242, 60, 60)), (0, 12, (251, 185, 66))):
        pen.circle(142 + dx, 106 + dy, 7, col + (255,))
    pen.line([(86, 118), (92, 114)], 5, DARK)
    pen.line([(102, 118), (108, 114)], 5, DARK)


def psx(pen):
    cable(pen)
    pen.rect(38, 82, 154, 126, 20, PAD)
    pen.rect(30, 98, 70, 154, 18, PAD)
    pen.rect(122, 98, 162, 154, 18, PAD)
    pen.plus(56, 108, 13, 9, DARK)
    pen.pill(89, 116, 10, 5, DARK)
    pen.pill(103, 116, 10, 5, DARK)
    cx, cy, o = 136, 108, 13
    pen.poly([(cx, cy - o - 6), (cx + 6, cy - o + 5), (cx - 6, cy - o + 5)], (61, 201, 125, 255))    # triangle
    pen.circle(cx + o, cy, 5.5, (240, 82, 90, 255))                                                  # circle
    pen.line([(cx - 5, cy + o - 5), (cx + 5, cy + o + 5)], 3.4, (77, 135, 240, 255))                # cross
    pen.line([(cx + 5, cy + o - 5), (cx - 5, cy + o + 5)], 3.4, (77, 135, 240, 255))
    pen.rect(cx - o - 5, cy - 5, cx - o + 5, cy + 5, 1, (240, 127, 192, 255))                        # square


def bm(pen):
    body = (43, 43, 51, 255)
    for i in range(4):                                                              # pins: 4 above, 4 below, 3 on each side
        x = 70 + i * 18
        pen.rect(x - 4.5, 28, x + 4.5, 70, 4.5, body)
        pen.rect(x - 4.5, 122, x + 4.5, 164, 4.5, body)
    for i in range(3):
        y = 77 + i * 18
        pen.rect(28, y - 4.5, 70, y + 4.5, 4.5, body)
        pen.rect(122, y - 4.5, 164, y + 4.5, 4.5, body)
    pen.rect(50, 50, 142, 142, 15, body)
    pen.rect(58, 58, 134, 134, 10, (56, 56, 66, 255))                               # the die: one step lighter, flat
    pen.rect(62, 62, 130, 130, 8, body)
    w, x0, y0 = 7, 68, 78                                                           # "bm": a b and an m drawn from strokes
    pen.rect(x0, y0 - 6, x0 + w, y0 + 42, w / 2, WHITE)                             # the stem of the b
    pen.ring(x0 + 13, y0 + 30, 11.5, w, WHITE)                                      # its bowl (an outline)
    mx = 96
    pen.rect(mx, y0 + 17, mx + w, y0 + 42, w / 2 - 1, WHITE)                        # m: three stems and two arches
    pen.rect(mx + 12, y0 + 24, mx + 12 + w, y0 + 42, w / 2 - 1, WHITE)
    pen.rect(mx + 24, y0 + 24, mx + 24 + w, y0 + 42, w / 2 - 1, WHITE)
    for ax in (mx + 3.5, mx + 15.5):
        pen.d.arc([ax * SS, (y0 + 17) * SS, (ax + 16) * SS, (y0 + 33) * SS], 180, 360, fill=WHITE, width=int(w * SS))


def settings(pen):
    cx = cy = U / 2
    for i in range(8):
        a = i * math.pi / 4
        c, s = math.cos(a), math.sin(a)
        hw, r0, r1 = 11, 40, 64
        pts = [(cx + c * r0 - s * hw, cy + s * r0 + c * hw), (cx + c * r1 - s * (hw - 2), cy + s * r1 + c * (hw - 2)),
               (cx + c * r1 + s * (hw - 2), cy + s * r1 - c * (hw - 2)), (cx + c * r0 + s * hw, cy + s * r0 - c * hw)]
        pen.poly(pts, WHITE)
    pen.circle(cx, cy, 50, WHITE)
    pen.circle(cx, cy, 22, DISCS["settings"] + (255,))


GLYPHS = {"market": market, "nes": nes, "snes": snes, "psx": psx, "bm": bm, "settings": settings}


def make(name):
    pen = Pen()
    disc(pen, name)
    GLYPHS[name](pen)
    mask = Image.new("L", pen.im.size, 0)                                           # keep everything inside the disc
    ImageDraw.Draw(mask).ellipse([0, 0, U * SS - 1, U * SS - 1], fill=255)
    out = Image.new("RGBA", pen.im.size, (0, 0, 0, 0))
    out.paste(pen.im, (0, 0), mask)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    sheet = Image.new("RGBA", (len(GLYPHS) * 224 + 32, 4 * 0 + 256), (16, 16, 18, 255))
    for k, name in enumerate(GLYPHS):
        master = make(name)
        for tag, size in SIZES.items():
            master.resize((size, size), Image.LANCZOS).save(os.path.join(OUT, "%s_%s.png" % (name, tag)))
        big = master.resize((192, 192), Image.LANCZOS)
        sheet.alpha_composite(big, (32 + k * 224, 16))
        s = master.resize((64, 64), Image.LANCZOS)
        sheet.alpha_composite(s, (32 + k * 224, 200))
        sheet.alpha_composite(master.resize((32, 32), Image.LANCZOS), (110 + k * 224, 216))
    sheet.save(os.path.join(OUT, "sheet.png"))
    print("icons in", OUT)


if __name__ == "__main__":
    main()
