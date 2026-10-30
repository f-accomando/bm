# Map kit (`.bmk`): a 3D map with its models and sprites

Status: format and example built on the PC (2026-10-29). The console does not read a kit yet. The owner gives the
final rules for kits later; this page records only what is built.

## Goal

One file holds a map for the 3D map page of bm SDK, with all the elements that go with it. The example is `island`:
an island in the ocean with a campfire in the middle. It becomes a graphics benchmark like `herocart` (3D and 2D
elements, flat colours and textures). Day, night, rain, snow and fog are not in the kit: the future `.bm` does them.

## Two looks: texture and flat

Every model is in the kit twice, with the same triangles: the textured one (`palm`) and the flat one (`palm_f`, colours
only). The object table has both names: `model` (textured) and `model_flat`. A game picks one by its options (as the
Texture row of `herocart`). `--look texture|flat` on `bmkit.py island` makes a kit with one look only (the names of the
flat look then end in `_f`).

- Textures are in the shared sheet, row 1 (y = 16) bark, leaf, stone, wood and row 2 (y = 32) grass, sand, water, soil.
  The palm leaves, bushes, rocks, fire stones, logs, island and sea use them.
- The map floor is drawn from the sheet by `map3d` and has no flat look of its own. The INFO block `[map island]` has the key
  `flat floor colours` (the colour of each floor tile) for a game that draws a flat floor.

## The island: size and sea

- The map is 72 x 72 tiles (144 x 144 cells, 1 unit a tile). The land is about 48 units across. Its middle is
  flat (height 2) and more than 32 units across, the size of the plaza of `herocart` (32 x 32 cells), so the same
  crowd of models fits. One step (height 1) and a beach (height 0, the sea level) go down to the water.
- The map holds 24 palms, 40 bushes, 60 grass tufts, 22 rocks, the campfire in the middle, a chest, a boat and the
  player start (about 144 objects).
- The sea is an object: the model `sea` (a square of radius 1, wave facets in four blues, 1152 triangles at `hi`),
  scale 72 in the object table (144 units across, as the map), sprite `sea2d`. The water tiles of the map stay under it
  (flag 3). The `islet` model (scale 30) is the whole island seen from far.

## What is in the file

A `.bmk` is a `BMRES` file of type 6 ([RISORSE](../RISORSE.md), section 4). It uses the sections that exist:

| Section | Content |
|---|---|
| MESH | The models. Bits 1-3 of the model flags are the map class: 2 solid, 4 floor, 8 decor ([BM-FORMAT](../BM-FORMAT.md)). |
| SHEET8 | One sheet for all: floor tiles (16x16, tile 0 means no floor), textures of the models, 2D sprites. |
| SPRITES | The 2D sprites. The flat version of each object (a billboard), by name. |
| MAP, LAYERS | The layers `floor`, `height`, `walls`, `objects`, as `map3d_build` reads them ([light3d](../api/light3d.md)). |
| FLAGS | Tile flags: 0 solid, 3 water, 4 hurts ([draw](../api/draw.md)). The island marks the water tile. |
| INFO | The object table (below). |

## The objects layer and the object table

- The objects layer holds an even number of 1024 or more on the even cell of a tile (`src/bm/map3d.c`).
- The kernel knows 1024 to 1034 (PLAYER_START, APPLE, CRATE, GOAL, LAMP, CRATE_BOUNCE). A kit uses 1040 and up.
- For each id, INFO has a block `[object NAME]` with these keys:

| Key | Meaning |
|---|---|
| `id` | The number on the objects layer. |
| `model` | The name in MESH (3D). |
| `sprite` | The name in SPRITES (2D). |
| `mode` | `3d` or `2d`: what the object is at first. The game swaps them (a tree 3D near, 2D far, as in Doom). |
| `scale` | Scale of the model and of the sprite. |
| `class` | `solid`, `floor`, `decor`, or `none`: the map class of the model. |
| `radius` | Collision radius in world units. 0 means none. |

- `map3d_objects` returns only the kernel kinds today. A kit object needs a later change of `map3d.c`: not done.
- Floor ids: tile at (x, y) in 16-pixel units has the id `y * 2 * (sheet width / 8) + x * 2`.

## The picture inside the kit

`island.bmk` carries a COVER (128x128) and a PREVIEW (12 frames of 96x96) of the whole island: the islet with palms,
fire, chest, bushes, rocks and boat merged into one model that is not kept in the kit (`scene_model` in
`scripts/bmkit.py`). `--no-preview` leaves both out and the file is 177 KB smaller. See
[RISORSE](../RISORSE.md), "The picture inside a file".

## Not a Market catalog item

