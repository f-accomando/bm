# V3D driver (Pi Zero W, ARM1176 only)

Tracking for M35, M36, M37, M39. Rest of the graphics stack: [graphics](graphics.md).
Numbers: `docs/DRIVERS.md`, `docs/BENCH3D.md` (none here).

## Today

- `v3d_start` / `v3d_busy` / `v3d_wait` (`src/gpu/v3d.c`) are async and polled. No IRQ:
  `INTENA` is never written and `src/kernel/irq.h` has no V3D line.
- Bin then render are sequenced in hardware by a semaphore.
- `queue=2` = two job memory blocks (`JOB_BLOCK` / `JOB2_BLOCK`, `gpu3d.c`): the ARM fills
  N+1 while the GPU renders N. `run_async` / `gpu3d_submit` still call `job_wait()` before
  `v3d_start`, so one GPU job runs at a time (`v3d_start` rejects a second).
- `gpu3d_later=1` (with `queue=2`, off): the frame is held one frame (`held` in `runtime.c`).
  Host test: `make test-queue2d`.
- Cache: `dcache_clean_invalidate_all` 4 times per job. Binner: static 4 MiB tile list + 2 MiB
  overflow (`BPOA` / `BPOS`); `OUTOMEM` is not handled (200 ms timeout, GPU off, ARM takes over).
- Vertex shader is done (`gpu3d_vs=2` default).
- `stat(16)`: the GPU's job time of the last frame (0 on the ARM). Read with the queue on, it
  includes the wait before the ARM looks: trust it only without the queue.
- `r3d_impostor=N` (off; 2..64 px of a 1080-line screen, threshold N*h/1080: `imp_threshold`
  in `r3d.c`; `gpu3d()` 5th; skinned meshes by their pose box). Host test: `make test-impostor`.
- `gpu3d_attr=1` (bm3d 7.1, off; Settings *3D corners*, 3D Bench GPU+VS+A8): compact
  attributes for `GV_BAKED` (5 words a corner, was 9: `vs_baked8`) and `GV_LIT` (10, was 14:
  `vs_lit8`). ARM side `put_mesh_corner` / `pack_abc`; shader side the regfile A unpack
  (`UNPACK_A` in `tools/qpuasm.py`, emulated in `tests/gpu/v3d_emu.c`); `probe_attr` keeps it
  only if the pixels match the floats (a step of RGB565 allowed). `make test-gpu3d`: same
  pixels, 28% fewer attribute words. Pi (probe `compact attributes yes`, 3D Bench GPU+VS+A8
  against GPU+VS+FS2 at 60 fps): spheres 837 vs 808 (+3.5%), heroes 72.3 vs 67.3 (+7%, GPU
  job 8.9 vs 10.3 ms), match 35.3 vs 30.8 (+15%, 9.3 vs 10.5 ms), big 11.4 vs 9.2 (triangles
  differ between the runs). Worth turning on; the default waits for a played Overbit with
  `gpu3d_attr=1` (the one played so far was `attr=0`, 1920x1080: 31.7 fps, GPU set the pace
  in 82% of the frames, GPU wait 10.4 ms).
- `gpu3d_tex16=1` (Settings *3D textures: ..., 16-bit*, 3D Bench GPU+T16): opaque sheets as
  RGB565 (type 4, bm3d 5.2); bm3d 7.2: sheets with transparent texels (cut-outs) as RGBA5551
  (type 3) in the same 64x32 T-tiles (`t16_word`), `cut16_of` / `tex_get`. Sheets have 1-bit
  alpha (`g16_sheet_t.alpha`), so nothing is lost but green's last bit (a step of RGB565 at
  most); the alpha shaders discard the same texels. `probe_cut16` draws a 64x32 texture
  with the alpha shader for each order of the fields (red/blue as RGB565 or swapped, alpha
  low or top bits) and keeps the first that gives every pixel and hole back (status `alpha
  textures RGBA5551 RGBA4444`, `gpu3d_tex16()` 2, options line `tex16=2`, `stats.cut16`).
  RGBA4444 (type 2) is probed the same way but unused: no sheet has alpha between 0 and 1.
  `make test-gpu3d`: same holes, red and blue the same, green a step at most
  (`EMU_T16_ALPHA=1` alpha on top: learned; `=2`: off).

## Decisions (from Pi reports, 2026-10-12)

- Interrupts, triple buffering, `v3d_async.c/.h` of the v3d-async-proposal: skip.
- Whole-cache clean (about 5 kcycles a job in the bench rows) and binner overflow (0 KiB, 0 jobs
  in every row) cost nothing measurable: range clean and dynamic overflow stay off the list.
- `gpu3d_later=1` = `queue=2` in the mean (50 vs 51 fps at 640x360), fewer late frames (38% vs
  50%): keep it an option, off.
- GPU 640x360 EXTREME: GPU job 4.2 ms of a 19.6 ms frame, the ARM sets the pace. At 1080p
  a played match is fill-bound (GPU 20.4 ms for 6105 tris; 720p 10.1 ms): fewer pixels, not
  fewer triangles, is the lever.

## To do, in order

