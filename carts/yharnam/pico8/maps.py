"""The map of Yharnam 8: 128 x 64 tiles, four regions of 64 x 32 (four
screens by two), walked in a U: Central Yharnam (top left), the forest (top
right), Cathedral Ward (bottom right), the forbidden quarter (bottom left).
Built here by rule (houses, lamps, trees...) so it can be read and changed;
mk.py turns it into the cart's map.

Cells hold a tile name; the creatures' places, the start and the mist gates
are kept apart (spawns, gates).
"""
import random

W, H = 128, 64
VOID = None                       # tile 0: black, solid


class World:
    def __init__(self):
        self.t = [[VOID] * W for _ in range(H)]
        self.spawns = []              # (kind, x, y) in tiles
        self.gates = []               # (x0, y0, x1, y1, region) in tiles, inclusive
        self.r = random.Random(1887)
        self.keep = set()              # cells kept free of trees

    # ---------------------------------------------------------- basics
    def ground(self, x0, y0, x1, y1, name):
        """a ground, its variants by chance (cobble -> cobble/cobble2...)"""
        alt = {"cobble": "cobble2", "setts": "setts2", "grass": "grass2", "floor": "floor2"}
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                v = name
                if name in alt and self.r.random() < 0.3:
                    v = alt[name]
                self.t[y][x] = v

    def void(self, x0, y0, x1, y1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.t[y][x] = VOID

    def put(self, x, y, name):
        self.t[y][x] = name

    def free(self, x, y, w=1, h=1, grounds=None):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                if not (0 <= xx < W and 0 <= yy < H):
                    return False
                v = self.t[yy][xx]
                if v is None or v not in GROUNDS or (grounds and v not in grounds):
                    return False
        return True

    # ---------------------------------------------------------- buildings
    def house(self, x, y, w, h, lit=0.35, door=True):
        """a terrace: the roof on top (ridge, slope, eaves), two rows of front"""
        for j in range(h):
            for i in range(w):
                yy = y + j
                if j == h - 1 or j == h - 2:
                    top = j == h - 2
                    mid = i == w // 2
                    if not top and mid and door:
                        v = "door"
                    elif (i % 3 == 1) or (top and mid):
                        v = "window_lit" if self.r.random() < lit else "window"
                    else:
                        v = "wall"
                elif j == 0:
                    v = "ridge"
                elif j == h - 3:
                    v = "eaves"
                else:
                    v = "roof"
                self.t[yy][x + i] = v

    def church(self, x, y, w, h, spires=True):
        for j in range(h):
            for i in range(w):
                if j == 0 and spires and i % 3 == 1:
                    v = "spire"
                elif j == h - 1 and i == w // 2:
                    v = "cdoor"
                elif j >= 1 and i % 4 == 2 and j < h - 1 and j % 3 == 1:
                    v = "cwindow"
                else:
                    v = "cwall"
                self.t[y + j][x + i] = v

    # ---------------------------------------------------------- things
    def lamp(self, x, y):
        self.t[y - 1][x] = "lamp_head"
        self.t[y][x] = "lamp_post"

    def tree(self, x, y):             # (x, y): the bottom left
        self.t[y - 1][x], self.t[y - 1][x + 1] = "tree_tl", "tree_tr"
        self.t[y][x], self.t[y][x + 1] = "tree_bl", "tree_br"

    def two(self, x, y, base):        # 2 x 2 from the top left
        self.t[y][x], self.t[y][x + 1] = base + "_tl", base + "_tr"
        self.t[y + 1][x], self.t[y + 1][x + 1] = base + "_bl", base + "_br"

    def tall(self, x, y, base):       # 1 x 2, (x, y) the bottom
        self.t[y - 1][x], self.t[y][x] = base + "_t", base + "_b"

    def shrine(self, x, y):
        self.tall(x, y, "shrine")

    def row(self, x0, x1, y, name):
        for x in range(x0, x1 + 1):
            self.t[y][x] = name

    def col(self, x, y0, y1, name):
        for y in range(y0, y1 + 1):
            self.t[y][x] = name

    def spawn(self, kind, x, y):
        self.spawns.append((kind, x, y))

    def gate(self, x0, y0, x1, y1, region):
        self.gates.append((x0, y0, x1, y1, region))

    def path(self, pts, w=3, name="dirt"):
        """an earth path through the points, w wide, wandering a little;
        -> the cells it covers (to keep the trees off)"""
        cells = set()
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            n = max(abs(bx - ax), abs(by - ay))
            for k in range(n + 1):
                t = k / max(n, 1)
                x, y = ax + (bx - ax) * t, ay + (by - ay) * t
                wob = round(0.9 * __import__("math").sin(k * 0.45 + ax * 0.7 + ay))
                if abs(bx - ax) >= abs(by - ay):
                    y += wob
                else:
                    x += wob
                for j in range(-(w // 2), w - w // 2):
                    for i in range(-(w // 2), w - w // 2):
                        cx, cy = int(round(x)) + i, int(round(y)) + j
                        if 0 < cx < W - 1 and 0 < cy < H - 1:
                            cells.add((cx, cy))
        for cx, cy in cells:
            self.t[cy][cx] = name if self.r.random() < 0.85 else "earth"
        return cells

    def woods(self, x0, y0, x1, y1, dens, keep=()):
        """trees and bushes where the ground is free, thicker in clumps"""
        r = self.r
        for y in range(y0 + 1, y1 + 1):
            for x in range(x0, x1):
                if any(a <= x <= c and b <= y <= d for (a, b, c, d) in keep) or \
                        any((x + i, y - j) in self.keep for i in (-1, 0, 1, 2) for j in (-1, 0, 1, 2)):
                    continue
                k = 0.5 + 0.5 * ((((x * 7 + y * 3) % 11) / 10.0) - 0.5) + 0.25 * r.random()
                if r.random() < dens * k and self.free(x, y - 1, 2, 2):
                    self.tree(x, y)
                elif r.random() < dens * 0.25 and self.free(x, y):
                    self.put(x, y, "bush" if r.random() < 0.7 else "stump")


GROUNDS = {"cobble", "cobble2", "setts", "setts2", "flags", "pave", "grass", "grass2", "floor", "floor2",
           "dirt", "earth"}


# ================================================================ the regions
def central(w):
    """Central Yharnam: streets between terraces, the fountain square, the
    Butcher's pyre square at the east end (its gate into the forest)"""
    X, Y = 0, 0
    w.ground(X + 1, Y + 1, X + 62, Y + 30, "cobble")
    # terraces along the north and south edges
    for x0, x1 in ((1, 10), (12, 24), (27, 38), (40, 46)):
        w.house(X + x0, Y + 1, x1 - x0 + 1, 5)
        w.row(X + x0, X + x1, Y + 6, "pave")
    for x0, x1 in ((1, 9), (11, 22), (25, 37), (40, 46)):
        w.house(X + x0, Y + 26, x1 - x0 + 1, 5)
        w.row(X + x0, X + x1, Y + 25, "pave")
    # the first street: the start, a hunter's lamp
    w.spawn("start", X + 4, Y + 16)
    w.shrine(X + 6, Y + 13)
    w.lamp(X + 2, Y + 9)
    w.lamp(X + 2, Y + 22)
    w.lamp(X + 10, Y + 9)
    w.lamp(X + 10, Y + 22)
    # a block of houses in the middle, the street round it
    w.house(X + 13, Y + 9, 9, 4)
    w.house(X + 13, Y + 19, 9, 4)
    w.row(X + 13, X + 21, Y + 13, "pave")
    w.row(X + 13, X + 21, Y + 23, "pave")
    w.put(X + 12, Y + 14, "barrel")
    w.put(X + 22, Y + 18, "crate")
    w.tall(X + 12, Y + 18, "coffin")
    # the fountain square
    w.ground(X + 24, Y + 8, X + 37, Y + 23, "flags")
    w.two(X + 30, Y + 15, "fount")
    w.tall(X + 26, Y + 11, "angel")
    w.tall(X + 35, Y + 11, "angel")
    w.lamp(X + 25, Y + 21)
    w.lamp(X + 36, Y + 21)
    w.put(X + 27, Y + 20, "crate")
    w.put(X + 34, Y + 9, "barrel")
    # east of the square: a narrow way, houses either side
    w.house(X + 39, Y + 8, 7, 5)
    w.house(X + 39, Y + 18, 7, 5)
    w.row(X + 39, X + 45, Y + 13, "pave")
    w.row(X + 39, X + 45, Y + 23, "pave")
    w.lamp(X + 38, Y + 15)
    w.shrine(X + 44, Y + 16)
    w.tall(X + 41, Y + 24, "coffin")
    # the arena: walls round it, the pyre in the middle, braziers
    w.void(X + 47, Y + 1, X + 62, Y + 5)
    w.void(X + 47, Y + 26, X + 62, Y + 30)
    w.col(X + 47, Y + 6, Y + 12, "stonewall")
    w.col(X + 47, Y + 19, Y + 25, "stonewall")
    w.row(X + 47, X + 62, Y + 6, "stonewall")
    w.row(X + 47, X + 62, Y + 25, "stonewall")
    w.ground(X + 48, Y + 7, X + 62, Y + 24, "flags")
    w.two(X + 54, Y + 15, "pyre")
    for bx, by in ((50, 9), (59, 9), (50, 22), (59, 22)):
        w.put(X + bx, Y + by, "brazier")
    w.put(X + 49, Y + 13, "barrel")
    w.tall(X + 61, Y + 21, "coffin")
    w.spawn("butcher", X + 55, Y + 20)
    w.gate(X + 63, Y + 14, X + 63, Y + 17, 1)
    w.ground(X + 63, Y + 14, X + 63, Y + 17, "flags")
    # the townsfolk
    for k, x, y in (("villager", 14, 16), ("villager", 17, 15), ("villager", 22, 8), ("rifle", 24, 6),
                    ("villager", 28, 13), ("villager", 33, 19), ("rifle", 36, 9), ("villager", 31, 22),
                    ("villager", 41, 15), ("villager", 43, 24), ("rifle", 45, 7), ("villager", 20, 24)):
        w.spawn(k, X + x, Y + y)


def forest(w):
    """the forest: earth paths through the trees, clearings, the Great
    Hound's glade (its gate south, into Cathedral Ward)"""
    X, Y = 64, 0
    w.ground(X + 0, Y + 1, X + 62, Y + 30, "floor")
    # the paths, winding
    for pts in ([(0, 15), (10, 15), (19, 13), (20, 6), (30, 5), (42, 6)],
                [(19, 13), (21, 21), (20, 26), (30, 26), (40, 25)],
                [(42, 6), (42, 12), (41, 17), (49, 18)],
                [(10, 15), (12, 9)],
                [(21, 21), (28, 15)]):
        w.keep |= w.path([(X + x, Y + y) for x, y in pts])
    # clearings: the lamp's, a hunters' camp, the glade of the Hound
    w.ground(X + 8, Y + 3, X + 16, Y + 10, "grass")
    w.ground(X + 26, Y + 10, X + 34, Y + 18, "grass")
    w.ground(X + 44, Y + 19, X + 61, Y + 29, "grass")
    w.path([(X + 40, Y + 25), (X + 44, Y + 24)])
    w.path([(X + 49, Y + 18), (X + 49, Y + 20)])
    w.path([(X + 49, Y + 29), (X + 49, Y + 30)], w=4)
    keep = [(7, 2, 17, 11), (25, 9, 35, 19), (43, 18, 62, 30), (39, 22, 45, 27)]
    w.shrine(X + 12, Y + 7)
    w.put(X + 30, Y + 14, "brazier")
    w.put(X + 28, Y + 13, "stump")
    w.put(X + 32, Y + 16, "crate")
    w.tall(X + 33, Y + 12, "coffin")
    w.shrine(X + 45, Y + 18)
    for bx, by in ((46, 22), (60, 22), (46, 28), (60, 28)):
        w.put(X + bx, Y + by, "brazier")
    w.lamp(X + 17, Y + 12)
    w.lamp(X + 24, Y + 28)
    w.lamp(X + 44, Y + 10)
    w.woods(X + 0, Y + 1, X + 62, Y + 30, 0.55, [(X + a, Y + b, X + c, Y + d) for a, b, c, d in keep])
    # a few trees in the glade's corners
    for tx, ty in ((44, 21), (60, 21)):
        if w.free(X + tx, Y + ty - 1, 2, 2):
            w.tree(X + tx, Y + ty)
    w.spawn("hound", X + 54, Y + 25)
    w.gate(X + 48, Y + 31, X + 51, Y + 31, 2)
    w.ground(X + 48, Y + 30, X + 51, Y + 31, "dirt")
    for k, x, y in (("beast", 10, 15), ("beast", 20, 9), ("villager", 19, 20), ("beast", 26, 5),
                    ("hunter", 30, 12), ("beast", 33, 26), ("rifle", 38, 5), ("beast", 42, 13),
                    ("hunter", 26, 25), ("beast", 47, 18), ("villager", 14, 4)):
        w.spawn(k, X + x, Y + y)


def cathedral(w):
    """Cathedral Ward: dark setts, a chapel, cemeteries, angels; Father
    Graves among the graves at the west end (its gate into the old quarter)"""
    X, Y = 64, 32
    w.ground(X + 1, Y + 0, X + 62, Y + 30, "setts")
    # the way down from the forest
    w.ground(X + 47, Y + 0, X + 52, Y + 6, "earth")
    w.shrine(X + 54, Y + 4)
    w.lamp(X + 46, Y + 3)
    # the chapel, its square
    w.church(X + 26, Y + 1, 14, 6)
    w.ground(X + 26, Y + 7, X + 39, Y + 12, "flags")
    for bx in (28, 37):
        w.put(X + bx, Y + 8, "brazier")
    w.tall(X + 32, Y + 10, "angel")
    # houses of the ward
    w.house(X + 54, Y + 8, 8, 5)
    w.house(X + 54, Y + 22, 8, 5)
    w.house(X + 41, Y + 1, 5, 5)
    w.house(X + 14, Y + 1, 10, 5)
    # a cemetery in the middle
    w.ground(X + 24, Y + 15, X + 44, Y + 27, "earth")
    w.row(X + 24, X + 44, Y + 14, "fence")
    w.row(X + 24, X + 44, Y + 28, "fence")
    for x in range(X + 33, X + 36):
        w.put(x, Y + 14, "earth")
        w.put(x, Y + 28, "earth")
    for gy in (17, 20, 23, 26):
        for gx in range(26, 44, 3):
            if gx in (34,):
                continue
            w.put(X + gx, Y + gy, "cross" if (gx + gy) % 2 else "stone")
    w.lamp(X + 23, Y + 13)
    w.lamp(X + 45, Y + 13)
    w.lamp(X + 50, Y + 19)
    # Father Graves' cemetery: graves round the edges, open in the middle
    w.ground(X + 1, Y + 4, X + 15, Y + 27, "earth")
    w.row(X + 1, X + 15, Y + 3, "stonewall")
    w.row(X + 1, X + 15, Y + 28, "stonewall")
    w.col(X + 16, Y + 3, Y + 12, "fence")
    w.col(X + 16, Y + 19, Y + 28, "fence")
    for gy in range(5, 27, 3):
        w.put(X + 2, Y + gy, "cross")
        w.put(X + 14, Y + gy + 1, "stone")
    w.tall(X + 8, Y + 6, "angel")
    for bx, by in ((5, 9), (11, 9), (5, 23), (11, 23)):
        w.put(X + bx, Y + by, "brazier")
    w.shrine(X + 19, Y + 16)
    w.lamp(X + 20, Y + 11)
    w.spawn("father", X + 8, Y + 16)
    w.gate(X + 0, Y + 14, X + 0, Y + 17, 3)
    w.ground(X + 0, Y + 14, X + 0, Y + 17, "earth")
    w.void(X + 0, Y + 0, X + 0, Y + 13)
    w.void(X + 0, Y + 18, X + 0, Y + 31)
    w.void(X + 1, Y + 31, X + 63, Y + 31)
    w.void(X + 63, Y + 0, X + 63, Y + 31)
    for k, x, y in (("hunter", 48, 12), ("beast", 52, 17), ("kin", 30, 9), ("hunter", 34, 21),
                    ("kin", 40, 24), ("villager", 20, 8), ("rifle", 44, 8), ("hunter", 22, 21),
                    ("kin", 28, 25), ("beast", 57, 15)):
        w.spawn(k, X + x, Y + y)


def forbidden(w):
    """the forbidden quarter: dark lanes, chapels, the children of the sky;
    the Watcher's square at the west end (the last)"""
    X, Y = 0, 32
    w.ground(X + 1, Y + 1, X + 63, Y + 30, "setts")
    w.shrine(X + 58, Y + 12)
    w.lamp(X + 60, Y + 19)
    w.church(X + 44, Y + 1, 11, 6)
    w.house(X + 56, Y + 1, 7, 5)
    w.house(X + 56, Y + 23, 7, 5)
    w.house(X + 30, Y + 1, 12, 5)
    w.house(X + 30, Y + 24, 12, 5)
    w.church(X + 44, Y + 22, 10, 7, spires=False)
    w.ground(X + 30, Y + 9, X + 42, Y + 20, "flags")
    w.two(X + 35, Y + 14, "fount")
    w.lamp(X + 31, Y + 10)
    w.lamp(X + 41, Y + 10)
    w.lamp(X + 31, Y + 20)
    w.lamp(X + 41, Y + 20)
    w.house(X + 18, Y + 1, 10, 6)
    w.house(X + 18, Y + 23, 10, 6)
    w.tall(X + 24, Y + 15, "coffin")
    w.tall(X + 26, Y + 18, "coffin")
    w.shrine(X + 19, Y + 15)
    w.lamp(X + 22, Y + 12)
    # the Watcher's square, before a dark church
    w.church(X + 1, Y + 1, 15, 6)
    w.ground(X + 1, Y + 7, X + 15, Y + 29, "flags")
    w.col(X + 16, Y + 7, Y + 11, "stonewall")
    w.col(X + 16, Y + 19, Y + 29, "stonewall")
    for bx, by in ((3, 9), (13, 9), (3, 26), (13, 26)):
        w.put(X + bx, Y + by, "brazier")
    w.tall(X + 8, Y + 9, "angel")
    w.spawn("watcher", X + 8, Y + 19)
    w.void(X + 0, Y + 0, X + 63, Y + 0)
    w.void(X + 0, Y + 31, X + 63, Y + 31)
    for k, x, y in (("kin", 50, 15), ("kin", 52, 16), ("hunter", 46, 10), ("kin", 36, 11),
                    ("kin", 38, 19), ("villager", 33, 21), ("hunter", 28, 15), ("kin", 25, 9),
                    ("kin", 27, 21), ("rifle", 40, 22), ("villager", 48, 18)):
        w.spawn(k, X + x, Y + y)


def build():
    w = World()
    central(w)
    forest(w)
    cathedral(w)
    forbidden(w)
    return w
