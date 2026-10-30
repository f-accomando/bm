# Hero Stress cart

A separate cart, `carts/hero/`: 64 heroes walk in a plaza. The player changes the hero count, the polygon level and the 3D options in real time, and reads the cost. Design: [hero-stress-cart-and-cel-shader](../design/hero-stress-cart-and-cel-shader.md). Area: [graphics](graphics.md).

## How it is today (stages 1 and 2)

- Build: `make build/carts/hero.bm`. The cart is not in `GAMES`.
- Sources: `carts/hero/src/*.lua`, joined by `build/build.py`. Assets are committed in `carts/hero/assets/`. `build/mkheroes.py` makes `models.bm`. `build/mkplaza.py` makes `sheet.png` and the three map layers.
- Hero: the bm-forge `charkit` preset `rango` with the bm-forge walk clip (984 triangles, flat colours). Levels `hero0` to `hero3`: 984, 492, 246, 97 triangles (quadric reducer). Hulls `hero0_o` to `hero3_o` for the outline row.
- Every face of every hero model has its three corners on one bone (`one_bone_faces` in `mkheroes.py` splits the vertices at the joints). The vertex shader of the console takes a plain lit face only on one bone. With the first models (216 faces of `hero0` on two bones) every hero went back to the ARM, one triangle at a time, whatever Vertex shader said. The split opens a thin gap at a joint in some poses, as the rigid parts of the bench hero do.
- Panel: the rows show what the console really gave. Vertex shader shows `asked -> applied (probe N)` when the boot test saw less than was asked (`gpu3d()` eighth value). With Backend ARM, the rows AA, Vertex shader, Queue and Frame later show `ignored (ARM)`. The footer shows `GPU driver bm3d X.Y` and the probe level. On screens 330 lines or taller, two lines of help for the row under the cursor (table `H.HELP` in `30_options.lua`). The LOD row shows the triangles of one hero in the model it picks (`LOD 2  492 tris/hero`; auto shows the range). The numbers are in `src/05_tris.lua`. `mkheroes.py` writes that file, and `mkheroes.py --check` fails when it differs from `models.bm`. The stats box has a `hero tris` line (the drawn heroes times the triangles of their level).
- Resolution row: 320x180 to 1920x1080 (`screen()`). The label shows the mode really in force after `->`. 640x360 is the default.
- Plaza: a 32 x 32 cell `map3d` (ring wall, fountain, 8 pillars). About 3000 triangles.
- Rows 1-16, 18, 19 and 20 are live. Row 17 (alpha) is greyed. Row 12 is `dither3d`. Row 20 is `cel3d` (off, 2, 3, 4, 5, 6, 8 bands, floor 0.35); the heroes get draw3d flag 256, the plaza does not.
- Keys: up and down choose a row. Left and right change it. A toggles it. L1 and R1 change the hero count by 1, L2 and R2 by 5 (keyboard: `[` and `]`). X and Y change the level (`,` and `.`). Select (Tab) changes the camera. B hides the panel. Start + A runs the sweep (F5). Start alone saves `HERO.TXT` in `/docs` and sends a report (F6). Pad serial keys: `u`/`o` L1/R1, `c`/`v` X/Y, `j` A.
- Once a second the cart prints one `hero 1 ...` line with the options in force, `ms`, `fps`, `gpu`, `wait`, `anim`, `tri`, `vtx`, `lua`, `kb`.
- The sweep runs 10 hero counts (0 to 64), 30 warm-up frames and 120 measured frames each, then prints the load at 60 and 30 fps.

## Scenes of the 3D Bench, 2D and the tests of the bridge (30 Oct 2026)

