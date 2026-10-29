#!/usr/bin/env python3
"""The bm font family: a stroke font drawn at the size of each cell (not scaled up from a smaller font).

  python3 scripts/bmfont.py            write src/gfx/font_bm12x32.c and src/gfx/font_bm18x48.c
  python3 scripts/bmfont.py --check    exit 1 if the two files are not what this script writes now
  python3 scripts/bmfont.py --sheet OUT.png   a picture of the whole set at both sizes

The design is the user's choice of 29 Oct 2026 (variant C3, then S1: whole pixels, a pen of 1 and 2 pixels): light, soft, condensed, fixed width. Each glyph is a
skeleton of strokes (a list of points in a unit box) that a round pen draws at 8 times the size; the picture is made
smaller and each pixel is on or off. Only ASCII 32 to 126 is drawn today; the other 161 entries of the code page 437
table are empty. The files have the format of `font_t` (src/gfx/font.h): ceil(width / 8) bytes for each row, MSB = left.
The tool needs Pillow. The generated files are in the repository, so a build does not need it.
The glyphs are original work of the bm project (no font file was used as a source).
"""
import math
import os
import sys

from PIL import Image, ImageDraw

SS = 8
SNAP = True                       # the straight strokes are put on whole pixels with a whole pen (the user's choice S1, 29 Oct 2026)
T, X, B, D = 0.0, 0.36, 1.0, 1.36      # cap top, x-height top, baseline, descender (cap height = 1)
SS = 8


def arc(cx, cy, rx, ry, a0, a1, n=24):
    pts = []
    for i in range(n + 1):
        a = math.radians(a0 + (a1 - a0) * i / n)
        pts.append((cx + rx * math.cos(a), cy + ry * math.sin(a)))
    return pts


def ell(cx, cy, rx, ry):
    return arc(cx, cy, rx, ry, 0, 360, 36)


H = B - X
M = (X + B) / 2
# each glyph: (width factor of the body, [polylines in a unit box: u 0..1, v in cap heights])
G = {}
G['o'] = (1, [ell(.5, M, .5, H / 2)])
G['e'] = (1, [[(0, M), (1, M)] + arc(.5, M, .5, H / 2, 0, -320)[0:0] + [], arc(.5, M, .5, H / 2, 0, -320)])
G['t'] = (.75, [[(.4, .08), (.4, B - .17)] + arc(.7, B - .17, .3, .17, 180, 90, 8)[1:], [(.05, X), (.8, X)]])
G['i'] = (.4, [[(.5, X), (.5, B)], [(.5, .1), (.5, .1)]])
G['n'] = (1, [[(.05, X), (.05, B)], arc(.5, X + .26, .45, .26, 180, 360, 16) + [(.95, B)]])
G['g'] = (1, [ell(.5, M, .5, H / 2), [(.95, X), (.95, D - .2)] + arc(.5, D - .2, .45, .2, 0, 150, 10)[0:]])
G['s'] = (.9, [arc(.5, X + H * .25, .45, H * .25, -30, -270, 24) + arc(.5, X + H * .75, .45, H * .25, -90, 150, 24)])
G['S'] = (1, [arc(.5, .25, .45, .25, -30, -270, 24) + arc(.5, .75, .45, .25, -90, 150, 24)])
G['M'] = (1, [[(.05, B), (.05, T), (.5, .78), (.95, T), (.95, B)]])
G['a'] = (1, [ell(.5, M, .5, H / 2), [(.95, X), (.95, B)]])
G['r'] = (.8, [[(.1, X), (.1, B)], arc(.55, X + .24, .45, .24, 180, 285, 10)])
G['k'] = (.95, [[(.08, T), (.08, B)], [(.92, X), (.08, M + .1)], [(.38, M + .02), (.95, B)]])
G['O'] = (1, [ell(.5, .5, .5, .5)])
G['v'] = (1, [[(.02, X), (.5, B), (.98, X)]])
G['b'] = (1, [[(.1, T), (.1, B)], ell(.52, M, .43, H / 2)])
G['l'] = (.6, [[(.4, T), (.4, B - .18)] + arc(.7, B - .18, .3, .18, 180, 90, 8)[1:]])
G['P'] = (.95, [[(.08, B), (.08, T), (.6, T)] + arc(.6, .27, .35, .27, -90, 90, 14) + [(.08, .54)]])
G['y'] = (1, [[(.05, X), (.5, B + .04)], [(.95, X), (.4, D - .02), (.2, D)]])
G['A'] = (1, [[(.02, B), (.5, T), (.98, B)], [(.2, .68), (.8, .68)]])
G['Q'] = (1, [ell(.5, .5, .5, .5), [(.6, .78), (.96, B + .1)]])
G['0'] = (1, [ell(.5, .5, .43, .5)])
G['1'] = (.75, [[(.1, .22), (.55, T), (.55, B)]])
G['2'] = (1, [arc(.5, .27, .42, .27, -170, 40, 14) + [(.1, B), (.95, B)]])
G['3'] = (1, [arc(.5, .25, .42, .25, -150, 90, 14), arc(.5, .75, .45, .25, -90, 150, 14)])
G['4'] = (1, [[(.75, B), (.75, T), (.05, .7), (.95, .7)]])
G['5'] = (1, [[(.9, T), (.15, T), (.1, .45)] + arc(.5, .72, .42, .28, -125, 150, 18)])
G['6'] = (1, [ell(.5, .68, .43, .32), arc(.92, .68, .84, .68, 180, 252, 10)])
G['7'] = (1, [[(.05, T), (.95, T), (.35, B)]])
G['8'] = (1, [ell(.5, .25, .36, .24), ell(.5, .74, .43, .27)])
G['9'] = (1, [ell(.5, .32, .43, .32), arc(.08, .32, .84, .68, 0, 72, 10)])
G[' '] = (1, [])


