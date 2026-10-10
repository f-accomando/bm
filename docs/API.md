# Native `.bm` cartridges: API and first cartridge

> A step-by-step practical guide (sprites, maps, 3D models, sound, lights, saves):
> [GAME-GUIDE.md](GAME-GUIDE.md). In Italian: [API-IT.md](API-IT.md) and
> [GUIDA-GIOCHI.md](GUIDA-GIOCHI.md).

A `.bm` cartridge is a game for bm written in **Lua 5.4**. The kernel draws in C
(640×360, 16-bit RGB565 colour, 60 frames a second); Lua only does the logic. Complete
examples in `carts/`: `pong/`, `snake/`, `shooter/` (code only) and `demo/` (with a PNG
sprite sheet and a CSV map).

## The first cartridge in 5 minutes

1. Create `carts/hello/main.lua`:

   ```lua
   local x, y = 300, 160

   function _init()                 -- once, at start
   end

   function _update()               -- 60 times a second: logic
     if btn(0) then x = x - 3 end   -- left
     if btn(1) then x = x + 3 end   -- right
     if btn(2) then y = y - 3 end   -- up
     if btn(3) then y = y + 3 end   -- down
   end

   function _draw()                 -- 60 times a second: drawing
     cls(0x102040)                  -- dark blue background
     circfill(x, y, 20, 0xFFD050)   -- a yellow ball
     print("hello from bm!", 8, 8, 0xFFFFFF)
   end
   ```

2. Pack it:

   ```sh
   python3 scripts/mkbm.py -o hello.bm --lua carts/hello/main.lua --title "Hello" --author "me"
   ```

   To add it to the build, put its name in `GAMES` in the `Makefile` and its title in a
   line `title_hello := Hello`: `make` creates it in `build/carts/hello.bm`.

   **Cover** (optional): `--cover cover.png`, a PNG of any size that the menu prints on
   the card: an 88×88 square (2026-10-04; 128×80 before). A square picture is scaled
   down; others stay whole, as wide as the square, over a blurred and darker copy of
   themselves that fills the rest. In the build a file `carts/hello/cover.png` is enough;
   those of the demo games are drawn by `scripts/mkcovers.py`. Without a cover the menu
   prints the title.

3. Try it:
   - **on the Pi**: copy `hello.bm` into the `carts/` folder of the SD card, power on (or
     press `R` in the menu): it appears in the menu with its title and author;
   - **in QEMU** (without a Pi):

     ```sh
     qemu-system-arm -M raspi0 -bios build/kernel.img -serial tcp:127.0.0.1:4444,server=on,wait=off -serial null &
     python3 tools/bm_load.py tcp:127.0.0.1:4444 --cart hello.bm
     ```

     (from the menu or the monitor: the loader sends `U`, then the cartridge);
   - **from the serial line** (a Pi with a USB-serial adapter):
     `python3 tools/bm_load.py /dev/ttyUSB0 --cart hello.bm`.

Ctrl+Esc (keyboard), PS or Start+Select (gamepad) go back to bm's menu leaving the game
**suspended** (still in memory: A on its cover resumes it where it was; if the cartridge has
`_exit()`, they ask it first); Esc alone, in a game, is Start (its menu). In a **network
match** (`online(true)`) the game is not suspended: the system asks the player who leaves,
only on their console, "Leave the match?" (they will leave the game and disconnect from the
server); yes calls `_leave()` and closes the game, back stays. From the serial line Ctrl+\
is Ctrl+Esc and `q` closes without asking. The system's keys (one table,
`src/kernel/syskeys.c`) and the cartridge's are shown while F12 is held. If there is a Lua
error, the cartridge stops and the error, with its line, appears on the console.

## Structure

| Function | When |
|---|---|
| `_init()` | once, after loading |
| `_update()` | every frame (60 Hz), before `_draw` |
| `_draw()` | every frame, after `_update` |
| `_exit()` | (optional) Ctrl+Esc, PS or Start+Select: `true` closes now; `false` stays (the cartridge asks, for example "unsaved changes: Ctrl+Esc again leaves", and closes later with `quit()` or at the second Ctrl+Esc) |
| `_leave()` | (optional) in a network match (`online(true)`), when the player confirms leaving: the game tells the server (or the host), then the cartridge closes without being suspended |