The cart is `hero` (`hero.bm`, `hero.bme`; it was `herocart`). The panel has 26 rows, 20 on the screen, the window follows the cursor.
- **Scene** (row 21): plaza (the heroes), spheres, small faces, draw calls, fill quads (`src/25_scenes.lua`, from `carts/bench3d`). The Heroes row is the load of the scene (steps of 15%). **Fill** (row 22): flat, smooth, tex, alpha (texels with holes), screen (screen-door), bilinear (`gpu3d_opt("filter")`), texswap (three textures in turn). The textures are painted into the free part of the sheet at the start. Not built: textured screen-door (`mesh()` has no such face), textured heroes and skins (the heroes have flat colours), sprite stacks (no stack zone in the sheet), baked light.
- **2D over 3D** (row 23) and **2D amount** (row 24): sprites (flips, holes), text, map layers, fade (`dark_end`), light (`light_end`), drawn after any scene.
- **GPU option** (row 25) and **GPU opt state** (row 26): one switch of `gpu3d_opt` (wc, tex16, etc1, fs2, sort, attr, filter, merge, light, 2d), on or off, with what is in force after `->`.
- **Tests by the bridge.** Set the keys with the bridge, then restart and play: `pi.py config hero_test=fill-tex.fill-alpha.2d-sprites hero_prof=arm.gpu`, `pi.py restart`, `pi.py play build/carts/hero.bm` (ids apart by dots, `all` for every test, profiles `arm` and `gpu`; both by default). The cart reads and clears the keys (`cart_config`), waits 60 frames, then runs a sweep of the load for each test on each profile (30 warm frames and 120 measured, up to 10 loads; a sweep stops when a load is over 70 ms). It prints `hero 1 point ID PROF n=.. ms=..`, `hero 1 result ID PROF load60=.. load30=..` and `hero 1 suite done`, and sends the report (kind `hero`) to the `reports` branch. The ids: `plaza spheres tiny draws fill-flat fill-smooth fill-tex fill-alpha fill-screen fill-bilinear fill-texswap 2d-sprites 2d-text 2d-map 2d-fade 2d-light`. On the PC: `BMHOST_CONFIG="hero_test=2d-text.plaza,hero_prof=arm" bmhost-bin hero.bm`.
- The Rango toes (charkit) now go on the three forward sectors of each foot: both feet are the same.

## Hero counts at 60 fps from the benchmark (22 Oct, Pi Zero W)

These numbers come from the 3D Bench `herotris` and `cel` tests, not from the cart. Detail and report names: [v3d](v3d.md). A set is one hero of 1536 triangles, its shadow and a map of 5000 triangles, at 640x360.

| profile | sets at 60 fps | hero triangles at 60 fps |
|---|---:|---:|
| ARM | 0.41 | 630 |
| GPU | 0.56 | 859 |
| GPU+VS | 3.85 | 5907 |
| GPU+VS+Q | 6.74 | 10351 |

For `cel`, with the vertex shader (n heroes of 1536 triangles, 50 heroes): lit 11.0 ms, cel 12.1 ms (+10%), outline 14.4 ms (+31%). On the ARM cel costs the same as lit (14.6 against 14.5 ms). The status line of the boot probe says `cel light yes`.

## Tests

- `make test-hero` also runs `build/host/test_hero_mesh` (V3D emulator): every hero model and hull goes through the mesh path with Vertex shader 2 (`glmeshes`, `gltris`), none with it off or at 1. On the models made before the one-bone rule, `test_hero_mesh OLD.bm --expect-fallback` shows the fallback.
- `make test-hero` (bmhost): model counts, one bone for each face, the Resolution row to 1920x1080, one help text of at most two lines for each row, line format, `tri` rises with `n` and falls with the level, the cel and dither rows change the printed options, the sweep prints 10 lines.
- `make test-cel`: the 64-entry table (bands, floor, band edges), a lit sphere shows exactly N grey levels, the light off gives the picture of before cel, `dither3d`.
- `make test-impostor`: also a cel draw on the GPU (vertex shader 1 and 2) gives the ARM's bands.
- `python tests/qemu_test.py -k hero` (QEMU, ARM raster): starts, prints its line, R1, X and a row change work, and Cel bands 2 shows in the printed options. The cart prints `hkeys f=<frame> row=<cursor row>` before each 1 s line. The test sends a key, waits for 11 more frames, and sends the key again if the printed state did not change. A serial key counts as held for 10 frames (`HOLD_FRAMES`), and QEMU runs the cart at 2 to 6 fps, so a time-based wait lost the second of two equal keys.

