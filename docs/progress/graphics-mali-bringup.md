# Mali bringup and variant probe (RGB30)

The first triangle on the Mali-G52 (root causes, 25 Oct 2026) and the variant probe of `mali.c`. Part of [graphics-mali](graphics-mali.md).

## Mali-G52 bringup (`src/rgb30/mali.c`, `mali_variants.h`)
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

## Rules (do not break)
- A frame uses one of two sets of the GPU memory (`MALI_SET_BASE`); the layout macros (`SO_*`, `D_*`) are in `mali_variants.h` and the variant patches use them. Change a layout macro and `make TARGET=rgb30 test-mali` must still pass.
- The variant probe runs only after the base jobs fail. It resets the GPU between variants.

## Open work
None. The probe has no entry point since 24 Oct 2026. Read the driver state in [graphics-mali](graphics-mali.md).
