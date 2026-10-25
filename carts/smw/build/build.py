#!/usr/bin/env python3
"""Builds the Super Mario World cartridge from the files of scripts/smw_map16.py.

    python3 carts/smw/build/build.py [EXTRACTED_DIR] [OUT_DIR]

Defaults: carts/smw/extracted and build/smw. SMW_START=x,y starts Mario elsewhere, for tests. Writes the map, the collision layer and the
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


def pack_poses(ex, tiles_png, out_png, restyle=None):
    """Appends Mario's poses (scripts/smw_sprites.py) under the tile cells, 32x32 each, facing right.
    Returns the Lua table POSES = {[pose] = {sx, sy, w, h, ox, oy}} (ox, oy from Mario's x, y)."""
    from PIL import Image
    sheet = Image.open(tiles_png).convert("RGBA")
    meta = json.load(open(ex + "/mario/poses.json"))
    ids = sorted({m["pose"] for m in meta.values()})
    en_meta = json.load(open(ex + "/enemies/pics.json")) if os.path.exists(ex + "/enemies/pics.json") else {}
    en_names = sorted(k for k, v in en_meta.items() if v["w"] <= 40 and v["h"] <= 40 and v["id"] == 5)
    rows = (len(ids) + 3) // 4 + (len(en_names) + 3) // 4
    big = Image.new("RGBA", (sheet.width, sheet.height + rows * 32), (0, 0, 0, 0))
    big.paste(sheet, (0, 0))
    lines = []
    for i, pose in enumerate(ids):
        m = meta.get("%02X_1" % pose)
        mirror = m is None
        if mirror:
            m = meta["%02X_0" % pose]
        im = Image.open(ex + "/mario/" + m["file"]).convert("RGBA")
        ox = m["ox"]
        if mirror:
            im = im.transpose(Image.FLIP_LEFT_RIGHT)
            ox = 16 - m["ox"] - m["w"]
        if restyle:
            im = restyle(im)
        sx, sy = (i % 4) * 32, sheet.height + (i // 4) * 32
        big.paste(im, (sx, sy))
        lines.append("  [%d] = { %d, %d, %d, %d, %d, %d }," % (pose, sx, sy, m["w"], m["h"], ox, m["oy"]))
    base_row = (len(ids) + 3) // 4
    en_lines = []
    for i, name in enumerate(en_names):
        m = en_meta[name]
        im = Image.open(ex + "/enemies/" + m["file"]).convert("RGBA")
        if restyle:
            im = restyle(im)
        sx, sy = (i % 4) * 32, sheet.height + (base_row + i // 4) * 32
        big.paste(im, (sx, sy))
        en_lines.append("  ['%s'] = { %d, %d, %d, %d, %d, %d }," % (name, sx, sy, m["w"], m["h"], m["ox"], m["oy"]))
    spawns = json.load(open(ex + "/spawns.json")) if os.path.exists(ex + "/spawns.json") else []
    seen, sp = set(), []
    for s in spawns:
        if s["id"] == 5 and (s["x"], s["y"]) not in seen:
            seen.add((s["x"], s["y"]))
            sp.append("  { %d, %d, %d, %d }," % (s["x"], s["y"], s["face"], 1 if s["status"] == 9 else 0))
    big.save(out_png)
    return ("local POSES = {\n" + "\n".join(lines) + "\n}\n"
            "local ENEMY = {\n" + "\n".join(en_lines) + "\n}\n"
            "local KOOPAS = {   -- x, y, face (1 left, 0 right), starts as a shell\n" + "\n".join(sp) + "\n}\n")


def block_data(defs, level):
    """Lua tables for the blocks: the Map16 grid, the used block and the coin. The kinds come from
    scripts/smw_blocks.py: 0x124 gives a coin, 0x126 and 0x11A give an item, 0x11E is a turn block."""
    rows = ",\n".join("{" + ",".join(map(str, r)) + "}" for r in level)
    return ("local L16 = {\n%s\n}\nlocal USED = {%s}\nlocal COIN16 = {%s}\n"
            "local KIND = { [0x124] = 'coin', [0x126] = 'item', [0x11A] = 'item', [0x11E] = 'turn' }\n"
            "local COINS = { [0x02B] = true, [0x02D] = true, [0x02E] = true }\n" % (
                rows, ",".join(map(str, defs.get(0x132, (0, 0, 0, 0)))), ",".join(map(str, defs.get(0x02B, (0, 0, 0, 0))))))


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
    sheet_png = out + "/sheet.png"
    poses_lua = pack_poses(ex, ex + "/sheet.png", sheet_png)
    lua = poses_lua + block_data(defs, level) + open(ROOT + "/carts/smw/src/physics.lua").read() + "\n" + open(ROOT + "/carts/smw/src/game.lua").read()
    lua = lua.replace("local backdrop = 0xFF0000", "local backdrop = 0x%06X" % backdrop(ex))
    if os.environ.get("SMW_START"):         # a test start, "x,y": the shots of a block or an enemy without the walk
        lua = lua.replace("local START_X, START_Y = 16, 352", "local START_X, START_Y = " + os.environ["SMW_START"])
    open(out + "/main.lua", "w").write(lua)
    base = [sys.executable, ROOT + "/scripts/mkbm.py", "--lua", out + "/main.lua", "--sheet", sheet_png,
            "--map", out + "/map_main.csv", "--map", "col=" + out + "/map_col.csv", "--flags", out + "/flags.csv",
            "--info", ROOT + "/carts/smw/bm.info", "--targets", "rgb30", "--input", "pad"]
    subprocess.run(base + ["-o", out + "/smw.b16", "--b16"], check=True)
    subprocess.run(base + ["-o", out + "/smw.bme"], check=True)
    print("built", out + "/smw.b16")


if __name__ == "__main__":
    main()
