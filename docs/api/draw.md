# API: Screen, shapes, sprites and map

Part of the game API. Index and basics: [API.md](../API.md).

### Screen and shapes

| Function | Description |
|---|---|
| `rgb(r, g, b)` | the colour integer `0xRRGGBB` of three numbers 0-255 (the colours of the API are 24-bit integers: `0xFFFFFF` is white; a small number such as 7 is nearly black) |
| `cls([c])` | fills the screen (black if `c` is missing). With the 3D on the GPU the ARM does not fill it: the GPU's job clears the page to that colour (free even at 1080p); if 2D comes before the 3D the ARM fills it as always |
| `screen(w, h)` | the cartridge's resolution from the next frame: `true`, or `false` if it is not one of these (16:9): 320×180, 384×216, 480×270, 640×360, 960×540, 1280×720, 1920×1080 (on a 1080p TV all at whole pixels but 1280×720). `SCREEN_W` and `SCREEN_H` change when it is done; the z-buffer, the 3D camera and the light buffers follow, the font and `camera()` stay, `clip()` goes back to the whole screen. If the console cannot give it, the one before stays. `screen()` → the width and height now; `screen(i)` → the i-th of the list (from 1), or `nil`. On the **RGB30** (a square panel) the list is different: 1 = 360×360 (shown twice as big), 2 = 720×720 (both at whole pixels) and 3 = 720×405 (16:9, centred, black bars above and below; the order is fixed, so `screen(1)` and `screen(2)` stay as they were). A square cartridge (256×256, 360×360) changes as the others (2026-10-10): on the Pi it then fills the TV, and the GPU can draw its 3D there (in the box in the middle the ARM draws it). The ARM's rasteriser pays for every pixel: on the Pi above 640×360 the GPU is needed |
| `safe_area()` | `x, y, w, h` of the part of the screen that has the aspect the cartridge was made for (the size in its header), in the middle. Same aspect: `0, 0, W, H`. The design is wider than the screen: the whole width and the middle of the height. Otherwise: the whole height and the middle of the width (a 360×360 game on 640×360 gives `140, 0, 360, 360`). Lay the HUD and the menus out in this area when the screen can have another shape (`screen()`, the RGB30's 720×405) |
| `aspect()` | the screen's width divided by its height, and `"wide"` (wider than tall), `"square"` or `"tall"`. 640×360 gives `1.777…, "wide"`; 360×360 gives `1, "square"` |
| `pset(x, y, c)` / `pget(x, y)` | writes / reads a pixel (`pget` gives `0xRRGGBB`, or `nil` off screen) |
| `line(x0, y0, x1, y1, c)` | a line |
| `rect(x, y, w, h, c)` / `rectfill(x, y, w, h, c)` | an empty / filled rectangle |
| `circ(x, y, r, c)` / `circfill(x, y, r, c)` | an empty / filled circle |
| `tri(x0, y0, x1, y1, x2, y2, c, [c1, c2])` | a filled triangle; with three colours (one per corner) the colour shades from one corner to the other (Gouraud, dithered) |
| `print(text, x, y, [c, scale])` | text with the 8×16 font (white if `c` is missing), `scale` times bigger (1–8: 2 = 16×32 characters); returns the x after the last character |
| `font([name])` | the font of `print` from now on: `"8x16"` (the normal one), `"8x14"` or `"6x12"` (106 columns by 30 rows at 640×360: for tools with a lot of text); returns the width and height of a character of the current font |
| `prompt(name, x, y, [small, scale, player, menu])` | a key drawn as a coloured chip (the apps' set, `src/kernel/prompts.c`) with its top left corner at (x, y): 16 pixels high beside the 8×16 text, 12 with `small` (by itself when the font is `"6x12"`), `scale` times bigger (1–8, as `print`); returns the x after the chip. In **upper case** the pad's buttons (`"A"`, `"B"`, `"X"`, `"Y"`, `"START"`, `"SELECT"`, `"L1"`…`"R3"`, `"UP"`, `"UPDOWN"`, `"LEFTRIGHT"`, `"DPAD"`, `"LSTICK"`, `"PS"`, `"TOUCHPAD"`), drawn as on the pad used last: a DS4 (cross, circle, square, triangle, OPTIONS, SHARE) until another one is used, which has them with letters. On the **RGB30** `"A"`, `"B"`, `"X"`, `"Y"` are the console's own buttons (Nintendo layout: X top, A right, B bottom, Y left), a dark button with the letter in its colour: A green, B blue, X red, Y yellow (until a Bluetooth pad is used); `"RGB30_A"`, `"RGB30_B"`, `"RGB30_X"`, `"RGB30_Y"` draw them on any console. In **lower case** the mouse (`"mouse"`; `"mouse_l"` and `"mouse_r"` with the left or the right button in red) and the keyboard's keys, with the names of `keyp()` (`"enter"`, `"esc"`, `"tab"`, `"space"`, `"up"`, `"f1"`…) or a character (`"s"`, `"1"`, `"+"`). `"ok"`, `"back"` and the actions of `keymap()` are their button (on the RGB30 `"ok"` is B and `"back"` A, swapped by `confirm=a`: the chip is the button really pressed). With `player` (1–4, after `scale`: `prompt(name, x, y, small, scale, player)`) the button as that player's controller shows it: the DS4's symbol, a pad's letter or the keyboard key that presses it (`"space"` for A). `prompt(name, [small, scale, player])` without coordinates does not draw: it returns the width and height; with `menu` true (the seventh argument) the menu's set instead: white keys, raised on a lip, always 16 pixels high (`small` is ignored) |
| `camera([x, y])` | moves all drawing by (−x, −y); without arguments it resets it |
| `clip([x, y, w, h])` | limits drawing to the rectangle; without arguments the whole screen |

### Sprites and map

The **sprite sheet** is a picture divided into 8×8 cells: cell `n` is at column
`n % (width/8)` and row `n // (width/8)`. It comes from `--sheet sheet.png` (RGB or RGBA
PNG; alpha < 128 = transparent) or, without a PNG, it is a transparent 256×256 sheet to
draw with `sset` (32 cells per row: see `carts/shooter`). The sheet can be up to 4096×4096
pixels. A big sheet with at most 256 colours goes into the cartridge with
`--sheet sheet.png --sheet8`: a palette and repeated runs (RLE) instead of 4 bytes per
pixel, decoded at loading (Titan Clash: 2048×3376 pixels in 1.7 MB).

| Function | Description |
|---|---|
| `spr(n, x, y, [w, h, flip_x, flip_y])` | draws cell `n` (w×h cells, default 1×1), mirrored too |
| `sspr(sx, sy, sw, sh, dx, dy, [flip_x, flip_y, zoom])` | copies any rectangle of the sheet; with `zoom` (default 1) it draws it bigger (`2`, `3`…) or smaller (`0.5`), pixel by pixel: it covers `sw * zoom` × `sh * zoom` pixels |
| `stack(sx, sy, w, h, n, x, y, [rz, scale, step])` | sprite stacking: draws `n` slices of `w`×`h` pixels that lie side by side on the sheet from `(sx, sy)`, left to right. Slice 1 is the bottom and is drawn first. `(x, y)` is the centre of the bottom slice on the ground. Each slice turns by `rz` radians (default 0) around its centre, is `scale` times bigger (default 1) and sits `step * scale` pixels above the slice below (default `step` 1). Colour 0 is transparent. It reads the sheet each call and keeps no memory. At `rz` 0 and `scale` 1 it equals `n` calls of `sspr`. A slice that is not wholly inside the sheet is skipped. `scale` of 0 or less, or above 64, draws nothing. `rz`, `scale` or `step` that is NaN or infinite draws nothing |
| `stackv(sx, sy, w, h, n, x, y, [rz, scale, step])` | as `stack`, but the `n` slices lie one under the other on the sheet from `(sx, sy)`, top to bottom: the top one is slice 1, the bottom of the stack. The same drawing, the same limits (a slice that is not wholly inside the sheet is skipped), the same cost |
| `sget(x, y)` / `sset(x, y, [c])` | reads / writes a pixel of the sheet (`nil` = transparent) |
| `zspr(name, x, y, [frame, flip_x, flip_y, zoom])` | draws a **named zone** of the sheet (below): `frame` from 1 to the number of frames (beyond it starts again), or `nil`: the one its fps give at `time()`, so the animation runs by itself. Returns the frame drawn. A zone that is not there is an error |
| `zstack(name, x, y, [rz, scale])` | draws a **stack zone** (a zone with slices, below) as `stack` does (as `stackv` for a `stackv` zone), with `step` 1: the slices of the zone, `(x, y)` the centre of the bottom one. A zone without slices (or one that is not there) is an error |
| `zone(name)` | `x, y, w, h, frames, fps, slices` of the zone (its first frame, in sheet pixels), or `nil`. `slices` is 0 for a plain zone, 2 to 255 for a stack zone |
| `zones()` | the names of the zones, in order (`{}` if there are none) |
| `zboxes(name, [frame, kind])` | the **hitboxes and hurtboxes** of the frames of a zone (the BOXES section, 2026-10-04): a list of `{x, y, w, h, kind, frame}`, `x` and `y` from the frame's top left corner (they may stick out), `kind` `"hurt"` (where it can be hit), `"hit"` (where it hurts), `"body"` (the room the body takes) or a number of the game (3–255), `frame` 0 for a box of every frame. With `frame` (from 1; past the last it starts again) its boxes and those of every frame; without, all the zone's. `kind` (a name or a number) keeps one kind. `{}` if there are none; a zone that is not there is an error |
| `map(mx, my, [x, y, mw, mh, layer, mask])` | draws the map from cell (mx, my), mw×mh cells, at (x, y). `layer`: the number (from 1) or the name of the layer (default 1). `mask`: only the cells whose tile has at least one of those **flags** (`fget`; 0 or missing: all) |
| `mget(mx, my, [layer])` / `mset(mx, my, n, [layer])` | reads / writes a cell of the map (0 = empty; outside the map `mget` gives 0) |
| `fget(n, [f])` | the **flags** of tile (sheet cell) `n`: a byte, 8 flags; with `f` (0–7) that flag, `true` or `false` |
| `fset(n, f, on)` / `fset(n, byte)` | turns flag `f` of tile `n` on or off; with two arguments it writes all 8 flags at once |
| `mflags(x, y, [w, h, layer])` | the flags of the tiles the rectangle touches, **in pixels** (the map drawn at 0, 0; without `w` and `h` a point), put together (or); 0 = nothing. Cell 0 and the outside of the map have none. For collisions: `mflags(x, y + h, w, 1) & 1 ~= 0` (flag 0 under the feet) |
| `msize([w, h])` | `w, h, layers`: the map's size in cells and how many layers it has; with `w` and `h` every layer gets that size, the cells that fit staying where they were |
| `mlayers([list])` | the names of the layers, in order (the first is drawn at the back). With a list (1 to 8 entries) the map gets those layers: an entry is a name (the layer with that name, or a new empty one) or `{name, from}` (a copy of layer `from`, a number or a name): so they are added, moved, renamed and taken away |

**The map's layers (R11, 2026-10-04).** The map has 1 to 8 **layers** of the same size,
each with a name (the first is called `"main"` unless given another). They are drawn one
at a time, in the order you like: for example the background, then the sprites, then the
layer that goes **in front of** the character (tree tops, arches, roofs).

```lua
cls(0)
map(0, 0, 0, 0, 80, 45, "back")     -- the floor
spr(hero, x, y)
map(0, 0, 0, 0, 80, 45, "front")    -- the tree tops over the hero
```

**The tiles' flags.** Every tile of the sheet has 8 flags (0–7) that say what it is to the
game: wall, water, ladder, danger... Collisions look at the flags instead of the tiles'
numbers. The convention of [bmlib](bmlib.md#bmlib-the-games-shared-library) (every game can choose
another): flag 0 **solid** (1), 1 **platform** you can pass through from below (2), 2
**ladder** (4), 3 **water** (8), 4 **hurts** (16); 5–7 free.

```lua
if fget(mget(cx, cy), 0) then ... end            -- the cell is solid
if mflags(px, py + 8, 8, 1) & 1 ~= 0 then        -- the character (8x8) stands on something
  on_ground = true
end
map(0, 0, 0, 0, 80, 45, 1, 4)                    -- only the ladders
```

**Named zones.** The SPRITES section gives names to rectangles of the sheet, with their
frames (the boxes of the same size to the right of the first) and the animation's speed.
The code no longer needs the coordinates:

```lua
zspr("coin", x, y)                     -- the coin's animation, it runs by itself
zspr("hero_run", x, y, f, to_the_left)
local _, _, w, h = zone("hero_run")    -- its size, for collisions
```

A **stack zone** names the slices of a sprite stack: in the zones file the line
`stack name x y w h n` gives `n` slices (2 to 255) of `w`×`h` pixels, side by side from
`(x, y)`, the bottom slice first. `zstack("tree", x, y, rz, scale)` draws it. The zone has
one frame; `zone` returns the number of slices as its 7th value. The line
`stackv name x y w h n` is the same with the slices one under the other, from `(x, y)` down (the
sheet must hold `h`×`n` pixels below); `zstack` then draws as `stackv`.

**Hitboxes and hurtboxes of the frames.** Under a zone, in the zones file, its boxes:
`hurt` where the character can be hit, `hit` where its blow hurts, `body` the room it
takes (so that bodies do not pass through each other), with the frame (from 1, or `*` for
every frame) and the rectangle from the frame's corner. The game reads them with `zboxes`
and puts them in the world (mirrored too) with bmlib's [`lib.hits`](bmlib.md#hitboxes-and-hurtboxes):

```
# name  x  y  w  h  frames fps
punch   0  64 32 32 4 12
  hurt  *  8  2 16 30        # the body, in every frame
  hit   3  24 10 10  6       # the fist, only in frame 3
```

```lua
for _, b in ipairs(zboxes("punch", f, "hit")) do
  rect(x + b.x, y + b.y, b.w, b.h, 0xFF4040)     -- to see them while making the game
end
```

**Where they come from.** With `mkbm.py`: `--map map.csv` is the first layer; every other
`--map name=file.csv` is one more layer (up to 8, in the order given; a CSV smaller than
the first is filled with empty cells); `--flags flags.csv` the flags (numbers 0–255 for
cells 0, 1, 2... in order, or `n=flags` for cell `n`); `--sprites zones.txt` the zones (one
per line: `name x y w h [frames [fps]]`, or `stack name x y w h n` for a stack zone; under a zone its boxes: `hurt`, `hit` or `body
frame x y w h`, or `box kind frame x y w h` for a kind of the game; the names `hurt`,
`hit`, `body`, `box` and `stack` are not zones). In the repository's build the files
`map_<name>.csv` (in order with `layers_<game> := name ...` in the `Makefile`),
`flags.csv` and `sprites.txt` in the game's folder are enough. On the console, on the
SDK's map page `l` goes to the next layer, Shift+L adds one (also from the menu, *New map
layer*), `o` shows that one alone, `c` the flags of the tiles; on the sprite page keys
`0`–`7` turn the cell's flags on; bm Pixel and `scripts/bmres.py` make zones (`bmres.py`
carries the boxes with their zones). `cart_save` and `cart_load` carry layers, flags, zones
and boxes with the cartridge. Format: the LAYERS (12), FLAGS (13), SPRITES (11) and BOXES
(14) sections in `src/bm/bm.h`; a kernel of before draws the first layer and ignores the
rest.

The map comes from `--map map.csv` (one row of comma-separated numbers per row of the
map; each number is a cell of the sheet). Without a map it is 256×256 empty cells, one
layer.
