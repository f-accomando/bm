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
- State (written, none of it has run on a Mali; H1, the triangle, comes first, then H6, the 3D Bench of the RGB30):
  - Phase 2 (W12, W13a, W13b): `mali_frame` draws N draws of one colour into an XRGB surface; `mali_backend` (flat colours, sorted far to near,
    resolved into the page by the key colour `0xFF00FF`, "not covered"); r3d hands textures and effects to the ARM (`colour_only`).
  - Phase 4 (W16): two buffer sets in the GPU memory (`MALI_SET_SIZE` 0x190000: a 368 x 368 surface with a 64-aligned pitch is 0x84400 bytes),
    `mali_frame_submit` / `mali_frame_busy` / `mali_frame_wait`; the backend draws synchronously.
  - Phase 3 (W15, `BM_VS3`, a 4x4 matrix pushed per draw, from the Bifrost compiler; `BM_VS` built with the full viewport scale): a draw with `mvp` runs the transforming vertex shader.
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
   - *Phase 1*: triangles prepared by the ARM and rasterized by the GPU (triangle with the variant probe; the *GPU test* page that ran it was removed on 24 Oct 2026).
   - *Phase 2*: `r3d` backend with batching and one color per draw (written, not run).
   - *Phase 3*: vertex shader for the geometry transforms on the GPU (`BM_VS3`, written, not run).
   - *Phase 4*: asynchronous job queue with multi-buffering support (written, not run).
- **Completion criterion**: On the RGB30 Overbit `.b16` runs stably at 360×360 and at 720×720, the benchmark produces the performance report (first with ARM, then with Mali) and the RGB30 3D Bench table reports the GPU column data.

## Open work
Hardware verified on RGB30:
- Triangle bringup H1 verified on 25 Oct 2026 (`step2-triangle-hw.txt`, report commit `80d62d1e`).
- 3D Bench flat colours H6 verified on 25 Oct 2026 (`step3-bench3d-flat.txt`, report commit `94aae9dd`, `3d0017.txt`).

Hardware Results (3D Bench 25 Oct 2026, 640x360):
| Test Scene | RGB30 ARM (0.2) tri @ 60 fps (ms) | RGB30 Mali-G52 (2.1) tri @ 60 fps (ms) | Pi Zero W V3D (2.1/VSQ) tri @ 60 fps (ms) |
| :--- | :--- | :--- | :--- |
| `spheres` | 13,433 tri (15.2 ms) | 3,165 tri (16.1 ms) | 78,000 tri (15.9 ms) |
| `spheres_smooth` | 6,845 tri (15.0 ms) | 2,894 tri (16.5 ms) | ~70,000 tri |
| `spheres_unlit` | 15,800 tri (16.1 ms) | 3,465 tri (15.1 ms) | ~85,000 tri |
| `spheres_baked` | 7,805 tri (14.9 ms) | 3,213 tri (16.1 ms) | ~75,000 tri |
| `spheres_shine` | 5,715 tri (15.3 ms) | 2,738 tri (9.6 ms) | ~65,000 tri |
| `heroes` | 10,620 tri (15.2 ms) | 2,845 tri (8.6 ms) | 104,945 tri (15.9 ms) |
| `split` | 8,326 tri (13.5 ms) | 1,419 tri (13.6 ms) | ~20,000 tri |
| `queue` | N/A | 2,360 tri (13.9 ms) | ~50,000 tri |
| `gpu2d` | 576 tri (16.3 ms) | 576 tri (14.6 ms) | ~15,000 tri |
| `mix` | 10,272 tri (13.5 ms) | [ARM fallback: textures] | ~45,000 tri |

Next hardware steps:
1. [DONE 25 Oct] Triangle bringup (H1): Primitive Size Constant = 1.0f and HIERARCHY_MASK 0x28 applied. Passed pixel check on HW.
2. [DONE 25 Oct] 3D Bench with flat colours (H6): With `mali3d=1` in `bm/config.txt`, ran 3D Bench (Settings > Screen > 3D Bench); flat-colour scenes ran on GPU (bm3d 2.1), textured scenes stayed on ARM. Report `94aae9dd`.
3. [DONE 25 Oct] Multi-draw batching fix (Phase 2): In `frame_build()`, aligned attribute buffer and tiler position pointers to 64 bytes (`first += (nv + 3) & ~3u;`). Eliminated `DATA_INVALID_FAULT` (0x58) in subsequent draws. Verified on hardware (report `fcfd2933`). Frames of up to 24 draws render without faults.
4. [DONE 25 Oct] Vertex shader transformation (Phase 3): Verified `BM_VS3` on hardware (`mali_probe_vs3`, report `b041b71a`): 4x4 matrix pushed via UBO 0 (`MALI_FRAME_VPUSH`) transforms model-space vertices (`41000000 42b00000 3f000000 3f800000`) and renders alongside `BM_VS` draws in 16 + 27 us.
5. Asynchronous queue & multi-buffering (Phase 4): `mali_frame_submit` with sets `MALI_SET_BASE(0)` and `(1)`.
6. Textures and lit faces: write textured fragment shader, remove `colour_only` fallback.