1. Pixels at 1080p (R41): the cheap soft enlargement is scaler kernel 6 (config only, no GPU
   cost; below). A QPU bicubic / FSR-style pass stays open, only if 6 is not enough. Cost from
   the Pi 3D Bench: a full-screen 1080p pass with one bilinear read is ~3.5-5 ms of GPU
   (textured fill 1.65 ns/px nearest, 2.3 ns/px bilinear, plus ~0.3 ms fixed); bicubic/FSR
   more, not measured. 2D on GPU: 9.5 ns/px (19.6 ms at 1080p).
2. Impostors: Pi runs at `r3d_impostor` 16, 32 with the new threshold (8 = 2.7 render px
   at 640x360, no effect: session at 8 gave 57.7 fps, GPU 7.8 ms a frame, 8588 tris), photo of
   far heroes; then the default for crowds (R42).
3. Chain two jobs in hardware (CT0/CT1) only if a measurement shows the GPU idle between jobs.
4. M39 remaining: ETC1 (at build), mipmaps; RGBA4444 only if sheets get soft alpha. Attributes:
   `vs_tex_rgb` light as bytes (8 -> 6 words), `vs_lit_tex`/`vs_lit_tex2` flags
   and uv as halves (`.16a/.16b`), normals as bytes, then 16-bit positions (scaled per mesh,
   a separate x y z stream for the coordinate shader).
5. QPU programs outside 3D (M44): the toolkit notes are in `docs/ROADMAP.md`.

## scaling_kernel

`boot/config.txt` line 21 (`scaling_kernel=8`) picks the firmware HVS filter that enlarges the
mailbox framebuffer to the HDMI mode. One global kernel for the whole display (no
per-layer kernel). No official config.txt page lists it (checked raspberrypi.com
`config_txt` and `legacy_config_txt`): the values come from popcornmix (Raspberry Pi
firmware engineer) in firmware issue #104, 2012-10-18:
https://github.com/raspberrypi/firmware/issues/104 (closed "This is now supported", 2013-07-20).

| Value | Kernel (`SCALERLIB_KERNELS_TYPES_…`) | Status |
|-------|--------------------------------------|--------|
| 1 | `SINC` | issue #104; not tried on Pi |
| 2 | `SINC_BLACKMAN` | issue #104; not tried on Pi |
| 3 | `SINC_NO_SIDE_LOBES` | issue #104; not tried on Pi |
| 4 | `SINC_HALF_FIRST_SIDE_LOBE` | issue #104; not tried on Pi |
| 5 | `SINC_HAMMING` | issue #104; not tried on Pi |
| 6 | `SINC_HAMMING_3PI`: firmware default ("we normally use 6") | **verified on Pi**: looks good (soft), 640x360 -> 1080p |
| 7 | `SINC_HAMMING_2_5PI` | issue #104; not tried on Pi |
| 8 | `NEAREST_NEIGHBOUR` | issue #104; **verified on Pi** (bm today) |

- No bilinear, bicubic or Mitchell value is listed: 1-7 are sinc variants. From the names
  only (not measured): side lobes = sharper with possible ringing, 3 (no side lobes) the
  softest. 0 and values above 8: not documented.
- Mailbox framebuffer: the issue asked for framebuffer upscaling, so it applies there.
  Old reports (firmware #104/#168, 2013, 320x240 fb): kernels other than 6 gave a black
  screen or missing pixels, later fixed in firmware; not checked on today's firmware.
- `vcgencmd scaling_kernel` reads/sets coefficients at run time (Linux only, not bm).

### Try on the SD

Optional, still untried: 3, 5, 7. Pi off, change `scaling_kernel` in `config.txt` at the root
of the SD (not `boot/config.txt`), run Overbit on the GPU at 640x360 on 1080p, compare with 6
(sharpness, ringing, black screen); photo per value. Put back `scaling_kernel=8` afterwards.

## To measure on the Pi

Reports in branch `reports`; ask the user. The kernel must end in the commit tested.

- Impostor 0 / 8 / 16 / 32: `res:640/gpu:arm+vs/q:4/ring:1/far:60`.
- Compact attributes: a played Overbit with `gpu3d_attr=1` (session report, as the `attr=0`
  one at 1920x1080), then decide the default.
- RGBA5551 cut-outs (bm3d 7.2): boot status `alpha textures RGBA5551 ...`, then
  `./easy_install.sh line "gpu; b3d tests=quad_alpha,quad_texscreen,quad_tex,mix profiles=GPU,T16; send"`
  (GPU+T16 against GPU: `quad_alpha`/`quad_texscreen` are the cut-out sheets); photo of
  trees/fences in Overbit with `gpu3d_tex16=1` (holes as with 0).
- A played Overbit on the GPU at 640x360, 720p and 1080p (session report): fps, `GPU 3D` ms.
- 2D on GPU, tex16, WC, `g` test steps 15-16, `menu_scale=3`, each from all-off.
- Cost of a pixel (`P` lines, `docs/BENCH3D.md` *The reports*): `./easy_install.sh line "gpu;
  b3d tests=quad_flat,quad_tex,bilinear,gpu2d profiles=ARM,GPU,FS2,2D,VSF; send"`.
