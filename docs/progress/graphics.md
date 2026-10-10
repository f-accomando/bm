# Graphics

Framebuffer, 2D (`gfx16`), software 3D (`r3d`), the V3D driver (`gpu3d`) and the Mali
bring-up. Driver versions and history: [`docs/DRIVERS.md`](../DRIVERS.md); bench and score:
[`docs/BENCH3D.md`](../BENCH3D.md); numbers: `docs/PRESTAZIONI.md`, `docs/STRESS.md`.
Lua API: [`docs/API.md`](../API.md).

## How it is today

### Framebuffer and screens (`src/drivers/fb.c`, `src/bm/runtime.c`)
- Mailbox framebuffer, RGB565; virtual height = height × buffers (1..3), page flip with
  `SET_VIRT_OFFSET`. The HDMI scaler enlarges to the TV mode at no CPU cost.
- Cartridge resolutions (header): 640×360 (default), 480×270, 320×180, and the square
  256×256 and 360×360 (the `.b16` screen), boxed in the middle of 16:9 on the Pi.
- `screen(w, h)` switches at run time among `screen_modes[]` (320×180 … 1920×1080, 16:9,
  whole pixels on 1080p); a square cartridge keeps its screen.
- Menu at 1080p: `menu_scale=3`, the 640×360 layout enlarged ×3 by the GPU
  (`gpu3d_enlarge`), by the ARM without GPU.

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

### V3D driver (`src/gpu/gpu3d.c`, `v3d.c`, `v3d_cl.c`, `shaders.h`)
- Version in `src/gpu/version3d.h` (`BM3D_VERSION` "6.8", block M36); the table there lists
  every step and which settings reproduce older ones.
- Default where a V3D answers; software when `gpu3d=0`, in QEMU, or when the boot probe
  fails. What is not documented is learnt by the probe at boot and switched off by itself
  (e.g. VPM offsets in bytes `vpm_bytes`, clipper always on `gl_clip_all`, indexed
  primitives).
- One job per frame where possible: binning list + NV/GL records, tiles loaded from the page
  (2D under the 3D stays), 24-bit depth, MSAA 4× option.
- QPU shaders written in `tools/qpuasm.py` and generated into `src/gpu/shaders.h`: fragment
  `fs_*` (two-thread variants `fs_*_t`), vertex `vs_baked`, `vs_tex_rgb`, `vs_lit`,
  `vs_lit_tex`, `vs_lit_tex2`, shadows `vs_shadow*`, coordinate `cs_*`, `fs_zclear`.
- Options (each a `bm/config.txt` key, a Settings row and, for the main ones, an argument of
  `gpu3d([on, aa, vs, queue])`): `gpu3d`, `gpu3d_aa`, `gpu3d_vs` (0/1/2), `gpu3d_queue`
  (1, 2 = two jobs in flight), `gpu3d_wc`, `gpu3d_2d`, `gpu3d_filter`, `gpu3d_tex16`,
  `gpu3d_fs2`, `gpu3d_sort`.
- Mesh cache: one slot per (mesh, sheet, unlit, smooth) (`mesh_get`); indexed groups are
  padded to 16 bytes in their allocation.
- 2D on the GPU (`gpu3d_2d=1`): rects, pixels, straight lines, `spr`/`sspr` (whole zooms),
  `map()`, `print` as quads in the job; the rest closes the job and is drawn by the ARM.

### Mali-G52 on the RGB30 (`src/rgb30/mali.c`, `mali.h`)
bm3d 6.0–6.1: supply, power domain, identify, reset, cores, MMU (LPAE, 2 MiB blocks), job
manager (WRITE_VALUE chain) and a FRAGMENT job that clears a surface. No triangles yet. PC
test with a simulated GPU: `make TARGET=rgb30 test-mali`. The 3D on the RGB30 is `r3d`.

### Rendering tests (Dev tab)
3D Bench (`src/bm/b3d.c`, `src/kernel/b3dpi.c`), render bench, stress test
(`src/bm/stress.c`) and Texture Room (`roombench.c`) stop on Start+Select, Ctrl+Esc or PS
after the frame on screen (`syskeys_test_*` in `src/kernel/syskeys.c`,
`b3d_platform_t.stop`, `bm_stats_t.left`); a stopped test leaves no report
(`reports_drop`). Tests: `make test-b3d` (`--stop`), QEMU `test_bench_stop`.

## Open work (`docs/ROADMAP.md`)

- **M35** frame queue, 2D after 3D, uncached job memory: done on the PC, to measure on the Pi.
- **M36** vertex shader: on the Pi only Overbit's benchmark with GPU+VS is missing.
- **M37** 2D on the GPU, menu at 1080p, bilinear textures, `r3d_fast`: to try on the Pi.
- **M39** toward the V3D's limit: compact 16/8-bit attributes, the runtime side of two jobs
  in flight, RGBA5551/4444, ETC1, mipmaps, hot game Lua in C.
- **M41** Mali: next are ARM-prepared triangles (tiler job, minimal Bifrost shader), then the
  vertex shader.
- **M43** sprite stacking (`stack()` in `gfx16.c`, bm Pixel page) — not started.
- **M44** QPU programs outside 3D (particles, full-screen 2D effects) — not started.

## Rules (do not break)

- Visible face: the side where the vertices are clockwise (`r3d.c`). Toward glTF z flips
  sign and the order reverses.
- `animate()` in `runtime.c` and the pose in `carts/animator/main.lua` do the same maths:
  change them together.
- 2D drawn while a GPU job is in flight goes through `draw2d()` / `sync3d()` /
  `flush3d(1)` in `runtime.c`.
- A step that changes what the driver can do raises `BM3D_VERSION` in `version3d.h` and adds
  a line to `docs/DRIVERS.md`.
- New GPU behaviour that changes look, latency or memory is an option (config key, Settings
  row, `gpu3d()` argument); the default is what the Pi verified.
- Early z is off after a `zclear()` quad in the job and in jobs that load the page (Mesa,
  HW-2905) — see the comments in `gpu3d.c` before touching depth.
- Shaders: edit `tools/qpuasm.py`, regenerate `shaders.h`, run `make test-qpu`.
- Linux GPU drivers are read, never copied (GPL); GPU structures from Mesa XML (MIT).
- In development only `make test-b3d` and Overbit (`make test-overbit`) for the GPU; also
  `make test-gpu3d`, `test-queue2d`, `test-v3d` when the driver changes.
