# Resources: one file per resource



Proposal of 2026-10-03, in progress on the `claude/lib-risorse` branch. It describes the files
that carry **a resource outside a `.bm`** (models, images, sounds, maps, palettes),
and the tools to extract them and integrate them. The menu has no tab for them (the Lib tab
is closed, see chapter 7): the editors open the resource files. Lua code as a resource is
not included: for now it stays out.

**Status (2026-10-25)**
- Steps 1–2 of chapter 10 done: the specification in `src/bm/bm.h`; `scripts/bmres.py`
  (`make test-res`: 112 checks in Python, 28 in C with the kernel's parser). The kernel
  reads `BMRES` files (`bm_parse_any`, `bm_zone`, `bm_info_get` in `format.c`).
- The Lib tab is removed (25 Oct 2026, user decision). The resource files are opened from
  the editors: bm Mesh, bm Studio, bm Pixel, bm Sound and bm Animator (Lua `ls`, `res_read`,
  `res_write`).
- To do: the apps that open every kind of resource file; SPRITES and tags in bm Pixel and in
  the other apps.

---

## 1. Starting point

- A game's resources live only in the sections of its `.bm` (`src/bm/bm.h`): SHEET or
  SHEET8 (sprites, textures, palette), MAP, COVER, AUDIO (sounds, effects, patterns, songs),
  MESH (models), ANIM (skeletons and animations).
- The sources live on the PC: `.png`, `.glb`, `.json` and `.csv` that `mkbm.py` turns into
  sections at build time. Only the `.bm` files reach the SD.
- On the SD, besides the games: bm Sound's sound packs in `/bm/sounds` (`.bm` with
  only AUDIO and a small program that plays them), the `.png`/`.jpg` images in `/pics`
  from which bm Studio makes a model ("Model from picture"), the nano8 cartridges in
  `carts/nano8`.
- The kernel reads PNG, JPEG and binary glTF (`src/bm/png.c`, `jpeg.c`, `glb.c`), today for
  the images in `/pics` and for the models from image-to-3D services.
- Today only the whole cartridge is exchanged. The only exception is bm Sound, which with
  "Import from…" takes a sound, an effect, a pattern or a song from another `.bm`.
- Three obstacles:
  - everything depends on the sheet: the models' texture corners are sheet pixels,
    the map uses sprite numbers, the code uses coordinates;
  - sprites, patterns, map and palette have no name;
  - author, licence and version exist only for the whole cartridge.

## 2. Common rules

1. **The same container as the `.bm`**: 128-byte header, section table,
   CRC-32, same section types. A section moves from a `.bm` to a resource file by
   copying it, and the existing readers (C `format.c`, Python `mkbm.py`, `bmmesh.py`,
   `bmaudio.py`, Lua in `bm3d.lua` and bm Sound) work almost as they are.
2. **A different signature, `BMRES`**: a resource file is not a game. The menu does not
   list it among the games (it only looks at `.bm` files) and `bm_parse` rejects it; the tools use
   a variant that accepts both signatures.
3. **No code**: a resource file never has a LUA section, so it can be
   received from anyone. The preview is made by the system.
4. **Self-contained**: the file carries what the resource depends on (a model its
   piece of texture and its skeleton, a song its patterns and sounds, a map its
   tiles).
5. **An INFO section in every file**: long name, author, licence, version, tags.
6. **8.3 names**: the console only writes names of 8 characters with a 3-character extension. The
   extensions are `bm` plus one letter (never `.bmp`, which belongs to bitmaps); the real name is
   inside the file.
7. **Games do not read them while running**: a resource enters a `.bm` by copying it
   (integration). Resource files are read by the menu, the development apps and the
   PC tools. A cartridge stays a single file, which works on its own.

## 3. The extensions