A `.bmk` is a resource (like `.bmm`, `.bmi`, `.bms`), not a game or an app. The Market catalog (`games/`, `apps/`) does
not list it (owner, 2026-10-29). Since 2026-10-30 the repository `bm-market` has the folders `bmk/` and `bme/` next to
`games/` and `apps/` (one folder per item, the file and an `info.txt`). The catalog tools do not read them. The console
installs from them with the Monitor command `net fetch gh:f-accomando/bm-market/bmk/island/island.bmk`. A `.bme`
installs the same way into `/bme`.
It also travels with a project: `bmres.py add` puts it in a game, or the file is copied to `/bmk` on the SD.

## Folder on the SD

`/bmk`, the same pattern as `/bmm` (one folder per extension). bm SDK reads it: the menu entry **Import a map kit
(/bmk)...** lists the `.bmk` files (`ls("/bmk")`). The SD map (Settings > System) does not count `/bmk` yet.

## In bm SDK

- **Import.** The menu entry **Import a map kit (/bmk)...** calls `kit_load(path)` ([tools API](../api/tools.md)). The
  kit's sheet, tile flags, sprite zones, map with its layers and 3D models take the place of those of the project. The
  code, the cover and the sound bank of the project stay. The kit's INFO text stays in the project (`kit_info`).
- **3D map page.** `map3d_objects` names an object of a kit `OBJ<id>` (ids 1040 and up). The 3D view reads the
  `[object NAME]` blocks of the INFO text and draws the model with the block's `scale`.
- **Both SDKs.** `carts/editor` (Pi) and `carts/sdk_b16` (RGB30) have the same code. Not run on a device.
- `bmres.py add` keeps the layers `height`, `walls` and `objects` as numbers (they hold heights and ids, not sheet tiles).

## The benchmark: Island Bench

`carts/islandbench` is the benchmark made from the kit (`.bme` in `/bme`, `.bm` in `/bm`, both built by
`make build/carts/islandbench.bm build/carts/islandbench.bme`; the kit is `carts/islandbench/assets/island.bmk`).
Manual mode: rows for the number of elements (0 to 200%), day or night (sun or moon), clear, rain, snow or fog, the
height of the light, the campfire (a `lamp3d` light, a 2D flame in the 3D and sparks), shadows (off, dark, dither),
texture or flat, 3D models or 2D sprites or a mix by distance, the sea, lighting, backend, AA, resolution and camera.
The rows are in five groups (Test, Environment, Light, Look, Screen) that open and close with A or the keys 1 to 5. The numbers are one line across the bottom of the screen.
The first values are the light ones (50% of the elements, no shadows, no fire, no sea, flat light, mix of 3D and 2D). The last row, **Block under 30 fps** (on), watches each change: if the mean frame time of the next 20 frames is over 33.4 ms, the options freeze for 1 s and then the last good ones come back. The guard is off in the benchmark.
**Speed (2026-10-30).** The floor of far water has no tile (floor id 0: `map3d` makes no triangle): of 5184 tiles 2737 are cut, the map goes from about 10400 to about 4900 triangles, and the sea model covers the rest. No rim and no gloss light, and no fog in clear weather: they are what sends a textured mesh to the ARM on the Mali and into the slow path on the V3D. **RGB30 (measured 30 Oct, 640x360):** the Mali takes flat colours and opaque textures only. This kit has textures with holes (leaves), and on the Mali a `point3d`, a `line3d`, a `sprite3d`, a shadow, a particle and light with a colour hand the 3D to the ARM for good (`r3d.c`, `colour_ok`; the log says "the 3D is drawn by the ARM from here"). So the cart starts on the GPU, and the first frame sends it to the ARM: 20 ms a frame (30 fps) with the light options. With the flat look the GPU stays but takes 88 ms (flag 128 draws a triangle at a time): slower. For the GPU on the RGB30 the kit needs opaque textures. With the GPU on a Mali the cart draws no effect (title of the panel: `Mali: no effects`).
Benchmark mode (Start + A, F5): the table `SCENES` in `main.lua` runs 16 scenes with a fixed camera and prints the
median frame time of each and a score. The values are a first choice: the owner sets the final parameters later.
Test: `make test-islandbench`.

## Tools

- `python scripts/bmkit.py island OUT.bmk [--quality hi|med|low] [--png PREVIEW.png]` builds the example. The
  models are made hi poly first (`hi`, the default: palm 696 triangles) and cut down later (`med`, `low`: palm 140).
  The names and the map classes do not change with the quality. The map and the sprites do not change.
- `python scripts/bmkit.py check FILE.bmk` reads a kit and checks the models, the layers and the object table.
- To put a kit in a game or a project: `python scripts/bmres.py add GAME.bm FILE.bmk -o OUT.bm`. A model whose name is
  taken gets a new name (`bush` becomes `bush2`): read the names after the merge.
- bm Studio: `c` on the models page sets the map class of a model.
