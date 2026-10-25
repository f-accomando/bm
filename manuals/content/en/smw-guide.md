title: A platformer built from a reference: the SMW first level
accent: platformer
lang: en
kicker: bm example guide
subtitle: How one real game is built as a bm project: the .bme, the APIs it uses, the structure of the game and the pipeline of its assets.
footer: bm manuals - smw-guide

::: readme
**What this is.** A guided tour of `carts/smw/`, a personal port of the first level of a classic 16-bit platformer to the RGB30 (360x360, `.b16`). It is an example of a complete game written as a bm project: map layers with flags, sprite zones with boxes, `bmlib`, a fixed-rate game loop and a measured physics.

**How to use it.** Read the first three chapters to see the shape of the project. Then read the chapter of the part you want to copy: movement, enemies, blocks, assets.

**What it does not give you.** The graphics of the original game. The project builds them from the ROM of the person who owns the cartridge; they are never in the repository. The pictures of this guide show the same level with its own graphics drawn again.
:::

# The project

## What it is for

The port has two aims. It is a **benchmark**: the same level, the same physics and the same controls on the console show how fast the platform runs a real game. And it is a **worked example** of how a bm game is split into data (the `.bme` resources) and code (Lua).

![The start of the level](shots/smw-guide-01-start.png)

## The files

| Path | What it is |
|---|---|
| `carts/smw/src/game.lua` | the game: states, enemies, blocks, camera, drawing |
| `carts/smw/src/physics.lua` | Mario's movement, with the constants measured on the original |
| `carts/smw/build/build.py` | turns the extracted data into the `.bme` and the `.b16` |
| `carts/smw/build/sprites.py` | cuts the sprite zones and their boxes |
| `carts/smw/build/bg.py` | cuts the background layer |
| `carts/smw/bm.info` | the metadata: name, `360x360`, target `rgb30`, input `pad` |
| `carts/smw/guide/build.py` | the edition for this guide: the same level, redrawn graphics |
| `scripts/smw_*.py` | the capture scripts: they run the ROM in an emulator and write the data |
| `tests/smw/` | the physics test and the comparison with the emulator |

::: note
The ROM, the files it produces and the real graphics are kept out of git (`.gitignore`). The build scripts read them from `carts/smw/extracted/` on your machine.
:::

## How a cartridge is made

A `.bme` is the project of a game: the Lua code, the sprite sheet, the map layers and the zones in one file. A `.b16` is the same content for the RGB30. `scripts/mkbm.py` makes both from the same inputs; the build calls it twice:

```
python3 scripts/mkbm.py --lua main.lua --sheet sheet.png --sprites sprites.txt \
  --map map_main.csv --map col=map_col.csv --map bg=map_bg.csv --flags flags.csv \
  --info carts/smw/bm.info --targets rgb30 --input pad -o smw.b16 --b16
```

`python3 scripts/bmres.py info smw.b16` lists what is inside: the Lua size, the sheet, the map (640x54 cells), the three layers `main`, `col` and `bg`, the flags and the 40 sprite zones.

# The level as a map

## Three layers

The level is 20 screens wide. The map has 640x54 cells of 8x8 pixels, and three layers:

| Layer | What it holds |
|---|---|
| `main` | what the player sees in front: ground, pipes, blocks, coins |
| `col` | what is solid: one marker cell per 16x16 block |
| `bg` | the background (hills and trees); it scrolls at half the speed of the camera |

The `col` layer uses the flags of the map. Two marker cells at the end of the tile sheet carry them: flag 1 means *solid*, flag 2 means *platform* (a floor you can only land on from above, such as a ledge). The game tells `bmlib` which layer and flags to use:

```lua
lib.tiles({ layer = "col", solid = 1, platform = 2 })
```

After this, `lib.solid(x, y)` answers for any point of the level, in pixels.

## Blocks of 16x16

The original builds its levels from blocks of 16x16 pixels, each made of four 8x8 tiles. The build keeps that grid in a Lua table (`L16`, one number per block) and writes the four tiles of every block into the `main` layer. When the game changes a block (a bump, a break, a coin taken) it changes the table, the four tiles and the four cells of `col` together:

```lua
local function put16(cx, cy, id)
  L16[cy + 1][cx + 1] = id
  set16(cx, cy, M16[id] or { 0, 0, 0, 0 })     -- the picture: four mset calls on "main"
  set_col(cx, cy, id)                           -- the walls: four mset calls on "col"
end
```

The list `changed` remembers every block the player touched, so a restart puts them back.

## The camera

The picture of the original is 256x224. It is drawn whole in the middle of the 360x360 screen, inside a `clip`. `map()` draws a window of the layers from the camera position:

```lua
map(bu // 8, camy // 8, VX - (bu % 8), VY - (camy % 8), 33, 29, "bg")      -- the background first
map(mx, my, VX - (camx % 8), VY - (camy % 8), 33, 29, "main")             -- then the level
```

The camera follows Mario on x (`x - 108`) and on y, and stops at the borders of the level.

