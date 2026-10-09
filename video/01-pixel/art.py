"""The art of episode 1: Kip, the hero of Skyvale World (an original fox,
16x16, seen from the front: it is symmetric, so bm Pixel's mirror draws it
with half the keys), its four walking frames and a slime.

The characters are the colours of the SDK's palette (the one bm Pixel starts
with): the number is the colour as bm Pixel counts it (0 = transparent, 1 = the
first of the palette; the keys 1-9 and 0 choose the first ten, "." and ","
move to the next and the one before).
"""

# char -> colour number (bm Pixel), name
COLOURS = {
    "K": (17, "outline"),    # 0x14161E
    "O": (10, "fur"),        # 0xFFA300
    "W": (8, "muzzle"),      # 0xFFF1E8
    "S": (13, "scarf"),      # 0x29ADFF
    "P": (15, "ear"),        # 0xFF77A8
}

# the standing frame (frame 0 of the walk and the idle pose)
KIP = [
    "................",
    "...KK......KK...",
    "..KPPK....KPPK..",
    "..KPOOKKKKOOPK..",
    "..KOOOOOOOOOOK..",
    "..KOOKOOOOKOOK..",
    "..KOOKOOOOKOOK..",
    "..KOWWOOOOWWOK..",
    "...KOWWKKWWOK...",
    "....KKWWWWKK....",
    "...KSSSSSSSSK...",
    "..KSSSSSSSSSSK..",
    "..KOOKOOOOKOOK..",
    "...KKOOOOOOKK...",
    "....KOOKKOOK....",
    "....KKK..KKK....",
]

# the walk: the legs (rows 14 and 15) change, the rest stays
LEGS_A = [  # step, weight on the left foot
    "...KOOK..KOOK...",
    "..KKKK....KKKK..",
]
LEGS_B = [  # the other step, the body one pixel lower is not needed: legs apart
    "....KOOKKOOK....",
    "...KKKKKKKKKK...",
]


def frame(legs=None):
    rows = list(KIP)
    if legs:
        rows[14], rows[15] = legs
    return rows


FRAMES = [frame(), frame(LEGS_A), frame(), frame(LEGS_B)]

# a slime (the first enemy), 16x16
SLIME = [
    "................",
    "................",
    "................",
    "................",
    "................",
    "......KKKK......",
    "....KKGGGGKK....",
    "...KGGGLLGGGK...",
    "..KGGGLLGGGGGK..",
    "..KGGGGGGGGGGK..",
    "..KGKWGGGKWGGK..",
    ".KGGKKGGGKKGGGK.",
    ".KGGGGGGGGGGGGK.",
    ".KGGGGGGGGGGGGK.",
    "..KKKKKKKKKKKK..",
    "................",
]


def check():
    for rows in FRAMES + [SLIME]:
        assert len(rows) == 16 and all(len(r) == 16 for r in rows), "16x16"
    # the fur must be one closed region (the fill must not leak)
    seen, todo = set(), [(8, 4)]
    while todo:
        x, y = todo.pop()
        if (x, y) in seen or KIP[y][x] == "K":
            continue
        assert KIP[y][x] != ".", "the fill would leak at %d,%d" % (x, y)
        seen.add((x, y))
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            todo.append((x + dx, y + dy))
    return len(seen)


if __name__ == "__main__":
    print("closed region of", check(), "cells")
