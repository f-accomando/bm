#!/usr/bin/env python3
"""Builds the guide edition: the same game code, with an original tile sheet and level.

    python3 carts/smw/guide/build.py [OUT_DIR]

Everything here is drawn and laid out by this script (flat colour shapes, a small hand-made
level of 4 screens). It reads no ROM and nothing from carts/smw/extracted, so its screenshots
can go into the PDF guide. physics.lua and game.lua are joined unchanged, as in
carts/smw/build/build.py. Default output: build/smw-guide (guide.b16, guide.bme)."""
import os, subprocess, sys
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
COLS, ROWS = 64, 27                  # the level in 16x16 tiles: 4 screens of 16, 27 rows as the game expects
SKY = 0x6CB6FF


def rgb(h):
    return ((h >> 16) & 255, (h >> 8) & 255, h & 255, 255)


# ---- the 16x16 tiles, drawn with flat shapes ------------------------------------------------
def tile_grass():
    t = Image.new("RGBA", (16, 16), rgb(0x9C6B3A)); d = ImageDraw.Draw(t)
    d.rectangle([0, 0, 15, 5], fill=rgb(0x3FA83F)); d.rectangle([0, 0, 15, 1], fill=rgb(0x6BD66B))
    for x in range(0, 16, 4):
        d.polygon([(x, 6), (x + 3, 6), (x + 1, 9)], fill=rgb(0x3FA83F))
    d.point([(3, 12), (10, 11), (13, 14), (6, 14)], fill=rgb(0x7B5128))
    return t


def tile_dirt():
    t = Image.new("RGBA", (16, 16), rgb(0x9C6B3A)); d = ImageDraw.Draw(t)
    d.point([(2, 3), (9, 2), (12, 8), (5, 9), (14, 13), (3, 14), (8, 12)], fill=rgb(0x7B5128))
    return t


def tile_brick():
    t = Image.new("RGBA", (16, 16), rgb(0xC8502A)); d = ImageDraw.Draw(t)
    for y in (0, 5, 10, 15):
        d.line([(0, y), (15, y)], fill=rgb(0x5A1E0E))
    for y, off in ((0, 4), (5, 0), (10, 4)):
        for x in (off, off + 8):
            d.line([(x, y), (x, y + 5)], fill=rgb(0x5A1E0E))
    return t


def tile_question():
    t = Image.new("RGBA", (16, 16), rgb(0xF0B820)); d = ImageDraw.Draw(t)
    d.rectangle([0, 0, 15, 15], outline=rgb(0x8A5A00))
    for p in ((2, 2), (13, 2), (2, 13), (13, 13)):
        d.point(p, fill=rgb(0x8A5A00))
    for p in ((5, 4), (6, 3), (7, 3), (8, 3), (9, 4), (9, 5), (8, 6), (7, 7), (7, 8), (7, 10)):
        d.point(p, fill=rgb(0x7A3E00))
    return t


def tile_pipe(top, right):
    t = Image.new("RGBA", (16, 16), rgb(0x2FA84F)); d = ImageDraw.Draw(t)
    d.rectangle([0, 0, 15, 15], fill=rgb(0x2FA84F))
    edge = 15 if right else 0
    d.line([(edge, 0), (edge, 15)], fill=rgb(0x145A28))
    hi = 12 if not right else 3
    d.line([(hi, 0), (hi, 15)], fill=rgb(0x7BE59B))
    if top:
        d.line([(0, 0), (15, 0)], fill=rgb(0x145A28)); d.line([(0, 15), (15, 15)], fill=rgb(0x145A28))
    return t


def tile_cloud(right):
    t = Image.new("RGBA", (16, 16), (0, 0, 0, 0)); d = ImageDraw.Draw(t)
    d.ellipse([-4 if not right else 2, 4, 14 if not right else 20, 22], fill=rgb(0xFFFFFF))
    d.rectangle([0, 10, 15, 16], fill=rgb(0xFFFFFF))
    d.line([(0, 15), (15, 15)], fill=rgb(0xC8DCF0))
    return t


def tile_bush(right):
    t = Image.new("RGBA", (16, 16), (0, 0, 0, 0)); d = ImageDraw.Draw(t)
    d.ellipse([-6 if not right else 0, 4, 16 if not right else 22, 26], fill=rgb(0x2E8B2E))
    d.ellipse([-2 if not right else 4, 8, 10 if not right else 18, 24], fill=rgb(0x46B546))
    return t


def tile_coin():
    t = Image.new("RGBA", (16, 16), (0, 0, 0, 0)); d = ImageDraw.Draw(t)
    d.ellipse([4, 2, 11, 13], fill=rgb(0xFFD030), outline=rgb(0xA87000))
    d.line([(7, 5), (7, 10)], fill=rgb(0xA87000))
    return t


