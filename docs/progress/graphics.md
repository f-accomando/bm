# Graphics

Framebuffer, 2D (`gfx16`), software 3D (`r3d`), the V3D driver (`gpu3d`) and the Mali
bring-up. Driver versions and history: [`docs/DRIVERS.md`](../DRIVERS.md); bench and score:
[`docs/BENCH3D.md`](../BENCH3D.md); numbers: `docs/PERFORMANCE.md`, `docs/STRESS.md`.
Lua API: [`docs/api/draw.md`](../api/draw.md), [`docs/api/light3d.md`](../api/light3d.md).

## How it is today

### Framebuffer and screens (`src/drivers/fb.c`, `src/bm/runtime.c`)
- Mailbox framebuffer, RGB565; virtual height = height × buffers (1..3), page flip with
  `SET_VIRT_OFFSET`. The HDMI scaler enlarges to the TV mode at no CPU cost.
- Cartridge resolutions (header): 640×360 (default), 480×270, 320×180, and the square
  256×256 and 360×360 (the `.b16` screen), boxed in the middle of 16:9 on the Pi.
- `screen(w, h)` switches at run time among `screen_modes[]` (320×180 … 1920×1080, 16:9,
  whole pixels on 1080p); a square cartridge keeps its screen.
- Menu: 1x (640x360) by default, the HDMI scaler enlarges it (`menu_scale` sets 1, 2 or 3; Settings > Screen > Menu size). `menu_gpu=1` enables 1080p 1:1 GPU rendering (V3D quads, blit, 24x48 text, enlarge 0 us; verified on the Pi Zero W). With `menu_gpu=0`, the ARM falls back to 1x (or manual `menu_scale` ARM pixel copy).

### 2D (`src/bm/gfx16.c`, `gfx16.h`)
- All 2D in C (Lua only logic): primitives, `spr`/`sspr`, `map()` with up to 8 layers
  (`BM_LAYERS_MAX`, LAYERS section 12), tile flags (FLAGS 13), named zones (SPRITES 11).
- Sheets up to `BM_SHEET_MAX` 4096 px a side; SHEET8 (≤ 256 colours) stored smaller.
- Darkness and glows: `g16_fade_*` tables (Lua `light_begin`/`dark_begin`/`glow`…), used by
  Yharnam.

### Software 3D (`src/bm/r3d.c`, `r3d.h`)
- The ARM transforms, lights, culls and clips (VFP floats) and rasterizes in fixed point
  into a 16-bit z-buffer: flat/Gouraud (dithered), perspective-correct textures from the
  sheet, near-plane clip, sun + sky/ground light, specular, rim, point lamps, fog, per-face
  materials (emissive, glossy, screen-door, LOD), planar shadows, a first-person layer,
  points/lines/billboards, rigid skinning (one bone per vertex).
- `r3d_fast=1` (*3D on the ARM: Fast*): one matrix and light in model space; off keeps the
  `bench3d` checksums.
- A backend (`r3d_t.backend`, NULL = software) takes the screen triangles: that is the GPU
  path; `arm_hook` hands a draw back to the ARM when the backend cannot do it.
- Flat textures (bm3d 7.0): `draw3d` flag 128 (`R3D_FLATTEX`) and `textures3d()` (`r3d_t.notex`)
  draw textured faces in their average texel (`flat_prepare` in `r3d.c`); on the GPU backend
  such a mesh goes triangle by triangle.

### V3D driver (`src/gpu/gpu3d.c`, `v3d.c`, `v3d_cl.c`, `shaders.h`)
- Version in `src/gpu/version3d.h` (`BM3D_VERSION` "7.2"); the table there lists
  every step and which settings reproduce older ones.
- Default where a V3D answers; software when `gpu3d=0`, in QEMU, or when the boot probe
  fails. What is not documented is learnt by the probe at boot and switched off by itself
  (e.g. VPM offsets in bytes `vpm_bytes`, clipper always on `gl_clip_all`, indexed
  primitives).
- One job per frame where possible: binning list + NV/GL records, tiles loaded from the page
  (2D under the 3D stays), 24-bit depth, MSAA 4× option.
- QPU shaders written in `tools/qpuasm.py` and generated into `src/gpu/shaders.h`: fragment
  `fs_*` (two-thread variants `fs_*_t`), vertex `vs_baked`, `vs_tex_rgb`, `vs_lit`,
  `vs_lit_tex`, `vs_lit_tex2`, compact `vs_baked8`, `vs_lit8`, shadows `vs_shadow*`, coordinate `cs_*`, `fs_zclear`.
