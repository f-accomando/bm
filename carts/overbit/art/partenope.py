"""
partenope.py - Partenope, Overbit's first map (Control): a waterfront of a
Naples of the future at sunset, Vesuvius across the bay.

The same on both sides (x < 0 is team 1's half, x > 0 team 2's, mirrored):
a spawn room at each end; three ways to the middle: the main street with
the pizzeria and its neon sign, the north alley under arches with stairs up
to a terrace over the square, the quay along the sea with containers and
kiosks; the square in the middle with the point to hold (a round platform)
and the fountain of the siren; a church with a tiled dome at the north end
of the square. Tall coloured facades all round, with windows, green
shutters, balconies, awnings, washing hung across the alley; lamps, neon,
holo panels.

  partenope.py OUT_PREFIX     (writes OUT_PREFIX.bm with the models and
                               OUT_PREFIX.lua with the map's data)
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "scripts"))

from geo import cylinder, ellipsoid, hull, lathe  # noqa: E402
import mapbake  # noqa: E402
from mapgeo import Map, Mat, Tex  # noqa: E402
import maptex  # noqa: E402

# ---------------------------------------------------------------- materials

BASALT = Mat(0x5F5C59)
BASALT2 = Mat(0x56534F)
PIAZZA = Mat(0xBDB3A2)
PIAZZA2 = Mat(0xA99F8E)
QUAY = Mat(0x9C9488)
KERB = Mat(0xCFC6B4, thin=True)
WALLS = [Mat(0xD9A55B), Mat(0xD98E8A), Mat(0xB9583E), Mat(0xE6C46A), Mat(0xE7DCC4), Mat(0xA9BFCF), Mat(0xE8B48F)]
TRIM = Mat(0xEEE6D4, thin=True)
ROOF = Mat(0xB45A3C)
IRON = Mat(0x2F3236, thin=True)
RAIL = Mat(0x2F3236, detail=True, thin=True)   # small iron work
STEEL = Mat(0x8C949C)
BOLLARD = Mat(0x2F3236, detail=True)
AWN_W = Mat(0xF0EAE0)
WOOD = Mat(0x7A5A3E)
GREEN = Mat(0x4E7A3E)
PINE = Mat(0x3E5E36)
TRUNK = Mat(0x6A4E3A)
WATER = Mat(0x3E6E8E)
DOME_Y = Mat(0xE2B640)
DOME_G = Mat(0x3E8A6A)
HOLO = Mat(0x5FE8FF, glow=True, see=True)
LAMP = Mat(0xFFE2A8, glow=True, detail=True)
TEAM = {-1: Mat(0x46B4FF, glow=True), 1: Mat(0xFF4646, glow=True)}
CONT = [Mat(0xB8452E), Mat(0x2E6E8E), Mat(0xD9A030), Mat(0x5E7E4E)]
INVISIBLE = None
# the pictures of the atlas (maptex.py): windows, doors, shop fronts, signs
PIC = {k: Tex(k) for k in ("win", "win_lit", "win_shut", "win_bal", "win_bal_lit", "win_arch", "win_shut_bal",
                           "door", "awning", "washing")}
PIC.update({k: Tex(k, glow=True) for k in ("shop", "pizza", "gelato", "bar", "holo")})

# the sun of the sunset: low in the west-south-west, over the sea
SUN_DIR = (-0.78, 0.36, -0.5)
LIGHT = mapbake.Light(sun_dir=SUN_DIR, sun_rgb=(1.0, 0.68, 0.42), sun_k=1.55,
                      zenith=(0.40, 0.46, 0.78), horizon=(0.78, 0.62, 0.66), sky_k=0.85,
                      bounce=(0.62, 0.44, 0.34), bounce_k=0.7)

M = Map("pt", maptex.build())


def X(s, x):
    """x on side s (the map is mirrored across x = 0)"""
    return s * x


def xs(s, a, b):
    """an x range on side s, low to high"""
    a, b = s * a, s * b
    return (a, b) if a <= b else (b, a)


# ---------------------------------------------------------------- buildings

def facade(x0, z0, x1, z1, h, wall, face, floors=None, seed=0, shop=None, lit=0.25):
    """the windows of one face of a building box ("n" +z, "s" -z, "e" +x,
    "w" -x): pictures of the atlas (green shutters open or closed, some lit,
    some with a balcony), arched windows and a door on the ground floor, a
    cornice on top; shop: the ground floor left blank (shop fronts)"""
    floors = floors or max(1, int(h // 3.2))
    if face in "ns":
        n = (0, 0, 1 if face == "n" else -1)
        at = lambda a, y: (a, y, (z1 + 0.04) if face == "n" else (z0 - 0.04))   # noqa: E731
        a0, a1 = x0, x1
    else:
        n = (1 if face == "e" else -1, 0, 0)
        at = lambda a, y: ((x1 + 0.04) if face == "e" else (x0 - 0.04), y, a)   # noqa: E731
        a0, a1 = z0, z1
    w = a1 - a0
    cols = max(1, int(w // 3.0))
    step = w / cols
    rnd = seed
    for f in range(floors):
        if f == 0 and shop is not None:
            continue
        for c in range(cols):
            rnd = (rnd * 1103515245 + 12345) & 0x7FFFFFFF
            a = a0 + step * (c + 0.5)
            if f == 0:
                if c == cols // 2:
                    M.decal(at(a, 1.4), n, 1.7, 2.8, PIC["door"])
                else:
                    M.decal(at(a, 1.65), n, 1.9, 2.5, PIC["win_arch"])
                continue
            lit_w = (rnd % 100) < lit * 100
            bal = (rnd >> 8) % 3 == 0
            shut = not lit_w and (rnd >> 12) % 4 == 0
            name = "win_shut" if shut else "win_lit" if lit_w else "win"
            if bal:
                name = {"win_shut": "win_shut_bal", "win_lit": "win_bal_lit", "win": "win_bal"}[name]
            M.decal(at(a, 0.9 + f * 3.2 + 0.85), n, 1.9, 2.5, PIC[name])
    # the cornice
    if face in "ns":
        zc = z1 if face == "n" else z0
        M.box(x0 - 0.1, h - 0.4, min(zc, zc + n[2] * 0.35), x1 + 0.1, h, max(zc, zc + n[2] * 0.35), TRIM, solid=False)
    else:
        xc = x1 if face == "e" else x0
        M.box(min(xc, xc + n[0] * 0.35), h - 0.4, z0 - 0.1, max(xc, xc + n[0] * 0.35), h, z1 + 0.1, TRIM, solid=False)


def building(x0, z0, x1, z1, h, wall, faces="", seed=0, shop=None, roof=True):
    """a block: walls, a flat roof with a low parapet, facades on `faces`"""
    if x0 > x1: x0, x1 = x1, x0
    if z0 > z1: z0, z1 = z1, z0
    M.box(x0, 0, z0, x1, h, z1, wall, top=ROOF if roof else wall)
    for f in faces:
        facade(x0, z0, x1, z1, h, wall, f, seed=seed + ord(f), shop=shop)


def awning(x0, x1, z, y, depth, n):
    """a striped awning over a shop front (along x, sloping out to z + n * depth):
    one picture on top, the same under it"""
    zo = z + n * depth
    M.tex_quad((x1, y - 0.6, zo), (x1, y, z), (x0, y, z), (x0, y - 0.6, zo), PIC["awning"])
    M.tex_quad((x0, y - 0.6, zo), (x0, y, z), (x1, y, z), (x1, y - 0.6, zo), PIC["awning"])
    M.occluders.append(((x0, y - 0.6, min(z, zo)), (x1, y, max(z, zo))))


def lamp_post(x, z, h=4.2, arm=0.8, toward=(0, 0)):
    M.part(cylinder(0.08, h, segs=5, caps=False).move(x, 0, z), IRON, solid=False)
    M.box(x - 0.15, 0, z - 0.15, x + 0.15, 0.5, z + 0.15, IRON)
    ax, az = x + toward[0] * arm, z + toward[1] * arm
    M.box(min(x, ax) - 0.04, h - 0.06, min(z, az) - 0.04, max(x, ax) + 0.04, h + 0.02, max(z, az) + 0.04, RAIL,
          solid=False)
    M.box(ax - 0.16, h - 0.36, az - 0.16, ax + 0.16, h - 0.06, az + 0.16, LAMP, solid=False, occlude=False)
    M.lamp((ax, h - 0.4, az), 0xFFC880, radius=7.0, k=0.9)


def pine(x, z, h=7.5, r=2.6):
    """an umbrella pine of the waterfront"""
    M.part(cylinder(0.18, h, segs=5, r2=0.12).turn(rz=6).move(x, 0, z), TRUNK, solid=False)
    M.part(ellipsoid(r, 0.9, r * 0.92, segs=7, rings=3).move(x + 0.6, h, z), PINE, solid=False)
    M.box(x - 0.25, 0, z - 0.25, x + 0.25, 3.0, z + 0.25, TRUNK, solid=True, sides="-")


def container(x, z, along_x, colour, h=2.6):
    if along_x:
        M.box(x - 3.0, 0, z - 1.2, x + 3.0, h, z + 1.2, colour, top=colour.shade(0.9))
    else:
        M.box(x - 1.2, 0, z - 3.0, x + 1.2, h, z + 3.0, colour, top=colour.shade(0.9))


def crate(x, z, s=1.0, y=0.0):
    M.box(x - s / 2, y, z - s / 2, x + s / 2, y + s, z + s / 2, WOOD, top=WOOD.shade(1.1))


def washing(x0, x1, z, y):
    """a line across the alley with clothes (a picture, seen from both sides)"""
    if x0 > x1: x0, x1 = x1, x0
    M.tex_quad((x0, y - 0.95, z), (x0, y, z), (x1, y, z), (x1, y - 0.95, z), PIC["washing"])
    M.tex_quad((x1, y - 0.95, z), (x1, y, z), (x0, y, z), (x0, y - 0.95, z), PIC["washing"])


# ---------------------------------------------------------------- one half

def half(s):
    team = TEAM[s]
    # -- the spawn room at the end: walls, roof, a wide door and a side door
    x_back, x_front = 46, 38
    a, b = xs(s, x_back, x_back + 1)
    M.box(a, 0, -8, b, 6, 8, WALLS[4])
    a, b = xs(s, x_front, x_back)
    M.box(a, 0, -8, b, 6, -7, WALLS[4])
    M.box(a, 0, 7, b, 6, 8, WALLS[4])
    M.box(a, 5.4, -7, b, 6, 7, WALLS[4], top=ROOF)                  # the roof
    fa, fb = xs(s, x_front, x_front + 0.6)
    M.box(fa, 0, -7, fb, 6, -2.2, WALLS[4])
    M.box(fa, 0, 2.2, fb, 6, 7, WALLS[4])
    M.box(fa, 3.6, -2.2, fb, 6, 2.2, WALLS[4])
    # the team's light inside, a strip over the door outside
    for z in (-4, 0, 4):
        M.lamp((X(s, 42), 5.0, z), team.rgb, radius=6, k=0.9)
        lo, hi = xs(s, 41.6, 42.4)
        M.box(lo, 5.3, z - 0.4, hi, 5.4, z + 0.4, team, solid=False, occlude=False)
    lo, hi = xs(s, x_front - 0.05, x_front + 0.65)
    M.box(lo, 3.6, -2.2, hi, 3.75, 2.2, team, solid=False, occlude=False)
    M.mark(f"spawn{1 if s < 0 else 2}", {"x": X(s, 42), "z": 0, "yaw": math.pi / 2 if s < 0 else -math.pi / 2})
    # -- the bots' places (mapbake.navgraph links them): the spawn, the main
    #    street, the passage to the alley, the alley, the side street to the
    #    quay, the quay, the ways into the square, the terrace and its stairs
    t = "w" if s < 0 else "e"
    for name, x, z, y in (("spawn", 42, 0, 0), ("spawn_n", 43, 5, 0), ("spawn_s", 43, -5, 0), ("door", 36.5, 0, 0),
                          ("st1", 31, -1.5, 0), ("st2", 24, -2, 0), ("st3", 17, -1, 0), ("st4", 11.5, -1, 0),
                          ("pass", 24, 7, 0), ("pass_s", 22.9, 3.6, 0), ("st2b", 21.4, 0.3, 0), ("al1", 35, 13, 0), ("al2", 28, 16.5, 0), ("al3", 24, 15, 0),
                          ("al4", 17, 15, 0), ("al5", 11, 15, 0), ("ss", 23, -10, 0), ("q1", 28.5, -16.5, 0),
                          ("q2", 23, -16.2, 0), ("q3", 15, -16.2, 0), ("q4", 29, -22.5, 0), ("q5", 20, -22.2, 0),
                          ("q6", 11, -19, 0), ("q7", 36, -21.5, 0), ("stairs0", 4.8, 10.5, 0), ("stairs1", 9.7, 10.5, 3.7),
                          ("terrace", 11, 6.5, 4.0), ("under", 11, 6, 0)):
        M.nav_node(f"{name}_{t}", X(s, x), z, y)

    # -- the north boundary: tall facades from the spawn to the church
    building(X(s, 38), 18, X(s, 13), 26, 13, WALLS[0 if s < 0 else 1], faces="s", seed=11 * s)
    building(X(s, 46), 8, X(s, 38), 26, 9, WALLS[4], faces="")
    # -- the north block between the alley (z 12..18) and the main street
    #    (z -5..3): two buildings with an arched passage between them
    building(X(s, 38), 3, X(s, 26), 12, 10, WALLS[3 if s < 0 else 5], faces="ns", seed=21 * s)
    building(X(s, 22), 3, X(s, 13), 12, 11, WALLS[6 if s < 0 else 2], faces="ns" + ("e" if s < 0 else "w"), seed=31 * s)
    # the passage roof (an arch's mass over it) and the terrace over the square
    a, b = xs(s, 22, 26)
    M.box(a, 4.0, 3, b, 10, 12, WALLS[3 if s < 0 else 5], top=ROOF)
    # the terrace: a slab at 3.7 m on the square's side of the block, stairs
    # up to it from the square, a railing
    a, b = xs(s, 9, 13)
    M.box(a, 3.7, 4, b, 4.0, 12, PIAZZA2, top=PIAZZA)
    for z in (4.2, 8.0):
        M.part(cylinder(0.25, 3.7, segs=6).move(X(s, 9.4), 0, z), TRIM, solid=True)
    a, b = xs(s, 9, 9.15)
    M.box(a, 4.0, 4, b, 5.0, 8.8, IRON, solid=True)                    # railing (the stairs arrive past it)
    a, b = xs(s, 5.5, 9)
    M.stairs(a, 9, b, 12, 0, 3.7, "w" if s < 0 else "e", PIAZZA2, n=12)
    # -- the south block: the pizzeria on the main street, a side street to the quay
    building(X(s, 38), -15, X(s, 25), -5, 9, WALLS[1 if s < 0 else 0], faces="n", seed=41 * s, shop=True)
    building(X(s, 21), -15, X(s, 13), -5, 10, WALLS[2 if s < 0 else 6], faces="n" + ("e" if s < 0 else "w"),
             seed=51 * s)
    # the pizzeria: its front on the main street, awning, the neon sign
    awning(min(X(s, 36), X(s, 27)), max(X(s, 36), X(s, 27)), -5, 3.1, 1.3, 1)
    a, b = xs(s, 34, 29)
    M.box(a, 3.6, -5.1, b, 4.4, -4.9, IRON, solid=False)
    M.decal((X(s, 31.5), 4.0, -4.88), (0, 0, 1), 4.6, 0.97, PIC["pizza"])
    M.lamp((X(s, 31.5), 3.6, -3.8), 0xFF5A40, radius=6.5, k=1.1)
    for xd in (34.6, 28.6):                                      # the shop windows glowing warm
        M.decal((X(s, xd), 1.45, -4.96), (0, 0, 1), 2.8, 2.1, PIC["shop"])
    M.decal((X(s, 31.6), 1.4, -4.96), (0, 0, 1), 1.7, 2.8, PIC["door"])
    # tables outside
    for xd in (34.5, 31):
        M.part(cylinder(0.5, 0.06, segs=8).move(X(s, xd), 0.75, -3.6), AWN_W, solid=False)
        M.part(cylinder(0.05, 0.75, segs=4).move(X(s, xd), 0, -3.6), IRON, solid=False)
        a, b = xs(s, xd - 0.6, xd + 0.6)
        M.box(a, 0, -4.2, b, 0.8, -3.0, IRON, solid=True, sides="-")
    # -- the main street: dark basalt paving, kerbs, lamps, a scooter
    for zz in (-4.8, 2.8):
        a, b = xs(s, 38, 13)
        M.box(a, 0, zz - 0.2, b, 0.15, zz + 0.2, KERB, solid=False)
    lamp_post(X(s, 30), 2.2, toward=(0, -1))
    lamp_post(X(s, 18), -4.4, toward=(0, 1))
    crate(X(s, 24), 1.6, 1.1)
    crate(X(s, 24.9), 1.8, 0.8)
    crate(X(s, 24.4), 1.6, 0.7, y=1.1)
    # a holo board on the side of the block, over the street
    a, b = xs(s, 15, 19.5)
    M.decal((X(s, 17.25), 5.8, 2.96), (0, 0, -1), 4.5, 2.4, PIC["holo"])
    M.lamp((X(s, 17.2), 5.8, 2.4), 0x5FE8FF, radius=5, k=0.7)
    # -- the alley: arches, washing lines, stairs
    for xd in (36, 30, 24, 18):
        washing(X(s, xd - 2), X(s, xd + 1.5), 13.5 + (xd % 3), 6.5 + (xd % 2))
    lamp_post(X(s, 32), 17.4, toward=(0, -1))
    # a low wall and planters in the alley for cover
    a, b = xs(s, 33, 35.5)
    M.box(a, 0, 14.4, b, 1.1, 15.2, WALLS[4], top=TRIM)
    M.box(a + 0.2, 1.1, 14.5, b - 0.2, 1.5, 15.1, GREEN, solid=False)
    # -- the quay: containers, kiosks, pines, bollards, the railing on the sea
    container(X(s, 33), -19.5, True, CONT[0 if s < 0 else 1])
    container(X(s, 26), -21.5, False, CONT[3 if s < 0 else 2])
    container(X(s, 20), -18.5, True, CONT[1 if s < 0 else 0])
    a, b = xs(s, 20, 23)
    M.box(a - 1.0, 2.6, -19.7, b + 1.0, 5.2, -17.3, CONT[2], top=CONT[2].shade(0.9))   # stacked on top
    pine(X(s, 36.5), -23)
    pine(X(s, 15.5), -22.5, h=8.2)
    for xd in range(14, 38, 6):
        M.part(cylinder(0.18, 0.6, segs=4).move(X(s, xd), 0, -23.6), BOLLARD, solid=False)
    a, b = xs(s, 46, 12)
    M.box(a, 0, -24.2, b, 1.0, -24.0, IRON, solid=True)                # railing
    M.box(a, 0, -24.4, b, 6.0, -24.2, KERB, solid=True, sides="-")     # the invisible wall over the sea
    lamp_post(X(s, 28), -23.5, toward=(0, 1))
    lamp_post(X(s, 16), -15.6, toward=(0, -1))
    # a kiosk with a glowing menu
    a, b = xs(s, 37, 41)
    M.box(a, 0, -17, b, 2.8, -14.5, WALLS[5], top=ROOF)
    M.decal(((a + b) / 2, 1.8, -17.04), (0, 0, -1), 2.8, 1.0, PIC["gelato"])
    # the quay's end and the spawn's south side closed
    a, b = xs(s, 38, 46)
    M.box(a, 0, -15, b, 7, -8, WALLS[0], top=ROOF)
    a, b = xs(s, 46, 47)
    M.box(a, 0, -24.4, b, 7, -15, WALLS[0], top=ROOF)
    # a bell tower between the church and the facades
    a, b = xs(s, 9, 13)
    M.box(a, 0, 18, b, 19, 26, WALLS[3], top=ROOF)
    facade(a, 18, b, 26, 19, WALLS[3], "s", seed=61 * s, lit=0.1)
    # -- ground: the street, the alley, the quay
    a, b = xs(s, 38, 12)
    M.ground(a, -5, b, 3, 2.5, lambda i, j, x, z: BASALT if (i + j) % 2 else BASALT2)
    M.ground(a, 12, b, 18, 2.5, lambda i, j, x, z: BASALT if (i * 3 + j) % 4 else BASALT2)
    M.ground(a, -24, b, -15, 3.0, lambda i, j, x, z: QUAY if (i + j) % 3 else QUAY.shade(0.93))
    # the side streets: through the north block, through the south block, by the spawn
    a, b = xs(s, 22, 26)
    M.ground(a, 3, b, 12, 2.0, lambda i, j, x, z: BASALT2)
    a, b = xs(s, 21, 25)
    M.ground(a, -15, b, -5, 2.5, lambda i, j, x, z: BASALT)
    a, b = xs(s, 38, 46)
    M.ground(a, -7, b, 7, 2.5, lambda i, j, x, z: PIAZZA2 if (i + j) % 2 else PIAZZA)
    a, b = xs(s, 38, 46)
    M.ground(a, 8, b, 18, 2.5, lambda i, j, x, z: BASALT2)


# ---------------------------------------------------------------- the square

def square():
    # the paving: light stone in rings round the point
    def tile(i, j, x, z):
        r = math.hypot(x, z + 2)
        if r < 6.4:
            return None                         # under the platform
        ring = int(r / 2.2)
        return PIAZZA if ring % 2 else PIAZZA2
    M.ground(-13, -15, 13, 18, 2.6, tile)
    M.ground(-12, -24, 12, -15, 3.0, lambda i, j, x, z: QUAY if (i + j) % 3 else QUAY.shade(0.93))
    # the point: a round platform, a ring of light round it (the game colours it)
    M.part(lathe([(6.2, 0.0), (6.2, 0.18), (6.0, 0.22)], segs=16).move(0, 0, -2), PIAZZA, solid=False)
    M.solids.append(((-4.4, 0, -6.4), (4.4, 0.22, 2.4)))
    M.solids.append(((-6.0, 0, -4.4), (6.0, 0.22, 0.4)))
    M.mark("point", {"x": 0, "y": 0.22, "z": -2, "r": 6.0})
    # the bots' places in the square: round the point, the corners, the church steps
    for name, x, z in (("p_w", -4.6, -2), ("p_e", 4.6, -2), ("p_n", 0, 2.6), ("p_s", 0, -6.6), ("p_nw", -3.4, 1.4),
                       ("p_ne", 3.4, 1.4), ("p_sw", -3.4, -5.4), ("p_se", 3.4, -5.4), ("sq_nw", -7.5, 13.6),
                       ("sq_ne", 7.5, 13.6), ("sq_w", -8, 3), ("sq_e", 8, 3), ("sq_sw", -7, -13.5), ("sq_se", 7, -13.5),
                       ("church", 0, 14), ("sq_s", 0, -15.5), ("quay", 0, -22.5), ("quay_w", -6, -21), ("quay_e", 6, -21)):
        M.nav_node(name, x, z, 0.22 if name.startswith("p_") else 0.0)
    # the fountain of the siren: a basin, a column, the figure on top
    M.part(lathe([(2.4, 0.22), (2.4, 0.9), (2.0, 0.95), (2.0, 0.55)], segs=12, close_top=False).move(0, 0, -2), TRIM, solid=False)
    M.part(cylinder(1.95, 0.05, segs=12).move(0, 0.58, -2), WATER, solid=False)
    M.part(lathe([(0.55, 0.6), (0.45, 1.8), (0.7, 2.0), (0.3, 2.3)], segs=8).move(0, 0, -2), TRIM, solid=False)
    M.part(lathe([(0.3, 2.3), (0.35, 2.9), (0.22, 3.5), (0.28, 3.8), (0.12, 4.3)], segs=8).move(0, 0, -2), STEEL, solid=False)
    M.part(ellipsoid(0.18, 0.22, 0.18, segs=6, rings=4).move(0, 4.45, -2), STEEL, solid=False)
    M.part(hull([(-0.1, 3.6, -2), (0.1, 3.6, -2), (0, 3.7, -1.9), (0.9, 4.6, -2.1), (1.0, 4.4, -2.0)]), STEEL, solid=False)
    M.part(hull([(-0.1, 3.6, -2), (0.1, 3.6, -2), (0, 3.7, -1.9), (-0.9, 4.6, -2.1), (-1.0, 4.4, -2.0)]), STEEL, solid=False)
    M.solids.append(((-2.4, 0, -4.4), (2.4, 0.95, 0.4)))            # the basin: one can stand on its edge
    M.solids.append(((-0.6, 0, -2.6), (0.6, 4.5, -1.4)))            # the column
    M.lamp((0, 1.2, -2), 0x5FE8FF, radius=4, k=0.6)
    # the church at the north end: steps, a facade with columns, the dome
    M.box(-9, 0, 18, 9, 16, 26, WALLS[4], top=ROOF)
    M.stairs(-7, 16, 7, 18, 0, 0.75, "n", PIAZZA, n=3)
    for x in (-5.5, -2, 2, 5.5):
        M.part(cylinder(0.45, 7.5, segs=8).move(x, 0.75, 17.3), TRIM, solid=True)
    M.box(-7, 8.25, 16.6, 7, 9.4, 18, TRIM, solid=False)
    M.part(hull([(-7, 9.4, 16.6), (7, 9.4, 16.6), (-7, 9.4, 18), (7, 9.4, 18), (0, 12.2, 17.3)]), TRIM, solid=False)
    M.decal((0, 2.85, 17.96), (0, 0, -1), 2.5, 4.2, PIC["door"])
    M.part(cylinder(4.2, 3.0, segs=12).move(0, 16, 22), WALLS[4], solid=False)
    dome = lathe([(4.2, 19.0), (4.0, 20.6), (3.2, 22.4), (1.8, 23.8), (0.5, 24.4)], segs=12)
    M.part(dome.move(0, 0, 22), DOME_Y, solid=False)
    M.part(lathe([(0.5, 24.4), (0.4, 25.6), (0.05, 26.6)], segs=6).move(0, 0, 22), DOME_G, solid=False)
    # the side facades of the square (the ends of the blocks face it, see half())
    # kiosks and benches for cover, the quay's railing and lamps
    for s in (-1, 1):
        M.box(s * 8.5 - 1.3, 0, -11, s * 8.5 + 1.3, 2.6, -8.6, WALLS[5], top=ROOF)
        M.decal((s * 8.5, 1.8, -11.04), (0, 0, -1), 2.2, 0.79, PIC["bar"])
        M.box(s * 7 - 1.4, 0, 7.5, s * 7 + 1.4, 0.5, 8.1, WOOD, top=WOOD.shade(1.1))   # benches
        lamp_post(s * 10.8, 6, toward=(-s, 0))
        lamp_post(s * 6.0, -15.5, toward=(0, 1))
        pine(s * 10, -21.5, h=7.8)
    M.box(-12, 0, -24.2, 12, 1.0, -24.0, IRON, solid=True)
    M.box(-12, 0, -24.4, 12, 6.0, -24.2, KERB, solid=True, sides="-")
    # the holo tower over the quay: a futuristic touch, the time and the score
    M.part(cylinder(0.35, 7, segs=6).move(0, 0, -20), STEEL, solid=True)
    M.part(cylinder(1.4, 1.6, segs=10).move(0, 7, -20), HOLO, solid=False)
    M.lamp((0, 7.8, -20), 0x5FE8FF, radius=8, k=0.7)


# ---------------------------------------------------------------- far away

def far():
    # the bay: a sea from the quay to the horizon, darker far out, the sun's
    # path on the water to the west
    for i in range(-6, 6):
        for j in range(1, 9):
            x0, x1 = i * 240, (i + 1) * 240
            z1, z0 = -24 - (j - 1) ** 1.6 * 40, -24 - j ** 1.6 * 40
            k = 1.0 - j * 0.045
            mat = WATER.shade(k)
            M.far_part(_flat(x0, z0, x1, z1, -0.8), mat)
    for j in range(1, 14):                    # the glitter of the sun on the water
        z = -40 - j * j * 6
        x = -60 - j * j * 9.0
        w = 6 + j * 3
        M.far_part(_flat(x - w, z - 3 - j, x + w, z + 3, -0.75), Mat(0xFFC070, glow=True) if j % 3 else Mat(0xFFE0A0, glow=True))
    # Vesuvius across the bay to the south-east: two summits, the old rim
    ves = lathe([(260, -1), (200, 30), (120, 70), (60, 98), (38, 104), (30, 98)], segs=16, close_top=True)
    M.far_part(ves.move(620, 0, -950), Mat(0x5A4E66))
    som = lathe([(180, -1), (120, 40), (70, 66), (50, 70)], segs=12)
    M.far_part(som.move(520, 0, -880), Mat(0x4E4560))
    # the far shore and the city up the hill behind (north): dark shapes
    for i in range(-10, 11):
        x = i * 60
        h = 20 + 14 * math.sin(i * 1.3) + 10 * math.sin(i * 0.7)
        M.far_part(_box(x, 120, x + 55, 170, h), Mat(0x6E5E78))
        M.far_part(_box(x + 10, 175, x + 50, 260, h * 2.2 + 30), Mat(0x5E5070))
    M.far_part(_box(-700, -60, -60, -30, 0.5), Mat(0x8A7A70))


def _flat(x0, z0, x1, z1, y):
    from geo import Part
    return Part([(x0, y, z0), (x0, y, z1), (x1, y, z1), (x1, y, z0)], [(0, 1, 2), (0, 2, 3)], False)


def _box(x0, z0, x1, z1, h):
    from geo import box
    return box(x1 - x0, h, z1 - z0).move((x0 + x1) / 2, h / 2, (z0 + z1) / 2)


def build():
    M.mirrored(half)
    square()
    far()
    return M


def main():
    import bmmesh
    import mkbm
    mp = build()
    models, lua = mapbake.build(mp, LIGHT, chunk=8.0, max_edge=12.0)
    prefix = sys.argv[1] if len(sys.argv) > 1 else "/tmp/partenope"
    mesh = bmmesh.encode([m for m, _ in models], inset=0)
    with open(prefix + ".lua", "w") as f:
        f.write(lua)
    data = mkbm.pack(b"-- map only\n", title="Partenope", author="bm", res=(320, 180), mesh=mesh)
    with open(prefix + ".bm", "wb") as f:
        f.write(data)
    print(f"{prefix}.bm: {len(data)} bytes, {len(models)} models")


if __name__ == "__main__":
    main()
