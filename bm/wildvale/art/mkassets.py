#!/usr/bin/env python3
"""Generate pixel-art sprite sheet (sheet.png) and cover (cover.png) for Wildvale."""
import os
import math
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_SHEET = os.path.join(HERE, "sheet.png")
OUT_COVER = os.path.join(os.path.dirname(HERE), "cover.png")

# Sheet: 512x256 (64x32 cells of 8x8, or 32x16 tiles of 16x16)
W, H = 512, 256
img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
px = img.load()

# Warm rustic palette inspired by Stardew / medieval pixel art
C = {
    "trans": (0, 0, 0, 0),
    "outline": (42, 33, 29, 255),
    # Grass & Nature (Spring/Summer/Autumn/Winter)
    "grass1": (124, 168, 72, 255),
    "grass2": (110, 154, 62, 255),
    "grass_hi": (146, 188, 88, 255),
    "forest1": (78, 126, 62, 255),
    "forest2": (64, 108, 52, 255),
    "autumn1": (182, 146, 68, 255),
    "autumn2": (162, 124, 54, 255),
    "winter1": (212, 228, 238, 255),
    "winter2": (188, 208, 224, 255),
    # Dirt / Sand / Stone / Water
    "dirt1": (188, 156, 112, 255),
    "dirt2": (168, 136, 94, 255),
    "dirt_dk": (128, 98, 66, 255),
    "sand1": (218, 198, 142, 255),
    "sand2": (198, 176, 122, 255),
    "rock_g1": (132, 134, 138, 255),
    "rock_g2": (112, 114, 118, 255),
    "water1": (68, 132, 178, 255),
    "water2": (88, 154, 198, 255),
    "water_hi": (148, 204, 232, 255),
    # Wood & Timber
    "wood_lt": (186, 136, 86, 255),
    "wood_md": (148, 102, 62, 255),
    "wood_dk": (104, 68, 42, 255),
    "bark1": (118, 82, 54, 255),
    "bark2": (86, 58, 38, 255),
    # Stone & Roof
    "stone_lt": (176, 176, 172, 255),
    "stone_md": (140, 142, 140, 255),
    "stone_dk": (98, 102, 104, 255),
    "roof_lt": (146, 136, 132, 255),
    "roof_md": (116, 106, 104, 255),
    "roof_dk": (84, 76, 76, 255),
    # Foliage
    "leaf_lt": (148, 186, 76, 255),
    "leaf_md": (106, 152, 58, 255),
    "leaf_dk": (72, 112, 44, 255),
    "aut_lt": (222, 168, 68, 255),
    "aut_md": (192, 132, 48, 255),
    "aut_dk": (144, 88, 36, 255),
    "birch1": (228, 224, 212, 255),
    "birch2": (184, 178, 166, 255),
    # Accents
    "berry": (212, 54, 64, 255),
    "flower_w": (244, 240, 224, 255),
    "flower_y": (242, 204, 72, 255),
    "gold": (236, 192, 74, 255),
    "fire1": (252, 224, 92, 255),
    "fire2": (242, 132, 44, 255),
    "fire3": (204, 64, 38, 255),
    "skin": (236, 188, 152, 255),
    "skin_dk": (196, 144, 112, 255),
    "hair": (96, 62, 42, 255),
    "tunic": (74, 128, 164, 255),
    "tunic_dk": (52, 92, 124, 255),
    "tunic_r": (176, 72, 68, 255),
    "tunic_g": (76, 152, 84, 255),
    "tunic_y": (196, 156, 58, 255),
    "pants": (84, 68, 56, 255),
    "steel": (196, 208, 218, 255),
    "steel_dk": (132, 144, 158, 255),
    "wolf1": (134, 138, 146, 255),
    "wolf2": (88, 92, 100, 255),
    "deer1": (176, 122, 74, 255),
    "deer2": (136, 88, 52, 255),
    "boar1": (114, 78, 58, 255),
    "boar2": (78, 52, 40, 255),
    "white": (248, 248, 248, 255),
    "win_lit": (246, 212, 122, 255),
}

def put(x, y, col):
    if 0 <= x < W and 0 <= y < H:
        px[x, y] = col

