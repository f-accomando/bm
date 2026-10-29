# Mali vs Pi V3D parity (RGB30)

Split from [graphics-mali](graphics-mali.md). The driver state and the open work are there.

## Mali vs Pi V3D Architectural Equivalence & Feature Parity (API-5)

Mapping of Mali-G52 developments against reference Pi V3D driver:

| Pipeline Stage / Feature | Pi Zero W (V3D 2.1) | PowKiddy RGB30 (Mali-G52) | Parity Status & Notes |
|---|---|---|---|
| **Framebuffer / Clear** | FBD (Framebuffer Descriptor) / Tile setup in RGB565/RGBA8888 | SFBD / MFBD (Single/Multi FB Descriptor) in linear XRGB8888 | **Full Parity** (HW verified; 64x64 clear & screen presentation pass) |
| **Vertex Processing** | Coordinate / Vertex Shaders on QPU (`vs_colour`, `vs_lit_tex`) | Compute Job on slot 1 with Bifrost vertex shader (`BM_VS`, `BM_VS3`) | **Full Parity** (HW verified in `mali_backend.mesh` with `gpu3d_vshader=1/2`) |
| **Binning / Tiling** | Binner Job (`BPOA` / `BPOS`), tile allocator pool with overflow | Tiler Job on slot 1 (`SO_HEAP`, `SO_HEAPD`, `SO_TCTX`, mask 0x28) | **Full Parity** (HW verified; Primitive Size 1.0f fixed fault) |
| **Fragment / Raster** | QPU Fragment Shaders (`fs_colour`, `fs_tex_lit`, `fs_cel`) | Fragment Job on slot 0 (`BM_FS` with pushed color words 4-7) | **Flat Colour Parity** (HW verified; textured shader done: `BM_FS_TEX`) |
| **State Management** | NV Shader State Record / Blend configuration | `Renderer State` (`md_renderer_state_t`) + `Blend` per target | **Full Parity** (Descriptors from Mesa `v7.xml`) |
| **Depth & Sorting** | Hardware Early-Z / Late-Z buffer attachment and depth test | Depth test in the tile buffer (`mali_z=1`, less or equal, cleared to 1); sort only when a frame does not fit one pass | **Parity**: hardware depth test, no ZS writeback |
| **Textures & Lighting** | TMU texturing (nearest, bilinear), lit faces, ETC1, 16-bit sheets | Opaque textures on GPU (`mali_tex=1`); sun, ambient, rim, lamps, gloss, fog and cel levels in the vertex shader for rigid and skinned meshes (`mali_lit=1`), their faces kept in a cache (`mali_res=1`); cel and alpha and screen textures stay on ARM | **Partial Parity**: textures and lit meshes on GPU |
| **Cel Shading** | `fs_cel` on QPU with 64-entry quant table and inverted hull outline | Per-face cel quantization (`cel_step`) in `be_mesh` | **Partial Parity**: Flat-shaded cel quant on GPU; smooth per-pixel cel on ARM |
| **Job Queue & Async** | `queue=1`/`queue=2` (`JOB_BLOCK`/`JOB2_BLOCK`), ARM/GPU overlap | 2 memory sets (`MALI_SET_SIZE`) + async Slot 0 Fragment Job (`mali_backend_submit`) | **Full Parity** (HW verified in 3D Bench `GPU+VS+Q` / `GPU+VS+Q2`) |
| **Draw Merging** | `gpu3d_merge` (bm3d 7.6, world-space multi-mesh batching) | 4 MB single-pass arena (`1,024` draws) + screen-space merge (`>1,024` draws) | **Full Parity** (HW verified; merges up to `1,024` draws in 1 GPU pass) |
| **Config & Safety** | `gpu3d=1` default, automatic fallback to ARM | `mali3d=1` default (`bm3d 7.15`), clean ARM fallback on any fault | **Full Parity** (`gpu3d_init` verifies Mali and falls back to ARM) |

## The `gpu3d_*` options on the Mali (Q5, 28 Oct 2026)

