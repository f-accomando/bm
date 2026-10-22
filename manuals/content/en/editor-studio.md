title: bm Studio: 3D modelling
accent: 3D modelling
lang: en
kicker: A bm editor manual
subtitle: Build 3D models for your game on the console, from tiles and blocks, and use them in your code.
footer: bm manuals - bm Studio

::: readme
**What this is.** The manual of bm Studio, the console program that makes the 3D models of a bm game. It is written from the program's own code and docs.

**How to use it.** Read the first three chapters once, then follow the tutorials with the console in your hands. Anything in a bordered box with a shadow is meant to be copied as it is.

**What is missing.** Every gap is marked with a highlighted TO FILL tag; nothing is invented.
:::

# What bm Studio is

bm Studio builds 3D models out of blocks and tiles, right on the console. You place a cube, stick a piece of your sprite sheet on its walls, move a corner to make a roof, and the model is ready to be drawn by your game with `draw3d()`.

## What it connects to

- **The project.** bm Studio reads and writes the MESH section of your project (a `.bme` file). It does not touch the game code, the map or the sections it does not know.
- **bm Animator.** Skeletons and animations are made there. In bm Studio's menu, *Open in bm Animator* opens the same file in it.
- **bm Mesh.** The analytic editor of vertices and triangles, including the meshes your code builds. It works on the same file.
- **bm Pixel and the sprite sheet.** The textures of the faces are pieces of the project's sprite sheet. bm Pixel draws it.
- **bm SDK.** The hub of the project. In its F1 page the *OPEN IN* list has bm Studio as number 3, and the menu of bm Studio has *Back to bm SDK* when the SDK opened it.

## Good to know

- bm Studio is a cartridge built into the kernel. You find it in the **Dev** tab of the menu.
- You use it with the keyboard or the gamepad (Bluetooth or USB). The mouse also works when one is connected.
- Hold **F12**, or press **?**, and the keys of the page you are on appear on the screen.

# Opening a project or a game

bm Studio changes **projects** (`.bme`). A **game** (`.bm` or `.b16`) is only read: you can open it to look at its models, and the first save makes an editable copy.

## Ways to start

| From | What you do |
|---|---|
| The menu | Open bm Studio in the **Dev** tab |
| A game in the menu | **X** on its cover, then *Open in bm Studio* |
| bm SDK | F1 page, *OPEN IN*, number 3 |
| bm Animator | In its menu, *Open in bm Studio* |

## Inside bm Studio

- Press **Esc** to reach the menu. *Open...* lists the `.bme` projects of `/bme` and the `.bm` and `.bme` files of `/carts` and of the root of the SD card. Shortcut: **Ctrl+O**.
- *New project* (**Ctrl+N**) starts an empty project. It comes with the starter tiles of bm Studio on its sprite sheet.
- A new project is not saved yet: the menu shows `(not saved yet)` next to the name. Save it with **Ctrl+S**.

## Caveats

- Opening a game says `a game: saving makes its copy`. The first save asks `Save an editable copy: NAME.BME?`; from then on you work on the copy.
- A game without models can receive some: build them and save.
- If the menu shows `*modified*`, there are changes not saved. *Open...*, *New project* and *Exit* ask before throwing them away.

# The screen and the 3D view

bm Studio has two pages, chosen with the F keys (or **Y + left/right** on the gamepad): **F1 build** and **F2 models**. **Esc** (on the gamepad **Y + B**) opens the menu.

## The build page

- On the left, a panel: the five tools, the brush (a tile or a colour, with the sheet around it) and the model (faces, triangles, vertices).
- Over the view, two lines say what is there and what is being done.
- At the bottom, the keys of the moment.
- The cursor is a cell-shaped box. The grid on the floor is one unit per square.

![The build page: tools on the left, the model in the middle](shots/editor-studio-build.png)

## The 3D view controls

