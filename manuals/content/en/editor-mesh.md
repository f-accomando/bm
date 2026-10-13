title: bm Mesh: editing meshes
accent: Mesh
lang: en
kicker: The editor of the suite
subtitle: Move the vertices and the faces of a 3D model, on the console, and see it in your game.
footer: bm manuals - editor-mesh

::: readme
**What this is.** The manual of **bm Mesh**, the editor of vertices and faces. Every key in it comes from the editor's own code.

**How to use it.** Read "Opening" and "The screen and the controls" once, then follow the three tutorials with a cartridge of yours.

**What is missing.** Every gap is marked with a highlighted TO FILL tag; nothing is invented.
:::

# What bm Mesh is

**bm Mesh** is the editor that works on the points (vertices) and on the triangles (faces) of a 3D model. It is built into the console: you find it in the **Dev** tab.

## What it does

- It shows the models of a cartridge turning in a preview, with vertices, triangles and skeleton.
- It lets you choose vertices or faces and move, rotate, scale, extrude, mirror, merge, delete and paint them.
- It also reads the meshes that the game's own code builds, so you can start from a ship or a ring made in Lua.

## How it connects to Studio and Animator

- **bm Studio** makes the models. bm Mesh edits the same models: they live in the MESH section of the cartridge.
- **bm Animator** adds skeletons and animations (the ANIM section). bm Mesh keeps the skeleton of a model: the bone of each new vertex is the one of the vertex it comes from, and bones and animations stay those of bm Animator.
- In the game you use a model with `model("name")`.

## The three kinds of meshes

The list marks each mesh with a letter.

| Letter | What it is | In the game |
|---|---|---|
| M | a model of the MESH section (the ones of bm Studio) | `model("name")` |
| C | a mesh in the code, written by bm Mesh as `mesh_name()` between the lines `-- [bm Mesh begin]` and `-- [bm Mesh end]` at the end of `main.lua` | `local m = mesh_name()`, then `draw3d(m, ...)` |
| G | a mesh the game's code builds with `mesh()`, `mesh_sphere()` or `mesh_cube()` | the game's own code |

## Good to know

- The editor is used with the keyboard, with the mouse or with the gamepad.
- Editing steps can be undone (30 steps): Ctrl+Z undoes, Ctrl+Y redoes (the gamepad: Y + A undoes). Rename and delete clear the undo and redo steps, and so does opening a file; copy as model, copy as code, duplicate and new do not make an undo step.

# Opening

## Projects and games

All the tools of the suite change only **projects** (`.bme`). A **game** (`.bm` or `.b16`) they only read.

- If you open a game, bm Mesh reads it and you can edit. The first save makes an editable copy, named after the game with the extension `.BME`, and from then on you work on the copy.
- The status bar tells you when you opened a game: `a game: saving makes its copy`.

## Ways to open

- From the **Dev** tab of the console menu.
- From a game: press **X** on its cover, then **Open in bm Mesh**.
- From the monitor: the key `4`.
- From inside the editor: Esc, then **Open...** (Ctrl+O). The list shows the `.bm` and `.bme` files of `/carts` and of the root of the SD card.
- If you started bm Mesh with no file, the file chooser opens by itself.

## What you see after opening

The status bar says how many models, code meshes and meshes from the game's code were found, and the editor shows the **list** page.

## Caveats

