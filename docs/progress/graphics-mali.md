# Mali (RGB30)

The RGB30's Mali-G52 driver, part of [graphics](graphics.md). Driver versions: [`docs/DRIVERS.md`](../DRIVERS.md); the device: [`docs/RGB30.md`](../RGB30.md).

## Mali-G52 (`src/rgb30/mali.c`, `mali.h`, `mali_desc.c`)
bm3d 6.0-6.1: supply, power domain, identify, reset, cores, MMU (LPAE, 2 MB blocks), job
manager (WRITE_VALUE chain), a FRAGMENT job that clears a surface (6.1) and, written and not yet a version, a triangle (Vertex,
Tiler and Fragment jobs, a 96x96 square of the screen; the step is only on the console,
`mali_hw_t.triangle`). The triangle step is not yet run on the device. Its only entry was the Dev > GPU test page, removed on 24 Oct 2026 (`mali_hw.c` sets no `triangle`); the report says (`mali: the vertex job wrote ...`, `mali: a triangle: inside ... outside ...`). PC test with
a simulated GPU (no tiler there): `make TARGET=rgb30 test-mali`. The 3D on the RGB30 is `r3d`.
- Descriptors: `mali_desc.c` (Primitive, Draw, Renderer State, Blend, Local Storage, Uniform Buffer,
  Invocation, Viewport, Tiler Heap/Context, Attribute(+Buffer), Vertex and Tiler job layouts), places
  from Mesa's `v7.xml` (MIT). `tests/rgb30/v7pack.py v7.xml` makes `mali_desc_golden.h` from the XML
  itself; `mali_desc_test` compares the packers to it (in `test-mali`). A "padded" field stores 1 as 0.
- Shaders: `mali_shaders.h`, from `tools/mali/mkshader.c` with Mesa's Bifrost compiler (recipe and
  limits: `tools/mali/README.md`): vertex (a position in pixels, w = 1, no varyings) and fragment (one
  colour per draw, pushed: words 4-7). Without IDVS: a Vertex job, then a Tiler job whose Draw
  `position` is the buffer the vertex job wrote (varying buffer 1; an attribute buffer of binding n is
  entry 2n; a Renderer State is followed by one Blend per render target).
- Hardware bringup breakthrough (25 Oct 2026, report `80d62d1e`):
  - Root cause of DATA_INVALID_FAULT (0x58): Tiler Job offset 64 holds Primitive Size (Constant float 32-bit).
    Mesa sets Constant = line_width = 1.0f (0x3f800000) for all non-point primitives. md_tiler_job had left it 0.0f,
    causing immediate tiler hardware failure. Set Constant to 1.0f in `src/rgb30/mali_desc.c`.
  - Bifrost hierarchy mask: RK3566 GPU_TILER_FEATURES register (0x00c) returns `0x00000209` (`max_levels = 2`, `bin_size = 9`).
    The hardware enforces bitcount(hierarchy_mask) <= max_levels. Fixed HIERARCHY_MASK to 0x28 (levels 3 and 5), matching Mesa
    pan_cs.c and v7pack.py.
  - Device verification: `mali: a triangle (vertex, tiler and fragment jobs): drawn by the GPU` (inside ff8019, outside 102050).
