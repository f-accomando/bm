# Map kit (`.bmk`): a 3D map with its models and sprites

Status: format and example built on the PC (2026-10-29). The console does not read a kit yet. The owner gives the
final rules for kits later; this page records only what is built.

## Goal

One file holds a map for the 3D map page of bm SDK, with all the elements that go with it. The example is `island`:
an island in the ocean with a campfire in the middle. It becomes a graphics benchmark like `herocart` (3D and 2D
elements, flat colours and textures). Day, night, rain, snow and fog are not in the kit: the future `.bm` does them.

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

## Folder on the SD

`/bmk`, the same pattern as `/bmm` (one folder per extension). Nothing on the console scans it yet.

## Tools

- `python scripts/bmkit.py island OUT.bmk [--quality hi|med|low] [--png PREVIEW.png]` builds the example. The
  models are made hi poly first (`hi`, the default: palm 696 triangles) and cut down later (`med`, `low`: palm 140).
  The names and the map classes do not change with the quality. The map and the sprites do not change.
- `python scripts/bmkit.py check FILE.bmk` reads a kit and checks the models, the layers and the object table.
- To put a kit in a game or a project: `python scripts/bmres.py add GAME.bm FILE.bmk -o OUT.bm`. A model whose name is
  taken gets a new name (`bush` becomes `bush2`): read the names after the merge.
- bm Studio: `c` on the models page sets the map class of a model.
