"""The art of episode 1: Kip, the hero of Skyvale World, an original fox seen
from the side (facing right, as in a side-scrolling platformer), 16x16: a
six-frame run cycle, a jump, and a coin.

The characters are the colours of the SDK's palette (the one bm Pixel starts
with): the number is the colour as bm Pixel counts it (0 = transparent, 1 = the
first of the palette; the keys 1-9 and 0 choose the first ten, "." and ","
move to the next and the one before).

Layers: BODY (head, scarf, torso) + a tail pose + a pair of legs, optionally
moved one pixel up or down (the bob of a run).
"""

# char -> (colour number in bm Pixel, name, RGB of the SDK palette)
COLOURS = {
    "K": (17, "outline", 0x14161E),
    "O": (10, "fur", 0xFFA300),
    "W": (8, "white", 0xFFF1E8),
    "S": (13, "scarf", 0x29ADFF),
    "P": (15, "ear", 0xFF77A8),
    "Y": (11, "gold", 0xFFEC27),
}

BODY = [
    "................",
    "..........KK.KK.",
    ".........KPKKPK.",
    "........KOOOOOOK",
    "........KOOOKOOK",
    "........KOOOOWWK",
    ".......KOOOOOWWK",
    ".......KSSSSWWK.",
    "....KKKSSSSSKK..",
    "...KOOOOOWWOOK..",
    "...KOOOOOWWOOK..",
    "....KKOOOOOOK...",
    "........K.......",
] + ["." * 16] * 3

TAIL = {
    "up":   [(5, 1, "KK"), (6, 0, "KWWK"), (7, 0, "KWOK"), (8, 1, "KOK"), (9, 2, "K")],
    "mid":  [(7, 0, "KKK"), (8, 0, "KWOK"), (9, 0, "KWWO")],
    "down": [(9, 0, "KWWO"), (10, 0, "KWOK"), (11, 0, "KKK")],
}
LEGS = {
    "wide":   [(12, 4, "KOOK"), (13, 3, "KOOK"), (14, 2, "KOOK"), (15, 1, "KKKK"),
               (12, 9, "KOOK"), (13, 10, "KOOK"), (14, 11, "KOOK"), (15, 11, "KKKK")],
    "stand":  [(12, 4, "KOOK"), (13, 4, "KOOK"), (14, 4, "KOOK"), (15, 3, "KKKK"),
               (12, 9, "KOOK"), (13, 9, "KOOK"), (14, 9, "KOOK"), (15, 9, "KKKK")],
    "backup": [(12, 4, "KOOK"), (13, 3, "KOOK"), (14, 3, "KKK"),
               (12, 9, "KOOK"), (13, 9, "KOOK"), (14, 9, "KOOK"), (15, 9, "KKKK")],
    "fwdup":  [(12, 4, "KOOK"), (13, 4, "KOOK"), (14, 4, "KOOK"), (15, 3, "KKKK"),
               (12, 9, "KOOK"), (13, 10, "KOOK"), (14, 11, "KKK")],
    "air":    [(12, 3, "KOOK"), (13, 2, "KOOK"), (14, 1, "KKKK"),
               (12, 9, "KOOK"), (13, 11, "KOOK"), (14, 12, "KKKK")],
    "tuck":   [(12, 4, "KOOK"), (13, 3, "KOOK"), (14, 3, "KKK"),
               (12, 9, "KOOK"), (13, 10, "KOOK"), (14, 10, "KKK")],
}


def frame(tail, legs, shift=0):
    g = [list(r) for r in BODY]

    def put(y, x, s):
        for i, c in enumerate(s):
            if 0 <= x + i < 16 and 0 <= y < 16:
                g[y][x + i] = c

    for t in TAIL[tail]:
        put(*t)
    for l in LEGS[legs]:
        put(*l)
    rows = ["".join(r) for r in g]
    if shift > 0:
        rows = ["." * 16] * shift + rows[:-shift]
    if shift < 0:
        rows = rows[-shift:] + ["." * 16] * (-shift)
    return rows


# the run: contact, down, passing, flight, down, passing
RUN = [frame("up", "wide"), frame("mid", "stand", 1), frame("mid", "backup"),
       frame("down", "air", -1), frame("down", "fwdup", 1), frame("up", "fwdup")]
JUMP = frame("up", "tuck", -1)
FRAMES = RUN + [JUMP]               # sprites 0, 2, 4, 6, 8, 10 (run) and 12 (jump)

# the coin (sprite 14) is drawn with the oval, line and rectangle tools: see record.py


def diff(a, b):
    return [(x, y) for y in range(16) for x in range(16) if a[y][x] != b[y][x]]


def regions(rows):
    """The cells the outline closes, by 4-connected region (what the fill
    reaches): [(cells, most common char)], and the cells of the open background."""
    seen, out, bg = set(), [], False
    for y0 in range(16):
        for x0 in range(16):
            if (x0, y0) in seen or rows[y0][x0] == "K":
                continue
            todo, cells = [(x0, y0)], []
            while todo:
                x, y = todo.pop()
                if not (0 <= x < 16 and 0 <= y < 16) or (x, y) in seen or rows[y][x] == "K":
                    continue
                seen.add((x, y))
                cells.append((x, y))
                todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
            chars = [rows[y][x] for x, y in cells]
            if "." in chars:
                bg = bg or len(cells) > 0
                continue
            top = max(set(chars), key=chars.count)
            out.append((cells, top))
    return out


def check():
    for rows in FRAMES:
        assert len(rows) == 16 and all(len(r) == 16 for r in rows), "16x16"
    for i, a in enumerate(FRAMES):
        for j in range(i + 1, len(FRAMES)):
            assert len(diff(a, FRAMES[j])) > 15, "frames %d and %d are too alike" % (i, j)
    # the fill must close in the first frame: every region is inside the outline
    regs = regions(FRAMES[0])
    for cells, top in regs:
        assert all(FRAMES[0][y][x] != "." for x, y in cells)
    return [(len(c), t) for c, t in regs]


if __name__ == "__main__":
    print("fill regions (cells, colour):", check())
    for y in range(16):
        print("  ".join(f[y].replace(".", " ") for f in FRAMES))
    print("frame to frame differences:", [len(diff(FRAMES[i], FRAMES[i + 1])) for i in range(len(FRAMES) - 1)])
