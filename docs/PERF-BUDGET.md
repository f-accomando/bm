# Performance budget

What a game can draw in one frame of 60 fps (16.6 ms). All numbers come from benchmark reports of the Pi Zero W. No number comes from a game that runs live.

Scene and size: every test draws at 640x360 in RGB565. Every frame starts with `cls` of the page. The ARM runs at 1000 MHz. The V3D asks for 250 MHz and the reports measure 300 MHz. SDRAM 400 MHz. No throttling (`throttled 00000`). Kernel `dev`, driver bm3d 7.5 for the reports of 22 Oct 2026 in the afternoon, bm3d 7.3 for the report `091857`.

How to read the tables:
- "At 60 fps" is the load `n60` of the report: the load that a frame of 16.67 ms holds. The bench interpolates it between two steps.
- `-1` or "misses" means that the first step already costs more than 16.67 ms.
- "or more" means that the test ran out of steps before it reached 16.67 ms. The real limit is higher.
- Values marked "derived" are a sum or a division of report values. They are not a separate measurement.
- `not measured` means that no report has this number.
- RGB30: `not verified on hardware`. No report of the RGB30 has the current drivers. The only RGB30 numbers are an old report of the ARM rasteriser (see the end of this file).

## Game budget summary (Pi Zero W, 640x360, 16.6 ms)

One thing alone fills the frame at the load given below. A game adds the costs, so each thing gets a part of the 16.6 ms. Treat the numbers as the limit, not as the target.

| Thing | One frame holds | Cost of one (derived) |
|---|---|---|
| Map layer, opaque, full screen | 5 layers (3.08 ms each) | 3.08 ms |
| Sprite 16x16, ARM | 2574 | 6.5 us |
| Sprite 16x16, GPU in the 2D job | 3569 or more | 4.7 us or less |
| Sprite stack of 8 slices, ARM | 55 | 0.30 ms |
| Full-screen fade, ARM | none (20.3 ms alone) | 20.3 ms |
| Full-screen fade, QPU (`gpu3d_qpu=1`) | 14.3 lamps (one run) | 9.3 ms with 1 lamp |
| Full-screen light, ARM | none (30.7 ms alone) | 30.7 ms |
| Hero of 1536 triangles, vertex shader | 66 | 0.25 ms |
| Hero set with map, 2D and logic (see herotris), vertex shader + queue | 6.7 sets (49,956 - 50,024 tris) | 13.5 ms (60 fps) |
| Crowd of animated heroes (Hero Stress, 640x360), VS2 + Q2 + later | 50 heroes (52,992 tris) | 15.1 ms (59-60 fps) |
| Crowd of animated heroes (Hero Stress, 1080p), VS2 + Q2 + later | 24 heroes (27,408 tris) | 16.2 ms (60 fps) |
| Draw call (one cube), vertex shader | 1386 | 12 us |

A plan for a 2D game on the ARM (derived, not a measured scene): 2 opaque layers (6.2 ms) + 1 layer with holes (about 1.5 ms) + 400 sprites (2.6 ms) + 10 stacks (3.0 ms) = 13.3 ms. About 3.3 ms stay for Lua and the rest.

A plan for a 3D game: the 3D job on the GPU with the vertex shader, and the 2D after the job (see `herotris`). With the queue the ARM runs the logic while the GPU draws. Without the queue 3.85 sets fit in the frame. With the queue 6.74 sets fit. The queue is off by default today (`gpu3d_queue`).

Do not add a full-screen fade or a full-screen light to a 60 fps frame. Each costs more than the whole frame on the ARM. Use them in transitions at 30 fps. The fade reaches 24.7 lamps at 30 fps. The light reaches 20.3 lamps at 30 fps.

## Reports used

Pi Zero W, kernel `dev`. Each file name is `20261022-<time>_<kind>_pi-zero-w_dev.txt`. The tables below give `<kind>-<time>`. The reports are on the branch `reports`, folder `reports/unknown/`.

| Id in the tables | Report | What it holds |
|---|---|---|
| `b3d-180848` | `20261022-180848_bench3d_...` | 2D scene `map8` |
| `b3d-180857` | `20261022-180857_bench3d_...` | 2D scene `sprites` |
| `b3d-180905` | `20261022-180905_bench3d_...` | 2D scene `stack` |
| `b3d-180913` | `20261022-180913_bench3d_...` | 2D scene `fade` |
| `b3d-191158` | `20261022-191158_bench3d_pi-zero-w_dev.txt` | `stress`, profile GPU+VS+Q (one run) |
| `b3d-184933` | `20261022-184933_bench3d_pi-zero-w_dev.txt` | 2D scene `fade`, profiles ARM and QPU (one run) |
| `b3d-180925` | `20261022-180925_bench3d_...` | 2D scene `light` |
| `b3d-180940` | `20261022-180940_bench3d_...` | `gpu2d`, all four profiles |
| `b3d-172217` | `20261022-172217_bench3d_...` | all 24 tests, profiles GPU and Q (bm3d 7.5) |
| `b3d-171354` to `b3d-171450` | `20261022-1713xx` and `-1714xx` | `herotris`, one profile in each report |
| `b3d-171533`, `b3d-171542` | `20261022-171533`, `-171542` | `cel`, profiles ARM and GPU+VS |
| `b3d-091857` | `20261022-091857_bench3d_...` | all tests, all profiles (bm3d 7.3) |
| `stack-091216` | `20261022-091216_stack_...` | Stack bench (tool removed on 24 Oct 2026) |
| `gpu-091318` | `20261022-091318_gpu_...` | Dev > GPU test (tool removed on 24 Oct 2026) |

