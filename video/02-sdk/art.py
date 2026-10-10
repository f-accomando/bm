"""The art and the level of episode 2: the tiles of Skyvale World (8x8, in the
cells 64-68 of the sheet, under Kip) and the first level, in map cells.

Characters -> the 32 colours of the SDK's sprite page (PALETTE in
carts/editor/main.lua): the number is the place in that list (1 = black).
"""

# char -> (place in the SDK's PALETTE, RGB)
COLOURS = {
    "L": (24, 0x6CC04A),   # light green
    "D": (4, 0x008751),    # dark green
    "B": (5, 0xAB5236),    # brown
    "d": (19, 0x4A3E36),   # dark brown
    "F": (16, 0xFFCCAA),   # light wood
    "W": (8, 0xFFF1E8),    # white
    "g": (7, 0xC2C3C7),    # grey
}

# cell -> (name, flag, rows). A flag: 0 solid, 1 platform, 2 ladder, 3 water, 4 hurts
TILES = {
    64: ("grass", 0, [
        "LLLLLLLL",
        "LLDLLLDL",
        "BDBBDBBD",
        "BBBBBBBB",
        "BBdBBBBd",
        "BBBBBdBB",
        "BdBBBBBB",
        "BBBBdBBB"]),
    65: ("dirt", 0, [
        "BBBBBBBB",
        "BBdBBBBd",
        "BBBBBdBB",
        "BdBBBBBB",
        "BBBBdBBB",
        "BBBBBBBd",
        "BdBBBdBB",
        "BBBBBBBB"]),
    66: ("plank", 1, [
        "FFFFFFFF",
        "BBBBBBBB",
        "BdBBBBdB",
        "dddddddd",
        "........",
        "........",
        "........",
        "........"]),
    67: ("cloud", None, [
        "........",
        "........",
        "..WWWW..",
        "WWWWWWWW",
        "WWWWWWWW",
        "gggggggg",
        "........",
        "........"]),
    68: ("spikes", 4, [
        "........",
        "........",
        "........",
        "........",
        "...WW...",
        "..gWWg..",
        ".gggggg.",
        "gggggggg"]),
}

# the level, in map cells of layer "main" (x, y); the pit is at x 44..49
GROUND_Y = 28
PIT = range(44, 50)
WIDTH = 80
PLANKS = [(range(16, 22), 22), (range(28, 32), 18), (range(36, 42), 22)]
CLOUDS = [(range(8, 11), 6), (range(30, 34), 3), (range(60, 63), 8)]   # on layer 2

SKY = 0x70A8F0


def level():
    """{layer: {(x, y): tile}}"""
    main, back = {}, {}
    for x in range(WIDTH):
        if x not in PIT:
            main[(x, GROUND_Y)] = 64
            main[(x, GROUND_Y + 1)] = 65
        main[(x, GROUND_Y + 2)] = 65
    for x in PIT:
        main[(x, GROUND_Y + 1)] = 68
    for xs, y in PLANKS:
        for x in xs:
            main[(x, y)] = 66
    for xs, y in CLOUDS:
        for x in xs:
            back[(x, y)] = 67
    return {"main": main, "layer2": back}


def check():
    for cell, (name, flag, rows) in TILES.items():
        assert len(rows) == 8 and all(len(r) == 8 for r in rows), name
        assert all(c == "." or c in COLOURS for r in rows for c in r), name


if __name__ == "__main__":
    check()
    lv = level()
    print({k: len(v) for k, v in lv.items()})
