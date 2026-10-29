# Mali frames: the queue, the frame pipeline and the cls (RGB30)

Split from [mali-parity](mali-parity.md). The driver state and the open work are in [graphics-mali](graphics-mali.md).

## The queue on heroes (29 Oct): no gain, and why

The queue profiles `GPU+VS+Q` and `GPU+VS+Q2` ran on `heroes` and `heroes_tex` (RGB30, kernel of 29 Oct). The 3D Bench did not keep them for these scenes.
`heroes`: the jobs time in the flush fell from 7.5 ms to 3.2 ms and the flush from 10.2 ms to 4.8 ms. The frame stayed at 14.1 ms (n60 15.2 in both). `heroes_tex`: n60 8.1 in both. The pixel check was the same.
The cause: the queue overlaps the GPU with the work of the same frame only. `heroes` has no work after the submit, so `mali_backend_sync` at the present waits for the GPU (about 4.3 ms). The mesh of the next frame (3.4 ms) and the draw build (1.5 ms) start after that wait.
A worker core does not change this: the wait only moves to another core. The gain needs a pipeline across frames. The ARM does the mesh and the build of frame N+1 while the GPU draws frame N. A worker core does the wait, the resolve (1.2 ms) and the present of frame N. In `heroes` the ARM needs about 6 ms and the GPU 7.5 ms, so a frame could take about 9 ms instead of 14 (n60 about 23).
Built in bm3d 7.29 (see *The frame pipeline*). The pipeline needs: two pages (the resolve of frame N writes one page while the game draws into the other), a present one frame late, and a `sync` before any 2D draw on the page of the frame in flight. It adds one frame of latency.

## The frame pipeline (bm3d 7.29, 29 Oct)

The runtime of the games has each frame shown a frame later since bm3d 6.9 (`gpu3d_later=1`, `gpu3d_queue=2`). On the Mali it was off, because `gpu3d_queue2()` was 0. Now it is on when the queue is 2.
Three changes: the frame in flight keeps a copy of the page descriptor; `gpu3d_sync_page(px)` waits only for the frame of that page; the 3D Bench profile `GPU+VS+Q2` leaves the frame in flight until the next submit.
RGB30, `heroes`, 13 heroes: 14.1 ms a frame, 8.5 ms with the pipeline (mesh 3.4 + build 1.5 + vertex job 3.2). `heroes_tex`: 15.9 ms to 12.6 ms. K and KC are the same as `GPU+VS`.
n60 grows less (heroes 15.2 -> 16.5, heroes_tex 8.1 -> 8.4). A frame holds 10922 faces, so a heavier load splits in two jobs and the second job waits for the first. A second arena would remove this wait. It costs about 4 MB of the GPU memory.
`gpu3d_later=1` with `gpu3d_queue=2` (bm3d 7.31, 29 Oct): `herocart` flickered. A log showed `the frame held was not the next page to show` at every frame. `later_hold` stepped the pages by height x pitch (0x70800 bytes); the pages of the RGB30 are 0x80000 bytes apart (`size / buffers`). The next frame was drawn over pages in use. Fixed. A cls request now belongs to its frame (`be_pend_u`), so the resolve of an older frame does not take the clear of the next page.
`herocart`, 50 heroes: no flicker, 53 ms a frame (15 fps).
Sweep of `herocart` on the RGB30 (29 Oct, 0 to 64 heroes, level 0, the cart's own sweep: median ms of 120 frames). Queue 0 against queue 2 with `gpu3d_later`: 0 heroes 12.5 and 12.6, 8 heroes 19.3 and 19.2, 16 heroes 26.2 and 26.1, 28 heroes 35.2 and 35.3, 48 heroes 50.9 and 51.6, 64 heroes 63.3 and 64.0. The mode gives no gain in this cart, with one job a frame (up to 8 heroes) or with more. A hero costs 0.8 ms a frame and the GPU only 0.44 ms of it; the map alone costs 12.5 ms with 1.9 ms on the GPU. The frame is limited by the ARM (Lua, the draw build), so a frame later hides little. Keep `gpu3d_later` off by default. The sweep with queue 2 and no `gpu3d_later` did not print (the cart's check of the three passes lost it): not measured.
Open: a worker core for the wait and the resolve (1.2 ms).

