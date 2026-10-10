# The console tools for `.bm` resources

Italian version: [README-IT.md](README-IT.md).

The resources of `.bm` cartridges (3D models, skeletons and animations, sprite sheet pixel
art, meshes in the code) are made **on the console**, in the **Dev** tab, with the
keyboard, the gamepad or the mouse (`require "bmui"`: the mouse does what the keys do, the
right button opens a menu with the page's keys): **bm SDK** (the hub of the project), **bm Studio** (tile-based
3D models), **bm Animator** (skeletons, animations, pre-rendered sprites), **bm Mesh**
(vertices and faces, including those of the meshes the code builds) and **bm Pixel** (the sprite
sheet). They work on the project, the `.bme` (they open it, change it, save it in its place
on the SD), they don't touch the game code, the map or the sections they don't know, and from
the menu of one you move to another on the same file.

On the PC there are command-line tools for the same files: `scripts/mkbm.py --models`
(a `.glb` or a `.bm` of models into a cartridge), `scripts/bmmesh.py`,
`scripts/bmres.py` (the resources), `tools/bmreduce.py`, `tools/cutout2mesh.py`,
`tools/meshy2mesh.py`, `tools/local2mesh.py`, `tools/img2mesh.py`.
The files the tools work on are **projects** (`.bme`); a **game** (`.bm`) is opened
to read it and take its pieces, and saving it makes an editable copy (see *Projects and
games* below).

## Models in a game

```lua
local casa
function _init()
  casa = model("house")            -- a mesh, like those of mesh()
end
function _draw()
  cls(0x1c2030)
  zclear()
  camera3d(0, 4, -8, 0, -0.4)
  light3d(-0.4, 0.8, -0.5, 0.4)
  draw3d(casa, 0, 0, 0, 0, time() * 0.5)
end
```

- `model(name)` (or `model(n)`, from 1) gives the mesh, `nil` if it isn't there; `models()` the
  list of names; `bounds3d(m)` the box `x0, y0, z0, x1, y1, z1` (for centering or for
  collisions). Reference: [docs/API.md](../docs/API.md) ([API-IT.md](../docs/API-IT.md) in Italian).
- The origin of the grid (the three colored lines) goes at the point `x, y, z` of `draw3d`; one
  square is one unit.
- Textured faces use the cartridge's sprite sheet: if the game changes the sheet
  with `sset`, the models change too.

**On the Pi**: copy the `.bm` into `carts/` on the SD (or send it with `tools/bm_net.py`).