Checked in `src/rgb30/bm_port.c`. The Pi flags are in [v3d-flags](v3d-flags.md).

| Option | On the Mali | Why |
|---|---|---|
| `gpu3d`, `gpu3d_vs`, `gpu3d_queue` | honoured (`mali3d=0` also turns it off) | `gpu3d_vs` reads 0 to 2; the Mali always draws meshes with level 2 |
| `gpu3d_merge` | honoured | the draw merge is in `r3d`, not in the driver |
| `gpu3d_sort` | replaced | the depth test (`mali_z`) does the job; the key is ignored |
| `gpu3d_wc`, `gpu3d_tex16`, `gpu3d_fs2`, `gpu3d_attr` | not needed | they answer V3D limits (write combining, texture size, QPU threads, vertex bytes); the Mali arena is already write-merged and its textures are RGB565 |
| `gpu3d_queue2`, `gpu3d_later`, `gpu3d_qpu` | not developed | `gpu3d_queue2()` returns 0; the Mali has no QPU programs |
| `gpu3d_filter` (bilinear) | honoured (bm3d 7.22) | the Sampler of each sheet filters; the 3D Bench `bilinear` test runs at 126.6 against the ARM's nearest quads |
| `gpu3d_aa` (MSAA) | `gpu3d_aa=1`, 4 samples | `mali_msaa=1` (default 0, bm3d 7.28): the tile buffer holds 4 samples, the ARM resolve blends the edges (see below) |
| `gpu3d_etc1` | not developed | the Mali reads ETC1 textures; the driver has no encoder call |
| `gpu3d_2d` | rectangles and sprites done (`mali_2d=1`, bm3d 7.23), off by default | the 3D Bench `gpu2d` test: 2573 sprites against 2999 with the ARM drawing them (-14%): the sprite fragment (texture, alpha kill) costs 3.2 ms for 2568 sprites. The ARM instructions fall 25%. `text2d` returns 0 |
| `gpu3d_light`, `gpu3d_enlarge` | not developed | `light2d` and `enlarge` return -1: the ARM does them |

## The 2D on the GPU of the RGB30 (28 Oct 2026)

How a game gets it. The cartridge does not change. Two system keys decide:
- `mali_2d=1` (hidden, default 0) lets the Mali take `rectfill`, `rect`, `pset`, straight `line`, `spr`, `sspr` and `map` cells.
- The runtime sends the 2D to the GPU only while a 3D job is open (3D waits in it), or from the first 2D call when `gpu2d_only=1` (hidden, default 0). `gpu3d_2d` (default 1) is the runtime switch; `gpu3d_opt("2d", on)` changes it in a run. `mali_2d` is read at the GPU start only.

What it costs and gives (3D Bench, RGB30, same kernel, 28 Oct):

| Test | ARM draws the 2D | Mali draws the 2D | Why |
|---|---|---|---|
| `gpu2d` (sprites of one sheet over 3D) | 2999 sprites at 60 fps | 2573 | 133 draws; the fragment of the sprites takes 3.2 ms |
| `sprites` (two sheets in turn) | 3569 (the test maximum) | 3569 (1702 before bm3d 7.25) | 3 draws: the sprites of up to 16 sheets in a row share a draw (a texture index a vertex); before, 1506 draws, each a vertex and a tiler job |