| Key | Gamepad | What it does |
|---|---|---|
| `q` / `e` | X + left / right | Turn the view by 45 degrees |
| `w` / `s` | - | Tilt the view |
| `Alt` + left / right | - | Turn the view by 45 degrees (build page) |
| `Alt` + up / down | - | Tilt the view |
| `+` / `-` | X + A / X + B in the models page; none on the build page | Zoom in / out |
| `z`, `0` or `Home` | - | Frame the whole model |
| `v` | - | Change the look: light, flat, wireframe |
| `b` | - | Show the faces seen from behind, darker |
| `g` | - | Show or hide the grid (block and tile tools) |
| `F12` or `?` held | - | Show the keys of the page |

With a mouse: in the view, the right button dragged turns and tilts, the middle button dragged moves, the wheel zooms. The pointer is the cursor: the left button does what space does.

## Good to know

- Moving the cursor is always relative to the view. After you turn the view, up still means "away from you".
- The status line shows `model 1/3: house`: the model you are on, out of how many.
- Press **[** or **]** to go to the previous or next model from any page.
- **Ctrl+Z** undoes, **Ctrl+Y** redoes.

# Building: tiles and blocks

The first two tools put things in a cell. Choose them with **1** (block) and **2** (tile).

## The block tool

A block is a cube of one unit. Two blocks side by side have no wall between them, and removing one brings back the neighbour's wall.

| Key | What it does |
|---|---|
| arrows | Move the cursor on the floor plane |
| `PgUp` / `PgDn` | Move the cursor one level up / down |
| `Space` | Put a block |
| `Backspace` or `Del` | Remove the block |

On the gamepad: **A** puts, **B** removes, **X + up/down** changes level.

## The tile tool

A tile goes on one side of the cell, not on a whole cube. Use it for floors, single walls, doors and windows.

- Press **f** to choose the side in turn: floor, far wall, right wall, near wall, left wall, ceiling. On the gamepad, tap **X**.
- **Space** puts the tile, **Backspace** removes what is on that side.

## The brush: tiles and colours

Press **Tab** (on the gamepad **Y**) to open the sheet.

| Key | What it does |
|---|---|
| arrows | Move over the sheet |
| `z` | Tile size: 8, 16 or 32 pixels |
| `a` / `d` | Narrower / wider selection |
| `w` / `s` | Shorter / taller selection |
| `c` | Switch between tiles and the palette of colours |
| `Enter` | Take it as the brush |
| `Tab` | Close without changing |

- A selection of several tiles is placed in one go: a door two tiles high is one put.
- Outside the sheet, **r** turns the brush by a quarter turn with tools 1-2 only (with tool 3, select, **r** turns the chosen faces), and **h** mirrors the brush with any tool. **x** picks the tile or colour from the face under the cursor.
- With a colour as brush, the faces get a flat colour and no texture.

![The tile picker: the sheet, the selection and the tile large](shots/editor-studio-tiles.png)

## Selecting and moving

### Select (tool 3)

The arrows bring the pointer to the nearest face in that direction on screen.

| Key | What it does |
|---|---|
| `Space` | Choose or release the face |
| `a` | All faces, or none if some are chosen |
| `c` | All the faces joined to the chosen ones: a whole object |
| `g` | Move (see below) |
| `r` / `t` / `m` | Turn / flip / mirror |
| `n` | Show the other side of the face |
| `Enter` | Put the brush's tile on the faces |
| `u` | Turn the texture |
| `,` / `.` | Half as big / twice as big |
| `d` | Copy and move the copy |
| `Del` | Delete the faces |
| `o` | Move the faces into a new model |

### Move (g)

Arrows move, `PgUp` / `PgDn` go up and down. **Tab** changes the step, from 1 down to 1/16. **Enter** leaves the faces there; **Esc** puts them back where they were.

### Vertex (tool 4)

The pointer goes on the corners. **Space** chooses, **a** all or none, **g** moves them (roofs, ramps, free shapes), **m** joins several corners into one.

# Painting a model

bm Studio paints **pixels of a face's tile**, not colours at the corners. There is no vertex colour: a face is either a tile of the sheet or one flat colour.

