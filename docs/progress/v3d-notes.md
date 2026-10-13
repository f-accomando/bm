# V3D: decisions, scaling_kernel and Pi measurements

Split from [v3d](v3d.md); the to-do list stays there.

## Decisions (from Pi reports, 2026-10-12)

- Interrupts, triple buffering, `v3d_async.c/.h` of the v3d-async-proposal: skip.
- Whole-cache clean (about 5 kcycles a job in the bench rows) and binner overflow (0 KiB, 0 jobs
  in every row) cost nothing measurable: range clean and dynamic overflow stay off the list.
- `gpu3d_later=1` = `queue=2` in the mean (50 vs 51 fps at 640x360), fewer late frames (38% vs
  50%): keep it an option, off.
- GPU 640x360 EXTREME: GPU job 4.2 ms of a 19.6 ms frame, the ARM sets the pace. At 1080p
  a played match is fill-bound (GPU 20.4 ms for 6105 tris; 720p 10.1 ms): fewer pixels, not
  fewer triangles, is the lever.

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
- 16-bit and ETC1 sheets (bm3d 7.2, 7.3): the bench is done (above). Open: photo of
  trees/fences in Overbit with `gpu3d_tex16=1` (holes as with 0) and of a textured scene with
  `gpu3d_etc1=1` (colours near the 16-bit ones, no block garbage), plus the played Overbit
  comparison of To do 4.
- A played Overbit on the GPU at 640x360, 720p and 1080p (session report): fps, `GPU 3D` ms.
- 2D on GPU, tex16, WC, `g` test steps 15-16, `menu_scale=3`, each from all-off.
- Cost of a pixel (`P` lines, `docs/BENCH3D.md` *The reports*): `./easy_install.sh line "gpu;
  b3d tests=quad_flat,quad_tex,bilinear,gpu2d profiles=ARM,GPU,FS2,2D,VSF; send"`.
