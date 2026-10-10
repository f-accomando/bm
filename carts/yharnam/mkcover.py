#!/usr/bin/env python3
"""Yharnam: the cover (carts/yharnam/cover.png), 88x88 as the menu's square
covers, made from the game's own sprites (sheet.png and the atlas that
mkassets.py writes into main.lua) with the title in the console's font.

    python3 carts/yharnam/mkcover.py        (needs Pillow; run it again
                                             after mkassets.py changed the sheet)

A night street: the cathedral's spire under the moon, cobbles, a lit lamp and
the hunter looking at the player, "YHARNAM" on top as the other covers have
their title (scripts/mkcovers.py).
"""
import os
import re

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
W = H = 88


def atlas():
    """the sprite boxes of main.lua's atlas: SPR names and the hunter's idle frames"""
    src = open(os.path.join(HERE, "main.lua"), encoding="utf-8").read()
    body = src[src.index("-- [atlas begin]"):src.index("-- [atlas end]")]
    spr = {}
    m = re.search(r"local SPR = \{(.*?)\n\}", body, re.S)
    for name, nums in re.findall(r"(\w+) = \{ ([-\d, ]+) \}", m.group(1)):
        spr[name] = [int(v) for v in nums.split(",")]
    m = re.search(r"local HUNT = \{\n  idle = .*?d = \{\n(.*?)\n  \} \}", body, re.S)
    dirs = [[int(v) for v in f.split(",")[:6]] for f in
            (re.findall(r"\{ \{ ([-\d, ]+) \}", line)[0] for line in m.group(1).splitlines())]
    tiles = {}
    m = re.search(r"local GROUND = \{(.*?)\n\}", body, re.S)
    for name, nums in re.findall(r"(\w+) = \{ ([\d, ]+) \}", m.group(1)):
        tiles[name] = [int(v) for v in nums.split(",")]
    return spr, dirs, tiles


def load_font():
    src = open(os.path.join(ROOT, "src", "gfx", "font8x16.c")).read()
    rows = re.findall(r"\{ (0x[0-9a-f]{2}(?:, 0x[0-9a-f]{2}){15}) \}", src)
    return [bytes(int(b, 16) for b in r.split(", ")) for r in rows]


def text(img, s, x, y, col, outline):
    """the console's 8x16 font, with a one-pixel outline all round"""
    font = load_font()
    px = img.load()

    def glyphs(dx, dy, c):
        for i, ch in enumerate(s):
            g = font[ord(ch)]
            for r in range(16):
                for b in range(8):
                    if g[r] & (0x80 >> b):
                        xx, yy = x + i * 8 + b + dx, y + r + dy
                        if 0 <= xx < W and 0 <= yy < H:
                            px[xx, yy] = c

    for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, -1), (-1, 1), (1, 1), (0, 2)):
        glyphs(dx, dy, outline)
    glyphs(0, 0, col)


def main():
    sheet = Image.open(os.path.join(HERE, "sheet.png")).convert("RGBA")
    spr, dirs, tiles = atlas()

    def cut(box):
        x, y, w, h = box[:4]
        return sheet.crop((x, y, x + w, y + h))

    def put(img, box, fx, fy, shade=1.0):
        """a sprite with its foot point (box[4], box[5]) at (fx, fy)"""
        s = cut(box)
        if shade != 1.0:
            r, g, b, a = s.split()
            s = Image.merge("RGBA", [c.point(lambda v: int(v * shade)) for c in (r, g, b)] + [a])
        img.alpha_composite(s, (fx - box[4], fy - box[5]))

    img = Image.new("RGBA", (W, H))
    d = ImageDraw.Draw(img)
    # the night: deep blue to violet, then the street
    for y in range(H):
        t = y / (H - 1)
        d.line([(0, y), (W, y)], fill=(int(8 + 22 * t), int(10 + 8 * t), int(26 + 14 * t), 255))
    d.ellipse((52, 22, 72, 42), fill=(232, 222, 190, 255))            # the moon, half veiled
    d.ellipse((57, 19, 77, 39), fill=(10, 12, 28, 255))
    # the cathedral's spire and the roofs, dark against the sky
    put(img, spr["spire"], 20, 66, shade=0.35)
    put(img, spr["spire"], 82, 74, shade=0.22)
    # the cobbles of the street
    ground = 60
    for gy in range(ground, H, 16):
        for gx in range(-8, W, 16):
            n = tiles["cobble"][(gx // 16 + gy // 16) % len(tiles["cobble"])]
            tx, ty = (n % 512) * 8, (n // 512) * 8
            img.alpha_composite(sheet.crop((tx, ty, tx + 16, ty + 16)), (gx, gy))
    # the lamp's warm light on the stones and on the hunter
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse((-6, 26, 54, 86), fill=(255, 150, 60, 90))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(9)))
    put(img, spr["lamp"], 16, 82)
    # the hunter, looking at the player (south), a little right of the lamp
    put(img, dirs[0], 52, 84)
    # a darker edge, as a lantern's circle
    vig = Image.new("L", (W, H), 0)
    ImageDraw.Draw(vig).ellipse((-30, -24, W + 30, H + 34), fill=255)
    vig = vig.filter(ImageFilter.GaussianBlur(10))
    dark = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    img = Image.composite(img, dark, vig)
    img = img.convert("RGBA")
    text(img, "YHARNAM", (W - 7 * 8) // 2, 3, (200, 32, 32, 255), (0, 0, 0, 255))
    out = os.path.join(HERE, "cover.png")
    img.convert("RGB").save(out, optimize=True)
    print(out)


if __name__ == "__main__":
    main()
