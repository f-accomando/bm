#!/usr/bin/env python3
"""The menu icons (flat, no text): a coloured disc with a white glyph, after the user's sheet of 2026-11-11.

    python3 scripts/mkmenuicons.py          # art/icons/<name>_<1080|720|360>.png and art/icons/sheet.png

One master is drawn at 1080p (192 x 192, 4x oversampled) and scaled to 720p (128) and 360p (64), the
menu's 3:2:1 steps. Icons: market (an SD card), nes, snes, psx (controllers), bm (the chip), settings (a gear).
Coordinates are in the units of the sheet's disc (280 across), measured from the disc's centre, x right, y down.
The SD card has the bottom-left corner cut and the other three corners slightly rounded.
"""
import math
import os

from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "art", "icons")
U = 192                                     # the 1080p icon, in pixels
SS = 4                                      # oversampling of the master
K = U / 280.0                               # sheet units to pixels
SIZES = {"1080": 192, "720": 128, "360": 64}

WHITE = (247, 247, 250, 255)
PAD = (238, 238, 243, 255)
DARK = (52, 52, 58, 255)
GREY = (205, 205, 214, 255)

DISCS = {
    "market": (250, 69, 80),
    "nes": (77, 155, 255),
    "snes": (112, 207, 139),
    "psx": (52, 190, 204),
    "bm": (252, 194, 80),
    "settings": (155, 125, 238),
}


