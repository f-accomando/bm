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

## Roadmap: Mali GPU & Overbit in `.b16` (RGB30)

1. **Overbit in `.b16` on RGB30**:
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
- **Completion criterion**: On the RGB30 Overbit `.b16` runs stably at 360×360 and at 720×720, the benchmark produces the performance report (first with ARM, then with Mali) and the RGB30 3D Bench table reports the GPU column data.

## Open work
Hardware verified on RGB30 (`bm3d 7.8`..`7.14`, 25 Oct 2026): H1 triangle (`80d62d1e`), H6 flat-colour 3D Bench (`94aae9dd`), Phase 2 multi-draw batching (`fcfd2933`), Phase 3 `BM_VS3` probe (`b041b71a`), Phase 4 async 2-set queue probe (`9e4652bd`), `BM_VS3` mesh path + async queue (`7509aa08`), and cached 2 MB resolve (`dc civac`) + 8192-triangle merge-sorted batching (`e259a8c8`, `docs/bench/mali-2026-10-25/step8-bench3d-cached-resolve.txt`).

Hardware Results (3D Bench 25 Oct 2026, 640x360, `bm3d 7.14`, report `e259a8c8`, GPU score **138** vs **14** in `7.10`):
| Test Scene | RGB30 ARM (0.2) n60 / n30 | RGB30 Mali GPU (2.1) n60 / n30 | RGB30 Mali GPU+VS (3.4) n60 / n30 | Pi Zero W V3D (GPU+VS) n60 |
| :--- | :--- | :--- | :--- | :--- |
| `quad_flat` (fill) | 15.8 / 33.6 (32 tri) | **211.5 / 457.0 (423 tri)** | **211.3 / 455.9 (0.27 ms/frame)** | 163.7 (174.5 GPU) |
| `draws` (cubes) | 762.9 / 1910.4 (9,155 tri) | 67.9 / 129.6 (815 tri) | **509.8 / 1112.6 (6,118 tri)** | 1,386 (467 ARM) |
| `spheres_unlit` | 164.8 / 437.2 (15,821 tri) | 13.6 / 26.7 (1,301 tri) | **92.5 / 187.3 (8,883 tri)** | ~885 (~85,000 tri) |
| `spheres` (lit) | 140.5 / 376.8 (13,489 tri) | 7.4 / 15.2 (709 tri) | **24.1 / 93.6 (2,309 tri)** | 782.9 (75,159 tri) |
| `spheres_smooth` | 72.1 / 264.7 (6,920 tri) | 7.2 / 14.7 (694 tri) | **24.1 / 93.6 (2,309 tri)** | ~730 (~70,000 tri) |
| `tiny` (512f grids) | 26.9 / 57.7 (13,765 tri) | 10.7 / 22.1 (5,486 tri) | **13.5 / 27.4 (6,891 tri)** | ~190 (97,373 tri) |
| `split` (3D+2D) | 21.8 / 46.8 (8,359 tri) | 1.8 / 3.6 (701 tri) | **4.0 / 8.6 (1,538 tri)** | ~52 (~20,000 tri) |
| `queue` (+4M instr) | - / - | 5.2 / 12.5 (501 tri) | **14.3 / 70.3 (1,374 tri, +Q)** | 631.1 (+Q) |

Next hardware steps:
1. [DONE 25 Oct] Triangle bringup (H1): Primitive Size Constant = 1.0f and HIERARCHY_MASK 0x28 applied. Passed pixel check on HW.
2. [DONE 25 Oct] 3D Bench with flat colours (H6): With `mali3d=1` in `bm/config.txt`, ran 3D Bench (Settings > Screen > 3D Bench); flat-colour scenes ran on GPU (bm3d 2.1), textured scenes stayed on ARM. Report `94aae9dd`.
3. [DONE 25 Oct] Multi-draw batching fix (Phase 2): In `frame_build()`, aligned attribute buffer and tiler position pointers to 64 bytes (`first += (nv + 3) & ~3u;`). Eliminated `DATA_INVALID_FAULT` (0x58) in subsequent draws. Verified on hardware (report `fcfd2933`). Frames of up to 24 draws render without faults.
4. [DONE 25 Oct] Vertex shader transformation (Phase 3): Verified `BM_VS3` on hardware (`mali_probe_vs3`, report `b041b71a`): 4x4 matrix pushed via UBO 0 (`MALI_FRAME_VPUSH`) transforms model-space vertices (`41000000 42b00000 3f000000 3f800000`) and renders alongside `BM_VS` draws in 16 + 27 us.
5. [DONE 25 Oct] Asynchronous queue & multi-buffering (Phase 4): Verified `mali_frame_submit` with sets `MALI_SET_BASE(0)` and `(1)` in flight on hardware (`mali_probe_async`, report `9e4652bd`): 2 frames submitted back-to-back complete in 204 us with exact pixel checks (`s0 40e080/102050`, `s1 e040a0/301040`).
6. [DONE 25 Oct] `mali_backend` `BM_VS3` mesh path + async queue + cached resolve (`bm3d 7.13`–`7.14`, reports `7509aa08`, `e259a8c8`): Wired `mali_backend.mesh` (`be_mesh`) with per-bone back-face culling, per-face lighting/cel/fog, 2-mode batching, async Slot 0 Fragment Job overlap, cached 2 MB resolve surface (`dc civac`), and 8192-triangle merge-sorted scene batching. GPU score rose **10x** (`14` -> `138`); `quad_flat` (`211.5` @ 60 fps) beats Pi Zero W V3D (`174.5`); `draws` reached `509.8` @ 60 fps (`1,112.6` @ 30 fps, **35x** vs `7.10`); `spheres_unlit` reached `92.5` @ 60 fps (**28x** vs `7.10`).
7. Textures: compile textured fragment shader with Mesa Bifrost compiler (`tools/mali/mkshader.c`), add texture/sampler descriptors, and remove `colour_only` fallback.

