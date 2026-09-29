"""Titan Clash: the pictures other than the robot (used by mkassets.py).

Pixel art drawn with a few fixed palettes (the whole sheet must stay within
255 colours). Sizes are in screen pixels; the robots are about 190 tall.
"""
import math
import random

import numpy as np
from PIL import Image, ImageDraw


def hexc(s, a=255):
    s = s.lstrip("#")
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16), a)


def arr(img):
    return np.array(img.convert("RGBA"))


CLEAR = (0, 0, 0, 0)

# ---------------------------------------------------------------- palettes

FAR = [hexc(c) for c in ("#281f3c", "#30264a", "#3a2e56")]
FAR_WIN = [hexc(c) for c in ("#c98a48", "#f0c068")]
MID = [hexc(c) for c in ("#10151f", "#171e2c", "#1f283a", "#29344a")]
RIM = [hexc(c) for c in ("#5e3238", "#9a4c3c", "#d0744a")]
WIN = [hexc(c) for c in ("#f4c86a", "#c38a44", "#0b0e15")]
GIRDER = hexc("#3a3244")
HOLE = hexc("#07090e")
ROAD = [hexc(c) for c in ("#1c1c22", "#24242c", "#2e2e38", "#3a3a46")]
CURB = [hexc(c) for c in ("#474754", "#5c5c6a", "#727282")]
LINE = hexc("#b89e56")
RUBBLE = [hexc(c) for c in ("#3e3846", "#524a5c", "#686074")]
FG = [hexc(c) for c in ("#08080c", "#121218", "#1e1e28")]
FIRE = [hexc(c) for c in ("#fff0a8", "#ffb43c", "#ee641a", "#a02c0c")]
CARS = [hexc(c) for c in ("#6e2e2e", "#2c4a6e", "#6a6034")]
GLASS = hexc("#0a0d14")
LAMP = hexc("#fff4c8")

HANG = [hexc(c) for c in ("#0b0e14", "#121720", "#1a202c", "#232b3a", "#2e384a", "#3c485e", "#566580")]
HAZ = [hexc("#d6ae2e"), hexc("#14161a")]
LIGHT = [hexc("#e2ecff"), hexc("#8ea6cc")]
FLOOR = [hexc(c) for c in ("#262a32", "#2e333c", "#373d47", "#434a55")]
PAINT = hexc("#8a2e24")
SKYOUT = [hexc("#0c1628"), hexc("#16284a"), hexc("#c8d8ff")]
WORK = [hexc("#f0a020"), hexc("#e8e0c8"), hexc("#2a4a78"), hexc("#f4d8b0")]

SPARK = [hexc(c) for c in ("#ffffff", "#fff2a0", "#ffc03c", "#ff7a1e")]
BLOCK = [hexc(c) for c in ("#ffffff", "#bfeaff", "#58b4ff", "#2a64d0")]
SMOKE = [hexc(c) for c in ("#4c4c54", "#6a6a72", "#8c8c94")]
DUST = [hexc(c) for c in ("#6e6258", "#8c7e70", "#aa9c8c")]

# ---------------------------------------------------------------- helpers


def canvas(w, h):
    im = Image.new("RGBA", (w, h), CLEAR)
    return im, ImageDraw.Draw(im)


def jag(rng, x0, x1, y, amp, step=4):
    """a jagged line from x0 to x1 around y (broken tops)"""
    pts = []
    x = x0
    while x < x1:
        pts.append((x, y + rng.randint(-amp, amp)))
        x += rng.randint(2, step)
    pts.append((x1, y + rng.randint(-amp, amp)))
    return pts

# ---------------------------------------------------------------- arena

W_FAR, H_FAR = 780, 150
W_MID, H_MID = 920, 250
W_GROUND, H_GROUND = 1024, 74
W_FG, H_FG = 1300, 40