**For a game in the repository** (built by `make`): put `models.bm` (a `.bm` with
the models and skeletons, saved by the console's bm Studio or bm Animator) or `models.glb`
in `carts/<game>/`; `make` puts it into the cartridge with `mkbm.py --models`. If the game doesn't
have its own `sheet.png`, the sheet is the one of `models.bm` or of the `.glb`. Complete example:
`carts/village` (Studio Village: the houses, the trees and the villager with its three animations
are in `models.bm`).

## Limits

- A model has at most **65535 corners** and 65535 triangles (until 2026-10-05 they were 4096
  and 16384, and an earlier kernel rejects a bigger model);
  the console draws about **1200 triangles at 60 fps** per scene.
- Pixels are solid or transparent (alpha < 128 = transparent), as for sprites.
- Repeating textures (uv outside 0..1) are not supported: bm stretches the edge.
- Each corner follows **one** bone (no mixed weights); 64 bones per model, 255 animations,
  1024 keyframes per animation; a keyframe is always the whole pose.
- The **texture margin** (0.25 px, in the MESH section) moves the texture corners
  of each face slightly inward, so that on the Pi the neighboring tile in the
  sheet doesn't show along the edges.

## On the console: bm SDK, the hub of the project

**bm SDK** (`carts/editor/main.lua`, built into the kernel, **Dev** tab; from a game
**X** → *Open in the SDK*; from the monitor `e`) is the point where a game starts and from which
the other programs of the suite are reached on the same file.

<p>
  <img src="../docs/img/sdk.png" width="32%" alt="bm SDK: the project page of Studio Village">
  <img src="../docs/img/sdk-3d.png" width="32%" alt="bm SDK: the 3D page with the assistant's animated dog">
  <img src="../docs/img/sdk-devkit.png" width="32%" alt="bm SDK: the dev kit after a test run">
</p>

It looks like bm Studio
and bm Animator (panel on the left with lists under gray titles, two lines above the
view with the name in orange, the keys at the bottom, the colors of bm Mesh) and uses their
library for the lists, the 3D view and the MESH and ANIM sections (`require "bm3d"`). Pages
with the F keys (Y + left/right on the gamepad), Esc the menu, **F12** held the keys.

- **F1, the project**: on the left *OPEN IN* (1 bm Code, 2 bm Pixel, 3 bm Studio, 4 bm
  Animator, 5 bm Mesh, 6 bm Sound: the SDK saves and opens the program on the same file; in
  its menu **Back to bm SDK** saves and returns) and *PROJECT* (t title, a author, r screen, b
  target `.bm`/`.b16`, n a template). On the right what is in the file: lines, KiB and **tokens**
  of the code, the drawn cells of the sheet, those of the map, the models, the
  skeletons and the animations, the sound bank; the numbers of the last test run; how to ask
  the assistant.
- **F1 again, the dev kit**: the tokens and the biggest functions, the memory the game's
  data takes while it runs (sheet, map, models, skeletons, sounds, z-buffer: the
  same sum as `stat(13)`; Lua is added on top), the saved file against the **8 MiB**
  of a `.b16` ([docs/B16.md](../docs/B16.md) §8.5) and, with the `.b16` target, the lines that
  format won't have (`math.random`, `time()`, files...). After a test run (**F5**) its
  numbers: fps, average and maximum ms of `_update` + `_draw`, frames over 16.7 ms,
  the maximum RAM (Lua + data), the instructions of the heaviest frame, the triangles and
  who drew the 3D (`cart_arg().run`). In games the same numbers come from F11
  (the overlay: fps, ms, Lua, RAM, tokens) and from `stat(11)`–`stat(14)`.
- **F2, the code**: the quick editor (bm Code, with tabs, is one key away: 1), with
  syntax colors and the 3D functions in teal; the tokens in the status line.
  Ctrl+G goes to the error of the last test run, **F9** has the assistant explain it.
- **F3, the sprites**: the chosen sprite enlarged, the sheet, the palette (space
  draws, x picks the color, f fills, z 8×8/16×16, Tab chooses on the sheet, h/v
  mirror, u undoes); **F3 again, the map** (space places the tile, Backspace
  clears, f fills, Tab chooses the tile). For all the tools: bm Pixel (2).
- **F4, the 3D**: the project's models in a list and the animations of the chosen one;
  the model turns on the grid with the animation playing (up/down the model,
  left/right the animation, space pauses, q/e/w/s the camera, + − zoom); **i**
  writes into the code the lines to load it (`model`) and draw it (`animate`, `draw3d`)
  and the view's `camera3d`; Enter, a, m open bm Studio, bm Animator, bm Mesh.
- **Ctrl+N, a project from a template**: Empty 2D, Platform 2D (hero, ground, map with
  platforms), Top-down 2D (walls and coins in the map), Shooter 2D (ship and enemies in
  waves), 3D scene (floor, cubes, ball with shadow, following camera), 3D with
  models (the project's models turning, animated). Each with the code, the sprites and
  the map it needs: F5 tries it right away.
- **F6, the assistant**: on the project page the **guides** to make a 2D or 3D game
  step by step (the `guide` mode: "come faccio un platform?", "how do I start a 3D game?";
  Enter puts their code into the code), in the code the functions and the examples, on the
  sprites a pixel art base in the chosen cell, in the 3D a model with a skeleton that
  goes into the project (MESH and ANIM sections, saved with the rest).

The **menu** (Esc): Continue, New project (the templates), Open, Save, Save as (8.3 name in
`/carts`), Build the game .bm (Ctrl+B), Try the game, Exit. Saving uses `cart_save`:
code, sheet, map, cover, sounds, models and skeletons.

**Projects and games** (`src/bm/project.h`, `docs/API.md`): all the tools of the suite
(SDK, bm Code, bm Studio, bm Animator, bm Mesh, bm Pixel, Sound) change only **projects**
(`.bme`); a **game** (`.bm`, `.b16`) they only read. With a game open, the first
save asks *Save an editable copy: NAME.BME?* and from there the tool works on the copy
(the second value of `cart_save`/`cart_write`/`cart_put_audio`); a new project is
`GAME.BME`. *Build the game .bm* (`cart_build`) writes the project's game in `/carts` and
replaces it at every build. In the bm menu projects are in Dev with the label "Project".
The stand-ins of the PC tests follow the same rules (`tests/studio/project_rules.lua`). Tests: `tests/studio/sdk_host.lua` (in `make
test-studio`: every game template runs 400 frames on the stand-ins), QEMU
`test_editor` and `test_sdk_suite` (Studio Village: the 3D page, the
assistant's saved model, bm Studio and back).

## On the console: bm Studio and bm Animator

On the console there are the same two programs, with the same names: **bm Studio**
(`carts/studio/main.lua`, the models) and **bm Animator** (`carts/animator/main.lua`,
skeletons, animations and sprites). They are cartridges built into the kernel, in the **Dev** tab;
from a game they are opened with **X** on the cover, **Open in bm Studio** or **Open in bm
Animator** (from the monitor the keys `3` and `6`), and from the menu of one you move to the other on the
same file (*Open in bm Animator*, *Open in bm Studio*). They read and write the
MESH and ANIM sections of the `.bm` (`src/bm/bm.h`), and a game without models can receive some. They are used with the keyboard or with the
gamepad (Bluetooth or USB); the mouse (M32) is there in the menu, but Studio and Animator don't use it
yet.

Pages are chosen with the F keys (or Y + left/right on the gamepad), the menu with Esc
(Y + B); holding **F12**, or with **?**, the page's keys appear. In all the
3D views + and − zoom and **Alt + arrows** (on the gamepad X + d-pad) turn the camera.

They look like the other console apps (bm Mesh, bm Pixel): on the left a panel
with the lists, above the view two lines that say what is there and what is being done, at the bottom
the keys; chosen things in yellow, the one under the pointer in light blue.

**bm Studio**, F1 **build** — in the panel on the left the tools, the brush's tile or color
(with the sheet around it) and the model (faces, triangles, vertices: it warns beyond
the 1200 triangles of 60 fps):

- **1 block** and **2 tile**: a cell-shaped cursor
  moves with the arrows on the plane and with PgUp/PgDn in height, always relative to the view
  (**q e** turn the view by 45°, **w s** tilt it). Space places, Backspace removes:
  two adjacent blocks have no wall between them, and removing one brings back the
  neighbor's wall; the tile goes on one side of the cell (**f**: floor, walls, ceiling). **Tab**
  (or Y) opens the sheet: tiles of 8, 16 or 32 pixels (**z**), also **several tiles together**
  (**a d w s**: a door two tiles high is placed in one go), or a color (**c**); **r**
  and **h** rotate and mirror the tile, **x** takes it from a face.
- **3 select**: the arrows bring the pointer to the nearest face in that direction
  on screen; space selects it, **a** all, **c** the connected ones (a whole object).
  The selected faces: **g** moves them (arrows, PgUp/PgDn; Tab changes the step from 1 to 1/16;
  Enter leaves them there, Esc puts them back where they were), **r** rotates them, **t** flips them, **m**
  mirrors them, **n** shows the other side, Enter puts the brush's tile on them, **u** rotates the
  texture, **,** **.** halve and double, **d** (Ctrl+D) copies them and moves the copy,
  Del deletes them, **o** moves them into a new model.
- **4 vertex**: the pointer goes on the corners; space selects them, **g** moves them (roofs,
  ramps, free shapes), **m** merges several into one.
- **5 paint**: the face under the pointer, Enter: its tile large in the panel, and
  you paint pixel by pixel (space; **e** transparent, **i** picks the color, **c** the
  palette); the model changes while you paint. The painted sheet is saved with the model.
- **v** changes the view (lighting, flat colors, wireframe), **b** also shows the faces
  seen from behind (darker), **z** goes back to the model.

F2 **models**: the models of the file, with their look; **n** new, **r** renames, **d**
duplicates, Del deletes (twice), PgUp/PgDn change the order, **i** the texture
margin, **-** reduces the triangles (asks how many; half by default): the kernel's
reducer (`src/bm/decimate.c`, edge collapse with quadrics) keeps borders, color
lines and texture seams, the skeleton follows the vertices, Ctrl+Z undoes. To
fit a heavy model (an imported `.glb`, a meshy2mesh model) into the Pi's 1200
triangles. On the PC `tools/bmreduce.py CART.bm --faces 1200` does the same. **m** (or
"Model from picture..." in the menu) makes a model from an image, see below. The menu also has
the cartridge's title and author.

**A model from an image, on the console.** Models page, **m**: three modes.

- **cutout**: the outline of the image (transparent background, or the color of the corners)
  becomes a cutout with some thickness, like a paper figure: the image in front,
  mirrored behind, the edge colors on the sides. Done on the console, without network.
- **lathe**: the half outline turned around the vertical axis (vases, towers, rockets,
  pawns), with the image projected in front. This one also without network.
- **meshy.ai**: a neural image-to-3D service (the first is [Meshy](https://www.meshy.ai),
  others are added to the table in `src/net/img3d.c`): bm Studio sends the image, follows
  the job and takes the complete model, seen from every side.

In all cases the texture goes onto the project's sprite sheet if it is still empty, otherwise
the faces take the colors of the texture; the model is 2 blocks high and fits in the 1200
triangles. The images (`.png` or `.jpg`, one subject on a clean background, preferably from the front)
go in the `pics/` folder of the SD. For the service you also need:

1. The console connected to WiFi (Settings > Network) and on the SD `bm/ca.pem` (it is in
   `dist/`: the certificates for https).
2. The service key in `bm/config.txt` on the SD, one line: `meshy_key=msy_...` (it is
   created on meshy.ai, Settings > API keys; models cost credits).

With the service the status line says how far the job is (a check every 5 seconds,
a couple of minutes in all; Esc abandons it); without a key or without network the message says
what is missing. In every case at the end the model appears in the list with the image's name
and Ctrl+S saves it. On the PC `tools/cutout2mesh.py hero.png -o hero.bm`
(`--lathe`, `--depth`, `--segments`) and `tools/meshy2mesh.py` (see below) do the same.

**Without a key or the cloud, on the PC: `tools/local2mesh.py`.** The same models with an open
image-to-3D network running on your computer: TripoSR (fast, an NVIDIA card with 6 GB
or the CPU alone, slow) or Hunyuan3D 2 (better, NVIDIA with 12 GB or more). Once:
`tools/local2mesh.py --install triposr` (clones the program into `~/.bm/local3d`, makes a venv
with PyTorch; the weights come from huggingface.co on first use, a few GB). Then
`tools/local2mesh.py hero.png -o hero.bm` (`--backend hunyuan3d`, `--faces`, `--height`,
`--flat`, `--name`): the model's `.glb` goes through the same conversion as meshy2mesh,
texture onto the sheet and reducer. With `--backend command --command "tool {image} --out
{out}"` any other tool that writes a `.glb` works. `--check` says what is there.

**bm Animator**:

- F1 **play**: the player. The models in a list, with vertices, triangles and bones; the camera
  turns by itself (a d w s to turn it by hand). Animations are chosen with
  left/right and start with space; `,` and `.` step forward and back one
  frame, `<` and `>` change the speed, **k** shows the skeleton, **b** blends
  the animation with the next one (25, 50, 75 %: the same `animate()` as games).
- F2 **rig**: **n** makes the skeleton (one bone, root, from the bottom of the model) and then adds
  child bones of the chosen one; up/down chooses the bone, **w a s d r f** move its tail
  (or the head, with Tab) by 1/8 (uppercase: 1/32): joints at the same point
  move together. **m** mirrors the bone and its children to the other side (names `.L` / `.R`,
  also in the animations), Enter renames it, **p** chooses the parent, x deletes it. The
  skin: **k** gives each face to the nearest bone (rigid parts), **K** each corner
  (the model stretches at the joints); **v** shows the faces with the bones' colors and lets you
  choose them with the pointer (space, **c** the connected ones), **a** gives them to the bone (PgUp/PgDn).
- F3 **animate**: **n** makes an animation; up/down chooses the bone, left/right the
  frame (12 per second, like bm Animator). w/s, a/d, q/e rotate the bone by 15° around
  x, y, z (uppercase: 5°), with **g** they move it: each turn is a keyframe at that point.
  **k** adds a keyframe, x removes it, **,** **.** go to the previous and next keyframe,
  **(** **)** move it by one frame; **c v** copy and paste the pose, **r** puts
  the bone back at rest, **m** mirrors the pose; **o** shows the bones of the previous and next keyframes
  (onion skin); **l** loop on/off, **i** linear / smooth / step, `<` `>` the duration. On
  the left the animations: PgUp/PgDn change them, **n** new, Ctrl+D duplicates, Enter
  renames, Backspace twice deletes.
- F4 **sprites**: the animation drawn into sprites by the console's 3D engine, for
  2D games: frames, size (16…128), directions (1, 2, 4, 8), how much from above,
  flat or perspective camera, light, outline, reduced colors (32, 16, 8), fill;
  the preview cycles through the directions. Enter puts the grid into the sprite sheet (below
  what is there, enlarging it if needed) and shows the `sspr()` code to draw it.

The **menu** opens the `.bm` files on the SD, saves (Ctrl+S, or *Save as* with an 8.3 name in
`/carts`), **tries the game** (F5: you play the saved file, then come back to the same
page); bm Studio also makes a new project (with bm Studio's initial tile sheet
and the code of the model viewer). `[` and `]` (or Y + up/down) change model.
Ctrl+Z and Ctrl+Y (Y + A on the gamepad) undo and redo, including painted pixels and
sprites placed into the sheet.

How they work: models and skeletons are Lua tables; at every change the cartridge
rewrites that model's part of the MESH and ANIM sections (`string.pack`, the format of
`src/bm/bm.h`) and passes them to the kernel with `cart_data()`, which checks them: `model()`,
`animate()` and `bone3d()` always draw and move what will be saved. The code
shared by the two programs (format, project, undo, menu, tabs, the keyboard
pointer) is the kernel library `src/script/bm3d.lua` (`require "bm3d"`). They save with
`cart_write`: in the file only MESH and ANIM change (and the sheet, if it was painted or has
received sprites); a `.bm` opened and saved without changes stays identical byte for byte.

A `.glb` goes into a cartridge from the PC with `scripts/mkbm.py --models` or
`tools/meshy2mesh.py --glb`; each corner follows one bone (no mixed weights) and a keyframe is
the whole pose.

## On the console: bm Mesh

**bm Mesh** is the mesh editor, built into the kernel (`carts/mesh/main.lua`): **Dev**
tab, or **X** on the cover of a game → **Open in bm Mesh** (from the monitor, the
key `4`). It reads three kinds of meshes of a `.bm`, marked in the list with a letter:

- **M**, the **models** of the MESH section (those of bm Studio, with the skeleton of bm
  Animator if they have one): in the game `model("name")`;
- **C**, the meshes **in the code** written by bm Mesh: functions `mesh_name()` at the end of
  `main.lua`, between the lines `-- [bm Mesh begin]` and `-- [bm Mesh end]`; in the game
  `local m = mesh_name()` (in `_init` or later) and then `draw3d(m, ...)`;
- **G**, the meshes the **game code** builds with `mesh()`, `mesh_sphere()` and
  `mesh_cube()`, like the ships and rings of Astro Wing. The kernel finds them by running the
  code separately (`cart_meshes()`: no files, screen or sound, an instruction limit)
  and gives each one the name of the variable that holds it (`M.ship` → `ship`).

Two pages (F1 list, F2 edit; on the gamepad Y + left/right) and the menu with Esc;
holding **F12**, or with **?**, the keys appear.

- **F1, the list**: up/down chooses the mesh, which turns in preview with vertices, triangles and
  skeleton. **m** copies it as a **model** (from mesh to model), **c** as **code** (from
  model to mesh: `mesh_name()`), **r** renames, **d** duplicates (a model with its
  skeleton), **Del** twice deletes, **n** makes a new model (a cube; with F3 also
  plane and sphere). The game's meshes can't be renamed or deleted: it's its code
  that makes them (bm Code edits it).
- **F2, the edit**: a **pointer** (arrows, faster when held; on the gamepad the
  d-pad) points at the vertex or face under it (**Tab** switches between vertices and faces;
  **n** and **b** move it to the next or previous, also behind). **Space**
  adds to or removes from the selection, **Enter** selects only that, **a** all or nothing,
  **l** everything connected. Then:
  - **g** moves, **r** rotates, **t** scales: the arrows and PgUp/PgDn change the value
    (left/right and PgUp/PgDn on the two axes of the view, up/down in height; **x y z**
    only on that axis, **n** along the normal), `,` and `.` the step (0.01–1; 1–90°;
    ×1.01–2), **Enter** confirms, **Esc** cancels;
  - **x** extrudes the selected faces (then they move along the normal), **d** duplicates them,
    **m** mirrors the selection (left-right on screen), **M** copies it to the other
    side of 0 (for symmetrical models: the vertices on 0 stay shared);
  - **j** makes a face on the 3 or 4 selected vertices (in order, towards the camera), **k**
    merges the selected vertices into one, **K** welds those at the same point (with flag 4
    of `draw3d` the faces around look smooth), **u** splits each face into 4, **i**
    flips them, **Del** deletes;
  - **p** colors the faces, **o** takes the color from the face, **c** the palette;
  - the view: q e turn, w s tilt, + − zoom, **f** frames the selection, 1 3 7 0 the
    straight views; on the gamepad X + d-pad. Ctrl+Z and Ctrl+Y undo and redo.

  Editing a **G** mesh first makes a copy of it as a model (the game code isn't
  rewritten): the game uses it with `model("name")`.

The **menu** opens another `.bm`, saves (Ctrl+S) or saves as (8.3 name in `/carts`) and
**tries the game** (F5: you come back to the same page). Saving uses
`cart_write(path, {sections = {[8] = MESH, [9] = ANIM}, lua = ...})`: only the
models and the bm Mesh block in the code change; sprite sheet, map, cover, sound bank
and the rest of the code stay byte for byte. A model with a skeleton keeps it: the bone
of each vertex follows added vertices (the one of the vertex they come from) and removed ones, and
bones and animations stay those of bm Animator.

Compatible with the other apps: the models are the ones bm Studio, bm
Animator, `mkbm.py --models` and the kernel read and write; the meshes in the code have the format
of the `mesh()` tables (vertices, then `a, b, c, color` with `-1` for the texture, then
the sheet coordinates) and open in bm Code like the rest of the code.

## On the console: bm Pixel

**bm Pixel** is the pixel art editor, built into the kernel (`carts/pixel/main.lua`):
**Dev** tab, or **X** on the cover of a game → **Open in bm Pixel** (from the monitor,
the key `5`). It works on the **sprite sheet** of the `.bm`, the same one used by games
(`spr`, `sspr`, `map`), the SDK and bm Studio (the textures of the models). Three pages
(F1–F3, on the gamepad Y + left/right), the menu with Esc; holding **F12**, or with
**?**, the keys appear; **Tab** (on the gamepad X) opens the list of the page's commands.

- **F1, the drawing**: the chosen sprite, enlarged (8×8, 16×16, 32×32, 64×64 or 128×128
  pixels: **z** changes the size, PgUp and PgDn go to the previous and next sprite), with the
  grid (**t**). A pointer moves with the arrows (or the d-pad) and **space** (or A)
  uses the tool; holding space the pencil draws a line.
  - **b** pencil, **e** eraser, **g** fill, **i** the color of a pixel (a new
    color goes into the palette), **l** line, **u**/**U** empty/filled rectangle, **o**/**O**
    empty/filled oval, **m** selection: for lines, rectangles, ovals and selections space
    fixes one corner, the arrows bring you to the other, space again draws.
  - With a selection (or the whole sprite): Ctrl+C copies, Ctrl+X cuts, Ctrl+V pastes
    (the block floats: it moves with the arrows and is placed with space or Enter; its
    transparent pixels leave what is underneath), **Enter** lifts the selection to
    move it, **h**/**v** mirror, **r** rotates by a quarter, Del deletes;
    Shift+**w a s d** scroll the sprite by one pixel (what goes out comes back in
    from the other side).
  - **y** draws mirrored (left-right), **,** and **.** change color, **x** goes back
    to the previous color, **1**–**9**, **0** the first ten; color 0 is transparent.
  - **Animation**: the sprite and the ones following it in the sheet are the frames
    (**+**/**−** how many, **<**/**>** the speed, **p** pauses or resumes): the box on the right
    plays them, next to the sprite at actual size. **k** is onion skin: the previous
    frame shows dotted under the transparent pixels.
  - **F6**: the assistant (M30) draws the base of a sprite from a word ("slime",
    "moneta", "astronave"…) with the palette's colors; it floats like a paste.
- **F2, the sheet**: the whole sheet (zoom with + and −), the arrows choose the sprite (in the
  chosen size), Enter draws it; Ctrl+C and Ctrl+V copy a sprite to another place,
  Del clears it, **R** changes the size of the sheet (multiples of 8 up to 4096: what
  fits stays). At the top the sprite's number and the `spr()` call that draws it.
- **F3, the palette**: up to 256 colors. Arrows and Enter choose the color to
  draw with, **e** edits it (R, G and B with the arrows, < and > by one; next to it the color as
  the console shows it, in RGB565), **a** adds one, Del removes it, **[** and **]**
  move it, **s** sorts by hue, **f** takes the colors used in the sheet, **1** and **2**
  set the palettes of the SDK and of bm Studio; **x** (**X**) changes the color being
  drawn with into the chosen one, in the whole sprite (in the whole sheet).

The **menu** opens another `.bm`, makes a **new sheet** (256×256; saving it with *Save as*
it becomes a cartridge with code that shows the sheet), saves (Ctrl+S), saves as,
**tries the game** (F5: you come back to the same page), changes the size of the sheet.
Ctrl+Z and Ctrl+Y (Y + A on the gamepad) undo and redo. For each file it remembers the
sprite, the size, the animation and the page.

Saving uses `cart_write(path, {sheet = true, palette = ...})`: in the file only
the sheet changes (code, map, cover, sounds, models and skeletons stay byte for byte, and a
file with a long name keeps it). The sheet becomes a **SHEET8** section when it has at most
256 colors, with bm Pixel's palette first: reopening the file the same
palette comes back, and the SDK, bm Studio, bm Animator, `mkbm.py` and games read it too. The
console keeps 16 bits per pixel (RGB565): a pixel that hasn't been redrawn keeps the 24 bits
it had in the file, a redrawn one takes those of the palette
color. Big sheets open too (the one of Titan Clash, 2048×3448, starts
scaled down to 1/4); writing them takes a few seconds, and meanwhile the screen says
"saving ...".

## Tests

```sh
make test-studio      # the console's bm Studio, bm Animator, bm Mesh, bm Pixel and SDK on the PC, their files reread by Python and the kernel
```

`make test` includes `test-studio` and, in QEMU, `test_models`, `test_sdk_keeps_models`,
`test_village` and `test_animation` (an arm rising on the emulated console).

The console's bm Studio and bm Animator have a test bench on the PC
(`tests/studio/tools3d_host.lua`, inside `make test-studio`, with the console's Lua 5.4):
bm's APIs replaced (a folder acts as the SD; the drawings check their arguments, the
chip keys their names), keys and gamepad simulated, and the whole path: bm Studio
on the village, blocks, tiles (also several together), selection and moves, vertices,
painting, views, models, undo, save and reopen; then bm Animator on the same
file (`cart_tool`): bones, mirror, names, parent, skin, keyframes, onion, animations,
sprites into the sheet, saving. The files are reread by Python
(`tests/studio/check_files.py`: the block's faces turned outward, the red tile on the
floor and the one on the wall, the skeleton and the animation) and by the kernel's parser
(`test_bm`). In QEMU, `test_studio_animator` opens the village in bm Studio from the game's
options, tries the tools, builds, saves, tries the game, switches to bm Animator,
animates, saves and puts the villager's sprites into the sheet.

bm Mesh has its own test bench on the PC (`tests/studio/mesh_host.lua`, in `make
test-studio`): `cart_meshes()` replaced by a `load` of the game code with the same
rules as the kernel, and the whole path (the 13 meshes of Astro Wing, mesh → model equal to
what the code gives to `mesh()`, mesh → code that rerun gives the same meshes,
moves, undo, subdivision, mirror, new faces, merging, color, extrusion,
duplication, the villager with the skeleton after moved and deleted vertices, save as).
The files it writes are reread by `check_files.py`, by `bmmesh.py` and by the kernel
(`test_meshcap`: each skeleton matches its model). `test_meshcap` also tests the
real capture (`src/bm/meshcap.c`) on Astro Wing, Texture Room and Chaos Kitchen. In QEMU,
`test_mesh` opens Astro Wing from the options, copies the ship as a model, moves its vertices,
copies it as code and saves.

bm Pixel has its own test bench on the PC (`tests/studio/pixel_host.lua`, in `make
test-studio`): the sheet in a table with the colors as the kernel keeps them, a `cart_write`
that writes it as the kernel does, and all the tools (pencil and stroke, line, rectangle, oval,
fill, eyedropper, mirror, selection, copy, paste, lift and move,
mirroring, rotating, scrolling, undo and redo, the assistant, the palette, the taller
sheet, save, reopen, new sheet, try the game). The files are reread by `check_files.py`
(the pixels not redrawn with their 24 bits, the palette, models and skeletons intact) and
by the kernel (`test_meshcap`); `test_bm` tests the kernel's SHEET8 packer, the sheet in place
of the old one in `bm_rewrite_with` and the enlarged `sspr`. In QEMU, `test_pixel` opens Studio
Village from the options, draws and saves: in the file only the drawn pixels change, the others
stay identical byte for byte; `test_pixel_big` does the same with the sheet of Titan Clash
(2048×3448): it changes a single pixel and the palette stays the same.

## Structure

```
carts/editor/main.lua        bm SDK, the hub of the project (built into the kernel, Dev tab)
carts/studio/main.lua        bm Studio (built into the kernel, Dev tab)
carts/animator/main.lua      bm Animator (built into the kernel, Dev tab)
src/script/bm3d.lua          the code shared by the two (require "bm3d")
carts/mesh/main.lua          bm Mesh, the mesh editor (built into the kernel, Dev tab)
src/bm/meshcap.c             cart_meshes(): the meshes a .bm's code builds
carts/pixel/main.lua         bm Pixel, the pixel art editor (built into the kernel, Dev tab)
scripts/bmmesh.py            MESH, ANIM and .glb for mkbm.py
tests/studio/tools3d_host.lua    bm Studio and bm Animator on the PC, with bm's APIs replaced
tests/studio/mesh_host.lua       bm Mesh on the PC
tests/studio/pixel_host.lua      bm Pixel on the PC
tests/studio/sdk_host.lua        the bm SDK on the PC (every game template runs 400 frames)
tests/studio/check_files.py      their files reread by the build's Python
tests/bm/test_meshcap.c          the capture on the real cartridges
```