- Variant probe (`src/rgb30/mali_variants.h`, `triangle()` in `mali.c`): the base jobs (set 0, draw 0 of
  `frame_build`) run first. If they fail, the probe resets the GPU and runs 8 variants of the jobs, each built
  again with one patch (`mv_table`: block, first bit, bit count, value). One run of the probe gives all the results (no entry point since the GPU test page was removed on 24 Oct 2026).
  Read the log lines `mali: variant N <name>: ok` or `mali: variant N <name>: JS_STATUS 0x..`:
  - 0 `vertex-only` (the vertex job alone) and 1 `tiler-only` (the CPU writes the positions, the tiler
    job alone, no dependency) isolate the failing job. They never count as the one that works.
    0 ok and 1 fail: the tiler job or its tables are wrong. 0 fail: the vertex job is wrong.
  - 2 `heap-bottom64` (heap bottom = base + 64), 3 `vary0-copy` (varying buffer 0 = the position entry),
    4 `mask-ff` (hierarchy mask 0xff), 5 `no-depth-cull`, 6 `tiler-vrsd` (tiler job state = the vertex
    shader's), 7 `provoke-0` (first provoking vertex = 0). The first of these that is `ok` goes on to the
    fragment job (`mali: variant N <name> goes on to the fragment job`) and the pixel check.
  - All 8 fail with the same status: the fault is not in these fields. Other guesses: the Blend's memory
    format order (RGBA; the screen is XRGB), the fragment's pixel kill/ZS update (Weak Early), the heap size,
    no message preload.
  - Host test: `make TARGET=rgb30 test-mali` (the patches and the log lines; the simulated GPU has no tiler).
- State (verified on Mali-G52 hardware 25 Oct 2026, `bm3d 7.8`..`7.13`):
  - Phase 2 (W12, W13a, W13b): `mali_frame` draws N draws of one colour into an XRGB surface; `mali_backend` (flat colours and untextured lit/cel/fog meshes, sorted far to near,
    resolved into the page by the key colour `0xFF00FF`, "not covered", 4 pixels per 64-bit pair); `r3d` hands textured faces to the ARM (`colour_only`).
  - Phase 4 (W16): two buffer sets in the GPU memory (`MALI_SET_SIZE` 0x190000: a 368 x 368 surface with a 64-aligned pitch is 0x84400 bytes),
    `mali_frame_submit` / `mali_frame_busy` / `mali_frame_wait`; `mali_backend_submit` kicks Slot 0 Fragment Job asynchronously so ARM logic overlaps GPU rasterization (`gpu3d_queue=1/2`).
  - Phase 3 (W15, `BM_VS3`, a 4x4 matrix pushed per draw, from the Bifrost compiler; `BM_VS` built with the full viewport scale): `mali_backend.mesh` (`be_mesh`) culls back faces in object space, computes per-face sun/sky/rim/specular/lamp/cel/fog lighting, and emits `BM_VS3` draws (`gpu3d_vshader=1/2`).
- `origin/bm3d-mali-v1` (Grok) was not used: its additions do not compile (`mali_tri_batch_t`
  undeclared), the descriptors are invented (placeholder shaders, a made-up tiler job)
  and its `gpu3d.c` edit is the V3D driver, which the RGB30 does not build.

## Roadmap: Mali GPU & Overbit (RGB30)

Game backlog (user, 25 Oct 2026): Overbit on the RGB30 (item 1 below) is not a kernel divergence any more. D24 left `docs/spec/divergences.toml`.

1. **Overbit on RGB30**:
   - Runs at **360×360** (doubled ×2 by the hardware video controller) and native **720×720**, selectable in the *RESOLUTION* menu.
   - Adaptation with `screen()` for square modes (`fb_init_mode`: 64-byte rows and tiles of 16).
   - In Overbit: adapt the HUD, menus, sky and field of view 1:1 (`SW`, `SH`, `LW`×`LH` and `UI` of `00_core.lua`).
2. **Benchmark with ARM**:
   - Measure Overbit's performance (bot fight and hero ring) at both resolutions on ARM, with report generation.
3. **Mali driver in phases**:
   - *Phase 1*: triangles prepared by the ARM and rasterized by the GPU (triangle with the variant probe; verified on hardware, `bm3d 7.8`).
   - *Phase 2*: `r3d` backend with batching and one color per draw (verified on hardware, `bm3d 7.9` and `7.10`).
   - *Phase 3*: vertex shader for the geometry transforms on the GPU (`BM_VS3` and `mali_backend.mesh`, verified on hardware, `bm3d 7.11` and `7.13`).
   - *Phase 4*: asynchronous job queue with multi-buffering support (`mali_frame_submit` and `mali_backend_submit`, verified on hardware, `bm3d 7.12` and `7.13`).
- **Completion criterion**: On the RGB30 Overbit runs stably at 360×360 and at 720×720, the benchmark produces the performance report (first with ARM, then with Mali) and the RGB30 3D Bench table reports the GPU column data.

## Open work
Hardware verified on RGB30 (`bm3d 7.8`..`7.15`, 25 Oct 2026): H1 triangle (`80d62d1e`), H6 flat-colour 3D Bench (`94aae9dd`), Phase 2 multi-draw batching (`fcfd2933`), Phase 3 `BM_VS3` probe (`b041b71a`), Phase 4 async 2-set queue probe (`9e4652bd`), `BM_VS3` mesh path + async queue (`7509aa08`), cached 2 MB resolve (`dc civac`) + 8192-triangle merge-sorted batching (`e259a8c8`), and default `mali3d=1` + 4 MB single-pass GPU arena (`1024` draws, L1-cached descriptor packing, report `b3cab428`, `docs/bench/mali-2026-10-25/step9-bench3d-singlepass-default.txt`).

Hardware Results (3D Bench 25 Oct 2026, 640x360, `bm3d 7.15`, report `b3cab428`, games score **247** vs **192**, GPU score **228** vs **14** in `7.10`):
| Test Scene | RGB30 ARM (0.2) n60 / n30 | RGB30 Mali GPU (2.1) n60 / n30 | RGB30 Mali GPU+VS (3.4) n60 / n30 | Pi Zero W V3D (GPU+VS) n60 |
| :--- | :--- | :--- | :--- | :--- |
| `quad_flat` (fill) | 15.8 / 33.6 (32 tri) | **211.3 / 457.6 (423 tri)** | **143.2 / 307.1 (0.27 ms/frame)** | 163.7 (174.5 GPU) |
| `draws` (cubes) | 766.7 / 1918.4 (9,200 tri) | 116.5 / 219.6 (1,398 tri) | **533.8 / 744.6 (6,405 tri)** | 1,386 (467 ARM) |
| `spheres_unlit` | 164.4 / 437.1 (15,781 tri) | 22.5 / 46.9 (2,162 tri) | **103.8 / 212.1 (9,967 tri)** | ~885 (~85,000 tri) |
| `spheres` (lit) | 140.1 / 376.6 (13,449 tri) | 13.4 / 27.5 (1,290 tri) | **22.9 / 141.5 (2,197 tri)** | 782.9 (75,159 tri) |
| `spheres_smooth` | 71.4 / 261.1 (6,853 tri) | 12.0 / 26.6 (1,246 tri) | **22.9 / 141.8 (2,201 tri)** | ~730 (~70,000 tri) |
| `tiny` (512f grids) | 26.9 / 57.7 (13,788 tri) | 14.8 / 30.3 (7,561 tri) | **21.2 / 42.7 (10,869 tri)** | ~190 (97,373 tri) |
| `split` (3D+2D) | 21.7 / 46.8 (8,344 tri) | 3.1 / 6.2 (1,195 tri) | **5.2 / 9.9 (1,980 tri, +Q2)** | ~52 (~20,000 tri) |
| `queue` (+4M instr) | - / - | 9.0 / 23.1 (868 tri) | **17.7 / 141.7 (1,699 tri, +Q)** | 631.1 (+Q) |
| `gpu2d` (sprites) | 2819.1 / 6332.9 (576 tri) | **1849.2 / 5354.8 (1 job)** | **1845.4 / 5340.1 (+2D)** | ~1,200 |

Next hardware steps:
1. [DONE 25 Oct] Triangle bringup (H1): Primitive Size Constant = 1.0f and HIERARCHY_MASK 0x28 applied. Passed pixel check on HW.
2. [DONE 25 Oct] 3D Bench with flat colours (H6): With `mali3d=1` in `bm/config.txt`, ran 3D Bench (Settings > Screen > 3D Bench); flat-colour scenes ran on GPU (bm3d 2.1), textured scenes stayed on ARM. Report `94aae9dd`.
3. [DONE 25 Oct] Multi-draw batching fix (Phase 2): In `frame_build()`, aligned attribute buffer and tiler position pointers to 64 bytes (`first += (nv + 3) & ~3u;`). Eliminated `DATA_INVALID_FAULT` (0x58) in subsequent draws. Verified on hardware (report `fcfd2933`). Frames of up to 24 draws render without faults.
4. [DONE 25 Oct] Vertex shader transformation (Phase 3): Verified `BM_VS3` on hardware (`mali_probe_vs3`, report `b041b71a`): 4x4 matrix pushed via UBO 0 (`MALI_FRAME_VPUSH`) transforms model-space vertices (`41000000 42b00000 3f000000 3f800000`) and renders alongside `BM_VS` draws in 16 + 27 us.
5. [DONE 25 Oct] Asynchronous queue & multi-buffering (Phase 4): Verified `mali_frame_submit` with sets `MALI_SET_BASE(0)` and `(1)` in flight on hardware (`mali_probe_async`, report `9e4652bd`): 2 frames submitted back-to-back complete in 204 us with exact pixel checks (`s0 40e080/102050`, `s1 e040a0/301040`).
6. [DONE 25 Oct] `mali_backend` `BM_VS3` mesh path + async queue + cached resolve + single-pass 4 MB arena (`bm3d 7.13`–`7.15`, reports `7509aa08`, `e259a8c8`, `b3cab428`): Wired `mali_backend.mesh` (`be_mesh`), async Slot 0 Fragment Job overlap, cached 2 MB resolve (`dc civac`), 8192-triangle merge-sorted batching, L1-cached descriptor packing (`frame_build_ex`), 4 MB single-pass GPU arena (`1,024` draws), and enabled `mali3d=1` by default (`gpu3d_vs=2`, `gpu3d_queue=1`, `gpu3d_merge=1`). Games score rose from `192` to **`247`**; GPU score rose **16.3x** (`14` -> `228`); `quad_flat` (`211.3` @ 60 fps) and `gpu2d` (`1,849.2` @ 60 fps) beat Pi Zero W V3D.
Result of 26 Oct 2026 (G3, kernels 1771-1801, reports on the `reports` branch `claude-mali-frame-build-cost/` and `claude-rgb30-fast-memset/`). Read first: the scores of kernels 1777-1800 (488, 666) were wrong. `copy_slot` skipped the first line of each job header, and the GPU writes the first 16 bytes of a job header when it ends the job; the next frame the stale header made the GPU skip the draw, so many GPU scenes were black (2.1 and 3.4). Fix (kernel 1801): the two header lines of a slot are always written. The user saw the scenes correct with the line skip off; the pixels of kernel 1801 are still to be confirmed by eye. The numbers below are the real ones.
What was found: `frame_build_ex` took 15-25 us a draw on the ARM (10 ms of a 15 ms frame at 400 draws), not the GPU or the lighting (`be_mesh` is 0.4-1.5 ms). Causes and fixes: `md_bits` packed every descriptor field one bit at a time (now a byte at a time); the 1344-byte draw table went to the non-cacheable GPU arena on each draw (`copy_slot` writes only the changed 64-byte lines, except the job-header lines); the library's `memset`, `memcpy`, `memmove` and `memcmp` were byte loops (`src/rgb30/memops.c` moves 8 or 32 bytes a step; this is the whole kernel, not only the Mali). Full 3D Bench, same bench for each row (the report's `score` line): kernel 1777 (before the fix, black scenes, not valid) 488; kernel 1801 without `memops.c`: games **333**, GPU (bm3d 2.1) **412**, ARM **387**; with `memops.c` (b72418be): games **375**, GPU **417**, ARM **466**. The first G3 run (kernel 1768) had games 247, GPU 229. Spheres per second at 60 fps, kernel 1801 with `memops.c`: `spheres` ARM 167, GPU+VS 82; `spheres_baked` ARM 98, GPU 32; `spheres_shine` ARM 72, GPU+VS 41; `heroes` ARM 8.0, GPU 2.2; `split` ARM 24, GPU+VS 8.8; `queue` GPU+VS+Q 75. The GPU is still slower than the ARM on `spheres_baked`, `spheres_shine`, `heroes` and `split`: a face with its own colour makes one draw a triangle, and a draw still costs about 3 us on the ARM (the descriptor writes `b1`-`b3`). Lesson: check the pixels of a scene before you trust a faster number.
Next: the cost of a draw (the writes of `b1`-`b3`), the per-face draws, then Q2-Q5. `bench3d_boot=<tests>` (config key, set through the bridge; runs once, at the next boot) runs the bench and sends the report at its end.