The ARM instructions of a frame fall (`gpu2d`: 13.2 M to 9.1 M). So the GPU 2D pays only when the ARM is the limit for other work. With 3569 sprites the Mali takes 14.0 ms of a frame (GPU 5.7 ms, the ARM's resolve of the covered pixels 4.8 ms, draw build 2.0 ms) against 12.6 ms for the ARM drawing them: where the ARM has free time it draws the 2D as fast or faster.

Limits: a sheet has to fit the 2 MB texture pool (a sheet of 4096 x 3400 does not: its sprites stay on the ARM); the order of the calls is the order of the picture, so the Mali does not reorder sprites: a draw takes the sprites in a row, from up to 16 sheets (a 17th sheet starts a draw); text, the light of `dark_end` and the enlarge stay on the ARM; a sheet with transparent texels keeps 5 bits of green.

The ARM resolve (surface to page, 4.8 ms for 3569 sprites): the GPU cannot write the page itself, because the game's page is not in the 64 MB window of the GPU. A NEON resolve was tried (28 Oct): it halves the resolve of the `sprites` test (4811 to 2448 us, same pixels) but slows the scenes that cover part of the screen (`spheres` -5%, `draws` -18%, `split` -16%; games 635 to 617, GPU 749 to 717), because the scalar loop skips empty blocks and predicts full ones. Not kept; the scalar resolve stays.

Yharnam: not applicable on the RGB30 today. Its sheet is over the pool, so only its rectangles would go to the GPU; on the Pi the GPU 2D of the map was slower than the ARM ([yharnam](yharnam.md)). The 3D work of the Mali (textures, depth, lit meshes, cel) does not touch a 2D game. What helps Yharnam on the Pi is the light by the GPU (`gpu3d_opt("light")`): the Mali has no light shader, so it stays on the ARM and the pause menu hides the entry.

## The meshes: the Pi and the Mali (28 Oct 2026)

| | Pi Zero W (V3D, `mesh_get` in `src/gpu/gpu3d.c`, bm3d 3.0) | RGB30 (Mali, face cache, bm3d 7.26) |
|---|---|---|
| What is kept | the corners of the mesh, in the GPU memory, indexed (a vertex once) | the vertices of every face (3 a face), in the kernel RAM |
| A frame sends | the matrices | the matrices, the light and the numbers of the visible faces; the ARM copies their blocks to the GPU memory |
| Back faces | the binner of the V3D throws them away | the ARM tests every face (`(s2 v0 + at) . n`) |
| Light | in the vertex shader | in the vertex shader (flat, per face) |
| Key | `gpu3d_vs` | `mali_res` (default 1) |

Why not the same: the Mali's flat light needs one normal and one colour a face, so the vertices cannot be shared between faces.

Culling on the GPU was tried (28 Oct): the cached blocks live in the GPU memory (a 3 MB area below the lit area), the whole mesh is one draw and the Draw descriptor culls back faces. With the front face clockwise on the screen (`front_ccw` 0) the pictures are the same as the ARM's (pixel check equal); `front_ccw` 1 culls every face. It is slower: `spheres` 246 against 257 with the ARM culling, `heroes` 12.6 against 13.5, `spheres_shine` 186 against 209. The vertex shader runs on every vertex, twice as many, and the ARM's test costs little. Not kept. A packed array for the culling test (32 bytes a face, in a row) gave nothing either (`heroes` 13.6 against 13.5, `spheres` 259 against 257): the ARM test is not the limit. `heroes` is bound by the GPU (7.9 ms of jobs for 19968 triangles, 13 heroes in about four frames: a frame holds 5461 faces by its vertex arrays). The idea left is indexed vertices with the light computed in a way that shares them (a normal from the positions), which is a change of the picture to measure first.

## 4x MSAA on the Mali (bm3d 7.28, `mali_msaa=1`)

The tile buffer keeps 4 samples a pixel (rotated grid) and the write-back averages them. The frame descriptor has sample count 4 and 4 KB of colour a tile. The Renderer States have the multisample bit.
The resolve model changes. The key colour does not work: the average of a covered sample and a key sample is a wrong colour. The surface is cleared to 0 and the alpha byte is the coverage. The colour is premultiplied. The ARM skips alpha 0, copies alpha 255 and blends the rest with the page: colour + page * (255 - alpha) / 255.
Measured on the RGB30 (28 Oct), GPU+VS, `mali_msaa=1` against 0: `spheres` 238 against 258 (-8%), `heroes` 14.1 against 14.8, `queue` 168 against 200. The GPU time does not change (7.5 ms of jobs). The cost is the ARM resolve: 2.7 ms against 1.5 ms. Scores: games 581 against 635, GPU 655 against 748.
The pixel check holds: `KC` is 203615 against 203348 ink pixels (the edge pixels add ink). The `K` line has 963 big differences with a maximum of 13: the smooth edges.
An 8-pixel block with all alpha 0 is skipped and a block with all alpha 254 or 255 is packed with no tests (NEON on the RGB30). The blend of an edge pixel has no division. A scalar version took 3.7 ms, this one 2.6 ms.
Tried and not kept: no cache invalidation (2.55 ms, so it is 0.1 ms), NEON for the full blocks alone (2.66 ms), and a test for alpha 254 (no change). The ARM still takes 1.1 ms more than without MSAA, and the cause is not found: the edge pixels are about 30000, so they do not explain it. The next idea is to count the blocks of each kind.

## Indexed vertices on the Mali: not built (28 Oct)

The face cache keeps 3 vertices a face. A mesh with shared vertices would keep about 0.6 vertices a face and the vertex shader would run less.
The gain has a limit. In `heroes` (GPU+VS, RGB30) the bin job takes 3.2 ms and the render job 4.3 ms. The vertex work is a part of the 3.2 ms, so at best 10% of the 14 ms frame.
The look would change. A shared vertex has one normal, so the light is smooth. The Mali meshes are lit flat, one colour a face (and `cel_flat` needs it). Flat light needs unshared vertices or a flat varying from a provoking vertex, and both give back the gain.
Build it only for a smooth-lit mesh (`spheres_smooth`) and measure it first with a test mesh. The cost: an index buffer in the Draw descriptor, the cache of the visible faces as indices, and a new pixel check.

## One batch of 32768 faces (bm3d 7.28, 28 Oct)

A batch ended at 8192 triangles and each batch was a frame of its own. A frame has no depth of the one before. In `heroes_tex` and `heroes_skin` (12288 faces a hero row, three rows) the far row came in the second batch and covered the near rows.
The batch holds 32768 triangles now (`BE_MAX_TRIS`). The sort runs on the whole batch, so the frames come in far-to-near order. A frame of flat triangles ends at 32768 vertex slots (the area at the start of the arena): a larger batch without this limit wrote over the next area and the GPU drew nothing in `quad_flat`, `draws` and `queue`.
Checked on the RGB30: scores 636 and 750 as before, the pixel check holds, and the far row of `heroes_tex` stays behind. The frame that holds more faces than the slots still needs the sort: a scene with more than 10922 lit faces and a gap in depth between frames is open.

## The rows of the heroes scenes (29 Oct)

At a high load the far rows of `heroes` covered the near ones. The pixel check cannot see it. The screenshot of the 3D Bench (`bm_shot_page`) shows it.
The cause: a batch held 512 matrices (`BE_MAX_MVPS`) and a hero takes 16, so after 32 heroes the batch ended and the next heroes (the far rows) came in a new frame, over the first one. Two other limits had the same effect: `BE_VIS_MAX` (the faces of the cached meshes: 12288, so only 8 heroes were cached and the others were plain triangles in a frame of their own) and the order of the cached meshes in a frame.
Now `BE_MAX_MVPS` is 2048, `BE_VIS_MAX` 65536, and the cached meshes are sorted far to near (the depth of the origin, `m[15]`) so that a frame split keeps the near meshes last. Checked by eye on the RGB30 (iterations 3 and 4 of `heroes`). The full 3D Bench was not run with these limits: the console was off.

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
herocart on the RGB30 (29 Oct, `gpu3d_queue=2`, `gpu3d_later=1`): the menus flickered left and right and the frame rate was 15. The runtime keeps `gpu3d_queue2()` at 0, so the option cannot start. The pictures of the shots were right. The cause is not found.
Ruled out by reading: the display flip (`plat_display_wait_vsync` waits until the new address is taken), the page of the frame in flight (a copy), the order of the resolve and the show.
Open: find the cause with a log of the page and the frame number at the submit, the resolve and the show; a worker core for the wait and the resolve (1.2 ms).