def glyph(ch, cw, ch_h, body, stroke):
    """one glyph of a cell cw x ch_h: body = width of an 'o' in pixels, stroke = pen width (one number, or the pair
    vertical stems / horizontal bars) in pixels; the pen is an ellipse, so the ends are round"""
    wf, lines = G[ch]
    px, py = stroke if isinstance(stroke, tuple) else (stroke, stroke)
    img = Image.new('L', (cw * SS, ch_h * SS), 0)
    d = ImageDraw.Draw(img)
    top = ch_h * .12
    capp = ch_h * .62                       # cap height in pixels
    bw = body * wf
    x0 = (cw - bw) / 2
    wx, wy = max(1, round(px)), max(1, round(py))
    if SNAP:
        px, py = wx, wy
    rx, ry = px * SS / 2, py * SS / 2

    def p(u, v):
        return ((x0 + u * (bw - px) + px / 2) * SS, (top + py / 2 + v * (capp - py)) * SS)

    def stamp(x, y):
        d.ellipse([x - rx, y - ry, x + rx, y + ry], fill=255)

    for line in lines:
        pts = [p(u, v) for u, v in line]
        if SNAP and len(line) > 1:                      # straight strokes sit on whole pixels, with a whole pen
            for i, (u, v) in enumerate(line):
                near = [line[k] for k in (i - 1, i + 1) if 0 <= k < len(line)]
                x, y = pts[i]
                if any(q[0] == u for q in near):
                    x = (round(x / SS - wx / 2) + wx / 2) * SS
                if any(q[1] == v for q in near):
                    y = (round(y / SS - wy / 2) + wy / 2) * SS
                pts[i] = (x, y)
        if all(q == pts[0] for q in pts):               # a dot: a little bigger than the pen
            x, y = pts[0]
            d.ellipse([x - rx * 1.5, y - ry * 1.5, x + rx * 1.5, y + ry * 1.5], fill=255)
            continue
        stamp(*pts[0])
        for a, b in zip(pts, pts[1:]):
            n = max(1, int(math.hypot(b[0] - a[0], b[1] - a[1]) / 2))
            for k in range(1, n + 1):
                stamp(a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n)
    small = img.resize((cw, ch_h), Image.BOX)
    return small.point(lambda v: 255 if v >= 128 else 0)


