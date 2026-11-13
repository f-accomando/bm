"""Victorian buildings seen from the game's 3/4 view, as 16x16 tiles.

A building is a rectangle of tiles: roof rows on top (ridge, slopes,
eaves), then FACADE_ROWS rows of front wall (upper storey with its windows,
string course, ground floor with windows and the door, plinth). Every
column of the front is a module drawn whole (16 x 80 pixels) and cut into
tiles, so windows and doors span rows without seams.

    facade_tiles() -> {(material, column kind, row): RGBA 16x16}
    roof_tiles()   -> {(material, row kind, side): RGBA 16x16}
"""
import math
import numpy as np

from tiles import RAMPS, c8

T = 16
FACADE_ROWS = 5
FH = T * FACADE_ROWS

RAMPS.update({
    'stucco': [c8(56, 48, 48), c8(96, 88, 80), c8(136, 128, 112), c8(176, 168, 144), c8(208, 200, 176)],
    'dark':   [c8(24, 24, 32), c8(40, 40, 48), c8(64, 64, 72), c8(96, 96, 104), c8(128, 128, 136)],
    'lead':   [c8(24, 32, 32), c8(48, 56, 56), c8(72, 80, 80), c8(104, 112, 104), c8(144, 152, 144)],
    'door':   [c8(24, 16, 16), c8(48, 32, 24), c8(72, 48, 32), c8(104, 72, 48)],
    'lit':    [c8(160, 72, 24), c8(216, 120, 40), c8(240, 176, 72), c8(248, 216, 128), c8(248, 240, 192)],
})


def col(ramp, v):
    r = RAMPS[ramp]
    return r[int(np.clip(round(v), 0, len(r) - 1))]


