"""The art of episode 2 (Tropic Trail): eight 16x16 tiles of the sprite sheet, drawn with bm Pixel.

Simple and deterministic, like video/01-pixel/art.py. A tile is a short program of the tools of bm Pixel; render() runs
it on a Python copy of the tools (same algorithms as carts/pixel/main.lua: 4-neighbour flood, Bresenham line,
filled oval) and gives the 16x16 grid of the colour numbers of *bm Studio's palette* (the numbers of bm Pixel
after F3, "2": 1-9 and 0 (=10) are keys, the others "." steps). record.py plays the same programs as keys,
verify.py compares the saved sheet against render().

Layout: the first 48 rows of the 256x256 sheet hold the starter tiles of bm Studio (3 rows of 16 px): the eight tiles go
in the free row below them (y = 48), at x = 16 * n.
"""

ROW_Y = 48                       # sheet row of the eight tiles
NAMES = ["grass", "path", "rock", "wood", "skin", "face", "apple", "apple_icon"]

# bm Studio's palette (carts/pixel/main.lua PAL_STUDIO), number -> RGB (1-based as bm Pixel counts)
PAL = [
    0x000000, 0x1C2030, 0x404450, 0x808490, 0xC0C4D0, 0xFFFFFF, 0x5A2A22, 0xA84632, 0xE84A5A, 0xE890B0,
    0xF09030, 0xF0D040, 0xFFF4C0, 0x8A5A30, 0xC49A5E, 0xE0C888, 0x285A28, 0x3E8A3A, 0x5CB048, 0xA8E070,
    0x1E4E6E, 0x2E6EB8, 0x3478C4, 0x7ABCE8, 0x2E8A70, 0x60D0C0, 0x3A2A6A, 0x6A50C8, 0xB060D8, 0xFFC050,
]
COLOUR_NAMES = {2: "dark", 3: "dark grey", 4: "grey", 5: "light grey", 6: "white", 7: "dark brown", 8: "dark red", 9: "red",
                10: "pink", 14: "wood brown", 15: "sand brown", 16: "sand", 17: "dark green", 18: "grass green",
                19: "leaf green", 20: "light green", 25: "dark mint", 26: "mint"}

# ops: ("fill", (x, y), colour)  ("dots", colour, [(x, y), ...])  ("line", colour, (x0, y0), (x1, y1))
#      ("oval", colour, (x0, y0), (x1, y1))  filled oval;  ("copy", from_tile)  the tile is a copy of another one
TILES = {
    "grass": [
        ("fill", (8, 8), 18),
        ("dots", 17, [(2, 3), (3, 3), (7, 1), (12, 2), (13, 2), (5, 7), (10, 6), (14, 9), (1, 11), (6, 12), (9, 10), (12, 13)]),
        ("dots", 20, [(4, 1), (9, 3), (14, 5), (2, 6), (7, 5), (11, 8), (4, 10), (8, 14), (13, 12)]),
    ],
    "path": [
        ("fill", (8, 8), 15),
        ("dots", 14, [(2, 2), (3, 2), (8, 4), (12, 3), (5, 7), (13, 8), (9, 10), (2, 12), (3, 12), (11, 13)]),
        ("dots", 16, [(3, 3), (9, 5), (6, 8), (14, 9), (10, 11), (4, 13)]),
    ],
    "rock": [
        ("fill", (8, 8), 4),
        ("line", 3, (4, 2), (10, 13)),
        ("oval", 5, (9, 2), (13, 6)),
    ],
    "wood": [
        ("fill", (8, 8), 14),
        ("line", 7, (0, 3), (15, 3)),
        ("line", 7, (0, 8), (15, 8)),
        ("line", 7, (0, 13), (15, 13)),
        ("dots", 15, [(2, 1), (9, 1), (5, 5), (12, 6), (3, 10), (10, 11), (6, 15), (13, 15)]),
    ],
    "skin": [
        ("fill", (8, 8), 26),
        ("dots", 25, [(1, 1), (5, 2), (10, 1), (14, 3), (3, 5), (8, 5), (12, 7), (1, 9), (6, 9), (10, 10), (14, 12),
                      (4, 13), (8, 14), (12, 15)]),
    ],
    "face": [("copy", "skin")],
    "apple": [
        ("fill", (8, 8), 9),
        ("oval", 10, (3, 3), (6, 6)),
        ("dots", 8, [(12, 10), (13, 11), (11, 12), (12, 13), (13, 9), (10, 14)]),
    ],
    "apple_icon": [
        ("oval", 9, (2, 4), (13, 14)),
        ("dots", 19, [(8, 3), (8, 2), (9, 2), (10, 1)]),
    ],
}