class Pen:
    def __init__(self):
        self.im = Image.new("RGBA", (U * SS, U * SS), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def t(self, v):
        return (U / 2 + v * K) * SS

    def pt(self, x, y):
        return (self.t(x), self.t(y))

    def rect(self, x0, y0, x1, y1, r, fill, corners=(True, True, True, True)):
        self.d.rounded_rectangle([self.t(x0), self.t(y0), self.t(x1) - 1, self.t(y1) - 1], r * K * SS, fill=fill, corners=corners)

    def circle(self, cx, cy, r, fill):
        self.d.ellipse([self.t(cx - r), self.t(cy - r), self.t(cx + r) - 1, self.t(cy + r) - 1], fill=fill)

    def poly(self, pts, fill):
        self.d.polygon([self.pt(x, y) for x, y in pts], fill=fill)

    def line(self, pts, w, fill):                       # a polyline with round joints and ends
        self.d.line([self.pt(x, y) for x, y in pts], fill=fill, width=int(w * K * SS))
        for x, y in pts:
            self.circle(x, y, w / 2, fill)

    def plus(self, cx, cy, arm, th, fill):
        self.rect(cx - arm, cy - th / 2, cx + arm, cy + th / 2, th / 3, fill)
        self.rect(cx - th / 2, cy - arm, cx + th / 2, cy + arm, th / 3, fill)

    def pill(self, cx, cy, w, h, fill):
        self.rect(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, h / 2, fill)

    def arc(self, cx, cy, r, a0, a1, w, fill):          # a thick arc, its centre line of radius r
        self.d.arc([self.t(cx - r), self.t(cy - r), self.t(cx + r), self.t(cy + r)], a0, a1, fill=fill, width=int(w * K * SS))


def cable(pen):
    pen.line([(12, -36), (12, -56), (28, -72), (46, -72), (46, -100)], 12, DARK)


def market(pen):
    c = DISCS["market"] + (255,)
    x0, y0, x1, y1, cut, r = -66, -80, 68, 85, 34, 13
    pen.rect(x0, y0, x1, y1, r, WHITE, corners=(True, True, True, False))            # three corners slightly rounded
    pen.poly([(x0 - 1, y1 - cut), (x0 + cut, y1 + 1), (x0 - 1, y1 + 1)], c)           # the bottom-left corner cut: the disc shows through
    pen.poly([(-18, 46), (26, 46), (4, 74)], c)                                       # the arrow of the card


def nes(pen):
    cable(pen)
    pen.rect(-104, -37, 104, 58, 9, PAD)
    pen.plus(-67, 12, 22, 15, DARK)
    for y in (-14, -8, -2):
        pen.rect(-22, y - 2, 22, y + 2.5, 2, GREY)
    pen.pill(-10, 22, 21, 10, DARK)
    pen.pill(15, 22, 21, 10, DARK)
    for cx in (45, 76):
        pen.rect(cx - 15, 8, cx + 15, 38, 7, (58, 22, 30, 255))
        pen.rect(cx - 12, 11, cx + 12, 35, 5, (244, 40, 58, 255))


def snes(pen):
    cable(pen)
    pen.rect(-110, -38, 110, 56, 44, PAD)
    pen.plus(-61, 10, 21, 14, DARK)
    for dx, dy, col in ((0, -23, (47, 107, 255)), (-23, 0, (47, 199, 122)), (24, 0, (242, 60, 60)), (0, 23, (251, 185, 66))):
        pen.circle(59 + dx, 8 + dy, 12, col + (255,))
    pen.pill(-14, 31, 16, 6, DARK)
    pen.pill(9, 31, 16, 6, DARK)


def psx(pen):
    cable(pen)
    pen.rect(-70, -37, 70, 25, 28, PAD)
    pen.rect(-104, -10, -34, 66, 30, PAD)
    pen.rect(34, -10, 104, 66, 30, PAD)
    pen.plus(-62, 4, 22, 14, DARK)
    pen.pill(-12, 20, 16, 7, DARK)
    pen.pill(12, 20, 16, 7, DARK)
    cx, cy, o = 62, 14, 20
    pen.poly([(cx, cy - o - 10), (cx + 11, cy - o + 9), (cx - 11, cy - o + 9)], (61, 201, 125, 255))    # triangle
    pen.circle(cx + o, cy, 10, (240, 82, 90, 255))                                                      # circle
    pen.line([(cx - 8, cy + o - 8), (cx + 8, cy + o + 8)], 6, (77, 135, 240, 255))                     # cross
    pen.line([(cx + 8, cy + o - 8), (cx - 8, cy + o + 8)], 6, (77, 135, 240, 255))
    pen.rect(cx - o - 9, cy - 9, cx - o + 9, cy + 9, 2, (240, 127, 192, 255))                           # square


def bm(pen):
    body = (58, 58, 63, 255)
    for i in (-1, 0, 1):                                                            # three pins on each side
        p = i * 33
        pen.rect(p - 8.5, -90, p + 8.5, -40, 8.5, body)
        pen.rect(p - 8.5, 40, p + 8.5, 90, 8.5, body)
        pen.rect(-90, p - 8.5, -40, p + 8.5, 8.5, body)
        pen.rect(40, p - 8.5, 90, p + 8.5, 8.5, body)
    pen.rect(-68, -66, 68, 66, 20, body)
    s = 11                                                                          # "bm": bold strokes
    pen.rect(-41, -28, -41 + s, 32, s / 2, WHITE)                                   # the stem of the b
    pen.circle(-23, 12, 17.5, WHITE)                                                # its bowl
    pen.circle(-23, 12, 6.5, body)
    pen.rect(2, -10, 2 + s, 32, s / 2, WHITE)                                        # m: three stems and two arches
    pen.rect(20, 4, 20 + s, 32, s / 2, WHITE)
    pen.rect(38, 4, 38 + s, 32, s / 2, WHITE)
    for ax in (7.5, 25.5):
        pen.arc(ax + 9, 4, 9, 180, 360, s, WHITE)


def settings(pen):
    for i in range(8):
        a = i * math.pi / 4
        c, sn = math.cos(a), math.sin(a)
        r0, r1, h0, h1 = 52, 83, 20, 15

        def p(r, h):
            return (c * r - sn * h, sn * r + c * h)
        pen.poly([p(r0, h0), p(r1, h1), p(r1, -h1), p(r0, -h0)], WHITE)
        for h in (h1 - 5, -(h1 - 5)):                                               # a slight round of the outer corners
            x, y = p(r1 - 5, h)
            pen.circle(x, y, 5, WHITE)
    pen.circle(0, 0, 62, WHITE)
    pen.circle(0, 0, 37, DISCS["settings"] + (255,))


GLYPHS = {"market": market, "nes": nes, "snes": snes, "psx": psx, "bm": bm, "settings": settings}


def make(name):
    pen = Pen()
    pen.d.ellipse([0, 0, U * SS - 1, U * SS - 1], fill=DISCS[name] + (255,))
    GLYPHS[name](pen)
    mask = Image.new("L", pen.im.size, 0)                                           # keep everything inside the disc
    ImageDraw.Draw(mask).ellipse([0, 0, U * SS - 1, U * SS - 1], fill=255)
    out = Image.new("RGBA", pen.im.size, (0, 0, 0, 0))
    out.paste(pen.im, (0, 0), mask)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    sheet = Image.new("RGBA", (len(GLYPHS) * 224 + 32, 280), (16, 16, 18, 255))
    for k, name in enumerate(GLYPHS):
        master = make(name)
        for tag, size in SIZES.items():
            master.resize((size, size), Image.LANCZOS).save(os.path.join(OUT, "%s_%s.png" % (name, tag)))
        sheet.alpha_composite(master.resize((192, 192), Image.LANCZOS), (32 + k * 224, 16))
        sheet.alpha_composite(master.resize((64, 64), Image.LANCZOS), (32 + k * 224, 210))
    sheet.save(os.path.join(OUT, "sheet.png"))
    print("icons in", OUT)


if __name__ == "__main__":
    main()
