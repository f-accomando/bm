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
    resolved into the page by the key colour `0xFF00FF`, "not covered", 4 pixels per 64-bit pair); `r3d` draws opaque textured faces on the GPU (G1, 28 Oct) and hands the rest to the ARM.
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
The Mali draw build on four cores (26 Oct): GPU 433 -> 513, games 451 -> 491 (`multicore.md`).
ARM raster on four cores (V1, 26 Oct, kernel 1812; details in `multicore.md`): 3D Bench RGB30 games 451, GPU 433, ARM 601. On heroes, baked, shine and split the ARM path now beats the Mali path again; the Mali draw build (about 4 us a draw) stays the cost to cut.
Hardware verified on RGB30 (`bm3d 7.8`..`7.15`, 25 Oct 2026): H1 triangle (`80d62d1e`), H6 flat-colour 3D Bench (`94aae9dd`), Phase 2 multi-draw batching (`fcfd2933`), Phase 3 `BM_VS3` probe (`b041b71a`), Phase 4 async 2-set queue probe (`9e4652bd`), `BM_VS3` mesh path + async queue (`7509aa08`), cached 2 MB resolve (`dc civac`) + 8192-triangle merge-sorted batching (`e259a8c8`), and default `mali3d=1` + 4 MB single-pass GPU arena (`1024` draws, L1-cached descriptor packing, report `b3cab428`, `docs/bench/mali-2026-10-25/step9-bench3d-singlepass-default.txt`).

Hardware Results (3D Bench 28 Oct 2026, `bm3d 7.18`, report `20261028-114152_bench3d_rgb30_dev` on the `reports` branch): games **639**, GPU **755**, ARM **601** (25 Oct, `7.15`: games 247, GPU 228). The GPU wins on `quad_flat`, `quad_tex`, `spheres_tex`, `heroes_tex` and `heroes_skin`. The ARM wins on the other scenes (four cores since V1).
| Test Scene | RGB30 ARM (0.2) n60 / n30 | RGB30 Mali GPU (2.1) n60 / n30 | RGB30 Mali GPU+VS (3.4) n60 / n30 | Pi Zero W V3D (GPU+VS) n60 |
| :--- | :--- | :--- | :--- | :--- |
| `quad_flat` | 17.7 / 35.6 | 212.6 / 459.9 | 189.2 / 404.9 | 163.7 (174.5 GPU) |
| `draws` | 876.6 / 2024.7 | 612.8 / 1207.9 | 619.3 / 1352.7 | 1,386 (467 ARM) |
| `spheres_unlit` | 215.5 / 500.7 | 131.2 / 242.5 | 127.7 / 221.9 | ~885 (~85,000 tri) |
| `spheres` | 199.7 / 461.2 | 91.5 / 173.7 | 102.5 / 174.8 | 782.9 (75,159 tri) |
| `spheres_smooth` | 106.9 / 376.7 | 72.9 / 151.0 | 102.7 / 175.0 | ~730 (~70,000 tri) |
| `tiny` | 42.5 / 91.6 | 17.8 / 36.6 | 25.1 / 49.5 | ~190 (97,373 tri) |
| `split` | 28.0 / 58.7 | 9.6 / 19.4 | 9.2 / 18.6 | ~52 (~20,000 tri) |
| `queue` | - | 47.8 / 131.4 | 71.9 / 156.5 | 631.1 (+Q) |
| `gpu2d` | 3239.2 / 6541.0 | 2982.6 / 6456.4 | - | ~1,200 |
| `quad_tex` | 6.5 / 13.0 | 126.5 / 274.9 | - | - |
| `spheres_tex` | 52.3 / 184.5 | 123.6 / 168.3 | - | - |
| `heroes` | 15.6 / 31.6 | 2.7 / 4.9 | 3.4 / 5.4 | - |
| `heroes_tex` | 12.7 / 26.1 | 8.1 / 10.4 | 8.1 / 10.4 | - |
| `heroes_skin` | 12.6 / 26.0 | 8.1 / 10.3 | 8.1 / 10.3 | - |