Profiles: `ARM` is the ARM rasteriser (bm3d 0.2). `GPU` is bm3d 2.1, the V3D draws the triangles that the ARM prepares. `GPU+VS` adds the vertex shader (the V3D places the corners). `GPU+VS+FS2` is the driver as games get it (vertex shader and textured shaders with two threads). `+Q` is the frame queue. `GPU+2D` draws the 2D in the GPU job. `A8` is compact attributes.

## 2D

All 2D rows are drawn by the ARM, unless the row says GPU. The scenes are fixed: the positions come from a hash of the item number and the frame number. See `docs/BENCH3D.md`, section "The 2D scenes".

| Thing | Pi Zero W at 60 fps | Report | Test id | RGB30 |
|---|---|---|---|---|
| Tiles, one layer (8x8 tiles, 81x46 cells on screen), opaque | 13.37 ns a pixel = 3.08 ms for the whole 640x360 layer (3726 tiles, 0.83 us a tile; derived) | `b3d-180848`, line `P` | `map8`, ARM | not verified on hardware |
| Map layers: 3 opaque layers + layers with half the cells and holes (alpha) | 7.7 layers (about 20000 tiles; derived). The last step under 16.7 ms is 6 layers: 13.7 ms, worst 14.7 ms | `b3d-180848`, line `R` | `map8`, ARM | not verified on hardware |
| Sprites 16x16 with flip, half opaque and half with alpha, ARM | 2574 sprites. Cost 21.88 ns a pixel | `b3d-180857` | `sprites`, ARM | not verified on hardware |
| Sprites 16x16, GPU in the 2D job | 3569 sprites or more (13.6 ms; the scene stops at 4000). Frame cost 13.23 ns a pixel, of which the GPU job 7.32 ns a pixel | `b3d-180857` | `sprites`, GPU+2D | not verified on hardware |
| Flips and alpha, each alone | not measured (the scene mixes them) | - | - | not verified on hardware |
| Sprite stacks of 8 slices (16x16, three sizes, turning) | 54.6 stacks (58.93 ns a pixel) | `b3d-180905` | `stack`, ARM | not verified on hardware |
| Sprite stacks, scale 1, 30 frames a case | 64 stacks of 8 slices: 9.69 ms (6608 stacks a second). 128 stacks of 8 slices: 18.27 ms. 64 stacks of 16 slices: 18.27 ms. About 116 stacks of 8 slices fit 16.6 ms (derived) | `stack-091216` | Stack bench (removed; the 3D Bench scene `stack` measures the same) | not verified on hardware |
| Full-screen fade (`g16_fade_glow` lamps and `g16_fade_apply`) | misses: 20.3 ms with 1 lamp (worst 20.5 ms). At 30 fps: 24.7 lamps | `b3d-180913` | `fade`, ARM | not verified on hardware |
| Full-screen fade on the QPU (`dark_end` hook, `gpu3d_qpu=1`) | 9.3 ms with 1 lamp (ARM 20.4 ms in the same run, 2.2 times faster). 14.3 lamps at 60 fps (ARM misses) | `b3d-184933` | `fade`, QPU; one run, all 98 frames on the QPU | not verified on hardware beyond this run |
| Full-screen light (`g16_light_add` lamps and `g16_light_apply`) | misses: 30.7 ms with 1 lamp. At 30 fps: 20.3 lamps | `b3d-180925` | `light`, ARM | not verified on hardware |
| Double the load of 60 fps (`herotris`-like scene, quarter sets), GPU+VS+Q | the V3D holds 2x the 60 fps load without a failure, at about 40 fps (25.2 ms, worst 25.3 ms), ARM-bound (ARM 25.2 ms, GPU 16.1 ms). 1x: 13.3 ms, 0 drops. `n60` = 24.8 quarter sets. Pool 142 KB, no overflow, `v3d_failed` 0 | `b3d-191158` | `stress`, GPU+VS+Q; one run | not verified on hardware |
| 2D over 3D: sprites and 3 lines of text over spheres | ARM 1592 sprites. GPU 1886. GPU+2D 2013. GPU+VS+FS2 1932 | `b3d-180940` | `gpu2d` | not verified on hardware |
| 2D particles | not measured (a particle is a sprite: use the sprite rows) | - | - | not verified on hardware |

