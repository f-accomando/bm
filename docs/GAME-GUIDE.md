# Making a `.bm` game

A practical guide: from the first Lua file to the cartridge on the SD card, with sprites,
maps, 3D models, sound and saves. The complete reference of every function is in
[API-EN.md](API-EN.md). In Italian: [GUIDA-GIOCHI.md](GUIDA-GIOCHI.md) and
[API-IT.md](API-IT.md).

## 0. On the console: the editor

Without a PC: in the cartridges' menu the last item is **bm editor** (up arrow from the
first; also `e` from the monitor). With a USB keyboard (and, if you like, a Bluetooth
gamepad to draw):

| Key | Page |
|---|---|
| **F1** | code (Ctrl+Z undoes, Ctrl+K cuts a line, Ctrl+D duplicates) |
| **F2** | sprites: arrows, space draws, `x` picks the colour, `f` fills, `,` `.` (or `[` `]`) colour, Tab goes to the sheet, `z` 8×8/16×16, `h`/`v` mirror, `u` undoes, `0`–`7` turn the tile's **flags** on and off (below: wall, platform...) |
| **F3** | map: arrows, space places the tile, `x` picks it, `f` fills, `,` `.` tile, Tab chooses the tile, `l` the next **layer**, `o` that layer alone; *New map layer* in the menu adds one |
| **F12** (held) | the list of the page's keys |
| **Esc** | menu: new, open, save, save as, title, author, resolution, exit |
| **Ctrl+S** / **Ctrl+R** (F5) | save / try the game (then back to the editor) |

**bm Code**: in the Dev tab there is also **Code**, the editor of the code alone: more
cartridges in tabs, two pages side by side (F4), a small sharp font (6x12: many lines), F5
tries the game and comes back to the error's line, F1 shows all the keys. It saves only the
code: the cartridge's sprites and map stay as they are. A line `#entry: comment this
function #` followed by Enter asks the assistant to do it.

**Assistant** (M30): in the Dev tab, **Assistant** answers questions like "how do I make
the character jump" or "attempt to call a nil value" with the explanation and the code
ready, and draws the base of a sprite ("red slime", "coin", "grass tile"). It is used by
itself for now; later it will open with F6 inside the editor.

If the game stops with an error, the editor goes back to the line in red (Ctrl+G finds it
again). Games are saved in `/carts` with an 8.3 name (e.g. `MYGAME.BM`) and appear in the
menu. Everything else in this guide holds for the editor too.

**On the PC**, for 3D models and pixel art: **bm Studio**; for skeletons, animations and
pre-rendered sprites: **bm Animator** ([sdk/README.md](../sdk/README.md)). They are web
pages that open and save `.bm` files (also right on the SD card). The console's editor,
when it saves, keeps the models and the animations. **On the console**, in the Dev tab,
there are **bm Studio** and **bm Animator** with the same names (X on a game's cover,
*Open in bm Studio* / *Open in bm Animator*): they build with blocks and tiles, choose and
move faces and corners, paint on the model, make skeletons, animations and sprites, save
and try the game, with the keyboard or the gamepad.

## 1. What a cartridge is made of

A `.bm` cartridge is one file that holds:

| Part | Where it comes from | Required |
|---|---|---|
| Lua code (`main.lua`) | `--lua` | yes |
| sprite sheet (PNG) | `--sheet` | no |
| tile map (CSV), one per layer | `--map` (`--map name=file.csv` for each more layer) | no |
| the tiles' flags (wall, platform, ladder...) | `--flags` | no |
| named zones of the sheet (sprites and animations) | `--sprites` | no |
| cover for the menu (PNG) | `--cover` | no |