![The camera follows Mario](shots/smw-guide-06-camera.png)

# The sprites

## Zones and boxes

Every moving thing is a **sprite zone**: a rectangle of the sheet, with a number of frames, and boxes inside it. `sprites.txt` is the list. This is the entry for the walking Mario:

```
mario_walk 0 448 32 48 2 8
  hurt * 10 24 12 16
  box 3 * 10 36 12 4
  box 4 * 10 24 12 16
```

The line gives the name, the place in the sheet (`0 448`), the size of a frame (`32 48`), the number of frames (`2`) and their rate. The three lines under it are boxes, in pixels from the corner of the frame:

- `hurt` is where Mario can be hurt (12x16, at 10,24).
- `box 3` is the **feet**: a thin box at the bottom. Landing on an enemy with it is a stomp.
- `box 4` is the **body**: touching a shell with it kicks it.

The game draws a zone with `zspr(zone, x, y, frame, flip)` and reads its boxes with `zboxes(zone, frame, kind)`. A table `ZA` gives the corner of each zone from the place of the sprite, because the pictures are not all the same size as the thing they show.

## Contacts with bmlib

`lib.hits` does the contact tests. Each frame the game clears it, puts every body in as a zone, and asks for the contacts:

```lua
H:clear()
H:zone(mario, mz, mf, mcx, mcy, mflip, { team = "p" })
for _, b in ipairs(zboxes(mz, mf, 3)) do          -- the feet
  local bx, by, bw, bh = lib.box(b, mcx, mcy, mflip, 32)
  H:hit(mario, bx, by, bw, bh, { team = "p", kind = "stomp" })
end
-- every enemy: H:zone(k, zone, frame, x, y, false, { team = "e" })
local contacts = H:check()
```

Each contact says who hit whom and with which kind: the game turns a `stomp` into a squashed enemy and a bounce, and a body contact with a walking enemy into a death.

## Mirroring and animation

The zones hold one direction only. A mirrored picture is a flag of the draw call (`zspr(..., flip)`), so a walking Mario facing left costs no extra graphics. The animation of Mario is one rule: a distance counter (`anim`) grows with his speed, and a frame lasts 122 units of it, so he steps faster when he runs.

# Mario's movement

## The fixed tick

The original runs at 50 Hz. The console draws at 60 frames per second. The game keeps the logic at a **fixed 50 Hz tick** and draws between ticks:

```lua
acc = acc + 50 / 60
while acc >= 1 do
  acc = acc - 1
  tick(inp)
end
```

`_draw` interpolates the position between the last two ticks (`prev` and `mario`) with the leftover `acc`, so the motion is smooth at 60 frames per second and the logic stays exactly as fast as the original.

## Constants measured, not guessed

`physics.lua` holds the movement in a table of constants: acceleration, deceleration, the three speed limits (walk, run, run with a full P meter), the skid, the jump speed, two gravities, the fall limit. Positions are in pixels and speeds in 1/16 pixel per tick.

Two rules of the original are visible in the numbers:

- **The jump button changes gravity.** While it is held the gravity is lighter (3.4); after it is released it is heavier (about 6.8). That is the whole variable-height jump.
- **The P meter.** Running at full speed on the ground fills it; at the top the run limit rises. It drains slowly, in the air too, unless it is full.

![Running with the P meter full](shots/smw-guide-03-run-pmeter.png)

## One tick of movement

`Phys.step` moves first, with the speed of the last tick, and only then changes the speed. The test shows that the original does the same: the position is one tick behind the speed. The move is x then y, so a wall stops Mario on its edge and a floor stops him on its top. A ledge is a *platform*: he lands on it only when he comes down into its row.

```lua
function Phys.step(m, inp, solid, plat)
  move(m, solid, plat)                  -- the position, with the last speed
  -- ... then the speed: accelerate, skid, decelerate, the P meter
  if inp.jump and not m.held and m.ground then
    m.vy = -(Phys.JUMP + Phys.JUMP_SPEED * math.abs(m.vx))     -- a faster Mario jumps higher
  end
end
```

The function takes the solid test as a parameter, so the same code runs in the game and in the test.

## How the constants were found

The capture script plays the original in an emulator with scripted keys (walk, run, jump, turn, skid, duck...) and writes one line per tick: position and speeds. The test builds a small cartridge with `physics.lua`, runs the same keys in `bmhost`, and scores the difference. A search then adjusts each constant, one at a time, to lower the score. Over twelve scenarios the mean position error is under 0.8 pixel; the worst is about 3 pixels.

::: note
A longer run, the whole first part of the level with the same keys, stays within 5 pixels for the first 700 ticks. After that a corner contact on a step differs in one run and not in the other, and the two runs part: a small difference in a collision is enough. The constants cannot fix this.
:::

![Mid jump](shots/smw-guide-04-mid-jump.png)

# The game

## States