class Canvas:
    def __init__(self, w, h):
        self.a = np.zeros((h, w, 4), np.uint8)
        self.w, self.h = w, h

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.a[y, x, :3] = c
            self.a[y, x, 3] = 255

    def get(self, x, y):
        return tuple(self.a[y, x, :3])

    def rect(self, x0, y0, x1, y1, c):
        """inclusive"""
        for y in range(max(0, y0), min(self.h, y1 + 1)):
            for x in range(max(0, x0), min(self.w, x1 + 1)):
                self.put(x, y, c)

    def tiles(self):
        out = []
        for j in range(self.h // T):
            out.append(self.a[j * T:(j + 1) * T].copy())
        return out


# ---------------------------------------------------------------- walls

MATERIALS = ('brick', 'ashlar', 'stucco', 'dark')
# the trim (frames, sills, string course, quoins) of each wall material
TRIM = {'brick': 'stone', 'ashlar': 'warm', 'stucco': 'stone', 'dark': 'stone'}
GOTHIC = {'ashlar', 'dark'}            # pointed windows


def wall(cv, mat, seed):
    rng = np.random.default_rng(seed)
    if mat == 'brick':
        for y in range(cv.h):
            course = y // 4
            off = (course % 2) * 4
            for x in range(cv.w):
                if y % 4 == 3:
                    c = col('stone', 1)
                elif (x + off) % 8 == 7:
                    c = col('stone', 1)
                else:
                    b = (x + off) // 8 + course * 7
                    v = 2.0 + ((b * 37) % 5 - 2) * 0.25 + rng.normal(0, 0.08)
                    if y % 4 == 0:
                        v += 0.5
                    c = col('brick', v)
                cv.put(x, y, c)
    elif mat in ('ashlar', 'dark'):
        ramp = 'stone' if mat == 'ashlar' else 'dark'
        base = 2.2 if mat == 'ashlar' else 2.0
        for y in range(cv.h):
            course = y // 8
            off = (course % 2) * 6
            for x in range(cv.w):
                if y % 8 == 7 or (x + off) % 12 == 11:
                    c = col(ramp, base - 1.4)
                else:
                    b = (x + off) // 12 + course * 5
                    v = base + ((b * 29) % 5 - 2) * 0.18 + rng.normal(0, 0.09)
                    if y % 8 == 0 or (x + off) % 12 == 0:
                        v += 0.6
                    c = col(ramp, v)
                cv.put(x, y, c)
    else:   # stucco: smooth, rusticated (grooved) below the string course
        for y in range(cv.h):
            for x in range(cv.w):
                v = 2.4 + rng.normal(0, 0.06)
                if y > 36 and y % 6 == 5:
                    v = 1.2
                elif y > 36 and y % 6 == 0:
                    v = 3.0
                if rng.random() < 0.02:
                    v -= 0.8                # stains
                cv.put(x, y, col('stucco', v))


def cornice(cv, mat):
    t = TRIM[mat]
    for x in range(cv.w):
        cv.put(x, 0, col('ink', 0))
        cv.put(x, 1, col(t, 0))
        cv.put(x, 2, col(t, 3.6))
        cv.put(x, 3, col(t, 2.6))
        cv.put(x, 4, col(t, 3.0) if x % 3 else col(t, 1))   # dentils
        cv.put(x, 5, col(t, 1) if x % 3 else col(t, 2.2))
        cv.put(x, 6, col(t, 0.6))


def string_course(cv, mat, y=35):
    t = TRIM[mat]
    for x in range(cv.w):
        cv.put(x, y, col(t, 3.4))
        cv.put(x, y + 1, col(t, 2.2))
        cv.put(x, y + 2, col(t, 0.8))


def plinth(cv, mat, y0=72):
    for y in range(y0, FH):
        for x in range(cv.w):
            if y == y0:
                c = col('stone', 3.0)
            elif y == FH - 1:
                c = col('ink', 0)
            elif (x + (y // 4) * 5) % 9 == 0 or y == y0 + 4:
                c = col('dark', 0.5)
            else:
                c = col('dark', 2.0 + ((x // 9 + y // 4) % 2) * 0.4)
            cv.put(x, y, c)


def arch_hw(dy, a, h, pointed):
    """half width of an arch top `dy` pixels below its apex"""
    if dy >= h:
        return a
    if pointed:
        return a * (dy / h) ** 0.9
    return math.sqrt(max(0.0, a * a - (a - dy * a / h) ** 2))


def window(cv, mat, x0, y0, w, h, lit, seed, sill=True):
    """a window: stone frame, arched top, glass with glazing bars"""
    rng = np.random.default_rng(seed)
    t = TRIM[mat]
    pointed = mat in GOTHIC
    cx = x0 + (w - 1) / 2
    a = w / 2
    ah = 8 if pointed else 4
    # frame (one pixel of trim around the opening), keystone
    for y in range(y0 - 1, y0 + h + 1):
        hw = arch_hw(y - y0 + 1, a + 1, ah + 1, pointed)
        for x in range(x0 - 1, x0 + w + 1):
            if abs(x - cx) <= hw:
                cv.put(x, y, col(t, 3.0 if x - cx < 0 else 2.2))
    cv.put(int(round(cx)), y0 - 2, col(t, 3.4))
    cv.put(int(round(cx)) - 1, y0 - 2, col(t, 3.0))
    # glass
    for y in range(y0, y0 + h):
        hw = arch_hw(y - y0, a - 0.5, ah, pointed)
        for x in range(x0, x0 + w):
            if abs(x - cx) > hw:
                continue
            u = (y - y0) / h
            if lit:
                v = 1.6 + 2.2 * u - abs(x - cx) / a * 0.9 + rng.normal(0, 0.15)
                c = col('lit', v)
            else:
                v = 1.0 + 0.6 * (1 - u) + rng.normal(0, 0.1)
                if (x - x0) + (y - y0) // 2 in (2, 3) and u < 0.5:
                    v = 2.4                     # the reflection of the sky
                c = col('glass', v)
            cv.put(x, y, c)
    # glazing bars, and a curtain in some lit windows
    bar = col('door', 0) if lit else col('ink', 0)
    for y in range(y0 + 2, y0 + h):
        cv.put(int(cx), y, bar)
    for yy in (y0 + ah + 2, y0 + ah + 2 + (h - ah) // 2):
        for x in range(x0, x0 + w):
            if abs(x - cx) <= arch_hw(yy - y0, a - 0.5, ah, pointed):
                cv.put(x, yy, bar)
    if lit and rng.random() < 0.5:
        side = x0 if rng.random() < 0.5 else x0 + w - 2
        for y in range(y0 + ah, y0 + h):
            cv.put(side, y, col('lit', 0))
            cv.put(side + 1, y, col('lit', 0.6))
    if sill:
        for x in range(x0 - 2, x0 + w + 2):
            cv.put(x, y0 + h + 1, col(t, 3.4))
            cv.put(x, y0 + h + 2, col(t, 1.8))
            cv.put(x, y0 + h + 3, col('ink', 0) if mat != 'stucco' else col('stucco', 1))


def door(cv, mat, x0, y0, w, lit_fan, seed):
    rng = np.random.default_rng(seed)
    t = TRIM[mat]
    pointed = mat in GOTHIC
    cx = x0 + (w - 1) / 2
    a = w / 2
    ah = 9 if pointed else 6
    h = FH - 3 - y0
    # surround
    for y in range(y0 - 2, FH - 2):
        hw = arch_hw(y - y0 + 2, a + 2, ah + 2, pointed)
        for x in range(x0 - 2, x0 + w + 2):
            if abs(x - cx) <= hw:
                cv.put(x, y, col(t, 3.2 if x < cx else 2.4) if abs(x - cx) > hw - 1.2 or y < y0 else col(t, 1.4))
    # the fanlight in the arch, then the two leaves
    for y in range(y0, FH - 3):
        hw = arch_hw(y - y0, a, ah, pointed)
        for x in range(x0, x0 + w):
            if abs(x - cx) > hw:
                continue
            if y < y0 + ah:
                c = col('lit', 1.5 + (y - y0) * 0.3) if lit_fan else col('glass', 1.2)
                if x == int(cx) or (y - y0) == ah - 1:
                    c = col('door', 0)
            else:
                leaf = 0 if x < cx else 1
                lx = x - x0 if leaf == 0 else x0 + w - 1 - x
                v = 2.0 + rng.normal(0, 0.1)
                if lx == 0 or x == int(cx) or x == int(cx) + 1:
                    v = 0.4
                elif (y - y0 - ah) % 14 in (0, 13) or lx == 1:
                    v = 1.2                     # panel mouldings
                elif (y - y0 - ah) % 14 == 1:
                    v = 3.0
                c = col('door', v)
            cv.put(x, y, c)
    # knob, step
    cv.put(int(cx) - 2, y0 + ah + 18, col('glow', 3))
    cv.put(int(cx) + 3, y0 + ah + 18, col('glow', 3))
    for x in range(x0 - 3, x0 + w + 3):
        cv.put(x, FH - 3, col('stone', 3.8))
        cv.put(x, FH - 2, col('stone', 2.2))
        cv.put(x, FH - 1, col('ink', 0))


def quoins(cv, mat, side):
    """corner stones at the left (side -1) or right (+1) end of a front"""
    t = TRIM[mat]
    for y in range(7, 72):
        k = (y - 7) // 6
        long = k % 2 == 0
        n = 6 if long else 4
        for i in range(n):
            x = i if side < 0 else T - 1 - i
            if (y - 7) % 6 == 5:
                c = col(t, 0.8)
            elif i == n - 1:
                c = col(t, 1.6)
            else:
                c = col(t, 3.0 - (0.6 if side > 0 else 0) + (0.4 if (y - 7) % 6 == 0 else 0))
            cv.put(x, y, c)
    # the corner itself: a dark edge on the far side (in shadow)
    x = 0 if side < 0 else T - 1
    for y in range(0, FH):
        cv.put(x, y, col('ink', 0) if side > 0 else col(t, 3.6) if 7 <= y < 72 else cv.get(x, y))


def facade_column(mat, kind, seed):
    """kind: plain, left, right, win (upper dark / ground dark), and the lit
    versions: upper window lit (win_u), ground window lit (win_g), both
    (win_ug), door, door_lit (lit fanlight)"""
    cv = Canvas(T, FH)
    wall(cv, mat, seed)
    cornice(cv, mat)
    string_course(cv, mat)
    plinth(cv, mat)
    if kind.startswith('win'):
        upper_lit = kind in ('win_u', 'win_ug')
        ground_lit = kind in ('win_g', 'win_ug')
        window(cv, mat, 3, 11, 10, 18, upper_lit, seed + 1)
        window(cv, mat, 3, 44, 10, 20, ground_lit, seed + 2)
        # a basement grate under the ground window
        for x in range(4, 12):
            cv.put(x, 75, col('ink', 0) if x % 2 else col('iron', 2))
    elif kind.startswith('door'):
        window(cv, mat, 3, 11, 10, 18, kind == 'door_lit', seed + 1)
        door(cv, mat, 2, 42, 12, kind == 'door_lit', seed + 3)
    elif kind in ('left', 'right'):
        quoins(cv, mat, -1 if kind == 'left' else 1)
    return cv.tiles()


FACADE_KINDS = ('plain', 'left', 'right', 'win', 'win_u', 'win_g', 'win_ug', 'door', 'door_lit')


def facade_tiles():
    out = {}
    for mi, mat in enumerate(MATERIALS):
        for ki, kind in enumerate(FACADE_KINDS):
            for row, tile in enumerate(facade_column(mat, kind, 100 + mi * 10 + ki)):
                out[(mat, kind, row)] = tile
    return out


# ---------------------------------------------------------------- roofs

ROOFS = ('slate', 'tile', 'lead', 'copper')


def shingles(cv, roof, y0, y1, seed, base):
    """fills rows y0..y1 with the roof covering, lighter towards the ridge"""
    rng = np.random.default_rng(seed)
    ramp = {'slate': 'slate', 'tile': 'tile', 'lead': 'lead', 'copper': 'copper'}[roof]
    n = len(RAMPS[ramp])
    for y in range(y0, y1 + 1):
        for x in range(cv.w):
            v = base(y) + rng.normal(0, 0.12)
            if roof == 'slate':
                course = y // 4
                off = (course % 2) * 4
                if y % 4 == 3:
                    v -= 1.1
                elif (x + off) % 8 == 0:
                    v -= 0.8
                elif y % 4 == 0:
                    v += 0.4
                v += (((x + off) // 8 * 13 + course * 7) % 3 - 1) * 0.15
            elif roof == 'tile':
                course = y // 4
                off = (course % 2) * 3
                u = (x + off) % 6
                if y % 4 == 3:
                    v -= 1.0
                v += [-.7, -.1, .3, .4, .1, -.4][u]           # round pantiles
            elif roof == 'lead':
                if x % 8 == 0:
                    v += 0.9                                  # standing seams
                elif x % 8 == 1:
                    v -= 0.7
                if y % 12 == 11:
                    v -= 0.5
            else:   # copper with verdigris streaks
                if x % 6 == 0:
                    v += 0.8
                elif x % 6 == 1:
                    v -= 0.6
                if (x * 7 + seed) % 11 < 2:
                    v += 0.5
            cv.put(x, y, col(ramp, np.clip(v, 0, n - 1)))


def roof_column(roof, side, seed):
    """three rows: ridge, slope, eaves; side 'l', 'm' or 'r'"""
    cv = Canvas(T, 3 * T)
    ramp = {'slate': 'slate', 'tile': 'tile', 'lead': 'lead', 'copper': 'copper'}[roof]
    top = len(RAMPS[ramp]) - 1
    # the front slope: lighter near the ridge, darker towards the eaves
    shingles(cv, roof, 0, 3 * T - 1, seed, lambda y: top * 0.8 - y / (3 * T) * top * 0.55)
    # beyond the ridge: a sliver of the back slope, in shade
    for y in range(0, 4):
        for x in range(T):
            cv.put(x, y, col(ramp, 0.6 + (y == 3) * 0.3))
    for x in range(T):
        cv.put(x, 0, col('ink', 0))
        # the ridge cap
        cv.put(x, 4, col(ramp, top))
        cv.put(x, 5, col(ramp, top - 0.6) if x % 4 else col(ramp, top - 1.6))
        cv.put(x, 6, col(ramp, 0.4))
    # eaves: the last course, the gutter, its shadow
    for x in range(T):
        cv.put(x, 3 * T - 6, col(ramp, top - 0.4))
        cv.put(x, 3 * T - 4, col('iron', 2.6))
        cv.put(x, 3 * T - 3, col('iron', 1.2))
        cv.put(x, 3 * T - 2, col('iron', 0.6))
        cv.put(x, 3 * T - 1, col('ink', 0))
    # gable verge at the ends: a bargeboard and the outline
    if side in 'lr':
        xs = (0, 1, 2) if side == 'l' else (T - 1, T - 2, T - 3)
        for y in range(0, 3 * T):
            cv.put(xs[0], y, col('ink', 0))
            cv.put(xs[1], y, col('wood', 2.6 if side == 'l' else 1.4))
            cv.put(xs[2], y, col('wood', 1.0))
    return cv.tiles()


def roof_tiles():
    out = {}
    for ri, roof in enumerate(ROOFS):
        for side in 'lmr':
            for row, tile in zip(('ridge', 'slope', 'eaves'), roof_column(roof, side, 300 + ri * 7)):
                out[(roof, row, side)] = tile
        # a second slope with other shingles, for long roofs
        for side in 'lmr':
            t = roof_column(roof, side, 400 + ri * 7)
            out[(roof, 'slope2', side)] = t[1]
    return out


def preview(path):
    from PIL import Image
    # a little street front: two houses of every kind
    rows = []
    for mi, mat in enumerate(MATERIALS):
        roof = ROOFS[mi]
        kinds = ['left', 'win', 'win_u', 'door_lit', 'win_g', 'plain', 'win', 'right']
        R = roof_tiles()
        F = facade_tiles()
        cols = []
        for i, k in enumerate(kinds):
            side = 'l' if i == 0 else 'r' if i == len(kinds) - 1 else 'm'
            col_ = [R[(roof, 'ridge', side)], R[(roof, 'slope', side)], R[(roof, 'eaves', side)]]
            col_ += [F[(mat, k, r)] for r in range(FACADE_ROWS)]
            cols.append(np.concatenate(col_, 0))
        rows.append(np.concatenate(cols, 1))
    img = np.concatenate(rows, 1)
    bg = np.zeros_like(img)
    bg[:, :, :3] = (40, 44, 52)
    bg[:, :, 3] = 255
    a = img[:, :, 3:4] / 255.0
    out = (img[:, :, :3] * a + bg[:, :, :3] * (1 - a)).astype(np.uint8)
    Image.fromarray(out).resize((out.shape[1] * 3, out.shape[0] * 3), Image.NEAREST).save(path)


if __name__ == '__main__':
    import sys
    preview(sys.argv[1])
