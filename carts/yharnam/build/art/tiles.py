"""Ground textures for the gothic town: 16x16 tiles, limited ramps."""
import math
import numpy as np

T = 16

def c8(*v):
    return tuple(int(x) // 8 * 8 for x in v)

RAMPS = {
    'ink':    [c8(16, 8, 24)],
    'stone':  [c8(32, 32, 40), c8(56, 56, 64), c8(80, 80, 88), c8(112, 108, 112), c8(144, 140, 136), c8(176, 168, 160)],
    'warm':   [c8(48, 40, 40), c8(80, 64, 56), c8(112, 96, 80), c8(144, 128, 104), c8(184, 160, 128)],
    'brick':  [c8(48, 24, 24), c8(80, 40, 32), c8(112, 56, 40), c8(144, 80, 56), c8(176, 112, 80)],
    'slate':  [c8(24, 32, 40), c8(40, 48, 64), c8(64, 72, 88), c8(96, 104, 120), c8(128, 136, 152)],
    'tile':   [c8(56, 24, 24), c8(96, 40, 32), c8(136, 64, 48), c8(168, 96, 64)],
    'copper': [c8(24, 48, 48), c8(40, 80, 72), c8(64, 112, 96), c8(104, 152, 128)],
    'wood':   [c8(40, 24, 16), c8(72, 48, 32), c8(104, 72, 40), c8(144, 104, 64)],
    'iron':   [c8(16, 16, 24), c8(40, 40, 48), c8(72, 72, 80), c8(112, 112, 120)],
    'grass':  [c8(16, 24, 24), c8(32, 40, 32), c8(48, 64, 40), c8(72, 88, 56), c8(104, 112, 72)],
    'dirt':   [c8(40, 32, 24), c8(64, 48, 40), c8(96, 72, 56), c8(128, 104, 72)],
    'water':  [c8(8, 24, 40), c8(16, 40, 64), c8(32, 64, 88), c8(64, 104, 128), c8(128, 160, 176)],
    'marble': [c8(72, 72, 80), c8(112, 112, 120), c8(160, 160, 160), c8(200, 200, 192), c8(232, 232, 224)],
    'moss':   [c8(32, 48, 24), c8(56, 72, 32), c8(80, 104, 48)],
    'blood':  [c8(64, 8, 16), c8(104, 16, 24), c8(144, 32, 32)],
    'glass':  [c8(16, 24, 48), c8(32, 48, 80), c8(64, 80, 112)],
    'glow':   [c8(152, 48, 16), c8(216, 104, 32), c8(248, 160, 56), c8(248, 216, 120), c8(248, 248, 208)],
    'leaf':   [c8(56, 32, 16), c8(96, 56, 24), c8(136, 88, 32)],
    'bone':   [c8(120, 112, 96), c8(176, 168, 144), c8(224, 216, 192)],
}

def col(ramp, v):
    r = RAMPS[ramp]
    i = int(np.clip(round(v), 0, len(r) - 1))
    return r[i]

def blank(w=T, h=T):
    return np.zeros((h, w, 4), np.uint8)

def put(img, x, y, c):
    h, w = img.shape[:2]
    if 0 <= x < w and 0 <= y < h:
        img[y, x, :3] = c
        img[y, x, 3] = 255

def fill_from(img, idx, ramp):
    """idx: float array of ramp positions (nan = keep)"""
    r = np.array(RAMPS[ramp], np.uint8)
    m = ~np.isnan(idx)
    k = np.clip(np.rint(np.nan_to_num(idx)), 0, len(r) - 1).astype(int)
    img[m, :3] = r[k[m]]
    img[m, 3] = 255

def tor(d, n=T):
    d = np.abs(d)
    return np.minimum(d, n - d)

def voronoi(rng, n, w=T, h=T, jitter=0.8, grid=None):
    """wrapping Voronoi: per pixel nearest / second distance, cell id, offset"""
    if grid:
        gx, gy = grid
        pts = []
        for j in range(gy):
            for i in range(gx):
                pts.append(((i + 0.5 + rng.uniform(-jitter, jitter) * 0.5) * w / gx,
                            (j + 0.5 + rng.uniform(-jitter, jitter) * 0.5) * h / gy))
    else:
        pts = [(rng.uniform(0, w), rng.uniform(0, h)) for _ in range(n)]
    pts = np.array(pts)
    yy, xx = np.mgrid[0:h, 0:w] + 0.5
    dx = xx[None] - pts[:, 0, None, None]
    dy = yy[None] - pts[:, 1, None, None]
    dx = np.where(dx > w / 2, dx - w, np.where(dx < -w / 2, dx + w, dx))
    dy = np.where(dy > h / 2, dy - h, np.where(dy < -h / 2, dy + h, dy))
    d = np.sqrt(dx * dx + dy * dy)
    order = np.argsort(d, axis=0)
    i0 = order[0]
    d0 = np.take_along_axis(d, order[0:1], 0)[0]
    d1 = np.take_along_axis(d, order[1:2], 0)[0]
    ox = np.take_along_axis(dx, order[0:1], 0)[0]
    oy = np.take_along_axis(dy, order[0:1], 0)[0]
    return i0, d0, d1, ox, oy, len(pts)

# ---------------------------------------------------------------- ground

def cobbles(seed, moss=0.0, wet=0.0, ramp='stone', n=(3, 3)):
    rng = np.random.default_rng(seed)
    img = blank()
    i0, d0, d1, ox, oy, np_ = voronoi(rng, 0, grid=n, jitter=0.9)
    tone = rng.uniform(-0.6, 0.6, np_)
    warm = rng.random(np_) < 0.18
    r = 3.6
    shade = 2.7 + tone[i0] - 0.55 * (ox + oy) / r - 0.25 * (d0 / r) ** 2
    shade += rng.normal(0, 0.18, shade.shape)
    joint = (d1 - d0) < 0.75
    idx = np.where(joint, np.nan, shade)
    fill_from(img, np.where(warm[i0] | joint, np.nan, idx), ramp)
    fill_from(img, np.where(~warm[i0] | joint, np.nan, idx - 0.4), 'warm')
    # joints: dark, mossy here and there
    jt = np.where(joint, 0.0, np.nan)
    fill_from(img, jt, ramp)
    if moss:
        mm = joint & (rng.random(joint.shape) < moss)
        fill_from(img, np.where(mm, rng.integers(0, 2, joint.shape).astype(float), np.nan), 'moss')
    # the highlight on the upper left rim of each stone
    rim = (~joint) & (ox < -0.3) & (oy < -0.3) & (d0 > r * 0.55) & (d0 < r * 0.95)
    fill_from(img, np.where(rim, np.clip(shade + 1.0, 0, 5), np.nan), ramp)
    if wet:
        # puddle sheen: a few stones catch light
        sel = rng.random(np_) < wet
        sh = (~joint) & sel[i0] & (ox < 0) & (oy < 0) & (d0 < r * 0.5)
        fill_from(img, np.where(sh, 5.0, np.nan), ramp)
    return img

def setts(seed, ramp='stone', course=4):
    """granite setts in courses, staggered"""
    rng = np.random.default_rng(seed)
    img = blank()
    for row in range(T // course):
        y0 = row * course
        x = -rng.integers(0, 6)
        while x < T:
            w = int(rng.integers(5, 8))
            tone = 2.4 + rng.uniform(-0.7, 0.7)
            for yy in range(y0, y0 + course):
                for xx in range(x, x + w):
                    px = xx % T
                    if yy == y0 + course - 1 or xx == x + w - 1:
                        put(img, px, yy, col(ramp, 0))
                    elif yy == y0:
                        put(img, px, yy, col(ramp, tone + 1.0))
                    elif xx == x:
                        put(img, px, yy, col(ramp, tone + 0.6))
                    elif yy == y0 + course - 2:
                        put(img, px, yy, col(ramp, tone - 0.7))
                    else:
                        v = tone + rng.normal(0, 0.25)
                        put(img, px, yy, col(ramp, v))
            x += w
    return img

def flags(seed, ramp='warm', cracks=1):
    """big flagstones: one to four slabs per tile, in several layouts"""
    rng = np.random.default_rng(seed)
    img = blank()
    layout = seed % 5
    ysplit = int(rng.integers(6, 11))
    if layout == 0:
        slabs = [(0, 0, T, T)]
    elif layout == 1:
        slabs = [(0, 0, T, ysplit), (0, ysplit, T, T)]
    elif layout == 2:
        xs = int(rng.integers(5, 12))
        slabs = [(0, 0, xs, T), (xs, 0, T, T)]
    elif layout == 3:
        xs = int(rng.integers(5, 12))
        slabs = [(0, 0, T, ysplit), (0, ysplit, xs, T), (xs, ysplit, T, T)]
    else:
        slabs = []
        for (ya, yb) in ((0, ysplit), (ysplit, T)):
            xs = int(rng.integers(5, 12))
            slabs += [(0, ya, xs, yb), (xs, ya, T, yb)]
    for (xa, ya, xb, yb) in slabs:
        tone = 2.3 + rng.uniform(-0.5, 0.5)
        for y in range(ya, yb):
            for x in range(xa, xb):
                if y == yb - 1 or x == xb - 1:
                    c = col(ramp, 0)
                elif y == ya or x == xa:
                    c = col(ramp, tone + 0.8)
                elif y == yb - 2 or x == xb - 2:
                    c = col(ramp, tone - 0.6)
                else:
                    c = col(ramp, tone + rng.normal(0, 0.22))
                put(img, x, y, c)
    for _ in range(cracks):
        x, y = rng.integers(2, 14, 2)
        for _ in range(int(rng.integers(3, 7))):
            put(img, x, y, col(ramp, 0))
            x += int(rng.integers(-1, 2)); y += int(rng.integers(0, 2))
    return img

def grass(seed, dark=0.0):
    rng = np.random.default_rng(seed)
    img = blank()
    base = 1.4 - dark + rng.normal(0, 0.25, (T, T))
    fill_from(img, base, 'grass')
    for _ in range(26):
        x, y = rng.integers(0, T, 2)
        h = int(rng.integers(2, 4))
        v = 2.6 - dark + rng.uniform(-0.4, 0.6)
        for k in range(h):
            put(img, x, (y - k) % T, col('grass', v - k * 0.5))
        put(img, x, (y + 1) % T, col('grass', 0))
    return img

def dirt(seed):
    rng = np.random.default_rng(seed)
    img = blank()
    base = 1.5 + rng.normal(0, 0.3, (T, T))
    fill_from(img, base, 'dirt')
    for _ in range(7):
        x, y = rng.integers(0, T, 2)
        put(img, x, y, col('stone', 3)); put(img, x, (y + 1) % T, col('dirt', 0))
    return img

def water(frame, seed=5):
    rng = np.random.default_rng(seed)
    img = blank()
    yy, xx = np.mgrid[0:T, 0:T]
    ph = frame * math.pi / 2
    v = 1.2 + 0.5 * np.sin((xx * 0.8 + yy * 0.4) * 0.785 + ph) + 0.4 * np.sin((yy * 0.9 - xx * 0.3) * 0.785 * 2 - ph)
    fill_from(img, v, 'water')
    # glints
    g = (np.sin((xx + yy * 2) * 0.785 + ph) > 0.92) & (np.sin(yy * 1.57 + ph) > 0.5)
    fill_from(img, np.where(g, 3.0, np.nan), 'water')
    return img

def marble(seed, dark):
    rng = np.random.default_rng(seed)
    img = blank()
    base = (1.0 if dark else 3.0) + rng.normal(0, 0.15, (T, T))
    fill_from(img, base, 'marble')
    x, y = rng.integers(0, T, 2)
    for _ in range(14):
        put(img, x % T, y % T, col('marble', base[0, 0] - 1))
        x += 1; y += int(rng.integers(-1, 2))
    for k in range(T):
        put(img, k, T - 1, col('marble', 0)); put(img, T - 1, k, col('marble', 0))
    return img


def pave(seed, ramp='stone'):
    """sidewalk: regular square slabs with chipped corners and cracks"""
    rng = np.random.default_rng(seed)
    img = blank()
    for sy in (0, 8):
        for sx in (0, 8):
            tone = 2.9 + rng.uniform(-0.4, 0.4)
            for y in range(sy, sy + 8):
                for x in range(sx, sx + 8):
                    if y == sy + 7 or x == sx + 7:
                        c = col(ramp, 0.6)
                    elif y == sy or x == sx:
                        c = col(ramp, tone + 0.7)
                    else:
                        c = col(ramp, tone + rng.normal(0, 0.15))
                    put(img, x, y, c)
    if rng.random() < 0.6:
        x, y = rng.integers(1, 15, 2)
        for _ in range(5):
            put(img, x, y, col(ramp, 0.6))
            x += int(rng.integers(-1, 2)); y += 1
    return img


def gravel(seed):
    rng = np.random.default_rng(seed)
    img = blank()
    base = 1.6 + rng.normal(0, 0.35, (T, T))
    fill_from(img, base, 'dirt')
    for _ in range(40):
        x, y = rng.integers(0, T, 2)
        v = rng.uniform(1.5, 4.2)
        put(img, x, y, col('stone', v))
        if v > 3:
            put(img, x, (y + 1) % T, col('stone', 1))
    return img


def planks(seed):
    rng = np.random.default_rng(seed)
    img = blank()
    for y in range(T):
        k = y // 4
        for x in range(T):
            if y % 4 == 3:
                c = col('wood', 0)
            else:
                v = 1.8 + ((k * 7) % 3 - 1) * 0.3 + 0.25 * math.sin(x * 0.7 + k * 2) + rng.normal(0, 0.1)
                if (x + k * 5) % 16 == 0:
                    v = 0.5
                c = col('wood', v)
            put(img, x, y, c)
    return img


def marble_check(seed):
    rng = np.random.default_rng(seed)
    img = blank()
    for y in range(T):
        for x in range(T):
            dark = ((x // 8) + (y // 8)) % 2
            v = (0.8 if dark else 3.0) + rng.normal(0, 0.1)
            if x % 8 == 7 or y % 8 == 7:
                v = 0.4 if dark else 1.8
            put(img, x, y, col('marble', v))
    for _ in range(2):
        x, y = rng.integers(0, T, 2)
        for _ in range(6):
            if 0 <= x < T and 0 <= y < T:
                c = img[y, x, :3]
                put(img, x, y, col('marble', 1.6 if c[0] > 120 else 0.2))
            x += 1; y += int(rng.integers(-1, 2))
    return img


# ---------------------------------------------------------------- overlays

def curb(mask):
    """the granite kerb where a road meets a sidewalk: bits N=1, E=2, S=4, W=8
    (the neighbours that are not road)"""
    img = blank()
    if mask & 1:                      # north: the kerb's face looks at us
        for x in range(T):
            put(img, x, 0, col('stone', 4.4))
            put(img, x, 1, col('stone', 2.6))
            put(img, x, 2, col('stone', 1.6))
            put(img, x, 3, col('ink', 0) if x % 5 else col('stone', 0.6))
    if mask & 4:                      # south: only its top
        for x in range(T):
            put(img, x, T - 2, col('stone', 3.6))
            put(img, x, T - 1, col('stone', 4.4))
    if mask & 8:
        for y in range(T):
            put(img, 0, y, col('stone', 4.2))
            put(img, 1, y, col('stone', 2.2))
    if mask & 2:
        for y in range(T):
            put(img, T - 1, y, col('stone', 3.0))
            put(img, T - 2, y, col('stone', 1.4))
    return img


def grass_edge(mask, seed=3):
    """grass spilling over from the neighbours in `mask` (N=1, E=2, S=4, W=8)"""
    rng = np.random.default_rng(seed + mask)
    img = blank()
    for x in range(T):
        for k, (dx, dy) in enumerate(((0, -1), (1, 0), (0, 1), (-1, 0))):
            if not mask & (1 << k):
                continue
            depth = int(rng.integers(1, 4))
            for d in range(depth):
                if dy == -1:
                    px, py = x, d
                elif dy == 1:
                    px, py = x, T - 1 - d
                elif dx == 1:
                    px, py = T - 1 - d, x
                else:
                    px, py = d, x
                put(img, px, py, col('grass', 2.4 - d * 0.6 + rng.uniform(-0.3, 0.3)))
    return img


def decal(kind, seed):
    rng = np.random.default_rng(seed)
    img = blank()
    yy, xx = np.mgrid[0:T, 0:T] + 0.5
    if kind == 'puddle':
        cx, cy = rng.uniform(6, 10, 2)
        n = 0.0
        for k in range(3):
            n = n + np.sin(xx * rng.uniform(0.5, 1.2) + rng.uniform(0, 6)) * np.sin(yy * rng.uniform(0.5, 1.2) +
                                                                                 rng.uniform(0, 6))
        d = ((xx - cx) / 6.5) ** 2 + ((yy - cy) / 4.2) ** 2 + n * 0.12
        m = d < 1
        fill_from(img, np.where(m, 1.0 + (yy < cy - 1) * 0.6, np.nan), 'water')
        fill_from(img, np.where(m & (d > 0.75), 0.2, np.nan), 'water')
        hl = m & (np.abs(yy - cy + 1.5) < 0.5) & (np.abs(xx - cx + 1) < 2)
        fill_from(img, np.where(hl, 3.0, np.nan), 'water')
    elif kind == 'leaves':
        for _ in range(9):
            x, y = rng.integers(1, 15, 2)
            v = rng.uniform(0, 2.4)
            put(img, x, y, col('leaf', v))
            put(img, x + 1, y, col('leaf', v + 0.5))
            if rng.random() < 0.5:
                put(img, x, y + 1, col('leaf', v - 0.6))
    elif kind == 'blood':
        cx, cy = rng.uniform(5, 11, 2)
        for _ in range(6):
            r = rng.uniform(1.0, 3.2)
            ox, oy = rng.normal(0, 2.2, 2)
            m = ((xx - cx - ox) ** 2 + (yy - cy - oy) ** 2) < r * r
            fill_from(img, np.where(m, rng.uniform(0.5, 1.6), np.nan), 'blood')
        for _ in range(5):
            put(img, int(rng.integers(0, T)), int(rng.integers(0, T)), col('blood', 1))
    elif kind == 'crack':
        x, y = rng.integers(2, 6), rng.integers(2, 14)
        for _ in range(14):
            put(img, x, y, col('ink', 0))
            if rng.random() < 0.3:
                put(img, x, y + 1, col('stone', 1))
            x += 1; y += int(rng.integers(-1, 2))
    elif kind == 'drain':
        for y in range(5, 11):
            for x in range(3, 13):
                edge = y in (5, 10) or x in (3, 12)
                put(img, x, y, col('iron', 2.4) if edge else (col('ink', 0) if x % 2 else col('iron', 1.2)))
    elif kind == 'manhole':
        d = (xx - 8) ** 2 + (yy - 8) ** 2
        m = d < 30
        fill_from(img, np.where(m, 1.6 + ((xx + yy).astype(int) % 3 == 0) * 0.8, np.nan), 'iron')
        fill_from(img, np.where((d >= 24) & (d < 30), 0.4, np.nan), 'iron')
        fill_from(img, np.where((d >= 30) & (d < 36), 3.0, np.nan), 'stone')
    elif kind == 'bones':
        for (x0, y0, x1, y1) in ((3, 9, 10, 6), (6, 11, 12, 12)):
            n = max(abs(x1 - x0), abs(y1 - y0))
            for k in range(n + 1):
                x = round(x0 + (x1 - x0) * k / n)
                y = round(y0 + (y1 - y0) * k / n)
                put(img, x, y, col('bone', 2))
                put(img, x, y + 1, col('bone', 0))
        for (x, y) in ((3, 9), (10, 6), (6, 11), (12, 12)):
            put(img, x - 1, y, col('bone', 2)); put(img, x + 1, y, col('bone', 1))
        put(img, 10, 3, col('bone', 2)); put(img, 11, 3, col('bone', 2)); put(img, 10, 4, col('bone', 1))
        put(img, 11, 4, col('ink', 0))
    elif kind == 'straw':
        for _ in range(16):
            x, y = rng.integers(1, 15), rng.integers(2, 14)
            ln = int(rng.integers(2, 5))
            dx = int(rng.choice([-1, 1]))
            for k in range(ln):
                put(img, x + k * dx, y - (k // 2), col('glow', 1.0) if k % 2 else col('wood', 3))
    return img


GROUND = {
    'cobble': [cobbles(s) for s in range(4)],
    'cobble_warm': [cobbles(20 + s, ramp='warm') for s in range(2)],
    'cobble_moss': [cobbles(30 + s, moss=0.45) for s in range(2)],
    'cobble_wet': [cobbles(40 + s, wet=0.35) for s in range(2)],
    'setts': [setts(s) for s in range(3)],
    'flags': [flags(s) for s in range(5)],
    'flags_dark': [flags(10 + s, 'stone') for s in range(5)],
    'pave': [pave(s) for s in range(3)],
    'grass': [grass(s) for s in range(3)],
    'grass_dark': [grass(10 + s, dark=0.5) for s in range(2)],
    'dirt': [dirt(s) for s in range(2)],
    'gravel': [gravel(s) for s in range(2)],
    'marble': [marble_check(s) for s in range(2)],
    'planks': [planks(0)],
    'water': [water(f) for f in range(4)],
}
CURBS = {m: curb(m) for m in range(1, 16)}
GRASS_EDGES = {m: grass_edge(m) for m in range(1, 16)}
DECALS = {k: [decal(k, s) for s in range(n)] for k, n in (('puddle', 3), ('leaves', 2), ('blood', 2), ('crack', 2),
                                                           ('drain', 1), ('manhole', 1), ('bones', 1), ('straw', 1))}
