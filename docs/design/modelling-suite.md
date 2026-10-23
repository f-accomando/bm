# Modelling suite: Studio, Mesh and Animator

Status: plan (docs only). Roadmap items: APP-10, APP-11, APP-12 in [`../ROADMAP.md`](../ROADMAP.md). Related: [suite-studio-mesh](suite-studio-mesh.md), [suite-animator](suite-animator.md), [tools](../progress/tools.md).

## Goal

A user builds a low-poly character with the console editors. Example: the `lopo` Kratos model (a poly.pizza `.glb`, about 1500 triangles, 16 bones). The suite is a basic equivalent of a commercial modeller. It is not high calibre. The bm philosophy is "make and play", and bm is a platform to learn.

## Roles

| Editor | Makes | Does not make |
|---|---|---|
| bm Studio | static models: objects, props, buildings (blocks and tiles) | characters; the 3D map of a game (that is the map page of bm SDK, APP-2) |
| bm Mesh | organic models: characters, heroes; fine editing of vertices, edges and triangles | skeletons |
| bm Animator | rigs and animations on the models of the file | models |

Studio and Mesh write the same MESH section of the `.bme` project. A `.bmm` is the same data as a resource file. One model can go from one editor to the other with no conversion.

## Decisions (owner, 22 Oct 2026)

1. No cap on triangles or vertices as policy. See "Counters and cap removal".
2. The suite reads `.glb` from the SD folder `/glb` (read-only) and converts it to `.bmm` in `/bmm`. The editors read both folders (APP-9).
3. The system writes lowercase file names (done by another task; this plan does not touch `fat.c`).

## The three phases

Each tool is described in plain words, as in Blender.

### Phase 1: character base (APP-10)

- **Ghost reference.** A `.glb` is shown as a faint copy behind the model. You trace over it. It is not part of the model and is never saved. A later option snaps points to its surface.
- **Primitives.** Cube, cylinder, sphere and plane. Each one is added as a new part of the model: a head, a limb, a body. Done in step 3 (`claude/app10-primitives`): `T.G.primitive(it, shape, centre, size, col, bone)` returns the new vertex numbers. In the Mesh edit page, Shift+A (and X > Add a part on the pad) opens the four shapes. The part goes to the middle of the choice, the new vertices become the choice, and one undo takes the part away.
- **Live mirror (X).** A switch. When it is on, each edit on one side happens on the other side too. You model half a body and get the whole body. Done in step 4 (`claude/app10-mirror`): in the Mesh edit page, Shift+X (also in the commands list on the pad) turns it on and off, and "MIRROR X" shows while it is on. Move, rotate, scale, paint and delete act on the chosen vertices or faces and on their counterparts across X = 0. `T.G.mirror_pairs(it, list, eps)` finds the counterpart by position (-x, y, z) within 0.001; `T.G.mirror_faces` finds the counterpart face. A vertex with no counterpart changes alone. When both sides are chosen, the lower vertex leads and the other follows. Topology tools (add a part, extrude, duplicate, subdivide) are not mirrored. One undo step per edit.
- **Edge mode.** You select the lines between vertices, not only the points or the triangles. Done in step 5a (`claude/app10-edges`): Tab goes vertices, edges, faces. The chosen edges are `ed.se` in the Mesh cart; `ed.sv` always holds their ends, so move, rotate, scale and extrude work on them. Del removes the triangles next to the edges. The live mirror also chooses the counterpart edge and acts on both.
- **Loop cut.** You pick an edge and the editor cuts a ring around the part, adding a ring of new vertices. Use it to add detail at an elbow or a knee. Done in step 5a: Ctrl+R (and the commands list on the pad). `T.G.loop_plan(it, a, b)` finds the ring without a change; `T.G.loop_apply(it, plan)` makes it. A quad is two triangles that share a diagonal and have normals within 60 degrees; when both other edges of a triangle fit, the flatter pair, then the longer edge, is the diagonal. The ring crosses the edge and the opposite edge of each quad. It stops at an open border and at a triangle that is no quad: that triangle is cut in two so no gap stays. A ring that crosses itself, or an edge with more than 2 faces, changes nothing. A new vertex takes the bone of the lower end and the mean uv; a new triangle takes the colour and uv of the old face with most of its corners. The new vertices (in edge mode the new edges) become the choice. The loop cut is not mirrored.
- **Knife-lite.** You cut one face along a line between two points of its edges. It is the simple form of the knife tool.
- **Snap.** A move jumps to the grid, or to the nearest vertex.
- **Counters.** The vertex and triangle counters are always visible.

