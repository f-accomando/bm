#!/usr/bin/env python3
"""Builds the Super Mario World cartridge from the files of scripts/smw_map16.py.

    python3 carts/smw/build/build.py [EXTRACTED_DIR] [OUT_DIR]

Defaults: carts/smw/extracted and build/smw. SMW_START=x,y starts Mario elsewhere, for tests. Writes the map, the collision layer and the
flags, joins physics.lua and game.lua into main.lua and calls mkbm.py for the .b16 and the
.bme. The output holds assets from the player's ROM: it stays on the player's machine."""
import json, os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sprites import pack_zones
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))


# Tested in the emulator (scripts/smw_solid.py): the ledge tiles are platforms (a floor from above only,
# Mario goes through them from below and from the side), the blocks and pipes are solid on every side,
# the tiles under 0x100 (hills, bushes, the goal posts) are scenery.
PLATFORM = {0x100, 0x101, 0x102, 0x103, 0x104, 0x106}
FULL = {0x11A, 0x11E, 0x124, 0x126, 0x132, 0x133, 0x134, 0x135, 0x136, 0x137, 0x138, 0x145, 0x146, 0x148, 0x14B, 0x14C}


def kind(k):
    """0 scenery, 1 solid, 2 platform: the flag bit of the collision layer's marker cell."""
    return 1 if k in FULL else (2 if k in PLATFORM else 0)


def backdrop(ex):
    """Until the parallax layer is extracted: the commonest colour of the sky in the screenshot."""
    from PIL import Image
    from collections import Counter
    im = Image.open(ex + "/screen.png").convert("RGB")
    c = Counter(im.getpixel((x, y)) for x in range(0, 256, 2) for y in range(40, 120, 2))
    r, g, b = c.most_common(1)[0][0]
    return (r << 16) | (g << 8) | b


def block_data(defs, level, col):
    """Lua tables for the blocks: the Map16 grid, the used block and the coin. The kinds come from
    scripts/smw_blocks.py: 0x124 gives a coin, 0x126 a Yoshi egg, 0x11A a vine, 0x11E is a turn block."""
    rows = ",\n".join("{" + ",".join(map(str, r)) + "}" for r in level)
    ids = sorted({k for r in level for k in r} | {0x02B, 0x11E, 0x25})
    colcell = ",".join("[%d]=%d" % (k, (0, col, col + 1)[kind(k)]) for k in ids)
    m16 = ",".join("[%d]={%s}" % (k, ",".join(map(str, defs.get(k, (0, 0, 0, 0))))) for k in ids)
    return ("local COLCELL = {%s}\nlocal M16 = {%s}\nlocal L16 = {\n%s\n}\nlocal USED = {%s}\nlocal COIN16 = {%s}\n"
            "local KIND = { [0x124] = 'coin', [0x126] = 'egg', [0x11A] = 'vine', [0x11E] = 'turn' }\n"
            "local COINS = { [0x02B] = true }\nlocal DRAGON_TOP, DRAGON_BOT = 0x02D, 0x02E       -- a dragon coin is two Map16 blocks, one above the other\n" % (
                colcell, m16, rows, ",".join(map(str, defs.get(0x132, (0, 0, 0, 0)))), ",".join(map(str, defs.get(0x02B, (0, 0, 0, 0))))))


def main():
    ex = sys.argv[1] if len(sys.argv) > 1 else ROOT + "/carts/smw/extracted"
    out = sys.argv[2] if len(sys.argv) > 2 else ROOT + "/build/smw"
    os.makedirs(out, exist_ok=True)
    defs = {int(k, 16): v for k, v in json.load(open(ex + "/map16.json")).items()}
    level = json.load(open(ex + "/level.json"))
    meta = json.load(open(ex + "/meta.json"))
    defs[0x025] = (0, 0, 0, 0)          # SMW draws the empty cell with a tile the PPU hides
    col = meta["cells"] + 1             # two marker cells after the tiles: col (solid, flag 1), col + 1 (platform, flag 2)
    main_rows, col_rows = [], []
    for row in level:
        top, bot, ctop, cbot = [], [], [], []
        for k in row:
            tl, bl, tr, br = defs.get(k, (0, 0, 0, 0))
            top += [tl, tr]
            bot += [bl, br]
            c = (0, col, col + 1)[kind(k)]
            ctop += [c, c]
            cbot += [c, c]
        main_rows += [top, bot]
        col_rows += [ctop, cbot]
    for name, rows in (("map_main.csv", main_rows), ("map_col.csv", col_rows)):
        open(out + "/" + name, "w").write("\n".join(",".join(map(str, r)) for r in rows) + "\n")
    open(out + "/flags.csv", "w").write("%d=1\n%d=2\n" % (col, col + 1))
    sheet_png = out + "/sheet.png"
    poses_lua = pack_zones(ex, ex + "/sheet.png", sheet_png, out + "/sprites.txt")
    lua = poses_lua + block_data(defs, level, col) + open(ROOT + "/carts/smw/src/physics.lua").read() + "\n" + open(ROOT + "/carts/smw/src/game.lua").read()
    lua = lua.replace("local backdrop = 0xFF0000", "local backdrop = 0x%06X" % backdrop(ex))
    if os.environ.get("SMW_START"):         # a test start, "x,y": the shots of a block or an enemy without the walk
        lua = lua.replace("local START_X, START_Y = 16, 352", "local START_X, START_Y = " + os.environ["SMW_START"])
    open(out + "/main.lua", "w").write(lua)
    base = [sys.executable, ROOT + "/scripts/mkbm.py", "--lua", out + "/main.lua", "--sheet", sheet_png, "--sprites", out + "/sprites.txt",
            "--map", out + "/map_main.csv", "--map", "col=" + out + "/map_col.csv", "--flags", out + "/flags.csv",
            "--info", ROOT + "/carts/smw/bm.info", "--targets", "rgb30", "--input", "pad"]
    subprocess.run(base + ["-o", out + "/smw.b16", "--b16"], check=True)
    subprocess.run(base + ["-o", out + "/smw.bme"], check=True)
    print("built", out + "/smw.b16")


if __name__ == "__main__":
    main()
