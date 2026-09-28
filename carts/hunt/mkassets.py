#!/usr/bin/env python3
"""Hunter's Night: draws the sprite sheet (sheet.png, 128x128) and builds the
2048x2048 pixel town (map.csv, 256x256 cells of 8x8) in code, reproducibly.

    python3 carts/hunt/mkassets.py

Sheet cells (8x8, 16 per row), the numbers used by main.lua:
   1-15  floors: cobblestones, dirt, grass, marble, carpet, planks, stairs
  32-63  solid: walls, windows, roofs, fences, graves, lamps, water, pillars...
  64-79  floor props: candles, blood, bones, the hunter's lamp
 128-    characters (16x16 = 2x2 cells) and items
Map markers replaced by main.lua at start: 244 player, 245 villager,
246 beast, 247 the great beast (boss).
"""
import os
import random
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SW = SH = 128
sheet = [(0, 0, 0, 0)] * (SW * SH)
rnd = random.Random(1887)


def rgb(c, a=255):
    return (c >> 16 & 255, c >> 8 & 255, c & 255, a)


def put(x, y, c):
    if 0 <= x < SW and 0 <= y < SH:
        sheet[y * SW + x] = rgb(c) if isinstance(c, int) else c


def cell_xy(n):
    return (n % 16) * 8, (n // 16) * 8


def shade(c, k):
    r, g, b = c >> 16 & 255, c >> 8 & 255, c & 255
    return (min(255, int(r * k)) << 16) | (min(255, int(g * k)) << 8) | min(255, int(b * k))


def tile(n, fn):
    """fn(x, y) -> colour for the 8x8 cell n"""
    ox, oy = cell_xy(n)
    for y in range(8):
        for x in range(8):
            c = fn(x, y)
            if c is not None:
                put(ox + x, oy + y, c)


def art(n, rows, pal, base=None):
    """character art on cell n (rows of any width, multiple of 8)"""
    ox, oy = cell_xy(n)
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch in pal:
                put(ox + x, oy + y, pal[ch])
            elif base is not None:
                put(ox + x, oy + y, base(x % 8, y % 8))


# ---------------------------------------------------------------- floors

def cobble(seed, dark=1.0):
    """rounded cobblestones: nearest of a few centres (wrapping, so the tile
    repeats without a seam), dark joints where two stones meet"""
    r = random.Random(seed)
    centres = [(r.uniform(0, 8), r.uniform(0, 8)) for _ in range(3)]
    cols = [shade(0x5C5A62, r.uniform(0.75, 1.2) * dark) for _ in centres]

    def f(x, y):
        ds = []
        for i, (cx, cy) in enumerate(centres):
            dx = min(abs(x + 0.5 - cx), 8 - abs(x + 0.5 - cx))
            dy = min(abs(y + 0.5 - cy), 8 - abs(y + 0.5 - cy))
            ds.append((dx * dx + dy * dy, i, dx, dy))
        ds.sort()
        d0, i0 = ds[0][0], ds[0][1]
        if ds[1][0] - d0 < 1.3:
            return shade(0x1C1A20, dark)
        c = cols[i0]
        if d0 < 1.2:                       # rounded top catches the light
            return shade(c, 1.18)
        return c
    return f


for i, n in enumerate((1, 2, 3, 4)):
    tile(n, cobble(10 + i))


def blood_on(base, amount, seed):
    r = random.Random(seed)
    spots = {(r.randrange(8), r.randrange(8)) for _ in range(amount)}
    grow = set(spots)
    for (x, y) in spots:
        for dx, dy in ((1, 0), (0, 1), (-1, 0), (0, -1)):
            if r.random() < 0.6:
                grow.add((x + dx, y + dy))

    def f(x, y):
        if (x, y) in grow:
            return 0x5A0A0A if (x + y) % 2 else 0x701010
        return base(x, y)
    return f


tile(5, blood_on(cobble(10), 5, 5))
dirt = lambda x, y: shade(0x3E3024, 0.85 + ((x * 7 + y * 13) % 5) * 0.06)
tile(6, dirt)
tile(7, lambda x, y: 0x2A3A1E if (x * 3 + y * 5) % 7 < 3 else shade(0x3E3024, 0.9))
marble_a = lambda x, y: 0x3A3640 if (x + y) % 7 else 0x4A4650
marble_b = lambda x, y: 0x201C24 if (x * y) % 5 else 0x2A2630
tile(8, marble_a)
tile(9, marble_b)
tile(10, lambda x, y: 0x5A0E14 if 0 < x < 7 else 0xA08030)
tile(11, lambda x, y: 0x201008 if y % 3 == 2 else shade(0x5A4028, 0.9 + (x % 4) * 0.05))
tile(12, lambda x, y: shade(0x5A5652, 1.2 - (y % 4) * 0.12))
tile(13, lambda x, y: shade(0x4A4440, 0.7 + ((x * 5 + y * 3) % 7) * 0.08))
tile(14, lambda x, y: 0x101010 if (x % 2 == 0 and 0 < y < 7) else cobble(11)(x, y))
tile(15, lambda x, y: 0x1A2230 if 1 < x < 7 and 2 < y < 6 else cobble(12)(x, y))

# ---------------------------------------------------------------- solids

def brick(x, y):
    row = y // 3
    off = 0 if row % 2 else 2
    if y % 3 == 2 or (x + off) % 4 == 0:
        return 0x1A1414
    return shade(0x5A4238, 0.9 + ((x + off) // 4 + row) % 3 * 0.08)


tile(32, brick)
tile(33, lambda x, y: (0xFFC060 if (x + y) % 3 else 0xE09040) if 2 <= x <= 5 and 1 <= y <= 6 and x != 3 and y != 3
     else (0x2A2020 if 1 <= x <= 6 and 0 <= y <= 7 else brick(x, y)))
tile(34, lambda x, y: shade(0x2A2C34, 0.85 + (y % 2) * 0.15) if x % 4 else 0x16181E)
tile(35, lambda x, y: 0x3A3C48 if 3 <= y <= 4 else shade(0x2A2C34, 0.9))
tile(36, lambda x, y: 0x121012 if (y % 4 == 3 or (x + (y // 4) * 4) % 8 == 0) else shade(0x4A4A50, 0.9 + (x % 3) * 0.05))
fence = lambda x, y: 0x0E0E12 if (x % 3 == 1 and y > 0) or y in (2, 6) else (0x3A3440 if x % 3 == 1 and y == 0 else cobble(11)(x, y))
tile(37, fence)
grave = lambda x, y: (0x8A8A90 if x in (2, 3, 4, 5) and y >= 2 else (0x7A7A80 if x in (3, 4) and y == 1 else None))
tile(38, lambda x, y: grave(x, y) or (0x3A3A40 if x in (2, 3, 4, 5) and y == 7 else dirt(x, y)))
tile(39, lambda x, y: (0x8A8A90 if (x in (3, 4) and y >= 0) or (y in (2, 3) and 1 <= x <= 6) else dirt(x, y)))
tile(40, lambda x, y: (0xFFD890 if y <= 1 and 2 <= x <= 5 else 0x1A1A20 if (x in (3, 4)) or (y == 2 and 1 <= x <= 6) or (y == 7 and 2 <= x <= 5) else cobble(12)(x, y)))
tile(41, lambda x, y: (0x3A2418 if 1 <= x <= 6 else cobble(13)(x, y)) if not (1 <= x <= 6 and y in (0, 7)) else (0x2A1A10 if 1 <= x <= 6 else cobble(13)(x, y)))
tile(42, lambda x, y: 0x0E1622 if (x + y * 2) % 7 else 0x1E2A3A)
tile(43, lambda x, y: 0x2E2824 if y < 2 else (0x0E1622 if (x + y) % 5 else 0x1E2A3A))
tile(44, lambda x, y: 0x6A6A70 if 1 <= x <= 6 and 1 <= y <= 6 else cobble(10)(x, y))
tile(45, lambda x, y: 0x2A1E16 if 2 <= x <= 5 else (0x1A120C if (x + y) % 3 == 0 and 1 <= x <= 6 else dirt(x, y)))
tile(46, lambda x, y: (0x4A3018 if (y % 4) else 0x2A1A0C) if 0 < x < 7 else cobble(11)(x, y))
tile(47, lambda x, y: shade(0x5A5660, 1.3 - abs(x - 3.5) * 0.12) if 1 <= x <= 6 else marble_a(x, y))
tile(48, lambda x, y: 0x121016 if (y % 4 == 3 or (x + (y // 4) % 2 * 4) % 8 == 0) else 0x3A3844)
tile(49, lambda x, y: 0x2A1A0C if (x % 4 == 0 or y == 0) else 0x4A3018)
tile(50, lambda x, y: (0xFFE0A0 if y == 0 and x in (1, 4, 6) else 0xFF9030 if y == 1 and x in (1, 4, 6)
                       else 0xC8B070 if (y >= 2 and x in (1, 4, 6)) or (y == 5 and 1 <= x <= 6) or (y == 7 and 3 <= x <= 4)
                       else marble_b(x, y)))
tile(51, lambda x, y: 0x7A6A5A if 0 < y < 7 else (0xC8A040 if y == 0 else 0x3A3040))

# ---------------------------------------------------------------- floor props

candles = lambda base: (lambda x, y: (0xFFE8A0 if (x, y) in ((1, 2), (4, 1), (6, 3)) else
                                      0xFF9030 if (x, y) in ((1, 3), (4, 2), (6, 4)) else
                                      0xE8E0C8 if (x == 1 and 4 <= y <= 6) or (x == 4 and 3 <= y <= 6) or (x == 6 and 5 <= y <= 6) else
                                      base(x, y)))
tile(64, candles(cobble(13)))
tile(65, blood_on(cobble(11), 12, 65))
tile(66, lambda x, y: 0xC8C0A8 if (x == y and 1 < x < 6) or (x == 6 and y == 2) or (x == 2 and y == 5) else dirt(x, y))
tile(67, candles(dirt))
tile(68, lambda x, y: (0xFFE8B0 if 2 <= x <= 5 and 1 <= y <= 3 else 0x3A3020 if x in (2, 5) and y in (0, 4)
                       else 0x1A1410 if x in (3, 4) and y >= 4 else cobble(10)(x, y)))

# ---------------------------------------------------------------- characters

K = 0x0A080A
HUNTER = {"k": K, "h": 0x3A302A, "H": 0x7A6A58, "m": 0x6A5A4C, "e": 0xE8D8B0, "f": 0xB09078,
          "c": 0x54443A, "C": 0x7A6858, "d": 0x2E2420, "r": 0xA02020, "s": 0xE0E0E8,
          "S": 0x707078, "g": 0x303038, "G": 0xB08840, "b": 0x1A1410}

hunter_down = [
    "................",
    ".....kkkkkk.....",
    "....khhhhhhk....",
    "..kkhhhhhhhhkk..",
    ".kHHHHHHHHHHHHk.",
    "..kkkmmmmmmkkk..",
    "....kmemmemk....",
    "....kmmmmmmk....",
    "...kcrrrrrrck...",
    "..kccCcrrcCcck..",
    ".gkccCccccCcckSs",
    ".GkcdCccccCdckSs",
    "..kcdccccccdckS.",
    "..kcdcckkccdck..",
    "...kbbk..kbbk...",
    "...kkk....kkk...",
]
hunter_down2 = hunter_down[:13] + [
    "..kcdcckkccdck..",
    "....kbbk.kbbk...",
    "....kkk...kkk...",
]
hunter_up = [
    "................",
    ".....kkkkkk.....",
    "....khhhhhhk....",
    "..kkhhhhhhhhkk..",
    ".kHHHHHHHHHHHHk.",
    "..kkkhhhhhhkkk..",
    "....kccccccck...",
    "...kcccCCccck...",
    "...kcCcccccCck..",
    "..kccCccccccCck.",
    "sSkccCccccccCckg",
    "sSkcdCccccccCdkG",
    ".Skcdccccccccdk.",
    "..kcdcckkcccdk..",
    "...kbbk..kbbk...",
    "...kkk....kkk...",
]
hunter_up2 = hunter_up[:13] + [
    "..kcdcckkcccdk..",
    "....kbbk.kbbk...",
    "....kkk...kkk...",
]
hunter_side = [
    "................",
    "....kkkkkk......",
    "...khhhhhhk.....",
    ".kkhhhhhhhhkkk..",
    "kHHHHHHHHHHHHHk.",
    ".kkkmmmmmmkkk...",
    "...kmmmmmek.....",
    "...kmmmmmmk.....",
    "...kccrrrck.....",
    "..kcCccrrcck....",
    "..kcCcccccckSSSs",
    "..kcCccccccSs.s.",
    "..kcdccccccks...",
    "..kcdcckccck....",
    "...kbbk.kbbk....",
    "...kkk..kkk.....",
]
hunter_side2 = hunter_side[:13] + [
    "..kcdccckcck....",
    "....kbbkkbbk....",
    "....kkk.kkk.....",
]

VILL = {"k": K, "a": 0x5A4A38, "A": 0x7A6A50, "d": 0x3A3026, "f": 0x8A8A70, "e": 0xFF4020,
        "t": 0x6A4020, "o": 0xFFA030, "O": 0xFFE080, "b": 0x201810, "h": 0x2A2622}
vill_down = [
    ".............OO.",
    "............OooO",
    ".....kkkk....oo.",
    "....khhhhk...t..",
    "...khffffhk..t..",
    "...kfefefek..t..",
    "....kffffk...t..",
    "...kaaaaaak.kt..",
    "..kaAaaaaAak.t..",
    "..kaAaddaAaakt..",
    "..kaAaddaAak.t..",
    "..kaaaddaaak....",
    "...kaadaaak.....",
    "...kdk..kdk.....",
    "...kbk..kbk.....",
    "...kk....kk.....",
]
vill_down2 = vill_down[:12] + [
    "...kaadaaak.....",
    "....kdkkdk......",
    "....kbkkbk......",
    "....kk..kk......",
]
vill_up = [
    ".OO.............",
    "OooO............",
    ".oo..kkkk.......",
    "..t.khhhhk......",
    "..t.khhhhhk.....",
    "..t.khhhhhk.....",
    "..t..khhhk......",
    "..tkaaaaaak.....",
    "..t.kaAaaaAk....",
    "..tkaAaaaaAak...",
    "..t.kaAaaaAak...",
    "....kaaaaaaak...",
    ".....kaadaak....",
    ".....kdk.kdk....",
    ".....kbk.kbk....",
    ".....kk...kk....",
]
vill_up2 = vill_up[:12] + [
    ".....kaadaak....",
    "......kdkdk.....",
    "......kbkbk.....",
    "......kk.kk.....",
]
vill_side = [
    "...........OO...",
    "..........OooO..",
    "....kkkk...oo...",
    "...khhhhk..t....",
    "...khhffek.t....",
    "...khhfffk.t....",
    "....kfffk..t....",
    "...kaaaaakkt....",
    "..kaAaaaaaat....",
    "..kaAaaaaak.....",
    "..kaAaddak......",
    "..kaaaddak......",
    "...kaadaak......",
    "...kdkkdk.......",
    "...kbk.kbk......",
    "...kk...kk......",
]
vill_side2 = vill_side[:13] + [
    "....kdkdk.......",
    "....kbkbk.......",
    "....kk.kk.......",
]

BEAST = {"k": K, "u": 0x5A5048, "U": 0x7A7064, "d": 0x322C28, "e": 0xFF2020, "w": 0xE8E0D0, "m": 0x5A1010}
beast_down = [
    "...k........k...",
    "..kuk......kuk..",
    "..kuUkkkkkkUuk..",
    "..kuUUuuuuUUuk..",
    "..kueeuuuueeuk..",
    "...kuuummuuuk...",
    "..kkuwmmmmwukk..",
    ".kuukuuwwuukuuk.",
    "kuUuuUuuuuUuuUuk",
    "kuUuuUuuuuUuuUuk",
    "kwUduUuuuuUudUwk",
    ".kwduuuuuuuudwk.",
    "..kdduuuuuudkk..",
    "..kduukkkkuudk..",
    "..kwwk....kwwk..",
    "..kkk......kkk..",
]
beast_down2 = beast_down[:12] + [
    "...kduuuuuudk...",
    "...kduukkuudk...",
    "...kwwk..kwwk...",
    "...kkk....kkk...",
]
beast_up = [
    "...k........k...",
    "..kuk......kuk..",
    "..kuUkkkkkkUuk..",
    "..kuUUuuuuUUuk..",
    "..kuuuuuuuuuuk..",
    "...kuuuuuuuuk...",
    "..kkuuUUUUuukk..",
    ".kuukuUuuUukuuk.",
    "kuUuuUuuuuUuuUuk",
    "kuUuudUuuUduuUuk",
    "kwUduUuuuuUudUwk",
    ".kwduuuuuuuudwk.",
    "..kdduuuuuudkk..",
    "..kduukkkkuudk..",
    "..kwwk....kwwk..",
    "..kkk......kkk..",
]
beast_up2 = beast_up[:12] + beast_down2[12:]
beast_side = [
    "................",
    "..........k.k...",
    ".........kukuk..",
    "..kkkkkkkuUUUuk.",
    ".kuUUuuuuuUeuuwk",
    "kuUuuUuuuuuuummk",
    "kuuuUuuuuuUuwwk.",
    "kduuuuuuuuuukk..",
    "kdduuUuuuuUuk...",
    ".kdduUuuuuUuk...",
    "..kduuuduuuuk...",
    "..kduk.kduuk....",
    "..kduk..kduk....",
    "..kwwk..kwwk....",
    "..kkk....kkk....",
    "................",
]
beast_side2 = beast_side[:10] + [
    "..kduuuduuuuk...",
    "...kduk.kduk....",
    "...kduk.kduk....",
    "...kwwk.kwwk....",
    "...kkk..kkk.....",
    "................",
]

BOSS = {"k": K, "u": 0xA89880, "U": 0xC8B8A0, "d": 0x6A5A48, "D": 0x4A3E32, "e": 0xFFE040,
        "a": 0xD8C8A8, "A": 0x8A7A60, "w": 0xF0E8D8, "m": 0x6A1010, "r": 0xB02020}


def boss_frame(breath):
    rows = [
        "..a..........................a..",
        "..aa........................aa..",
        "...aa.a..................a.aa...",
        "....aaa.......kkkk.......aaa....",
        ".....aaa....kkuUUukk....aaa.....",
        "......aAa..kuUUUUUUuk..aAa......",
        ".......aAakuUUuuuuUUukaAa.......",
        "........kkuUeeuuuueeUukk........",
        ".......kuuuUuuummuuuUuuuk.......",
        "......kuUuuuuwmmmmwuuuuUuk......",
        ".....kuUUuuuuuwwwwuuuuuUUuk.....",
        "....kuUuuUuuuuuuuuuuuuUuuUuk....",
        "...kuUuuUUuuuuuuuuuuuuUUuuUuk...",
        "..kuUuuuUuuuuUUUUUUuuuuUuuuUuk..",
        ".kuUuuddUuuuUUuuuuUUuuuUdduuUuk.",
        "kuUuuddDUuuuUuuuuuuUuuuUDdduuUuk",
        "kuUuddDDuuuuuuuuuuuuuuuuDDdduUuk",
        "kuuuddDkkuuuuuuuuuuuuuukkDdduuuk",
        "kwuudDk.kuuuUuuuuUuuuuk.kDduuwk.",
        "kwwddk..kuuuUUuuUUuuuuk..kddwwk.",
        ".kwwk...kduuuUUUUuuudk....kwwk..",
        "..kk....kdduuuuuuuuddk.....kk...",
        "........kDdduuuuuudDDk..........",
        "........kDDdduuuuddDDk..........",
        ".......kdDDddkkkkddDDdk.........",
        ".......kddDdk....kdDddk.........",
        "......kdddDk......kDdddk........",
        "......kwwwk........kwwwk........",
        "......kkkk..........kkkk........",
        "................................",
        "................................",
        "................................",
    ]
    if breath:          # the chest heaves: shift the lower body down one row
        rows = rows[:12] + [rows[12]] + rows[12:30] + [rows[30]]
    return rows[:32]


ITEMS = {"k": K, "r": 0xC01818, "R": 0xFF4040, "g": 0xB8C0D0, "G": 0x606878, "y": 0xFFE080,
         "o": 0xFF9030, "w": 0xF0F0F0, "b": 0x8A1010}
bullet = ["........", "........", "...yy...", "..yyyy..", "..yyyy..", "...yy...", "........", "........"]
flash = ["..y..y..", "...yy...", "yyyooyyy", ".yowwoy.", ".yowwoy.", "yyyooyyy", "...yy...", "..y..y.."]
splat = ["........", "..r..r..", ".rbrrb..", "..rbbrr.", ".rrbbr..", "..rbr.r.", ".r..r...", "........"]
vial = ["...kk...", "...gg...", "..kGGk..", ".kRrrRk.", ".krrrrk.", ".krrbrk.", "..kkkk..", "........"]
bullets = ["........", ".g.g.g..", ".g.g.g..", ".GkGkGk.", ".GkGkGk.", ".GkGkGk.", "........", "........"]
orb = ["...rr...", "..rRRr..", ".rRwwRr.", ".rRwwRr.", "..rRRr..", "...rr...", "........", "........"]
skull = [".kwwwk..", "kwwwwwk.", "wkkwkkw.", "wkkwkkw.", ".wwwww..", ".wkwkw..", "........", "........"]

art(128, hunter_down, HUNTER)
art(130, hunter_down2, HUNTER)
art(132, hunter_up, HUNTER)
art(134, hunter_up2, HUNTER)
art(136, hunter_side, HUNTER)
art(138, hunter_side2, HUNTER)
art(140, boss_frame(True), BOSS)
art(160, vill_down, VILL)
art(162, vill_down2, VILL)
art(164, vill_up, VILL)
art(166, vill_up2, VILL)
art(168, vill_side, VILL)
art(170, vill_side2, VILL)
art(192, beast_down, BEAST)
art(194, beast_down2, BEAST)
art(196, beast_up, BEAST)
art(198, beast_up2, BEAST)
art(200, beast_side, BEAST)
art(202, beast_side2, BEAST)
art(204, boss_frame(False), BOSS)
for n, a in ((224, bullet), (225, flash), (226, splat), (227, vial), (228, bullets), (229, orb), (230, skull)):
    art(n, a, ITEMS)

# ---------------------------------------------------------------- the town

MW = MH = 256
FLOOR = (1, 2, 3, 4)
m = [[rnd.choice(FLOOR) for _ in range(MW)] for _ in range(MH)]


def fill(x0, y0, x1, y1, v):
    for y in range(max(0, y0), min(MH, y1)):
        for x in range(max(0, x0), min(MW, x1)):
            m[y][x] = v() if callable(v) else v


def building(x0, y0, x1, y1):
    """roof, ridge along the middle, brick face with windows at the south"""
    if x1 - x0 < 4 or y1 - y0 < 4:
        return
    fill(x0, y0, x1, y1 - 2, 34)
    mid = (y0 + y1 - 2) // 2
    fill(x0, mid, x1, mid + 1, 35)
    for x in range(x0, x1):
        for y in (y1 - 2, y1 - 1):
            m[y][x] = 33 if (y == y1 - 2 and x % 3 == 1 and rnd.random() < 0.22) else 32
    if rnd.random() < 0.5:                  # a door
        dx = rnd.randrange(x0 + 1, x1 - 1)
        m[y1 - 1][dx] = 49


# streets every 28 cells, 6 wide; blocks become buildings
STREET, BLOCK = 6, 28
for by in range(4, MH - 4, BLOCK):
    for bx in range(4, MW - 4, BLOCK):
        x0, y0 = bx + STREET, by + STREET
        x1, y1 = min(bx + BLOCK, MW - 3), min(by + BLOCK, MH - 3)
        if x1 - x0 < 6 or y1 - y0 < 6:
            continue
        # sometimes two houses with an alley between them
        if rnd.random() < 0.55:
            cut = (x0 + x1) // 2 + rnd.randint(-3, 3)
            building(x0 + 1, y0 + 1, cut - 1, y1 - 1)
            building(cut + 2, y0 + 1, x1 - 1, y1 - 1)
        else:
            building(x0 + 1, y0 + 1, x1 - 1, y1 - 1)

# the city wall
fill(0, 0, MW, 3, 36)
fill(0, MH - 3, MW, MH, 36)
fill(0, 0, 3, MH, 36)
fill(MW - 3, 0, MW, MH, 36)

# central plaza with a statue
fill(96, 108, 160, 150, lambda: rnd.choice(FLOOR))
fill(126, 126, 130, 130, 44)
for (x, y) in ((110, 116), (146, 116), (110, 142), (146, 142), (128, 112), (128, 146)):
    m[y][x] = 40

# canal in the west with bridges on the streets
for y in range(8, MH - 8):
    for x in range(38, 44):
        m[y][x] = 43 if x == 38 else 42
    for x in (37, 44):
        if m[y][x] in (42, 43):
            m[y][x] = 1
for by in range(4, MH - 4, BLOCK):
    for y in range(by + 1, by + STREET - 1):
        for x in range(37, 45):
            m[y][x] = 11

# graveyard in the east
GX0, GY0, GX1, GY1 = 176, 64, 244, 200
fill(GX0, GY0, GX1, GY1, lambda: 6 if rnd.random() < 0.7 else 7)
for x in range(GX0, GX1):
    m[GY0][x] = 37
    m[GY1 - 1][x] = 37
for y in range(GY0, GY1):
    m[y][GX0] = 37
    m[y][GX1 - 1] = 37
for y in range(GY0 + 4, GY0 + 9):           # gates
    m[y][GX0] = 6
for x in range(GX0 + 30, GX0 + 35):
    m[GY1 - 1][x] = 6
for y in range(GY0 + 6, GY1 - 6, 5):
    for x in range(GX0 + 6, GX1 - 6, 4):
        r = rnd.random()
        m[y][x] = 38 if r < 0.55 else 39 if r < 0.8 else (67 if r < 0.9 else 6)
for _ in range(14):
    m[rnd.randrange(GY0 + 3, GY1 - 3)][rnd.randrange(GX0 + 3, GX1 - 3)] = 45
for _ in range(20):
    m[rnd.randrange(GY0 + 3, GY1 - 3)][rnd.randrange(GX0 + 3, GX1 - 3)] = 66

# the cathedral in the north: the great beast waits inside
CX0, CY0, CX1, CY1 = 100, 6, 156, 62
fill(CX0, CY0, CX1, CY1, 48)
fill(CX0 + 3, CY0 + 3, CX1 - 3, CY1 - 3, lambda: 8)
for y in range(CY0 + 3, CY1 - 3):
    for x in range(CX0 + 3, CX1 - 3):
        if (x + y) % 2:
            m[y][x] = 9
fill(126, CY0 + 6, 130, CY1, 10)            # carpet to the altar
fill(124, CY0 + 4, 132, CY0 + 6, 51)
for y in range(CY0 + 10, CY1 - 6, 8):
    for x in (CX0 + 10, CX0 + 20, CX1 - 21, CX1 - 11):
        m[y][x] = 47
    for x in (CX0 + 6, CX1 - 7):
        m[y][x] = 50
fill(125, CY1 - 3, 131, CY1, 10)            # the door
fill(122, CY1, 134, CY1 + 4, 12)            # steps
# a clear square in front of the cathedral
fill(96, CY1 + 4, 160, CY1 + 16, lambda: rnd.choice(FLOOR))

# street lamps along the streets, props and blood
for by in range(4, MH - 4, BLOCK):
    for x in range(8, MW - 8, 19):
        y = by + 1
        if m[y][x] in FLOOR:
            m[y][x] = 40
for bx in range(4, MW - 4, BLOCK):
    for y in range(10, MH - 8, 21):
        x = bx + STREET - 2
        if m[y][x] in FLOOR:
            m[y][x] = 40
for _ in range(900):
    x, y = rnd.randrange(4, MW - 4), rnd.randrange(4, MH - 4)
    if m[y][x] in FLOOR:
        r = rnd.random()
        m[y][x] = 5 if r < 0.35 else 65 if r < 0.5 else 64 if r < 0.62 else 41 if r < 0.75 else 46 if r < 0.85 else 13 if r < 0.95 else 15

# hunter's lamps (checkpoints)
LAMPS = [(20, 234), (128, 104), (128, 70), (182, 72)]
for x, y in LAMPS:
    m[y][x] = 68

# spawns: markers replaced by main.lua
m[230][20] = 244
walk = [(x, y) for y in range(8, MH - 8) for x in range(8, MW - 8) if m[y][x] in FLOOR]
rnd.shuffle(walk)
placed = 0
for x, y in walk:
    if abs(x - 20) + abs(y - 230) < 30 or CX0 <= x < CX1 and CY0 <= y < CY1:
        continue
    near_lamp = any(abs(x - lx) + abs(y - ly) < 10 for lx, ly in LAMPS)
    if near_lamp:
        continue
    in_grave = GX0 <= x < GX1 and GY0 <= y < GY1
    m[y][x] = 246 if (in_grave or rnd.random() < 0.3) else 245
    placed += 1
    if placed >= 90:
        break
for x, y in walk:                           # the graveyard is mostly beasts
    pass
m[CY0 + 18][128] = 247


def write_png(path, w, h, px):
    raw = b"".join(b"\0" + bytes(v for p in px[y * w:(y + 1) * w] for v in p) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


write_png(os.path.join(HERE, "sheet.png"), SW, SH, sheet)
with open(os.path.join(HERE, "map.csv"), "w") as f:
    f.write("# generated by mkassets.py - 256x256 cells of 8x8 pixels\n")
    for row in m:
        f.write(",".join(str(v) for v in row) + "\n")
print("sheet.png, map.csv:", placed, "enemies")