## What it does

With tool **5** (paint) chosen, **Enter** or **Space** opens the tile of the face under the pointer, large in the panel. You paint it pixel by pixel while the model on the view changes. The painted sheet is saved with the model.

## Good to know

- The painted pixels are on the project's sprite sheet. A tile used by several faces changes on all of them.
- A face with a flat colour is not painted pixel by pixel: **Enter** gives it the paint colour in one go.
- The sheet is the same one bm Pixel and your game use. If your game changes it with `sset`, the models change too.

| Key | What it does |
|---|---|
| arrows | Move the pixel cursor |
| `Space` | Paint the pixel |
| `e` | Clear the pixel (transparent) |
| `i` | Pick the colour of the pixel under the cursor |
| `c` | Choose a colour: the palette, then the colours already in the tile |
| `Esc` or `Enter` | Close the paint panel |

With a mouse, hold the left button over the pixels and drag.

![The paint panel: the tile of a face, pixel by pixel](shots/editor-studio-paint.png)

# The models page and mesh reduction

Press **F2**. The list on the left holds the models of the file. Over the view, three lines show the name, the counts (faces, triangles, vertices) and either the skeleton (`N bones, N animations`) or `no skeleton (bm Animator makes one)`. A blue dot marks a model with a skeleton.

## Keys

| Key | What it does |
|---|---|
| up / down | Choose the model |
| `Enter` | Build it (go to the build page) |
| `n` | New model |
| `r` | Rename (up to 16 letters) |
| `d` | Duplicate |
| `Del` | Delete (press twice) |
| `PgUp` / `PgDn` | Change the order |
| `-` | Reduce the triangles |
| `m` | Make a model from a picture |
| `i` | Texture margin: 0, 0.125, 0.25, 0.5 pixels |

![The models page: the list, the counts and the model turning](shots/editor-studio-models.png)

## Reducing triangles

Press `-`. The box asks for the triangles wanted and proposes half. The reducer keeps borders, colour lines and texture seams, and the skeleton follows the vertices. **Ctrl+Z** undoes it.

## Why it matters

The console draws about 1200 triangles in a scene at 60 fps. Beyond that, the counts turn to a warning: `heavy for 60 fps (1200 triangles a scene)`.

## Caveats

- If you ask for more triangles than the model has, you read `it has N triangles already`.
- Sometimes the model stays above the number asked: the message adds `(no further without turning faces over)`.
- On the models page the key `-` reduces, so zoom with `+` or the mouse wheel.

## A model from a picture

`m` (or *Model from picture...* in the menu) makes a model from an image in the `pics/` folder of the SD or in the SD root (`.png` or `.jpg`). There are three ways: **cutout** (a thick paper figure), **lathe** (the half outline turned around the vertical axis: vases, towers) and **meshy.ai** (an online service that needs the network and an account).

# Saving and exporting

| Key | What it does |
|---|---|
| `Ctrl+S` | Save in place |
| `Ctrl+Shift+S` | Save as: a name in 8.3 form, in `/bme`, ending in `.BME` |
| `F5` | Save, then try the game |

## What is saved

- The models go in the MESH section of the file, together with the sprite sheet if you painted it. The skeletons of the models are saved with them.
- The menu lets you set the cartridge's title and author.
- A new project is born with a small viewer program: it shows the models, so F5 works at once.

## Resource files

The resource format for models is `.bmm`: MESH, ANIM when animated and a sheet with only the cells the textures use. On the PC, `scripts/bmres.py extract GAME.bm --model villager -o VILLAGER.bmm` makes one from a game.