- If the game's code stops while it is read, you get the message `the game's code stopped while read`: the meshes found before the error are still there.
- A file that cannot be opened gives `cannot open ...` with the reason.

# The screen and the controls

## The pages

The tab bar at the top has three pages. A click on a tab, or its key, changes page.

| Page | Key | What it is for |
|---|---|---|
| list | F1 | choose a mesh; copy, rename, duplicate, delete, make a new one |
| edit | F2 | choose and change the vertices and the faces |
| menu | Esc | open, save, try the game, exit |

![The list page: the meshes on the left, the chosen one turning in the preview](shots/editor-mesh-list.png)

Other keys. F3 works only on the list and edit pages, and `[` and `]` are ignored on the menu page; the others work on every page:

| Key | What it does |
|---|---|
| F3 | the commands of the page, as a menu |
| `[` and `]` | the previous and the next mesh |
| F12 held, or `?` | shows the keys of the page |
| Ctrl+S | save |
| Ctrl+Shift+S | save as (a name of 8 letters, in `/carts`) |
| Ctrl+O | open another file |
| Ctrl+N | a new model, a cube |
| F5 or Ctrl+R | try the game |
| F6 | the assistant |
| Ctrl+Z, Ctrl+Y | undo, redo |
| Ctrl+Esc | back to the bm menu (it asks first if there are unsaved changes) |

## The list page (F1)

| Key | What it does |
|---|---|
| up, down, home, end | choose the mesh |
| Enter | edit it |
| m | copy as a model (mesh to model) |
| c | copy as code (model to `mesh_name()`) |
| r | rename |
| d | duplicate (a model keeps its skeleton) |
| Del | delete: press it twice |
| n | a new model, a cube; with F3 also a plane or a sphere |
| q, e | turn the view |
| w, s | tilt the view |
| + and - | zoom |
| space or o | start or stop the spin |
| f | frame the mesh again |

A G mesh cannot be renamed or deleted: the game's code makes it. Copy it with **m** or **c** to get one you can name.

## The edit page (F2)

The edit page draws the model with its edges, a pointer, and a line at the top with the name, the number of vertices and triangles, the mode (`VERTICES` or `FACES`) and how many are chosen. A small colour swatch and the current step are shown under it.

| Key | What it does |
|---|---|
| arrows | move the pointer (faster when held) |
| home | the pointer to the middle |
| n, b | the pointer on the next or the previous vertex or face, also one behind |
| space | add to the choice, or take away |
| Enter | choose only that one |
| a | choose all; if something is chosen, choose none |
| l | choose everything connected to it |
| Tab | switch between vertices and faces |
| g, r, t | move, rotate, scale |
| x | extrude the chosen faces |
| d | duplicate the chosen faces |
| m | mirror the choice, left to right on the screen |
| M | copy the choice to the other side of 0 |
| j | make a face on 3 or 4 chosen vertices |
| k | merge the chosen vertices into one |
| K | weld the vertices at the same point |
| u | split each chosen face into 4 |
| i | turn the chosen faces over |
| Del | delete the choice |
| p | paint the chosen faces with the current colour |
| o | take the colour of the face under the pointer |
| c | open the palette |
| q, e | turn the view |
| w, s | tilt the view |
| + and - | zoom |
| f | frame the chosen vertices |
| 1, 3, 7, 0 | front view, side view, top view, the free view |
| `,` and `.` | a finer or a coarser step |

![The edit page: a model with its edges, the pointer and a vertex chosen](shots/editor-mesh-edit.png)

## The gamepad

| Control | What it does |
|---|---|
| Y + left or right | change page |
| Y + up or down | the previous or the next mesh |
| Y + B | the menu |
| Y + A | undo |
| X, tapped | the commands of the page |
| X + d-pad | turn and tilt the view |
| X + A, X + B | zoom |
| A | in the list: edit the mesh; in the edit page: choose the vertex or face under the pointer |
| B | in the edit page: choose none |
| d-pad | the pointer; inside a tool, change the value |

[TO FILL: the pad controls are written from the code; not tried on the PowKiddy RGB30]

# Selecting and moving

## Vertices or faces

The edit page has two modes. **Tab** switches between them; the line at the top says `VERTICES` or `FACES`. In each mode a choice is a set of vertices or a set of faces.

- Put the pointer on a vertex or on a face: it lights up.
- **space** adds it to the choice, or takes it away if it was in. **Enter** makes it the only one.
- With the mouse, a click chooses only what is under the pointer; Shift + click adds or takes away.
- **a** chooses all; press it again to choose none. **l** adds everything joined to the choice by faces.
- **n** and **b** walk through all the vertices or faces in order: this reaches also the ones hidden behind the model.

When you switch mode, the choice follows: the vertices of the chosen faces, or the faces whose three corners are chosen.

## Move, rotate, scale

Press **g** to move, **r** to rotate, **t** to scale the choice (the corners of chosen faces move too). The tool opens and the top line shows the value, for example `MOVE x 0 y 0.25 z 0`.

- The arrows and PgUp/PgDn change the value. For a move: left and right go along the view's horizontal axis, up and down change the height, PgUp and PgDn go nearer or farther.
- **x**, **y**, **z** limit the change to that axis. **n** works along the normal. Press the same key again to free it.
- `,` and `.` change the step. The move steps are 0.01, 0.05, 0.1, 0.25, 0.5 and 1; the rotation steps are 1, 5, 15, 30, 45 and 90 degrees; the scale steps are 1.01, 1.05, 1.1, 1.25, 1.5 and 2.
- **Enter** confirms. **Esc** cancels and puts everything back.
- With the mouse, drag the left button to change the value and click to put it. With the gamepad, A confirms, B cancels and a tap of X switches the axis.

## Other changes

- **x** extrudes the chosen faces; they then move along the normal.
- **j** makes a face on 3 or 4 chosen vertices, in the order you chose them, turned towards the camera. It works in vertex mode only.
- **k** merges the chosen vertices in their middle; **K** welds the vertices that are at the same point (with flag 4 of `draw3d` the faces around look smooth).
- **Del** deletes: in vertex mode the vertices and the faces that use them, in face mode the faces.
- **p** paints, **o** takes a colour, **c** opens the palette (arrows to choose, Enter to take it, Esc to close). A face with a texture of the sheet has no colour.

## The view

**q** and **e** turn, **w** and **s** tilt, **+** and **-** zoom, **f** frames the choice. **1**, **3**, **7** and **0** give the straight views. With the mouse: the right button dragged turns, the middle one moves, the wheel zooms.

## Caveats

- Editing a **G** mesh first makes a copy of it as a model; the game's code is not rewritten. The status bar tells the name of the copy.
- Edits that would pass 65535 vertices or 65535 triangles are refused and undone.

# The preview and cart_meshes

The model you edit is drawn lit and with its textures from the sheet of the cartridge, so you see it as the game will. On the edit page only the edges of the faces that look at the camera are drawn, and only when the mesh has 3000 triangles or fewer.

## How the game's meshes arrive

The G meshes come from the kernel function `cart_meshes(path)`. It runs the game's code **apart**, in a Lua state of its own:

- it runs the file's body, then `_init`, `_update` and `_draw` once;
- there is an instruction limit; files, screen and sound do nothing;
- it collects what the code builds with `mesh()`, `mesh_sphere()` and `mesh_cube()`;
- the name of a mesh is the name of the variable that holds it: `M.ship` gives `ship`.

## Good to know

- A mesh built only in a later frame, or only after some input, may not be found.
- If the code stops with an error, bm Mesh still shows what was found and tells you the error.
- A G mesh that equals one of the C meshes is not listed twice.

# Saving

- **Ctrl+S** saves. **Ctrl+Shift+S** asks for a name (8 letters, saved in `/carts` with the extension `.BME`).
- The menu (Esc) has **Save**, **Save as...**, **Try the game (F5)** and **Exit bm Mesh**. A mark `*` after the file name at the top means unsaved changes.
- Only the MESH and ANIM sections and the bm Mesh block in the code change. Sprite sheet, map, cover, sound and the rest of the code stay exactly as they were.
- A model with no vertices or no faces is left out of the file: the message says which one.
- A cartridge holds at most 256 models.
- If you opened a game, the first save asks for the editable copy and from then on the file is the `.BME`. To get a game again, build it from the project in the SDK.
- Exit and Open ask you to choose the same entry twice when there are unsaved changes.

# Tutorials

::: try
Do the three tutorials in order with a cartridge of yours that has a model, or with a game that builds its meshes in code.
:::

## Move a vertex

1. Start bm Mesh from the **Dev** tab (or **X** on the cover of a game, **Open in bm Mesh**). Choose the cartridge in the file chooser with up and down and Enter.
2. You are on the list page. Choose a mesh with up and down: it turns in the preview.
3. Press **Enter** to edit it. If the mesh is a G mesh, the first change makes a copy as a model.
4. Look at the top line. If it says `FACES`, press **Tab**: it must say `VERTICES`.
5. Move the pointer with the arrows to a vertex until it gets a circle around it.
6. Press **Enter**. The top line says `VERTICES: 1 chosen`.
7. Press **g**. The top line shows `MOVE`. Press the **up** arrow once: the vertex moves up by the step.
8. Press **Enter**. The status bar says `move ...: 1 vertices`.
9. Press **Ctrl+S**. You see `saved` with the file name.

If it does not go well: **Esc** while the tool is open puts the vertex back; **Ctrl+Z** after confirming undoes it.

::: try
Press **g** again, then **x**, then the right arrow a few times, then **Enter**. The vertex moves only along x.
:::

## Fix a face

On the edit page, when the mesh has 3000 triangles or fewer, only the faces that look at the camera have their edges drawn. If a face has no edges from any side, either it is turned the wrong way or it is missing.

### The face is turned the wrong way

1. On the edit page, press **Tab** until the top line says `FACES`.
2. Move the pointer on the face and press **Enter**. The face is outlined and the top line says `FACES: 1 chosen`.
3. Press **i**. You see `1 faces turned over`.
4. Press **q** or **e** to turn the view and check it.

### The face is missing

1. Press **Tab** until the top line says `VERTICES`.
2. Choose three (or four) vertices with **space**, one after the other, in the order around the face.
3. Press **j**. You see `a new face (1 triangles)`, or 2 for four vertices. The face takes the current colour; **o** and **p** change colours.
4. If it still looks wrong, choose the face and press **i**.

If you see `choose 3 or 4 vertices (in order) for a new face`, the number of chosen vertices is wrong.

## Check the model in a game

1. Save with **Ctrl+S**. Remember the name the model has on the list (for a copy of a G mesh, the status bar told you).
2. In the game's code use the name: for a model, `model("name")`.
3. Press **F5**. bm Mesh saves if needed and runs the game.
4. Look at the model in the game. Then come back to the editor: you return to the same page.
5. If something is wrong, edit again and press F5 again.

::: copy
local m = model("ship")
draw3d(m, 0, 0, 0, 0, 0, 0, 1, 0)
:::

To leave the game and come back to bm Mesh, press Start + Select on the pad or Ctrl+Esc on the keyboard; the editor returns to the same page.

# Tips and limits

## Tips

- Use **1**, **3** and **7** to check a move from the front, the side and the top.
- Use a small step (`,`) to align a vertex, a larger one (`.`) to place a whole part.
- Use **M** to build a symmetrical model: make half, then copy it to the other side.
- Use **K** after joining parts, so the faces around look smooth.
- Save often: the editor remembers 30 steps of undo, and opening or renaming clears them.

## Limits

- At most 65535 vertices and 65535 triangles in a mesh.
- At most 256 models in a cartridge.
- A G mesh cannot be renamed or deleted: change the code with bm Code.
- A copy as code does not keep the skeleton.
- The assistant (F6) adds a model with no skeleton.
- [TO FILL: the size of a model that the Pi Zero W draws smoothly]
