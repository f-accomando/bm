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

## Pi Zero W hardware runs, 19 Oct 2026 (bm3D 7.3, kernel `dev`, 1000 MHz ARM, 42-46 C, throttled 0)

Raw reports: branch `reports`, `reports/unknown/`. Source commits: 61dc9f73, 935b738a (queue), c180b564 (compare).

- Test `queue`, 4 runs (n60 / ms): GPU+VS 584.9-586.4 / 14.5-14.6; GPU+VS+Q 651.4-652.4 / 11.1; GPU+VS+Q2
  651.6-652.7 / 11.1; GPU+VS+FS2 584.8-585.4 / 14.6. Spread inside a profile 0.15-0.26%. Q gives +11.2 to
  +11.5% n60 and 3.4 ms less a frame (-23.6%) at 47232 triangles with 4 million ARM instructions in the frame.
  GPU and GPU+Q (no VS) are noisier (3% and 2%). Not measured: Overbit with `gpu3d_queue` 0 and 1.
- Test `g` step 16: ARM 2D 11260 us in 2 jobs, GPU 9265 us in 1 job (37 quads), same pixels. Steps 10, 14, 15
  differ by 0.1%, 0.0%, 0.1%.
- Full 3D Bench, 3 runs, 28 tests x 16 profiles, no pixel failure. Noise: n60 steps (split 5.0-5.9, big_logic
  FS2 3-4, gpu2d 1854-2172); the middle-frame ms stay within 1.5 ms.
- GPU+ETC1 has no vertex shader, so it is far below GPU+VS (heroes_tex n60 6.5 against 73.0): compare it with
  GPU+T16. With the shader (commit 72710c09, profiles VS and VSE, 2 runs) ETC1 equals the plain path: n60 heroes_tex
  74.6 / 74.6, match 32.2 / 32.1 and 33.8 / 33.3, mix 26.1 / 26.1 and 26.3 / 25.0 (ratio 1.00-1.05).
- Fade (`g16_fade_apply`, cart `FADE.BM`): 16.55 ms a fade, 1542 fades. The CRC sample read 0 (`pget`), so the
  fade result is not proven. No QPU comparison yet.

## Waiting for the user: first QPU run on the Pi

The QPU check is behind the config key `gpu3d_qpu` (default off). With the key off, nothing changes: no `qpu_init` call and no new work. Nothing here is verified on hardware.

1. Edit `bm/config.txt` on the SD card. Add the line `gpu3d_qpu=1`.
2. Put the card in the Pi Zero W. Do not use a serial connection.
3. Open Dev > GPU test (monitor key `g`). Read the new step `QPU programs (gpu3d_qpu=1)` after the speed steps.
   - `QPU PASS` = both programs gave the ARM result.
   - `QPU SKIP` = `qpu_init` failed, the probe turned the programs off, or a run timed out. The reason follows the line.
   - `QPU FAIL` = a QPU result differs from the ARM result. The line names the first word or pixel.
4. Open Dev > 3D Bench and run it. The log has one `QPU ...` line after the ramps. The saved report has one `qpu row (gpu3d_qpu=1)` line before the score.
5. Copy back: a photo of the GPU test lines (or the console text) and the new report in `bm/bench` (the console also sends it to the `reports` branch). The times are the numbers to read: ARM us and QPU us for 1024 particles and for 1024 pixels of fade.
6. Play Yharnam (it lights by levels) with `gpu3d_qpu=1`. The darkness (`dark_end()`) now runs on the QPUs. Compare the look with `gpu3d_qpu=0`: it must be the same picture. Read the fps in the performance overlay (F11 or the dev kit) for both settings.
7. If the picture is wrong or the game stalls, note it and set `gpu3d_qpu=0`. After a failed QPU run the game goes on with the ARM fade by itself.
8. Set `gpu3d_qpu=0` again (or delete the line) after the run.

If the Pi freezes, the last line on the screen shows the step that stopped. Each QPU run has a timeout of 50 ms, so a freeze means a hang outside the QPU code (for example the mailbox call in `qpu_init`).
