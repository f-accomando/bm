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
- It lets you choose vertices, edges or faces and move, rotate, scale, extrude, mirror, merge, delete and paint them.
- It also reads the meshes that the game's own code builds, so you can start from a ship or a ring made in Lua.

## How it connects to Studio and Animator

- **bm Studio** makes the models. bm Mesh edits the same models: they live in the MESH section of the cartridge.
- **bm Animator** adds skeletons and animations (the ANIM section). bm Mesh keeps the skeleton of a model: the bone of each new vertex is the one of the vertex it comes from, and bones and animations stay those of bm Animator.
- In the game you use a model with `model("name")`.

## Which editor for what

- **bm Studio**: static models. Objects, props and buildings, made of blocks and tiles. The 3D map of a game is made in the map page of bm SDK.
- **bm Mesh**: organic models. Characters and heroes, and all fine editing of vertices and triangles.
- **bm Animator**: rigs and animations. It puts a skeleton into a model and moves it. It does not build models.
- bm Studio and bm Mesh write the same MESH section, so a model can go from one editor to the other.

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
- From inside the editor: Esc, then **Open...** (Ctrl+O). The list shows the `.bme` projects of `/bme` and the `.bm` and `.bme` files of `/carts` and of the root of the SD card.
- If you started bm Mesh with no file, the file chooser opens by itself.

## What you see after opening

The status bar says how many models, code meshes and meshes from the game's code were found, and the editor shows the **list** page.

## Caveats