`game.lua` is a small state machine: `play`, `dying`, `clear`, `over` and `msg` (a message box). `tick` calls the step of the current state. The HUD shows lives, coins, dragon coins, time and score. The time counts down one every 35 ticks.

![The HUD](shots/smw-guide-08-hud.png)

## Enemies as data

Every enemy is a table with a kind and a state. A table `FOE` says, for each kind, how it steps, which zone and frame it shows, what a stomp does and what hurts Mario:

```lua
FOE.koopa = { step = koopa_step, zone = koopa_zone, stomp = koopa_stomp,
              hurts = function(k, mx) return k.state == "walk" or ... end }
```

So the loop that moves, draws and tests contacts does not know any enemy. To add one, add an entry to `FOE`. The level has the Koopa Troopa (red and green, with a shell that Mario can kick), a jumping plant that follows the path the original gave it, and two Chargin' Chucks that look, charge and wait, and fall after three stomps. The Koopas move with `lib.move`, and turn at a wall and at the end of a ledge.

## Blocks

A table `KIND` marks the blocks that do something when hit from below: a question block gives a coin, another gives an egg that hatches into a dinosaur, another grows a vine. The hit comes from the movement code: when Mario's head stops against a block, `move` leaves the place in `m.bump`, and `hit_block` reads the block there.

![Hitting a block](shots/smw-guide-07-bump.png)

Other blocks:

- **Turn blocks** break when Mario lands on them with a spin jump; four pieces fly and fall.
- **The blue P switch** swaps turn blocks and coins for 704 ticks, then restores them. `swap_pswitch` walks the level once and uses `put16`.
- **Message blocks** are solid sprites; a hit opens a box of text and the game waits for a button.

## The vine

The vine block makes a sprite head that rises one pixel a tick, with a leaf cell drawn under it. Pressing up near it makes Mario grab it: the normal physics stops, up and down move him at a fixed speed, left and right slide him, and a jump lets go. It is a good example of a *mode*: one flag (`mario.climb`) and one function (`climb_tick`) that runs before `Phys.step` and tells the game whether it took the tick.

## The goal and the end

The goal is a bar that moves up and down along a fixed cycle. Crossing it starts the `clear` state: a banner, the time turned into score, and a restart.

# The assets pipeline

## Why a pipeline

The graphics are not in the repository. A script reads the ROM and writes everything the build needs into `carts/smw/extracted/`. Anyone with their own cartridge can rebuild the same game. The scripts do this in order:

| Script | What it extracts |
|---|---|
| `scripts/smw_extract.py` | checks the ROM and decompresses its graphics |
| `scripts/smw_map16.py` | the blocks of the level and the tiles of each |
| `scripts/smw_sprites.py` | the poses of Mario, with their colours |
| `scripts/smw_spawns.py` | where each enemy starts |
| `scripts/smw_diff.py` | the exact picture of a sprite, taken by hiding it between two frames |
| `scripts/smw_bg2.py` | the background layer |
| `scripts/smw_solid.py` | which blocks are solid and which are platforms |
| `scripts/smw_vine.py` | the vine, its growth and the climbing poses |

Two techniques are worth knowing. The **diff capture** plays two frames from the same state, one with a sprite and one without it, and keeps the pixels that differ: the picture is exact even when the game builds it from several parts. The **solidity test** puts Mario against each kind of block in the emulator and records what happens, instead of reading a table.

## From pictures to a sheet

`sprites.py` cuts every picture into a zone: it puts each frame in a cell of a fixed size, asserts it fits, adds the boxes, writes the line of `sprites.txt` and returns the Lua table `ZA` of anchors. The tile sheet and the zone sheet are one image, so the cartridge has a single `SHEET`.

The background has 637 distinct cells. `bg.py` folds the picture to its period, fills the holes, cuts it into 8x8 cells and writes them as a third map layer.

## The edition of this guide

`carts/smw/guide/build.py` builds the same game with the graphics **redrawn** by role: the shapes are the same, the colours are picked by role and the shading is procedural, so no pixel of the original is in the output. The pictures in this guide come from it.

# Try it

1. Build the guide edition: `python3 carts/smw/guide/build.py`. It writes `build/smw-guide/guide.b16`.
2. Run it in `bmhost` and take pictures: `python3 tests/smw/guide_shots.py`. The shots go to `manuals/shots/`.
3. Open `carts/smw/src/physics.lua` and change `GRAV_HOLD` from 3.4 to 6.8. Build again and jump: the button no longer changes the height.
4. Open `game.lua`, find `FOE.koopa` and change what `hurts` returns for a walking Koopa. Build and walk into one.
5. Press `Select` in the game to see the frame cost, and read the `bench` lines in the log.

::: try
Change the physics and see the result.
:::

# Open points

- The real frame time on the RGB30 is not measured yet: on a PC the level takes about 0.05 ms a frame on average. [TO FILL: the number from the console, from the `bench` log lines]
- Big Mario, power-ups, riding the dinosaur, the other moves of the Chargin' Chuck and the sound are not in the port.
