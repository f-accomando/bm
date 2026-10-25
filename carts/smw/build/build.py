#!/usr/bin/env python3
"""Builds the Super Mario World cartridge from the files of scripts/smw_map16.py.

    python3 carts/smw/build/build.py [EXTRACTED_DIR] [OUT_DIR]

Defaults: carts/smw/extracted and build/smw. Writes the map, the collision layer and the
flags, joins physics.lua and game.lua into main.lua and calls mkbm.py for the .b16 and the
.bme. The output holds assets from the player's ROM: it stays on the player's machine."""
import json, os, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))


def solid(k):
    """Map16 numbers the player stands on or bumps into (first level only, from the tile graphics)."""
    return 0x100 <= k < 0x1C0 or k in (0x03F, 0x040, 0x041)


def backdrop(ex):
    """Until the parallax layer is extracted: the commonest colour of the sky in the screenshot."""
    from PIL import Image
    from collections import Counter
    im = Image.open(ex + "/screen.png").convert("RGB")
    c = Counter(im.getpixel((x, y)) for x in range(0, 256, 2) for y in range(40, 120, 2))
    r, g, b = c.most_common(1)[0][0]
    return (r << 16) | (g << 8) | b


def main():
    ex = sys.argv[1] if len(sys.argv) > 1 else ROOT + "/carts/smw/extracted"
    out = sys.argv[2] if len(sys.argv) > 2 else ROOT + "/build/smw"
    os.makedirs(out, exist_ok=True)
    defs = {int(k, 16): v for k, v in json.load(open(ex + "/map16.json")).items()}
    level = json.load(open(ex + "/level.json"))
    meta = json.load(open(ex + "/meta.json"))
    defs[0x025] = (0, 0, 0, 0)          # SMW draws the empty cell with a tile the PPU hides
    col = meta["cells"] + 1
    main_rows, col_rows = [], []
    for row in level:
        top, bot, ctop, cbot = [], [], [], []
        for k in row:
            tl, bl, tr, br = defs.get(k, (0, 0, 0, 0))
            top += [tl, tr]
            bot += [bl, br]
            c = col if solid(k) else 0
            ctop += [c, c]
            cbot += [c, c]
        main_rows += [top, bot]
        col_rows += [ctop, cbot]
    for name, rows in (("map_main.csv", main_rows), ("map_col.csv", col_rows)):
        open(out + "/" + name, "w").write("\n".join(",".join(map(str, r)) for r in rows) + "\n")
    open(out + "/flags.csv", "w").write("%d=1\n" % col)
    lua = open(ROOT + "/carts/smw/src/physics.lua").read() + "\n" + open(ROOT + "/carts/smw/src/game.lua").read()
    lua = lua.replace("local backdrop = 0xFF0000", "local backdrop = 0x%06X" % backdrop(ex))
    open(out + "/main.lua", "w").write(lua)
    base = [sys.executable, ROOT + "/scripts/mkbm.py", "--lua", out + "/main.lua", "--sheet", ex + "/sheet.png",
            "--map", out + "/map_main.csv", "--map", "col=" + out + "/map_col.csv", "--flags", out + "/flags.csv",
            "--info", ROOT + "/carts/smw/bm.info", "--targets", "rgb30", "--input", "pad"]
    subprocess.run(base + ["-o", out + "/smw.b16", "--b16"], check=True)
    subprocess.run(base + ["-o", out + "/smw.bme"], check=True)
    print("built", out + "/smw.b16")


if __name__ == "__main__":
    main()