def fill_rect(x0, y0, w, h, col):
    for yy in range(y0, y0 + h):
        for xx in range(x0, x0 + w):
            put(xx, yy, col)

def tile_xy(tx, ty):
    return tx * 16, ty * 16

def draw_grass(tx, ty, c1, c2, chi, detail=0):
    ox, oy = tile_xy(tx, ty)
    fill_rect(ox, oy, 16, 16, c1)
    # subtle blades
    pts = [(2, 3), (10, 2), (6, 9), (13, 12), (3, 13)]
    for bx, by in pts:
        put(ox + bx, oy + by, c2)
        put(ox + bx + 1, oy + by - 1, chi)
    if detail == 1:  # wildflowers
        for fx, fy, fc in [(4, 5, C["flower_w"]), (11, 10, C["flower_y"]), (12, 4, C["flower_w"])]:
            put(ox + fx, oy + fy, fc)
            put(ox + fx + 1, oy + fy, fc)
            put(ox + fx, oy + fy + 1, c2)
    elif detail == 2:  # pebbles
        for px0, py0 in [(5, 6), (11, 11)]:
            put(ox + px0, oy + py0, C["stone_md"])
            put(ox + px0 + 1, oy + py0, C["stone_dk"])

def draw_water(tx, ty, frame=0):
    ox, oy = tile_xy(tx, ty)
    fill_rect(ox, oy, 16, 16, C["water1"])
    shift = frame * 4
    for rx, ry in [(2, 4), (9, 10), (4, 13)]:
        x = (rx + shift) % 13
        put(ox + x, oy + ry, C["water2"])
        put(ox + x + 1, oy + ry, C["water_hi"])
        put(ox + x + 2, oy + ry, C["water2"])

def draw_dirt(tx, ty, dark=False):
    ox, oy = tile_xy(tx, ty)
    c1 = C["dirt_dk"] if dark else C["dirt1"]
    c2 = C["bark2"] if dark else C["dirt2"]
    fill_rect(ox, oy, 16, 16, c1)
    for dx, dy in [(3, 3), (11, 5), (6, 11), (13, 13)]:
        put(ox + dx, oy + dy, c2)
        put(ox + dx + 1, oy + dy, c2)

def draw_tilled(tx, ty):
    ox, oy = tile_xy(tx, ty)
    fill_rect(ox, oy, 16, 16, C["dirt_dk"])
    for r in range(2, 15, 4):
        fill_rect(ox + 2, oy + r, 12, 2, C["bark2"])

# Row 0: Ground tiles (16x16)
draw_grass(0, 0, C["grass1"], C["grass2"], C["grass_hi"], 0)   # 0: meadow plain
draw_grass(1, 0, C["grass1"], C["grass2"], C["grass_hi"], 1)   # 1: meadow flowers
draw_grass(2, 0, C["forest1"], C["forest2"], C["grass2"], 0)   # 2: forest floor
draw_grass(3, 0, C["rock_g1"], C["rock_g2"], C["stone_lt"], 2) # 3: highland rock
draw_grass(4, 0, C["sand1"], C["sand2"], C["flower_w"], 0)     # 4: sand shore
draw_water(5, 0, 0)                                            # 5: water 0
draw_water(6, 0, 1)                                            # 6: water 1
draw_dirt(7, 0, False)                                         # 7: dirt path
draw_tilled(8, 0)                                              # 8: tilled soil
draw_grass(9, 0, C["autumn1"], C["autumn2"], C["aut_lt"], 0)   # 9: autumn grass
draw_grass(10, 0, C["winter1"], C["winter2"], C["white"], 0)   # 10: winter snow