## How the user measures on the Pi

Copy `hero.bm` to the SD card (`bm/`). Run it at 640x360. To check the vertex shader: set Backend GPU and Vertex shader 2, and read the panel: the row must show `2` with no `->`, and the footer a probe of 2. Then run the sweep with Backend GPU and Vertex shader off, then 2, and compare `tri` at 60 fps and `gpu` (expected: `tri` and fps rise at 2). Start the sweep (Start + A), then Start to save. Copy `HERO.TXT` from `/docs`. Do the runs of the design, stage 1 table. For the cel cost: at 20 heroes and level 0, run the sweep with `Cel bands` off, then `4` bands, then with `Dither` off, and compare the `ms` columns of `HERO.TXT` (ARM backend, then GPU). Expected: cel near 0 on the ARM. Not verified on hardware.

## Open work

- Pi numbers (runs 1-6 of the design) are not collected.
- Stage 1b: `bench_save()` into `bm/bench/` (owner decision, the proposal is to wait).
- Heroes have flat colours. The Texture row switches the plaza only. Four tints and a textured hero are not done.
- The `options` line of the console (`tex16`, `fs2`) is not read by Lua. `report()` adds it. The panel shows the 3D driver version.
- Free camera: the arrow keys also move the row cursor.
- The Pi has not run the models with one bone for each face: not verified on hardware.
- Stage 5 (RGB30 panels): see the design. Stages 3 and 4 (cel in the V3D vertex programs, outline by the inverted hull, bm3D 7.5) are built and tested on the V3D emulator only. The 3D Bench of 22 Oct measured on the Pi: the probe says `cel light yes`, cel at `vs` 2 costs +10% over lit, the hull (flag 512) costs +31% over lit (under the +60% gate: the distance limit is not needed). Not verified on the Pi: the same costs in the cart itself (the sweep of `HERO.TXT`).
- Cel and dither costs are measured by the 3D Bench (22 Oct: dither off equals dither on within 0.1 ms). The cart sweep with `Cel bands` on and `Dither` off (runs 3 and 4 of the design) is still open.
- `dither3d(false)` adds a constant half step. It saves no fill time. The ARM fill saving of the design is not real; the row shows the look only.
- Row 19 `Outline` has three values: off, hull (baked), flag 512. The printed line has `out0` to `out2` (0 off, 1 baked, 2 flag).

## Rules (do not break)

- A cel draw (flag 256) with `cel3d` above 0 is lit in the `_cel` vertex programs on the V3D with `Vertex shader` 2 when the boot probe passed, else by the ARM; the Mali backend has no cel and its draws go to the ARM. A `cel` program change in `tools/qpuasm.py` goes with `probe_lit(…, 1)` and the `cel *` scenes of `test_gpu3d`.
- The outline (flag 512) is a hull mesh drawn through the ordinary mesh call. Keep it free of backend code: the Mali session depends on that.
- `cel3d` and `dither3d` are cart state. `r3d_init` resets them.
- The logic of the 64 heroes runs every frame, whatever the count.
- `stat(16)` to `stat(18)` are read only for display and the printed line. No quality governor.
- The shadows (row 16) are drawn in a first pass, before all the heroes, so a foot is never darkened: the animation runs twice for each hero while the row is on (`anim` shows both). With bm3D 7.7 the GPU shadow darkens as the ARM's.
- `_init` calls `frameskip(4)`. A frame over budget is not drawn: up to 4 `_update` run before the next `_draw`. `stat(15) - 1` counts the frames not drawn. The panel (`skip`) and the printed line (`skip=`) show them for each second.
- Keep the `hkeys f= row=` line. `tests/qemu_test.py` test_hero reads it. `tests/hero/check.py` ignores it.