Globals: `SCREEN_W` and `SCREEN_H` (640 and 360; 480 and 270 with `--res 480x270`; 320 and
180 with `--res 320x180`; 256 and 256 with `--res 256x256`; 360 and 360 with `--res 360x360`, the
`.b16`'s screen). The cartridge can change its
resolution while it runs with `screen(w, h)` (below): from the next frame the new sizes hold.
The screen is **not** cleared by itself: `_draw` usually starts with `cls()`.

Limits: an error or an endless loop (over **20 million Lua instructions** in one frame)
stops the cartridge without blocking the console. Sandbox: no `io`, `os`, `load`,
`dofile`; `require` loads only the libraries built into the kernel (`"bmlib"`, the games'
shared library: [below](#bmlib-the-games-shared-library); `"assist"`, the assistant's
panel; `"bm3d"`, what bm Studio and bm Animator share; `"predict"` and `"words"`, the word
completion); there are `string`, `table`, `math`, `utf8`, `coroutine`.

## Colours

Colours are integers `0xRRGGBB` (e.g. `0xFF8000` orange) or `rgb(r, g, b)` with values
0–255. The screen turns them into RGB565 (5 bits red, 6 green, 5 blue).

## Reference

Coordinates are in pixels, (0,0) at the top left; `w` and `h` are width and height.

### Screen and shapes

| Function | Description |
|---|---|
| `cls([c])` | fills the screen (black if `c` is missing). With the 3D on the GPU the ARM does not fill it: the GPU's job clears the page to that colour (free even at 1080p); if 2D comes before the 3D the ARM fills it as always |
| `screen(w, h)` | the cartridge's resolution from the next frame: `true`, or `false` if it is not one of these (16:9): 320×180, 384×216, 480×270, 640×360, 960×540, 1280×720, 1920×1080 (on a 1080p TV all at whole pixels but 1280×720). `SCREEN_W` and `SCREEN_H` change when it is done; the z-buffer, the 3D camera and the light buffers follow, the font and `camera()` stay, `clip()` goes back to the whole screen. If the console cannot give it, the one before stays. `screen()` → the width and height now; `screen(i)` → the i-th of the list (from 1), or `nil`. A 256×256 cartridge keeps its screen. The ARM's rasteriser pays for every pixel: above 640×360 the GPU is needed |
| `pset(x, y, c)` / `pget(x, y)` | writes / reads a pixel (`pget` gives `0xRRGGBB`, or `nil` off screen) |
| `line(x0, y0, x1, y1, c)` | a line |
| `rect(x, y, w, h, c)` / `rectfill(x, y, w, h, c)` | an empty / filled rectangle |
| `circ(x, y, r, c)` / `circfill(x, y, r, c)` | an empty / filled circle |
| `tri(x0, y0, x1, y1, x2, y2, c, [c1, c2])` | a filled triangle; with three colours (one per corner) the colour shades from one corner to the other (Gouraud, dithered) |
| `print(text, x, y, [c, scale])` | text with the 8×16 font (white if `c` is missing), `scale` times bigger (1–8: 2 = 16×32 characters); returns the x after the last character |
| `font([name])` | the font of `print` from now on: `"8x16"` (the normal one), `"8x14"` or `"6x12"` (106 columns by 30 rows at 640×360: for tools with a lot of text); returns the width and height of a character of the current font |
| `prompt(name, x, y, [small, scale])` | a key drawn as a coloured chip (the apps' set, `src/kernel/prompts.c`) with its top left corner at (x, y): 16 pixels high beside the 8×16 text, 12 with `small` (by itself when the font is `"6x12"`), `scale` times bigger (1–8, as `print`); returns the x after the chip. In **upper case** the pad's buttons (`"A"`, `"B"`, `"X"`, `"Y"`, `"START"`, `"SELECT"`, `"L1"`…`"R3"`, `"UP"`, `"UPDOWN"`, `"LEFTRIGHT"`, `"DPAD"`, `"LSTICK"`, `"PS"`, `"TOUCHPAD"`), drawn as on the pad used last: a DS4 (cross, circle, square, triangle, OPTIONS, SHARE) until another one is used, which has them with letters. In **lower case** the keyboard's keys, with the names of `keyp()` (`"enter"`, `"esc"`, `"tab"`, `"space"`, `"up"`, `"f1"`…) or a character (`"s"`, `"1"`, `"+"`). `"ok"`, `"back"` and the actions of `keymap()` are their button. With `player` (1–4, after `scale`: `prompt(name, x, y, small, scale, player)`) the button as that player's controller shows it: the DS4's symbol, a pad's letter or the keyboard key that presses it (`"space"` for A). `prompt(name, [small, scale, player])` without coordinates does not draw: it returns the width and height |
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
| `sget(x, y)` / `sset(x, y, [c])` | reads / writes a pixel of the sheet (`nil` = transparent) |
| `zspr(name, x, y, [frame, flip_x, flip_y, zoom])` | draws a **named zone** of the sheet (below): `frame` from 1 to the number of frames (beyond it starts again), or `nil`: the one its fps give at `time()`, so the animation runs by itself. Returns the frame drawn. A zone that is not there is an error |
| `zone(name)` | `x, y, w, h, frames, fps` of the zone (its first frame, in sheet pixels), or `nil` |
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
numbers. The convention of [bmlib](#bmlib-the-games-shared-library) (every game can choose
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

**Hitboxes and hurtboxes of the frames.** Under a zone, in the zones file, its boxes:
`hurt` where the character can be hit, `hit` where its blow hurts, `body` the room it
takes (so that bodies do not pass through each other), with the frame (from 1, or `*` for
every frame) and the rectangle from the frame's corner. The game reads them with `zboxes`
and puts them in the world (mirrored too) with bmlib's [`lib.hits`](#hitboxes-and-hurtboxes):

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
per line: `name x y w h [frames [fps]]`, under a zone its boxes: `hurt`, `hit` or `body
frame x y w h`, or `box kind frame x y w h` for a kind of the game; the names `hurt`,
`hit`, `body` and `box` are not zones). In the repository's build the files
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

### Input

| Function | Description |
|---|---|
| `btn(i, [p])` | `true` while the button is held; without `p` from **any** controller, with `p` = 1–4 only from player `p`'s. Instead of `i` also a **name** (2026-10-04): a button (`"a"`, `"b"`, `"x"`, `"y"`, `"left"`…`"down"`, `"start"`, `"select"`, `"l1"`, `"r1"`, `"l2"`, `"r2"`, `"l3"`, `"r3"`), `"ok"` and `"back"` (the system's yes and back: cross and circle on the DS4, A and B on an Xbox pad, Space and X on the keyboard; on the RGB30 as `confirm=` and `game_buttons=` say) or an action of `keymap()` |
| `btnp(i, [p])` | `true` only in the frame it is pressed (the same `p`, also with a name) |
| `keymap(t)` | the game's **actions** on the buttons: `keymap({ jump = "a", fire = {"x", "r1"}, pause = "start", confirm = "ok" })` (up to 32 actions, 4 buttons each); then `btn("jump", p)`, `btnp("fire")` and `prompt("jump", x, y)`. The game changes the keys by calling it again (its options menu; it keeps them with `save()`); `keymap()` returns the table, `keymap(nil)` takes it away. An unknown button or an action with a button's name is an error |
| `controller([p])` | what player `p` (1–4, the first if missing) plays with: `{kind = "keyboard" / "ds4" / "xbox" / "pad" / "builtin" / "none", layout = "keyboard" / "ds4" / "xbox" / "nintendo" / "none", bluetooth = bool, ok = "a" / "b", back = "b" / "a"}`: `layout` says what the buttons are called (the DS4's symbols; letters with A at the bottom as Xbox; A on the right as the RGB30), `ok` and `back` which of the game's buttons are the yes and the back, `color` the player's colour (that of their pad's light: 1 blue, 2 red, 3 green, 4 pink) |
| `online([on, note])` | the match is played over the **network** (2026-10-04): with `online(true)` PS, Ctrl+Esc and Start+Select do not suspend the game (the others go on playing) but ask the player who leaves, only on their console, "Leave the match?" (they will leave the game and disconnect from the server), over the game that goes on. While the question is open the game sees neither buttons nor sticks nor keys; ok (cross on the DS4, Enter or Space on the keyboard) or PS again leave: `_leave()` and the cartridge closes; back (circle, Esc) stays, and the buttons still held go back to the game only after being released. `note`: a line under the question (e.g. `"You are the host: the match ends for all."`). `online(false)` at the end of the match; `online()` returns whether it was online and whether the question is open (the player is away) |
| `players()` | how many players have a controller (at least 1) and, as the second value, which: bit `n` = player `n+1` (e.g. `3, 7` = players 1, 2 and 3) |
| `stick([p, n])` | player `p`'s left stick: `x, y` between −1 and 1 (x to the right, y down), with a dead zone; with the keyboard or a pad without a stick it is the cross (8 directions). With `n = 1` the **right** stick (to aim in shooters; `0, 0` without a stick). Without `p`: the one pushed most |

**Mouse and pointer (M32).** A cartridge has the pointer only if it asks for it: without
`mouse(true)` there is none (in bm's menu there always is). Only a USB or Bluetooth mouse
moves it: a controller never shows it (its right stick is `stick(p, 1)`); the console may
have it off for the whole system (`mouse=off` in `bm/config.txt`).

| Function | Description |
|---|---|
| `mouse(on, [arrow])` | `mouse(true)`: the cartridge wants the pointer, and the console draws its arrow over the frame (`mouse(true, false)`: no arrow, the cartridge draws its cursor); `mouse(false)` takes it away. Returns `false` if the console has the mouse off |
| `mouse()` | `x, y, buttons, wheel, visible`: the position in pixels of the cartridge's screen (without `camera`), the buttons held as bits (1 left, 2 right, 4 middle), the wheel's clicks in this frame (up positive) and `true` if the pointer shows (as soon as something moves it). `nil` if the cartridge has not asked for it or if nothing moves it (neither mouse nor right stick) |
| `mousep([i])` | `true` in the frame button `i` is pressed (0 left, the default; 1 right; 2 middle) |

```lua
function _init() mouse(true) end
function _update()
  local x, y = mouse()
  if x and mousep() then sfx(0) end   -- a click
end
```

**bmui, the mouse of bm's tools** (`require "bmui"`, `src/script/bmui.lua`, 2026-10-06): the
editors (SDK, bm Code, bm Studio, bm Animator, bm Mesh, bm Pixel, Sound, the assistant's
panel) use it, and a game or a tool of your own can too. Its rule: the mouse does what the
keys do. Things are made clickable as they are drawn, and a click presses the key behind
them or chooses what they show; the right button opens a context menu whose entries are keys.

| Function | Description |
|---|---|
| `U.update()` / `U.begin()` | in `_update` first (reads `mouse()` once a frame), in `_draw` first (forgets the zones of the frame before) |
| `U.zone(x, y, w, h, kind, a, b)` | something clickable, as it is drawn (the last one drawn wins: a dialog over the page); `U.at()` the zone under the pointer, `U.click([b])` the zone pressed this frame (0 left, 1 right, 2 middle), `U.clicked(b)` a button let go without a drag (the zone where it went down, or `true`) |
| `U.key_zone(x, y, w, h, keys)` / `U.keys()` | a chip (`"ctrl s"`, `{"shift", "l"}`, `"f5"`) that presses its key when clicked; `U.keys()` does it for the click of the frame |
| `U.press(key)` | a key as if typed: the next `keyp()` gives it (bmui puts its own `keyp` first) |
| `U.menu(items, [x, y])` / `U.menu_update()` / `U.menu_draw()` | the context menu at the pointer: `{ {"Copy", "^c"}, "-", {"Run", function() ... end} }` (the key shown on the right, "Ctrl+C"); `U.menu_update()` in `_update` (true: it took the input), `U.menu_draw()` last in `_draw`; rows on the 8x16 grid |
| `U.x`, `U.y`, `U.on`, `U.moved`, `U.wheel`, `U.double`, `U.drag` | the pointer (`U.on`: a mouse moved it and it shows), whether it moved, the wheel's clicks of the frame, a double click, the drag `{b, x0, y0, dx, dy}` (past 3 pixels; `dx, dy` the frame's move); `U.pressed(b)`, `U.released(b)`, `U.down(b)`, `U.shift()`, `U.drag_arrows(step)` (a drag as arrow keys) |

In the editors: a click chooses a row, a double click (or a click on the chosen one in the
dialogs) takes it, the wheel moves or changes a value; in the 3D views the right button
dragged turns, the middle one moves, the wheel zooms; on canvases the left button draws and
the middle one picks the colour; in bm Code a click places the cursor, a drag selects, a double
click takes the word, a click in the numbers is a breakpoint.

**More players (M16).** Bluetooth controller *n* is player *n* (paired from the monitor
with `T`, one at a time: each takes the first free place and its light the player's colour:
1 blue, 2 red, 3 green, 4 pink). The USB keyboard or gamepad and the serial line are the
first player without a pad (without Bluetooth pads: player 1); the Bluetooth keyboard (M28)
is a player of its own, the next one without a pad (the first, if there is nothing on USB).
One-player games use `btn(i)` without `p` and work with any controller; a game for more
players asks `btn(i, p)` for each (example: `carts/pong`, two-player mode). bmlib has the
screen where the players join (`lib.party`), the split screen (`lib.split`) and the
colours (`lib.PLAYER_COLORS`): [Players on one console](#players-on-one-console); the SDK's
*Versus 2D* template uses them.

| `i` | Keyboard | Gamepad | Serial |
|---|---|---|---|
| 0 left | ← or A | cross / stick | `a` or ← |
| 1 right | → or D | cross / stick | `d` or → |
| 2 up | ↑ or W | cross / stick | `w` or ↑ |
| 3 down | ↓ or S | cross / stick | `s` or ↓ |
| 4 **A** | space, Z, J | A / cross (DS4) | space, `j` |
| 5 **B** | X, K | B / circle | `x`, `k` |
| 6 **X** | C, L | X / square | `c`, `l` |
| 7 **Y** | V, I | Y / triangle | `v`, `i` |
| 8 **Start** | Enter | Start / Options | Enter |
| 9 **Select** | Tab | Select / Share | — |

Start+Select together (or the PS button) always close the cartridge: Start alone is free for
the game's pause.

A cartridge that never asks `btn(6)`/`btn(7)` (or `btnp`) gets X as A and Y as B: games
with two buttons work with all four.

**Keys chosen by the player.** A cartridge that lets the player choose the keys (as nano8)
reads the keyboard key by key and the controllers button by button:

| Function | Description |
|---|---|
| `rawkeys(on)` | with `true` the keyboards stop being controllers for `btn()` and `pad()`: they are read with `keydown()`. Ctrl+Esc still closes the cartridge |
| `keydown(u)` | `true` while the key with USB HID usage `u` is held (USB or Bluetooth): `0x04`…`0x1D` the letters A–Z, `0x1E`…`0x27` the digits, `0x28` Enter, `0x2C` space, `0x4F`…`0x52` the arrows (right, left, down, up), `0xE0`…`0xE7` Ctrl, Shift, Alt, GUI on the left and then on the right |
| `keys()` | the usages of the keys held now (`{0x1D, 0xE1}`): for "press a key" |
| `pad([p])` | the buttons player `p` (1–4) holds, as bits: 1 left, 2 right, 4 up, 8 down, 16 A, 32 B, 64 Start, 128 Select, 256 X, 512 Y, 1024 L1, 2048 R1, 4096 L2, 8192 R2, 16384 L3, 32768 R3 (the triggers and the pressed sticks: DS4 and Xbox 360; on generic pads buttons 7–8 and 11–12); without `p` everybody's. The serial keys count as the first player's controller (L1 and R1: `u` and `o` from the serial line, Q and E from the USB keyboard; Select: Tab from both) |
| `lastinput()` | what was pressed last: `"keyboard"`, `"ds4"` or `"pad"` (another controller); `nil` before any key. To show the right keys with `prompt()` (for example `"enter"` or `"A"`) |

### Time and system

| Function | Description |
|---|---|
| `time()` | seconds since the cartridge started (with decimals) |
| `stat(n)` | 0 KiB used by Lua, 1 ms of the last frame (`_update` + `_draw`, with the GPU's 3D), 2 fps, 3 frame number, 4 3D triangles, 5 3D pixels (0 with the GPU), 6 ms spent in 3D drawing (since `zclear`; with the GPU the ARM's part), 7 3D vertices transformed, 8 ms since the start of this frame (to measure the phases), 9 `1` if the GPU draws the 3D, 10 Lua instructions of the last frame (`_update` + `_draw`, in thousands); the **dev kit** (2026-10-04): 11 tokens of the cartridge's code (`code_tokens`), 12 the most KiB of Lua of this run, 13 KiB of the cartridge's data in memory (sprite sheet, map, models and skeletons, sound bank, the 3D z-buffer), 14 the Lua instructions of the busiest frame of this run; 15 how many `_update` ran before this `_draw` (1; more with `frameskip`) |
| `frameskip([n])` | the game's time at 60 `_update` a second whatever `_draw` costs (2026-10-05): when a frame takes longer than 1/60 s, up to `n` `_update` run before the next `_draw` (the frames in between are not drawn), so a game that moves 1/60 s per `_update` does not slow down; past `n` the time is let go (the game slows rather than never drawing). `1` (the default) is one `_update` a frame, as before; at most 8. Returns the old value. A button pressed counts once in `btnp()` (and `mousep()`, the wheel) however many `_update` see it; `btn()` stays held. Overbit uses `frameskip(4)` (its benchmark `1`) |
| `devkit([mode])` | the dev kit's overlay: `0` off, `1` simple, `2` detailed, `3` functions; with a mode it shows that page (a game's own key for it, e.g. Select on a pad: F11 is the keyboard's), in this run only: every game starts as Settings says. Returns the old mode |
| `profile([on])` | the profiler of functions (R14): the functions that cost the most in the last whole second (60 frames), the costliest first, up to 32: `{name, where, c, self, total, calls}` (`where` `"main.lua:120"` or `"[C]"`; `c` true for the console's functions and Lua's written in C; `self` and `total` ms a frame, its own time and with what it calls; `calls` the calls a frame, C ones only), and the frames measured (0 during the first second). `profile(true)` turns it on for the run even without the dev kit's page, `profile(false)` turns it off (and forgets). E.g. `for _, f in ipairs(profile()) do log(f.name, f.self) end` |
| `devinfo(line, ...)` | up to 4 lines of the game on the dev kit's detailed page (its quality, its actors...), 18 characters each; `devinfo()` none. Call it again when they change (Overbit every frame while the detailed page is shown) |
| `code_tokens(text)` | the **tokens** of a piece of Lua code, counted as `stat(11)`, the overlay and the SDK's dev kit do (`src/bm/tokens.c`): each name, keyword, number, string and operator is one; comments, spaces, `,` `.` `:` `;` `::`, closing brackets (`)` `]` `}`), `end` and `local` do not count, nor the minus sign in front of a number (`-1` is one token). Information, not a limit: bm puts no ceiling on tokens (nor does the `.b16`, [B16.md](B16.md) §2.4) |
| `log(...)` | writes in the kernel's log (serial line and console), not on the game's screen |
| `report(kind, text)` | a report for the people who develop bm (2026-10-04): saved in `bm/reports` on the SD card with kernel, branch, board and date, then sent to the reports' repository if there are `github_token` and the network (`src/kernel/reports.h`); at most 8 per run, 256 KiB each; `true` if saved. The first time a game calls it, the player is asked (the answer stays in `bm/config.txt`, `allow_...`): after a no, `false` |
| `quit()` | closes the cartridge at the end of the frame |
| `timeslice(co, [k])` | coroutine `co` stops by itself after about `k` thousand Lua instructions in a frame (400 if missing) and `coroutine.resume` returns `true` without values: a long computation goes on in the next frames instead of stopping the cartridge at the instruction limit. `timeslice(nil)` takes it away (nano8 uses it for its cartridges) |

Random numbers: `math.random`. For different games at every start, seed the generator when
the player presses a key: `math.randomseed(stat(3))`.

### Network (UDP)

For network games (M38.5: Overbit). A game is better off with the
[bmnet](#bmnet-games-over-the-network) library (lobby, sure messages, lockstep), which uses
these functions. UDP packets up to 1024 bytes; each cartridge has 2
sockets, closed when it ends. Addresses are text (`"192.168.1.23"`); `"*"` is the LAN's
broadcast. The console's network is needed (WiFi or cable): without it `udp_open` returns
`nil`. In bmhost the same packets go through the PC's sockets (`BMHOST_NET_ID=k` for more
consoles on the same PC, `--realtime` to play at 60 frames a second).

| Function | What it does |
|---|---|
| `s, port = udp_open([port])` | a socket on the port (0 or nothing: any); `nil` and the reason if there is no network or free socket. The first time a game opens one, the player is asked whether it may use the network (the answer stays in `bm/config.txt`); after a no, `nil` and the reason |
| `udp_send(s, address, port, data)` | sends a string (at most 1024 bytes); `true` if it left (UDP: it can be lost) |
| `data, address, port = udp_recv(s)` | the next packet that arrived, or `nil`; up to 48 wait in the queue |
| `udp_close(s)` | closes the socket |
| `net_ip()` | the console's address, or `nil` without a network |
| `net_resolve(name)` | the address of a name: `nil` while it looks for it (call it again the frame after), `false` if it does not exist |

### Saves

| Function | Description |
|---|---|
| `save(t, [slot])` | saves table `t` on the SD card, in slot 1–8 (default 1); `true`, or `false` and the reason (no SD card, card full...) |
| `saved([slot])` | the table saved last time in that slot (default 1), or `nil` |
| `saves()` | the slots in use, `{[slot] = bytes}` (e.g. `{[1] = 40, [3] = 212}`), and how many there are (8): for a "load game" page |
| `delsave([slot])` | empties the slot (default 1); `true`, or `false` and the reason (`"nothing saved in slot 3"`) |

Each cartridge has **8 save slots**, each a table of up to 32 KiB, in `/bm/save/` on the SD
card: slot 1 is `XXXXXXXX.SAV` (the one of always: `save(t)` and `saved()` without a slot),
the others `XXXXXXXX.S02` ... `.S08`. The name depends on title and author: changing them
starts again from zero. A common use: slot 1 for settings and records (`lib.store` uses it
too), the others for the games in progress. A table can hold numbers, strings, booleans and
other tables (no functions). The game's options in the menu (X on the cover) show the bytes
of all the slots and *Delete the save data* deletes them all. Writing on the SD card takes a
few milliseconds: call `save()` at moments like the end of a match, not in every frame
(`lib.store` and `lib.best` of [bmlib](#bmlib-the-games-shared-library) write only when a
value changes). Example (Snake's record):

```lua
function _init()
  local data = saved()
  if data then best = data.best end
end
-- at the end of the match
if score > best then best = score; save({ best = best }) end
```

Three games in progress, with the page to choose one (slots 2, 3 and 4):

```lua
local function slots()                  -- the rows of the "load game" page
  local used, rows = saves(), {}
  for i = 1, 3 do
    local p = used[i + 1] and saved(i + 1)
    rows[i] = p and ("Game " .. i .. ": level " .. p.level) or ("Game " .. i .. ": empty")
  end
  return rows
end
function save_game(i) save({ level = level, hp = hp }, i + 1) end
function load_game(i) local p = saved(i + 1) if p then level, hp = p.level, p.hp end end
```

### Documents

The player's files, shared by the apps: `/docs` on the SD card (bm Write keeps its `.BMD`
there, with the exports `.TXT`, `.MD`, `.HTM` and `.PDF`). Names are 8.3 without folders
(`"LETTER.BMD"`; lower case is fine, the file is written in upper case); the folder is made
by the first write. The first time an app uses them the console asks the player, as for the
network; the answer stays in `bm/config.txt` (`allow_...=docs=yes`), and after a no every
call returns `nil` and the reason.

| Function | Description |
|---|---|
| `doc_list()` | the files in `/docs`: `{ {name = "LETTER.BMD", size = 1234}, ... }` (empty if there are none); `nil` and the reason if the player did not allow it |
| `doc_read(name)` | the bytes of a document (a string), or `nil` and the reason |
| `doc_write(name, bytes)` | writes a document, new or replaced (at most 4 MiB); `true`, or `nil` and the reason |
| `doc_delete(name)` | deletes a document; `true`, or `nil` and the reason |

```lua
local ok, why = doc_write("NOTES.TXT", "shopping:\nbread\nmilk\n")
if not ok then msg = why end
for _, d in ipairs(doc_list() or {}) do print(d.name .. " " .. d.size) end
```

### Sound

Audio goes out in **stereo** at 48 kHz (on the Pi over HDMI, the monitor's speakers; on
the RGB30 from the headphones or the speaker) and is generated in an interrupt: it costs
nothing to your `_update`. The synthesizer is **hi-fi** (2026-10-06): waves without
aliasing, exponential envelopes, a resonant filter, a room (reverb) and an echo, a light
compressor on the output; the **8-bit** voice of before stays (`retro(true)`, `tone()`'s
`raw`, or *Settings > Screen and sound > Sound style*). There are two ways to use it, also
together:

- **the sound bank** of the cartridge (sound effects and music made with the Sound editor
  of the Dev tab): `sfx(n)` and `music(n)`;
- **notes** played by the code, one voice at a time: `note`, `slide`, `arp`...

Eight voices (0–7). Waveforms: `SQUARE` (with `duty`), `TRIANGLE`, `SAW`, `NOISE`, `SINE`,
`METAL` (short metallic noise: cymbals, bells), `FM` (two sines, one bending the other:
electric piano, bells, basses, brass), `PLUCK` (a plucked string: guitars, harps),
`SUPERSAW` (three saws a little out of tune: pads, wide leads), `ORGAN` (four harmonics:
organs, flutes). Each voice has an ADSR envelope, a filter, a place between left and right
and how much goes to the room and the echo (`tone()`). Pitches
are in **Hz** (with decimals too: `261.63`) or a **note name**: `"C4"` (middle C), `"A4"`
(440 Hz), `"F#3"`, `"Bb2"`.

#### Sound effects and music (the cartridge's bank)

| Function | Description |
|---|---|
| `sfx(n, [v], [semitones], [vol])` | plays sound effect `n` of the bank; without `v` it chooses a free voice, preferring those the music leaves empty. `semitones` transposes it (`sfx(0, nil, 12)`: an octave up), `vol` 0–1. Returns the voice, or `nil` (no bank, a number that is not there) |
| `sfx(-1, [v])` | stops the effect of voice `v`, or all |
| `sfxpos(v)` | the effect playing on voice `v` and its step, or `nil` |
| `music(n, [fade_ms], [pos])` | plays song `n` from the start (or from position `pos` of its sequence), fading in over `fade_ms` |
| `music(-1, [fade_ms])` | stops the music, fading it |
| `music()` | while it plays: song, position, step, pattern (to keep in time); otherwise `nil` |
| `tempo(x)` | the music goes `x` times faster (1 = as written): speed up when the game gets hard |
| `mute(track, [on])` | turns off (`on`, the default) or back on a track of the music: layers that come and go |
| `volume([level])` | the general volume 0–10 (with `level` it changes it). It is the console's: it holds for every game and stays in `bm/config.txt` |

Track `t` of a pattern plays on voice `t`. While a sound effect uses a voice, the music's
track on that voice is silent. A bank with a song that uses tracks 0–5 leaves voices 6 and
7 free for the effects.

```lua
function _init()
  music(0)                         -- song 0, looping as decided in the editor
end
function _update()
  if btnp(4) then sfx(1) end       -- jump
  if got_coin then sfx(0) end
  if boss then tempo(1.2) end      -- faster
  if btnp(8) then music(-1, 500) end
end
```

#### Notes from the code

| Function | Description |
|---|---|
| `note(v, hz, [ms], [wave], [vol])` | plays a note on voice `v` (the envelope starts again); with `ms` it ends by itself, without it it stays on until `noteoff(v)`. `hz` in Hz or a name (`"C4"`). `vol` 0–255 (default 128) |
| `noteoff(v)` | releases the note (the release phase starts) |
| `freq(v, hz)` | changes the pitch without starting again |
| `slide(v, hz, [ms])` | the note slides to `hz` in `ms` (default 100): glissandos, sirens, lasers |
| `vibrato(v, [semitones], [hz])` | a vibrato `semitones` wide (for example 0.3) at `hz` swings a second (default 6); `vibrato(v)` takes it away |
| `arp(v, chord, [ms])` | the note runs through a chord, `ms` per note (default 50): `"major"`, `"minor"`, `"maj7"`, `"min7"`, `"7"`, `"sus2"`, `"sus4"`, `"dim"`, `"aug"`, `"power"`, `"octave"`, or a table of semitones (`{0, 4, 7, 12}`); `arp(v)` takes it away |
| `hz(note)` | the frequency of a note: a MIDI number (60 = middle C, 69 = A 440) or a name (`hz("A4")` = 440) |
| `envelope(v, a, d, s, r)` | the voice's envelope: attack, decay and release are times 0–255 (0 = instant, 255 = 2 s), sustain is a level 0–255. Default `1, 0, 255, 10` |
| `duty(v, d)` | the width of the square wave, 0–255 (128 = 50%; 32–64 sounds more "nasal") |
| `playing(v)` | `true` while the voice sounds (release included) or an effect / the music holds it |
| `apu(v, reg, [value])` | reads or writes a raw register of the voice (32 bytes per voice, 0–31: `src/audio/synth.h`; register 10 is the 1/256 of Hz, from 11 the tone: filter, place, room, echo, LFO) |

Waveform and volume stay those of the voice's last note, so giving them once is enough. At
the start and at the end of the cartridge the voices go off and back to their defaults.
Several loud voices together add up: a limiter keeps them under the top, but keep the
volumes around 100–130. Examples:

```lua
note(0, 880, 60, SQUARE, 100)          -- a 60 ms "blip"
note(0, "C5", 60, SQUARE, 100)         -- the same with the note's name
envelope(2, 0, 60, 0, 30)              -- an explosion that fades...
note(2, 2500, 300, NOISE, 130)         -- ...with noise
note(1, 1300, 300, SQUARE, 70)         -- laser: starts high...
slide(1, 300, 250)                     -- ...and goes down
note(3, "C4", 600, SQUARE, 90)         -- an arpeggiated major chord
arp(3, "major", 40)
```

Pong, Snake and Star Shooter in `carts/` use notes (a 10-line `jingle` function for the
tunes; now there is `lib.jingle` in bmlib); the Sound editor's demo project
(`carts/sound/demo.json`) has sound effects and three songs to listen to and copy (HIFI
uses the new voices).

#### Instruments, tone, room

| Function | Description |
|---|---|
| `tone(v, sound)` | the voice's tone for the `note()`s that follow: the name of a **ready-made instrument** (`"epiano"`, `"pluck"`, `"pad"`, `"kick"`… `instruments()` lists them) or a table in plain units (below). `tone(v)` goes back to the square of a fresh voice |
| `play([v], sound, [note], [ms], [vol])` | plays an instrument as the music plays it, with its pitch envelope and vibrato (the kick that falls, the laser): a name, a table as `tone()`'s (plus `pitch` semitones to start from, `ptime` ms to get there, `vib` cents, `vibhz`, `detune` cents) or a sound of the bank (a number; a name is first a sound of the bank with that name, then a ready-made instrument). `note`: a MIDI number or a name (`"C4"`, default 60), `ms` 0 = held until `noteoff`, `vol` 0–1; without `v` a free voice. Returns the voice |
| `instruments([kind])` | the ready-made instruments: `{ {name=, kind=, about=}, … }`; `kind`: `"drum"`, `"bass"`, `"keys"`, `"pad"`, `"pluck"`, `"lead"`, `"fx"` |
| `instrument(name)` | an instrument with all its values, as bm Sound keeps a sound (`wave`, `a`, `d`, `s`, `r`, `pitch`, `tone` = the tone's 21 bytes…), or `nil` |
| `reverb([size], [damp], [wet])` | the room the voices play in (0–1: a small room to a hall, bright to dull, how much is heard); returns the three. Each voice sends to it what its `reverb` says (a little by default: 0.16) |
| `echo([ms], [feedback], [wet])` | the ping-pong echo (left, right): the time between repeats (680 ms at most), how much comes back (0–0.95), how much is heard; returns the three. What goes in is each voice's `echo` |
| `retro([on])` | every voice **8-bit** as in the first versions (naive waves, straight envelopes, no room nor echo) while the game runs; returns whether they are (Settings can ask for it too) |
| `audio_time()` | the sound's clock in seconds (it moves with the samples played) |
| `play_at(t, sound, [note], [ms], [vol], [tag])` | a note at the time `t` of `audio_time()`: the sound's interrupt starts it, within 1.3 ms, whatever the frame rate. `sound` as `play()`'s, `ms` how long it is held (250 by default), `vol` 0–1, `tag` 1–255 (a group for `play_cancel`, 1 by default). The voice is chosen when it starts (a free one, a tail, the oldest note of `play_at`; never one of a song or of an effect). `true`, or `false` when the queue (160 notes) is full. riff uses it |
| `play_cancel([tag])` | forgets the notes of `play_at` waiting with that `tag` (none: all) and releases those that sound |
| `play_voices([v, …])` | the voices `play_at` may take (none: all 8); returns how many notes wait |

The keys of `tone()` (all optional; the missing ones stay as they are):

| Key | Value |
|---|---|
| `preset` | a ready-made instrument to start from |
| `wave` | `"square"`, `"triangle"`, `"saw"`, `"noise"`, `"sine"`, `"metal"`, `"fm"`, `"pluck"`, `"supersaw"`, `"organ"` |
| `vol`, `duty`, `sustain` | 0–1 |
| `attack`, `decay`, `release` | ms (up to 2000) |
| `cutoff` | the **filter**: Hz (0 = no filter) |
| `res`, `filter` | resonance 0–1; `"lp"` low pass, `"bp"` band pass, `"hp"` high pass, `"notch"` |
| `keytrack` | `true`: the cutoff follows the note (at middle C it is the one given) |
| `fenv`, `fdecay` | the filter's envelope: octaves that open it (negative too), ms to close again |
| `lfo`, `wah`, `pwm` | a slow wobble (Hz) that moves the filter (`wah` octaves) and the square's width (`pwm` 0–1) |
| `drive`, `noise` | soft saturation before the filter, white noise added (0–1) |
| `pan` | −1 left, 0 middle, 1 right |
| `reverb`, `echo` | how much the voice sends to the room and to the echo (0–1) |
| `ratio`, `depth`, `mdecay`, `feedback` | FM: the modulator's ratio (1, 2, 3.5…), depth (radians, 0–8), in how many ms it fades, feedback 0–1 |
| `bright`, `ring` | PLUCK: how bright the string is and how long it rings (0–1) |
| `spread` | SUPERSAW: how out of tune the three saws are (0–1) |
| `bars` | ORGAN: the four drawbars `{8, 6, 3, 2}` (0–15) |
| `raw` | `true`: this voice is 8-bit |

```lua
tone(0, "epiano")                                   -- an electric piano for note()
note(0, "E4", 400)
tone(1, { preset = "bass", cutoff = 300, res = 0.7 })   -- a darker, ringing bass
play(nil, "kick", "C2")                             -- the kick, on a free voice
play(nil, { wave = "noise", cutoff = 900, fenv = -2, fdecay = 300, decay = 400, sustain = 0 }, "C3")
reverb(0.8, 0.5, 1)                                 -- a cathedral (Yharnam)
echo(375, 0.4, 1)                                   -- an echo on the beat at 80 BPM
retro(true)                                         -- the 8-bit sound of before
```

**Ready-made instruments** (`src/audio/presets.c`): drums `kick`, `punch`, `snare`, `clap`,
`hat`, `openhat`, `tom`, `rim`, `crash`, `cowbell`, `shaker` (and `chipkick`, `chipsnr`,
`chiphat` in 8 bits); basses `bass`, `acid`, `sub`, `fmbass`, `pickbass`; keys `epiano`,
`organ`, `bell`, `marimba`, `glock`; pads `pad`, `strings`, `warm`, `glass`; strings
`pluck`, `guitar`, `harp`; leads `lead`, `sawlead`, `flute`, `brass`, `triangle`, `chip`, `chiptri`;
effects `laser`, `blip`, `boom`, `wind`. The same in bm Sound's *Instrument...* menu.

#### The bank: format and tools

The bank is the **AUDIO** section of the `.bm` (format in `src/audio/player.h`, version 2;
version 1 is still read): up to 32 sounds (instruments, each with its tone: filter, place,
room, echo, LFO), 64 sound effects, 64 patterns and 8 songs (each with the echo on the
beat, in steps, and the room's size). It is made with the
**Sound editor** (Dev tab), which opens a game and saves its sounds right inside it. On the
PC: `scripts/bmaudio.py unpack game.bm -o sounds.json` extracts it as readable JSON,
`mkbm.py --audio sounds.json` puts it back in a cartridge, `make wav BANK=sounds.json
SONG=0` plays it into a WAV.

### Keyboard and files (for tools such as the editors)

**Games and projects** (2026-10-06): a `.bm` (and a `.b16`) is a **game**: bm's tools read it
and take its code and its assets, but never change it. What they change is a **project**, a
`.bme`: the same container, with bit 0 of the u16 at offset 18 of the header set
(`src/bm/project.h`). A tool that saves a game asks first (*Save an editable copy: NAME.BME?*,
like the permissions) and writes the copy, next to the game (`NAME.BME`, or `NAME1.BME`… if
that is taken); a new `.bm` becomes a project (`"/carts/NEW.BM"` → `/carts/NEW.BME`). So
`cart_save`, `cart_write` and `cart_put_audio` return `true` **and the file written** when it is
not the one asked: the tool goes on with that one. A project makes its game with
`cart_build`. In the menu: *Make an editable copy* in a game's options (the project goes to
Dev, with a "Project" badge), *Build the game (.bm)* in a project's; A on a project opens it in
the SDK. These rules are for bm's tools; the other cartridges write only `.bm` and `.bme` files
in `/carts` that they made in the same run.

| Function | Description |
|---|---|
| `keyheld(name)` | `true` while key `name` of a keyboard is held: `"f1"`…`"f12"`, `"tab"`, `"space"`, `"enter"`, `"esc"` (bm Pixel: space held to draw) |
| `keyp()` | the next key typed: a character (`"a"`, `"\n"` Enter, `"\b"` Backspace, `"\t"`), a name (`"up"`, `"down"`, `"left"`, `"right"`, `"home"`, `"end"`, `"pgup"`, `"pgdn"`, `"del"`, `"esc"`, `"f1"`…`"f10"`), `"^s"` for Ctrl+S or `"^S"` for Ctrl+Shift+S (Ctrl+I and Ctrl+M too: `"^i"`, `"^m"`, not Tab and Enter); `nil` if none. F11 and F12 are the system's and never come. From the first call the keyboard types and no longer works as a gamepad for `btn()`, and Esc is a key like the others (Ctrl+Esc, Start+Select and PS close) |
| `keyhelp(list, [title])` | the cartridge's keys, shown under the system's while **F12** is held (2026-10-04): `list` is `{ {"keys", "what they do"}, "subtitle", … }`; keys as in `prompt()` (lower case the keyboard, upper case the pad), separated by spaces: `"ctrl s"`, `"shift w a s d"`, `"a / d"` (alternatives), `"1 - 5"` (a range), `"Y LEFTRIGHT"`. Returns how many entries name a key the system keeps for itself and the cartridge never gets (F11, F12, Ctrl+Esc, Ctrl+Shift+Esc: in red and in the log, they must go); the other system keys (Esc, Ctrl+S…) are listed when saying what they do there; `keyhelp(nil)` takes it away. Call it again when the page changes |
| `ls([folder])` | the files of the SD card: `{ {name=, size=, dir=}, … }` |
| `cart_load(path)` | opens a `.bm`: its sprite sheet (with the tiles' flags, the zones and their boxes), its map (with its layers) and its 3D models (with the skeletons) take the place of those of the calling cartridge; returns `{title, author, res, lua, sheet_w, sheet_h, map_w, map_h, layers, [palette]}`; `layers` the names of the map's layers; `palette` the colours (0xRRGGBB) of the SHEET8 section's palette, in their order, if the sheet is saved that way |
| `cart_sheet([w, h])` | the width and height of the project's sprite sheet; with `w` and `h` (multiples of 8, from 8 to 4096) it gets that size: the pixels that fit stay where they are, the new ones are transparent (bm Pixel); the flags stay with their tile |
| `cart_new()` | an empty sprite sheet and map (256×256, one layer), no flags, zones, boxes or models |
| `cart_save(path, {title, author, res, lua})` | writes a `.bm` with the code given and the current sprite sheet, tile flags, zones with their boxes, map with its layers, cover, sound bank, models and skeletons (the other sections of the file opened stay as they were); an 8.3 name, e.g. `"/carts/GAME.BME"`. `true` (and the file written when it is not `path`: a game's editable copy, see above), or `false` and a message |
| `cart_build(project)` | bm's tools: builds the **game** of a project (`.bme`): the same bytes, marked as a game that keeps the project's name (offset 104), in `.BM` next to it; the game it built before is replaced, a game of the same name it did not build stays and the new one takes a digit (`PONG1.BM`). `true` and the game's path, or `false` and a message. The SDK: Ctrl+B, *Build the game .bm* |
| `cart_read(path)` | the code and the header of a `.bm`: `{title, author, res, lua, size}`, **without** touching the caller's sheet and map (unlike `cart_load`): for editors with several files open |
| `cart_write(path, {[lua, title, author, res, from, sections]})` | changes **only** the code (and the data fields) of a `.bm`: sprite sheet, map, cover, sound bank and the sections the kernel does not know stay as they were; a file with a long name keeps it. Without `lua` the code stays as it is. A file that is not there becomes a cartridge with only the code (an 8.3 name). `from`: the other sections come from another file ("save as"); `from = false`: a new cartridge, whatever the file holds (bm Studio's new project). `sections`: `{[8] = MESH bytes, [9] = ANIM bytes}` (`false` takes them away), checked first (`false, "broken MESH section"`): so bm Mesh writes the models. `sheet = true`: the project's sprite sheet (`cart_load`, `sset`, `cart_sheet`) takes the place of the file's, as **SHEET8** when it has at most 256 colours (otherwise SHEET), with the colours of `palette` (`{0xRRGGBB, …}`) first in its palette, as they are and in that order (the transparent entry of the palette of before stays in its place); a pixel that still has the RGB565 it had in the file keeps its 24 bits of before (the console keeps 16 bits per pixel): only the pixels drawn again change. So bm Pixel saves the sheet. If `fset` changed some flags meanwhile, those go into the file too. Returns like `cart_save` |
| `cart_meshes(path)` | the meshes the **code** of a `.bm` builds with `mesh()`, `mesh_sphere()` and `mesh_cube()` (also through bmlib's builder): `{ {name=, kind=, verts={x,y,z,…}, faces={a,b,c,colour,…}, [uv={…}]}, … }` (the arguments of `mesh()`, indices from 1, colour `-1` = texture) and `nil` or the code's first error; `nil` and a message if the file cannot be read. The code runs **apart** (a Lua state of its own, `src/bm/meshcap.c`): the file's body, then `_init`, `_update` and `_draw` once, with an instruction limit; bm's other functions do nothing (no files, screen or sound). The name is that of the variable holding the mesh (`M.ship` → `"ship"`; in an array `chef[2].body` → `"chef2_body"`). For bm Mesh |
| `mesh_reduce(record, triangles, [bones, [max_err]])` | **fewer triangles** for a model of the MESH section (`src/bm/decimate.c`: edge collapse with Garland-Heckbert quadrics, no AI): `record` is the model's part of the section (as `encode_mesh` of `bm3d.lua` writes it), `bones` the bone of each vertex (a byte each, from the ANIM section), `max_err` stops before a costlier collapse (0: no limit). Returns the record with at most `triangles` triangles (more only if it cannot go further without flipping faces), the bones of its vertices (`nil` without `bones`) and the number of triangles; `nil` and a message if the record is broken. Borders, colour lines and texture seams stay in place; the vertices stay the model's. bm Studio's models page (**-**); on the PC `tools/bmreduce.py` |
| `picture3d(action, ...)` | a **picture becomes a 3D model** through an image-to-3D service (`src/net/img3d.c`; the first is Meshy, key `meshy_key=...` in `bm/config.txt` on the SD card). `picture3d("providers")` the services; `picture3d("ready", service)` `true`, or `false` and why (the key is missing); `picture3d("start", picture, {provider=, polycount=})` starts the job on a `.png`/`.jpg` of the SD card or an https URL and gives the job's id (or `nil` and a message); `picture3d("status", id, service)` → `"running", progress` or `"done", url` of the `.glb` (or `nil` and a message); `picture3d("take", url, {name=, faces=, height=})` downloads the `.glb` and converts it (`src/bm/glb.c`: positions merged, faces clockwise, PNG or JPEG texture reduced to 256×256, the model framed `height` high, reduced to `faces` triangles) → `{record=, flat=, texture=, nv=, nf=, textured=}`: `record` is the model for the MESH section (with the texture, if any), `flat` the same with the colours taken from the texture, `texture` 256×256 RGBA. The calls block while the network works. bm Studio's models page (**m**) |
| `cutout3d(picture, {name=, lathe=, height=, depth=, segments=, faces=})` | a model from the **picture's outline**, made on the console without network or AI (`src/bm/cutout.c`): the transparent background (or the corners' colour) goes, the outline becomes a simplified polygon and the polygon a solid: a **cutout** `depth` thick (a fraction of the height, 0.2: the picture in front, mirrored behind, the border's colours on the sides) or, with `lathe = true`, the half outline **turned** round the vertical axis in `segments` steps (vases, towers, rockets; the picture projected in front). `picture`: a `.png` or `.jpg` of the SD card; `height` in blocks (2); `faces` triangles at most (1200). Gives the same table as `picture3d("take")`, or `nil` and a message. bm Studio's models page (**m**, the first two entries) |
| `cart_audio([path])` | the sound bank of a `.bm` as a string (`false` if it has none) and its title; without a path, the bank of the running cartridge |
| `cart_put_audio(path, bank, [title, lua])` | puts the bank (a string; `nil` takes it away) into a `.bm`, the rest of the file as before; if the file is not there it creates it with that title and that code. `true` (and the file written, as `cart_save`), or `false` and a message |
| `audio_bank(bank)` | from now on this bank plays (for the editors: music and effects playing go on); `nil`: none |
| `audio_pattern(p, bpm, swing)` / `audio_play(v, sound, note, [vol], [fx], [ms])` | a pattern in a loop, a sound of the bank on a voice (the editors' previews) |
| `cart_run(path, [options])` | leaves, plays that file and then opens again the cartridge that asked, with `cart_arg()` = `{path=, error=, back=true, run=}` (from the menu, "Open in the SDK", "... Sound editor", "... bm Studio", "... bm Animator", "... bm Mesh" or "... bm Pixel": `back=false`). `run` (2026-10-04, the dev kit) are the numbers of the run tried: `{frames, secs, fps, ms, ms_max, slow, lua_kb, lua_peak_kb, data_kb, instr_max, tokens, tris, gpu}` (average and top ms of `_update` + `_draw`, `slow` the frames over 16.7 ms, Lua's memory at the end and at its top, the data's, the instructions of the busiest frame, the tokens, the triangles of the last frame, whether the GPU did the 3D); the SDK shows them in its dev kit |
| `cart_run(path, {breaks = {lines}, stop = true})` | the game tried is a session of the **debugger** (R13, bm Code: F8 and F5): it stops at the lines `breaks` of its `main.lua`, at `breakpoint()` and, with `stop`, at its first line. Stopped, the debugger covers the screen: the code round the line, the function's variables (the locals, then the upvalues in light blue), the stack; F10 or A the next line (over the calls), F8 or X into the call, Shift+F8 or Y out of the function, F5 or Start goes on, Esc or Select stops the game (which ends with `main.lua:N: stopped in the debugger`: the tool comes back on the line); the arrows scroll the variables and choose the function of the stack. Every game tried from a tool with `cart_run` is a session: without lines it costs nothing until it reaches `breakpoint()` |
| `breakpoint([why])` | in a game tried from a tool (`cart_run`) the debugger stops here, with the reason in its title; in the other games it does nothing |
| `cart_tool(name, [path])` | leaves and opens another tool of the console on the same file: `"studio"`, `"animator"`, `"mesh"`, `"pixel"`, `"code"`, `"sdk"`, `"sound"` (bm Studio → *Open in bm Animator*, and back); the tool finds it in `cart_arg()` as from the menu, with `from` = the name of the tool that opened it (`"sdk"`: the suite's menus offer *Back to bm SDK*) |
| `cart_config(key, [value])` | the text of a key of `bm/config.txt`, or `nil`: the **flags** a game takes from the monitor line (`set overbit_bench=auto ; play overbit`) or from `easy_install.sh` (`bench`, `config`). Only the keys that start with the first word of the game's title in lower case and `_` (Overbit: `overbit_`); anything else is an error. With a value the key is changed and saved (`""` clears it: a flag read once). Values up to 127 characters, without `;` or `,` (they split the monitor line and the host's config): Overbit's flags are `key:value` apart by `/`, lists by `+` |
| `cart_data(kind, [bytes])` | the project's **MESH** (`kind` 8) and **ANIM** (9) sections, as strings in the format of `src/bm/bm.h`: without `bytes` it returns them (`nil` if there are none), with `bytes` it replaces them (`nil` or `""` takes them away) → `true`, or `false` and the reason. The kernel checks them first; `model()`, `animate()` and `bone3d()` use the new ones at once and `cart_save` writes them. So bm Studio and bm Animator of the console change models and skeletons (with `string.pack` / `string.unpack`, in the library `require "bm3d"`) |

### Assistant (M30, for the development tools)

A small AI that runs on the console: it understands a question (Italian or English, also
with typos) and answers with the entries of its knowledge base (every function of the API,
code examples for games, Lua errors, tips) or draws the base of a sprite. It is not a
chatbot: a tiny INT8 network chooses among the entries it knows, in less than a
millisecond. It does nothing until you call it (no background process; knowledge base and
network are in the kernel). Try it from **Dev > Assistant** (or `I` from the monitor).

| Function | Description |
|---|---|
| `ai.ask(question, [{n=5, ctx=word, kinds="api,howto"}])` | the best entries, the first the most likely: `{ {id=, title=, kind=, score=}, … }`, and as the second value the microseconds taken. Titles and texts are in the language of the answers (`ai.lang()`), which follows the question's. `ctx`: the word under the cursor (if it is a function of the API, its entry goes to the top). `kinds`: `api`, `howto`, `error`, `tip`, `sprite` |
| `ai.entry(id)` | an entry: `{id, kind, title, name, text, code, gen, see = {id, …}}` |
| `ai.list([kinds])` | all the entries `{id, title, kind}` (to browse them with the pad) |
| `ai.lang([language])` | the language of the answers (R18): `"it"` or `"en"`, and whether it follows the questions (`true`); `"it"`/`"en"` fix it, `"auto"` makes it follow the language of each question again. It starts from `assist_lang` in `bm/config.txt` (`it`, `en`, `auto`: the default, from the language used last). In the panel Ctrl+E changes it; the language is on the title bar |
| `ai.near(word)` | the name of the API closest to a misspelt word (`"sprr"` → `"spr"`, 1), or `nil` |
| `ai.sprite(request, [{gen=, size=16, seed=1, outline=true, palette={…}}])` | the base of a sprite: `{w, h, gen, name, seed, px = {0xRRGGBB or -1 (transparent), …}}` row by row. The recipe comes from the words (`"slime"`, `"spaceship"`, `"coin"`, `"grass"`…) or from `gen`; the colours (`"red"`, `"blu"`…) and the size (`"8x8"`, `"32x32"`, `"small"`, `"big"`) from the words; another `seed` is a variant; with `palette` each pixel becomes the closest colour of the palette |
| `ai.recipes()` | the sprites' recipes `{id, name}`; `ai.recipes("mesh")` the 3D ones `{id, name, rigged}` |
| `ai.script(text)` | a model written in the **parts language** (`src/ai/mesh_script.c`: `mat`, `box`, `bx`, `tube`, `cyl`, `ell`, `prism`, `wedge`, `tf`, `bone`, `use`, `side`, `mirror`, `clip`, `key`, `turn`, `shift`, one per line): the same table as `ai.mesh`, or `nil` and the error (`"line 3: ..."`). It is the format `tools/img2mesh.py` gets from the vision model for a picture |
| `ai.music(request, [{gen=, seed=1, key=, minor=, bpm=, bars=, inst=, context=}])` | **music** for a sound bank (`src/ai/music.c`): rhythms, backing tracks (drums, bass and chords), bass lines, arpeggios, melodies and classic sound effects, 87 recipes. The words choose the recipe (`"rock beat"`, `"lofi backing"`, `"sad melody"`, `"coin sound"`), the key (`"in A minor"`, `"la minore"`), the tempo (`"120 bpm"`, `"fast"`), the length (`"8 bars"`) and the instrument (`"with the piano"`, `"8 bit"`); another `seed` is a variant. The melodies are written by a small network trained on traditional tunes and tunes written for bm. `context = {notes = {…}, bars = {…}}`: the notes already there (MIDI) and the bar of each: melodies, arpeggios and bass lines take their key and chords. Returns `{gen, name, kind, bpm, swing, key, minor, meter, bars, echo, room, seed, chords = {"Am", …}, instruments = {"kick", …}, patterns = { {len, tracks = {[0..7] = {step, …}}} }, sfx = {ms, loop = {a, b}, steps = {step, …}}}`; a step is `note \| instrument << 8 \| vol << 16 \| fx << 24` (instrument: 0-based index into `instruments`). On the RGB30 too, where the words choose the recipe without the network. `ai.music_recipes()` lists them |
| `ai.mesh(request, [{gen=, seed=1, scale=1, rig=true}])` | the base of a **3D model** for bm Studio and bm Animator: `{gen, name, seed, faces = { {p = {{x,y,z}, …}, c = 0xRRGGBB, b = {bone, …}}, … }, bones = { {name, parent, head, tail}, … } or nil, clips = { {name, loop, length, mode, keys = { {t, pose = { {q, t}, … }}, … }}, … }}`, the same tables as `bm3d.lua` (one unit = one block of bm Studio, the model looks toward −z and stands on y = 0). The recipe (53: shapes, objects, people, animals, machines) comes from the words (`"house"`, `"tree"`, `"mech"`…) or from `gen`; the colours (`"red"`, `"blue"`), the size (`"small"`, `"big"`, `"huge"`), the proportions (`"tall"`, `"short"`, `"wide"`, `"thin"`) and `"no skeleton"` from the words; another `seed` is a variant. People, animals and machines have a skeleton (every corner on a bone) and animations (`idle`, `walk`, `fly`, `attack`…) |
| `ai.checksum(question)` | the CRC-32 of the network's outputs for a question: for the tests (equal to that of the Python reference) |

**Small networks for the games** (M38.4: Overbit's bots). A network of dense layers with
INT8 weights and activations (the same maths as the assistant, with the ARMv6's SIMD
instructions), from a string a training script writes (`scripts/nnetlib.py`: it quantises a
numpy network, packs it and gives the console's exact numbers for the tests):

| Function | What it does |
|---|---|
| `nnet(blob)` | the network of a "BMNN" string (format in `src/ai/net.h`; an error if it is broken), up to 8 layers of 256 |
| `net:run(inputs, [outputs])` | the outputs (numbers) for a table of inputs; `outputs`: a table to fill instead of a new one |
| `k, v = net:pick(inputs, [mask])` | the index (from 1) of the biggest output and its value; `mask`: a table of booleans, `false` = that output is not chosen |
| `n_in, n_out = net:size()` | how many inputs and outputs |

**The panel** (`require "assist"`): what the tools open with a key (F6 in the Assistant).
It answers while you type; Enter (A) passes the code or the sprite to the tool, Esc (B)
closes, Tab (X) changes mode; without a question you browse everything with the pad. While
a word of the question is typed the rest of the most likely one appears in blue-grey and Tab
writes it (the completion, below).

```lua
local assist = require "assist"

function _update()
  if assist.update() then return end          -- open: the keys are its own
  local k = keyp()
  if k == "f6" then
    assist.open{ mode = "code", ctx = word_under_cursor,
                 on_insert = function(code) insert_lines(code) end }
  end
end

function _draw()
  draw_tool()
  assist.draw()                                -- on top, if open
end
```

`assist.open{...}`: `mode` = `"code"` (API, examples, errors), `"sprite"`, `"mesh"` (the 3D
recipes: the model turns in the panel, `on_mesh(m)` gets it; it is bm Studio's and bm
Animator's mode with F6), `"error"` or `"any"`; `query` (a question already written), `ctx`,
`error` (an error message: the panel shows the line, the misspelt name and what it means),
`size` and `palette` for the sprites, `on_insert(code)`, `on_sprite(sprite)`, `on_close()`,
`x, y, w, h` (default: almost the whole screen). Then `assist.update()` and `assist.draw()`
in every frame, `assist.is_open()`, `assist.close()`.

**Actions on the code** (`assist.act(request, lines, n)`): what a line `#entry: request #`
at line `n` of `lines` (a table of strings) asks. The network chooses among the actions
(ternary operator, comment, log, remove the logs, nil check, make local, indentation,
rename, comment/uncomment, optimise, explain), an example to insert or a sprite written as
code; it works on the function round the line or just below it. Returns `{lines, ok,
message, cursor, explain}` (the new lines, without the `#entry:` line), and changes nothing
if it is not sure enough. bm Code uses it with Enter on those lines.

The knowledge base is in `src/ai/kb/` (format and how to train again:
`src/ai/kb/README.md`).

**Word completion** (`require "predict"`, guide in [PREDICT.md](PREDICT.md)):
`predict.complete(text_before_the_cursor, {lang = "lua"})` → `nil` or
`{prefix, word, rest, ending, list}`: the most likely word that starts as the one written
(`rest` is what is missing, to show in `predict.C_GHOST`; Tab replaces `prefix` with
`word`). `lang`: `"it"`, `"en"`, `"lua"`, `"ask"` (the questions to the assistant), a mix
with weights (`{it = 1, ask = 2}`) or `"none"`; `words` the code's names
(`predict.count_words(lines)`), weighted by `words_weight`. The dictionaries are read at the
first word, or one piece per frame with `predict.preload({"lua", "it"})`.

**Typing with the pad** (`require "padtype"`, guide in [PADTYPE.md](PADTYPE.md)): quick
composing (the cross writes consonants, the prediction finishes the syllable, □ and △ turn
it, a press waits for its double, L2 / R2 / L2+R2 more levels, R2 + ✕ □ △ the words) and
the on-screen keyboard; Share goes from one to the other. `pt.update(host)` every frame
reads `pad()` and changes the text through the host (`before`, `insert`, `erase`,
`newline`, `move`, `lang`; `pt.text_host(lang)` makes one on a string), `pt.draw(x, y)`
draws the controller's overlay (`pt.size()`), `pt.coach(host, text)` gives the next button
to write a text. bm Code and the Pad Typing cartridge use it.

### nano8 (the `n8` library)

Every cartridge also sees the `n8` table: the **nano8** machine (`src/bm/n8*.c`), with the
functions of the `.p8` cartridges (`n8.spr`, `n8.map`, `n8.print`, `n8.peek`…, with their
arguments and numbers as they want them) and those to load, start and show them
(`n8.load`, `n8.power`, `n8.buttons`, `n8.blit`, `n8.preview`, `n8.compile`). It is made
for `carts/nano8` (see the comment in `src/bm/n8lua.c`); a `.bm` game does not need it.

### Light

Dark scenes lit only by lamps, candles, torches: the frame's drawing is multiplied by a
"light map" computed in C (a grid every 4 pixels, interpolated and slightly dithered). From
the first `light_begin()` the cartridge draws in RAM instead of directly on the screen.

| Function | Description |
|---|---|
| `light_begin([ambient])` | starts the frame's lights: `ambient` is the colour of the background light (`0x000000` pitch dark, `0xFFFFFF` no effect) |
| `light(x, y, radius, colour, [intensity])` | a soft light in world coordinates (`camera` holds); more lights add up, up to 2× the brightness |
| `light_end()` | applies the light to everything drawn; what you draw after (HUD, text) stays at full light |

```lua
cls(0); map(...); spr(...)                  -- the scene
light_begin(0x0A0A16)                        -- dark blue night
light(lx, ly, 50 * (0.95 + math.random() * 0.1), 0xFFB060)   -- a flickering street lamp
light(px, py, 40, 0xFFC888, 0.9)             -- the player's lantern
light_end()
print("life", 4, 4, 0xFFFFFF)                -- the HUD is not darkened
```

Complete example: `carts/hunt` (Hunter's Night).

### Light by levels (as in Dank Tomb)

The other light, that of the PICO-8 game *Dank Tomb*: each pixel has a **light level** (0
the darkest) and its colour becomes the one a **fade table** gives at that level. Lamps
make concentric rings of levels, from their level in the middle down to 0 at the edge; the
rings' edges are mixed with a 4×4 ordered dither; where two lamps meet the stronger wins.
The tables are chosen by the cartridge: shadows can turn night blue and colours near the
lamps orange, staying on the colours of its own palette. All in C (`g16_fade_*` in
`src/bm/gfx16.c`).

| Function | Description |
|---|---|
| `fades(tables)` | the tables: `{ {colour, l0, l1, ...}, ... }`, for each colour of the palette what it becomes at level 0 (the darkest), 1, ...; all rows have the same number of levels (2–16, up to 255 colours). The colours without a table are scaled as the average of the tables. Returns the number of levels |
| `dark_begin([ambient])` | starts the frame: every pixel at level `ambient` (default 0); from here the cartridge draws in RAM |
| `glow(x, y, radius, level, [dither])` | a lamp in world coordinates (`camera` holds): `level` in the middle, 0 at `radius`; `dither` 0–1 (default 0.5) is how much the rings' edges mix (0 sharp rings, 1 a continuous dithered shade) |
| `dark_end()` | applies the levels to everything drawn; what you draw after (flames, sparks, HUD) stays as it is and "shines" |

```lua
fades(TABLES)                                -- once, in _init
cls(0); map(...); spr(...)                   -- the scene at full light
dark_begin(1)                                -- night: level 1 everywhere
glow(lx, ly, 72 + math.random(2), 6)         -- a street lamp
glow(px, py, 28, 3)                          -- the little light round the player
dark_end()
spr(FLAME, fx, fy)                           -- the flames are not darkened
```

Complete example: `carts/yharnam`; a small one: `SQUARE_CART` in `tests/qemu_test.py` (a lamp on a 256×256 cartridge).

### 3D (software)

| Function | Description |
|---|---|
| `mesh(v, f, [uv])` | a mesh from tables (up to 65535 vertices and 65535 faces: 4096 and 16384 before bm3d 5.0): `v` = {x,y,z, x,y,z, …}, `f` = {a,b,c,colour, …} (indices from 1; a face shows from the side where its vertices appear **clockwise**). With `uv` (6 numbers per face: u,v of the three vertices in sprite sheet pixels) the faces with colour `-1` have the sprite sheet's **texture** (perspective correct, transparent pixels stay empty). The colour can have the **material bits** (table below) |
| `mesh_sphere([r, segments, c1, c2])`, `mesh_cube([c])` | ready-made meshes |
| `model(name)` / `model(n)` | a **3D model of the cartridge** (made with [bm Studio](../sdk/README.md), MESH section) as a mesh, with the sprite sheet's texture; `n` counts from 1; `nil` if it is not there. Every call builds a new mesh: do it in `_init`. A model with **baked light** (MESH's "lit" bit, `src/bm/bm.h`: the light of each corner of the faces, made by a script, as Overbit's map) is drawn smooth with that light, without sun or sky but with the lamps (`lamp3d`) and the fog: it costs less than computed light; its textured faces (windows, signs) have one light per face, coloured, and the fog |
| `models()` | the names of the cartridge's models, in order (`{}` if it has none) |
| `bounds3d(m)` | `x0, y0, z0, x1, y1, z1`: the box round a mesh's vertices, in its coordinates (before moving, turning and scaling it with `draw3d`): to centre it, for collisions |
| `animate(m, [anim, t, anim2, t2, k, bone])` | **skeletal animation**: a model with a skeleton from [bm Animator](../sdk/README.md#bm-animator) takes the pose of animation `anim` (name or number) at time `t` in seconds (looping, if the animation loops); with `anim2, t2` it blends two animations (`k` from 0, the first only, to 1, the second only: to go from one to the other); with `bone` the second holds only for that bone and those under it (a torso that shoots on running legs); without an animation the rest pose. Returns the animation's length. An error if the mesh has no skeleton or the animation is not there. The bones move the vertices while the mesh is drawn (rigid skinning): `animate` costs only the bones |
| `bone_turn(m, bone, [rx, ry, rz])` | from now on every `animate` also turns the bone by these angles (radians, x then y then z, in the parent's frame) over the animation: aiming up and down, legs that follow the walking direction. `bone_turn(m, bone)` takes it away |
| `bones3d(m)` | the names of the skeleton's bones, in order |
| `hit3d(m, x, y, z, ry, scale, ox, oy, oz, dx, dy, dz, [maxd])` | `t, bone`: the ray from `o` along `d` against the mesh's bones in the last pose, drawn at (x, y, z) turned by `ry` and scaled; each bone is a capsule from head to tail as wide as its vertices. The closest hit within `maxd` (`t` in units of `d`), or `nil`: hitboxes that follow the animation (headshot: bone `"head"`) |
| `clips(m)` | a model's animations: `{ {name=, length=, loop=}, ... }` (`{}` without a skeleton) |
| `bone3d(m, bone)` | `x, y, z, cx, cy, cz`: where a bone's head and tail (name or number) are in the last pose, in the model's coordinates (as `bounds3d`); `nil` if the bone is not there. To attach objects to the hands (the head), a sword's tip (the tail), lights, effects |
| `draw3d(m, x, y, z, [rx, ry, rz, scale, flags])` | draws a mesh with z-buffer and per-face light. `flags`: 1 = no z-buffer (neither test nor write: floors and backgrounds drawn first, faster), 2 = no light (flat colours), 4 = **smooth** (Gouraud: light computed at the vertices and shaded over the face, dithered; faces sharing the same vertex indices look like a curved surface, for sharp edges use separate vertices or the "flat" bit), 8 = the mesh's **shadow** on the plane `y` of the point (along the sun: it darkens what is already there, the mesh is not drawn), 16, 32, 48 = **level of detail** 2, 1, 0 (only that level's faces, see the material bits; without: 3, all), 64 = **in front** (first-person weapons and arms: the z-buffer under it is cleared, precise depths from 0.1 units); they add up |
| `sky3d(sun, sky, ground)` | colours (0xRRGGBB) of the sun's light and of the ambient light coming from above and below (faces turned up take the sky, those turned down the ground); `sky3d()` goes back to white |
| `shine3d(spec, exponent, rim)` | the sun's reflections on glossy faces (`spec` 0–2, `exponent` 4–64: higher, smaller) and light on the shapes' rim (`rim` 0–1) |
| `shadow3d(style)` | the shadows of `draw3d` with flag 8: 0 darken (default), 1 dithered black (without reading the screen) |
| `point3d(x, y, z, radius, colour, [flags])` | a round point of that radius in the world, behind the closer things (it does not write the z-buffer): particles, sparks, bullets. `flags` 1 = every other pixel. Returns the pixels |
| `line3d(x0, y0, z0, x1, y1, z1, colour, [width, flags])` | a 3D line, cut by the near plane and hidden by the closer things: tracers, beams |
| `sprite3d(sx, sy, sw, sh, x, y, z, width, [flags])` | a rectangle of the sprite sheet facing the camera, `width` units wide in the world, hidden by the closer things: explosions, smoke, icons over the characters |
| `camera3d(x, y, z, [yaw, pitch, fov, roll])` | the camera (default at z = −5, fov 60°); `roll` tilts the view (radians) |
| `light3d(x, y, z, [ambient])` | the light's direction and the ambient light (0–1) |
| `zclear()` | clears the z-buffer (every frame, before `draw3d`) |
| `gpu3d([on, aa, vs, queue])` | `on, aa, vs, version, queue`: whether the GPU draws the 3D, whether with 4× MSAA and whether the GPU's vertex shader places the models' vertices (M36): `vs` is `false`, `1` (the scenery: models without light or with light at the corners) or `2` (all, also those lit by the sun and with bones; `true` is 2); `queue` (M35): whether the GPU's job starts without waiting for it and the next frame's `_update` runs meanwhile (where the boot test saw it work); the 2D drawn after the 3D (the HUD) and that of the `_update` are recorded and go on the page when the GPU is done, in the same order (the result is the same; an `_update` that draws 3D or reads the screen with `pget` goes back to running after the frame). With arguments it changes it for this cartridge (a "3D: GPU / GPU + VS / GPU + AA / ARM" menu in the game); `on` stays `false` if there is no GPU (QEMU, `gpu3d=0`) and in 256×256 games (the GPU writes whole pages), `aa` if MSAA cannot be used and `vs` if the boot test did not see the vertex shader (and its clipping) work. `version` is the version of the 3D drivers this choice reproduces (`"0.2"` the ARM, `"2.1"`, `"3.0"`, `"3.4"`, `"4.1"`: `docs/DRIVERS.md`). At the end it goes back to the settings' |
| `fog3d(colour, near, far)` | fog: the faces fade into the colour between the two distances; `fog3d()` takes it away |
| `lamp3d(i, x, y, z, radius, [k, colour])` | point light `i` (1–4): the faces with their centre within `radius` get brighter, up to `k` more (default 1) in the middle, of the `colour` given (white if missing); `lamp3d(i)` turns it off, `lamp3d()` all. With `light3d` at a low ambient it makes dark scenes with lanterns |
| `project3d(x, y, z)` | a point of the world → `sx, sy, depth` on the screen (`nil` if it is behind the camera): to draw in 2D things lined up with the 3D (horizon, sights, labels) |
| `visible3d(x, y, z, r)` | `false` if a sphere (centre, radius) cannot be seen: behind the camera, beyond an edge of the screen, or (after `pvs3d`) on pieces of the map that the camera's cell does not see. To skip the characters and effects hidden by the walls before drawing them (bm3d 5.5) |
| `pvs3d(t)` | the map's precomputed visibility for `visible3d`: `t = { x0, z0, cell, nx, nz, max_y, sets, boxes }`, a grid of `nx`×`nz` cells of side `cell` on the ground from (`x0`, `z0`); `sets` a string a cell (row by row along x), each byte the number (from 1) of a piece seen from it; `boxes` six numbers a piece (`x0 y0 z0 x1 y1 z1`). With the camera above `max_y` or out of the grid only the view counts; a sphere off every piece is seen. `pvs3d()` forgets it |

**Material bits** in a face's colour (of `mesh()` and of the models; 0 = the usual face):

| Bit | Value | Effect |
|---|---|---|
| 30 | `0x40000000` | **emissive**: a flat colour, without light (lights, screens, energy) |
| 29 | `0x20000000` | **glossy**: the sun's reflection (`shine3d`) |
| 28 | `0x10000000` | **screen door**: every other pixel, what is behind shows (shields, glass) |
| 27 | `0x08000000` | **flat**: also in a smooth drawing (flag 4) it takes the light of its plane; sharp edges can share vertices |
| 24–26 | | **level of detail**: bits 24–25 a level `k` (0–3); bit 26 at 0 the face shows from `k` up (a detail), at 1 below `k` (a simple version) |

```lua
local SHIELD = 0x40C8FF | 0x40000000 | 0x10000000   -- light blue, emissive, screen door
local DETAIL = 0xFFFFFF | 0x02000000                 -- white, only from detail 2 up
```

### Collision worlds

Solid boxes, rays and bodies that move sliding on the walls (in C: much faster than in
Lua). Complete example: `carts/overbit`. For the 2D map there are the tiles' flags
(`mflags`) and bmlib's `lib.move` / `lib.step`.

| Function | Description |
|---|---|
| `world3d()` | an empty collision world |
| `world_box(w, x0, y0, z0, x1, y1, z1, [tag])` | a solid box; returns its number |
| `world_ray(w, ox, oy, oz, dx, dy, dz, [maxd, ground])` | `t, nx, ny, nz, box`: the first point hit along the ray (the face's normal; box 0 = the ground `y = 0`, which counts unless `ground` is `false`), or `nil` |
| `world_move(w, x, y, z, r, h, dx, dy, dz, [step, on_ground])` | `x, y, z, flags`: a body (feet at `x, y, z`, radius `r`, height `h`) moved by `d`, sliding on the walls and climbing steps up to `step` (0.45) if it was on the ground. `flags`: 1 on the ground, 2 a wall (4 along x, 8 along z), 16 a ceiling |
| `world_floor(w, x, z, y, r, [step])` | the floor's height under `(x, z)` |

Triangles that cross the plane near the camera are cut, not dropped: floors and big objects
stay whole even when they pass by the camera.

**3D on the GPU (M33).** The Pi's GPU (V3D) draws the triangles; with *Settings > Graphics
> 3D of the games* on `ARM` (`gpu3d=0` in `bm/config.txt`), and in QEMU, the ARM draws
them. The same functions, no change in the cartridges. The ARM goes on transforming,
lighting and clipping; the GPU fills the pixels with a 24-bit z-buffer, shading without
dithering and textures with the nearest texel. The waiting 3D is drawn before every 2D
drawing that follows it, before `pget`, `sset` and at the end of the frame. If a cartridge
draws more 3D after the 2D in the same frame, from the next frame the GPU keeps the
z-buffer between the two parts (about 1 MB of memory written and read again per frame; not
the first frame): it is still better to draw all the 3D first and then the HUD. The GPU's
z-buffer starts from zero every frame, even without `zclear()`. `stat(9)` is 1 when the GPU
does the 3D. If the GPU does not answer, the kernel goes back to the ARM by itself and
writes it in the log. The GPU also draws the shadows (`draw3d` with flag 8: always dithered
black, `shadow3d`'s style 1, over the things already drawn thanks to the z-buffer), the 3D
effects (`point3d`, `line3d`, `sprite3d`), the screen-door faces and the textures of the
models with baked light (light and fog shaded at the corners, also with the `lamp3d`s).
Only the faces **textured and screen door** together it cannot do: at the first one the
cartridge goes to the ARM for the rest of the run (a line in the log; the mixed frame is not
shown). A frame that starts with `cls()` costs the GPU less: the tiles start from the colour
of `cls` instead of reading the page again. Textures with sides that are multiples of 32
(128×128 sprite sheets, 256×256, …) go to the GPU in T-format, the tiled format of its
cache, faster to read.

**Anti-aliasing (M34).** *Settings > Graphics > 3D anti-aliasing: 4x* (`gpu3d_aa=1` in
`bm/config.txt`) makes the GPU draw the 3D with 4× MSAA: four samples per pixel, the
average at the end of the tile, triangles' edges without steps. Nothing changes in the
cartridges. It holds only where the GPU bears it (the boot test says *4x: not on this GPU*
otherwise) and only in the jobs without a kept z-buffer: cartridges that draw 3D, then 2D,
then more 3D in the same frame stay without anti-aliasing (the 4-sample z-buffer cannot be
saved). If the test finds the GPU cannot load the page again into the 4 samples, MSAA is
used only in the frames that start with `cls()`. On the ARM there is no anti-aliasing.
Complete example: `carts/astrowing` (Star Fox-style flight: models built in code, horizon
with `project3d`, fog, explosions, boss). With bm Studio's models: `carts/village`
(`model()` for each model, terrain drawn without z-buffer, night with `lamp3d` and
`fog3d`; bm Animator's villager with `animate()`, two animations blended, a light in hand
with `bone3d()`, and its version as pre-rendered sprites).

## bmlib: the games' shared library

`local lib = require "bmlib"` (R10, 2026-10-04): what every game wrote again by itself
(found in 5–10 cartridges of the repository) in a single library, in Lua, in the kernel
(`src/script/bmlib.lua`). It does not touch the global variables and hides no function of
the console. Units: **times in seconds**, positions in pixels, speeds in **pixels per
frame**. Objects (particles, camera, states, menu, pause, 3D builder) are used with a
colon: `cam:follow(x, y)`.

Tweens, timers, scripts and jingles go on with **`lib.update()`**, to call once in
`_update` (it adds 1/60 s; `lib.update(dt)` another step). `lib.time` is the time counted
that way.

```lua
local lib = require "bmlib"

function _update()
  lib.update()          -- tweens, timers, scripts and jingles
  -- ... the game
end
```

### Numbers

| Function | Description |
|---|---|
| `lib.clamp(v, lo, hi)` | `v` between `lo` and `hi` |
| `lib.lerp(a, b, t)` / `lib.unlerp(a, b, v)` | from `a` (t = 0) to `b` (t = 1); the reverse: where `v` is between `a` and `b` |
| `lib.remap(v, a0, a1, b0, b1)` | `v` from the range `a0..a1` to the range `b0..b1` |
| `lib.approach(v, target, step)` | `v` toward `target` by at most `step` (bars that go down slowly, speeds that brake) |
| `lib.sign(v)` | −1, 0 or 1 (0 for 0) |
| `lib.round(v, [step])` | to the nearest integer, or to the nearest multiple of `step` |
| `lib.wrap(v, lo, hi)` | `v` brought back into `lo..hi` (`hi` excluded) going round: worlds that wrap |
| `lib.cycle(i, d, n)` | index `i` (1..n) moved by `d` going round: the rows of a menu |
| `lib.dist(ax, ay, bx, by)` / `lib.dist2(...)` | the distance / its square (to compare it with `r * r` without the root) |
| `lib.len(x, y)` / `lib.norm(x, y)` | the length of a vector / the vector 1 long (and the length it had); `0, 0, 0` for `0, 0` |
| `lib.angle(ax, ay, bx, by)` | the angle from `a` to `b` in radians (0 to the right, π/2 down on the screen) |
| `lib.angdiff(a, b)` / `lib.turn(a, target, step)` | from `a` to `b` the shortest way (−π..π) / angle `a` turned toward `target` by at most `step` |
| `lib.dir8(x, y)` | one of 8 directions: 0 right, 1 down-right, 2 down… 7 up-right (`nil` for `0, 0`); `lib.DIR8[d + 1]` is the vector 1 long |
| `lib.TAU` | 2π |

### Random numbers

| Function | Description |
|---|---|
| `lib.rnd([a, b])` | a number between `a` and `b` (`b` excluded); `lib.rnd(n)`: between 0 and `n`; `lib.rnd()`: between 0 and 1 |
| `lib.chance(p)` | `true` with probability `p` |
| `lib.choose(t)` / `lib.shuffle(t)` | a random element of the list (`nil` if it is empty) / the list shuffled (in place) |
| `lib.rng(seed)` | a generator of its own, which always gives the same numbers from the same seed (worlds made from a seed, lockstep network matches): `r:next()` between 0 and 1, `r:range(a, b)`, `r:int(a, b)` (integers, both included), `r:pick(t)`, `r:chance(p)`, `r:seed(s)` |

### Collisions

| Function | Description |
|---|---|
| `lib.overlap(ax, ay, aw, ah, bx, by, bw, bh)` | two rectangles (x, y, width, height) touch |
| `lib.hit(a, b)` | the same for two tables with `x, y, w, h` (`w` and `h` 8 if missing) |
| `lib.inside(px, py, x, y, w, h)` | the point is in the rectangle |
| `lib.circles(ax, ay, ar, bx, by, br)` / `lib.circrect(cx, cy, r, x, y, w, h)` | two circles / a circle and a rectangle touch |

### The map: walls, platforms, gravity

The functions look at the **tiles' flags** ([above](#sprites-and-map)): `lib.SOLID` (flag
0, 1), `lib.PLATFORM` (flag 1, 2), `lib.LADDER` (4), `lib.WATER` (8), `lib.HURT` (16).

| Function | Description |
|---|---|
| `lib.tiles(opt)` | how the map stops the bodies: `layer` (the layer: number or name, 1), `solid` and `platform` (the flags' masks: 1 and 2; 0 = none), `edge` (`true`: outside the map is solid). Returns the configuration |
| `lib.solid(x, y, [w, h])` | the rectangle (or the point) touches something solid |
| `lib.move(b, dx, dy)` | moves body `b = {x, y, w, h}` (`w`, `h` 8 if missing) by `dx, dy`, stopping it against the solid tiles and sliding it along the walls; it falls on a platform only from above (and not with `b.drop`: down through it). In steps shorter than a tile: it never jumps one. Returns `hx, hy`: −1 / 1 where it hit (left / right, up / down), otherwise 0 |
| `lib.step(b)` | one frame of a body with gravity (a platformer): `b.vx, b.vy` in pixels per frame, `b.gravity` (`lib.GRAVITY`, 0.25) and `b.maxfall` (`lib.MAXFALL`, 6); then `b.ground` is `true` on the ground and the speed that hit is 0. Returns what `lib.move` gives |
| `lib.ray(x0, y0, x1, y1, [mask])` | the first tile with a flag of `mask` (`lib.tiles`' solid one if missing) on the segment: `x, y` where the segment enters it and its cell `mx, my`; `nil` if the way is free (line of sight, bullets) |

```lua
local hero = { x = 40, y = 40, w = 8, h = 8, vx = 0, vy = 0 }
lib.tiles({ edge = true })                         -- the map's borders are walls
function _update()
  hero.vx = (btn("right") and 2 or 0) - (btn("left") and 2 or 0)
  if hero.ground and btnp("a") then hero.vy = -5 end
  hero.drop = btn("down")                          -- down from a platform
  lib.step(hero)
end
```

### Hitboxes and hurtboxes

**Hurtboxes** are where a body can be hit, **hitboxes** where an attack hurts (a punch, a
sword, a bullet). A world of hits gathers them in every frame and says who hits whom:
teams, one hit per attack, the parts of the body, the lanes of a beat 'em up, blades that
clash. The boxes are given in the code or come from the sheet (`zboxes`,
[above](#sprites-and-map)).

| Function | Description |
|---|---|
| `lib.hits()` | a world of hits `H` (tables `H.hurts`, `H.hitl`: the frame's boxes) |
| `H:clear()` | a new frame, to call in `_update` before the boxes: those of before go; the attacks (`id`) that did not come back in the last frame are over |
| `H:hurt(who, x, y, w, h, [opt])` | a hurtbox of `who` (any value: the body's table). `team` (the same team does not hit itself), `part` (a name: `"head"`; the parts that count most first), `z` and `depth` (a third dimension: the lane, the height) |
| `H:hit(who, x, y, w, h, [opt])` | a hitbox of the attacker `who`: `team`, `id` (an attack: it hits each body **once** while the same `id` comes back frame after frame; the boxes of one attack share it; without `id` it hurts in every frame it touches), `clash` (two hitboxes with `clash` that touch make a `"clash"` contact), `z`, `depth` and whatever the game needs (`damage`, `knock`...) |
| `H:zone(who, name, frame, x, y, [flip, opt])` | the boxes of a zone's frame (`zboxes`: `hurt` and `hit`) for the sprite drawn with `zspr(name, x, y, frame, flip)`: the hurtboxes with `opt` (`team`, `part`), the hitboxes with `opt.attack` (the options of `H:hit`; the team of `opt` if missing) |
| `H:check()` | the contacts of the frame, in the order of the hitboxes: `{kind = "hit" or "clash", by (the attacker), to (the body hit, or the other attacker), hit (the options of H:hit), part, x, y (the middle of where they touch)}`. An attack hits a body once a frame (its first box that touches) |
| `H:draw([hurt_c, hit_c])` | the boxes, to see them while making the game (blue and red) |
| `lib.box(b, x, y, [flip, w])` | a box of a frame (`b.x`, `b.y` from the corner, `b.w`, `b.h`) in the world, for the frame drawn at `x, y`; with `flip` mirrored over the frame's width `w`: `x, y, w, h` |
| `lib.separate(a, b)` | two bodies `{x, y, w, h}` that overlap move apart along the axis where they overlap less, half each (`a.fixed` or `b.fixed`: only the other); `true` if they touched. For fighters that do not pass through each other, crowds |

```lua
local H = lib.hits()

function _update()
  H:clear()
  for _, f in ipairs(fighters) do
    H:zone(f, f.anim, f.frame, f.x, f.y, f.face < 0,
           { team = f.team, attack = { id = f.swing, damage = 5 } })
  end
  for _, c in ipairs(H:check()) do
    c.to.life = c.to.life - c.hit.damage    -- c.by hit c.to
  end
end
```

### Easing, tweens, timers and scripts

`lib.ease` has the curves from 0 to 1: `linear`, `inquad`, `outquad`, `inoutquad`,
`incubic`, `outcubic`, `inoutcubic`, `insine`, `outsine`, `inoutsine`, `inback`, `outback`,
`inoutback`, `outelastic`, `outbounce`, `smooth` (smoothstep).

| Function | Description |
|---|---|
| `lib.tween(obj, to, secs, [ease, done])` | the fields of `obj` go to the values of table `to` in `secs` seconds (`ease`: a function or the name of one of `lib.ease`, linear if missing), then `done(obj)`. Returns a handle: `h:cancel()` |
| `lib.after(secs, f)` | `f()` in `secs` seconds |
| `lib.every(secs, f, [times])` | `f()` every `secs` seconds (`times` times, for ever if missing); `f` returning `false` stops it |
| `lib.script(f, ...)` | `f(...)` as a **script that can wait**: `lib.wait(secs)` (a frame if missing) and `lib.waitfor(cond)` (until `cond()` is true). It runs at once up to its first wait, then `lib.update()` takes it on: dialogues, cutscenes, waves of enemies written in a row |
| `lib.cancel(h)` | stops a tween, a timer or a script (the same as `h:cancel()`) |
| `lib.countdown(t, keys, [d])` | the fields `keys` of `t` above 0 go down by `d` (1/60 if missing), not under 0: a hero's cool-downs (in frames with `d = 1`) |
| `lib.clear()` | stops every tween, timer, script and jingle (a new level) |

```lua
lib.tween(title, { y = 80 }, 0.6, "outback")
lib.after(2, function() door.open = true end)
lib.script(function()
  say("Who goes there?")
  lib.wait(1.5)
  lib.waitfor(function() return btnp("a") end)
  say("You may pass.")
end)
```

### Lists and particles

| Function | Description |
|---|---|
| `lib.each(list, f)` | `f(item, i)` for each item; those for which it returns `false` leave the list (the others keep their order) |
| `lib.sweep(list)` | takes away the items with `.dead` (in order) |
| `lib.particles([max])` | a pool of at most `max` particles (200); when it is full a new one takes the place of the oldest |
| `P:add(x, y, vx, vy, life, colour, [opt])` | one particle (speeds in pixels per frame, life in seconds); `opt`: `size` (radius; 0 or 1 a pixel), `gravity`, `drag` (the part of the speed kept every frame), `colors` (a list: the colour through its life), `shrink`, `floor` (the y where it bounces), `bounce` (0.3) |
| `P:burst(x, y, n, [opt])` | `n` particles from `x, y`: `speed` (2), `angle` and `spread` (in radians; all round if missing), `life` (0.5 s), `color` or `colors`, and the options of `:add` |
| `P:update()` / `P:draw()` | in `_update` / in `_draw` |
| `P:count()` / `P:clear()` | how many there are / all away |

```lua
local fx = lib.particles(300)
fx:burst(x, y, 20, { speed = 3, colors = { 0xFFFFFF, 0xFFD050, 0xFF6020 }, gravity = 0.1 })
```

### Camera

| Function | Description |
|---|---|
| `lib.camera([opt])` | a 2D camera: `smooth` (0.15: the part of the way it goes every frame; 1 = it sticks to the target), `dead` `{w, h}` (a box in the middle where the target moves without the camera), `bounds` `{x0, y0, x1, y1}` in pixels or `true` (the map, `msize()`), `offset` `{x, y}`, `w`, `h` (the screen if missing). Fields `x`, `y` |
| `C:follow(x, y, [snap])` | one step toward point `x, y` (in the middle of the screen); `snap`: there at once |
| `C:shake(amount, [secs])` | the screen shakes up to `amount` pixels, less and less, for `secs` seconds (0.3) |
| `C:apply([view])` | `camera()` at the camera's place (with the shake): in `_draw` before the world, then `camera()` for the HUD. With a view `{x, y, w, h}` (`lib.split`) the drawing stays in the view (`clip`) and the camera's corner is the view's: after the views, `clip()` and `camera()` |
| `C:map([layer, mask])` | the cells of the map that show |
| `C:sees(x, y, [w, h])` / `C:screen(x, y, [view])` | the rectangle shows / where a point of the world is on the screen (in the view) |

### Game states

`lib.states(defs, [first, ...])`: title, match, pause, game over as tables
`{enter, update, draw, exit}`; each function gets its table (`function play:update()`),
which has `t` (the seconds spent in the state) and `name`.

| Function | Description |
|---|---|
| `S:go(name, ...)` | to state `name`: the open ones leave (`exit`), `name` enters (`enter(...)`) |
| `S:push(name, ...)` / `S:pop()` | `name` over the one now (a pause, a dialogue: only it updates, both draw) / back to the one under it |
| `S:update()` / `S:draw()` | in `_update` / in `_draw` (from the bottom: the game under its pause) |
| `S:is(name)`, `S.name`, `S:top()` | the state on top |

```lua
local S = lib.states({
  title = { update = function(s) if btnp("ok") then S:go("play") end end,
            draw = function() cls(0); lib.printc("PRESS A", 160, 0xFFFFFF) end },
  play = { enter = function(s) s.score = 0 end,
           update = function(s) ... end, draw = function(s) ... end },
}, "title")
function _update() lib.update(); S:update() end
function _draw() S:draw() end
```

### Text, bars and menus

| Function | Description |
|---|---|
| `lib.textw(text, [scale])` | width and height in pixels with the font of `print` now (the longest line) |
| `lib.printc(text, y, [c, scale, x, w, grid])` | in the middle of the screen (or of `x..x + w`); `grid`: on the font's columns (text that stays still, as the menus). Returns the x |
| `lib.printr(text, x, y, [c, scale])` | ending at `x` (numbers aligned to the right) |
| `lib.prints(text, x, y, c, [shadow, scale])` / `lib.printo(...)` | with a shadow under it to the right / with an outline all round (black if the colour is missing) |
| `lib.bar(x, y, w, h, v, max, [c, back, border])` | a bar filled for `v` of `max` (life, stamina, loading) |
| `lib.blink([period, t])` | `true` half of the time, changing every `period` seconds (0.5): "press A" |
| `lib.timestr(secs, [tenths])` | `"m:ss"` (`"h:mm:ss"` from an hour), with tenths `"m:ss.d"` |
| `lib.btnr(i, [p, delay, rate])` | `btnp` that **repeats** while the button is held: the first frame, then after `delay` frames (15) every `rate` (4). `i` as for `btn()`. Call it in every frame |
| `lib.menu(items, [opt])` | a list to choose from: the items are texts or tables `{label=, value=, change=function(item, d), ok=function(item), off=true}`; `opt.p` the player, `opt.wrap` (`true`) |
| `M:update()` | up and down (repeating) move, skipping the `off` items, left and right call `change`, ok chooses (`ok(item)`; it returns the item and `"ok"`), back returns `nil, "back"` |
| `M:draw(x, y, [opt])` | the rows from `x, y`: `w` (width: the values on the right), `c`, `sel_c`, `bar`, `dim`, `scale`, `gap` |
| `lib.pause([opt])` | the **pause menu** every game had: Start (Esc) opens it when `opt.when()` is true (always if missing); RESUME, VOLUME (the console's: left and right), the rows of `opt.rows`, QUIT (`opt.quit()`, if given). `opt.color`, `opt.title`, `opt.scale` |
| `P:update()` / `P:draw()` | in `_update`: `if pause:update() then return end` (while it is open the game stands still); in `_draw`, after the game |

```lua
local pause = lib.pause({ when = function() return S:is("play") end,
                          quit = function() S:go("title") end })
function _update()
  if pause:update() then return end
  lib.update(); S:update()
end
function _draw() S:draw(); pause:draw() end
```

### Players on one console

`btn(i, p)`, `stick(p)` and `controller(p)` read player `p` ([Input](#input)); bmlib adds
the screen where they join, the split screen and the colours. The SDK's *Versus 2D*
template is a whole example.

| Function | Description |
|---|---|
| `lib.PLAYER_COLORS` | the players' colours, those of the pads' lights: 1 blue, 2 red, 3 green, 4 pink (as `controller(p).color`) |
| `lib.pads()` | the numbers of the players who have a controller now, in order (`{1, 3}`) |
| `lib.party([opt])` | the screen where the players of one console join: each presses ok on their controller to be in, back to go out; Start of a player who is in begins, with at least `min` players (1); `max` (4); `join(p)`, `leave(p)`: functions called when one comes or goes. `P:update()` in `_update` returns the players in (their numbers, in the order they came) when the game begins, else `nil`; `P:draw([x, y, w, h])` a card for each place, in the player's colour, with their controller and the key to join; `P.list` who is in now |
| `lib.split(n, [opt])` | the screen cut into views `{x, y, w, h}` for `n` players (1–4): two side by side (`vertical`: one above the other), three or four in the corners (with three the bottom right stays free: a map, the scores); `gap` pixels between them (2); `x, y, w, h` the part of the screen (all of it if missing) |

```lua
local party = lib.party({ min = 2 })
local views, cams

function _update()
  if not views then
    local who = party:update()               -- e.g. {1, 2}
    if who then
      views, cams = lib.split(#who), {}
      for i, v in ipairs(views) do cams[i] = lib.camera({ w = v.w, h = v.h }) end
    end
    return
  end
  -- ... cams[i]:follow(hero[i].x, hero[i].y)
end

function _draw()
  cls(0)
  if not views then party:draw(16, 80, SCREEN_W - 32, 200) return end
  for i, v in ipairs(views) do
    cams[i]:apply(v)
    cams[i]:map()
    draw_world()
  end
  clip()
  camera()
end
```

### Sound, saves, animations, colours

| Function | Description |
|---|---|
| `lib.jingle(notes, [voice, wave, vol])` | a short tune on the voice (3): `notes = { {note, secs, [wave, vol]}, ... }`, the note in Hz or by name (`"C5"`), 0 or `"-"` a rest; `lib.jingle(nil, voice)` stops it, `lib.jingling([voice])` says whether it still plays |
| `lib.store([k, [v]])` | a field of the cartridge's save (`save`/`saved`): `lib.store(k)` reads it, `lib.store(k, v)` writes it (on the SD card only if it changed: not in every frame), `lib.store()` the whole table |
| `lib.best(k, v)` | record `k`: `v` if it beats it (and saves it), and `true` if it is new |
| `lib.frame(frames, fps, [t])` | the element of `frames` that time `t` (`time()` if missing) shows at `fps` a second, looping |
| `lib.anim(frames, fps, [loop])` | an animation of its own: `a:update()` moves on by a frame and returns the element; `a.done` when one that does not loop (`false`) is over; `a:reset()` |
| `lib.mix(c1, c2, t)` / `lib.shade(c, k)` | between two `0xRRGGBB` colours / a colour `k` times brighter (0 black) |

### 3D: Astro Wing's builder

`lib.builder()`: a mesh made of convex pieces; each face of a piece is turned outwards by
itself, so the order of the corners never matters.

| Function | Description |
|---|---|
| `B:piece(points, triangles, [colour])` | a convex piece: `points = { {x, y, z}, ... }`, `triangles = { {i, j, k, [colour]}, ... }` |
| `B:box(x0, y0, z0, x1, y1, z1, colour, [top])` | a box between two corners; `top`: the colour of its top face |
| `B:tetra(p1, p2, p3, p4, colour)` / `B:quad(p1, p2, p3, p4, colour, nx, ny, nz)` | a tetrahedron / a flat quad seen from the side `nx, ny, nz` |
| `B:build()` | the mesh (`mesh()`); the methods return `B`, so they chain |

```lua
local house = lib.builder()
  :box(-1, 0, -1, 1, 1.5, 1, 0xC0A080)
  :piece({ { -1.1, 1.5, -1.1 }, { 1.1, 1.5, -1.1 }, { 1.1, 1.5, 1.1 }, { -1.1, 1.5, 1.1 }, { 0, 2.4, 0 } },
         { { 1, 2, 5 }, { 2, 3, 5 }, { 3, 4, 5 }, { 4, 1, 5 }, { 1, 2, 3 }, { 1, 3, 4 } }, 0xA03020)
  :build()
```

Tests of it all: `make test-gameapi` (bmhost, `tests/gameapi/cart.lua`: every function
with its cases, the buttons too with a script) and `test_game_api` in QEMU.

## riff: music as patterns

`local R = require "riff"` (2026-10-06, the whole guide in [RIFF.md](RIFF.md), in
Italian): rhythms and melodies written in a line, as in TidalCycles and Strudel, played on
time by the sound's interrupt (`play_at`) with the ready-made instruments and the bank's
sounds by name. Time is counted in **cycles** (a bar of four beats; 2 s at first).

```lua
local R = require "riff"

function _init()
  R.setcpm(30)
  R.play("drums", R.s "kick*4, ~ snare, hat*8")
  R.play("bass", R.note "<c2 a1 f1 g1>" :s "acid" :lpf(R.sine:range(300, 1800):slow(4)))
end

function _update()
  R.update()                      -- every frame: the notes of the next moments into the queue
end
```

| Mini-notation | What it does |
|---|---|
| `a b c d` | a sequence in the cycle |
| `~` `-` | a rest |
| `[a b]` | a group in one part |
| `a, b` | together |
| `<a b>` | one a cycle |
| `a*2`, `a/2` | faster, slower (also `a*<2 4>`) |
| `a!3`, `a@3`, `a _ _` | repeated, longer |
| `a?`, `a?0.3` | at random |
| `a(3,8,2)` | a Euclidean rhythm |
| `{a b c}%4` | polymeter |
| `a \| b` | one of the two, each cycle |
| `kick:2` | a variant (two semitones up) |

| Function | Description |
|---|---|
| `R.s(p)`, `R.note(p)`, `R.n(p)`, `R.chord(p)` | instruments, notes (`c4` = 60, octave 3 when missing), degrees of a scale, chords (`"<Am F C G7>"`) |
| `R.seq`, `R.cat`, `R.stack`, `R.timecat`, `R.arrange`, `R.run(n)` | in sequence, a cycle each, together, with weights, sections of several cycles, `0..n-1` |
| `R.sine`, `R.cosine`, `R.saw`, `R.tri`, `R.square`, `R.rand`, `R.perlin`, `R.irand(n)`, `R.choose(…)` | signals 0..1 |
| `:fast`, `:slow`, `:early`, `:late`, `:rev`, `:ply`, `:iter`, `:palindrome` | time |
| `:every(n, f)`, `:lastOf`, `:sometimes(f)`, `:often`, `:rarely`, `:degradeBy(x)`, `:someCycles` | changes now and then, at random |
| `:euclid(k, n, r)`, `:struct(p)`, `:mask(p)`, `:segment(n)`, `:chunk(n, f)`, `:linger(x)`, `:swing(n)` | structure |
| `:off(t, f)`, `:superimpose(f)`, `:layer(…)`, `:jux(f)` | layers, left and right |
| `:add`, `:sub`, `:mul`, `+`, `:transpose`, `:scale("C:minor")`, `:arp("updown")`, `:range(a, b)` | notes and numbers |
| `:s`, `:gain`, `:legato`, `:lpf`, `:hpf`, `:bpf`, `:res`, `:pan`, `:room`, `:delay`, `:attack`, `:decay`, `:sustain`, `:release`, `:shape`, `:vib`, `:fm`, `:raw`, `:tone{…}` | the sound of each note (the units of `tone()`) |
| `R.play(name, p)`, `R.stop(name)`, `R.hush()`, `R.update()` | play (with a name), stop, to call in `_update` |
| `R.setcps(x)`, `R.setcpm(x)`, `R.bpm(x)` | the speed |
| `R.code(text)` | live code: riff's functions are globals, every global given a pattern plays under its name, the earlier ones not named again stop; `true` or `nil` and the error |
| `R.bake(p, {cycles=, steps=16})`, `R.piece(t)` | the pattern as a piece for the bank (the shape of `ai.music`), a piece of `ai.music` as a pattern |
| `R.playing()`, `R.get([name])`, `R.errors()`, `R.active()`, `R.voices(v, …)` | the names playing, their patterns, the errors, where the notes sounding are written, the voices it may use |

```lua
R.code [[
setcpm(28)
local verse = chord "<Am F C G>"
pad   = verse :s "pad" :room(.6) :gain(.7)
arp   = verse :s "pluck" :arp("updown") :fast(4)
drums = s "kick ~ ~ kick, ~ snare, hat*8?" :every(4, fast(2))
]]
```

In **bm Code** Ctrl+Enter plays the code (the tab, or the `riff.code [[ ]]` block under
the cursor) and lights up the words of the notes sounding, Ctrl+. stops it; in **bm
Sound** F7 (*Riff...*) plays a line and Ctrl+Enter puts it into the bank as a song. Tests:
`make test-riff`, QEMU `test_riff_code`, `test_sound_riff`.

## bmnet: games over the network

`local net = require "bmnet"` (2026-10-04): what Overbit's network code does, for every
game. The consoles find each other on the **LAN** (UDP broadcasts) or over the
**internet** through a relay (`tools/overbit_relay.py`: a room passes every packet to the
other consoles, as a broadcast would); one **hosts** the match and the others **join**;
they send **messages** (that may be lost, or sure and in order) and, for action games,
the match runs in **lockstep**: every console runs the whole game with the same seed and
only the players' inputs travel; a frame runs when everybody's inputs for it are there
(the host gathers them and sends them to all). The SDK's *Online 2D* template is a whole
example; the packets are described at the top of `src/script/bmnet.lua`.

| Function | Description |
|---|---|
| `net.open([opt])` | opens the network: `game` (4 characters, the game and its version: the packets of other games are not seen), `port` (47320, the same on every console), `relay` (`"name"` or `"name:port"`, 47310: over the internet; without, the LAN), `room` (4 characters, `"PLAY"`: the consoles of a room see each other on the relay), `name` (the player's name). `true`, or `false` and why (no network) |
| `net.close()` | leaves: the others know (in a match the seat's input becomes `false`; when the host leaves the match ends for all), the socket closes. Call it from `_leave()` too |
| `net.host([opt])` | this console hosts a match: `max` players (2–8, 4), `info` (a line the others see in the list). It is seat 1 |
| `net.hosts()` | the matches found in the last 3 seconds: `{id, name, players, max, info}`, by id |
| `net.join(id)` | asks console `id` for a seat: the event `"joined"` when it says yes, `"refused"` if it is full or has begun |
| `net.peers()` | the consoles in the room or the match: `{id, name, seat, me}`, by seat |
| `net.start([opt])` | (the host) starts the match with those in: `seed` (the same random numbers on every console; one by chance if missing), `delay` (frames between a press and its frame: 4 on the LAN, 7 through the relay), `data` (a string for everybody: the options chosen). Everybody gets the event `"start"` (the host too, at the next `net.update()`) |
| `net.update()` | once in each `_update`: the packets that came, the lobby's announcements, the sure messages to send again. Returns the frame's **events**, a list of `{type = ...}`: `"join"` (`id, name, seat`), `"leave"` (`seat, id`), `"joined"` (`seat`), `"refused"`, `"start"` (`seed, seat, seats, host, data`), `"msg"` (`from, data, sure`), `"lost"` (`why`: the host has gone), `"desync"` (`frame`), `"error"` (`text`) |
| `net.send(data, [to])` | a message (a string up to 900 bytes) to everybody or to console `to` (an id): it may be lost or come after a newer one (positions) |
| `net.post(data, [to])` | a **sure** message: repeated until it arrives, and each console gets another's in the order they left (chat, a turn, a choice) |
| `net.input(v)` | (lockstep) my input for frame `net.frame + delay`: a 32-bit whole number (`net.pad(1)`). Once in each `_update` of the match, before `net.frames()` |
| `net.frames()` | the frames of the match that can run now: `for f, inputs in net.frames() do ... end`, `inputs[seat]` that seat's input (`false` once its player has left). One, two when the console is behind; none while an input is missing (`net.stall`: seconds since the last) |
| `net.check(hash)` | a number that sums up the game after the frame just run (positions, scores): the consoles compare them and a difference is the event `"desync"`. Every second is enough |
| `net.pad([p])` / `net.unpad(v)` | the 16 buttons of `pad(p)` and the left stick (8 bits each way) in 32 bits / back to buttons, `x`, `y` (−1..1) |
| `net.held(v, button)` | a button (`"a"`, `"left"`... as `pad()`) held in an input of `net.pad` |

Fields: `net.state` (`"off"`, `"lobby"`, `"hosting"`, `"joining"`, `"joined"`,
`"playing"`, `"lost"`), `net.me` (my id), `net.seat`, `net.seats` (the match's seats),
`net.is_host`, `net.frame` (the next frame), `net.seed`, `net.delay`, `net.data`,
`net.error`.

**The rules of lockstep.** The match's game depends **only** on the frames' inputs and the
seed: random numbers with `lib.rng(seed)` (never `math.random`, `time()` or `stat()`),
nothing of the console (the camera, the quality) inside the simulation, no game state
changed in `_draw`. The drawing may differ on every console. Two players on the same
console in a network match put their inputs in the same number (16 bits each).
`online(true)` is turned on by `bmnet` when the match begins: PS asks before leaving and
calls `_leave()`.

```lua
local net = require "bmnet"
local lib = require "bmlib"
local rng

function _init() net.open({ game = "MYG1" }) end
function _leave() net.close() end

function _update()
  for _, e in ipairs(net.update()) do
    if e.type == "start" then rng = lib.rng(e.seed) end
  end
  if net.state == "lobby" and btnp("a") then net.host({ max = 2 }) end
  if net.state == "hosting" and btnp("start") then net.start() end
  if net.state == "playing" then
    net.input(net.pad(1))
    for _, inputs in net.frames() do step(inputs) end   -- the game, the same everywhere
  end
end
```

**Trying it on the PC.** Two `bmhost` on the same PC are two consoles: `BMHOST_NET_ID=0`
and `BMHOST_NET_ID=1`, with `--realtime`; the relay runs with `tools/overbit_relay.py
--port N`. `make test-bmnet` does it: the lobby, sure messages with a fifth of the packets
lost (`loss = 0.2` in `net.open`, for tests only), a lockstep match the same on both
consoles, a player leaving; on the LAN and through the relay.

## Budget and tips

- 60 fps = **16.7 ms** per frame for `_update` + `_draw` + the copy to the screen. At the
  top left of the demo, `stat(1)` shows how much the cartridge uses.
- The **dev kit**: the performance overlay over any game, at the top right. It is turned on
  from Settings > Screen and sound > "Performance overlay" (Off, Simple, Detailed,
  Functions: it stays saved), with F11 on the keyboard (a system key, also in the tools; it
  was F3), with `p` from the serial line or with `devkit(mode)` from the game: once the
  simple page, again the detailed one, again the functions page, again off. F11, `p` and `devkit()` last one run: every game starts (and
  resumes) as Settings says. The simple page:

      60fps 6.1ms ^7.5      frames a second; ms of _update + _draw: average and,
                            after ^, the top of the last second
      lua 9k ^10k           Lua instructions of a frame (thousands): average, top
      ram 612k ^700k        memory: Lua now plus the data (sheet, map, models,
                            sounds, z-buffer) and, after ^, the top
      1234 tokens           the code's tokens (code_tokens)

  under it, the time of the last 64 frames: the top is 16.7 ms; green under half, yellow up
  to 16.7, red beyond (the frame is skipped). The detailed page adds the last frame in its
  phases and the 3D:

      update 5.8ms x2       all its _update (x2: two ran, frameskip)
      draw   17.4ms         its _draw
      3D     13.3ms         the 3D calls (stat(6))
      gpu 9.1ms 2 jobs      the GPU's work (with the ARM: px, the pixels it drew)
      tri 3620 vtx 5699     triangles drawn, vertices placed
      bm3d 2.1 GPU          the 3D driver (ARM, GPU, GPU+AA)
      quality HIGH          the game's lines (devinfo)

  The **functions** page (R14) shows, under the simple one, the ten functions that cost
  the most, in ms a frame, the average of the last second (60 frames):

      function        self   all
      draw_scene      2.28  6.43    a Lua function: its own time and with what it calls
      map             0.94  0.94    in light blue the console's functions (spr, map, the 3D...)
      _draw           0.03  7.70    the callbacks have their names

  `self` is the function's own time, `all` the time with the functions it calls: a
  function with a big `all` and a small `self` spends its time in the others. The console's
  functions are measured exactly, the Lua ones in pieces of 1000 instructions (enough to see
  where the time goes). The profiler runs only with this page or `profile(true)`; off it
  costs nothing. From the code: `profile()` (below).

- The **last session's report** (2026-10-10): when a game ends the dev kit writes one file
  per game into the reports, `reports/<branch>/session_<game>_<board>.txt`, which the next
  run of the same game **replaces**. In it: screen, frames and fps, the mean and most time
  of `_update` + `_draw`, memory, and the time **from one picture to the next** in six
  classes (up to 18 ms, 25, 34, 50, 100, more) with the 12 longest frames and their parts
  (`cpu`, `upd`, `draw`, 3D, copy and `outside`: the time that is none of them, the SD
  card, the GPU, the machine), and the game's own `devinfo()` lines as the run ended ("the game says:"; Yharnam: the chunks the view waited for). A half-second freeze is no longer lost after the overlay's 64
  frames. It goes from the menu when there is a network. `bm/config.txt`:
  `session_report=0` never, `=1` always; without the line, only with a `github_token`.

  On the big screens it is bigger (x2 from 1280 wide, x3 at 1920). From the code:
  `stat(1)`, `stat(2)`, `stat(6)`, `stat(10)`, `stat(11)`–`stat(15)`, `devkit()`,
  `profile()`. The
  limit is 20 million instructions per call. The
  **SDK's dev kit** (F1 twice) has the same numbers for the project: tokens, the biggest
  functions, the data's memory, the file against the 8 MiB of a `.b16` and the numbers of
  the last try (F5). At the end of a run the serial line also writes the line `dev kit:
  Lua peak ... KiB, data ... KiB, busiest frame ...k instructions, ... frames over 16.7 ms,
  ... tokens`.
- Drawing is in C: a `spr` or `rectfill` call costs a few microseconds, but every call from
  Lua has a fixed cost. Orders of magnitude on the Pi (docs/STRESS.md): ~1800 16×16 sprites
  called from Lua at 60 fps, ~4500 from C; ~1200 3D triangles.
- Avoid creating new tables in every frame if not needed (less work for the GC).
- Text is in 8×16 cells: for well aligned writing use x multiples of 8 and y of 16.