def far_layer():
    rng = random.Random(7)
    im, d = canvas(W_FAR, H_FAR)
    x = -10
    while x < W_FAR:
        w = rng.randint(26, 70)
        h = rng.randint(50, 140)
        top = H_FAR - h
        c = rng.choice(FAR)
        d.rectangle([x, top, x + w, H_FAR], fill=c)
        if rng.random() < 0.3:        # a spire or antenna
            d.rectangle([x + w // 2 - 1, top - rng.randint(8, 24), x + w // 2, top], fill=c)
        if rng.random() < 0.35:       # broken top
            d.polygon(jag(rng, x, x + w, top + 6, 5) + [(x + w, top - 1), (x, top - 1)], fill=CLEAR)
        for wy in range(top + 6, H_FAR - 4, 5):
            for wx in range(x + 3, x + w - 2, 4):
                if rng.random() < 0.09:
                    d.rectangle([wx, wy, wx, wy + 1], fill=rng.choice(FAR_WIN))
        x += w + rng.randint(-8, 6)
    return arr(im)


def building(d, rng, x, w, h, H):
    """a big ruined tower: lit windows, the sunset on its left edge,
    holes with girders, a broken top, things on the roof"""
    top = H - h
    d.rectangle([x, top, x + w, H], fill=MID[1])
    d.rectangle([x + w - max(6, w // 5), top, x + w, H], fill=MID[0])          # the shaded side
    d.rectangle([x, top, x + 3, H], fill=RIM[1])                              # sunset rim
    d.rectangle([x, top, x, H], fill=RIM[2])
    # floors: bands every 12 px
    for fy in range(top + 10, H, 12):
        d.line([(x + 4, fy), (x + w - max(6, w // 5), fy)], fill=MID[2])
    # windows
    for wy in range(top + 4, H - 3, 6):
        for wx in range(x + 6, x + w - max(6, w // 5) - 2, 5):
            r = rng.random()
            col = WIN[0] if r < 0.12 else (WIN[1] if r < 0.18 else (WIN[2] if r < 0.7 else MID[3]))
            d.rectangle([wx, wy, wx + 2, wy + 2], fill=col)
    # holes where something hit it
    for _ in range(rng.randint(0, 2)):
        hx = x + rng.randint(4, max(5, w - 30))
        hy = top + rng.randint(20, max(21, h - 60))
        hw, hh = rng.randint(12, 26), rng.randint(10, 22)
        pts = [(hx + hw * (0.5 + 0.5 * math.cos(a)) + rng.randint(-3, 3),
                hy + hh * (0.5 + 0.5 * math.sin(a)) + rng.randint(-3, 3)) for a in np.linspace(0, 2 * math.pi, 9)]
        d.polygon(pts, fill=HOLE)
        for gy in range(hy + 3, hy + hh, 6):
            d.line([(hx, gy), (hx + hw, gy + rng.randint(-2, 2))], fill=GIRDER)
        d.line([(hx + hw // 2, hy), (hx + hw // 2 + rng.randint(-4, 4), hy + hh)], fill=GIRDER)
    # the top: broken, with girders sticking out, or a roof with a tank
    if rng.random() < 0.55:
        pts = jag(rng, x, x + w + 1, top + rng.randint(8, 22), 9, 6)
        d.polygon(pts + [(x + w + 1, top - 2), (x - 1, top - 2)], fill=CLEAR)
        for _ in range(3):
            gx = x + rng.randint(3, w - 3)
            gy = top + rng.randint(6, 20)
            d.line([(gx, gy), (gx + rng.randint(-6, 6), gy - rng.randint(6, 16))], fill=GIRDER)
    else:
        d.rectangle([x + 4, top - 3, x + w - 4, top], fill=MID[2])
        tx = x + rng.randint(6, max(7, w - 20))
        d.rectangle([tx, top - 14, tx + 10, top - 6], fill=MID[3])
        d.line([(tx + 1, top - 6), (tx + 1, top - 3)], fill=MID[2])
        d.line([(tx + 9, top - 6), (tx + 9, top - 3)], fill=MID[2])
        d.line([(x + w - 8, top - 3), (x + w - 8, top - 26)], fill=MID[3])
        d.point((x + w - 8, top - 27), fill=hexc("#ff4040"))


def mid_layer():
    rng = random.Random(11)
    im, d = canvas(W_MID, H_MID)
    x = -20
    while x < W_MID:
        w = rng.randint(54, 110)
        h = rng.randint(120, 240)
        building(d, rng, x, w, h, H_MID)
        x += w + rng.randint(4, 30)
    return arr(im)


def car(d, x, y, col, wrecked=False):
    """a car 16 px long: the robots are that big"""
    d.rectangle([x, y - 4, x + 15, y - 1], fill=col)
    d.rectangle([x + 4, y - 7, x + 11, y - 4], fill=col)
    d.rectangle([x + 5, y - 6, x + 7, y - 5], fill=GLASS)
    d.rectangle([x + 9, y - 6, x + 10, y - 5], fill=GLASS)
    d.rectangle([x + 2, y - 1, x + 4, y], fill=ROAD[0])
    d.rectangle([x + 11, y - 1, x + 13, y], fill=ROAD[0])
    if wrecked:
        d.polygon([(x + 4, y - 7), (x + 11, y - 7), (x + 9, y - 5), (x + 6, y - 5)], fill=ROAD[1])


def ground_layer():
    """the street where the robots stand; its row 16 is the ground line"""
    rng = random.Random(3)
    im, d = canvas(W_GROUND, H_GROUND)
    # the far sidewalk and curb (rows 0..15), the road below
    d.rectangle([0, 0, W_GROUND, 12], fill=CURB[0])
    d.rectangle([0, 0, W_GROUND, 1], fill=CURB[2])
    d.rectangle([0, 11, W_GROUND, 13], fill=CURB[1])
    d.rectangle([0, 14, W_GROUND, H_GROUND], fill=ROAD[1])
    for y in range(14, H_GROUND, 1):
        k = (y - 14) / (H_GROUND - 14)
        if k > 0.55:
            d.line([(0, y), (W_GROUND, y)], fill=ROAD[2] if k < 0.8 else ROAD[3])
    # lane dashes, in perspective lower down
    for x in range(0, W_GROUND, 64):
        d.rectangle([x + 8, 40, x + 36, 42], fill=LINE)
    # cracks and potholes
    for _ in range(40):
        x = rng.randint(0, W_GROUND)
        y = rng.randint(18, H_GROUND - 4)
        pts = [(x, y)]
        for _k in range(rng.randint(2, 5)):
            x += rng.randint(3, 9)
            y += rng.randint(-2, 2)
            pts.append((x, y))
        d.line(pts, fill=ROAD[0])
    for _ in range(6):
        x = rng.randint(30, W_GROUND - 60)
        y = rng.randint(24, H_GROUND - 12)
        w = rng.randint(24, 50)
        d.ellipse([x, y, x + w, y + w // 4], fill=ROAD[0])
        d.arc([x, y, x + w, y + w // 4], 180, 360, fill=RUBBLE[1])
    # on the sidewalk: tiny cars, lamp posts, rubble
    for x in range(12, W_GROUND - 20, 70):
        if rng.random() < 0.6:
            car(d, x + rng.randint(0, 30), 12, rng.choice(CARS), rng.random() < 0.4)
        lx = x + rng.randint(40, 60)
        bend = rng.choice((0, 0, 3, -4))
        d.line([(lx, 11), (lx, -2), (lx + bend + 3, -4)], fill=CURB[2])
        d.point((lx + bend + 3, -3), fill=LAMP)
    for _ in range(30):
        x = rng.randint(0, W_GROUND)
        y = rng.randint(4, 12)
        s = rng.randint(2, 5)
        d.polygon([(x, y), (x + s, y - s), (x + 2 * s, y)], fill=rng.choice(RUBBLE))
    return arr(im)


def fg_layer():
    """rubble in front of everything, at the bottom of the screen"""
    rng = random.Random(5)
    im, d = canvas(W_FG, H_FG)
    x = 0
    while x < W_FG:
        if rng.random() < 0.55:
            w = rng.randint(30, 90)
            h = rng.randint(8, 30)
            pts = [(x, H_FG)] + jag(rng, x, x + w, H_FG - h, 5, 7) + [(x + w, H_FG)]
            d.polygon(pts, fill=FG[0])
            for _ in range(3):
                rx = x + rng.randint(4, w - 4)
                d.line([(rx, H_FG - h + 4), (rx + rng.randint(-6, 6), H_FG - h - rng.randint(4, 12))], fill=FG[2])
            d.line(jag(rng, x + 2, x + w - 2, H_FG - h + 2, 2, 5), fill=FG[1])
            x += w
        x += rng.randint(40, 160)
    return arr(im)


def fire(k):
    """a small flame, 3 frames"""
    rng = random.Random(20 + k)
    im, d = canvas(14, 22)
    for i, c in enumerate((FIRE[3], FIRE[2], FIRE[1], FIRE[0])):
        s = 1 - i * 0.22
        pts = [(7 - 6 * s, 21), (7 + 6 * s, 21)]
        tip = 21 - (18 + rng.randint(-3, 3)) * s
        pts += [(7 + 3 * s + rng.randint(-1, 1), 21 - 9 * s), (7 + rng.randint(-2, 2), tip),
                (7 - 3 * s + rng.randint(-1, 1), 21 - 10 * s)]
        d.polygon(pts, fill=c)
    return arr(im)

# ---------------------------------------------------------------- hangar


def hangar():
    """the hall, 640x360: the robot stands on the turntable in the middle"""
    rng = random.Random(9)
    im, d = canvas(640, 360)
    # back wall in bands
    for i, y in enumerate(range(0, 300, 60)):
        d.rectangle([0, y, 640, y + 60], fill=HANG[1 + (i % 2)])
    # the bay door, half open: night outside
    d.rectangle([210, 58, 430, 296], fill=SKYOUT[0])
    for y in range(58, 180, 8):
        d.line([(210, y), (430, y)], fill=SKYOUT[1])
    for _ in range(26):
        d.point((rng.randint(212, 428), rng.randint(60, 170)), fill=SKYOUT[2])
    d.rectangle([210, 176, 430, 296], fill=HANG[2])            # the door's lower half, closed
    for x in range(214, 430, 10):
        d.line([(x, 178), (x, 294)], fill=HANG[3])
    d.rectangle([204, 52, 436, 58], fill=HANG[4])
    d.rectangle([204, 52, 210, 300], fill=HANG[4])
    d.rectangle([430, 52, 436, 300], fill=HANG[4])
    # columns and braces
    for x in (0, 96, 176, 456, 536, 620):
        d.rectangle([x, 30, x + 18, 300], fill=HANG[3])
        d.rectangle([x, 30, x + 3, 300], fill=HANG[5])
        d.rectangle([x + 15, 30, x + 18, 300], fill=HANG[2])
        for y in range(40, 300, 16):
            d.point((x + 9, y), fill=HANG[6])
    for x0, x1 in ((18, 96), (114, 176), (474, 536), (554, 620)):
        d.line([(x0, 60), (x1, 140)], fill=HANG[3], width=2)
        d.line([(x1, 60), (x0, 140)], fill=HANG[3], width=2)
        d.line([(x0, 150), (x1, 230)], fill=HANG[3], width=2)
        d.line([(x1, 150), (x0, 230)], fill=HANG[3], width=2)
    # ceiling truss and lights
    d.rectangle([0, 0, 640, 30], fill=HANG[0])
    for x in range(0, 640, 20):
        d.line([(x, 4), (x + 10, 26), (x + 20, 4)], fill=HANG[3])
    d.line([(0, 4), (640, 4)], fill=HANG[4])
    d.line([(0, 26), (640, 26)], fill=HANG[4])
    for x in range(40, 640, 110):
        d.rectangle([x, 28, x + 30, 32], fill=LIGHT[0])
        d.line([(x + 2, 33), (x + 28, 33)], fill=LIGHT[1])
    # the crane rail
    d.rectangle([0, 40, 640, 46], fill=HANG[4])
    d.rectangle([0, 40, 640, 41], fill=HANG[6])
    # painted numbers and a warning band on the wall
    from mkassets import text_mask
    m = text_mask("VG-01", 4)
    ys, xs = np.nonzero(m)
    for y, x in zip(ys, xs):
        d.point((460 + x, 250 + y), fill=PAINT)
    for x in range(0, 640, 16):
        d.polygon([(x, 290), (x + 8, 290), (x + 4, 300), (x - 4, 300)], fill=HAZ[0])
    d.rectangle([0, 288, 640, 289], fill=HAZ[1])
    # catwalks with railings and ladders
    for x0, x1 in ((0, 170), (470, 640)):
        d.rectangle([x0, 200, x1, 206], fill=HANG[5])
        d.line([(x0, 200), (x1, 200)], fill=HANG[6])
        d.line([(x0, 188), (x1, 188)], fill=HANG[5])
        for x in range(x0 + 4, x1, 14):
            d.line([(x, 188), (x, 200)], fill=HANG[5])
        lx = x1 - 20 if x0 == 0 else x0 + 12
        d.line([(lx, 206), (lx, 300)], fill=HANG[5])
        d.line([(lx + 8, 206), (lx + 8, 300)], fill=HANG[5])
        for y in range(210, 300, 7):
            d.line([(lx, y), (lx + 8, y)], fill=HANG[5])
    # scaffolding towers beside the robot
    for x in (186, 436):
        for dx in (0, 16):
            d.line([(x + dx, 110), (x + dx, 300)], fill=HANG[5])
        for y in range(110, 300, 22):
            d.line([(x, y), (x + 16, y)], fill=HANG[5])
            d.line([(x, y), (x + 16, y + 22)], fill=HANG[4])
        d.rectangle([x - 6, 150, x + 22, 154], fill=HANG[6])
        d.rectangle([x - 6, 230, x + 22, 234], fill=HANG[6])
    # the floor, the turntable, containers and a tool cart
    for i, y in enumerate(range(300, 360, 6)):
        d.rectangle([0, y, 640, y + 6], fill=FLOOR[min(3, i // 3)])
    for x in range(-200, 840, 60):
        d.line([(320 + (x - 320) * 0.6, 300), (x, 360)], fill=FLOOR[0])
    d.ellipse([196, 306, 444, 336], fill=FLOOR[3])
    d.ellipse([196, 306, 444, 336], outline=HAZ[0])
    d.ellipse([210, 309, 430, 333], outline=HAZ[1])
    for x, w, h, c in ((24, 60, 34, CARS[1]), (70, 44, 24, CARS[0]), (560, 64, 38, CARS[2]), (520, 36, 20, CARS[1])):
        d.rectangle([x, 300 - h + 10, x + w, 310], fill=c)
        d.rectangle([x, 300 - h + 10, x + w, 300 - h + 12], fill=HANG[6])
        d.line([(x + 4, 300 - h + 16), (x + w - 4, 300 - h + 16)], fill=HANG[1])
    return arr(im)


def worker(k):
    """a tiny person: helmet, vest, legs; 0-1 walking, 2 welding"""
    im, d = canvas(6, 11)
    d.rectangle([1, 0, 4, 1], fill=WORK[0])
    d.rectangle([2, 2, 3, 3], fill=WORK[3])
    d.rectangle([1, 4, 4, 7], fill=WORK[0] if k != 2 else WORK[2])
    if k == 0:
        d.line([(1, 8), (1, 10)], fill=WORK[2])
        d.line([(4, 8), (4, 10)], fill=WORK[2])
    elif k == 1:
        d.line([(2, 8), (2, 10)], fill=WORK[2])
        d.line([(3, 8), (3, 10)], fill=WORK[2])
    else:
        d.line([(1, 8), (1, 10)], fill=WORK[2])
        d.line([(4, 8), (4, 10)], fill=WORK[2])
        d.line([(4, 5), (5, 6)], fill=WORK[1])
    return arr(im)


def crane():
    """the trolley on the rail with its cable and hook"""
    im, d = canvas(40, 90)
    d.rectangle([0, 0, 39, 10], fill=HANG[5])
    d.rectangle([0, 0, 39, 1], fill=HANG[6])
    d.rectangle([4, 10, 35, 14], fill=HAZ[0])
    for x in range(4, 36, 6):
        d.line([(x, 10), (x + 3, 14)], fill=HAZ[1])
    d.line([(18, 14), (18, 78)], fill=HANG[6])
    d.line([(21, 14), (21, 78)], fill=HANG[6])
    d.rectangle([13, 78, 26, 83], fill=HAZ[0])
    d.arc([14, 80, 26, 92], 0, 200, fill=HANG[6], width=2)
    return arr(im)

# ---------------------------------------------------------------- effects


def spark(k, pal, size=34):
    """a hit spark: a star of rays that grows and breaks up (4 frames)"""
    rng = random.Random(40 + k)
    im, d = canvas(size, size)
    c = size / 2
    n = 9
    for i in range(n):
        a = i / n * 2 * math.pi + rng.random() * 0.3
        r0 = (2 + k * 3) * size / 34
        r1 = (8 + k * 5 + rng.randint(0, 5)) * size / 34
        col = pal[min(3, k + (i % 2))]
        d.line([(c + math.cos(a) * r0, c + math.sin(a) * r0), (c + math.cos(a) * r1, c + math.sin(a) * r1)],
               fill=col, width=2 if k < 2 else 1)
    if k < 2:
        r = (5 - k * 2) * size / 34
        d.ellipse([c - r, c - r, c + r, c + r], fill=pal[0])
    return arr(im)


def muzzle(k):
    im, d = canvas(30, 18)
    L = 26 - k * 8
    d.polygon([(0, 9), (L, 2), (L - 4, 9), (L, 16)], fill=FIRE[1])
    d.polygon([(0, 9), (L * 0.7, 5), (L * 0.55, 9), (L * 0.7, 13)], fill=FIRE[0])
    return arr(im)


def flame(k):
    """a booster's flame pointing down"""
    im, d = canvas(14, 30)
    L = 28 - k * 7
    d.polygon([(1, 0), (13, 0), (7, L)], fill=FIRE[2])
    d.polygon([(3, 0), (11, 0), (7, L * 0.65)], fill=FIRE[1])
    d.polygon([(5, 0), (9, 0), (7, L * 0.35)], fill=FIRE[0])
    return arr(im)


def explosion(k):
    """a fireball, 5 frames"""
    rng = random.Random(60 + k)
    s = 64
    im, d = canvas(s, s)
    c = s / 2
    R = 10 + k * 5
    for i, col in enumerate((SMOKE[1] if k > 2 else FIRE[3], FIRE[2], FIRE[1], FIRE[0])):
        r = R * (1 - i * 0.24)
        pts = [(c + math.cos(a) * r * (0.8 + 0.3 * rng.random()), c + math.sin(a) * r * (0.8 + 0.3 * rng.random()))
               for a in np.linspace(0, 2 * math.pi, 12, endpoint=False)]
        if k < 4 or i == 0:
            d.polygon(pts, fill=col)
    return arr(im)


def dust(k):
    im, d = canvas(40, 16)
    r = 6 + k * 4
    for i, col in enumerate(DUST):
        d.ellipse([20 - r + i * 2, 15 - r * 0.6, 20 + r - i * 2, 16], fill=col if k < 3 else DUST[0])
    return arr(im)

# ---------------------------------------------------------------- the list


def everything():
    """(name, image, dx, dy) for the sheet"""
    out = [("far", far_layer(), 0, 0), ("mid", mid_layer(), 0, 0), ("ground", ground_layer(), 0, 0),
           ("fg", fg_layer(), 0, 0), ("hangar", hangar(), 0, 0), ("crane", crane(), -20, 0)]
    for k in range(3):
        out.append((f"fire{k}", fire(k), -7, -21))
        out.append((f"worker{k}", worker(k), -3, -11))
    for k in range(4):
        out.append((f"spark{k}", spark(k, SPARK), -17, -17))
        out.append((f"bspark{k}", spark(k, BLOCK), -17, -17))
        out.append((f"bigspark{k}", spark(k, SPARK, 60), -30, -30))
    for k in range(2):
        out.append((f"muzzle{k}", muzzle(k), 0, -9))
    for k in range(3):
        out.append((f"flame{k}", flame(k), -7, 0))
        out.append((f"dust{k + 1}", dust(k + 1), -20, -16))
    for k in range(5):
        out.append((f"boom{k}", explosion(k), -32, -32))
    return out


GOLD = (hexc("#fff4b0"), hexc("#e87818"))
STEEL = (hexc("#f4f8ff"), hexc("#56688a"))
RED = (hexc("#ffd8b8"), hexc("#d02818"))
DARK = hexc("#140a06")
TEXTS = [
    ("t_fight", "FIGHT!", 6, *GOLD, DARK, hexc("#ffffff"), hexc("#ffffff")),
    ("t_ko", "K.O.", 7, *RED, DARK, hexc("#fff4b0"), hexc("#ffffff")),
    ("t_round", "ROUND", 4, *STEEL, DARK, None, None),
    ("t_final", "FINAL ROUND", 4, *RED, DARK, None, None),
    ("t_wins", "WINS", 4, *GOLD, DARK, None, None),
    ("t_draw", "DRAW", 5, *STEEL, DARK, None, None),
    ("t_time", "TIME OVER", 4, *STEEL, DARK, None, None),
    ("t_p1", "P1", 4, hexc("#c0e8ff"), hexc("#2878e0"), DARK, None, None),
    ("t_p2", "P2", 4, hexc("#ffd0c0"), hexc("#e03a28"), DARK, None, None),
    ("t_cpu", "CPU", 4, *STEEL, DARK, None, None),
    ("t_titan", "TITAN", 8, *STEEL, DARK, hexc("#ffffff"), hexc("#e87818")),
    ("t_clash", "CLASH", 8, *GOLD, DARK, hexc("#ffffff"), hexc("#b02818")),
] + [(f"d{n}", str(n), 3, *STEEL, DARK, None, None) for n in range(10)] + \
    [(f"r{n}", str(n), 4, *STEEL, DARK, None, None) for n in (1, 2, 3)]


def cover(path):
    """the picture on the cartridge in the menu (128x80): the logo on the city"""
    far = Image.fromarray(far_layer())
    im = Image.new("RGBA", (128, 80), hexc("#3a2448"))
    d = ImageDraw.Draw(im)
    for i, c in enumerate(("#241a3a", "#3a2448", "#6a3048", "#a84a3c", "#e08048")):
        d.rectangle([0, i * 12, 128, i * 12 + 12], fill=hexc(c))
    im.alpha_composite(far.crop((0, 60, 128, 150)).resize((128, 45)), (0, 35))
    from mkassets import big_text
    for s, y, pal, o2 in (("TITAN", 8, STEEL, hexc("#e87818")), ("CLASH", 36, GOLD, hexc("#b02818"))):
        t = Image.fromarray(big_text(s, 3, *pal, DARK, None, o2))
        im.alpha_composite(t, ((128 - t.width) // 2, y))
    im.convert("RGB").save(path)


def preview(sheet, lua, outdir):
    """a mock fight: the layers in place, two robots, to judge the look"""
    import re
    spr = {}
    for line in lua:
        m = re.match(r"\s+(\w+) = \{(\d+), (\d+), (\d+), (\d+), (-?\d+), (-?\d+)\},", line)
        if m:
            spr[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    S = Image.fromarray(sheet)

    def get(name):
        sx, sy, w, h, dx, dy = spr[name]
        return S.crop((sx, sy, sx + w, sy + h)), dx, dy
    im = Image.new("RGBA", (640, 360))
    d = ImageDraw.Draw(im)
    for i, c in enumerate(("#1e1636", "#2c1c42", "#44244c", "#6a3048", "#94403e", "#c05c3e", "#e0844a")):
        d.rectangle([0, i * 34, 640, i * 34 + 34], fill=hexc(c))
    for name, y in (("far", 250 - H_FAR), ("mid", 306 - H_MID), ("ground", 300)):
        img, _, _ = get(name)
        im.alpha_composite(img.crop((100, 0, 740, img.height)), (0, y))
    frames = [ln for ln in lua if ln.strip().startswith("{ b = ")]

    def robot_at(i, x, flip, keys):
        line = frames[i]
        for key in ["b"] + keys:
            m = re.search(r"\b" + key + r" = \{(\{.*?\})\}", line)
            if not m:
                continue
            for r in re.findall(r"\{(\d+), (\d+), (\d+), (\d+), (-?\d+), (-?\d+)\}", m.group(1)):
                sx, sy, w, h, dx, dy = (int(v) for v in r)
                part = S.crop((sx, sy, sx + w, sy + h))
                if flip:
                    part = part.transpose(Image.FLIP_LEFT_RIGHT)
                    im.alpha_composite(part, (x - dx - w, 316 + dy))
                else:
                    im.alpha_composite(part, (x + dx, 316 + dy))
    robot_at(0, 230, False, ["s", "g"])
    robot_at(0, 420, True, ["sw", "hv", "sh"])
    img, _, _ = get("fg")
    im.alpha_composite(img.crop((0, 0, 640, H_FG)), (0, 360 - H_FG))
    t, dx, dy = get("t_fight")
    im.alpha_composite(t, (320 + dx, 140 + dy))
    im.save(outdir + "/fight.png")
    im.resize((1280, 720), Image.NEAREST).save(outdir + "/fight2.png")
    h, _, _ = get("hangar")
    h.save(outdir + "/hangar.png")