# ---- capitals
G['B'] = (.95, [[(.08, B), (.08, T), (.5, T)] + arc(.5, .25, .35, .25, -90, 90, 12) + [(.08, .5)],
                [(.5, .5), (.58, .5)] + arc(.58, .75, .35, .25, -90, 90, 12) + [(.08, B)]])
G['C'] = (1, [arc(.52, .5, .45, .5, -40, -320, 28)])
G['D'] = (1, [[(.08, T), (.08, B), (.42, B)] + arc(.42, .5, .5, .5, 90, -90, 20) + [(.08, T)]])
G['E'] = (.9, [[(.92, T), (.1, T), (.1, B), (.92, B)], [(.1, .5), (.78, .5)]])
G['F'] = (.9, [[(.92, T), (.1, T), (.1, B)], [(.1, .5), (.78, .5)]])
G['G'] = (1, [arc(.5, .5, .45, .5, -35, -360, 30) + [(.55, .5)]])
G['H'] = (1, [[(.08, T), (.08, B)], [(.92, T), (.92, B)], [(.08, .5), (.92, .5)]])
G['I'] = (.7, [[(.5, T), (.5, B)], [(.2, T), (.8, T)], [(.2, B), (.8, B)]])
G['J'] = (.85, [[(.8, T), (.8, B - .2)] + arc(.45, B - .2, .35, .2, 0, 160, 10)])
G['K'] = (1, [[(.08, T), (.08, B)], [(.92, T), (.08, .62)], [(.35, .48), (.94, B)]])
G['L'] = (.9, [[(.1, T), (.1, B), (.92, B)]])
G['N'] = (1, [[(.08, B), (.08, T), (.92, B), (.92, T)]])
G['R'] = (1, [[(.08, B), (.08, T), (.6, T)] + arc(.6, .27, .35, .27, -90, 90, 14) + [(.08, .54)], [(.5, .54), (.95, B)]])
G['T'] = (1, [[(.05, T), (.95, T)], [(.5, T), (.5, B)]])
G['U'] = (1, [[(.08, T), (.08, .7)] + arc(.5, .7, .42, .3, 180, 0, 16) + [(.92, T)]])
G['V'] = (1, [[(.02, T), (.5, B), (.98, T)]])
G['W'] = (1, [[(0, T), (.25, B), (.5, .4), (.75, B), (1, T)]])
G['X'] = (1, [[(.05, T), (.95, B)], [(.95, T), (.05, B)]])
G['Y'] = (1, [[(.05, T), (.5, .5), (.95, T)], [(.5, .5), (.5, B)]])
G['Z'] = (1, [[(.08, T), (.92, T), (.08, B), (.92, B)]])
# ---- small letters
G['c'] = (.95, [arc(.52, M, .46, H / 2, -40, -320, 24)])
G['d'] = (1, [ell(.48, M, .43, H / 2), [(.92, T), (.92, B)]])
G['f'] = (.8, [[(.4, B), (.4, .2)] + arc(.7, .2, .3, .2, 180, 270, 8), [(.1, X), (.75, X)]])
G['h'] = (1, [[(.08, T), (.08, B)], arc(.5, X + .26, .42, .26, 180, 360, 16) + [(.92, B)]])
G['j'] = (.6, [[(.6, X), (.6, D - .2)] + arc(.25, D - .2, .35, .2, 0, 110, 8), [(.6, .1), (.6, .1)]])
G['m'] = (1, [[(.05, X), (.05, B)], arc(.275, X + .22, .225, .22, 180, 360, 10) + [(.5, B)],
              arc(.725, X + .22, .225, .22, 180, 360, 10) + [(.95, B)]])
