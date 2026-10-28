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
| `gpu3d_aa` (MSAA) | not developed | the tile buffer can do 4 samples; the Mali ignores the key |
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

Culling on the GPU was tried (28 Oct): the cached blocks live in the GPU memory (a 3 MB area below the lit area), the whole mesh is one draw and the Draw descriptor culls back faces. With the front face clockwise on the screen (`front_ccw` 0) the pictures are the same as the ARM's (pixel check equal); `front_ccw` 1 culls every face. It is slower: `spheres` 246 against 257 with the ARM culling, `heroes` 12.6 against 13.5, `spheres_shine` 186 against 209. The vertex shader runs on every vertex, twice as many, and the ARM's test costs little. Not kept. The idea left is indexed vertices with the light computed in a way that shares them (a normal from the positions), which is a change of the picture to measure first.
