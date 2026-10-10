"""The placeholders of Yharnam's drawn map: the layer "objects" (carts/yharnam/map_objects.csv). A 16x16 tile
for each kind of thing the game makes where it is placed (a house, a tree, a street lamp...). The game never
draws them: they are seen in the SDK's map page, where they are placed with the 16x16 brush (z).

A tag in the colour of its family (houses red, plants green, lamps and fires orange, graves blue, the other
things purple) with two letters. A house is its corner tile (top left: its height, H7 / H8, or a chapel), the
roof tiles to its right (its width: a hatch under a red band) and the body tiles over the rest of its footprint
(a hatch: only to be seen, the game reads the corner and the roof).

Only colours the sheet already has: its palette (SHEET8) and the game's fades stay as they are. The order of
KINDS is the order of the tiles on the sheet: append, never insert (a tile keeps its cell, the map its
numbers)."""
import numpy as np

T = 16
INK, TEXT = (0x10, 0x10, 0x18), (0xF8, 0xF8, 0xD8)        # the frame, the letters
FAMILY = {'house': (0xB8, 0x48, 0x48), 'plant': (0x40, 0x70, 0x60), 'fire': (0xD8, 0x78, 0x28),
          'grave': (0x58, 0x70, 0x98), 'thing': (0xA0, 0x80, 0xA0)}
# (kind, family, letters); body: no letters, a hatch
KINDS = [('house7', 'house', 'H7'), ('house8', 'house', 'H8'), ('chapel', 'house', 'CH'), ('body', 'house', None),
         ('tree', 'plant', 'TR'), ('bush', 'plant', 'BU'),
         ('lamp', 'fire', 'LA'), ('brazier', 'fire', 'BR'), ('pyre', 'fire', 'PY'),
         ('grave', 'grave', 'GR'), ('mausoleum', 'grave', 'MA'), ('angel', 'grave', 'AN'),
         ('fountain', 'thing', 'FO'), ('well', 'thing', 'WE'), ('bench', 'thing', 'BE'),
         ('fence_h', 'thing', 'F-'), ('fence_v', 'thing', 'F|'), ('crates', 'thing', 'BX'),
         ('coffin', 'thing', 'CO'), ('cage', 'thing', 'CA'), ('bollard', 'thing', 'BO'),
         ('carriage', 'thing', 'CG'), ('roof', 'house', None)]

# letters of 3 x 5 pixels
FONT = {
    'A': ('.x.', 'x.x', 'xxx', 'x.x', 'x.x'), 'B': ('xx.', 'x.x', 'xx.', 'x.x', 'xx.'),
    'C': ('.xx', 'x..', 'x..', 'x..', '.xx'), 'E': ('xxx', 'x..', 'xx.', 'x..', 'xxx'),
    'F': ('xxx', 'x..', 'xx.', 'x..', 'x..'), 'G': ('.xx', 'x..', 'x.x', 'x.x', '.xx'),
    'H': ('x.x', 'x.x', 'xxx', 'x.x', 'x.x'), 'L': ('x..', 'x..', 'x..', 'x..', 'xxx'),
    'M': ('x.x', 'xxx', 'xxx', 'x.x', 'x.x'), 'O': ('.x.', 'x.x', 'x.x', 'x.x', '.x.'),
    'P': ('xx.', 'x.x', 'xx.', 'x..', 'x..'), 'R': ('xx.', 'x.x', 'xx.', 'x.x', 'x.x'),
    'T': ('xxx', '.x.', '.x.', '.x.', '.x.'), 'U': ('x.x', 'x.x', 'x.x', 'x.x', 'xxx'),
    'W': ('x.x', 'x.x', 'xxx', 'xxx', 'x.x'), 'X': ('x.x', 'x.x', '.x.', 'x.x', 'x.x'),
    'Y': ('x.x', 'x.x', '.x.', '.x.', '.x.'), 'N': ('xx.', 'x.x', 'x.x', 'x.x', 'x.x'),
    '7': ('xxx', '..x', '.x.', '.x.', '.x.'), '8': ('xxx', 'x.x', 'xxx', 'x.x', 'xxx'),
    '-': ('...', '...', 'xxx', '...', '...'), '|': ('.x.', '.x.', '.x.', '.x.', '.x.'),
}


def tag(family, letters, band=False):
    img = np.zeros((T, T, 4), np.uint8)
    if letters is None:                       # a house's body: a hatch, the ground seen through; its roof: a band
        for y in range(T):
            for x in range(T):
                if (x + y) % 4 == 0 or band and 1 <= y <= 3:
                    img[y, x] = FAMILY[family] + (255,)
        return img
    img[1:T - 1, 1:T - 1] = INK + (255,)      # the frame
    img[2:T - 2, 2:T - 2] = FAMILY[family] + (255,)
    for k, ch in enumerate(letters):
        for j, row in enumerate(FONT[ch]):
            for i, c in enumerate(row):
                if c == 'x':
                    img[5 + j, 4 + k * 4 + i] = TEXT + (255,)
    return img


TILES = {k: tag(f, t, k == 'roof') for k, f, t in KINDS}