## Mali vs Pi V3D Architectural Equivalence & Feature Parity (API-5)

Mapping of Mali-G52 developments against reference Pi V3D driver:

| Pipeline Stage / Feature | Pi Zero W (V3D 2.1) | PowKiddy RGB30 (Mali-G52) | Parity Status & Notes |
|---|---|---|---|
| **Framebuffer / Clear** | FBD (Framebuffer Descriptor) / Tile setup in RGB565/RGBA8888 | SFBD / MFBD (Single/Multi FB Descriptor) in linear XRGB8888 | **Full Parity** (HW verified; 64x64 clear & screen presentation pass) |
| **Vertex Processing** | Coordinate / Vertex Shaders on QPU (`vs_colour`, `vs_lit_tex`) | Compute Job on slot 1 with Bifrost vertex shader (`BM_VS`, `BM_VS3`) | **Full Parity** (HW verified in `mali_backend.mesh` with `gpu3d_vshader=1/2`) |
| **Binning / Tiling** | Binner Job (`BPOA` / `BPOS`), tile allocator pool with overflow | Tiler Job on slot 1 (`SO_HEAP`, `SO_HEAPD`, `SO_TCTX`, mask 0x28) | **Full Parity** (HW verified; Primitive Size 1.0f fixed fault) |
| **Fragment / Raster** | QPU Fragment Shaders (`fs_colour`, `fs_tex_lit`, `fs_cel`) | Fragment Job on slot 0 (`BM_FS` with pushed color words 4-7) | **Flat Colour Parity** (HW verified; textured shader pending Mesa build tree) |
| **State Management** | NV Shader State Record / Blend configuration | `Renderer State` (`md_renderer_state_t`) + `Blend` per target | **Full Parity** (Descriptors from Mesa `v7.xml`) |
| **Depth & Sorting** | Hardware Early-Z / Late-Z buffer attachment and depth test | CPU far-to-near sort (`be_tris`), 64-bit resolve key `0xFF00FF` (`be_resolve`) | **Missing on Mali**: Hardware Z-buffer attachment (currently software sort) |
| **Textures & Lighting** | TMU texturing (nearest, bilinear), lit faces, ETC1, 16-bit sheets | Per-face lighting/cel/fog in `be_mesh`; textures stay on ARM (`colour_only = 1`) | **Partial Parity**: Untextured lit/cel/fog meshes on GPU; textures on ARM |
| **Cel Shading** | `fs_cel` on QPU with 64-entry quant table and inverted hull outline | Per-face cel quantization (`cel_step`) in `be_mesh` | **Partial Parity**: Flat-shaded cel quant on GPU; smooth per-pixel cel on ARM |
| **Job Queue & Async** | `queue=1`/`queue=2` (`JOB_BLOCK`/`JOB2_BLOCK`), ARM/GPU overlap | 2 memory sets (`MALI_SET_SIZE`) + async Slot 0 Fragment Job (`mali_backend_submit`) | **Full Parity** (HW verified in 3D Bench `GPU+VS+Q` / `GPU+VS+Q2`) |
| **Draw Merging** | `gpu3d_merge` (bm3d 7.6, world-space multi-mesh batching) | 2-mode batching in `be_flush_mode` (`BM_VS3` `<=64` draws, screen-space merge `>64`) | **Full Parity** (HW verified; merges multi-mesh scenes into 1 GPU pass) |
| **Config & Safety** | `gpu3d=1` default, automatic fallback to ARM | `mali3d=0` default (D45), clean ARM fallback on any fault | **Full Parity** (`gpu3d_init` verifies Mali and falls back to ARM) |

## Rules (do not break)
- `mali3d` is off by default. With the key off, or after any Mali failure, `gpu3d_*` goes to the ARM (`gpu3d_init` reads the key on every init).
- The Mali draws flat colours only. `r3d` hands textures and effects to the ARM (`colour_only`).
- A frame uses one of two sets of the GPU memory (`MALI_SET_BASE`); the layout macros (`SO_*`, `D_*`) are in `mali_variants.h` and the variant patches use them. Change a layout macro and `make TARGET=rgb30 test-mali` must still pass.
- The variant probe runs only after the base jobs fail. It resets the GPU between variants.
- Report hardware behaviour only from a report file. Without one, write `not verified on hardware`.
