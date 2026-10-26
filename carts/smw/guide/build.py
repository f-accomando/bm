#!/usr/bin/env python3
"""Builds the guide edition: the same level, the same game code, redrawn graphics.

    python3 carts/smw/guide/build.py [EXTRACTED_DIR] [OUT_DIR]

The level is the real one: the Map16 grid, the collision layer, the camera and the physics are
identical to carts/smw/build/build.py (it reads the same files of scripts/smw_map16.py, so it
needs the player's ROM once). Only the pixels change. From the level picture this script keeps
the shape (where a pixel is opaque) and a coarse colour role (green, earth, yellow, red, blue,
light, grey, dark), then paints each role with its own flat colours and its own light and
shadow from the shape. The shading and the earth speckle are made here. No pixel of the ROM
reaches the output. Run it for screenshots of the PDF guide; the real build is for the
benchmark. Default output: build/smw-guide (guide.bm, guide.bme)."""
import colorsys, importlib.util, json, os, subprocess, sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
spec = importlib.util.spec_from_file_location("smw_real_build", ROOT + "/carts/smw/build/build.py")
real = importlib.util.module_from_spec(spec)
spec.loader.exec_module(real)

# role -> (light, base, shadow)
PAINT = {
    "green": ((0x7BE07B), (0x3FA83F), (0x2A7A2A)),
    "earth": ((0xB8844A), (0x9C6B3A), (0x7B5128)),
    "yellow": ((0xFFE060), (0xF0C020), (0xB08810)),
    "red": ((0xF06850), (0xD04030), (0x982818)),
    "blue": ((0x6890F0), (0x4068D8), (0x2848A0)),
    "light": ((0xFFFFFF), (0xE8E8E8), (0xB0B0B0)),
    "grey": ((0xB0B0B0), (0x909090), (0x606060)),
    "dark": ((0x305030), (0x203020), (0x102010)),
}


def role(px):
    """A coarse colour role from one source pixel. Its brightness is thrown away."""
    r, g, b, a = px
    if a < 128:
        return None
    h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
    h *= 360
    if v < 0.22:
        return "dark"
    if s < 0.18:
        return "light" if v > 0.75 else "grey"
    if 65 <= h < 170:
        return "green"
    if 15 <= h < 48 and s < 0.85 and v < 0.85:
        return "earth"
    if 40 <= h < 70:
        return "yellow"
    if h < 15 or h >= 330:
        return "red"
    if 170 <= h < 270:
        return "blue"
    return "earth"


def restyle(src):
    w, h = src.size
    px = src.load()
    roles = [[role(px[x, y]) for x in range(w)] for y in range(h)]
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    o = out.load()
    for y in range(h):
        for x in range(w):
            rl = roles[y][x]
            if rl is None:
                continue
            up = roles[y - 1][x] if y else None
            dn = roles[y + 1][x] if y + 1 < h else rl
            lf = roles[y][x - 1] if x else None
            rt = roles[y][x + 1] if x + 1 < w else rl
            light, base, shade = PAINT[rl]
            c = base
            if up != rl and rl != "dark":
                c = light                                    # a lit top edge
            elif dn != rl or rt != rl:
                c = shade                                    # the shadow side
            if rl == "earth" and c == base and ((x * 7 + y * 13) % 11 == 0 or (x % 16 * 5 + y % 16 * 3) % 17 == 0):
                c = shade                                    # the speckle of the earth
            o[x, y] = ((c >> 16) & 255, (c >> 8) & 255, c & 255, 255)
    return out