## A cls before the 3D (bm3d 7.30, 29 Oct)

`herocart` with the GPU showed the sky colour and the heroes of the last job only. A log of the backend (a debug build, not kept) showed 3 jobs a frame: the map and 10600 triangles (201062 pixels covered), then two jobs of heroes (about 5900 pixels each).
The first jobs are resolved at once and the last one is kicked. Then `cls_settle` filled the page with the cls colour, because `gpu3d_cleared()` was 0 on the Mali. The fill covered the map and the first heroes.
Fix: `gpu3d_page(1, c)` marks the page, the first resolve after it fills the page with `c`, and `gpu3d_cleared()` says so. A frame that fits one job did not show the fault: the fill came before the resolve.

## Where the ARM spends a frame (29 Oct)

`herocart` on the RGB30, 0 heroes (the plaza of 3036 triangles), a probe cart that times each phase with `stat(8)`, 120 frames each (ms a frame):
- The whole frame: 13.0 with the panel, 9.0 without it, 0.9 without the map.
- `map3d_draw`: 5.9 (the largest part). The plaza is textured, and `be_mesh_impl` in `mali.c` leaves a textured face (`R3D_TEXTURED`) to the ARM: r3d transforms, culls, lights and projects it one triangle at a time, 1.9 us a triangle. The GPU vertex shader takes only the flat meshes (the heroes: 0.17 us a triangle).
- The panel (`panel_draw`, 2D text in Lua): 2.8.
- The end of the frame (build of the draws, the job, the resolve): 2 to 4 (`flush_us` 5.1 with the GPU job and the wait, `resolve_us` 2.2).
- `_update` (the logic of 64 heroes): 0.6.
With the panel off the run had no GPU job at all (`jobs` 0, `ms` 9.0 with the ARM raster): not explained, check it before the next probe.
Next development: a textured variant of the cached mesh path (UV in the vertex, `BM_VS_TEX` with a matrix), so the plaza costs the ARM what a hero does. Expected: `d_map` from 5.9 ms to under 1 ms, and a 3D game with a textured map gains the same.

## Textured meshes in the vertex shader (bm3d 7.32, 29 Oct)

Built. `be_mesh_impl` (`src/rgb30/mali.c`) takes a rigid mesh when every shown face has `R3D_TEXTURED`, the mesh has `uv` and one sheet with a slot (`be_tex_find`), and the light is the sun and the sky. It goes through the face cache as kind 3 (12 floats a vertex: position, normal and emissive flag, `u v` scaled by the sheet). `build_draws` draws it with the shader `BM_VS_TEXM` and `BM_FS_TEX`; the draw has `tex_pa` and `litx` 0.
- The light: r3d gives a textured face k = the grey of the light (`lit_grey`), with no fog. `BM_VS_TEXM` computes the same value from the face normal (`R3D_SMOOTH`: from the vertex normal, as r3d does; a face with `R3D_FLAT` keeps the face normal). An unlit mesh and an emissive face have k = 1.
- The ARM keeps the mesh with cel, lamps, rim, a skeleton, `R3D_FRONT`, a face without a texture or with `R3D_SCREEN`, fewer than 16 faces, or small faces (`mali_lit_min`), and with `mali_res=0`.
- Result (RGB30, 29 Oct): `herocart`, 0 heroes, the plaza (3036 triangles): `map3d_draw` 5.8 ms -> 0.6 ms, frame 12.6 -> 6.1 ms (panel on, GPU 2.0 ms). The screen is the same pixel for pixel as with `mali_res=0` (only the stats text and 3 pixels differ). 3D Bench `spheres_tex` now runs `GPU+VS`: n60 254 (GPU 122, ARM 52), K and KC equal to `GPU`. Full suite: games 633, GPU 743, ARM 601, no KC at 0.
- `herocart` with the panel off had no GPU job (frame 8.8 ms, `gpu` 0.0) before this change too. Not explained: check it before the next probe (the cart is not changed).
- The near plane: a mesh with a vertex behind the near plane goes to the ARM unless the camera is inside its box (`env->inside`: the GPU clips it, as for a flat mesh). No scene has shown a fault.