- If the game's code stops while it is read, you get the message `the game's code stopped while read`: the meshes found before the error are still there.
- A file that cannot be opened gives `cannot open ...` with the reason.

# The screen and the controls

## The pages

The tab bar at the top starts with the name `bm Mesh`. Then it has three pages. A click on a tab, or its key, changes page.

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
| Tab | switch between vertices, edges and faces |
| g, r, t | move, rotate, scale |
| x | extrude the chosen faces |
| d | duplicate the chosen faces |
| A (Shift+A) | add a part: a cube, a cylinder, a sphere or a plane |
| m | mirror the choice, left to right on the screen |
| M | copy the choice to the other side of 0 |
| X (Shift+X) | live mirror on or off |
| S (Shift+S) | snap: off, grid, vertex, grid and vertex |
| G (Shift+G) | grid step: 0.05, 0.1, 0.25, 0.5 |
| Ctrl+R | loop cut across the chosen edge |
| Shift+J | knife: cut a triangle between the middles of two chosen edges |
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

## Vertices, edges or faces

The edit page has three modes. **Tab** goes from one to the next: vertices, edges, faces, and again vertices. The line at the top says `VERTICES`, `EDGES` or `FACES`. In each mode a choice is a set of vertices, a set of edges or a set of faces.

- Put the pointer on a vertex, on an edge or on a face: it lights up. An edge is the line between two vertices of a triangle; the pointer must be within 6 pixels of it.
- **space** adds it to the choice, or takes it away if it was in. **Enter** makes it the only one.
- With the mouse, a click chooses only what is under the pointer; Shift + click adds or takes away.
- **a** chooses all; press it again to choose none. **l** adds everything joined to the choice by faces.
- **n** and **b** walk through all the vertices, edges or faces in order: this reaches also the ones hidden behind the model.

When you switch mode, the choice follows: from vertices to edges, the edges whose two ends are chosen; from edges to faces, the faces whose three corners are chosen; from faces to vertices, the corners of the chosen faces.

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

## Add a part

**Shift+A** opens a list of four shapes: cube, cylinder, sphere and plane. The one you pick is added to the model as a new part, with the width 1 and the colour of the page. It goes to the middle of the chosen vertices, or to 0 if nothing is chosen. The vertices of the new part are then the choice, so you can move, rotate or scale it at once. One undo takes the part away. If the model has a skeleton, the new vertices follow the bone of the first chosen vertex (or the first bone). With the gamepad, tap X and pick **Add a part**.

## Live mirror

**Shift+X** turns the live mirror on and off. While it is on, the page shows **MIRROR X**. Move, rotate, scale, paint and delete then act on the chosen vertices or faces and on their counterparts across the plane X = 0. The editor finds the counterpart by position: the vertex at (-x, y, z), within 0.001. A vertex on the plane is its own counterpart. A vertex with no counterpart changes alone. If you choose both sides, the vertex with the lower number leads and the other follows. The tools that add parts or change the topology (add a part, extrude, duplicate, subdivide) do not act on the other side. One undo takes back both sides. With the gamepad, tap X and pick **Live mirror X on / off**.

## Snap

**Shift+S** changes the snap. The order is: off, grid, vertex, grid and vertex, then off again. The page shows the mode at the top right, for example **SNAP grid 0.1**, and a message tells the new mode. The snap works while you move vertices (**g**, and the move after **d**). **Shift+G** changes the grid step: 0.05, 0.1, 0.25 or 0.5 units. You can press both keys while a move is open.

The snap works on the leading vertex. This is the chosen vertex with the lowest number. The editor snaps the position of this vertex, and every other chosen vertex moves by the same distance. The grid mode rounds each axis of the leading vertex to the step. If you lock an axis (x, y or z), only that axis is rounded. The vertex mode moves the leading vertex to the nearest other vertex of the model when that vertex is within 6 pixels on the screen. A small green ring shows the vertex that the snap took. The chosen vertices are never a target. In the mode grid and vertex, a vertex in range wins, and the grid works when no vertex is in range. With an axis locked, the vertex snap changes only that axis.

With the live mirror on, the counterparts follow the same move, mirrored. One undo takes back the whole move. The tool line shows **SNAP** and the move after the snap. The snap does not change a rotate or a scale, and it does not place a new part. With the gamepad, tap X and pick **Snap: off, grid, vertex, both** or **Snap grid step**.

## Edges and the loop cut

In edge mode a choice is a set of edges. The chosen edges show as thick lines. Move, rotate and scale act on the two ends of each chosen edge. **x** and **d** act on the faces whose corners are all chosen. **Del** removes the triangles next to the chosen edges, and the vertices that no triangle uses any more. With the live mirror on, choosing an edge also chooses its counterpart across X = 0, and move, rotate, scale and delete act on both. The loop cut and the knife do not act on the other side.

**Ctrl+R** makes a loop cut. Choose one edge first (in vertex mode, two vertices that make an edge are enough). The editor goes from the edge across the quads around the model. A quad is two triangles that look the same way (the angle between their normals is less than 60 degrees) and share a diagonal. In each quad it crosses the edge and the opposite edge, and goes on from that edge. It puts one new vertex in the middle of each crossed edge and cuts each quad in four triangles. A new vertex takes the bone of the lower end of its edge and the mean of the texture points (UV). A new triangle takes the colour of the old face that holds most of its corners.

The ring stops in two cases. At an open border (an edge with one triangle only) there is nothing to cross. At a triangle that is not part of a quad, the triangle is cut in two, from the new vertex to its far corner, so no gap stays. The ring is closed if it comes back to the first edge. If the ring would cross itself, or an edge has more than two triangles, nothing changes and a message tells why.

When it is done, the new vertices are the choice (in edge mode, the new edges across the quads), so you can move or scale the loop at once. One undo takes back the whole cut. With the gamepad, tap X and pick **Loop cut on the chosen edge**.

**Shift+J** is the knife. Use it in edge mode. Choose two edges of one triangle (two edges that meet at a corner of the triangle). The editor puts a new vertex in the middle of each edge and cuts the triangle so that a new edge joins the two new vertices. The triangle across each of the two edges is cut in two, from the new vertex to its far corner, so no crack appears. A new vertex takes the bone of the lower end of its edge and the mean of the texture points (UV). A new triangle keeps the colour of its old face. The two new vertices are the choice (the new edge, in edge mode), and one undo takes back the whole cut.

The knife does nothing, and a message tells why, in these cases: you did not choose exactly two edges, the two edges are not edges of one triangle, or an edge has more than two triangles. The knife is a tool that changes the shape of the mesh, so the live mirror does not copy it to the other side. With the live mirror on, choosing an edge also chooses its counterpart, so turn the mirror off (Shift+X) before you choose the two edges. With the gamepad, tap X and pick **Knife on two edges of a triangle**.

## The view

**q** and **e** turn, **w** and **s** tilt, **+** and **-** zoom, **f** frames the choice. **1**, **3**, **7** and **0** give the straight views. With the mouse: the right button dragged turns, the middle one moves, the wheel zooms.

## Caveats

- Editing a **G** mesh first makes a copy of it as a model; the game's code is not rewritten. The status bar tells the name of the copy.
- Edits that would pass the format limit of 65535 vertices or 65535 triangles are refused and undone.

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

- **Ctrl+S** saves. **Ctrl+Shift+S** asks for a name (8 letters, saved in `/bme` with the extension `.BME`).
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
4. Look at the top line. If it does not say `VERTICES`, press **Tab** until it does.
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

- The format limit of a mesh is 65535 vertices and 65535 triangles. The edit page shows the count of vertices against it.
- At most 256 models in a cartridge.
- A G mesh cannot be renamed or deleted: change the code with bm Code.
- A copy as code does not keep the skeleton.
- The assistant (F6) adds a model with no skeleton.
- [TO FILL: the size of a model that the Pi Zero W draws smoothly]
