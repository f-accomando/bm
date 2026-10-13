# Project layout: games and apps

Common layout for `carts/<id>/` (games and apps). Migration is gradual: a folder without
`bm.info` still builds the old way (title, author and resolution from the Makefile).

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
| `desc`, `license` (SPDX id), `version`, `tags`, `date` | as in `bm.h` | stored only |

Unknown keys are kept in the cartridge. The binary `.bm` format does not change: an
older kernel ignores INFO.

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

## What breaks

- Nothing in the `.bm` format. The build of a migrated game takes title, author and resolution
  from `bm.info`; its `title_<id>` line leaves the Makefile.
- Each migrated game's `.bm` gains an INFO section (a few hundred bytes).
- Moving a game's files into `assets/` and `build/` changes the paths in its own `mk*.py` and
  in any doc that names them (for `hunt`: `mkassets.py`, `GAME-GUIDE.md`, `GUIDA-GIOCHI.md`). The
  Makefile needs no change per game. The `.bm` built from `hunt` is identical except for INFO.

## Open decisions

See [open-decisions.md](open-decisions.md), section "Project layout".