## 3D

The 3D rows count triangles that the game gives to the 3D (`submitted`). The GPU throws back faces away later, so the drawn triangles are fewer on the `GPU` and `ARM` profiles. With the vertex shader the report counts all of them as drawn.

| Thing | Pi Zero W at 60 fps | Report | Test id | RGB30 |
|---|---|---|---|---|
| Hero of 1536 triangles, 16 bones, Gouraud | ARM 3.9 heroes (5982 triangles). GPU 6.3 (9634). GPU+VS 66.3 (101885). GPU+VS+FS2 66.2 (101694). GPU+VS+A8 72.2 (110946) | `b3d-091857` | `heroes` | not verified on hardware |
| Hero with texture | ARM 3.4. GPU 6.5. GPU+VS+FS2 73.9 (113534 triangles) | `b3d-091857` | `heroes_tex` | not verified on hardware |
| Hero with texture and joints on two bones | ARM 3.3. GPU 6.5. GPU+VS+FS2 42.9 (65923 triangles) | `b3d-091857` | `heroes_skin` | not verified on hardware |
| Hero with shadow on a floor | ARM misses (24.7 ms with 1 hero). GPU 3.3. GPU+VS+FS2 36.2 (111774 triangles, shadows included) | `b3d-091857` | `heroes_shadow` | not verified on hardware |
| Hero sets (a set = 1 hero with shadow + a map of 100 pieces: 8036 triangles) with 2D and 4M instructions of logic after the 3D | ARM misses (0.41 sets, 40.7 ms at 1 set). GPU misses (0.56 sets, 29.8 ms at 1 set). GPU+VS 3.85 sets (31045 triangles). GPU+VS+A8 4.03 (32490). GPU+VS+Q 6.74 (54396). GPU+VS+Q2 6.74 (54406). GPU+VS+A8+Q 6.75 (54464). GPU+VS+FS2: not measured (the report has no row) | `b3d-171354` (ARM), `-171401` (GPU), `-171410` (VS), `-171419` (VS+Q), `-171435` (A8), `-171442` (A8+Q), `-171450` (Q2) | `herotris`, line `H` | not verified on hardware |
| Spheres of 96 faces, flat | ARM 70.8 (6801 triangles). GPU 203.2 (19503). GPU+VS+FS2 782.9 (75159) | `b3d-091857` | `spheres` | not verified on hardware |
| Game-like scene (road slices with map, hero, shadow, fence, glass, lamps, fog, HUD) | ARM 1.2 slices (5200 triangles, 2304 drawn). GPU 2.7 (9951, 4489 drawn). GPU+VS+FS2 24.5 (78188). Same scene with bm3d 7.5, GPU: 2.7 (9938, 4483 drawn) | `b3d-091857`, `b3d-172217` | `mix` | not verified on hardware |
| Draw calls (one cube of 12 triangles for each call) | ARM 467 calls. GPU 1228. GPU+VS+FS2 1386. Same test with bm3d 7.5, GPU: 1200 | `b3d-091857`, `b3d-172217` | `draws` | not verified on hardware |
| Small faces (about 4 pixels each) | ARM 7294 triangles. GPU 9895. GPU+VS+FS2 97373 | `b3d-091857` | `tiny` | not verified on hardware |
| Big meshes (models of 10080 triangles on a map of 14112) | ARM and GPU miss (82.2 ms and 59.1 ms with 1 model). GPU+VS+FS2 9.2 models (106846 triangles). With 4M instructions of logic: GPU+VS+FS2 4.0 (54743). GPU+VS+Q 9.3 (107673) | `b3d-091857` | `big`, `big_logic` | not verified on hardware |
| Queue: spheres and 4M instructions of logic after the 3D | GPU+VS 557.8 spheres. GPU+VS+Q 631.1 (13 percent more) | `b3d-091857` | `queue` | not verified on hardware |
| 3D fill, quads of 320x180 (a quarter of the screen), flat | ARM 11.5 quads. GPU 174.5. GPU+VS+FS2 163.7 (41 screens; derived) | `b3d-091857` | `quad_flat` | not verified on hardware |
| 3D fill, quads of 320x180, textured | ARM 3.4 quads. GPU 110.9. GPU+VS+FS2 131.1 | `b3d-091857` | `quad_tex` | not verified on hardware |
| Cel shading (4 bands), heroes of 1536 triangles | Cost against the sun light: x1.00 on the ARM (3.3 heroes against 3.4), x0.99 to x1.02 with GPU+VS. GPU+VS: 50 heroes or more (12.1 ms at 50; lit 11.0 ms) | `b3d-171533` (ARM), `b3d-171542` (GPU+VS) | `cel`, `cel_lit` | not verified on hardware |
| Outline (inverted hull) | Cost against the lit hero: x1.34 to x1.50 on the ARM (2.5 heroes). x1.05 to x1.23 with GPU+VS (42.4 heroes, 14.4 ms) | `b3d-171533`, `b3d-171542`, column `outline_over_lit` | `cel_outline` | not verified on hardware |
| Cel and outline with GPU+VS+FS2 | not measured (the report has no row) | `b3d-171550` | `cel` | not verified on hardware |
| Raw V3D speed | 2966 thousand small triangles a second. 811 Mpixel a second for full screens (render only) | `gpu-091318`, steps 6 and 7 | GPU test (removed) | not verified on hardware |
| 3D particles | not measured (the scene is not developed: `docs/BENCH3D.md`) | - | - | not verified on hardware |