G['p'] = (1, [ell(.52, M, .43, H / 2), [(.08, X), (.08, D)]])
G['q'] = (1, [ell(.48, M, .43, H / 2), [(.92, X), (.92, D)]])
G['u'] = (1, [[(.08, X), (.08, B - .26)] + arc(.5, B - .26, .42, .26, 180, 0, 16) + [(.92, X)], [(.92, X), (.92, B)]])
G['w'] = (1, [[(0, X), (.25, B), (.5, X + .25), (.75, B), (1, X)]])
G['x'] = (1, [[(.05, X), (.95, B)], [(.95, X), (.05, B)]])
G['z'] = (1, [[(.08, X), (.92, X), (.08, B), (.92, B)]])
# ---- punctuation and symbols
G['.'] = (.4, [[(.5, B), (.5, B)]])
G[','] = (.4, [[(.55, B), (.3, B + .22)]])
G[':'] = (.4, [[(.5, M), (.5, M)], [(.5, B), (.5, B)]])
G[';'] = (.4, [[(.55, M), (.55, M)], [(.55, B), (.3, B + .22)]])
G['!'] = (.4, [[(.5, T), (.5, .7)], [(.5, B), (.5, B)]])
G['?'] = (.85, [arc(.5, .25, .4, .25, -180, 45, 14) + [(.5, .55), (.5, .72)], [(.5, B), (.5, B)]])
G['-'] = (.7, [[(.12, .58), (.88, .58)]])
G['_'] = (1, [[(0, 1.12), (1, 1.12)]])
G['+'] = (.85, [[(.5, .3), (.5, .82)], [(.1, .56), (.9, .56)]])
G['='] = (.85, [[(.1, .42), (.9, .42)], [(.1, .7), (.9, .7)]])
G['/'] = (.8, [[(.9, T), (.1, B)]])
G['\\'] = (.8, [[(.1, T), (.9, B)]])
G['('] = (.7, [arc(1.0, .5, .9, .66, 232, 128, 14)])
G[')'] = (.7, [arc(0.0, .5, .9, .66, -52, 52, 14)])
G['['] = (.55, [[(.75, T - .04), (.3, T - .04), (.3, B + .06), (.75, B + .06)]])
G[']'] = (.55, [[(.25, T - .04), (.7, T - .04), (.7, B + .06), (.25, B + .06)]])
G['{'] = (.6, [[(.8, T - .04), (.55, T - .04), (.55, .4), (.25, .5), (.55, .6), (.55, B + .06), (.8, B + .06)]])
G['}'] = (.6, [[(.2, T - .04), (.45, T - .04), (.45, .4), (.75, .5), (.45, .6), (.45, B + .06), (.2, B + .06)]])
G['<'] = (.85, [[(.88, .2), (.12, .5), (.88, .8)]])
G['>'] = (.85, [[(.12, .2), (.88, .5), (.12, .8)]])
G['*'] = (.7, [[(.5, .12), (.5, .56)], [(.15, .24), (.85, .44)], [(.85, .24), (.15, .44)]])
G["'"] = (.4, [[(.5, T), (.5, .28)]])
G['"'] = (.6, [[(.28, T), (.28, .28)], [(.72, T), (.72, .28)]])
G['|'] = (.4, [[(.5, T - .06), (.5, D)]])
G['~'] = (1, [[(.06 + .88 * i / 20, .52 - .09 * math.sin(2 * math.pi * i / 20)) for i in range(21)]])
G['^'] = (.8, [[(.15, .32), (.5, T), (.85, .32)]])
G['`'] = (.5, [[(.3, T), (.65, .24)]])
G['#'] = (1, [[(.38, T), (.26, B)], [(.76, T), (.64, B)], [(.1, .36), (.92, .36)], [(.06, .72), (.88, .72)]])
G['%'] = (1, [ell(.25, .2, .2, .18), ell(.75, .8, .2, .18), [(.9, T), (.1, B)]])
G['&'] = (1, [[(.95, B), (.25, .46), (.28, .28), (.5, T), (.72, .28), (.25, .7), (.3, .92), (.55, B), (.75, .9), (.92, .62)]])
G['@'] = (1, [arc(.5, .5, .5, .55, -20, -340, 28), ell(.52, .55, .2, .2), [(.72, .38), (.72, .72)]])
G['$'] = (.9, [arc(.5, .25, .4, .25, -30, -270, 20) + arc(.5, .75, .4, .25, -90, 150, 20), [(.5, T - .1), (.5, B + .1)]])

