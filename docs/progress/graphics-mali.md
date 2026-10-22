# Mali (RGB30)

The RGB30's Mali-G52 driver, part of [graphics](graphics.md). Driver versions: [`docs/DRIVERS.md`](../DRIVERS.md); the device: [`docs/RGB30.md`](../RGB30.md).

## Mali-G52 (`src/rgb30/mali.c`, `mali.h`, `mali_desc.c`)
bm3d 6.0-6.1: supply, power domain, identify, reset, cores, MMU (LPAE, 2 MiB blocks), job
manager (WRITE_VALUE chain), a FRAGMENT job that clears a surface (6.1) and, written and not yet a version, a triangle (Vertex,
Tiler and Fragment jobs, a 96x96 square of the screen; the step is only on the console,
`mali_hw_t.triangle`). The triangle step is not yet run on the device: the Dev > GPU test
report says (`mali: the vertex job wrote ...`, `mali: a triangle: inside ... outside ...`). PC test with
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
- Fault on the device (21 Oct, report `20261021-154617_gpu_rgb30_dev.txt`): the vertex and tiler job
  ends with `mali: a vertex and a tiler job: failed, JS_STATUS 58 DATA_INVALID_FAULT`. The clear works.
- Variant probe (`src/rgb30/mali_variants.h`, `triangle()` in `mali.c`): the base jobs (set 0, draw 0 of
  `frame_build`) run first. If they fail, the probe resets the GPU and runs 8 variants of the jobs, each built
  again with one patch (`mv_table`: block, first bit, bit count, value). One Dev > GPU test run gives all the results.
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
   - *Phase 1*: triangles prepared by the ARM and rasterized by the GPU (triangle in the *GPU test*, with the variant probe).
   - *Phase 2*: `r3d` backend with batching and one color per draw (written, not run).
   - *Phase 3*: vertex shader for the geometry transforms on the GPU (`BM_VS3`, written, not run).
   - *Phase 4*: asynchronous job queue with multi-buffering support (written, not run).
- **Completion criterion**: On the RGB30 Overbit `.b16` runs stably at 360×360 and at 720×720, the benchmark produces the performance report (first with ARM, then with Mali) and the RGB30 3D Bench table reports the GPU column data.

## Open work
All of it needs the RGB30 (not verified on hardware). The user runs these; nothing here starts a device run.
1. Dev > GPU test (the triangle, with the variant probe). Read the `mali: variant N <name>` lines in the report on the SD card. If a variant is `ok`, apply its patch to `frame_build` and delete the probe.
2. If the triangle draws: `mali3d=1` in `config.txt`, then 3D Bench (`b3d`) scenes of flat colours. The `GPU` profile row appears only for those scenes; the others show the ARM.
3. Then the phases written but not run: phase 2 (`mali_backend`), phase 3 (`BM_VS3` with a matrix), phase 4 (`mali_frame_submit` and two sets). Run them in this order, one report each.
4. If all three run: `mali3d` default decision (divergence D45 in `docs/spec/divergences.toml`) and a bm3D version with a row in `docs/DRIVERS.md`.
5. A shader for textured triangles is not started: textures and effects stay on the ARM (`colour_only`).

Alignment with the V3D driver (API-5). Do the steps in this order. Test each step on the RGB30 through the bridge (`--dev rgb30`).
1. The user's triangle run (H1) and the 3D Bench run (H6), with the bridge.
2. Textures and lit faces: remove `colour_only`.
3. The vertex-shader mesh path (`BM_VS3`).
4. The draw queue.
5. The cel shader.
6. The 3D Bench scenes (heroes, herotris) and the ARM, GPU and GPU+VS columns.

## Rules (do not break)
- `mali3d` is off by default. With the key off, or after any Mali failure, `gpu3d_*` goes to the ARM (`gpu3d_init` reads the key on every init).
- The Mali draws flat colours only. `r3d` hands textures and effects to the ARM (`colour_only`).
- A frame uses one of two sets of the GPU memory (`MALI_SET_BASE`); the layout macros (`SO_*`, `D_*`) are in `mali_variants.h` and the variant patches use them. Change a layout macro and `make TARGET=rgb30 test-mali` must still pass.
- The variant probe runs only after the base jobs fail. It resets the GPU between variants.
- Report hardware behaviour only from a report file. Without one, write `not verified on hardware`.
