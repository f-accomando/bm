# Hero Stress cart

A separate cart, `carts/herocart/`: 64 heroes walk in a plaza. The player changes the hero count, the polygon level and the 3D options in real time, and reads the cost. Design: [hero-stress-cart-and-cel-shader](../design/hero-stress-cart-and-cel-shader.md). Area: [graphics](graphics.md).

## How it is today (stage 1)

- Build: `make build/carts/herocart.bm`. The cart is not in `GAMES`. It has no `.b16` edition.
- Sources: `carts/herocart/src/*.lua`, joined by `build/build.py`. Assets are committed in `carts/herocart/assets/`. `build/mkheroes.py` makes `models.bm`. `build/mkplaza.py` makes `sheet.png` and the three map layers.
- Hero: the bm-forge `charkit` preset `rango` with the bm-forge walk clip (984 triangles, flat colours). Levels `hero0` to `hero3`: 984, 492, 246, 97 triangles (quadric reducer). Hulls `hero0_o` to `hero3_o` for the outline row.
- Plaza: a 32 x 32 cell `map3d` (ring wall, fountain, 8 pillars). About 3000 triangles.
- Rows 1-11, 13-16, 18 and 19 are live. Rows 12 (dither), 17 (alpha) and 20 (cel bands) are greyed.
- Keys: up and down choose a row. Left and right change it. A toggles it. L1 and R1 change the hero count by 1, L2 and R2 by 5 (keyboard: `[` and `]`). X and Y change the level (`,` and `.`). Select (Tab) changes the camera. B hides the panel. Start + A runs the sweep (F5). Start alone saves `HEROCART.TXT` in `/docs` and sends a report (F6). Pad serial keys: `u`/`o` L1/R1, `c`/`v` X/Y, `j` A.
- Once a second the cart prints one `herocart 1 ...` line with the options in force, `ms`, `fps`, `gpu`, `wait`, `anim`, `tri`, `vtx`, `lua`, `kb`.
- The sweep runs 10 hero counts (0 to 64), 30 warm-up frames and 120 measured frames each, then prints the load at 60 and 30 fps.

## Tests

- `make test-herocart` (bmhost): model counts, line format, `tri` rises with `n` and falls with the level, the sweep prints 10 lines.
- `python tests/qemu_test.py -k herocart` (QEMU, ARM raster): starts, prints its line, R1, X and a row change work.

## How the user measures on the Pi

Copy `herocart.bm` to the SD card (`bm/`). Run it at 640x360. Start the sweep (Start + A), then Start to save. Copy `HEROCART.TXT` from `/docs`. Do the runs of the design, stage 1 table. Not verified on hardware.

## Open work

- Pi numbers (runs 1-6 of the design) are not collected.
- Stage 1b: `bench_save()` into `bm/bench/` (owner decision, the proposal is to wait).
- Heroes have flat colours. The Texture row switches the plaza only. Four tints and a textured hero are not done.
- The `options` line of the console (`tex16`, `fs2`) is not read by Lua. `report()` adds it. The panel shows the 3D driver version.
- Free camera: the arrow keys also move the row cursor.
- Stages 2 to 5 (cel on the ARM raster, cel on the V3D, outline, RGB30 edition): see the design.

## Rules (do not break)

- The logic of the 64 heroes runs every frame, whatever the count.
- `stat(16)` to `stat(18)` are read only for display and the printed line. No quality governor.
- The `.b16` edition, when it comes, is built from its own sources.
