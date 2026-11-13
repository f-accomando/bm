#!/usr/bin/env python3
"""The cover of Dark Night: a bmhost screenshot of the courtyard, cropped square, darkened at the edges, with the title.

  cover.py SHOT.png OUT.png

Make the shot with bmhost (F9 twice: the courtyard; SHIFT: the saw open) and pick a frame with no help lines in the crop.
"""
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"


def main(src, out, size=256):
    im = Image.open(src).convert("RGB")
    w, h = im.size
    c = min(w, h) * 0.86
    x0, y0 = (w - c) / 2, h * 0.03
    im = im.crop((int(x0), int(y0), int(x0 + c), int(y0 + c))).resize((size, size), Image.LANCZOS)
    # a cold night grade and a vignette
    r, g, b = im.split()
    im = Image.merge("RGB", (r.point(lambda v: int(v * 0.86)), g.point(lambda v: int(v * 0.92)), b))
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).ellipse((-size * 0.15, -size * 0.1, size * 1.15, size * 1.05), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(size / 8))
    im = Image.composite(im, Image.new("RGB", (size, size), (6, 6, 10)), mask)
    # the title: pale bone over a dark band, a blood-red rule under it
    d = ImageDraw.Draw(im, "RGBA")
    d.rectangle((0, size * 0.72, size, size), fill=(4, 4, 8, 170))
    font = ImageFont.truetype(FONT, int(size * 0.118))
    t = "DARK NIGHT"
    tw = d.textlength(t, font=font)
    y = size * 0.75
    d.text(((size - tw) / 2 + 2, y + 2), t, font=font, fill=(0, 0, 0, 255))
    d.text(((size - tw) / 2, y), t, font=font, fill=(222, 210, 186, 255))
    d.rectangle((size * 0.2, size * 0.93, size * 0.8, size * 0.935), fill=(150, 24, 24, 255))
    im.save(out)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