| Extension | Resource | Sections | On the console | On the PC |
|---|---|---|---|---|
| `.bmm` | 3D models | MESH, ANIM (if animated), SHEET8 or SHEET (the textures) | bm Studio, bm Mesh, bm Animator | bm Studio, bm Animator, `bmmesh.py`; from and to `.glb` |
| `.bmi` | images and sprites | SHEET8 or SHEET, SPRITES, BOXES (the zones' boxes), FLAGS | bm Pixel, bm editor, bm Studio (tiles) | bm Studio; from and to `.png` |
| `.bms` | sounds | AUDIO | bm Sound | `bmaudio.py`; from and to `.json`, to `.wav` |
| `.bmt` | tile map | MAP, SHEET8 or SHEET (the tiles) | bm editor | from and to `.csv` + `.png` |
| `.bmc` | palette | SHEET8 of N×1 pixels | bm Pixel, bm Studio (painting) | from and to `.hex`, `.gpl` |
| `.bmk` | kit (several types together) | any combination of the sections above | all, each takes its part | all |

Every file also has INFO.

### `.bmm`: models
- MESH with one or more models (up to 256), with the texture margin as in the `.bm`.
- ANIM with the skeletons of the animated models (bound to the model by name).
- A sheet with **only the cells used by the textures**, with the texture corners referring
  to this sheet. A colours-only model has no sheet.

### `.bmi`: images and sprites
- The sheet (or a piece of it) and the SPRITES section with the named zones.
- BOXES with the hitboxes and hurtboxes of the zones' frames (2026-10-04): they travel with their
  zone (extracted, added with a new name).
- Without SPRITES the whole image is a single entry.
- An image can also become the cover of a `.bm` or bm Studio's tiles.

### `.bms`: sounds
- The AUDIO section as it is (the `BMAU` bank, `src/audio/player.h`).
- The entries are the songs, the effects and the sounds (instruments); patterns travel with the
  songs that use them.
- The packs in `/bm/sounds` (`.bm`) can still be read.

### `.bmt`: maps
- MAP and a sheet with **only the tiles used by the map**, with the map's numbers
  referring to this sheet.

### `.bmc`: palettes
- A SHEET8 of N×1 pixels (N up to 256): its palette is the palette, pixel *i* is
  colour *i*. No new format to read.
- On the PC: `.hex` (one colour per line, Lospec's format) and `.gpl` (GIMP).

### `.bmk`: kits
- A set of resources of different types with **a single shared sheet**, for example a
  character with model, animations, sprites and sounds.
- The editors list a kit and its parts.
- A map kit (3D map, models, sprites, object table in INFO): [design/map-kit](design/map-kit.md),
  tool `scripts/bmkit.py`. SD folder: `/bmk` (not scanned by the console yet).

## 4. The `BMRES` container

Little endian, like the `.bm`. Only bytes 0–7 and 12–16 change; the rest is in the same
place, so the readers share the code.

| Offset | Type | Content |
|---|---|---|
| 0 | char[8] | signature `"BMRES"` and three zero bytes |
| 8 | u16 | version (1) |
| 10 | u16 | header size (128) |
| 12 | u16 | type: 1 `.bmm`, 2 `.bmi`, 3 `.bms`, 4 `.bmt`, 5 `.bmc`, 6 `.bmk` |
| 14 | u16 | reserved (0) |
| 16 | u8 | reserved (0) |
| 17 | u8 | number of sections |
| 18 | u16 | reserved (0) |
| 20 | u32 | CRC-32 of everything after the header |
| 24 | char[48] | display name (UTF-8) |
| 72 | char[32] | author |
| 104 | … | reserved (0) up to 128 |
| 128 | | section table: for each one `u32 type, u32 offset, u32 size, u32 reserved`; bodies aligned to 4 bytes |

- **The type counts more than the extension**: the extension is for lists, but a
  renamed file is recognised by its type.
- The allowed sections are those in the table of chapter 3. A reader ignores the
  sections it does not know and keeps them when it rewrites the file; a file with a
  LUA section is rejected.
- One sheet per file (SHEET or SHEET8, never both), as in the `.bm`.
- The limits are those of the `.bm`: sheet up to 4096×4096, 256 models, 64 bones, the sound
  bank as it is.

## 5. New sections

### INFO (10)
UTF-8 text, `key: value` lines. First the file's lines; then, for the single entries,
blocks starting with `[type name]`.

```
name: Cavalieri del villaggio
author: Mario
license: CC-BY-4.0
version: 2
tags: personaggio, medievale
origin: 9f86d081884c7d65...

[model knight]
desc: cavaliere con spada, 3 animazioni
tags: personaggio, armatura

[sfx clang]
tags: metallo, colpo
```

- Keys: `name`, `desc`, `author`, `license` (SPDX code: `CC0-1.0`, `CC-BY-4.0`…),
  `version`, `tags` (comma-separated), `origin` (SHA-256 of the resource it was
  copied from), `date` (YYYY-MM-DD).
- Block types: `model`, `sprite`, `sound`, `sfx`, `song`, `map`, `palette`.
- At most 16 KB. Unknown keys are kept.
- Computed tags (animated, textured, triangle count, size) are not
  written: the reader derives them.
- INFO can also be in a game's `.bm`: old kernels ignore it and `bm_rewrite`
  keeps it.

### SPRITES (11)
Named zones of the sheet.

```
u16 zones (1..1024), u16 reserved (0)
per zone, 28 bytes:
  char[16] name (UTF-8, zero-padded, unique in the section)
  u16 x, y, w, h     sheet pixels (w, h ≥ 1)
  u8  frames         1..16
  u8  fps            0 = still
  u16 reserved (0)
```

- The frames after the first are the w×h boxes to the right of the first, on the same row
  (as in bm Pixel until the strip wraps): a zone moves in the sheet
  whole, without the frames changing order.
- It can also be in a game's `.bm`: the sprites get a name, and the frames and
  speed that bm Pixel keeps today in its save move there. Games
  draw them with `zspr(name, x, y)` (R11, 2026-10-04).

### BOXES (14)
The boxes of the frames of the SPRITES zones: hitbox, hurtbox (2026-10-04).

```
u16 boxes (1..4096), u16 reserved (0)
per box, 28 bytes:
  char[16] zone (the name of a SPRITES zone)
  u8  frame          1..16, 0 = all frames of the zone
  u8  type           0 hurt, 1 hit, 2 body, 3..255 the game's
  i16 x, y           from the frame's top-left corner (they can go outside it)
  u16 w, h           ≥ 1
  u16 reserved (0)
```

- In a `.bm`, broken boxes (or boxes of a zone that does not exist) are ignored; a resource
  file with such boxes is rejected.
- Games read them with `zboxes(name, [frame, type])`; `mkbm.py --sprites`
  writes them from the `hurt|hit|body frame x y w h` lines under the zone.

## 6. Extraction and integration

**Extraction** (from a `.bm` to a resource file):
- a model becomes `.bmm` with its skeleton and only the texture cells it uses;
- a sheet zone becomes `.bmi` (with SPRITES if it has a name or frames);
- chosen songs, effects or sounds become `.bms`, with the patterns and sounds they
  depend on;
- the map becomes `.bmt` with the tiles it uses;
- the palette of a SHEET8 becomes `.bmc`;
- all of a game's resources become `.bmk`.

**Integration** (from a resource file to a `.bm`):
- textures, sprites and tiles: free 8×8 cells in the destination sheet, in islands (the
  boxes that faces, zones or tiles use together). A cell is free if it is
  transparent and no face, zone or tile uses it; cell 0 is never used (in the
  map it is the empty one). If there is no room the sheet grows **in height**, up to 4096: the
  width stays, so the sprite and map numbers do not change. An island wider
  than the sheet does not fit (a message says so). Then the texture corners,
  the SPRITES zones and the map numbers are corrected;
- palette: the colours are merged; beyond 256 the sheet becomes SHEET by itself, as
  `sheet_section` in `runtime.c` already does;
- names: if they already exist, a number is added (16 characters for models and zones, 8 for
  sounds);
- the skeleton follows its model;
- the sounds go into the free slots, as bm Sound's "Import from…" already does;
- the resource's INFO (author, licence, tags) goes into the entry's block, with `origin`.

Integration never removes anything from the destination `.bm`. Extracting a resource and
integrating it into an empty project must give the same sections, byte for byte (except INFO,
which in the project receives the entries' blocks with `origin`).

## 7. The Lib tab (closed)

- The menu has no Lib tab. The tabs are **Market · Games · Dev · Settings** (keys `1`–`4`).
- The user closed the tab on 6 Oct 2026 (hidden) and removed its code on 25 Oct 2026. Before, it
  listed the resource files and the resources inside the `.bm` files, with a preview.
- Open a resource file from an editor: bm Mesh and bm Studio for models, bm Pixel for images
  and palettes, bm Sound for sounds. Each editor lists its own files.
- The kernel still reads the resource files of a game (`format.c`, `BMRES`). It no longer
  scans the card for them.
- The previous design is in the git history (`src/kernel/lib.c`, `libview.c`, QEMU test
  `test_lib_tab`).

## 8. PC tools

- `scripts/bmres.py`, with only Python's standard library like `mkbm.py`:
  - `list FILE`: what a `.bm` or a resource file contains;
  - `extract GAME.bm --model villager -o VILLAGER.bmm` (and `--sprite`, `--sfx`, `--song`,
    `--map`, `--palette`, `--all`);
  - `add GAME.bm FILE...`: integrates, with the rules of chapter 6;
  - `convert`: from and to `.glb`, `.png`, `.json`, `.csv`, `.hex`, `.gpl`;
  - `info FILE --tags …`: writes INFO.
- Previews on the PC: `tools/bmrender.py` for models (PNG), `make wav` for sounds.
- `mkbm.py --res FILE` (also several times) integrates resources at build time: a game
  in the repository can keep its files in `carts/<game>/res/`.
- The console's bm Studio and bm Animator will open and save `.bmm`, `.bmi` and `.bmk`
  (step 7).

## 9. Compatibility and tests

- Old kernels do not see resource files (the menu lists only `.bm` files) and ignore
  INFO and SPRITES inside a `.bm`; the tools keep them.
- The packs in `/bm/sounds` can still be read; bm Sound saves new ones as `.bms`
  in `/bm/lib`.
- The format lives in three languages (C, Lua, Python): as for the sound bank
  (`make test-sound`), a test extracts, re-integrates and compares the sections byte by byte.
- Every reader checks the data before using it (`bm_mesh_check`, `bm_anim_check`,
  `au_parse` already do), and a test tries deliberately corrupted files.
- Licences: INFO carries them through copies; to publish in the store (M25) they are
  mandatory.
- Exchange (M24–M26): resource files travel like `.bm` files, on the local network and in the
  store, each with its SHA-256.

## 10. Steps

1. Specification in `src/bm/bm.h` (`BMRES`, INFO, SPRITES) and this document.
2. `scripts/bmres.py` with the tests on the PC: extraction, integration, conversions.
3. Kernel: reading `BMRES` (done). The Lib tab (list, previews, QEMU test) was built, then removed
   on 25 Oct 2026.
4. The apps open the resource files.
5. Tags and details (INFO); SPRITES in bm Pixel.
6. The console's bm Studio and bm Animator open and save resource files.