TILES = [("grass", tile_grass()), ("dirt", tile_dirt()), ("brick", tile_brick()), ("question", tile_question()),
         ("pipe_tl", tile_pipe(True, False)), ("pipe_tr", tile_pipe(True, True)),
         ("pipe_bl", tile_pipe(False, False)), ("pipe_br", tile_pipe(False, True)),
         ("cloud_l", tile_cloud(False)), ("cloud_r", tile_cloud(True)),
         ("bush_l", tile_bush(False)), ("bush_r", tile_bush(True)), ("coin", tile_coin())]
SOLID = {"grass", "dirt", "brick", "question", "pipe_tl", "pipe_tr", "pipe_bl", "pipe_br"}


def build_sheet():
    """Cell 0 is empty. Each tile takes 4 cells in the order the map wants: TL, BL, TR, BR."""
    ncells = 1 + 4 * len(TILES) + 1                  # empty + tiles + the collision marker
    cols = 16
    rows = (ncells + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * 8, rows * 8), (0, 0, 0, 0))
    ids, n = {}, 1
    for name, img in TILES:
        quad = []
        for (cx, cy) in ((0, 0), (0, 1), (1, 0), (1, 1)):     # TL, BL, TR, BR
            sheet.paste(img.crop((cx * 8, cy * 8, cx * 8 + 8, cy * 8 + 8)), ((n % cols) * 8, (n // cols) * 8))
            quad.append(n)
            n += 1
        ids[name] = quad
    return sheet, ids, n                              # n is the collision marker cell (left transparent)


# ---- the level ---------------------------------------------------------------------------------
def build_level():
    g = [[None] * COLS for _ in range(ROWS)]

    def put(x, y, name):
        if 0 <= x < COLS and 0 <= y < ROWS:
            g[y][x] = name

    pit = range(28, 31)
    for x in range(COLS):
        if x in pit:
            continue
        put(x, 24, "grass"); put(x, 25, "dirt"); put(x, 26, "dirt")
    for x, name in zip(range(10, 15), ("brick", "question", "brick", "question", "brick")):
        put(x, 20, name)
    for x in range(16, 20):                           # a higher ledge of bricks
        put(x, 17, "brick")
    put(17, 16, "coin"); put(18, 16, "coin")
    put(22, 22, "pipe_tl"); put(23, 22, "pipe_tr"); put(22, 23, "pipe_bl"); put(23, 23, "pipe_br")
    for i, x in enumerate(range(36, 40)):             # a staircase of bricks
        for y in range(23 - i, 24):
            put(x, y, "brick")
    for x in range(44, 49):
        put(x, 21, "grass")
    put(45, 20, "coin"); put(46, 20, "coin"); put(47, 20, "coin")
    for x in (3, 26, 52, 58):
        put(x, 8, "cloud_l"); put(x + 1, 8, "cloud_r")
    for x in (5, 33, 55):
        put(x, 23, "bush_l"); put(x + 1, 23, "bush_r")
    return g


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else ROOT + "/build/smw-guide"
    os.makedirs(out, exist_ok=True)
    sheet, ids, col = build_sheet()
    sheet.save(out + "/sheet.png")
    grid = build_level()
    main_rows, col_rows = [], []
    for row in grid:
        top, bot, ctop, cbot = [], [], [], []
        for name in row:
            tl, bl, tr, br = ids[name] if name else (0, 0, 0, 0)
            top += [tl, tr]; bot += [bl, br]
            c = col if name in SOLID else 0
            ctop += [c, c]; cbot += [c, c]
        main_rows += [top, bot]; col_rows += [ctop, cbot]
    for name, rows in (("map_main.csv", main_rows), ("map_col.csv", col_rows)):
        open(out + "/" + name, "w").write("\n".join(",".join(map(str, r)) for r in rows) + "\n")
    open(out + "/flags.csv", "w").write("%d=1\n" % col)          # flag 0 = solid, on the marker cell only
    lua = open(ROOT + "/carts/smw/src/physics.lua").read() + "\n" + open(ROOT + "/carts/smw/src/game.lua").read()
    lua = lua.replace("local backdrop = 0xFF0000", "local backdrop = 0x%06X" % SKY)
    open(out + "/main.lua", "w").write(lua)
    base = [sys.executable, ROOT + "/scripts/mkbm.py", "--lua", out + "/main.lua", "--sheet", out + "/sheet.png",
            "--map", out + "/map_main.csv", "--map", "col=" + out + "/map_col.csv", "--flags", out + "/flags.csv",
            "--info", ROOT + "/carts/smw/bm.info", "--title", "SMW Guide Edition", "--targets", "rgb30", "--input", "pad"]
    subprocess.run(base + ["-o", out + "/guide.b16", "--b16"], check=True)
    subprocess.run(base + ["-o", out + "/guide.bme"], check=True)
    print("built", out + "/guide.b16")


if __name__ == "__main__":
    main()