### Phase 2: comfort (APP-11)

- **Box select.** You drag a rectangle and select all points inside it.
- **Hide and isolate.** Hide the selection, or hide everything else, to reach the part you edit.
- **Proportional (soft) move.** A move that also pulls the nearby points, less and less with the distance. It gives smooth shapes.
- **Numeric entry.** You type the exact value of a move, rotation or scale.
- **Reference images.** A front picture and a side picture from `/pics` stand behind the model.
- **More undo steps.** Today there are 30.

### Phase 3: finish (APP-12)

- **Bevel.** An edge becomes a narrow strip, so hard corners get a soft highlight.
- **Automatic texture projection.** Box projection: each face gets a place on the sprite sheet from the side it looks to. It saves the work of hand-placed UV.
- **Humanoid base mesh.** A ready low-poly body with a skeleton. The generator and scripts of the `bm-forge` skill (`humanoid_boxes.json`) are reused if they fit.
- **Studio blocks.** Ramp, corner and rounded blocks.

## Data and code

- All the editing functions live in `src/script/bm3d.lua`, shared by the three editors. A new tool is one function there, with a host test that calls it without a screen.
- The pure geometry of a model is the table `T.G` in `src/script/bm3d.lua` (done, step 2 of APP-10: `claude/app10-shared-geo`). It holds `model_item`, `encode_model`, `orient`, `normal_of`, `unit`, `flip`, `compact`, `drop_degenerate`, `add_vertex`, `primitive`, `mirror_pairs`, `mirror_faces`, `edge_key`, `edge_ends`, `edge_faces`, `loop_plan` and `loop_apply` (`loop_cut` does both). They work on the Mesh shape `{v, f, vb}` and never touch an editor state: `G.compact(it, sv)` takes the selection and returns the remapped one. bm Mesh calls `require "bm3d"` and keeps the same names as aliases of `B.G`. Studio has not moved to `T.G` yet.
- One shared import function reads a model for the three editors: `.bmm` directly, `.glb` through the converter (`src/bm/glb.c`; on the PC `scripts/bmres.py convert IN.glb OUT.bmm`). No editor has its own copy.
- The model data stay as they are: the MESH section (vertices, u16 triangle indices, colour or tile per face, bone per vertex). The ghost reference is a separate, unsaved layer. Reference images are paths in the project; they are not inside the model.
- Edges are not stored. They are derived from the triangles when needed.
- Loop cut, knife-lite, primitives and mirror keep the bone of the source vertex (as the extrude does today), so a rigged model can still be edited.

## Counters and cap removal

- Both editors show the number of vertices and the number of triangles all the time.
- The hint "heavy for N triangles at 60 fps on the Pi Zero W" (`T.TRIS_60FPS`) stays as a warning only. It never blocks an action. What does not run well on the Pi can run on a faster board.
- The only limit is technical: indices and counts are u16, so one mesh has at most 65535 vertices and 65535 triangles. The counter shows it ("of 65535"). Docs and the manuals say so.
- The Studio save cap is removed (done, step 1 of APP-10: `claude/app10-counters-caps`). The code changes were:
  - `src/script/bm3d.lua`: `T.LIMIT_V` and `T.LIMIT_T` (line 34), the check in the save function (lines 187-188) and the red panel (line 1053). Keep a 65535 vertex check.
  - `carts/mesh/main.lua`: `LIMIT_V` and `LIMIT_F` (65535) are the format limit. The messages say "format limit".
  - bm Studio reducer (`carts/studio/main.lua`, line 1659): the text "at most N a scene" becomes a hint.
  - `src/bm/meshcap.c` captures meshes built by game code. It has no size policy. Check it and the assistant text. The `mesh` line of `src/ai/api.tsv` ("4096 and 16384 before bm3d 5.0") is history: keep it.
  - Manual text: done in this step for the Studio manual (en and it).

## Done condition

A Lua host test drives the editor functions with the phase 1 tools only and builds a low-poly humanoid of the lopo class (about 1500 triangles). The model plays with an Animator skeleton.

## Not verified on hardware

Nothing in this plan ran on the Pi Zero W or the RGB30. These stay unverified until a user runs them: speed of the live mirror and of loop cut on big meshes, frame rate of a 1500 triangle character, memory of a ghost `.glb`, input with the gamepad and the mouse for the new tools. The `lopo` model is a reference size, not a performance proof.
