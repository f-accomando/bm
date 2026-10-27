# Yharnam

The game: `carts/yharnam/`. Its graphics come from `build/mkassets.py`; after a change, run it
again and commit `assets/sheet.png`. Never commit a sheet made with `YH_DRAFT=1`.

---

## 1. Architecture and Structure

- **Resolution & Screen**: 360×360 (gothic 3/4 isometric view, perfect for the RGB30's 1:1 panel).
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
- Compare the parameters with the 60 fps budget on the RGB30.
- Numbers so far: 25 Oct session, 59.0 fps, 15.6 ms a frame, 14.8 ms of it drawing (the light on the ARM). The fade on the QPU (`gpu3d_qpu=1`) costs 8.6 ms against 20.4 ms on the ARM at 640×360 (`docs/PERF-BUDGET.md`). Open: a 10 minute run on the Pi with `set gpu3d_qpu=1`, then `all`, `on the spot` and `in time` of the report.

- Pi Zero W, 27 Oct 2026 (360x360, sessions in `reports/claude-yharnam-gpu2d/`): all ARM, 36.6 to 39.4 fps, draw 21.8 to 23.4 ms (light 12.6 to 13.6 ms: `glow` 8.6, fade 4.0 to 4.6; the rest 9.1 to 9.8 ms). `dark_begin(ambient, 2)` (`LIGHT_STEP = 2` in `main.lua`, a level for each 2x2 pixels): 50.5 fps, draw 16.4 ms, light 6.3 ms (`glow` 2.2, fade 4.1). Not a gain on the Pi: `gpu3d_qpu=1` (fade 4.6 ms against 4.1 on the ARM, the QPUs ran: 5631 of 5631) and a table of the lamp's profile by squared distance (`glow` 2.2 ms as before, 98% of the tables found).
- Open: `LIGHT_STEP` is 1 in the cartridge until the picture is judged on the Pi at step 2 (50 fps against 37 to 39). Open: for 60 fps about 2.5 ms are missing (19.2 ms a frame at step 2): the fade 4.1 ms and the map, sprites and text 10 ms on the ARM; the GPU as it is did not help (see above). Next idea, a task of its own: the 2D map on the GPU as resident batches (a buffer of quads for each block of tiles, drawn with an offset), as `map3d.c` does for the 3D. `gpu2d_only=1` (bm3d 7.16, hidden key, off) is measured, see above. The session report has the lines `2D: GPU` and the light's share; the overlay (F11) has a row `2D: GPU wait`. Host: the same pictures as the ARM (`make test-gpu2d-only`). Default on only after the Pi shows it pays.
- Pi Zero W, 27 Oct 2026, `gpu2d_only=1` on the step 2 build (`reports/claude-yharnam-gpu2d/`): the GPU did start (`2D: GPU`, 3D on) but took 58 triangles a frame (about 29 quads of 2900 operations), 1.2 ms of GPU a frame: 48.5 fps, draw 17.0 ms (16.4 without the key). Cause: Yharnam's sheet is 4096 x 3400 and the driver takes sheets up to 2048 x 2048 (`tex_get`), so every `spr`, `sspr` and `map()` cell fell back to the ARM; only rectangles went to the GPU. To use it the driver would split the sheet into 2 x 2 textures and convert 14 M texels (28 MB as 16 bits, 55 MB as 32), a start-up cost and a memory cost, for a gain that was estimated at 0 to 3 ms. The key stays off.
- Pi Zero W, 27 Oct 2026, `gpu2d_only=1` with `gpu2d_sheet(0, 0, 1536, 32)` (the strip of the map's 408 cells; `reports/claude-yharnam-gpu2d/`, 3939 frames): the GPU took the map, 5302 triangles a frame (about 2650 quads), 2.0 ms of GPU a frame. Worse than the ARM: 44.3 fps, draw 18.5 ms (16.4), light 8.8 ms (6.3: `dark_begin` builds and starts the job), the rest 9.7 ms (10.0), 22.0 ms a frame (19.2). The ARM copies the opaque tiles fast and the quads cost it as much (6 new vertices each, the job, the cache upkeep): the GPU's 2D has no resident geometry, unlike the 3D. The window call is not in the cartridge; the key stays off. The `sspr` of Yharnam read the whole sheet (x 0..4095, y 33..3377), so a window cannot hold them. Host: the same pictures as the ARM (`make test-gpu2d-only`, also with the window).

- Pi Zero W, 27 Oct 2026, autoplay (`yharnam_autoplay=1`, 7200 frames, 206 s) on a kernel with the 2D call timers (`reports/claude-2d-cost-counters/`), all ARM, `LIGHT_STEP` 1: 34.9 fps, update 2.9 ms, draw 23.0 ms, audio 2.0 ms. Of the draw: light 13.0 ms (fade 4.6, `glow` 8.4), 2D calls 5.7 ms, Lua and the rest 4.3 ms. The 2D calls a frame: `map` 2.9 ms (2 calls, one for each layer), `sspr` 1.7 ms (74 calls), `spr` 0.9 ms (99 calls), shapes 0.1 ms (112 calls), `print` and `stack` about 0. So the map is the largest 2D cost (half of the 5.7 ms); the sprites together 2.6 ms.

- Pi Zero W, 27 Oct 2026, autoplay, `map()` from a packed copy of the cells (`docs/progress/graphics.md` Rules): `map` 2.9 -> 2.0 ms (2650 cells a frame, 0.75 us a cell), draw 23.0 -> 22.0 ms, 34.9 -> 36.2 fps (`LIGHT_STEP` 1). `spr` 0.9 ms and `sspr` 1.7 ms unchanged. What is left of the 2D calls: `sspr` 1.7 ms (74 calls, 23 us each, rectangles of the 4096 x 3400 sheet), `spr` 0.9 ms (99 calls), `map` 2.0 ms (the page is written 2 times: the two layers). Open: a sprite is read from two arrays (pixels and alpha) of the wide sheet; one packed array with the alpha in it would halve the misses.

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
- Yharnam is one `.bm` for the Pi and the RGB30. The user checks it on both devices.
- Start: the title is the town in the dark with a small light round the hunter and only the chip of the controller in use (`prompt("ok")`, no bar, no text). After the press the state is `intro` for 150 frames (`INTRO_LEN`): the light grows from the hunter to the screen edges (`dark_mask`, a stippled rim), the controls do not work, then the state is `play`. Checked on the PC with `bmplay` (frames saved as pictures); not checked on a device.
- A boss slain: "PREY SLAUGHTERED" has no black bar: its letters are red, blood drips from them, then the text slides away. No written banner is left ("A WAY GIVES", "LAMP LIT" and the others are gone). The hints at the hunter's lamps stay.
- Light: the shadows are lighter and colder (the night colours in `_init`); each lit street lamp has a wide faint ring (radius 120, level 3) round its strong light (radius 74). In the start fade the hunter is drawn again over the dark, at full light.
- `main.lua` has 10 `--@module` markers (numbers, plan, chunks, hunter, particles, sound, frame, actions, foes, drawing): bm Code shows them as tabs. The markers are comments: the build does not change. The modules `foes` (1684 lines) and `chunks` (1090 lines) are still long.
- Animations: the module `anim` of `main.lua` (`ANIM.play`, `ANIM.step`, `ANIM.frame`) plays the animations of the hunter and of the creatures. The old `play`, `step_anim`, `FOE.play`, `FOE.step` and `FOE.frame` call it. Checked on the PC: five pictures of a run and the bot's result are the same before and after the change. Manual *characters*, chapter *Top-down characters, 8 directions*.

---

## Autoplay (benchmarks)

- Autoplay plays Yharnam with no button pressed, for 7200 frames (2 minutes at 60 fps), then calls `quit()`. It is off by default.
- Switch on: `yharnam_autoplay=1` in `bm/config.txt` (read with `cart_config`, no kernel change). From the bridge: `tools/pi.py --dev pizero config yharnam_autoplay=1` (or `--dev rgb30`). Switch off: `config yharnam_autoplay=0`. On the host: `YHARNAM_AUTOPLAY=1 build/host/bmplay build/carts/yharnam.bm`; `make test-yharnam-autoplay`.
- Code: the module `autoplay` of `main.lua`. It makes a mask of buttons each frame and replaces `btn`, `btnp` and `pad`, so the game logic and the drawing run as for a person. It is the bot of `tools/bmplay/yharnam_bot.lua` with a route. Its dice have a fixed seed, so each run is the same.
- Route: Central Yharnam. It lights the lamp of chunk (0,2) (1 lamp in the host run), walks the north streets, goes for the lamp of chunk (2,1), then crosses the squares, the cemetery and the park, and fights what it meets. If the hunter dies, it respawns and goes on. If the hunt is lost, it starts a new hunt and the route begins again. The frame cap ends the run (`AUTO_FRAMES`); the end writes one `autoplay:` line (frames, deaths, lamps, districts) with `log`.
- Host result: 7200 frames, 0 deaths, 8 districts (Ash Way, Black Court, Bone Square, Fog Yard, Gloam Hill, Mourn Street, Saint Agnes Yard, The Wither Pyre), the same in two runs. Not run on a device. A slow console plays the same frames in more time.

## Rules (do not break)

- `MAP.grid` has one area for each cell. A forest cell never touches a cathedral cell along a side (a load-time check stops the game).
- The 8 hunter's lamps are at least 2 chunks apart (not even diagonal). Each lamp chunk and each boss chunk is inside one area.
- The kinds in `art/places.py` are append only: a tile keeps its number, and the drawn map keeps its numbers.
- A change to the plan (areas, streets, borders, lamps): run `make yharnam-map`, then `make test-yharnam` (it checks the plan and the drawn map), then rebuild the `.bm` and the `.bme`. Without a new map the game shows the old things.
- The `.bme` is made from the `.bm`: `python3 scripts/mkbm.py --from build/carts/yharnam.bm -o carts/yharnam/yharnam.bme`. Check it with `mkmap.py --from carts/yharnam/yharnam.bme --out DIR`: the three CSV files must be the same.
- Run `mkassets.py` without `YH_DRAFT`; it rewrites the atlas in `main.lua` and keeps the `--@module` lines.
- Read the keys only with `btn()` and `btnp()`. Do not call `keyp()`. Harm: `keyp()` turns on typing, and then the keyboard is not a gamepad (Space does not start the hunt).
