#!/usr/bin/env python3
"""Builds the Super Mario World cartridge from the files of scripts/smw_map16.py.

    python3 carts/smw/build/build.py [EXTRACTED_DIR] [OUT_DIR]

Defaults: carts/smw/extracted and build/smw. SMW_START=x,y starts Mario elsewhere, for tests. Writes the map, the collision layer and the
flags, joins physics.lua and game.lua into main.lua and calls mkbm.py for the .b16 and the
.bme. The output holds assets from the player's ROM: it stays on the player's machine."""
import json, os, subprocess, sys
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


def pack_poses(ex, tiles_png, out_png, restyle=None):
    """Appends Mario's poses (scripts/smw_sprites.py) under the tile cells, 32x32 each, facing right.
    Returns the Lua table POSES = {[pose] = {sx, sy, w, h, ox, oy}} (ox, oy from Mario's x, y)."""
    from PIL import Image
    sheet = Image.open(tiles_png).convert("RGBA")
    meta = json.load(open(ex + "/mario/poses.json"))
    ids = sorted({m["pose"] for m in meta.values()})
    en_meta = json.load(open(ex + "/enemies/pics.json")) if os.path.exists(ex + "/enemies/pics.json") else {}
    en_names = sorted(k for k, v in en_meta.items() if v["w"] <= 40 and v["h"] <= 40 and v["id"] == 5)
    tape_json = ex + "/goal/goal.json"
    rows = (len(ids) + 3) // 4 + (len(en_names) + 3) // 4 + (1 if os.path.exists(tape_json) else 0)
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
    tape_lua = "local TAPE = nil\n"
    if os.path.exists(tape_json):
        t = json.load(open(tape_json))
        im = Image.open(ex + "/goal/tape.png").convert("RGBA")
        if restyle:
            im = restyle(im)
        sx, sy = 0, sheet.height + (base_row + (len(en_names) + 3) // 4) * 32
        big.paste(im, (sx, sy))
        tape_lua = "local TAPE = { %d, %d, %d, %d, %d, %d, %d }   -- sx, sy, w, h, ox, oy, level x\n" % (
            sx, sy, t["w"], t["h"], t["ox"], t["oy"], t["x"])
    spawns = json.load(open(ex + "/spawns.json")) if os.path.exists(ex + "/spawns.json") else []
    seen, sp = set(), []
    for s in spawns:
        if s["id"] == 5 and (s["x"], s["y"]) not in seen:
            seen.add((s["x"], s["y"]))
            sp.append("  { %d, %d, %d, %d }," % (s["x"], s["y"], s["face"], 1 if s["status"] == 9 else 0))
    big.save(out_png)
    return ("local POSES = {\n" + "\n".join(lines) + "\n}\n"
            "local ENEMY = {\n" + "\n".join(en_lines) + "\n}\n"
            + tape_lua +
            "local KOOPAS = {   -- x, y, face (1 left, 0 right), starts as a shell\n" + "\n".join(sp) + "\n}\n")


def block_data(defs, level):
    """Lua tables for the blocks: the Map16 grid, the used block and the coin. The kinds come from
    scripts/smw_blocks.py: 0x124 gives a coin, 0x126 and 0x11A give an item, 0x11E is a turn block."""
    rows = ",\n".join("{" + ",".join(map(str, r)) + "}" for r in level)
    ids = sorted({k for r in level for k in r})
    m16 = ",".join("[%d]={%s}" % (k, ",".join(map(str, defs.get(k, (0, 0, 0, 0))))) for k in ids)
    return ("local M16 = {%s}\nlocal L16 = {\n%s\n}\nlocal USED = {%s}\nlocal COIN16 = {%s}\n"
            "local KIND = { [0x124] = 'coin', [0x126] = 'item', [0x11A] = 'item', [0x11E] = 'turn' }\n"
            "local COINS = { [0x02B] = true, [0x02D] = true, [0x02E] = true }\n" % (
                m16, rows, ",".join(map(str, defs.get(0x132, (0, 0, 0, 0)))), ",".join(map(str, defs.get(0x02B, (0, 0, 0, 0))))))


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
