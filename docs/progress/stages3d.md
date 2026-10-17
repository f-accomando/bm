# Stages 3D

A 3-stage 3D cartridge for the Pi (`.bm`, 640×360) and the RGB30 (`.b16`, 360×360): `carts/stages3d/`. Shared logic in `common/`,
one front per edition (`bm/` and `b16/`). Not in `GAMES`
(`Makefile`): it does not go into `dist/bm` or the Market.

## How it is today

- Layout: `common/` (`00_core` locals and the `GAME` table, `10_stages` the data, `20_world` map,
  height, collision, objects, `30_logic` movement, apples, goal, record, `40_states` the state
  machine and the log, `99_main` `_init/_update/_draw`), `bm/` (`50_front.lua`, `bm.info`),
  `b16/` (`50_front.lua`, `bm.info`, `README.txt`: the RGB30 edition), `assets/` (sheet and four map layers, made by `build/mksheet.py`
  and `build/mkmap.py`, committed), `build/build.py`.
- Build: `build.py EDITION OUT.lua` copies `common/*.lua` and `<EDITION>/*.lua` into `OUT`'s
  `src/` (a copy, not a reference: each edition has its own sources) and joins them in file-name
  order. `make build/carts/stages3d.bm`; `b16` builds `build/carts/stages3d.b16`
  (`make build/carts/stages3d.b16`).
- `FRONT` interface (the front defines it, each function gets `g`, the `GAME` table, read only):
  `title_draw`, `play_draw`, `clear_draw`, `final_draw`, `record_draw`. Fields of `g`: `state`,
  `stage` (1..3), `px py pz`, `apples`, `apples_total`, `stage_frames`, `total_frames`, `best`,
  `gpu`, `world`, `stages`, `objects`.
- The game: move on the plane with the d-pad, 2 apples and a goal per stage, three stages, the
  fewest frames is the record (`save`/`saved`). The three stages are three zones of ONE map: the
  3D map has a single `objects` layer (`src/bm/map3d.c`). The map reads one tile in 2×2, so the
  floor draws as stripes (the other three tiles of a cell are 0).
- Log lines: `stages3d: title gpu3d=on|off`, `stage <k> start`, `stage <k> clear frames=.. apples=a/t`,
  `final frames=..`, `record frames=.. saved=.. readback=.. best=..`, `title`.
- Test: `make test-stages3d` plays the three stages in bmhost with `tests/stages3d/input.txt`
  and `tests/stages3d/check.py` prints `stages3d: N/N checks passed` (not in `HOST_TESTS`).
  `gpu3d` is off in bmhost: the GPU path is not tried there. The camera pitch is negative
  (looks down), checked on a shot. `make test-stages3d-b16` plays the same script on the `.b16`
  (not in `HOST_TESTS`); without `--square` bmhost frames the 360×360 page in 640×360.

## Open work

- Run the `.b16` on the RGB30: 30 fps at 360×360 (optional 720×720), `3D: GPU` on the title, the
  buttons and the `save()` record (`mkbm.py` warns that saves are not designed for a `.b16`). Not
  verified on hardware.
- `test_stages3d_b16` (`tests/rgb30/qemu_test.py`, `-k stages3d`, needs `make build/carts/stages3d.b16`): boot, Dev tab,
  stage 3 reached by keys (Start starts the game in QEMU), no exception, in QEMU `virt`; not in the default
  `rgb30.mk` test run (decision: SPV). Not verified on hardware.
- A run on the Pi: 60 fps at 640×360 (`stat(1)` under 16.7 ms), `3D: GPU` on the title. Not verified on hardware.
- Fill the four tiles of each map cell, or the stripes stay.
- Content (enemies, jumps, music), into `GAMES` and the Market: when decided.

## Rules (do not break)

1. `common/` holds logic and data only: the screen is known by the front alone (`SCREEN_W`,
   `SCREEN_H`, `camera3d`, `light3d`, `draw3d`, `print`, `rect`).
2. The front is the only file that changes between editions and implements `FRONT`; it writes
   nothing in `GAME` and calls no `log`.
3. Each edition has its own front sources and `bm.info`, a Makefile rule and a `.bm`/`.b16`:
   writing `b16/` changes no file of `bm/`.
4. Changing `common/` changes both editions: `test-stages3d` runs on the `.bm` and must be
   repeated on the `.b16`; both tests (`test-stages3d`, `test-stages3d-b16`) must pass.