def text_img(s, cw, ch_h, body, stroke):
    im = Image.new('L', (cw * len(s), ch_h), 0)
    for i, c in enumerate(s):
        im.paste(glyph(c, cw, ch_h, body, stroke), (i * cw, 0))
    return im


# ---- the choices of the user, 29 Oct 2026: a with a hook on top, l with a tail, 0 with a bar, 1 without a base
G['a'] = (1, [arc(.5, X + .17, .42, .17, -170, 0, 8) + [(.92, B)], ell(.46, B - .2, .42, .2)])
G['0'] = (1, [ell(.5, .5, .43, .5), [(.78, .14), (.22, .86)]])

# the cells: (name, width, height, width of an "o" in pixels, pen in pixels)
SIZES = [('12x32', 12, 32, 9, 1), ('18x48', 18, 48, 13, 2)]
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'src', 'gfx')


def bits(ch, cw, chh, body, pen):
    """the rows of one glyph: a list of chh lists of cw values (0 or 1)"""
    im = glyph(ch, cw, chh, body, pen)
    return [[1 if im.getpixel((x, y)) else 0 for x in range(cw)] for y in range(chh)]


def c_file(name, cw, chh, body, pen):
    bpr = (cw + 7) // 8
    out = ["/*",
           " * bm console font %s: the bm font family (variant C3), drawn at this size." % name,
           " * Generated by scripts/bmfont.py - do not edit.",
           " * Glyphs are in code page 437 order; only ASCII 32 to 126 is drawn, the other entries are empty.",
           " * %d byte(s) for each row, MSB = left." % bpr,
           " */",
           '#include "font.h"', "",
           "static const uint8_t glyphs[256][%d] = {" % (bpr * chh)]
    for code in range(256):
        rows = bits(chr(code), cw, chh, body, pen) if 32 <= code <= 126 else [[0] * cw for _ in range(chh)]
        data = []
        for r in rows:
            for k in range(bpr):
                v = 0
                for x in range(8):
                    xx = k * 8 + x
                    v = v << 1 | (r[xx] if xx < cw else 0)
                data.append('0x%02x' % v)
        out.append("    { %s }, /* 0x%02x */" % (", ".join(data), code))
    out += ["};", "", "const font_t font_bm_%s = {" % name,
            "    .width = %d, .height = %d, .glyphs = &glyphs[0][0]," % (cw, chh), "};", ""]
    return "\n".join(out)


def main():
    check = '--check' in sys.argv
    if '--sheet' in sys.argv:
        path = sys.argv[sys.argv.index('--sheet') + 1]
        pages = [" !\"#$%&'()*+,-./", "0123456789:;<=>?", "@ABCDEFGHIJKLMNO", "PQRSTUVWXYZ[\\]^_",
                 "`abcdefghijklmno", "pqrstuvwxyz{|}~"]
        ims = []
        for _, cw, chh, body, pen in SIZES:
            for l in pages:
                ims.append(text_img(l, cw, chh, body, pen))
        W = max(i.width for i in ims) + 20
        page = Image.new('RGB', (W, sum(i.height + 3 for i in ims) + 20), (24, 26, 34))
        y = 10
        for i in ims:
            col = Image.new('RGB', i.size, (24, 26, 34))
            col.paste((235, 238, 245), mask=i)
            page.paste(col, (10, y))
            y += i.height + 3
        page.save(path)
        return 0
    bad = 0
    for name, cw, chh, body, pen in SIZES:
        path = os.path.join(OUT, 'font_bm%s.c' % name)
        text = c_file(name, cw, chh, body, pen)
        if check:
            old = open(path, encoding='utf-8', newline='').read() if os.path.exists(path) else ''
            if old.replace('\r\n', '\n') != text:
                print('out of date: ' + path)
                bad = 1
        else:
            open(path, 'w', encoding='utf-8', newline='\n').write(text)
            print('wrote ' + path)
    return bad


if __name__ == '__main__':
    sys.exit(main())