- Options (each a `bm/config.txt` key, a Settings row and, for the main ones, an argument of
  `gpu3d([on, aa, vs, queue])`): `gpu3d`, `gpu3d_aa`, `gpu3d_vs` (0/1/2), `gpu3d_queue`
  (1 = default on the Pi, 0 = off, 2 = two jobs in flight), `gpu3d_wc`, `gpu3d_2d`, `gpu3d_filter`, `gpu3d_tex16`,
  `gpu3d_fs2`, `gpu3d_sort`, `gpu3d_attr` (off by default, `1` on), `gpu3d_etc1`, and the hidden
  `gpu3d_merge` (draw merging, bm3d 7.6, on, `0` off; no Settings row).
- Textures: RGBA8888 in T-tiles; with `gpu3d_tex16` opaque sheets as RGB565 and cut-out
  sheets (1-bit alpha) as RGBA5551 (bm3d 7.2); with `gpu3d_etc1` opaque sheets as ETC1
  (bm3d 7.3, `src/gpu/etc1.c`); each type kept only if its boot probe passed
  (`docs/progress/v3d.md`).
- Mesh cache: one slot per (mesh, sheet, unlit, smooth), made again when `gpu3d_attr` changes
  (`mesh_get`); indexed groups are
  padded to 16 bytes in their allocation.
- 2D on the GPU (`gpu3d_2d=1`): rects, pixels, straight lines, `spr`/`sspr` (whole zooms),
  `map()`, `print` as quads in the job; the rest closes the job and is drawn by the ARM.

### Mali-G52 on the RGB30
Bring-up, draw descriptors, shaders and the triangle probe: [graphics-mali](graphics-mali.md).

### Rendering tests (Settings > Screen)
The 3D Bench (`src/bm/b3d.c`, `src/kernel/b3dpi.c`) stops on Start+Select, Ctrl+Esc or PS
after the frame on screen (`syskeys_test_*` in `src/kernel/syskeys.c`,
`b3d_platform_t.stop`, `bm_stats_t.left`); a stopped test leaves no report
(`reports_drop`). Tests: `make test-b3d` (`--stop`), QEMU `test_bench_stop`.

## Open work (`docs/ROADMAP.md`)