Queue to align bm3D on Mali with the Pi driver (26 Oct 2026, order of work; detail in [multicore](multicore.md) track G):
- Q1. [DONE 26 Oct, see the result above] Read the new `mesh_us` / `flush_us` / `resolve_us` / `draws` columns of the C 3D Bench (kernel 1771 and later) and name the cost of the lit scenes.
- Q2. Lighting per face off the ARM: in the vertex shader (as the Pi's V3D does) or, as a shortcut, on worker cores (multicore V1).
- Q3. Textures on the GPU (item 7 below).
- Q4. Hardware Z-buffer (item 8 below), then drop `sort_tris` on the GPU path.
- Q5. The other `gpu3d_*` options of the Pi driver: check one by one which Mali ignores (list in `docs/BENCH3D.md`).
Needs the Mesa Bifrost compiler (`tools/mali/README.md`) for Q2 (shader), Q3 and Q4.

7. [FUTURE] Textures on GPU: compile textured fragment shader (`BM_FS_TEX`) + UV varying vertex shader with Mesa Bifrost compiler (`tools/mali/mkshader.c`), add Bifrost `Texture` and `Sampler` descriptors in `mali_desc.c`, and remove `colour_only` fallback.
8. [FUTURE] Hardware Z-Buffer / Early-Z: attach a 16/24-bit Depth/Stencil (`ZS`) buffer via MFBD `md_zs_crc_ext` and enable hardware depth test (`Z_FUNC = LESS`) to eliminate CPU far-to-near `sort_tris` and reduce pixel overdraw.

## Mali vs Pi V3D Architectural Equivalence & Feature Parity (API-5)

Mapping of Mali-G52 developments against reference Pi V3D driver:

| Pipeline Stage / Feature | Pi Zero W (V3D 2.1) | PowKiddy RGB30 (Mali-G52) | Parity Status & Notes |
|---|---|---|---|
| **Framebuffer / Clear** | FBD (Framebuffer Descriptor) / Tile setup in RGB565/RGBA8888 | SFBD / MFBD (Single/Multi FB Descriptor) in linear XRGB8888 | **Full Parity** (HW verified; 64x64 clear & screen presentation pass) |
| **Vertex Processing** | Coordinate / Vertex Shaders on QPU (`vs_colour`, `vs_lit_tex`) | Compute Job on slot 1 with Bifrost vertex shader (`BM_VS`, `BM_VS3`) | **Full Parity** (HW verified in `mali_backend.mesh` with `gpu3d_vshader=1/2`) |
| **Binning / Tiling** | Binner Job (`BPOA` / `BPOS`), tile allocator pool with overflow | Tiler Job on slot 1 (`SO_HEAP`, `SO_HEAPD`, `SO_TCTX`, mask 0x28) | **Full Parity** (HW verified; Primitive Size 1.0f fixed fault) |
| **Fragment / Raster** | QPU Fragment Shaders (`fs_colour`, `fs_tex_lit`, `fs_cel`) | Fragment Job on slot 0 (`BM_FS` with pushed color words 4-7) | **Flat Colour Parity** (HW verified; textured shader pending Mesa build tree) |
| **State Management** | NV Shader State Record / Blend configuration | `Renderer State` (`md_renderer_state_t`) + `Blend` per target | **Full Parity** (Descriptors from Mesa `v7.xml`) |
| **Depth & Sorting** | Hardware Early-Z / Late-Z buffer attachment and depth test | CPU far-to-near merge sort (`be_tris`), cached 64-bit resolve `0xFF00FF` | **Missing on Mali**: Hardware Z-buffer attachment (currently software sort) |
| **Textures & Lighting** | TMU texturing (nearest, bilinear), lit faces, ETC1, 16-bit sheets | Per-face lighting/cel/fog in `be_mesh`; textures stay on ARM (`colour_only = 1`) | **Partial Parity**: Untextured lit/cel/fog meshes on GPU; textures on ARM |
| **Cel Shading** | `fs_cel` on QPU with 64-entry quant table and inverted hull outline | Per-face cel quantization (`cel_step`) in `be_mesh` | **Partial Parity**: Flat-shaded cel quant on GPU; smooth per-pixel cel on ARM |
| **Job Queue & Async** | `queue=1`/`queue=2` (`JOB_BLOCK`/`JOB2_BLOCK`), ARM/GPU overlap | 2 memory sets (`MALI_SET_SIZE`) + async Slot 0 Fragment Job (`mali_backend_submit`) | **Full Parity** (HW verified in 3D Bench `GPU+VS+Q` / `GPU+VS+Q2`) |
| **Draw Merging** | `gpu3d_merge` (bm3d 7.6, world-space multi-mesh batching) | 4 MB single-pass arena (`1,024` draws) + screen-space merge (`>1,024` draws) | **Full Parity** (HW verified; merges up to `1,024` draws in 1 GPU pass) |
| **Config & Safety** | `gpu3d=1` default, automatic fallback to ARM | `mali3d=1` default (`bm3d 7.15`), clean ARM fallback on any fault | **Full Parity** (`gpu3d_init` verifies Mali and falls back to ARM) |

## Rules (do not break)
- The draw slots of the arena: `copy_slot` always writes the two job-header lines of a slot (D_VJOB, D_TJOB). The GPU writes the first 16 bytes of a job header; a skipped line gives black frames.
- Check the pixels of a GPU scene by eye before you trust a faster 3D Bench number (26 Oct: a faster score came from black frames).
- `src/rgb30/memops.c` holds the kernel's `memcpy`, `memmove`, `memset` and `memcmp`. Device memory goes through volatile accessors, not through them.
- `mali3d` is on by default (`bm3d 7.15`). With `mali3d=0`, `gpu3d=0`, or after any Mali failure, `gpu3d_*` goes to the ARM (`gpu3d_init` reads the key on every init).
- The Mali draws flat colours and untextured lit/cel/fog meshes. `r3d` hands textures and effects to the ARM (`colour_only`).
- A frame uses one of two sets of the GPU memory (`MALI_SET_BASE`); the layout macros (`SO_*`, `D_*`) are in `mali_variants.h` and the variant patches use them. Change a layout macro and `make TARGET=rgb30 test-mali` must still pass.
- The variant probe runs only after the base jobs fail. It resets the GPU between variants.
- Report hardware behaviour only from a report file. Without one, write `not verified on hardware`.
