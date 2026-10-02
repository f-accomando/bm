#!/usr/bin/env python3
"""Writes carts/nano8/roms/yharnam8.p8, Yharnam 8: the hunt of Yharnam made
small, to the measure of a PICO-8 cartridge (128 x 128, 16 colours, 8192
tokens, the sprite sheet and map of the format). Run by nano8.

  python3 carts/yharnam/pico8/mk.py [--png PREVIEW.png] [--label PICTURE] [--check]

The sprites and tiles are in art.py and tiles.py, the map in maps.py, the
sounds in sfx.py, the code in game.lua; here they are put in their places.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import art      # noqa: E402
import tiles    # noqa: E402
import maps     # noqa: E402
import sfx      # noqa: E402
import tokens   # noqa: E402

OUT = os.path.join(HERE, "..", "..", "nano8", "roms", "yharnam8.p8")

# ---------------------------------------------------------------- the sheet
# sprites 0-63: the map's tiles (0 is the void); 64-127: the creatures;
# 128-255 are the lower half of the map
TILE = {None: 0}
for n in tiles.GROUND:
    TILE[n] = len(TILE)
for n in tiles.DRAWN:
    TILE[n] = len(TILE)
assert len(TILE) <= 64, len(TILE)
FLAGS = [0] * 256
FLAGS[0] = tiles.SOLID
for n, (rows, f) in tiles.DRAWN.items():
    FLAGS[TILE[n]] = f

# the creatures: 8 x 16 frames are two tiles one over the other (n, n+16)
TALL = {64: art.HUNTER["down"], 65: art.HUNTER["down_walk"], 66: art.HUNTER["up"], 67: art.HUNTER["up_walk"],
        68: art.HUNTER["side"], 69: art.HUNTER["side_walk"], 70: art.FOES["villager"],
        71: art.FOES["villager_b"], 72: art.FOES["beast"], 73: art.FOES["beast_b"]}
SMALL = {74: art.FOES["kin"], 75: art.FOES["kin_b"], 90: art.ROLL["roll"], 91: art.ROLL["roll_b"],
         78: art.ICON["echo"]}
WIDE = {76: art.ROLL["dead"]}
BIG = {96: art.BOSSES["butcher"], 98: art.BOSSES["butcher_b"], 100: art.BOSSES["hound"],
       102: art.BOSSES["hound_b"], 104: art.BOSSES["father"], 106: art.BOSSES["father_b"],
       108: art.BOSSES["watcher"], 110: art.BOSSES["watcher_b"]}


def sheet(world):
    px = [[0] * 128 for _ in range(128)]

    def blit(n, rows):
        ox, oy = n % 16 * 8, n // 16 * 8
        for y, row in enumerate(rows):
            for x, ch in enumerate(row):
                px[oy + y][ox + x] = 0 if ch == "." else int(ch, 16)
    for n, rows in tiles.GROUND.items():
        blit(TILE[n], rows)
    for n, (rows, f) in tiles.DRAWN.items():
        blit(TILE[n], rows)
    for d in (TALL, SMALL, WIDE, BIG):
        for n, rows in d.items():
            blit(n, rows)
    # the lower half of the map, a byte a cell (low nibble: the left pixel)
    for y in range(32, 64):
        for x in range(128):
            v = TILE[world.t[y][x]]
            k = (y - 32) * 128 + x
            gy, gx = 64 + k // 64, (k % 64) * 2
            px[gy][gx], px[gy][gx + 1] = v & 15, v >> 4
    return px


def lua_data(world):
    """the creatures' places and the gates, as strings for the code"""
    kinds = {"start": 0, "villager": 1, "rifle": 2, "beast": 3, "hunter": 4, "kin": 5,
             "butcher": 6, "hound": 7, "father": 8, "watcher": 9}
    sp = []
    for k, x, y in world.spawns:
        sp += [kinds[k], x, y]
    gt = []
    for g in world.gates:
        gt += list(g)
    return ",".join(map(str, sp)), ",".join(map(str, gt))


def code(world):
    src = open(os.path.join(HERE, "game.lua"), encoding="utf-8").read()
    sp, gt = lua_data(world)
    src = src.replace("$SPAWNS", sp).replace("$GATES", gt)
    src = src.replace("$GLOW", str(glow_bits()))
    for k, n in (("$BRAZIER", "brazier"), ("$PYRE", "pyre_tl"), ("$SHRINE", "shrine_b")):
        src = src.replace(k, str(TILE[n]))
    return src


def glow_bits():
    """palt() bits while the lit things are drawn again: all transparent
    but the colours of light (bit 15 is colour 0)"""
    keep = {7, 9, 10, 12}
    return sum(1 << (15 - c) for c in range(16) if c not in keep)


