#!/usr/bin/env python3
"""Draws the artwork covers of the games and apps (carts/<name>/cover.png).

One style for all: a graded sky or room, a soft light behind one emblem of the
game, a dark foot with the title in a bold sans font. The pictures are NOT
screenshots and do not use the in-game pixel look. They are drawn at 4x and
reduced to 88x88, the size of the menu's square cards (src/kernel/menu_ui.h
MENU_CARD).

    python3 scripts/mkartcovers.py            (needs Pillow; all covers)
    python3 scripts/mkartcovers.py pong snake (only these)

The covers of the bm editors (animator, editor, mesh, pixel, sound, studio,
write) are drawn by scripts/mkcovers.py. This script never writes them.
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageOps

S = 4                       # supersampling
N = 88                      # final size
W = N * S
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def rgb(c):
    return (c >> 16 & 255, c >> 8 & 255, c & 255)


def P(pts):
    return [(x * S, y * S) for x, y in pts]


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


class Art:
    def __init__(self, top, bottom):
        mask = Image.linear_gradient("L").resize((W, W))
        self.im = Image.composite(Image.new("RGB", (W, W), rgb(bottom)),
                                  Image.new("RGB", (W, W), rgb(top)), mask).convert("RGBA")

    def layer(self):
        return Image.new("RGBA", (W, W), (0, 0, 0, 0))

    def over(self, lay):
        self.im.alpha_composite(lay)

    def glow(self, cx, cy, r, col, a=0.8):
        m = ImageOps.invert(Image.radial_gradient("L")).resize((2 * r * S, 2 * r * S))
        m = m.point(lambda v: int(v * a))
        lay = self.layer()
        lay.paste(Image.new("RGBA", m.size, rgb(col) + (255,)), (int((cx - r) * S), int((cy - r) * S)), m)
        self.over(lay)

    def shadow(self, draw_fn, dx=1.5, dy=2, blur=2, a=110):
        lay = self.layer()
        draw_fn(ImageDraw.Draw(lay), (0, 0, 0, 255))
        lay = lay.filter(ImageFilter.GaussianBlur(blur * S))
        alpha = lay.split()[3].point(lambda v: v * a // 255)
        black = Image.new("RGBA", (W, W), (0, 0, 0, 255))
        sh = self.layer()
        sh.paste(black, (int(dx * S), int(dy * S)), alpha)
        self.over(sh)

    def draw(self):
        lay = self.layer()
        return lay, ImageDraw.Draw(lay)

    def finish(self, title, path):
        im = self.im
        lay = self.layer()
        foot = Image.linear_gradient("L").resize((W, 36 * S)).point(lambda v: int(v * 0.86))
        lay.paste(Image.new("RGBA", foot.size, (6, 8, 16, 255)), (0, 52 * S), foot)
        im.alpha_composite(lay)
        vig = ImageOps.invert(Image.radial_gradient("L")).resize((W, W))
        vig = ImageOps.invert(vig).point(lambda v: int(v * 0.28))
        im.alpha_composite(Image.composite(Image.new("RGBA", (W, W), (0, 0, 0, 255)),
                                           Image.new("RGBA", (W, W), (0, 0, 0, 0)), vig))
        d = ImageDraw.Draw(im)
        size = 11 * S
        while size > 6 * S:
            f = ImageFont.truetype(FONT, size)
            tw = d.textlength(title, font=f)
            if tw <= 78 * S:
                break
            size -= 1
        x = (W - tw) / 2
        y = 70 * S
        d.text((x + S, y + S), title, font=f, fill=(0, 0, 0, 200), anchor="lm")
        d.text((x, y), title, font=f, fill=(255, 255, 255, 255), anchor="lm")
        d.rounded_rectangle((S, S, W - S - 1, W - S - 1), 4 * S, outline=(255, 255, 255, 38), width=S)
        out = im.convert("RGB").resize((N, N), Image.LANCZOS)
        out.save(path)


def stars(a, seed, n, y1=52, col=(255, 255, 255)):
    lay, d = a.draw()
    s = seed
    for _ in range(n):
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        x = s % 88
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        y = s % y1
        r = 0.35 + (s >> 8) % 3 * 0.25
        d.ellipse(((x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S), fill=col + (200,))
    a.over(lay)


def moon(a, cx, cy, r, col, halo):
    a.glow(cx, cy, int(r * 3), halo, 0.55)
    lay, d = a.draw()
    d.ellipse(((cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S), fill=rgb(col) + (255,))
    a.over(lay)


# ----------------------------------------------------------------- the emblems

def astrowing(a):
    a.glow(44, 40, 34, 0xFF9040, 0.9)
    stars(a, 11, 40, 30)
    lay, d = a.draw()
    d.polygon(P([(0, 44), (88, 38), (88, 60), (0, 60)]), fill=(24, 40, 70, 255))
    for i in range(6):                                     # the grid below
        y = 44 + i * 3.4
        d.line(P([(0, y), (88, y - 1.5 + i * 0.4)]), fill=(70, 120, 190, 120), width=S)
    a.over(lay)

    def wing(dr, c):
        dr.polygon(P([(44, 14), (60, 44), (44, 38), (28, 44)]), fill=c)
    a.shadow(wing, 2, 4, 2.5)
    lay, d = a.draw()
    d.polygon(P([(44, 12), (62, 45), (44, 38), (26, 45)]), fill=(232, 240, 255, 255))
    d.polygon(P([(44, 12), (44, 38), (26, 45)]), fill=(150, 175, 220, 255))
    d.polygon(P([(44, 22), (49, 34), (44, 32), (39, 34)]), fill=(60, 110, 230, 255))
    d.polygon(P([(41, 40), (47, 40), (44, 49)]), fill=(255, 190, 70, 255))
    a.over(lay)
    a.glow(44, 44, 6, 0xFFC060, 0.9)


def kitchen(a):
    a.glow(44, 36, 36, 0xFFC050, 0.75)
    lay, d = a.draw()
    for i in range(0, 88, 11):                               # tile floor
        d.polygon(P([(i, 52), (i + 5.5, 52), (i + 8, 60), (i - 2.5, 60)]), fill=(255, 255, 255, 40))
    a.over(lay)
    for x, y in ((38, 14), (46, 10), (53, 15)):              # steam
        lay, d = a.draw()
        pts = [(x + math.sin(t / 2.2) * 2.2, y + 18 - t) for t in range(0, 16)]
        d.line(P(pts), fill=(255, 255, 255, 150), width=int(1.6 * S), joint="curve")
        a.over(lay)
    a.shadow(lambda dr, c: dr.ellipse(P([(22, 30), (66, 60)])[0] + P([(22, 30), (66, 60)])[1], fill=c))
    lay, d = a.draw()
    d.ellipse(P([(24, 28), (64, 58)])[0] + P([(24, 28), (64, 58)])[1], fill=(170, 52, 38, 255))
    d.ellipse(P([(24, 28), (64, 44)])[0] + P([(24, 28), (64, 44)])[1], fill=(90, 28, 24, 255))
    d.ellipse(P([(27, 30), (61, 42)])[0] + P([(27, 30), (61, 42)])[1], fill=(236, 110, 40, 255))
    d.rectangle(P([(20, 33), (26, 36)]), fill=(60, 30, 26, 255))
    d.rectangle(P([(62, 33), (68, 36)]), fill=(60, 30, 26, 255))
    a.over(lay)
    lay, d = a.draw()                                        # the chef hat floating above
    for cx, cy, r in ((38, 14, 5), (44, 11, 6), (50, 14, 5)):
        d.ellipse(((cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S), fill=(255, 255, 255, 255))
    d.rectangle(P([(36, 14), (52, 22)]), fill=(255, 255, 255, 255))
    d.rectangle(P([(36, 20), (52, 22.5)]), fill=(200, 205, 215, 255))
    a.over(lay)


def ko2(a):
    a.glow(44, 34, 34, 0x40C0FF, 0.5)

    def body(dr, c):
        dr.rounded_rectangle(P([(14, 12), (74, 56)])[0] + P([(14, 12), (74, 56)])[1], 5 * S, fill=c)
    a.shadow(body)
    lay, d = a.draw()
    body(d, (36, 40, 56, 255))
    d.rounded_rectangle(P([(17, 15), (71, 27)])[0] + P([(17, 15), (71, 27)])[1], 3 * S, fill=(10, 20, 24, 255))
    for i in range(14):                                      # the LCD bars
        h = 2 + (i * 7 % 6)
        d.rectangle(P([(19 + i * 3.6, 25 - h), (21.4 + i * 3.6, 25)]), fill=(80, 255, 170, 255))
    cols = (0xFF7A30, 0xFFC030, 0x40D0A0, 0x50A0FF)
    for r in range(3):
        for c in range(4):
            x, y = 19 + c * 9.6, 30 + r * 8.2
            d.rounded_rectangle(P([(x, y), (x + 8, y + 6.4)])[0] + P([(x, y), (x + 8, y + 6.4)])[1], 1.4 * S,
                                fill=rgb(cols[(c + r) % 4]) + (255,))
    d.ellipse(P([(60, 31), (69, 40)])[0] + P([(60, 31), (69, 40)])[1], fill=(210, 214, 224, 255))
    d.line(P([(64.5, 35.5), (66.5, 32)]), fill=(40, 40, 50, 255), width=int(1.2 * S))
    d.rectangle(P([(61, 44), (69, 46)]), fill=(100, 106, 120, 255))
    d.rectangle(P([(64, 43), (66, 47)]), fill=(255, 122, 48, 255))
    a.over(lay)


def nano8(a):
    a.glow(44, 36, 32, 0xC060FF, 0.6)
    stars(a, 31, 24, 30, (230, 210, 255))

    def body(dr, c):
        dr.rounded_rectangle(P([(20, 12), (68, 56)])[0] + P([(20, 12), (68, 56)])[1], 6 * S, fill=c)
    a.shadow(body)
    lay, d = a.draw()
    body(d, (232, 228, 240, 255))
    d.rounded_rectangle(P([(24, 16), (64, 40)])[0] + P([(24, 16), (64, 40)])[1], 3 * S, fill=(18, 20, 40, 255))
    d.pieslice(P([(30, 32), (58, 52)])[0] + P([(30, 32), (58, 52)])[1], 180, 360, fill=(70, 220, 110, 255))
    d.polygon(P([(44, 20), (47, 27), (41, 27)]), fill=(255, 255, 255, 255))
    d.rectangle(P([(38, 24), (50, 27)]), fill=(255, 90, 120, 255))
    d.rectangle(P([(26, 44), (34, 46)]), fill=(120, 120, 140, 255))
    d.rectangle(P([(29, 41), (31, 49)]), fill=(120, 120, 140, 255))
    d.ellipse(P([(52, 43), (57, 48)])[0] + P([(52, 43), (57, 48)])[1], fill=(255, 90, 120, 255))
    d.ellipse(P([(58, 40), (63, 45)])[0] + P([(58, 40), (63, 45)])[1], fill=(255, 190, 60, 255))
    a.over(lay)


def overbit(a):
    a.glow(44, 36, 40, 0xFF8A50, 0.85)
    lay, d = a.draw()
    for x, h in ((0, 14), (8, 22), (18, 12), (60, 20), (70, 28), (80, 16)):   # the city
        d.rectangle(P([(x, 52 - h), (x + 9, 56)]), fill=(48, 26, 58, 255))
    a.over(lay)

    def mech(dr, c):
        dr.polygon(P([(14, 62), (18, 38), (32, 30), (56, 30), (70, 38), (74, 62)]), fill=c)
        dr.polygon(P([(33, 31), (36, 14), (52, 14), (55, 31)]), fill=c)
    a.shadow(mech, 0, 3, 2.5, 150)
    lay, d = a.draw()
    d.polygon(P([(14, 62), (18, 38), (32, 30), (56, 30), (70, 38), (74, 62)]), fill=(182, 36, 44, 255))
    d.polygon(P([(14, 62), (18, 38), (28, 33), (32, 62)]), fill=(120, 24, 34, 255))
    d.polygon(P([(33, 31), (36, 14), (52, 14), (55, 31)]), fill=(210, 54, 58, 255))
    d.polygon(P([(36, 14), (52, 14), (50, 18), (38, 18)]), fill=(240, 120, 110, 255))
    d.polygon(P([(37, 22), (51, 22), (50, 27), (38, 27)]), fill=(255, 220, 90, 255))
    d.polygon(P([(41, 8), (44, 14), (47, 8)]), fill=(240, 200, 70, 255))               # the crest
    d.rectangle(P([(40, 40), (48, 46)]), fill=(255, 200, 70, 255))
    d.line(P([(24, 42), (24, 60)]), fill=(255, 150, 120, 120), width=S)
    a.over(lay)


def pong(a):
    a.glow(44, 34, 34, 0x3070FF, 0.55)
    lay, d = a.draw()
    for y in range(8, 58, 6):
        d.rectangle(P([(43, y), (45, y + 3)]), fill=(255, 255, 255, 70))
    a.over(lay)
    for i in range(1, 6):                                    # the ball's trail
        a.glow(64 - i * 7, 20 + i * 4.2, 3, 0xFFB030, 0.5 - i * 0.07)
    a.glow(62, 22, 8, 0xFFC040, 0.85)
    lay, d = a.draw()
    d.rounded_rectangle(P([(8, 16), (13, 46)])[0] + P([(8, 16), (13, 46)])[1], 2 * S, fill=(235, 240, 255, 255))
    d.rounded_rectangle(P([(75, 24), (80, 54)])[0] + P([(75, 24), (80, 54)])[1], 2 * S, fill=(235, 240, 255, 255))
    d.ellipse(P([(58.5, 18.5), (65.5, 25.5)])[0] + P([(58.5, 18.5), (65.5, 25.5)])[1], fill=(255, 245, 210, 255))
    a.over(lay)


def shooter(a):
    stars(a, 5, 60, 52)
    a.glow(44, 48, 26, 0x4080FF, 0.55)
    lay, d = a.draw()
    for i, x in enumerate((14, 30, 46, 62)):                 # the invaders
        y = 12 + (i % 2) * 5
        c = (110, 235, 120, 255)
        d.rectangle(P([(x + 2, y + 2), (x + 12, y + 8)]), fill=c)
        d.rectangle(P([(x, y + 4), (x + 14, y + 7)]), fill=c)
        d.rectangle(P([(x + 3, y), (x + 5, y + 3)]), fill=c)
        d.rectangle(P([(x + 9, y), (x + 11, y + 3)]), fill=c)
        d.rectangle(P([(x + 4, y + 4), (x + 6, y + 6)]), fill=(10, 30, 20, 255))
        d.rectangle(P([(x + 8, y + 4), (x + 10, y + 6)]), fill=(10, 30, 20, 255))
        d.rectangle(P([(x + 2, y + 8), (x + 4, y + 10)]), fill=c)
        d.rectangle(P([(x + 10, y + 8), (x + 12, y + 10)]), fill=c)
    d.rectangle(P([(43, 28), (45, 38)]), fill=(255, 230, 120, 255))
    a.over(lay)

    def ship(dr, c):
        dr.polygon(P([(44, 38), (54, 54), (44, 50), (34, 54)]), fill=c)
    a.shadow(ship, 0, 3)
    lay, d = a.draw()
    d.polygon(P([(44, 38), (55, 55), (44, 50), (33, 55)]), fill=(80, 150, 255, 255))
    d.polygon(P([(44, 38), (44, 50), (33, 55)]), fill=(50, 100, 210, 255))
    d.polygon(P([(44, 44), (47, 50), (41, 50)]), fill=(220, 240, 255, 255))
    a.over(lay)


def snake(a):
    a.glow(44, 36, 36, 0xB0F060, 0.5)
    lay, d = a.draw()
    for r in range(0, 6):                                    # the checkered ground
        for c in range(0, 11):
            if (r + c) % 2:
                d.rectangle(P([(c * 8, r * 10), (c * 8 + 8, r * 10 + 10)]), fill=(255, 255, 255, 14))
    a.over(lay)
    pts = []
    for i in range(0, 61):                                   # the body: an S curve
        t = i / 60
        pts.append((14 + t * 56, 40 - math.sin(t * math.pi * 2.2) * 16 + (t - 0.5) * 6))
    a.shadow(lambda dr, c: dr.line(P(pts), fill=c, width=int(9 * S), joint="curve"))
    lay, d = a.draw()
    d.line(P(pts), fill=(36, 140, 60, 255), width=int(9 * S), joint="curve")
    d.line(P([(x, y - 1) for x, y in pts]), fill=(80, 200, 90, 255), width=int(5 * S), joint="curve")
    for i in range(4, 60, 6):
        x, y = pts[i]
        d.ellipse(P([(x - 1.2, y - 1.2), (x + 1.2, y + 1.2)])[0] + P([(x - 1.2, y - 1.2), (x + 1.2, y + 1.2)])[1],
                  fill=(200, 240, 120, 255))
    hx, hy = pts[-1]
    d.ellipse(P([(hx - 6, hy - 6), (hx + 6, hy + 6)])[0] + P([(hx - 6, hy - 6), (hx + 6, hy + 6)])[1], fill=(60, 170, 76, 255))
    for ey in (-3, 3):
        d.ellipse(P([(hx + 1, hy + ey - 1.7), (hx + 4.4, hy + ey + 1.7)])[0] +
                  P([(hx + 1, hy + ey - 1.7), (hx + 4.4, hy + ey + 1.7)])[1], fill=(255, 255, 255, 255))
        d.ellipse(P([(hx + 2.4, hy + ey - 0.8), (hx + 4, hy + ey + 0.8)])[0] +
                  P([(hx + 2.4, hy + ey - 0.8), (hx + 4, hy + ey + 0.8)])[1], fill=(20, 20, 30, 255))
    d.line(P([(hx + 6, hy), (hx + 11, hy)]), fill=(230, 40, 60, 255), width=int(1 * S))
    a.over(lay)
    a.glow(18, 20, 6, 0xFF4050, 0.6)
    lay, d = a.draw()
    d.ellipse(P([(14.5, 16.5), (21.5, 23.5)])[0] + P([(14.5, 16.5), (21.5, 23.5)])[1], fill=(230, 50, 62, 255))
    d.line(P([(18, 16.5), (19.5, 13)]), fill=(90, 60, 30, 255), width=int(1 * S))
    a.over(lay)


def robot(d, cx, top, face, body, accent, dark, s):
    """a boxy titan bust looking at the side of s (+1 right, -1 left)"""
    def X(x):
        return cx + x * s
    d.polygon(P([(X(-14), 62), (X(-12), 40), (X(-6), 36), (X(8), 36), (X(14), 40), (X(14), 62)]), fill=body)
    d.polygon(P([(X(-14), 62), (X(-12), 40), (X(-6), 36), (X(-6), 62)]), fill=dark)
    d.polygon(P([(X(-9), top + 20), (X(-9), top + 4), (X(-5), top), (X(7), top), (X(10), top + 5), (X(10), top + 20),
                 (X(5), top + 24), (X(-5), top + 24)]), fill=face)
    d.polygon(P([(X(-3), top + 7), (X(9), top + 7), (X(9), top + 12), (X(-1), top + 12)]), fill=accent)
    d.polygon(P([(X(-6), top), (X(-2), top - 6), (X(2), top)]), fill=accent)
    d.polygon(P([(X(-4), 44), (X(6), 44), (X(6), 48), (X(-4), 48)]), fill=accent)


def titan(a, ex=False):
    a.glow(44, 34, 40, 0xFFE070 if ex else 0xFF8A40, 0.7)
    stars(a, 17, 22, 26, (255, 230, 190))
    lay, d = a.draw()
    robot(d, 21, 12, (60, 120, 220, 255), (36, 76, 160, 255), (120, 230, 255, 255), (22, 46, 100, 255), 1)
    robot(d, 67, 12, (230, 90, 50, 255), (170, 56, 36, 255), (255, 214, 90, 255), (110, 32, 24, 255), -1)
    a.over(lay)
    a.glow(44, 24, 12, 0xFFFFFF, 0.9)
    lay, d = a.draw()
    pts = []
    for i in range(16):
        r = 11 if i % 2 == 0 else 4.6
        ang = i * math.pi / 8
        pts.append((44 + math.cos(ang) * r, 24 + math.sin(ang) * r))
    d.polygon(P(pts), fill=(255, 238, 150, 255) if not ex else (255, 255, 255, 255))
    if ex:
        d.rounded_rectangle(P([(38, 5), (50, 12)])[0] + P([(38, 5), (50, 12)])[1], 2 * S, fill=(255, 190, 40, 255))
        d.text((44 * S, 8.6 * S), "EX", font=ImageFont.truetype(FONT, 6 * S), fill=(60, 30, 0, 255), anchor="mm")
    a.over(lay)


def typing(a):
    a.glow(44, 32, 34, 0x70E0C0, 0.45)

    def pad(dr, c):
        dr.rounded_rectangle(P([(14, 26), (74, 54)])[0] + P([(14, 26), (74, 54)])[1], 12 * S, fill=c)
    a.shadow(pad)
    lay, d = a.draw()
    pad(d, (44, 52, 78, 255))
    d.rectangle(P([(22, 38), (34, 41)]), fill=(150, 160, 190, 255))
    d.rectangle(P([(26.5, 33.5), (29.5, 45.5)]), fill=(150, 160, 190, 255))
    for (x, y), c in zip(((60, 31), (66, 38), (60, 45), (54, 38)), (0x60E0A0, 0xFF6080, 0x6090FF, 0xFFB040)):
        d.ellipse(P([(x - 3.2, y - 3.2), (x + 3.2, y + 3.2)])[0] + P([(x - 3.2, y - 3.2), (x + 3.2, y + 3.2)])[1],
                  fill=rgb(c) + (255,))
    a.over(lay)
    lay, d = a.draw()                                        # the speech bubble with the typed word
    d.rounded_rectangle(P([(18, 6), (62, 22)])[0] + P([(18, 6), (62, 22)])[1], 4 * S, fill=(248, 250, 255, 255))
    d.polygon(P([(30, 22), (36, 22), (32, 28)]), fill=(248, 250, 255, 255))
    f = ImageFont.truetype(FONT, int(9 * S))
    d.text((38 * S, 14.4 * S), "ciao|", font=f, fill=(40, 50, 90, 255), anchor="mm")
    a.over(lay)


def iso_box(d, x, y, w, h, top, left, right, hw=None):
    """an isometric block: centre-bottom (x, y), half width w, wall height h"""
    t = w / 2
    d.polygon(P([(x, y - t - h), (x + w, y - h), (x, y + t - h), (x - w, y - h)]), fill=top)
    d.polygon(P([(x - w, y - h), (x, y + t - h), (x, y + t), (x - w, y)]), fill=left)
    d.polygon(P([(x + w, y - h), (x, y + t - h), (x, y + t), (x + w, y)]), fill=right)


def yharnam(a):
    stars(a, 23, 26, 28, (210, 230, 240))
    moon(a, 62, 16, 9, 0xE8F0F0, 0x4090A0)
    lay, d = a.draw()
    d.polygon(P([(30, 62), (30, 30), (36, 30), (36, 20), (44, 2), (52, 20), (52, 30), (58, 30), (58, 62)]), fill=(16, 30, 40, 255))
    d.polygon(P([(30, 62), (30, 30), (36, 30), (36, 62)]), fill=(10, 20, 30, 255))
    for x, y in ((40, 36), (48, 36), (44, 44)):
        d.polygon(P([(x - 1.5, y + 5), (x - 1.5, y + 1), (x, y - 2), (x + 1.5, y + 1), (x + 1.5, y + 5)]), fill=(230, 190, 90, 255))
    d.polygon(P([(0, 62), (0, 44), (22, 44), (22, 62)]), fill=(10, 22, 30, 255))
    d.polygon(P([(66, 62), (66, 40), (88, 40), (88, 62)]), fill=(10, 22, 30, 255))
    a.over(lay)
    a.glow(14, 38, 12, 0xFFC060, 0.7)
    lay, d = a.draw()
    d.rectangle(P([(13, 34), (15, 62)]), fill=(8, 14, 20, 255))
    d.rounded_rectangle(P([(10.5, 28), (17.5, 36)])[0] + P([(10.5, 28), (17.5, 36)])[1], 1.5 * S, fill=(255, 214, 120, 255))
    d.polygon(P([(70, 62), (73, 40), (79, 40), (82, 62)]), fill=(6, 10, 16, 255))
    d.ellipse(P([(73.2, 31), (78.8, 38.5)])[0] + P([(73.2, 31), (78.8, 38.5)])[1], fill=(6, 10, 16, 255))
    d.polygon(P([(69, 33), (83, 33), (79.5, 30), (78, 26), (74, 26), (72.5, 30)]), fill=(6, 10, 16, 255))
    a.over(lay)


def herocart(a):
    a.glow(44, 30, 36, 0x60A0FF, 0.6)
    lay, d = a.draw()
    d.polygon(P([(0, 40), (88, 40), (88, 62), (0, 62)]), fill=(34, 44, 76, 255))
    for i in range(-4, 14):
        d.line(P([(44 + (i - 5) * 5, 40), (44 + (i - 5) * 16, 62)]), fill=(80, 100, 150, 120), width=S)
    a.over(lay)
    cols = (0xFF5050, 0x50B0FF, 0xFFD050, 0x60E080, 0xC070FF)
    for row, (y, sc) in enumerate(((40, 0.6), (47, 0.8), (57, 1.05))):
        cnt = (7, 5, 4)[row]
        for k in range(cnt):
            x = 14 + k * (60 / (cnt - 1))
            c = rgb(cols[(k + row) % 5])
            lay, d = a.draw()
            h = 11 * sc
            d.polygon(P([(x - 3.2 * sc, y), (x - 2.4 * sc, y - h * .55), (x + 2.4 * sc, y - h * .55), (x + 3.2 * sc, y)]), fill=c + (255,))
            d.ellipse(P([(x - 2.2 * sc, y - h), (x + 2.2 * sc, y - h * .55)])[0] +
                      P([(x - 2.2 * sc, y - h), (x + 2.2 * sc, y - h * .55)])[1], fill=(250, 220, 190, 255))
            d.polygon(P([(x - 2.6 * sc, y - h * .98), (x, y - h * 1.3), (x + 2.6 * sc, y - h * .98)]), fill=c + (255,))
            a.over(lay)


def bmweb(a):
    a.glow(44, 34, 34, 0x40D0FF, 0.55)

    def win(dr, c):
        dr.rounded_rectangle(P([(12, 10), (76, 56)])[0] + P([(12, 10), (76, 56)])[1], 4 * S, fill=c)
    a.shadow(win)
    lay, d = a.draw()
    win(d, (240, 244, 252, 255))
    d.rounded_rectangle(P([(12, 10), (76, 20)])[0] + P([(12, 10), (76, 20)])[1], 4 * S, fill=(52, 70, 120, 255))
    d.rectangle(P([(12, 16), (76, 20)]), fill=(52, 70, 120, 255))
    for i, c in enumerate((0xFF6060, 0xFFC040, 0x60E080)):
        d.ellipse(P([(16 + i * 5, 13), (19 + i * 5, 16)])[0] + P([(16 + i * 5, 13), (19 + i * 5, 16)])[1], fill=rgb(c) + (255,))
    d.rounded_rectangle(P([(34, 12), (72, 18)])[0] + P([(34, 12), (72, 18)])[1], 2 * S, fill=(230, 236, 250, 255))
    cx, cy, r = 44, 40, 14
    d.ellipse(P([(cx - r, cy - r), (cx + r, cy + r)])[0] + P([(cx - r, cy - r), (cx + r, cy + r)])[1], fill=(60, 130, 230, 255))
    d.polygon(P([(36, 32), (42, 30), (46, 34), (42, 38), (38, 41), (34, 38)]), fill=(90, 200, 120, 255))
    d.polygon(P([(48, 42), (54, 40), (56, 46), (50, 50)]), fill=(90, 200, 120, 255))
    for k in (-8, 0, 8):
        d.arc(P([(cx - r, cy - r), (cx + r, cy + r)])[0] + P([(cx - r, cy - r), (cx + r, cy + r)])[1], 0, 360,
              fill=(255, 255, 255, 0), width=S)
        d.line(P([(cx + k * .1, cy - r), (cx + k * .1, cy + r)]), fill=(255, 255, 255, 0), width=S)
    d.ellipse(P([(cx - r + 6, cy - r), (cx + r - 6, cy + r)])[0] + P([(cx - r + 6, cy - r), (cx + r - 6, cy + r)])[1],
              outline=(220, 240, 255, 255), width=S)
    d.line(P([(cx - r, cy), (cx + r, cy)]), fill=(220, 240, 255, 255), width=S)
    a.over(lay)


def agent(a):
    a.glow(44, 32, 36, 0xB080FF, 0.55)

    def bubble(dr, c):                                       # a chat bubble with its tail at the bottom left
        dr.rounded_rectangle(P([(12, 12), (76, 50)])[0] + P([(12, 12), (76, 50)])[1], 8 * S, fill=c)
        dr.polygon(P([(22, 48), (18, 62), (36, 48)]), fill=c)
    a.shadow(bubble)
    lay, d = a.draw()
    bubble(d, (240, 244, 252, 255))
    for i, c in enumerate((0x7060E0, 0x9070F0, 0xB080FF)):  # three dots: the model is typing
        x = 30 + i * 14
        d.ellipse(P([(x - 4, 27), (x + 4, 35)])[0] + P([(x - 4, 27), (x + 4, 35)])[1], fill=rgb(c) + (255,))
    sx, sy = 70, 14                                          # a spark at the top right
    d.polygon(P([(sx, sy - 9), (sx + 2.5, sy - 2.5), (sx + 9, sy), (sx + 2.5, sy + 2.5), (sx, sy + 9),
                 (sx - 2.5, sy + 2.5), (sx - 9, sy), (sx - 2.5, sy - 2.5)]), fill=(255, 210, 90, 255))
    a.over(lay)


def bench3d(a):
    a.glow(44, 30, 36, 0x60E0FF, 0.5)
    lay, d = a.draw()                                        # the bars of a chart, rising to the right
    for i, (h, c) in enumerate(((10, (70, 150, 230)), (18, (70, 190, 200)), (27, (90, 220, 150)), (36, (250, 200, 80)))):
        x = 12 + i * 12
        d.rectangle(P([(x, 62 - h), (x + 8, 62)]), fill=c + (255,))
    d.line(P([(10, 62.5), (62, 62.5)]), fill=(200, 215, 235, 255), width=S)
    a.over(lay)
    a.shadow(lambda dr, c: iso_box(dr, 66, 42, 12, 16, c, c, c), 1, 2)
    lay, d = a.draw()                                        # the cube in front of the chart
    iso_box(d, 66, 42, 12, 16, (240, 245, 255, 255), (120, 150, 220, 255), (70, 100, 180, 255))
    a.over(lay)


# name -> (title, top colour, bottom colour, emblem)
COVERS = {
    "astrowing": ("Astro Wing", 0x0A1030, 0x2A3A78, astrowing),
    "kitchen": ("Chaos Kitchen", 0x6A2A18, 0xD08038, kitchen),
    "ko2": ("KO-II", 0x10161E, 0x2A3A4E, ko2),
    "nano8": ("nano8", 0x1C1040, 0x4A2A78, nano8),
    "overbit": ("Overbit", 0x2A1A50, 0xE87848, overbit),
    "pong": ("Pong", 0x0A1030, 0x14308A, pong),
    "shooter": ("Star Shooter", 0x040616, 0x14204A, shooter),
    "snake": ("Snake", 0x0C3A28, 0x2A7A44, snake),
    "titan": ("Titan Clash", 0x1A1030, 0x6A2A38, lambda a: titan(a, False)),
    "titan_ex": ("Titan Clash EX", 0x120C28, 0x5A3A18, lambda a: titan(a, True)),
    "typing": ("Pad Typing", 0x0E2A38, 0x2A6A70, typing),
    "yharnam": ("Yharnam", 0x06141E, 0x1A4450, yharnam),
    "herocart": ("Hero Stress", 0x141A3A, 0x2A4A9A, herocart),
    "bmweb": ("bm Web", 0x0E2850, 0x2078B0, bmweb),
    "bench3d": ("3D Bench", 0x0A1A2E, 0x1E4A6E, bench3d),
    "agent": ("bm Agent", 0x1A1240, 0x4A3A9A, agent),
}


def main(names):
    for n in names or COVERS:
        title, top, bottom, fn = COVERS[n]
        a = Art(top, bottom)
        fn(a)
        out = os.path.join(ROOT, "carts", n, "cover.png")
        os.makedirs(os.path.dirname(out), exist_ok=True)
        a.finish(title, out)
        print("wrote", os.path.relpath(out, ROOT))


if __name__ == "__main__":
    main(sys.argv[1:])