# Row 1: Modular Building Tiles (16x16) - proportioned 3/4 view like reference image
# 0: Stone foundation wall
def draw_stone_wall(tx, ty, door=False, win=False):
    ox, oy = tile_xy(tx, ty)
    fill_rect(ox, oy, 16, 16, C["stone_md"])
    fill_rect(ox, oy, 16, 1, C["outline"])
    fill_rect(ox, oy + 15, 16, 1, C["outline"])
    # brick pattern
    for r in range(1, 15, 4):
        fill_rect(ox, oy + r, 16, 1, C["stone_dk"])
        off = 0 if (r // 4) % 2 == 0 else 4
        for c in range(off, 16, 8):
            fill_rect(ox + c, oy + r, 1, 4, C["stone_dk"])
            put(ox + (c + 1) % 16, oy + r + 1, C["stone_lt"])
    if door:
        fill_rect(ox + 3, oy + 2, 10, 14, C["outline"])
        fill_rect(ox + 4, oy + 3, 8, 13, C["wood_md"])
        fill_rect(ox + 5, oy + 4, 6, 12, C["wood_lt"])
        fill_rect(ox + 7, oy + 3, 1, 13, C["wood_dk"])
        put(ox + 10, oy + 9, C["gold"])
    elif win:
        fill_rect(ox + 4, oy + 4, 8, 7, C["outline"])
        fill_rect(ox + 5, oy + 5, 6, 5, C["win_lit"])
        fill_rect(ox + 7, oy + 5, 1, 5, C["wood_dk"])
        fill_rect(ox + 5, oy + 7, 6, 1, C["wood_dk"])

# 2: Timber-framed upper wall
def draw_timber_wall(tx, ty, win=False):
    ox, oy = tile_xy(tx, ty)
    fill_rect(ox, oy, 16, 16, C["wood_lt"])
    fill_rect(ox, oy, 16, 2, C["wood_dk"])
    fill_rect(ox, oy + 14, 16, 2, C["wood_dk"])
    fill_rect(ox, oy, 2, 16, C["wood_dk"])
    fill_rect(ox + 14, oy, 2, 16, C["wood_dk"])
    # vertical planks
    for x in (5, 10):
        fill_rect(ox + x, oy + 2, 1, 12, C["wood_md"])
    if win:
        fill_rect(ox + 4, oy + 4, 8, 8, C["outline"])
        fill_rect(ox + 5, oy + 5, 6, 6, C["win_lit"])
        fill_rect(ox + 7, oy + 5, 1, 6, C["wood_dk"])
        fill_rect(ox + 5, oy + 7, 6, 1, C["wood_dk"])

# 4: Pitched slate roof tile
def draw_roof(tx, ty, ridge=False, edge=False):
    ox, oy = tile_xy(tx, ty)
    fill_rect(ox, oy, 16, 16, C["roof_md"])
    for r in range(0, 16, 4):
        fill_rect(ox, oy + r, 16, 1, C["roof_dk"])
        off = 0 if (r // 4) % 2 == 0 else 4
        for c in range(off, 16, 8):
            fill_rect(ox + c, oy + r, 1, 4, C["roof_dk"])
            put(ox + (c + 1) % 16, oy + r + 1, C["roof_lt"])
    if ridge:
        fill_rect(ox, oy, 16, 3, C["roof_lt"])
        fill_rect(ox, oy + 3, 16, 1, C["outline"])
    if edge:
        fill_rect(ox, oy + 14, 16, 2, C["outline"])
        fill_rect(ox, oy + 13, 16, 1, C["roof_lt"])

draw_stone_wall(0, 1, door=False)       # (0,1) stone wall
draw_stone_wall(1, 1, door=True)        # (1,1) stone wall + door
draw_stone_wall(2, 1, win=True)         # (2,1) stone wall + window
draw_timber_wall(3, 1, win=False)       # (3,1) timber wall
draw_timber_wall(4, 1, win=True)        # (4,1) timber wall + window
draw_roof(5, 1, ridge=False, edge=False)# (5,1) roof mid
draw_roof(6, 1, ridge=True, edge=False) # (6,1) roof ridge
draw_roof(7, 1, ridge=False, edge=True) # (7,1) roof front edge

# (8,1) Wooden fence
ox, oy = tile_xy(8, 1)
fill_rect(ox + 6, oy + 2, 4, 13, C["outline"])
fill_rect(ox + 7, oy + 3, 2, 11, C["wood_lt"])
fill_rect(ox, oy + 6, 16, 3, C["wood_md"])
fill_rect(ox, oy + 6, 16, 1, C["wood_lt"])
fill_rect(ox, oy + 11, 16, 2, C["wood_md"])

# (9,1) Wood floor (interior)
ox, oy = tile_xy(9, 1)
fill_rect(ox, oy, 16, 16, C["wood_lt"])
for r in range(0, 16, 4):
    fill_rect(ox, oy + r, 16, 1, C["wood_md"])

# Row 2: Props, Resources, Crops (16x16)
# (0,2) Boulder / Stone vein
ox, oy = tile_xy(0, 2)
d = ImageDraw.Draw(img)
d.ellipse([ox + 2, oy + 4, ox + 14, oy + 14], fill=C["stone_md"], outline=C["outline"])
d.ellipse([ox + 4, oy + 5, ox + 10, oy + 9], fill=C["stone_lt"])

# (1,2) Berry bush (with berries)
ox, oy = tile_xy(1, 2)
d.ellipse([ox + 1, oy + 3, ox + 14, oy + 14], fill=C["leaf_md"], outline=C["outline"])
d.ellipse([ox + 3, oy + 4, ox + 11, oy + 10], fill=C["leaf_lt"])
for bx, by in [(4, 7), (9, 6), (7, 11), (11, 10)]:
    fill_rect(ox + bx, oy + by, 2, 2, C["berry"])

# (2,2) Bush (harvested)
ox, oy = tile_xy(2, 2)
d.ellipse([ox + 1, oy + 3, ox + 14, oy + 14], fill=C["leaf_md"], outline=C["outline"])
d.ellipse([ox + 3, oy + 4, ox + 11, oy + 10], fill=C["leaf_lt"])

# (3,2) Tree stump
ox, oy = tile_xy(3, 2)
d.ellipse([ox + 4, oy + 8, ox + 12, oy + 14], fill=C["bark1"], outline=C["outline"])
d.ellipse([ox + 5, oy + 8, ox + 11, oy + 11], fill=C["wood_lt"])

# (4,2) Campfire frame 0
ox, oy = tile_xy(4, 2)
fill_rect(ox + 3, oy + 10, 10, 3, C["bark2"])
fill_rect(ox + 4, oy + 9, 8, 2, C["stone_dk"])
fill_rect(ox + 6, oy + 4, 4, 6, C["fire3"])
fill_rect(ox + 7, oy + 5, 2, 5, C["fire2"])
fill_rect(ox + 7, oy + 7, 2, 2, C["fire1"])

# (5,2) Campfire frame 1
ox, oy = tile_xy(5, 2)
fill_rect(ox + 3, oy + 10, 10, 3, C["bark2"])
fill_rect(ox + 4, oy + 9, 8, 2, C["stone_dk"])
fill_rect(ox + 5, oy + 3, 5, 7, C["fire3"])
fill_rect(ox + 6, oy + 4, 3, 6, C["fire2"])
fill_rect(ox + 7, oy + 6, 2, 3, C["fire1"])

# (6,2) Grave / Wooden cross
ox, oy = tile_xy(6, 2)
fill_rect(ox + 4, oy + 12, 8, 3, C["dirt_dk"])
fill_rect(ox + 7, oy + 3, 2, 10, C["wood_dk"])
fill_rect(ox + 4, oy + 5, 8, 2, C["wood_md"])

# (7,2) Crop stage 1 (seedling)
ox, oy = tile_xy(7, 2)
for cx in (4, 8, 12):
    fill_rect(ox + cx, oy + 10, 2, 3, C["leaf_lt"])

# (8,2) Crop stage 2 (growing)
ox, oy = tile_xy(8, 2)
for cx in (3, 7, 11):
    fill_rect(ox + cx, oy + 6, 2, 8, C["leaf_md"])
    fill_rect(ox + cx - 1, oy + 7, 4, 2, C["leaf_lt"])

# (9,2) Crop stage 3 (ripe golden wheat/corn)
ox, oy = tile_xy(9, 2)
for cx in (3, 7, 11):
    fill_rect(ox + cx, oy + 4, 2, 10, C["leaf_md"])
    fill_rect(ox + cx - 1, oy + 3, 4, 5, C["gold"])

# (10,2) Barrel / Shared Chest
ox, oy = tile_xy(10, 2)
fill_rect(ox + 3, oy + 3, 10, 12, C["outline"])
fill_rect(ox + 4, oy + 4, 8, 10, C["wood_md"])
fill_rect(ox + 5, oy + 4, 6, 2, C["wood_lt"])
fill_rect(ox + 4, oy + 7, 8, 1, C["stone_dk"])
fill_rect(ox + 4, oy + 11, 8, 1, C["stone_dk"])

# (11,2) Workbench
ox, oy = tile_xy(11, 2)
fill_rect(ox + 2, oy + 4, 12, 10, C["outline"])
fill_rect(ox + 3, oy + 5, 10, 4, C["wood_lt"])
fill_rect(ox + 3, oy + 9, 3, 5, C["wood_dk"])
fill_rect(ox + 10, oy + 9, 3, 5, C["wood_dk"])
fill_rect(ox + 5, oy + 3, 5, 3, C["steel_dk"])
fill_rect(ox + 6, oy + 2, 3, 2, C["steel"])

# (12,2) Iron Ore Vein
ox, oy = tile_xy(12, 2)
d.ellipse([ox + 2, oy + 4, ox + 14, oy + 14], fill=C["stone_dk"], outline=C["outline"])
d.ellipse([ox + 4, oy + 5, ox + 10, oy + 9], fill=C["stone_md"])
fill_rect(ox + 5, oy + 7, 3, 2, C["steel"])
fill_rect(ox + 9, oy + 9, 3, 2, C["steel"])
fill_rect(ox + 6, oy + 11, 2, 2, C["steel_dk"])

# Row 3..4: Tall Trees (32x32 each, so 2x2 tiles: rows 3-4)
def draw_tree(tx, ty, c_lt, c_md, c_dk, birch=False):
    ox, oy = tile_xy(tx, ty)
    # shadow
    d.ellipse([ox + 6, oy + 24, ox + 26, oy + 30], fill=(30, 30, 30, 90))
    # trunk
    trunk_c = C["birch1"] if birch else C["bark1"]
    trunk_d = C["birch2"] if birch else C["bark2"]
    fill_rect(ox + 12, oy + 16, 8, 13, C["outline"])
    fill_rect(ox + 13, oy + 16, 6, 12, trunk_c)
    fill_rect(ox + 17, oy + 16, 2, 12, trunk_d)
    if birch:
        fill_rect(ox + 14, oy + 20, 2, 1, C["outline"])
        fill_rect(ox + 15, oy + 24, 3, 1, C["outline"])
    # canopy
    d.ellipse([ox + 3, oy + 3, ox + 28, oy + 22], fill=C["outline"])
    d.ellipse([ox + 4, oy + 4, ox + 27, oy + 21], fill=c_dk)
    d.ellipse([ox + 5, oy + 4, ox + 26, oy + 18], fill=c_md)
    d.ellipse([ox + 7, oy + 5, ox + 22, oy + 14], fill=c_lt)

draw_tree(0, 3, C["leaf_lt"], C["leaf_md"], C["leaf_dk"], False)  # Oak (tx=0,ty=3, 32x32)
draw_tree(2, 3, C["leaf_lt"], C["leaf_md"], C["leaf_dk"], True)   # Birch (tx=2,ty=3, 32x32)
draw_tree(4, 3, C["aut_lt"], C["aut_md"], C["aut_dk"], False)     # Autumn Oak (tx=4,ty=3, 32x32)

# Row 5: Player Characters (16x24, 4 directions x 2 walk frames, plus 4 seat colors at rows 5-6? Let's keep 16x24 at y=80..104 -> row 5..6)
# Actually let's place Player sprites at y=80 (16x24 each), 4 colors (seats 1..4) in rows at y=80, 104, 128, 152!
#Wait: 8x8 cells! 24 is 3 cells high. Let's use y=80, 104, 128, 152 -> all multiples of 8!
def draw_player(sx, sy, tunic_c, dir_idx, frame):
    # dir_idx: 0=down, 1=up, 2=left, 3=right
    # shadow
    d.ellipse([sx + 3, sy + 20, sx + 13, sy + 23], fill=(20, 20, 20, 90))
    step = 1 if frame == 1 else 0
    # legs
    fill_rect(sx + 5, sy + 16 + (step if dir_idx < 2 else 0), 3, 5, C["outline"])
    fill_rect(sx + 8, sy + 16 + ((1 - step) if dir_idx < 2 else 0), 3, 5, C["outline"])
    fill_rect(sx + 6, sy + 17 + (step if dir_idx < 2 else 0), 1, 3, C["pants"])
    fill_rect(sx + 9, sy + 17 + ((1 - step) if dir_idx < 2 else 0), 1, 3, C["pants"])
    # torso
    fill_rect(sx + 4, sy + 10, 8, 7, C["outline"])
    fill_rect(sx + 5, sy + 11, 6, 5, tunic_c)
    # head
    fill_rect(sx + 4, sy + 2, 8, 8, C["outline"])
    fill_rect(sx + 5, sy + 3, 6, 6, C["hair"] if dir_idx == 1 else C["skin"])
    # hair top
    fill_rect(sx + 5, sy + 2, 6, 3, C["hair"])
    if dir_idx == 0:  # facing down: eyes
        put(sx + 6, sy + 6, C["outline"])
        put(sx + 9, sy + 6, C["outline"])
    elif dir_idx == 2:  # left
        fill_rect(sx + 8, sy + 3, 3, 5, C["hair"])
        put(sx + 6, sy + 6, C["outline"])
    elif dir_idx == 3:  # right
        fill_rect(sx + 5, sy + 3, 3, 5, C["hair"])
        put(sx + 9, sy + 6, C["outline"])

tunic_colors = [C["tunic"], C["tunic_r"], C["tunic_g"], C["tunic_y"]]
for seat in range(4):
    py0 = 80 + seat * 24
    for d_i in range(4):
        for fr in range(2):
            draw_player((d_i * 2 + fr) * 16, py0, tunic_colors[seat], d_i, fr)

# Row at y=176: Fauna (16x16 each)
# (0,11) Deer, (1,11) Wolf, (2,11) Boar
def draw_animal(sx, sy, c1, c2, kind):
    d.ellipse([sx + 2, sy + 11, sx + 14, sy + 15], fill=(20, 20, 20, 80))
    fill_rect(sx + 3, sy + 6, 10, 6, C["outline"])
    fill_rect(sx + 4, sy + 7, 8, 4, c1)
    # legs
    fill_rect(sx + 4, sy + 11, 2, 3, c2)
    fill_rect(sx + 10, sy + 11, 2, 3, c2)
    # head
    fill_rect(sx + 10, sy + 4, 5, 5, C["outline"])
    fill_rect(sx + 11, sy + 5, 3, 3, c1)
    put(sx + 13, sy + 6, C["outline"])
    if kind == "deer":
        put(sx + 11, sy + 2, C["wood_lt"])
        put(sx + 13, sy + 2, C["wood_lt"])
        put(sx + 12, sy + 3, C["wood_lt"])
    elif kind == "wolf":
        put(sx + 11, sy + 3, c2)
        put(sx + 2, sy + 7, c2)
    elif kind == "boar":
        put(sx + 14, sy + 7, C["white"])

draw_animal(0, 176, C["deer1"], C["deer2"], "deer")
draw_animal(16, 176, C["wolf1"], C["wolf2"], "wolf")
draw_animal(32, 176, C["boar1"], C["boar2"], "boar")

# Row at y=192: UI Icons (16x16 each): 0=Axe, 1=Sword, 2=Hoe, 3=Hammer, 4=Campfire, 5=Wood, 6=Stone, 7=Berry, 8=Meat
def draw_icon_axe(sx, sy):
    for i in range(9):
        fill_rect(sx + 3 + i, sy + 12 - i, 2, 2, C["wood_md"])
    fill_rect(sx + 9, sy + 2, 5, 5, C["steel"])
    fill_rect(sx + 12, sy + 2, 2, 5, C["white"])

def draw_icon_sword(sx, sy):
    for i in range(9):
        fill_rect(sx + 4 + i, sy + 11 - i, 2, 2, C["steel"])
    fill_rect(sx + 3, sy + 10, 4, 4, C["gold"])
    fill_rect(sx + 2, sy + 12, 2, 2, C["wood_dk"])

def draw_icon_hoe(sx, sy):
    for i in range(9):
        fill_rect(sx + 3 + i, sy + 12 - i, 2, 2, C["wood_md"])
    fill_rect(sx + 9, sy + 2, 5, 2, C["steel"])
    fill_rect(sx + 12, sy + 4, 2, 3, C["steel_dk"])

def draw_icon_hammer(sx, sy):
    fill_rect(sx + 7, sy + 6, 2, 8, C["wood_md"])
    fill_rect(sx + 3, sy + 2, 10, 5, C["stone_lt"])
    fill_rect(sx + 3, sy + 5, 10, 2, C["stone_dk"])

def draw_icon_wood(sx, sy):
    fill_rect(sx + 2, sy + 6, 12, 6, C["outline"])
    fill_rect(sx + 3, sy + 7, 10, 4, C["bark1"])
    fill_rect(sx + 11, sy + 7, 2, 4, C["wood_lt"])

def draw_icon_stone(sx, sy):
    d.ellipse([sx + 3, sy + 4, sx + 13, sy + 13], fill=C["stone_md"], outline=C["outline"])
    d.ellipse([sx + 5, sy + 5, sx + 9, sy + 8], fill=C["stone_lt"])

def draw_icon_berry(sx, sy):
    d.ellipse([sx + 3, sy + 5, sx + 10, sy + 12], fill=C["berry"], outline=C["outline"])
    d.ellipse([sx + 7, sy + 6, sx + 13, sy + 12], fill=C["berry"], outline=C["outline"])
    fill_rect(sx + 6, sy + 3, 4, 2, C["leaf_lt"])

def draw_icon_meat(sx, sy):
    d.ellipse([sx + 3, sy + 5, sx + 12, sy + 12], fill=C["tunic_r"], outline=C["outline"])
    fill_rect(sx + 11, sy + 7, 3, 2, C["white"])

draw_icon_axe(0, 192)
draw_icon_sword(16, 192)
draw_icon_hoe(32, 192)
draw_icon_hammer(48, 192)
# Campfire icon reuses (4,2)
img.paste(img.crop((64, 32, 80, 48)), (64, 192))
draw_icon_wood(80, 192)
draw_icon_stone(96, 192)
draw_icon_berry(112, 192)
draw_icon_meat(128, 192)
# Chest/Barrel icon (144,192), Workbench icon (160,192), Fence icon (176,192), Iron icon (192,192)
img.paste(img.crop((160, 32, 176, 48)), (144, 192))
img.paste(img.crop((176, 32, 192, 48)), (160, 192))
img.paste(img.crop((128, 16, 144, 32)), (176, 192))
fill_rect(192 + 3, 192 + 6, 10, 6, C["outline"])
fill_rect(192 + 4, 192 + 7, 8, 4, C["steel_dk"])
fill_rect(192 + 5, 192 + 6, 6, 2, C["steel"])

img.save(OUT_SHEET)
print("Saved sheet:", OUT_SHEET)

# Generate cover.png (88x88) - cozy little house + tree scene
cov = Image.new("RGBA", (88, 88), (124, 168, 72, 255))
cd = ImageDraw.Draw(cov)
# dirt path
cd.rectangle([38, 60, 50, 88], fill=C["dirt1"])
# house scaled 2x from tiles
# roof
cd.rectangle([20, 22, 68, 42], fill=C["roof_md"], outline=C["outline"])
cd.rectangle([20, 22, 68, 28], fill=C["roof_lt"])
# timber wall
cd.rectangle([24, 42, 64, 56], fill=C["wood_lt"], outline=C["outline"])
# stone base
cd.rectangle([24, 56, 64, 70], fill=C["stone_md"], outline=C["outline"])
# door
cd.rectangle([39, 57, 49, 70], fill=C["wood_md"], outline=C["outline"])
# windows
cd.rectangle([28, 45, 36, 52], fill=C["win_lit"], outline=C["outline"])
cd.rectangle([52, 45, 60, 52], fill=C["win_lit"], outline=C["outline"])
# tree left
cd.ellipse([4, 30, 24, 52], fill=C["leaf_md"], outline=C["outline"])
cd.rectangle([12, 50, 16, 64], fill=C["bark1"])
cov.save(OUT_COVER)
print("Saved cover:", OUT_COVER)