# the eyes, painted later in bm Studio on the tile "face" (the paint panel starts at 8,8): colour numbers of
# Studio's own palette list (T.PALETTE of bm3d), here as RGB
EYE_WHITE = [(7, 7), (7, 8), (8, 7)]     # 3 pixels white ...
EYE_DARK = [(8, 8)]                      # ... and the pupil over the 4th
WHITE_RGB, DARK_RGB = 0xFFFFFF, 0x1C2030


def _oval(x0, y0, x1, y1):
    """oval_pts(fill=true) of carts/pixel/main.lua"""
    x0, x1, y0, y1 = min(x0, x1), max(x0, x1), min(y0, y1), max(y0, y1)
    w, h = x1 - x0 + 1, y1 - y0 + 1
    if w <= 2 or h <= 2:
        return [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)]
    cx, cy, a, b = x0 + w / 2, y0 + h / 2, w / 2, h / 2
    out = []
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            u, v = (x + 0.5 - cx) / a, (y + 0.5 - cy) / b
            if u * u + v * v <= 1.0001:
                out.append((x, y))
    return out


def _line(x0, y0, x1, y1):
    """line_pts of carts/pixel/main.lua (Bresenham)"""
    pts = []
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err = dx + dy
    while True:
        pts.append((x0, y0))
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy
    return pts


def _flood(g, x, y, c):
    target = g[y][x]
    if target == c:
        return
    stack = [(x, y)]
    while stack:
        xx, yy = stack.pop()
        if 0 <= xx < 16 and 0 <= yy < 16 and g[yy][xx] == target:
            g[yy][xx] = c
            stack += [(xx + 1, yy), (xx - 1, yy), (xx, yy + 1), (xx, yy - 1)]


def render(name):
    """The tile as a 16x16 grid of colour numbers of bm Studio's palette (None: transparent)."""
    g = [[None] * 16 for _ in range(16)]
    for op in TILES[name]:
        if op[0] == "copy":
            return render(op[1])
        if op[0] == "fill":
            _flood(g, op[1][0], op[1][1], op[2])
        elif op[0] == "dots":
            for x, y in op[2]:
                g[y][x] = op[1]
        elif op[0] == "line":
            for x, y in _line(*op[2], *op[3]):
                g[y][x] = op[1]
        elif op[0] == "oval":
            for x, y in _oval(*op[2], *op[3]):
                g[y][x] = op[1]
    return g


def rgb(n):
    return PAL[n - 1]


def face_expected_rgb():
    """The tile "face" after the eyes: the RGB grid of skin with the eye pixels (None: transparent)."""
    g = [[None if c is None else PAL[c - 1] for c in row] for row in render("skin")]
    for x, y in EYE_WHITE:
        g[y][x] = WHITE_RGB
    for x, y in EYE_DARK:
        g[y][x] = DARK_RGB
    return g


def check():
    """The tiles are all different, the face is the skin but for the eyes, the icon has a transparent background."""
    grids = {n: render(n) for n in NAMES}
    for i, a in enumerate(NAMES):
        for b in NAMES[i + 1:]:
            if grids[a] == grids[b] and not (a, b) == ("skin", "face"):
                raise SystemExit("art.check: %s == %s" % (a, b))
    sk, fc = face_expected_rgb(), [[None if c is None else PAL[c - 1] for c in row] for row in grids["skin"]]
    diff = [(x, y) for y in range(16) for x in range(16) if sk[y][x] != fc[y][x]]
    if sorted(diff) != sorted(set(EYE_WHITE + EYE_DARK)):
        raise SystemExit("art.check: face differs from skin outside the eyes")
    ic = grids["apple_icon"]
    if ic[0][0] is not None or ic[15][15] is not None or not any(c is not None for r in ic for c in r):
        raise SystemExit("art.check: apple_icon needs a transparent background and an apple")
    for n in NAMES:
        if n != "apple_icon" and any(c is None for r in grids[n] for c in r):
            raise SystemExit("art.check: %s has a hole" % n)
    return True


if __name__ == "__main__":
    check()
    for n in NAMES:
        print(n)
        for row in render(n):
            print("".join("." if c is None else chr(48 + c) if c < 10 else chr(87 + c) for c in row))