## Mali vs Pi V3D Architectural Equivalence & Feature Parity (API-5)

Mapping of Mali-G52 developments against reference Pi V3D driver:

| Pipeline Stage / Feature | Pi Zero W (V3D 2.1) | PowKiddy RGB30 (Mali-G52) | Parity Status & Notes |
|---|---|---|---|
| **Framebuffer / Clear** | FBD (Framebuffer Descriptor) / Tile setup in RGB565/RGBA8888 | SFBD / MFBD (Single/Multi FB Descriptor) in linear XRGB8888 | **Full Parity** (HW verified; 64x64 clear & screen presentation pass) |
| **Vertex Processing** | Coordinate / Vertex Shaders on QPU (`vs_colour`, `vs_lit_tex`) | Compute Job on slot 1 with Bifrost vertex shader (`BM_VS`, `BM_VS3`) | **Full Parity** (HW verified for both `BM_VS` and `BM_VS3` 4x4 matrix) |
| **Binning / Tiling** | Binner Job (`BPOA` / `BPOS`), tile allocator pool with overflow | Tiler Job on slot 1 (`SO_HEAP`, `SO_HEAPD`, `SO_TCTX`, mask 0x28) | **Full Parity** (HW verified; Primitive Size 1.0f fixed fault) |
| **Fragment / Raster** | QPU Fragment Shaders (`fs_colour`, `fs_tex_lit`, `fs_cel`) | Fragment Job on slot 0 (`BM_FS` with pushed color words 4-7) | **Flat Colour Parity** (HW verified; textured/cel shaders pending) |
| **State Management** | NV Shader State Record / Blend configuration | `Renderer State` (`md_renderer_state_t`) + `Blend` per target | **Full Parity** (Descriptors from Mesa `v7.xml`) |
| **Depth & Sorting** | Hardware Early-Z / Late-Z buffer attachment and depth test | CPU far-to-near sort (`be_tris`), resolve key `0xFF00FF` (`be_resolve`) | **Missing on Mali**: Hardware Z-buffer attachment (currently software sort) |
| **Textures & Lighting** | TMU texturing (nearest, bilinear), lit faces, ETC1, 16-bit sheets | None (`colour_only = 1`); textures and lighting handled on ARM | **Missing on Mali**: Textured fragment shader, samplers, texture descriptors |
| **Cel Shading** | `fs_cel` on QPU with 64-entry quant table and inverted hull outline | None on Mali; ARM fallback | **Missing on Mali**: Cel fragment shader |
| **Job Queue & Async** | `queue=1`/`queue=2` (`JOB_BLOCK`/`JOB2_BLOCK`), ARM/GPU overlap | 2 memory sets (`MALI_SET_SIZE`), state machine `SET_QUEUED`..`SET_DONE` | **Equivalent in code** (`mali_frame_submit` in `mali.c`; pending HW bench) |
| **Draw Merging** | `gpu3d_merge` (bm3d 7.6, world-space multi-mesh batching) | Basic batching in `mali_backend` (`MALI_BATCH_MAX` per color) | **Missing on Mali**: Multi-mesh world-space merge (`r3d_backend_t.merge`) |
| **Config & Safety** | `gpu3d=1` default, automatic fallback to ARM | `mali3d=0` default (D45), clean ARM fallback on any fault | **Full Parity** (`gpu3d_init` verifies Mali and falls back to ARM) |

## Rules (do not break)
- `mali3d` is off by default. With the key off, or after any Mali failure, `gpu3d_*` goes to the ARM (`gpu3d_init` reads the key on every init).
- The Mali draws flat colours only. `r3d` hands textures and effects to the ARM (`colour_only`).
- A frame uses one of two sets of the GPU memory (`MALI_SET_BASE`); the layout macros (`SO_*`, `D_*`) are in `mali_variants.h` and the variant patches use them. Change a layout macro and `make TARGET=rgb30 test-mali` must still pass.
- The variant probe runs only after the base jobs fail. It resets the GPU between variants.
- Report hardware behaviour only from a report file. Without one, write `not verified on hardware`.