Next hardware steps:
6. [DONE 25 Oct] `mali_backend` `BM_VS3` mesh path + async queue + cached resolve + single-pass 4 MB arena (`bm3d 7.13`–`7.15`, reports `7509aa08`, `e259a8c8`, `b3cab428`): Wired `mali_backend.mesh` (`be_mesh`), async Slot 0 Fragment Job overlap, cached 2 MB resolve (`dc civac`), 8192-triangle merge-sorted batching, L1-cached descriptor packing (`frame_build_ex`), 4 MB single-pass GPU arena (`1,024` draws), and enabled `mali3d=1` by default (`gpu3d_vs=2`, `gpu3d_queue=1`, `gpu3d_merge=1`). Games score rose from `192` to **`247`**; GPU score rose **16.3x** (`14` -> `228`); `quad_flat` (`211.3` @ 60 fps) and `gpu2d` (`1,849.2` @ 60 fps) beat Pi Zero W V3D.
Result of 26 Oct 2026 (G3, kernels 1771-1801, reports on the `reports` branch `claude-mali-frame-build-cost/` and `claude-rgb30-fast-memset/`). Read first: the scores of kernels 1777-1800 (488, 666) were wrong. `copy_slot` skipped the first line of each job header, and the GPU writes the first 16 bytes of a job header when it ends the job; the next frame the stale header made the GPU skip the draw, so many GPU scenes were black (2.1 and 3.4). Fix (kernel 1801): the two header lines of a slot are always written. The user saw the scenes correct with the line skip off; the pixels of kernel 1801 are still to be confirmed by eye. The numbers below are the real ones.
What was found: `frame_build_ex` took 15-25 us a draw on the ARM (10 ms of a 15 ms frame at 400 draws), not the GPU or the lighting (`be_mesh` is 0.4-1.5 ms). Causes and fixes: `md_bits` packed every descriptor field one bit at a time (now a byte at a time); the 1344-byte draw table went to the non-cacheable GPU arena on each draw (`copy_slot` writes only the changed 64-byte lines, except the job-header lines); the library's `memset`, `memcpy`, `memmove` and `memcmp` were byte loops (`src/rgb30/memops.c` moves 8 or 32 bytes a step; this is the whole kernel, not only the Mali). Full 3D Bench, same bench for each row (the report's `score` line): kernel 1777 (before the fix, black scenes, not valid) 488; kernel 1801 without `memops.c`: games **333**, GPU (bm3d 2.1) **412**, ARM **387**; with `memops.c` (b72418be): games **375**, GPU **417**, ARM **466**. The first G3 run (kernel 1768) had games 247, GPU 229. Spheres per second at 60 fps, kernel 1801 with `memops.c`: `spheres` ARM 167, GPU+VS 82; `spheres_baked` ARM 98, GPU 32; `spheres_shine` ARM 72, GPU+VS 41; `heroes` ARM 8.0, GPU 2.2; `split` ARM 24, GPU+VS 8.8; `queue` GPU+VS+Q 75. The GPU is still slower than the ARM on `spheres_baked`, `spheres_shine`, `heroes` and `split`: a face with its own colour makes one draw a triangle, and a draw still costs about 3 us on the ARM (the descriptor writes `b1`-`b3`). Lesson: check the pixels of a scene before you trust a faster number.
`md_bits` writes a field in one or two 32-bit words (kernel 1811, partial bench): `b2_us` 2786 -> 1991 on `heroes`, `spheres_baked` 32.0 -> 35.4 and `heroes` 2.4 -> 2.6 per second at 60 fps. What is left of a 14 ms frame at about 1000 draws: `jobs_us` 4.1-5.9 ms (the GPU: two jobs a draw, about 4 us a draw), `build_us` 4.1-4.8 ms, `resolve_us` 0.8-2.1 ms. The ARM stays ahead on those four scenes. Splitting `frame_build_ex` over the workers (V1) can take at most about 3 ms of the 14; the GPU's own 4 us a draw stays, so only one draw for many faces (a vertex-colour shader, Q2) closes the gap. Next: Q2-Q5 once the Mesa Bifrost compiler builds. `bench3d_boot=<tests>` (config key, set through the bridge; runs once, at the next boot) runs the bench and sends the report at its end.