`scripts/mkbm.py` makes it (Python's standard library only, no dependencies).
Resolution: **640×360** (default), **480×270** with `--res 480x270` (the compromise for
textured 3D), **320×180** with `--res 320x180` (bigger pixels, 16-bit style, and more time
per frame) or **256×256** square with `--res 256x256` (in the middle of the screen, 4×
bigger on 1080p, black borders). Colours: `0xRRGGBB`, the screen is 16-bit (RGB565).

## 2. The smallest game

`carts/ball/main.lua`:

```lua
local x, y, c = 320, 180, 0xFFD050

function _init()                -- once, at start
end

function _update()              -- 60 times a second: logic and input
  if btn(0) then x = x - 3 end  -- 0 left, 1 right, 2 up, 3 down
  if btn(1) then x = x + 3 end
  if btn(2) then y = y - 3 end
  if btn(3) then y = y + 3 end
  if btnp(4) then               -- A, only in the frame it is pressed
    c = rgb(math.random(255), math.random(255), math.random(255))
    note(0, 660, 80, SQUARE, 100)
  end
end

function _draw()                -- 60 times a second: drawing
  cls(0x101828)
  circfill(x, y, 20, c)
  print("arrows move, A changes colour", 16, 16, 0xFFFFFF)
end
```

Buttons: `btn(i)` while it is held, `btnp(i)` only when it is pressed.

| `i` | Button | DS4 | Keyboard |
|---|---|---|---|
| 0–3 | left, right, up, down | cross / stick | arrows or WASD |
| 4 | A | cross | space, Z, J |
| 5 | B | circle | X, K |
| 6 | X | square | C, L |
| 7 | Y | triangle | V, I |

Esc, Start+Select or the PS button go back to the menu, and the game stays **suspended** in
memory (A on its cover resumes it). `quit()` instead really closes the cartridge. A network
game calls `online(true)`: there PS asks the player whether to leave the match and
disconnect, and if so it calls `_leave()` (see `docs/API-EN.md`).

**More players.** With two or more Bluetooth controllers (paired from the monitor with `T`:
the first is player 1, the second player 2...) each player has their own buttons:

```lua
local n = players()                       -- how many controllers there are
if btn(2, 1) then p1.y = p1.y - 3 end     -- up, player 1
if btn(2, 2) then p2.y = p2.y - 3 end     -- up, player 2
local x, y = stick(1)                     -- player 1's stick, from -1 to 1
```

`btn(i)` without a player answers to every controller: fine for menus and one-player games.
The USB keyboard is the first player without a pad.

**Mouse.** A game has the pointer only if it asks for it (a mouse or the right stick of a
pad moves it):

```lua
function _init() mouse(true) end           -- the console draws the arrow
function _update()
  local mx, my = mouse()                   -- nil if nothing moves it
  if mx and mousep() then                  -- left click
    tx, ty = mx, my
  end
end
```

## 3. Packing and trying

```sh
cd ~/bm
python3 scripts/mkbm.py -o ball.bm --lua carts/ball/main.lua --title "Ball" --author "me"
```

Options: `--sheet sprites.png`, `--map map.csv` (again `--map front=over.csv` for another
layer), `--flags flags.csv`, `--sprites zones.txt`, `--cover cover.png`, `--res 480x270` or
`--res 320x180`.

**On the Pi:** copy the file into the `carts/` folder of the SD card.

```sh
sudo mount -t drvfs D: /mnt/d
sudo cp ball.bm /mnt/d/carts/
sync && sudo umount /mnt/d
```

The cartridge appears in the menu (if the Pi is already on, **R** in the menu reads the SD
card again). On leaving, the line at the bottom of the menu shows fps and milliseconds of
`_update` + `_draw`: the budget is **16.7 ms** per frame.

**In the repository's build:** put the game in `carts/<name>/main.lua`, add `<name>` to
`GAMES` in the `Makefile` and a line `title_<name> := Title` (plus `res_<name> := 320x180`
if needed). `make` creates it in `build/carts/<name>.bm` and `make sdcard` copies it into
`dist/carts/`. From the same folder `sheet.png`, `map.csv`, `flags.csv`, `sprites.txt` and
`cover.png` are taken by themselves, if they are there; the map's other layers are
`map_<name>.csv`, in the order of `layers_<game> := name ...`.

**Errors:** if the Lua code goes wrong, the game stops and the message with the line number
appears on the console.

## 4. 2D sprites

Sprites live in a **PNG** (8-bit RGB or RGBA, sides multiples of 8, up to 2048×2048),
drawn with any editor (Aseprite, LibreSprite, Piskel, GIMP). Pixels with alpha under 128
are **transparent**.

The sheet is divided into **8×8 cells**, numbered from left to right and top to bottom:
with a sheet 128 px wide there are 16 cells per row, and cell `n` is in column `n % 16`,
row `n // 16`.

```lua
spr(0, x, y)                   -- cell 0 (8x8)
spr(2, x, y, 2, 2)             -- 2x2 cells from cell 2: a 16x16 sprite
spr(2, x, y, 2, 2, true)       -- mirrored horizontally (and then vertically)
sspr(sx, sy, w, h, x, y)       -- any rectangle of the sheet, in pixels
```

With `sset(x, y, colour)` / `sget(x, y)` the code reads and writes the sheet: sprites can
be drawn without a PNG (Star Shooter writes them as strings in `carts/shooter/main.lua`) or
changed during the game.

Animation: change cell every so many frames, for example
`spr(base + (frame // 8) % 2 * 2, x, y, 2, 2)`.

**Sprites with a name.** Instead of coordinates, a name: the sheet's **zones**, each with
its frames (boxes of the same size side by side) and its speed. They are written in a text
file, one per line (`name x y width height [frames [fps]]`), and passed to
`mkbm.py --sprites` (in the build `sprites.txt` in the game's folder is enough):

```
# zones.txt
hero_still   0  32 16 16
hero_run     16 32 16 16 4 10
coin         0  48 8  8  6 12
```

```lua
zspr("coin", x, y)                         -- it animates by itself, at 12 frames a second
zspr("hero_run", x, y, nil, to_the_left)   -- mirrored when it goes left
zspr("hero_still", x, y, 1)                -- one precise frame
```

`zone(name)` gives where it is and how big (for collisions), `zones()` all the names. Zones
are also made with bm Pixel on the console and with `scripts/bmres.py`.

## 5. Tile maps

The map is a grid of the sprite sheet's cell numbers (0 = empty), in a **CSV** (one line of
text per row of the map, lines starting with `#` are comments):

```
# a 4x3 map
1,1,1,1
1,0,0,1
1,1,1,1
```

```lua
camera(cam_x, cam_y)                                  -- scrolling
map(cam_x // 8, cam_y // 8, cam_x // 8 * 8, cam_y // 8 * 8, 81, 46)   -- only the part that shows
local t = mget(px // 8, py // 8)                      -- the cell under a point
mset(cx, cy, 5)                                       -- changing a cell (doors, objects taken)
```

Without `--map` the map is 256×256 empty and is filled with `mset` (`msize(w, h)` gives it
another size). A big map (Hunter's Night: 256×256 cells, 2048×2048 pixels) is better made
by a script: see `carts/hunt/mkassets.py`, which writes `sheet.png` and `map.csv`.

**Flags: what each tile is.** Every tile of the sheet has 8 flags (0–7), on or off.
Collisions look at the flags, not at the tiles' numbers: new tiles (a stone wall, a wooden
one) can be added without touching the code. bmlib's convention:

| Flag | Value | Meaning |
|---|---|---|
| 0 | 1 | solid: walls, floors |
| 1 | 2 | platform: passed through from below, stood on from above |
| 2 | 4 | ladder |
| 3 | 8 | water |
| 4 | 16 | hurts (spikes, lava) |
| 5–7 | | free for the game |

They are written in a file for `mkbm.py --flags` (in the build: `flags.csv` in the game's
folder), turned on from the editor (keys `0`–`7` on the sprite page) or from the code:

```
# flags.csv: tile 1 is a wall (1), tiles 2 and 3 platforms (2), tile 9 a ladder (4)
1=1 2 2
9=4
```

```lua
fset(12, 0, true)                       -- tile 12 is solid too
if fget(mget(cx, cy), 0) then ... end   -- is the cell solid?
local f = mflags(x, y + 8, 8, 1)        -- the flags under the feet of an 8x8 sprite (in pixels)
if f & 1 ~= 0 then on_ground = true end
if f & 16 ~= 0 then hurt() end
```

**Layers.** A map can have up to 8 layers of the same size, each with its name: the floor,
the decorations, what goes **in front of** the character. They are drawn one at a time:

```lua
map(mx, my, x, y, w, h, "main")     -- behind
spr(hero, hx, hy)
map(mx, my, x, y, w, h, "front")    -- in front: tree tops, arches, roofs
mset(cx, cy, 0, "front")            -- each layer is read and written by itself
```

In the `.bm` they go with `--map map.csv --map front=over.csv`; from the code `mlayers()`
lists, adds and renames them. For movement with walls, platforms and gravity there is
`bmlib` (chapter 11): `lib.move` and `lib.step` do all the work.

## 6. 3D models

The 3D is software (in C on the ARM; the Pi's GPU draws it when it can): triangles with a
z-buffer, per-face light, fog, textures. A rough budget: about 1200 triangles drawn at 60
fps.

**Ready-made shapes:** `mesh_cube(colour)`, `mesh_sphere(rings, segments, c1, c2)`.

**Your own models** with `mesh(vertices, faces, [uv])`, usually made in `_init`:

```lua
local pyramid = mesh(
  { 0,1,0,  -1,-1,-1,  1,-1,-1,  1,-1,1,  -1,-1,1 },           -- x,y,z of each vertex
  { 1,3,2,0xE04040,  1,4,3,0xC03030,  1,5,4,0xE04040,           -- a,b,c,colour (indices from 1)
    1,2,5,0xC03030,  2,3,4,0x802020,  2,4,5,0x802020 })

function _draw()
  cls(0)
  zclear()                                   -- every frame, before draw3d
  camera3d(0, 1, -5, 0, 0, 60)               -- position, yaw, pitch, fov (and roll)
  light3d(-0.4, 0.8, -0.5, 0.3)              -- the light's direction, ambient light
  draw3d(pyramid, 0, 0, 0, 0, time(), 0, 1)  -- position, x/y/z turn, scale
end
```

- Faces go **clockwise seen from outside**, that is as they appear on the screen from the
  side that shows (those turned the other way are not drawn). bm Studio makes them so
  already. bmlib's builder (`lib.builder()`, Astro Wing's) makes models from convex pieces
  and turns the faces by itself: `lib.builder():box(-1, 0, -1, 1, 2, 1, 0xC08040):build()`.
- **Textures:** with the third table `uv` (6 numbers per face: u,v of the three vertices,
  in sprite sheet pixels) the faces with colour `-1` take the picture from the sheet, with
  correct perspective.
- **Fog:** `fog3d(colour, near, far)` fades the far objects.
- **Projection:** `project3d(x, y, z)` gives the screen position of a 3D point, to draw on it
  in 2D (horizon, sights, names).
- 2D and 3D mix: a background with `rectfill`/`map`, models with `draw3d`, the HUD with
  `print` at the end.

**With bm Studio** ([sdk/README.md](../sdk/README.md)): models are made on the PC placing
the sprite sheet's tiles on a grid, and they live in the `.bm` itself; in the game
`model("name")` gives them as meshes:

```lua
local house
function _init() house = model("house") end           -- in _init: it builds the mesh
function _draw()
  cls(0) zclear()
  camera3d(0, 4, -8, 0, -0.4)
  draw3d(house, 0, 0, 0, 0, time() * 0.5)
end
```

`models()` gives the names, `bounds3d(m)` the box round the model.

**Animated with bm Animator**: the skeleton and the animations (made on the PC, in the
`.bm`) come with the model; `animate(m, "walk", t)` puts the model in the pose of "walk" at
time `t` (in seconds), before `draw3d`:

```lua
local man, t = nil, 0
function _init() man = model("villager") end
function _update() t = t + 1 / 60 end
function _draw()
  cls(0) zclear()
  camera3d(0, 2, -6, 0, -0.25)
  animate(man, "walk", t)                     -- or animate(man, "walk", t, "idle", t, k): a blend
  draw3d(man, 0, 0, 0)
end
```

`clips(m)` says which animations there are, `bone3d(m, "arm.L")` where a bone is (the
head, then the tail: to attach a sword or a light to it). bm Animator also makes
**pre-rendered sprites**: the animation drawn from 1 to 8 directions in the sprite sheet, to
use with `sspr()` in a 2D game (it prepares the Lua code that draws them).

**From Blender** (or other programs): a `.glb` is imported into bm Studio (its textures end
up in the sprite sheet) and from there into the `.bm`; for a game of the repository putting
`models.glb` in its folder is enough (see `carts/village`).

## 7. Sound

Audio goes out over HDMI: eight synth voices (0–7), six waveforms (`SQUARE`, `TRIANGLE`,
`SAW`, `NOISE`, `SINE`, `METAL`) with an ADSR envelope. Two ways, also together.

**Effects and music made with the Sound editor** (**Dev** tab of the menu). It is the
easiest way: you compose with the pad or the keyboard and save inside the game.

1. In the menu, on the game: **X** → *Open in the Sound editor* (or Dev → Sound, then
   *Open...* from the editor's menu, SELECT).
2. **SOUNDS** page: the instruments (try *KICK*, *BASS*, *LEAD* of the demo project).
3. **SFX** page: the game's effects, one note per step (A adds, A + up/down changes the
   note, START plays).
4. **PATTERN** page: 8 tracks × 16 steps, like a drum machine; **SONG**: the patterns'
   order.
5. **Save** (SELECT → Save, or Ctrl+S): the sounds end up in the `.bm`. *Try it in the game*
   starts it and comes back to the editor.

In the code two functions are enough:

```lua
function _init() music(0) end          -- song 0, looping
-- ...
if jump then sfx(1) end                -- effect 1, on a free voice
if coin then sfx(0, nil, combo) end    -- transposed by `combo` semitones
if over then music(-1, 800) end        -- the music fades in 0.8 s
```

**Notes from the code**, for sounds that depend on the game:

```lua
note(0, 880, 60, SQUARE, 100)        -- voice, Hz (or "A5"), length in ms, wave, volume 0-255
envelope(2, 0, 60, 0, 30)            -- attack, decay, sustain, release of voice 2
note(2, 2500, 300, NOISE, 130)       -- an explosion that fades
note(1, 1300, 300, SQUARE, 70)       -- a laser that starts high...
slide(1, 300, 250)                   -- ...and goes down
note(3, "C4", 500, SQUARE, 90)
arp(3, "minor", 40)                  -- an arpeggiated chord, chip style
```

A good habit: one voice per kind of sound (weapon, hits, music), so an effect does not cut
another; with `sfx(n)` the console chooses the voice, leaving the music alone. The
**volume** is the console's: it changes in Settings or in the game's pause menu (`volume()`
reads and changes it; put it in your game's pause too: `lib.pause()` of bmlib has it). For
short tunes there is `lib.jingle`.

## 8. Lights (dark scenes)

```lua
cls(0); map(...); spr(...)                   -- the scene
light_begin(0x0A0A16)                         -- background light: night
light(lx, ly, 50, 0xFFB060)                   -- a street lamp (world coordinates)
light(px, py, 40, 0xFFC888, 0.9)              -- the player's lantern
light_end()                                   -- applies the light to everything above
print("life", 4, 4, 0xFFFFFF)                 -- the HUD after: it stays at full light
```

A flickering flame: the radius multiplied by `0.95 + math.random() * 0.1`. A complete
example: `carts/hunt/main.lua`.

## 9. Saves

```lua
function _init()
  local d = saved()                  -- the table saved last time, or nil
  if d then record = d.record end
end
-- at the end of the match (not in every frame: writing on the SD card takes a few ms)
save({ record = record })
```

Numbers, strings, booleans and tables, up to 32 KiB, in `/bm/save/` on the SD card. With
bmlib: `lib.best("record", score)` keeps the record and writes only when it is beaten.

## 10. Cover

`--cover cover.png`: a PNG of any size, printed on the game's "card" in the menu, an 88×88
square: a square picture is scaled down, others (16:10) stay whole over a blurred copy of
themselves. The demo games' covers are drawn by `scripts/mkcovers.py`. Without a cover the
menu prints the title.

## 11. The shared library: bmlib

Many things come back in almost every game: keeping a number between limits, choosing at
random, seeing whether two rectangles touch, making a character walk between walls, making
the camera follow, an explosion's particles, the title and the match, the pause menu, a
jingle, the record. They are in **bmlib**, a library built into the console:

```lua
local lib = require "bmlib"
```

Times in seconds, positions in pixels, speeds in pixels per frame; tweens, timers and
jingles go on with `lib.update()`, once in `_update`. The complete list is in
[API-EN.md](API-EN.md#bmlib-the-games-shared-library). Here is a whole platformer, with no
other files (the tiles are drawn in the code and the map is built with `mset`): title,
match, jumps, platforms, coins with sparks, camera, pause, record.

```lua
-- Jumps: a platformer with bmlib
local lib = require "bmlib"

local WALL, LEDGE, COIN = 1, 2, 3               -- tiles = cells of the sheet
local hero, cam, fx, S, pause

local function tile(n, c)                       -- an 8x8 tile with a darker border
  local x0, y0 = n % 32 * 8, n // 32 * 8
  for y = 0, 7 do
    for x = 0, 7 do sset(x0 + x, y0 + y, (x == 0 or y == 0) and lib.shade(c, 0.6) or c) end
  end
end

function _init()
  tile(WALL, 0x806040); tile(LEDGE, 0x40A040); tile(COIN, 0xFFD050)
  fset(WALL, 0, true)                           -- flag 0: solid
  fset(LEDGE, 1, true)                          -- flag 1: platform (you go up through it)
  msize(160, 45)                                -- 1280x360 pixels
  for x = 0, 159 do mset(x, 44, WALL) end       -- the floor
  for x = 10, 16 do mset(x, 38, LEDGE) end
  for x = 22, 28 do mset(x, 32, LEDGE) end
  mset(25, 31, COIN); mset(60, 43, COIN)
  lib.tiles({ edge = true })                    -- the map's borders are walls
  cam = lib.camera({ bounds = true, dead = { 64, 48 } })
  fx = lib.particles(200)
  pause = lib.pause({ when = function() return S:is("game") end, quit = function() S:go("title") end })
  S = lib.states({
    title = {
      update = function() if btnp("ok") then S:go("game") end end,
      draw = function()
        cls(0x102030)
        lib.printc("JUMPS", 120, 0xFFD050, 3)
        if lib.blink() then lib.printc("press A", 200, 0xFFFFFF) end
        lib.printc("record " .. (lib.store("record") or 0), 240, 0x8090A0)
      end,
    },
    game = {
      enter = function(s)
        hero = { x = 40, y = 300, w = 8, h = 8, vx = 0, vy = 0 }
        s.coins = 0
        cam:follow(hero.x, hero.y, true)
      end,
      update = function(s)
        hero.vx = (btn("right") and 2 or 0) - (btn("left") and 2 or 0)
        if hero.ground and btnp("a") then hero.vy = -5; note(0, 440, 60, SQUARE, 90) end
        hero.drop = btn("down")                 -- down from a platform
        lib.step(hero)                          -- gravity, walls, platforms
        local cx, cy = (hero.x + 4) // 8, (hero.y + 4) // 8
        if mget(cx, cy) == COIN then            -- taken
          mset(cx, cy, 0)
          s.coins = s.coins + 1
          fx:burst(cx * 8 + 4, cy * 8 + 4, 16, { colors = { 0xFFFFFF, 0xFFD050 }, gravity = 0.05 })
          lib.jingle({ { "E5", 0.08 }, { "B5", 0.15 } })
          lib.best("record", s.coins)           -- saved only if it is a record
          cam:shake(2, 0.2)
        end
        fx:update()
        cam:follow(hero.x, hero.y)
      end,
      draw = function(s)
        cls(0x203048)
        cam:apply()                             -- the camera (and the shake)
        cam:map()                               -- only the cells that show
        rectfill(hero.x, hero.y, 8, 8, 0xFF6040)
        fx:draw()
        camera()                                -- the HUD stands still
        lib.prints("coins " .. s.coins, 8, 8, 0xFFFFFF)
      end,
    },
  }, "title")
end

function _update()
  if pause:update() then return end             -- Start: the pause menu
  lib.update()
  S:update()
end

function _draw()
  S:draw()
  pause:draw()
end
```

Other useful things: `lib.script` for scenes written in a row (`lib.wait(1)`,
`lib.waitfor(...)`), `lib.tween` for titles and menus that come in, `lib.menu` for your
own menus, `lib.btnr` for buttons that repeat, `lib.rng(seed)` for worlds that are always
the same from the same seed, `lib.ray` to see whether an enemy sees the hero, `lib.dir8`
for 8-direction sprites.

## 12. Tips

- **Performance:** the cost is almost all in the Lua of `_update`/`_draw`; the drawing is
  in C. Avoid creating new tables in every frame in hot loops; update only the enemies near
  the camera; draw only the part of the map that shows.
- **Numbers:** Lua has integers and decimals; for screen positions use `math.floor` or `//`
  when you need whole pixels.
- **Randomness:** `math.randomseed(stat(3))` when the player presses a button (the frame
  number changes every match).
- **Debugging:** `log(...)` writes on the serial console; `stat(1)` is the last frame's time
  in ms, `stat(2)` the fps.
- **Do not write again what is there:** before writing a helper function look in bmlib
  (chapter 11); the assistant (F6) knows it.

## 13. Examples to start from

| Game | What it shows |
|---|---|
| `carts/pong` | the simplest: shapes, input, sounds, saves |
| `carts/snake` | a grid, game time, record |
| `carts/shooter` | sprites written in the code with `sset`, waves, particles |
| `carts/astrowing` | 3D: models, fog, a camera that tilts, horizon |
| `carts/hunt` | 320×180, a generated 2048×2048 map, lights, combat, boss |
| `carts/demo` | a real PNG sprite sheet and CSV map |
| `carts/kitchen` | a big game: sources in several files joined by `build.py`, 3D with meshes built in code, 1–4 players (`btn(i, p)`, `players()`), saves, and a host simulator (`tests/kitchen/sim.lua`) that plays by itself to find errors and measure the cost of each frame |
| `carts/village` | 3D models made with bm Studio and a villager animated with bm Animator (`models.bm`): `model()`, `animate()` with two animations blended, `bone3d()`, terrain without z-buffer, night with `lamp3d` and fog, pre-rendered sprites |
| `carts/titan` | big pre-rendered sprites (a 3D model made in Python becomes layered pixel art: one frame, many combinations of equipment), a big sheet with a palette (`--sheet8`), parallax, the states of a fighting game with per-frame hitboxes, a CPU opponent |
| `tests/gameapi/cart.lua` | every function of the map's layers, the tiles' flags, the named zones and bmlib, with its cases |