### Pi Zero W, 25 Oct (kernel `1199bd2`, [report](bench/pi-2026-10-25/summary.md))

- `herotris` after the cut of `draw_flags`: ARM 2373 triangles at 41.0 ms; GPU+VS+Q 50915 triangles at 13.1 ms.
- Lua 3D Bench (`carts/bench3d`, default profiles ARM and GPU+VS+Q): 37 s of benchmark time, about 52 s wall-clock, about 2120 frames. First `R` lines (sets at 60 fps, drawn60): spheres ARM 58.1, GPU+VS+Q 558.5; heroes ARM 3.9, GPU+VS+Q 60.1; herotris ARM 7.0 sets at 44.6 ms, GPU+VS+Q 13.3. The numbers of the Lua edition are not comparable with the C bench.

### Pi Zero W, 24 Oct (reports `c51c239a`, `dfedfea0`, `62dc910a`)

- `herotris`, drawn60 triangles a frame: ARM 2385 (40.8 ms), GPU 3214 (30.3 ms), GPU+VS 29398 (15.4 ms), GPU+VS+Q 49956 (13.7 ms), GPU+VS+Q2 50024 (13.5 ms), GPU+VS+FS2 28830, GPU+VS+A8 28875, GPU+VS+A8+Q 49989. VS plus the queue holds 50k triangles at 60 fps.
- `htmap`: `ht_p10m` equals `ht_p10` (40360 triangles, 16.2 ms against 16.4 ms). Draw merging gives +33% for `ht_p100m` (4.0 against 3.0 n60). With the queue `ht_p100m` draws 60242 and `ht_p10m` 91475 triangles at 60 fps.
- `bmweb`: 723 frames at 59.6 fps, no late frame, update+draw 3.22 ms mean and 4.29 ms worst.

## RGB30: the old ARM-rasteriser report

The RGB30 has no report for the current drivers. The Mali driver and the 2D scenes have no numbers: `not verified on hardware`.

One old report exists on the branch `reports`: `reports/v0.2.5/20261010-111937_bench3d_rgb30_v0.2.5.txt`. It was made with kernel v0.2.5, driver bm3d 6.8, the 3D on the ARM (Cortex-A55), no Mali. The report does not state the render size. These numbers are the load at 60 fps of the ARM rasteriser only. Do not use them for the Mali or for today's kernel.

| Test id | RGB30, ARM rasteriser, at 60 fps | Pi Zero W, ARM, same test (`b3d-091857`) |
|---|---|---|
| `heroes` | 8.5 heroes (13121 triangles) | 3.9 |
| `heroes_tex` | 6.6 | 3.4 |
| `heroes_skin` | 6.5 | 3.3 |
| `heroes_shadow` | 2.2 | misses |
| `spheres` | 177.3 | 70.8 |
| `draws` | 886.4 calls | 467.1 |
| `tiny` | 34.2 | 14.2 |
| `mix` | 3.5 slices (12405 triangles) | 1.2 |
| `big` | misses (37.7 ms with 1 model) | misses |
| `gpu2d` | 3323.9 sprites | 1607.4 |

## Not measured

- RGB30 with the Mali driver: all rows (`not verified on hardware`).
- RGB30 2D scenes (`map8`, `sprites`, `stack`, `fade`, `light`, `gpu2d`).
- Flips and alpha, each alone.
- 2D particles and 3D particles.
- `GPU+VS+FS2` in `herotris` and in `cel`: the reports have no row.
- The cost of `profile(true)` itself.

To measure them, run the 3D Bench on the device (`docs/BENCH3D.md`) and send the reports.

See also: `docs/BENCH3D.md` (the tests and the report columns), `docs/PERFORMANCE.md` (decisions and history), `docs/design/triangle-throughput.md` (the triangle count work), `docs/GAME-GUIDE.md` (the profiler).