Queue to align bm3D on Mali with the Pi driver (26 Oct 2026, order of work; detail in [multicore](multicore.md) track G):
- Q1. [DONE 26 Oct, see the result above] Read the new `mesh_us` / `flush_us` / `resolve_us` / `draws` columns of the C 3D Bench (kernel 1771 and later) and name the cost of the lit scenes.
- Q2. Lighting per face off the ARM: in the vertex shader (as the Pi's V3D does) or, as a shortcut, on worker cores (multicore V1).
- Q5. The other `gpu3d_*` options of the Pi driver: check one by one which Mali ignores (list in `docs/BENCH3D.md`).
Q2 needs the Mesa Bifrost compiler (`tools/mali/README.md`).

7. [DONE 28 Oct] Opaque textures on the GPU: `BM_FS_TEX` and `BM_VS_TEX` (`tools/mali/mkshader.c`), `Texture`, `Surface` and `Sampler` descriptors (`mali_desc.c`), a 2 MB texture pool. RGB30 3D Bench, same kernel: `mali_tex=0` games 482, GPU 495; `mali_tex=1` games 530, GPU 570. `quad_tex` 6.4 -> 126.3, `spheres_tex` 52 -> 105.3. The pixel check (K lines) shows rounding differences against the ARM: `quad_tex` 640 of 57600 pixels differ by more than 2 steps. Still open: alpha, screen-space and bilinear textures stay on the ARM; the first look by eye on the console is not done.
8. [DONE 28 Oct] Hardware depth test (`mali_z=1`, default): the tile buffer tests "less or equal" and clears to 1; no ZS surface is written back. Each vertex has z = 1 - R3D_NEAR / depth (mesh: row 2 of the matrix). `sort_tris` is skipped when one frame holds all the triangles (up to 1024 draws, tex slots). RGB30 3D Bench, same kernel: `mali_z=0` games 531, GPU 571; `mali_z=1` games 639, GPU 755 (ARM 601). `spheres` n60: GPU 46.0 -> 91.3, GPU+VS 81.1 -> 102.6. Pixel check equal to `mali_z=0` (`split` GPU+VS nearer to the ARM). Still open: look at the scenes by eye; the lit smooth scenes (`spheres_smooth`, `spheres_shine`) differ from the ARM by shading.
Parity table (Pi V3D 2.1 against Mali-G52, stage by stage): [mali-parity](mali-parity.md).

## Rules (do not break)
- The draw slots of the arena: `copy_slot` always writes the two job-header lines of a slot (D_VJOB, D_TJOB). The GPU writes the first 16 bytes of a job header; a skipped line gives black frames.
- Check the pixels of a GPU scene by eye before you trust a faster 3D Bench number (26 Oct: a faster score came from black frames).
- `src/rgb30/memops.c` holds the kernel's `memcpy`, `memmove`, `memset` and `memcmp`. Device memory goes through volatile accessors, not through them.
- `mali3d` is on by default (`bm3d 7.15`). With `mali3d=0`, `gpu3d=0`, or after any Mali failure, `gpu3d_*` goes to the ARM (`gpu3d_init` reads the key on every init).
- The Mali draws flat colours, untextured lit/cel/fog meshes and opaque textures. `r3d` hands alpha textures, screen textures, bilinear textures and effects to the ARM. `mali_tex=0` sends all textures to the ARM.
- Depth values: a triangle of `be_tri` carries 1/depth in z; the backend writes z = 1 - R3D_NEAR / depth. A mesh matrix has z/w = 1 - R3D_NEAR / depth (row 2 = row 3 minus R3D_NEAR). All triangles of a frame must be in one pass: a second frame has no depth of the first.
- The texture pool (2 MB) sits at `mem - 8 MB`; `fb.c` stops the framebuffer 8 MB lower. Move both together.
- Write textured vertices with volatile stores (a 128-bit float copy breaks strict aliasing). `BM_FS_TEX` keeps blue at 1/255 or more: `0xFF00FF` is the key colour.
- A frame uses one of two sets of the GPU memory (`MALI_SET_BASE`); the layout macros (`SO_*`, `D_*`) are in `mali_variants.h` and the variant patches use them. Change a layout macro and `make TARGET=rgb30 test-mali` must still pass.
- The variant probe runs only after the base jobs fail. It resets the GPU between variants.
- Report hardware behaviour only from a report file. Without one, write `not verified on hardware`.
