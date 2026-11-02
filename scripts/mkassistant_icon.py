#!/usr/bin/env python3
"""bm Assistant's cover: the bm Code icon (art/brand/bm-suite.png) turned
to gold, its </> glyph replaced by a speech bubble with a spark.

    python3 scripts/mkassistant_icon.py     # carts/assistant/icon.png
"""
import os

from PIL import Image, ImageDraw, ImageFilter

import mkicons

ROOT = mkicons.ROOT
S = 512                                   # work size, scaled down at the end
HUE = 42                                  # gold, in 0..255 PIL hue units: 42 = 59 deg


def base():
    sheet = Image.open(mkicons.SHEET).convert("RGB")
    x0 = mkicons.ICONS["code"]
    im = sheet.crop((x0, mkicons.TOP, x0 + mkicons.W, mkicons.TOP + mkicons.H)).resize((S, S), Image.LANCZOS)
    px = im.load()
    # erase the </> glyph: its pixels are bright and saturated green/cyan in the lower right
    bx0, by0, bx1, by1 = int(S * .50), int(S * .55), int(S * .97), int(S * .93)
    ref = im.copy().load()
    for y in range(by0, by1):
        for x in range(bx0, bx1):
            r, g, b = ref[x, y]
            if g > 120 or (g > r + 60 and g > 80):
                # take the background from the left of the glyph, same row, shifted along the gradient
                for dx in range(24, 400, 6):
                    r2, g2, b2 = ref[max(x - dx, 0), y]
                    if g2 < 100 and g2 <= r2 + 60 + 40:
                        px[x, y] = (r2, g2, b2)
                        break
    im = im.filter(ImageFilter.GaussianBlur(0.0))
    h, s, v = im.convert("HSV").split()
    # the bm wordmark is white (low saturation): only the coloured pixels move
    h = h.point(lambda p: HUE)
    gold = Image.merge("HSV", (h, s, v)).convert("RGB")
    return gold


def glyph(im):
    d = ImageDraw.Draw(im)
    x0, y0, x1, y1 = int(S * .56), int(S * .60), int(S * .90), int(S * .86)
    c = (255, 224, 110)
    d.rounded_rectangle((x0, y0, x1, y1), radius=int(S * .06), outline=c, width=int(S * .028))
    tx = x0 + int(S * .06)
    d.polygon([(tx, y1 - 2), (tx + int(S * .05), y1 - 2), (tx - int(S * .02), y1 + int(S * .06))], fill=c)
    # three dots: the answer being written
    cy = (y0 + y1) // 2
    for i in range(3):
        cx = x0 + int((x1 - x0) * (.28 + .22 * i))
        r = int(S * .022)
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=c)
    # a spark at the corner
    sx, sy, k = x1 - int(S * .02), y0 - int(S * .02), int(S * .06)
    d.polygon([(sx, sy - k), (sx + k // 3, sy - k // 3), (sx + k, sy), (sx + k // 3, sy + k // 3),
               (sx, sy + k), (sx - k // 3, sy + k // 3), (sx - k, sy), (sx - k // 3, sy - k // 3)], fill=(255, 250, 220))
    return im


def main():
    im = glyph(base()).resize((mkicons.SIZE, mkicons.SIZE), Image.LANCZOS)
    px = im.load()
    out = im.copy()
    po = out.load()
    for y in range(mkicons.SIZE):
        for x in range(mkicons.SIZE):
            ix, iy = mkicons.inside(x, y)
            if (ix, iy) != (x, y):
                po[x, y] = px[ix, iy]
    path = os.path.join(ROOT, "carts", "assistant", "icon.png")
    out.save(path)
    print("icon:", os.path.relpath(path, ROOT))


if __name__ == "__main__":
    main()
