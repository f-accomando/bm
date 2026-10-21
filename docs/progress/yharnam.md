# Yharnam

The game: `carts/yharnam/`. Its graphics come from `build/mkassets.py`; after a change, run it
again and commit `assets/sheet.png`. Never commit a sheet made with `YH_DRAFT=1`.

---

## 1. Architecture and Structure

- **Resolution & Screen**: 360×360 (gothic 3/4 isometric view, perfect for the RGB30's 1:1 panel and the `.b16` format).
- **World & Streaming**: Continuous map generated in chunks of 16×16 cells during movement (`visible_chunks`, `stream()`).
- **Lighting**: Per-pixel light levels with fade tables (`fades`, `dark_begin`, `glow`, `dark_end`) inspired by Dank Tomb, gas lanterns, braziers and pyres.
- **Graphics Pipeline**: Procedural generation of over 5,000 frames via Signed Distance Fields (`build/mkassets.py`, `build/art/sdf.py`, `art/anims.py`, `art/hunter.py`), Hunter animations on 8-direction cubic Hermite curves, with automatic weapon collision resolution (`unclip`).
- **Layered Map**: `map_ground.csv`, `map_overlay.csv` and `map_objects.csv` (256×256 cells), drawn in the SDK and converted with `mkmap.py`.

---

## 2. Open Checks and Verifications

### A. Performance on Pi Zero W and RGB30
- Measure the frame cost at 60 fps (background chunk generation, `stream()`, tiered lighting, particles).
- Collect the numbers with the dev kit session report and the `devinfo()` lines at the end of a run.
- Compare the parameters with the 60 fps budget of the `.b16` profile.

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
- **To be completed**:
  - Constrain each house within its own 16×16 block.
  - The boundaries of named areas still live in `MAP.areas` in the code: consider moving them to map file metadata.