[TO FILL: saving and opening `.bmm` files from the console's bm Studio; the resource docs say it is planned, the program only saves the project]

## For a game built from the repository

A `.bm` with the models and skeletons, saved by bm Studio, can be put as `models.bm` in `carts/<game>/`. `make` puts it into the cartridge with `mkbm.py --models`. If the game has no `sheet.png`, the sheet is the one of `models.bm`.

# Using the model in a game

The functions below are documented in the 3D API.

| Function | What it gives |
|---|---|
| `models()` | The names of the cartridge's models, in order |
| `model(name)` | The model as a mesh (`nil` if missing). Call it in `_init` |
| `bounds3d(m)` | `x0, y0, z0, x1, y1, z1`, the box around the mesh |
| `draw3d(m, x, y, z, rx, ry, rz, scale, flags)` | Draws the mesh |
| `camera3d`, `light3d`, `zclear` | Camera, light, clear the depth |

::: copy
local casa
function _init()
  casa = model("house")
end
function _draw()
  cls(0x1c2030)
  zclear()
  camera3d(0, 4, -8, 0, -0.4)
  light3d(-0.4, 0.8, -0.5, 0.4)
  draw3d(casa, 0, 0, 0, 0, time() * 0.5)
end
:::

## Good to know

- The grid origin of bm Studio goes at the point `x, y, z` of `draw3d`. One square is one unit.
- Textured faces use the sprite sheet of the cartridge.
- Models with a skeleton are animated with `animate()`; see the bm Animator manual.

# Guided tutorials

## Your first block

1. Open bm Studio from the **Dev** tab.
2. Press **Esc**, choose *New project*. You see the build page with tool **1 block** chosen.
3. Press **Space**. A cube appears in the view.
4. Press **PgUp**, then **Space**. A second cube sits on top of the first.
5. Press **q** a few times. The view turns by 45 degrees each time.
6. Press **Backspace**. The block under the cursor disappears.
7. If you made a mistake, press **Ctrl+Z**.

::: try
Put three blocks in a row. Check in the panel that `faces` and `tri` count only the visible walls.
:::

## Paint a model

1. Put one block with tool **1**.
2. Press **5** to choose paint, then **Enter**. The tile of the face under the pointer appears large in the panel.
3. Press **c** and choose a colour from the list.
4. Move with the arrows and press **Space**: the pixel takes the colour, and so does the face on the view.
5. Press **e** on a pixel to clear it.
6. Press **Esc** to close the panel and **Ctrl+S** to save.

If the face has a flat colour instead of a tile, **Enter** paints the whole face. To paint pixels, first give the face a tile: tool **3**, choose the face with **Space**, press **Tab**, pick a tile and press **Enter**, then **Enter** again to put it on the face.

## Reduce a mesh and see the triangle count

1. Press **F2**. Choose a model with many triangles.
2. Read the second line over the view: the faces, the `tri` and the vertices.
3. Press `-`. The box proposes half of the triangles. Press **Enter**.
4. You read `reduced to N triangles  (Ctrl+Z undoes)` and the count changes.
5. Press **Ctrl+Z** to go back.

::: warn
If the count is above 1200, the line says `heavy for 60 fps`. Reduce until the warning goes away.
:::

## Put your model in a game

1. Build and name a model, for example `house`. On the models page, **r** renames it.
2. Press **Ctrl+S**. A new project asks for a name: type one in 8.3 form.
3. Press **F5**. The game runs with the viewer that bm Studio put in the project; left and right change the model.
4. Open the same project in bm Code (bm SDK, F1 page, *OPEN IN*, number 1) and replace the viewer's code with the example of the previous chapter, using your model's name in `model("house")`.
5. Press **F5** again. You see your model turning.

# Tips and limits

- About 1200 triangles in a scene is what the console draws at 60 fps. A model you reduced is the cheapest way to stay under it.
- Cursor cells go from -64 to 63 on each axis.
- A vertex count over the limit turns red in the panel, and saving fails above the limit: 4096 vertices (and 16384 triangles).
- Textured faces need the sprite sheet: the textures are pieces of it.
- Use **F5** often: it saves and shows the model in the game.
- A game and a project are not the same: the menu shows the path of the file you are working on, so check it is the `.BME` copy before you go on.
- [TO FILL: the gamepad keys of bm Studio tried on the PowKiddy RGB30]