- V3D (bm3D 3.0-7.3) on the Pi Zero W: [v3d](v3d.md).
- Menu 1080p GPU drawing on the Pi Zero W: done with `menu_gpu=1` (24 Oct: 1920x1080 1:1 drawn by V3D, font 24x48, enlarge 0 us, tested and verified on Pi Zero W hardware).
- Mali-G52 driver: [graphics-mali](graphics-mali.md) (the variant probe has no entry point since the GPU test page was removed on 24 Oct 2026; then the 3D Bench with `mali3d=1` on the device).
- 3D Bench, Lua edition for the Market (`carts/bench3d`, [BENCH3D](../BENCH3D.md) *Lua edition*; the C bench stays): B3D-1 `gpu3d_opt()`, B3D-2 `gpu3d_stats()`, B3D-3 (the ramp, the report) and B3D-4a/b/c (the tests) are done and tested on the V3D emulator (`make test-gpu3d-opt test-bench3d test-bench3d-all`). Scope of 25 Oct (second decision): bm3D 7.7 only and light. Profiles: ARM and GPU+VS+Q by default, GPU+VS+Q2 (frame later) on request. 22 tests: spheres, heroes (plain, skin, shadow), tiny, draws, quad_flat/tex/alpha, cel (lit, 4 bands, outline), the 2D scenes (map8, sprites, stack, fade, light), herotris, the htmap pair for merging (ht_p100, ht_p100m, ht_p10m) and stress. Models with bones and baked light come from `carts/bench3d/mkassets.py` at build time. The default run is about 400 steps of 4 frames (estimate 40 to 60 s on the Pi). Left: B3D-8 Market and docs; a Pi run to check the estimate and the logic loop (`WORK_STEPS`); an RGB30 run. Not run on the Pi or the RGB30.
- Sprite stacking: `stack()`, stack zones (`stack name x y w h n` in the zones file, `zone()` 7th value) and `zstack()` are done (`g16_stack` in `gfx16.c`, CPU only, no memory per stack; Pi speed not measured). `stackv` (the same call with the slices one under the other; zone line `stackv name x y w h n`, bit 0 of byte 27 of the SPRITES zone) is done. The mesh conversion is a host tool: `tools/stack2mesh.py` (see [tools](tools.md)). The bm Pixel Stack page is done (see [tools](tools.md)). Not done: a button for the conversion in bm Pixel and bm Studio, the Pi speed.
- 3D particles: `particles*` and `lib.particles3d` are done (ARM step and draw in `r3d_pfx_*`, no cost without a pool; host test `test_pfx`, `make test-gameapi`). Not measured on the Pi Zero W. Not done: the QPU step (the idea is dropped for now).
- 2D benchmarks: `b3d tests=map8,sprites,stack,fade,light` ([BENCH3D](../BENCH3D.md), *The 2D scenes*). Compare with them before and after `map()` (PR 105), the `-O3` knob (`PI_O3_FILES`, when it lands) and the fade. Not run on the Pi yet.
- QPU programs outside 3D: the driver, the queue, a particle step and the fade by levels are done on the emulator (`src/gpu/qpu.c`, [v3d](v3d.md) item 5). The QPU programs PASS on the Pi Zero W (the GPU test, removed on 24 Oct 2026, with `gpu3d_qpu=1`, 22 Oct: 1024 particles ARM 158 us, QPU 47 us; 1024 fade pixels ARM 79 us, QPU 36 us). Only `dark_end()` calls them in the runtime (with `gpu3d_qpu=1`); its gain on a full-screen fade is not measured yet.
- 2D API gaps against LÖVE (API-7 to API-11, `docs/ROADMAP.md`): sprite rotation and scale with a transform stack, off-screen render targets, `palt` with alpha and blend modes, 2D physics, noise. Nothing is built. Write a design note for each item before its work order.
- Hero stress cart (plaza, LOD, option toggles) and cel shader API: design in [hero-stress-cart-and-cel-shader](../design/hero-stress-cart-and-cel-shader.md) (five stages). Stages 1 (the cart), 2 (cel light and `dither3d` on the ARM raster, bm3D 7.4), 3 (cel in the V3D vertex programs) and 4 (outline by the inverted hull, flag 512; both bm3D 7.5) are done: [herocart](herocart.md). Stage 5 (RGB30 edition) is not started. Stages 3 and 4 are tested on the emulator only; the Pi numbers are open.
- Hero benchmark ([BENCH3D](../BENCH3D.md), *herotris* and *The cel shader*): `b3d tests=herotris,cel` gives the hero triangles at 60 fps (ARM, GPU, GPU+VS, GPU+VS+Q2 with the frame-later model; the report says if the mesh path was ACCEPTED or fell back) and the ms of the lit hero against the cel hero (dither on and off). Built and tested on the host. Not run on the Pi yet. After the run, copy the report to `docs/bench/`.
- Pi speed, two checks for the user (`g16_map_mask` tile path and the per-file -O3 knob). Both are built and tested on the host and in QEMU; neither is measured on the Pi. Steps:
  1. Baseline: build the kernel from `bm-core` before this PR. Run `b3d tests=mix,heroes_tex,tiny,gpu2d,queue,big_logic,quad_tex` twice, back to back. Baseline numbers: `git show d8b1290b:docs/bench/20261019_bench3d_pi-zero-w_7.3.txt`. Write down `ms` of `gpu2d` and `mix`, and the size of `kernel.img`.
  2. map() change: build this branch with `make`. Run the same `b3d` line twice. The 3D Bench has no test made for the 2D tile layer (`map()` of `gfx16.c`). `gpu2d` and `mix` include layers, but how much `map()` they draw was not checked. The gain of the map() change is NOT measured. To measure it, run a cartridge with a large `map()` layer before and after, and compare the frame time.
  3. -O3 knob: build with `make clean; make PI_O3_FILES="src/bm/gfx16.c src/bm/r3d.c"`. Run the same `b3d` line twice.
  4. Accept the -O3 knob only if `ms` of `gpu2d` and of `mix` drop by at least 3 % and `kernel.img` grows by at most 5 %. Then make the list the default in the `Makefile`. If not, leave `PI_O3_FILES` empty.
  5. The same variable exists in `rgb30.mk`; a dry run shows `-O3` on the listed files, nothing else was tried there.

## Rules (do not break)

- Visible face: the side where the vertices are clockwise (`r3d.c`). Toward glTF z flips
  sign and the order reverses.
- `animate()` in `runtime.c` and the pose in `carts/animator/main.lua` do the same maths:
  change them together.
- 2D drawn while a GPU job is in flight goes through `draw2d()` / `sync3d()` /
  `flush3d(1)` in `runtime.c`.
- A step that changes what the driver can do raises `BM3D_VERSION` in `version3d.h` and adds
  a line to `docs/DRIVERS.md`.
- Settings: in a sub-menu Enter changes a choice (left goes back, no arrows).
- New GPU behaviour that changes look, latency or memory is an option (config key, Settings
  row, `gpu3d()` argument); the default is what the Pi verified.
- Early z is off after a `zclear()` quad in the job and in jobs that load the page (Mesa,
  HW-2905) — see the comments in `gpu3d.c` before touching depth.
- Shaders: edit `tools/qpuasm.py`, regenerate `shaders.h`, run `make test-qpu`.
- Linux GPU drivers are read, never copied (GPL); GPU structures from Mesa XML (MIT).
- In development only `make test-b3d` for the GPU; also
  `make test-gpu3d`, `test-queue2d`, `test-v3d` when the driver changes.