def main():
    ex = sys.argv[1] if len(sys.argv) > 1 else ROOT + "/carts/smw/extracted"
    out = sys.argv[2] if len(sys.argv) > 2 else ROOT + "/build/smw-guide"
    os.makedirs(out, exist_ok=True)
    defs = {int(k, 16): v for k, v in json.load(open(ex + "/map16.json")).items()}
    defs[0x025] = (0, 0, 0, 0)
    level = json.load(open(ex + "/level.json"))
    sheet = Image.open(ex + "/sheet.png").convert("RGBA")
    cols = sheet.width // 8
    cell_img = lambda n: sheet.crop(((n % cols) * 8, (n // cols) * 8, (n % cols) * 8 + 8, (n // cols) * 8 + 8))
    W, H = len(level[0]) * 16, len(level) * 16
    pic = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    for y, row in enumerate(level):
        for x, k in enumerate(row):
            tl, bl, tr, br = defs.get(k, (0, 0, 0, 0))
            for n, (dx, dy) in zip((tl, bl, tr, br), ((0, 0), (0, 8), (8, 0), (8, 8))):
                if n:
                    pic.paste(cell_img(n), (x * 16 + dx, y * 16 + dy))
    new = restyle(pic)
    cells, cell_list = {}, []
    main_rows, col_rows = [], []
    col_n = None
    for cy in range(len(level) * 2):
        mrow, crow = [], []
        for cx in range(len(level[0]) * 2):
            im = new.crop((cx * 8, cy * 8, cx * 8 + 8, cy * 8 + 8))
            key = im.tobytes()
            if im.getchannel("A").getbbox() is None:
                mrow.append(0)
            else:
                if key not in cells:
                    cells[key] = len(cells) + 1
                    cell_list.append(im)
                mrow.append(cells[key])
        main_rows.append(mrow)
    col_n = len(cell_list) + 1
    for y, row in enumerate(level):
        for half in (0, 1):
            crow = []
            for k in row:
                c = (0, col_n, col_n + 1)[real.kind(k)]
                crow += [c, c]
            col_rows.append(crow)
    defs_new = {}                       # the redrawn cells of the used block and the coin, read from the level
    for y, row in enumerate(level):
        for x, k in enumerate(row):
            if k not in defs_new:
                defs_new[k] = (main_rows[2 * y][2 * x], main_rows[2 * y + 1][2 * x], main_rows[2 * y][2 * x + 1],
                               main_rows[2 * y + 1][2 * x + 1])
    from bg import bg_layer                   # the background, redrawn like the rest, as a third layer
    bg_cells, bg_rows, _ = bg_layer(ex, col_n + 2, len(main_rows[0]), restyle=restyle)
    open(out + "/map_bg.csv", "w").write("\n".join(",".join(map(str, r)) for r in bg_rows) + "\n")
    n_cells = col_n + 2 + len(bg_cells)
    sh = Image.new("RGBA", (128, ((n_cells + 15) // 16) * 8), (0, 0, 0, 0))
    for i, im in enumerate(cell_list, 1):
        sh.paste(im, ((i % 16) * 8, (i // 16) * 8))
    for i, im in enumerate(bg_cells):
        n = col_n + 2 + i
        sh.paste(im, ((n % 16) * 8, (n // 16) * 8))
    sh.save(out + "/tiles.png")
    for name, rows in (("map_main.csv", main_rows), ("map_col.csv", col_rows)):
        open(out + "/" + name, "w").write("\n".join(",".join(map(str, r)) for r in rows) + "\n")
    open(out + "/flags.csv", "w").write("%d=1\n%d=2\n" % (col_n, col_n + 1))
    poses_lua = real.pack_zones(ex, out + "/tiles.png", out + "/sheet.png", out + "/sprites.txt", restyle=restyle)
    lua = poses_lua + real.block_data(defs_new, level, col_n) + open(ROOT + "/carts/smw/src/physics.lua").read() + "\n" + open(ROOT + "/carts/smw/src/game.lua").read()
    lua = lua.replace("local backdrop = 0xFF0000", "local backdrop = 0x%06X" % real.backdrop(ex))
    open(out + "/main.lua", "w").write(lua)
    base = [sys.executable, ROOT + "/scripts/mkbm.py", "--lua", out + "/main.lua", "--sheet", out + "/sheet.png", "--sprites", out + "/sprites.txt",
            "--map", out + "/map_main.csv", "--map", "col=" + out + "/map_col.csv", "--map", "bg=" + out + "/map_bg.csv", "--flags", out + "/flags.csv",
            "--info", ROOT + "/carts/smw/bm.info", "--title", "SMW Guide Edition", "--targets", "rgb30", "--input", "pad"]
    subprocess.run(base + ["-o", out + "/guide.bm"], check=True)
    subprocess.run(base + ["-o", out + "/guide.bme"], check=True)
    print("built", out + "/guide.bm", len(cell_list), "cells")


if __name__ == "__main__":
    main()
