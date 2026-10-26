# Yharnam

The game: `carts/yharnam/`. Its graphics come from `build/mkassets.py`; after a change, run it
again and commit `assets/sheet.png`. Never commit a sheet made with `YH_DRAFT=1`.

---

## 1. Architecture and Structure

- **Resolution & Screen**: 360×360 (gothic 3/4 isometric view, perfect for the RGB30's 1:1 panel and the `.b16` format).
- **World & Streaming**: Continuous map generated in chunks of 16×16 cells during movement (`visible_chunks`, `stream()`).
- **Lighting**: Per-pixel light levels with fade tables (`fades`, `dark_begin`, `glow`, `dark_end`) inspired by Dank Tomb, gas lanterns, braziers and pyres.
- **Graphics Pipeline**: Procedural generation of over 5,000 frames via Signed Distance Fields (`build/mkassets.py`, `build/art/sdf.py`, `art/anims.py`, `art/hunter.py`), Hunter animations on 8-direction cubic Hermite curves, with automatic weapon collision resolution (`unclip`).
- **Layered Map**: `map_ground.csv`, `map_overlay.csv` and `map_objects.csv` (256×256 cells), drawn in the SDK and converted with `mkmap.py`. `make yharnam-map` makes them again from the street plan of `main.lua`.
- **Areas**: `MAP.grid` gives one area for each half-chunk cell (16×16 cells of 8×8 tiles). Four areas: central, forest, cathedral, forbidden. Each has one boss and two hunter's lamps.
- **Borders**: the borders between areas, the outer edge of the map and three interior walls are walls, rocks, tree lines, iron gates, hidden passages and a deadfall (`add_border` in `gen()`, `MAP.wall`, `MAP.decor`, `MAP.cuts`). There is no fog: the camera stops at the edge (`clamp_cam`).

---

## 2. Open Checks and Verifications

### A. Performance on Pi Zero W and RGB30
- Measure the frame cost at 60 fps (background chunk generation, `stream()`, tiered lighting, particles).
- Collect the numbers with the dev kit session report and the `devinfo()` lines at the end of a run.
- Compare the parameters with the 60 fps budget of the `.b16` profile.
- Numbers so far: 25 Oct session, 59.0 fps, 15.6 ms a frame, 14.8 ms of it drawing (the light on the ARM). The fade on the QPU (`gpu3d_qpu=1`) costs 8.6 ms against 20.4 ms on the ARM at 640×360 (`docs/PERF-BUDGET.md`). Open: a 10 minute run on the Pi with `set gpu3d_qpu=1`, then `all`, `on the spot` and `in time` of the report.

### A2. Not checked on a device
- The look of the walls, gates, hidden passages and the deadfall with its horde in the game (checked on the PC only: `bmplay` pictures and `make test-yharnam`).
- The start fade, the blood on the boss text, the colder shadows and the keyboard.
- The border walls at the map edge are dark and hard to read on the PC pictures.

### B. Map Drawn on the Pi
- Hardware test procedure:
  1. Open `yharnam.bm` in the SDK on the console.
  2. Map page: `F3` twice, 16×16 brush (`z`).
  3. Place terrain tiles and, on the `objects` layer (`l`), place placeholders (e.g. tree, house with H7 corner and roof tiles).
  4. Save with `Ctrl+S` (generates the `YHARNAM.BME` copy, ~6 MB: the sheet stays in compact `SHEET8` format).
  5. Try with `F5`: verify that the modified terrain and objects appear in the game.
  6. Run `python3 carts/yharnam/build/mkmap.py --from` on the cartridge extracted from the SD and commit the updated CSVs.

### C. Graphical Glitch at Startup
- Investigate the small visual defect present in the first frames:
  - Document on the Pi when it appears, its exact duration and the screen (title or initial transition after the first chunks load).
  - Investigation points:
    - `_init`: check whether the fade and dissolve tables are fully initialized before the first `_draw` frame.
    - `visible_chunks`: check that chunks not yet completed are excluded from rendering until they are ready.

---

## 3. Objects, Places and Level Design (R36)

- **Placeholders in the `objects` Layer** (`map_objects.csv`):
  - Hunter lanterns (`shrine` = 1186)
  - Barricades (`barricade` = 1188)
  - Gates (`gate` = 1190)
  - Boss arenas (`boss` = 1192)
  - They can be placed and moved directly in the `bm SDK`.
- **Creature Spawning**: the terrain type (`ch.kind`: road, sidewalk, grass) is derived dynamically from the tiles drawn in `map_ground.csv`, so monster spawning follows the changes made in the SDK.
- **Border placeholders** (appended in `art/places.py`, in this order): `wall_h`, `wall_v`, `rocks`, `treeline`, `iron_gate_h`, `iron_gate_v`, `passage_h`, `passage_v`, `deadfall`. In the SDK, the key `p` shows their real sprite (`assets/sprites.txt`, zones `pv<cell>`).
- **To be completed**:
  - Constrain each house within its own 16×16 block.
  - The cut walls and the gates stay in `MAP` in the code. The areas come from `MAP.grid` in the code, not from the map file.
  - The map of 26 Oct was made again from the plan: edits made by hand in the SDK before this date are not in it.

## 4. Map on the Pi (moved from tools)
- Map on the Pi: open `yharnam.bm` in the SDK, F3 F3, edit ground,lamps/barricades/gates/bosses on `objects`,
  Ctrl+S (`YHARNAM.BME`, about 6 MB), F5; then `mkmap.py --from` and commit the CSVs. A house
  stays in its 16×16-tile block.

## 5. Controls and editions
- One `.bm` runs on the Pi Zero W and on the RGB30 (`targets: pi,rgb30`, `input: pad,keyboard`, `aspect: square` in `bm.info`). The Market `.bm` is made by `make market-seed`.
- The keyboard is a controller for `btn()`. Move: arrows or W A S D. A (Space, Z, J): saw cleaver, light a lamp. B (X, K): run, backstep. X (C, L): pistol. Y (V, I): open or close the saw cleaver. Start (Enter): pause. Menus use "ok" (Space) and "back" (X).
- The hints on the screen come from `prompt()`: they show the key of the controller in use.
- Not verified: the frame rate on the Pi Zero W (see 2.A), and the keyboard on the device.
- The `.b16` of Yharnam stays as it is (`B16_AS_BM`: the same bytes as the `.bm`, flag bit 1). The user checks the `.b16` editions separately.
- Start: the title is the town in the dark with a small light round the hunter and only the chip of the controller in use (`prompt("ok")`, no bar, no text). After the press the state is `intro` for 150 frames (`INTRO_LEN`): the light grows from the hunter to the screen edges (`dark_mask`, a stippled rim), the controls do not work, then the state is `play`. Checked on the PC with `bmplay` (frames saved as pictures); not checked on a device.
- A boss slain: "PREY SLAUGHTERED" has no black bar: its letters are red, blood drips from them, then the text slides away. No written banner is left ("A WAY GIVES", "LAMP LIT" and the others are gone). The hints at the hunter's lamps stay.
- Light: the shadows are lighter and colder (the night colours in `_init`); each lit street lamp has a wide faint ring (radius 120, level 3) round its strong light (radius 74). In the start fade the hunter is drawn again over the dark, at full light.
- `main.lua` has 10 `--@module` markers (numbers, plan, chunks, hunter, particles, sound, frame, actions, foes, drawing): bm Code shows them as tabs. The markers are comments: the build does not change. The modules `foes` (1684 lines) and `chunks` (1090 lines) are still long.
- Animations: the module `anim` of `main.lua` (`ANIM.play`, `ANIM.step`, `ANIM.frame`) plays the animations of the hunter and of the creatures. The old `play`, `step_anim`, `FOE.play`, `FOE.step` and `FOE.frame` call it. Checked on the PC: five pictures of a run and the bot's result are the same before and after the change. Manual *characters*, chapter *Top-down characters, 8 directions*.

---

## Rules (do not break)

- `MAP.grid` has one area for each cell. A forest cell never touches a cathedral cell along a side (a load-time check stops the game).
- The 8 hunter's lamps are at least 2 chunks apart (not even diagonal). Each lamp chunk and each boss chunk is inside one area.
- The kinds in `art/places.py` are append only: a tile keeps its number, and the drawn map keeps its numbers.
- A change to the plan (areas, streets, borders, lamps): run `make yharnam-map`, then `make test-yharnam` (it checks the plan and the drawn map), then rebuild the `.bm` and the `.bme`. Without a new map the game shows the old things.
- The `.bme` is made from the `.bm`: `python3 scripts/mkbm.py --from build/carts/yharnam.bm -o carts/yharnam/yharnam.bme`. Check it with `mkmap.py --from carts/yharnam/yharnam.bme --out DIR`: the three CSV files must be the same.
- Run `mkassets.py` without `YH_DRAFT`; it rewrites the atlas in `main.lua` and keeps the `--@module` lines.
