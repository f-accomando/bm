# Hero Stress cart

A separate cart, `carts/herocart/`: 64 heroes walk in a plaza. The player changes the hero count, the polygon level and the 3D options in real time, and reads the cost. Design: [hero-stress-cart-and-cel-shader](../design/hero-stress-cart-and-cel-shader.md). Area: [graphics](graphics.md).

## How it is today (stages 1 and 2)

- Build: `make build/carts/herocart.bm`. The cart is not in `GAMES`. It has no `.b16` edition.
- Sources: `carts/herocart/src/*.lua`, joined by `build/build.py`. Assets are committed in `carts/herocart/assets/`. `build/mkheroes.py` makes `models.bm`. `build/mkplaza.py` makes `sheet.png` and the three map layers.
- Hero: the bm-forge `charkit` preset `rango` with the bm-forge walk clip (984 triangles, flat colours). Levels `hero0` to `hero3`: 984, 492, 246, 97 triangles (quadric reducer). Hulls `hero0_o` to `hero3_o` for the outline row.
- Plaza: a 32 x 32 cell `map3d` (ring wall, fountain, 8 pillars). About 3000 triangles.
- Rows 1-16, 18, 19 and 20 are live. Row 17 (alpha) is greyed. Row 12 is `dither3d`. Row 20 is `cel3d` (off, 2, 3, 4, 5, 6, 8 bands, floor 0.35); the heroes get draw3d flag 256, the plaza does not.
- Keys: up and down choose a row. Left and right change it. A toggles it. L1 and R1 change the hero count by 1, L2 and R2 by 5 (keyboard: `[` and `]`). X and Y change the level (`,` and `.`). Select (Tab) changes the camera. B hides the panel. Start + A runs the sweep (F5). Start alone saves `HEROCART.TXT` in `/docs` and sends a report (F6). Pad serial keys: `u`/`o` L1/R1, `c`/`v` X/Y, `j` A.
- Once a second the cart prints one `herocart 1 ...` line with the options in force, `ms`, `fps`, `gpu`, `wait`, `anim`, `tri`, `vtx`, `lua`, `kb`.
- The sweep runs 10 hero counts (0 to 64), 30 warm-up frames and 120 measured frames each, then prints the load at 60 and 30 fps.

## Tests

- `make test-herocart` (bmhost): model counts, line format, `tri` rises with `n` and falls with the level, the cel and dither rows change the printed options, the sweep prints 10 lines.
- `make test-cel`: the 64-entry table (bands, floor, band edges), a lit sphere shows exactly N grey levels, the light off gives the picture of before cel, `dither3d`.
- `make test-impostor`: also a cel draw on the GPU (vertex shader 1 and 2) gives the ARM's bands.
- `python tests/qemu_test.py -k herocart` (QEMU, ARM raster): starts, prints its line, R1, X and a row change work, and Cel bands 2 shows in the printed options.

## How the user measures on the Pi

Copy `herocart.bm` to the SD card (`bm/`). Run it at 640x360. Start the sweep (Start + A), then Start to save. Copy `HEROCART.TXT` from `/docs`. Do the runs of the design, stage 1 table. For the cel cost: at 20 heroes and level 0, run the sweep with `Cel bands` off, then `4` bands, then with `Dither` off, and compare the `ms` columns of `HEROCART.TXT` (ARM backend, then GPU). Expected: cel near 0 on the ARM. Not verified on hardware.

## Open work

- Pi numbers (runs 1-6 of the design) are not collected.
- Stage 1b: `bench_save()` into `bm/bench/` (owner decision, the proposal is to wait).
- Heroes have flat colours. The Texture row switches the plaza only. Four tints and a textured hero are not done.
- The `options` line of the console (`tex16`, `fs2`) is not read by Lua. `report()` adds it. The panel shows the 3D driver version.
- Free camera: the arrow keys also move the row cursor.
- Stages 3 to 5 (cel on the V3D, outline, RGB30 edition): see the design. On the V3D a cel draw is lit by the ARM today (the triangles go one by one), so the cost on the GPU rows is not the final one.
- Cel and dither costs on the Pi are not measured (runs 3 and 4 of the design with `Cel bands` on and `Dither` off).
- `dither3d(false)` adds a constant half step. It saves no fill time. The ARM fill saving of the design is not real; the row shows the look only.
- The outline flag 512 and the `cel3d` outline arguments are kept but draw nothing. Row 19 is still the baked hull (stage 4).

## Rules (do not break)

- A cel draw (flag 256) with `cel3d` above 0 is lit by the ARM on every backend. Light it on the ARM until the QPU vertex programs have the cel cut (stage 3).
- `cel3d` and `dither3d` are cart state. `r3d_init` resets them.
- The logic of the 64 heroes runs every frame, whatever the count.
- `stat(16)` to `stat(18)` are read only for display and the printed line. No quality governor.
- The `.b16` edition, when it comes, is built from its own sources.
