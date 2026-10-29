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
| `targets` | the kernels the cartridge runs on: `pi`, `rgb30` or `pi,rgb30` | default of `--targets` (the compatibility block, bytes 120..127) |
| `input`, `aspect`, `needs` | lists of names, as the flags `--input` (`pad,keyboard,text,mouse,mouse_optional`), `--aspect` (`wide,square,any,safe_area`) and `--needs` (`3d,audio,net,mouse_hw`) | defaults of the flags of the same name |
| `min_ram` (MB), `min_spec`, `min_width` (pixels) | numbers, as `--min-ram`, `--min-spec` and `--min-width` | defaults of the flags of the same name |
| `desc`, `license` (SPDX id), `version`, `tags`, `date` | as in `bm.h` | stored only |

The keys of the compatibility block use the syntax of the flags. A flag wins over the key. A key of the block
(`input`, `aspect`, `needs`, `min_ram`, `min_spec` or `min_width`) without `targets` is an error, as the flag
without `--targets` is. A `.bme` carries the same keys: the SDK build writes the block from them.

Unknown keys are kept in the cartridge. The binary `.bm` format does not change: an
older kernel ignores INFO.

`mkbm.py` mirrors `type` in bits 2-3 of the u16 flags at offset 18 of the header (`docs/BM-FORMAT.md`), so the menu
groups a cartridge without reading its INFO. On the SD, `.bm` games live in `/bm` and `.bme` projects in `/bme`.

## Add a cart to the Makefile

One rule builds every cart (`build/carts/<id>.bm`, and `build/<tool>.bm` for the built-in tools).
The rule is in the Makefile, section "Cartridges: one data-driven recipe".

1. Make the folder `carts/<id>/` with `main.lua`. Add `bm.info`, `cover.png` and the files of `assets/` that the cart uses.
2. Add `<id>` to `GAMES` in the Makefile. Then `make build/carts/<id>.bm` builds it.
3. The rule adds each file that exists: `bm.info`, `cover.png`, `sheet.png`, `map.csv`, `flags.csv`, `sprites.txt`, `models.bm` or `models.glb`. Each file is also a prerequisite, so a change rebuilds the cart.
4. Set a variable only where the cart differs from the default:

| Variable | Meaning |
|---|---|
| `cdir_<id>` | folder of the cart (default `carts/<id>`) |
| `lua_<id>` | the Lua file (default `main.lua` in the folder). For a cart joined by a `build.py`, give `$(BUILD)/<id>/main.lua` and write the rule of that file |
| `bminfo_<id>`, `cover_<id>`, `sheet_<id>`, `models_<id>`, `audio_<id>` | a file other than the default |
| `noinfo_<id> := 1` | no `bm.info`: use `title_<id>` and `res_<id>` (default `640x360`) |
| `sheet8_<id> := 1` | sheet with a palette and runs |
| `layers_<id> := a b` | map layers, the files `map_a.csv`, `map_b.csv`, in this order |
| `extra_<id>` | more `mkbm.py` flags, at the end |

A variant of a cart has its own key. Its rule names the key:
`$(call CART_RECIPE,<key>)`. A `.bme` project (`BME_CARTS`) uses the same recipe.
Special builds keep their own rules: the Overbit variants (reel, match), the test carts.

## Where projects live on the SD card

- A `.bme` project lives in `/bme`. The tools write there: the editable copy of a game, a new project, Save as, Ctrl+S.
  The console makes the folder when it is missing. One helper builds the path: `bm_project_path()` in `src/bm/project.h`
  for the kernel, `U.project_path()` in `src/script/bmui.lua` for the tools.
- The menu and every open list of the tools read `/bme` first. They also read a `.bme` found next to its game (`/carts`
  or the root, old SD cards). The console does not move it. The tool saves it where it is and the status line says so
  (`old place`).
- `Ctrl+B` builds the game from a project in `/bme` into the games folder: `/bm` on both consoles
  (`bm_games_dir()`). An old project next to its game builds next to it, as before.