def check(world):
    """every creature on open ground, and all of them, the lamps and the
    gates reachable from the start (the gates open)"""
    def walk(x, y):
        return 0 <= x < 128 and 0 <= y < 64 and world.t[y][x] in tiles.GROUND
    start = [(x, y) for k, x, y in world.spawns if k == "start"][0]
    seen, todo = {start}, [start]
    while todo:
        x, y = todo.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (x + dx, y + dy)
            if n not in seen and walk(*n):
                seen.add(n)
                todo.append(n)
    bad = [(k, x, y) for k, x, y in world.spawns if (x, y) not in seen]
    for y in range(64):
        for x in range(128):
            if world.t[y][x] == "shrine_b" and (x, y + 1) not in seen:
                bad.append(("lamp", x, y))
    assert not bad, "not reachable: %s" % bad


# the 32 colours of the format (the extra ones 128-143), for the label
RGB = [0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8, 0xFF004D, 0xFFA300,
       0xFFEC27, 0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA, 0x291814, 0x111D35, 0x422136, 0x125359,
       0x742F29, 0x49333B, 0xA28879, 0xF3EF7D, 0xBE1250, 0xFF6C24, 0xA8E72E, 0x00B543, 0x065AB5, 0x754665,
       0xFF6E59, 0xFF9D81]


def label_rows(path):
    """the cart's label (128 x 128) from a picture of the game: each pixel
    the nearest of the 32 colours (g-v are 128-143)"""
    from PIL import Image
    im = Image.open(path).convert("RGB").resize((128, 128), Image.NEAREST)
    cols = [((c >> 16) & 255, (c >> 8) & 255, c & 255) for c in RGB]
    out = []
    for y in range(128):
        row = ""
        for x in range(128):
            p = im.getpixel((x, y))
            k = min(range(32), key=lambda i: sum((a - b) ** 2 for a, b in zip(p, cols[i])))
            row += "%x" % k if k < 16 else chr(ord("g") + k - 16)
        out.append(row)
    return out


def write(path):
    world = maps.build()
    check(world)
    px = sheet(world)
    out = ["pico-8 cartridge // http://www.pico-8.com", "version 41", "__lua__"]
    out += code(world).rstrip("\n").split("\n")
    out += ["__gfx__"] + ["".join("%x" % v for v in row) for row in px]
    out += ["__gff__"] + ["".join("%02x" % f for f in FLAGS[i * 128:(i + 1) * 128]) for i in range(2)]
    out += ["__map__"] + ["".join("%02x" % TILE[world.t[y][x]] for x in range(128)) for y in range(32)]
    out += ["__sfx__"] + sfx.sfx_lines()
    out += ["__music__"] + sfx.music_lines()
    lab = os.path.join(HERE, "label.txt")
    if os.path.exists(lab):
        out += ["__label__"] + open(lab).read().split()
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    return world, px


def preview(world, px, path, z=1):
    """the whole map drawn with its tiles (the light left out)"""
    from PIL import Image
    pal = [c for _, c in art.PAL]
    im = Image.new("RGB", (1024, 512))
    pix = im.load()
    under = {}
    for y in range(64):
        for x in range(128):
            n = world.t[y][x]
            v = TILE[n]
            if FLAGS[v] & tiles.UNDER:
                # the ground next to it
                g = 1
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1), (-2, 0), (2, 0), (0, 2), (0, -2)):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < 128 and 0 <= yy < 64 and world.t[yy][xx] in tiles.GROUND:
                        g = TILE[world.t[yy][xx]]
                        break
                under[(x, y)] = g
            for j in range(8):
                for i in range(8):
                    c = 0
                    if v:
                        c = px[v // 16 * 8 + j][v % 16 * 8 + i]
                        if c == 0 and (x, y) in under:
                            g = under[(x, y)]
                            c = px[g // 16 * 8 + j][g % 16 * 8 + i]
                    pix[x * 8 + i, y * 8 + j] = pal[c]
    for k, x, y in world.spawns:
        for j in range(-2, 3):
            for i in range(-2, 3):
                pix[x * 8 + 4 + i, y * 8 + 4 + j] = (255, 60, 60) if k not in ("start",) else (60, 255, 60)
    if z != 1:
        im = im.resize((1024 * z, 512 * z), Image.NEAREST)
    im.save(path)


if __name__ == "__main__":
    if "--label" in sys.argv:
        # the label from a picture of the game (needs Pillow)
        rows = label_rows(sys.argv[sys.argv.index("--label") + 1])
        open(os.path.join(HERE, "label.txt"), "w").write("\n".join(rows) + "\n")
    if "--check" in sys.argv:
        # the rom made again is the one in the repository
        import tempfile
        with tempfile.TemporaryDirectory() as tmp:
            write(os.path.join(tmp, "y8.p8"))
            same = open(os.path.join(tmp, "y8.p8"), "rb").read() == open(OUT, "rb").read()
        print("yharnam8.p8: " + ("up to date" if same else "OUT OF DATE: run carts/yharnam/pico8/mk.py"))
        sys.exit(0 if same else 1)
    world, px = write(OUT)
    src = code(world)
    print("%s: %d tiles, %d tokens of 8192, %d characters" % (os.path.relpath(OUT), len(TILE),
                                                              tokens.count(src), len(src)))
    if "--png" in sys.argv:
        preview(world, px, sys.argv[sys.argv.index("--png") + 1])
