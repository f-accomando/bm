# Project layout: games and apps

Common layout for `carts/<id>/` (games and apps). Migration is gradual: a folder without
`bm.info` still builds the old way (title, author and resolution from the Makefile).
Check it with `python scripts/carts_layout.py`: every project with `bm.info` must pass, and the table "Migrated so far" must match the folders.

```
carts/<id>/
  bm.info        metadata (INFO section), the single source: see below
  main.lua       entry point (always this name)
  cover.png      menu cover (optional)
  assets/        sheet.png, map.csv, map_<layer>.csv, flags.csv, sprites.txt, models.bm|glb
                 (the Makefile looks here first, then in carts/<id>/: old games still work)
  res/           shared resource files .bmm .bmi .bms .bmt .bmc .bmk   (docs/RISORSE.md)
  build/         the game's own build scripts (mk*.py, build.py)
```

## `bm.info`

Same syntax as the INFO section of a `.bm` (`src/bm/bm.h`, section 10): `key: value` lines,
`[type name]` blocks for parts, `#` lines are comments and are not copied. `mkbm.py --info
carts/<id>/bm.info` copies it into the cartridge.

| key | meaning | used by the build |
|---|---|---|
| `name` | name shown | default of `--title` |
| `author` | author | default of `--author` |
| `resolution` | `640x360`, `480x270`, `320x180`, `256x256`, `360x360`, `720x720` | default of `--res` |
| `type` | `game`, `app` or `utility` (no key = game) | bits 2-3 of the header flags (`mkbm.py`) |
| `desc`, `license` (SPDX id), `version`, `tags`, `date` | as in `bm.h` | stored only |

Unknown keys are kept in the cartridge. The binary `.bm` format does not change: an
older kernel ignores INFO.

`mkbm.py` mirrors `type` in bits 2-3 of the u16 flags at offset 18 of the header (`docs/BM-FORMAT.md`), so the menu
groups a cartridge without reading its INFO. On the SD, `.bm` and `.b16` games live in `/bm` and `.bme` projects in `/bme`.

## Where projects live on the SD card

- A `.bme` project lives in `/bme`. The tools write there: the editable copy of a game, a new project, Save as, Ctrl+S.
  The console makes the folder when it is missing. One helper builds the path: `bm_project_path()` in `src/bm/project.h`
  for the kernel, `U.project_path()` in `src/script/bmui.lua` for the tools.
- The menu and every open list of the tools read `/bme` first. They also read a `.bme` found next to its game (`/carts`
  or the root, old SD cards). The console does not move it. The tool saves it where it is and the status line says so
  (`old place`).
- `Ctrl+B` builds the game from a project in `/bme` into the games folder: `/carts` on the Pi, `/bm` on the RGB30
  (`bm_games_dir()`). An old project next to its game builds next to it, as before.
- The Pi menu scans `/`, `/carts`, `/bm` and `/bme`. The RGB30 menu scans `/bm` and `/carts`: it does not list projects, the
  SDK opens them. `/carts` stays as it is.

## File names the system writes

- The system writes names and extensions in lowercase: `/bme/name.bme`, `/carts/name.bm`, `name.b16`, `bm/config.txt`, `bm/reports/rptNNNNN.txt`, `bm/bench/3dNNNN.txt`, `bm/shots/pongNNNN.png`, `bm/rec/rNNNNNNN.bmv`, `bm/save/xxxxxxxx.sav`, `bm/web/name.prt`.
- The FAT driver writes an 8.3 entry in capitals with the NT lowercase flags (`fat.c`, `to_83()`). Windows, Linux and the console show the name in lowercase.
- A name typed by the user is lowercased too. A project name is the lowercase of what the user typed (up to 8 characters).
- Every lookup ignores case. A file of an old card, in capitals, is still read and replaced in place. The system never renames or deletes it.

## Credits: reusing other people's assets

Sharing among developers is free: nothing refuses a resource because of its licence. To keep the credit, list what you
reused in `bm.info` (it is copied into the INFO section like every key), one line each:

```
credit: knight.bmm - Ada Rossi - CC0-1.0 - https://example.org/knight
```

`bmres.py extract` keeps the `license`, `version` and `tags` of the cartridge it takes a resource from, and `bmres.py add` writes the
`origin` (SHA-256) and the author/licence of each part into the game's INFO.

## Migrated so far

| game | state |
|---|---|
| `pong` | `bm.info` |
| `yharnam` | `bm.info`; `sheet.png`, `map_*.csv` in `assets/`; `mk*.py`, `mapview.py`, `art/` in `build/` (they find `main.lua` and `assets/` from there) |
| `hunt` | `bm.info`; `sheet.png`, `map.csv` moved to `assets/`; `mkassets.py` moved to `build/` (writes `assets/`) |
| `snake`, `shooter`, `astrowing`, `typing`, `write` | `bm.info` (their `title_<id>` lines left the Makefile) |
| `village` | `bm.info`; `models.bm` moved to `assets/` |
| `kitchen`, `titan` | `bm.info`; `sheet.png` in `assets/`; `build.py`, `mkassets.py` and the other scripts (`import_chefs.py`, `mkrobot.py`, `art.py`) in `build/`; `titan_ex` shares `titan/assets/sheet.png` |
| `nano8` | `bm.info`; `build.py` and `mkdemo.py` in `build/` |
| `overbit` | `bm.info`; `build.py`, `mkcover.py`, `reel.py` and `viewer.lua` in `build/`; `art/` stays at the top (generators and Meshy figures, like `yharnam`). The `.b16` rules keep `--title`/`--res` |
| `code`, `assistant`, `editor` (bm SDK), `sound`, `animator`, `mesh`, `pixel` | `bm.info` used by their rule (`--info`); built into the kernel or installed on the SD as before |
| `studio` | `bm.info`; `sheet.png` moved to `assets/` |
| `bmweb`, `herocart` | `bm.info`; `src/` holds the modules and `build/` the build script |
| `demo`, `stress`, `texroom` | `bm.info` used by their rule (`--info`); the kernel embeds them. `sheet.png` and `map.csv` are in `assets/`; `make_assets.py` and `mkassets.py` are in `build/` |
| `sdk_b16`, `titan_ex` | `bm.info`; the `.b16` editions use it with `--info`; `build.py` is in `build/`; `sdk_b16` keeps `--res $(SDK_B16_RES)`; `titan_ex` shares `titan/assets/sheet.png` |

`stages3d` is not a project of this layout. It has one front per edition, and each front has its own `bm.info` (`bm/bm.info` and `b16/bm.info`). Logic is in `common/`, assets are shared, and `build.py` joins an edition's sources ([stages3d](progress/stages3d.md)). The tool does not list it.

## Files the console writes on the SD card

- `bm/sdmap.txt` is the cache of the SD map (Settings > System > SD map), rewritten by the console after each scan. The SD map lists `/glb` and `/bmm` as folders of their own, like `/bm` and `/bme`.

## What breaks

- Nothing in the `.bm` format. The build of a migrated game takes title, author and resolution
  from `bm.info`; its `title_<id>` line leaves the Makefile.
- Each migrated game's `.bm` gains an INFO section (a few hundred bytes).
- Moving a game's files into `assets/` and `build/` changes the paths in its own `mk*.py` and
  in any doc that names them (for `hunt`: `mkassets.py`, `GAME-GUIDE.md`, `GUIDA-GIOCHI.md`). The
  Makefile needs no change per game. The `.bm` built from `hunt` is identical except for INFO.

## Open decisions

See [open-decisions.md](open-decisions.md), section "Project layout".