- Decision of 25 Oct 2026 (done the same day): both kernels stop scanning `/carts`. The games folder is `/bm`. The Pi menu
  scans `/`, `/bm` and `/bme`; the RGB30 menu scans `/bm`. The Market, a file sent from the PC to `carts/` (it is stored in
  `bm/`), the games a cartridge writes and `make sdcard` all use `/bm`. A game left in `/carts` on an old card is not listed:
  move it to `/bm` (`make install` and `easy_install.sh` do it). The tools still read `/carts` and the root for a `.bme` next to its game.
  No menu tab lists a project on either kernel (user decision 21 Oct 2026, D44): the editors open the projects.
- `/glb` and `/bmm` are the folders of APP-9 (the model import, `docs/progress/tools.md`): the editors read and write them.
  The menus never list them. The SD map counts them.

## File names the system writes

- The system writes names and extensions in lowercase: `/bme/name.bme`, `/carts/name.bm`, `bm/config.txt`, `bm/reports/rptNNNNN.txt`, `bm/bench/3dNNNN.txt`, `bm/shots/pongNNNN.png`, `bm/rec/rNNNNNNN.bmv`, `bm/save/xxxxxxxx.sav`, `bm/web/name.prt`.
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
| `smw` | `bm.info`; code in `src/` (`physics.lua`, `game.lua`), joined into `main.lua` by `build/build.py`. The assets come from the player's own ROM into `extracted/` (ignored by git) |
| `snake`, `shooter`, `astrowing`, `typing`, `write`, `ko2` | `bm.info` (their `title_<id>` lines left the Makefile) |
| `kitchen`, `titan` | `bm.info`; `sheet.png` in `assets/`; `build.py`, `mkassets.py` and the other scripts (`import_chefs.py`, `mkrobot.py`, `art.py`) in `build/`; `titan_ex` shares `titan/assets/sheet.png` |
| `nano8` | `bm.info`; `build.py` and `mkdemo.py` in `build/` |
| `overbit` | `bm.info`; `build.py`, `reel.py` and `viewer.lua` in `build/`; `art/` stays at the top (generators and Meshy figures, like `yharnam`). |
| `code`, `assistant`, `editor` (bm SDK), `sound`, `animator`, `mesh`, `pixel` | `bm.info` used by their rule (`--info`); built into the kernel or installed on the SD as before |
| `studio` | `bm.info`; `sheet.png` moved to `assets/` |
| `bench3d` | `bm.info`; `build/mkassets.py` builds the models and the sprite sheet at build time (no binary in git) |
| `bmweb`, `herocart` | `bm.info`; `src/` holds the modules and `build/` the build script |
| `kratos` | `bm.info`; `build/glb2parts.py` writes the PARTS table into `main.lua` (between the DATA lines); the `.bme` is built with the `.bm` |
| `sdk_b16`, `titan_ex` | `bm.info`; `sdk_b16` uses it with `--info`; `build.py` is in `build/`; `sdk_b16` keeps `--res $(SDK_B16_RES)`; `titan_ex` shares `titan/assets/sheet.png` |

## Files the console writes on the SD card

- `bm/ai.txt` holds the AI settings of bm Agent (`url=`, `model=`, `key=`), apart from `bm/config.txt`. `bm/ca-user.pem` holds the roots the user added (the relay's CA). The kernel writes both only after its own question. A release update never changes them.
- `bm/sdmap.txt` is the cache of the SD map (Settings > System > SD map), rewritten by the console after each scan. The SD map lists `/glb` and `/bmm` as folders of their own, like `/bm` and `/bme`.

## What breaks

- Nothing in the `.bm` format. The build of a migrated game takes title, author and resolution
  from `bm.info`; its `title_<id>` line leaves the Makefile.
- Each migrated game's `.bm` gains an INFO section (a few hundred bytes).
- Moving a game's files into `assets/` and `build/` changes the paths in its own `mk*.py` and
  in any doc that names them. The Makefile needs no change per game.

## Open decisions

See [open-decisions.md](open-decisions.md), section "Project layout".
