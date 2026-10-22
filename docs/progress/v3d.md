# V3D driver (Pi Zero W, ARM1176 only)

Tracking for bm3D 3.0-7.3. Rest of the graphics stack: [graphics](graphics.md). Numbers: `docs/DRIVERS.md`, `docs/BENCH3D.md` (none here). Triangles at 60 fps, ranked options: [triangle-throughput](../design/triangle-throughput.md).

## Today

- `v3d_start` / `v3d_busy` / `v3d_wait` (`src/gpu/v3d.c`) are async and polled. No IRQ:
  `INTENA` is never written and `src/kernel/irq.h` has no V3D line.
- Bin then render are sequenced in hardware by a semaphore.
- Rule: `gpu3d_queue=1` is the default on the Pi (22 Oct). Without the key the runtime and the Settings row use 1. `gpu3d_queue=0` is the off switch and the fallback. The queue works only where the boot probe passes (`gpu3d_queue_ok`). The Mali (RGB30) has no queue. The check of Overbit with queue 0 and 1 is not measured (graphics data comes only from benchmarks).
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
- `stat(17)`: ms of audio interrupt time in the last frame. `stat(18)`: ms the ARM waited for the GPU
  in the last frame (0 on the ARM, RGB30 and QEMU). Both: `gpu_frame_account` in `runtime.c`.
  Measurements only (QEMU test: `-k stat_perf`, range only). The overlay already shows both as its audio and wait bars.
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
  low or top bits) and keeps the first that gives every pixel within a step of the type's
  bits (`cut16_near`) and every hole as the cleared page (status `alpha
  textures RGBA5551 RGBA4444`, `gpu3d_tex16()` 2, options line `tex16=2`, `stats.cut16`).
  RGBA4444 (type 2) is probed the same way but unused: no sheet has alpha between 0 and 1.
  `make test-gpu3d`: same holes, red and blue the same, green a step at most
  (`EMU_T16_ALPHA=1` alpha on top: learned; `=2`: off; `EMU_T16_LOSSY=1` widens with half a
  step, as the Pi gives some pixels a step off: still learned).
  Pi (report 20261012-171826): type 3 works, order 0 (RGB565's, alpha low) is the right one
  (5551 1845/2048 exact, 4444 1561/2048, other orders under 45%); the exact check failed it.
  Next report: status `alpha textures RGBA5551 RGBA4444` expected.
- `gpu3d_etc1=1` (bm3d 7.3, off; Settings *3D textures: ..., ETC1* also sets `gpu3d_tex16`;
  3D Bench GPU+ETC1): opaque sheets with sides multiples of the ETC1 tile (64x128, or 128x64
  for 4x2 utiles) as ETC1 (type 8), encoded in `tex_get` (`etc1_sheet`, encoder
  `src/gpu/etc1.c`, unit test `make test-etc1`); else the 16-bit / 32-bit chain as before.
  Layout: `etc1_block` (tiles and utile slots from RGB565's `t16_inner`: `etc1_slots`),
  `etc1_put` (byte order). `probe_etc1` draws a 128x256 texture of varied blocks for 8
  candidates (utiles 2x4 then 4x2; bytes BE, words LE, LE, halves swapped) and keeps the first
  whose sampled texels come back (status `ETC1 textures yes (utiles 2x4, bytes BE)`,
  `gpu3d_etc1()`, options line `etc1=1`, `stats.etc1`). Each candidate is also checked
  against the 8 maps of a block's texels (`etc1_at`, `e1_map`: columns/rows reversed,
  transposed); `etc1_sheet` moves each block's texels by the map learned before encoding
  (status adds e.g. `, rows reversed`). Emulator: `EMU_ETC1=0` (2x4, BE), `=1` (4x2, LE: a
  later candidate), `=2` (off), `=3` (2x4, BE, rows reversed as the Pi). `make test-gpu3d`:
  same silhouette, 37 dB PSNR against RGB565, worst channel 9 (also with `=3`).
- Pi (v0.2.5-208): `alpha textures RGBA5551 RGBA4444` (the probe compares within a step of the
  type's bits; the exact compare failed) and `ETC1 textures yes` (type 8 = ETC1, 2x4 utiles,
  bytes BE, each block's rows reversed: `e1_map`). 3D Bench GPU / GPU+T16 / GPU+ETC1 are equal
  within 1-6% (quad_tex 2.20 / 2.35 / 2.19 ns/px, spheres_tex 157 / 158 / 159 fps, heroes_tex
  6.5 all): small sheets sit in the TMU cache, so fewer bytes buy nothing there.
- A failed `probe_cut16` / `probe_etc1` logs to the probes' line (report) `params0`, each
  candidate's right pixels, first 4 texels wanted/got and (5551/4444) an opaque-shader control;
  `etc1_diagnose` adds flat-block, block-map and one-block lines. Tests: `EMU_T16_ALPHA=2`,
  `EMU_ETC1=2`.

- Pi Zero W, 3D Bench, 21 Oct (bm3D 7.x hardware check): score 3412 (GPU 953, ARM 166). Spheres:
  ARM 70 fps, GPU+VS 809, GPU+VS+A8 840 (+3.8% over GPU+VS). Draw queue: VS 573 fps, VS+queue
  643 (+12%). Compressed textures: T16 161.6 fps and ETC1 160.6 fps, no gain, so they stay off.
  Step 16 of test `g`: identical pixels on the ARM and the GPU.
- Pi Zero W, 22 Oct (reports branch): GPU test 16/16 ok (3311 triangles: ARM 28.2 ms, GPU 9.7 ms). 3D Bench score 3382 (GPU bm3d 2.1 = 950, ARM = 169).
- Pi Zero W, 22 Oct, hero path (3D Bench `herotris`, one profile for each run; kernel built from `bm-core` 38799d49 plus the bench; 640x360; ARM 1000 MHz; V3D 250 MHz, measured 300; reports `20261022-171354` to `-171450` on the `reports` branch). A set is one hero of 1536 triangles, its shadow and a map of 5000 triangles. Benchmark numbers only, not games.

  | profile | sets at 60 fps | hero tris at 60 fps | drawn at 60 fps | ms | gpu_ms | vs mesh |
  |---|---:|---:|---:|---:|---:|---|
  | ARM | 0.41 | 630 | 2393 | 40.7 | - | NA |
  | GPU | 0.56 | 859 | 3265 | 29.8 | 2.1 | NA |
  | GPU+VS | 3.85 | 5907 | 28961 | 14.9 | 3.9 | ACCEPTED |
  | GPU+VS+Q | 6.74 | 10351 | 50746 | 13.0 | 7.1 | ACCEPTED |
  | GPU+VS+A8 | 4.03 | 6182 | 30310 | 16.6 | 5.1 | ACCEPTED |
  | GPU+VS+A8+Q | 6.75 | 10364 | 50809 | 13.2 | 7.1 | ACCEPTED |
  | GPU+VS+Q2 | 6.74 | 10353 | 50755 | 13.3 | 7.1 | ACCEPTED |

  - The vertex shader places the 1536 triangles of the hero (`probe_gltris` 1536, `probe_meshes` 1): GPU+VS gives x6.9 over GPU and x9.4 over ARM.
  - The queue gives x1.75 over GPU+VS (3.85 -> 6.74 sets). The design range was +10% to +45%, so the queue does better than the estimate on this scene. At GPU+VS the frame is ARM-bound (`gpu_ms` 3.9 of 14.9 ms). With the queue the GPU works 7.1 of 13.0 ms.
  - A8 gives +4.7% without the queue (3.85 -> 4.03) and 0% with it (6.74 -> 6.75).
  - The frame-later model (Q2, `later_drawn60` 51222.8 against 50755) shows no gain over Q: +0.9% of triangles drawn, and the same sets (6.74).
  - Job times at the last step: `bin_us`/`rnd_us` GPU+VS 947/2928, GPU+VS+A8 1254/3811; the queue profiles print 0 and about 7070 (the bin time is not split out under the queue).
  - Boot probe: the status line of every GPU report says `cel light yes`.
  - Open issue: the Pi froze once on `b3d tests=herotris` with all profiles in one line (see `docs/NEXT.md`, Open issues).
- Pi Zero W, 22 Oct, cel cost (3D Bench `cel`: n heroes, textured, 1536 triangles each, lit by the sun; reports `20261022-171533` ARM, `-171542` GPU+VS). The bench ramp stops at 50 heroes (76800 triangles) for lit and cel.

  | profile | lit ms | cel ms | cel over lit | outline ms (flag 512) | outline over lit | gpu_ms lit / cel / outline |
  |---|---:|---:|---:|---:|---:|---|
  | GPU+VS (n = 50) | 11.0 | 12.1 | +10% | 14.4 | +31% | 6.9 / 6.9 / 8.2 |
  | ARM (n60) | 14.5 (n60 3.4) | 14.6 (n60 3.3) | +1% | 12.5 (n60 2.5) | - | - |

  - Cel is on the ARM path already, so the ARM rows are equal: cel does not cost more there. Dither off (`nd`) is the same as dither on within 0.1 ms on both profiles.
  - Outline +31% is under the +60% gate, so the distance limit of the outline is not needed. At 1 to 4 heroes the outline over lit is +5% to +23% on GPU+VS.
  - The outline test draws the hull as a second mesh: 116736 triangles at n = 42.4 (the ramp reached 42.4 heroes at 60 fps, `n60`).
- Decision (user, 22 Oct): `gpu3d_attr` stays OFF by default. A8 gains only 3.8% on the
  spheres and it is a compact-attribute path with its own probe. Turn it ON only for the
  heavy-mesh games where the Pi reports above (heroes, match) show a gain.
- Mali: the RGB30 triangle fails (`JS_STATUS` 58); the variant probe is ready, the next Dev > GPU test run is in [graphics-mali](graphics-mali.md) `Open work`.
- Sprite stacking (`stack()`, stack zones, `zstack()`) is done. It runs on the CPU only.
Decisions from the Pi reports, `scaling_kernel` and what to measure on the Pi: [v3d-notes](v3d-notes.md). First QPU run (key `gpu3d_qpu`): same file, "First QPU run on the Pi".

## To do, in order

1. Pixels at 1080p (QPU scaling): the cheap soft enlargement is scaler kernel 6 (config only, no GPU
   cost; below). A QPU bicubic / FSR-style pass stays open, only if 6 is not enough. Cost from
   the Pi 3D Bench: a full-screen 1080p pass with one bilinear read is ~3.5-5 ms of GPU
   (textured fill 1.65 ns/px nearest, 2.3 ns/px bilinear, plus ~0.3 ms fixed); bicubic/FSR
   more, not measured. 2D on GPU: 9.5 ns/px (19.6 ms at 1080p).
2. Impostors: Pi runs at `r3d_impostor` 16, 32 with the new threshold (8 = 2.7 render px
   at 640x360, no effect: session at 8 gave 57.7 fps, GPU 7.8 ms a frame, 8588 tris), photo of
   far heroes; then the default for crowds (billboard impostors).
2a. Stress test at double the load of 60 fps (22 Oct, Pi Zero W, one run, report `20261022-191158_bench3d_pi-zero-w_dev.txt`; `docs/BENCH3D.md`, *stress*): `n60` = 24.8 quarter sets. At 1x (24 quarters, 48 432 triangles in): 13.3 ms a frame, worst 13.5 ms, 0 drops, ARM 13.1 ms, GPU 6.8 ms, pool 122 KiB, no overflow. At 2x (48 quarters, 96 864 triangles in): 25.2 ms, worst 25.3 ms, all 116 frames over 16.7 ms (about 40 fps), ARM 25.2 ms (ARM-bound), GPU 16.1 ms, 2 jobs, pool 142 KiB, `ovf_kb` 0, `v3d_failed` 0. The Pi did not hang. Tests: `make test-b3d` (PC emulator), `test_monitor_line` (QEMU, no `stress` rows). Open: a second run to confirm the numbers.
3. Chain two jobs in hardware (CT0/CT1) only if a measurement shows the GPU idle between jobs. Read `bin_us`, `rnd_us` first (`b3d tests=herotris`; run on 22 Oct: with the queue the frame is ARM-bound, `arm_ms` 12.6 against `gpu_ms` 7.1, so the GPU is not idle and chaining is not the next step).
4. Remaining work of bm3D 5.0-7.3. 16-bit and ETC1 textures: the Pi shows no gain (21 Oct), so
   they stay off. Reopen them only if a played Overbit at 1080p shows a gain. Attributes:
   `vs_tex_rgb` light as bytes (8 -> 6 words), `vs_lit_tex`/`vs_lit_tex2` flags
   and uv as halves (`.16a/.16b`), normals as bytes, then 16-bit positions (scaled per mesh,
   a separate x y z stream for the coordinate shader).
5. **QPUs as a coprocessor: programs on the QPUs**:
   The 12 QPUs run SIMD operations on 16 floats at a time: parallel computation on large masses of data, when 3D does not saturate them (2D games, menus) or by reserving some QPUs in 3D games.
   - **Step 1 (Driver)**: Done on the emulator. Not verified on hardware. `v3d_qpu_run` (`src/gpu/v3d.c`) starts user programs through the SRQ registers and waits with a timeout. `src/gpu/qpu.c` adds the queue, the probe at start and the ARM fallback. Tests: `make test-v3d`, `make test-qpurun`, `make test-qpu`, `make test-qpucheck`.
   - Done on the emulator: key `gpu3d_qpu` (default 0) adds a GPU test step and a `qpu row` line in the 3D Bench report (`src/gpu/qpucheck.c`; PASS/FAIL/SKIP). Key off: no `qpu_init` call. The row is a report line, not a profile column (a profile changes the summary for every user).
   - Pi Zero W result (Dev > GPU test with `gpu3d_qpu=1`, 22 Oct, report `20261022-182624_gpu_pi-zero-w_dev.txt` on the `reports` branch): `QPU PASS`. Particles PASS (1024 particles: ARM 158 us, QPU 47 us). Fade PASS (1024 pixels: ARM 79 us, QPU 36 us). Steps 1 to 15 of the GPU test are ok. So the driver, the queue, the SRQ launch, the VPM share, the TMU lookup and the DMA store work on the real V3D for these two programs. The 3D Bench `qpu row` line, the time of 12 QPUs and a full-screen fade on QPU are not measured.
  - Done (step 4, host tests `make test-v3d`): after a failed QPU run (the mailbox QPU off/on may power-cycle the V3D), `v3d_qpu_run` waits 1 ms and runs the `v3d_init` register block again (`regs_init`: VPMBASE, INTDIS, INTCTL, CT0CS, CT1CS, BFC, RFC). `v3d_qpu_run` has a lock: a second caller (another core or an interrupt) gets -2 with the status `another QPU run` and nothing starts. The kernel's fibers are cooperative and the function never yields, so fibers cannot overlap in it. The callers already treat -2 as "use the ARM now".
  - Open: `prop_query` (`mbox_call`) has no timeout. A mailbox that does not answer after the QPU off/on would block the kernel. Not seen on the Pi; the failed-run path did not run there.
   - **Step 2 (Particles on GPU)**: half done. The update is done (`qp_particles` in `tools/qpuprogs.py`, `qpu_particles` with its ARM version `qpu_particles_arm`; blocks of 16 particles with x, y, vx, vy, life; on the emulator the QPU result equals the ARM result to the bit). Decision (user, 22 Oct): `lib.particles` on the QPU stays parked; reopen it as a C pool only if the Pi shows the QPU faster than the ARM. Pi data of 22 Oct: QPU 47 us against ARM 158 us for 1024 particles (the step only, without drawing). The decision to reopen is the user's. Not planned: the Lua `particles()` call and the `lib.particles` module in bmlib (they keep their Lua pool today), and the drawing on the GPU (the ARM draws).
   - **Step 3 (Full-screen 2D effects)**: half done. The darkness levels of Yharnam are done (`qp_fade`, `qpu_fade`, `qpu_fade_apply` for `g16_fade_t`: the same picture as `g16_fade_apply`, tested at 640x360 and with a tail of pixels). The runtime call is done: `dark_end()` calls `bm_fade_accel` = `qpu_fade_hook` only with `gpu3d_qpu=1` (set in `src/kernel/main.c`; key off = `g16_fade_apply`, no call). The hook runs `qpu_init` on the first call. After a failed `qpu_init`, a failed run or a timeout it uses the ARM for good. Test: `make test-qpurun` (mode 3: the V3D stops answering). The 3D Bench measures it: the `fade` test has a `QPU` profile (opt-in, `tests=fade profiles=ARM,QPU`; `fade_qpu` in `b3d_platform_t`; the Pi sets it to `qpu_fade_hook` only with `gpu3d_qpu=1`) and the report line `qpufade` gives the path that ran (`qpu`, `arm`, `mixed`), the frames of each path, and `n60`/`ms` of both profiles (`docs/BENCH3D.md`, *The fade on the QPU*). Tests: `make test-b3d` (a stand-in hook gives `path=qpu`), `test_monitor_line` (no key gives `path=arm`). Measured on the Pi Zero W (22 Oct, one run, report `20261022-184933_bench3d_pi-zero-w_dev.txt`, `fade` with `ARM,QPU`): full-screen fade 640x360 with 1 lamp 20.4 ms on ARM (misses 60 fps), 9.3 ms on the QPU (2.2 times faster; 14.3 lamps at 60 fps); `qpufade path=qpu frames_qpu=98 frames_arm=0`. The `qpu row` of the same run: particles PASS (1024: ARM 225 us, QPU 50 us), fade PASS (1024 px: ARM 118 us, QPU 54 us). Open: the decision to make `gpu3d_qpu` the default for `dark_end` (`docs/NEXT.md`), other fades, blurs, CRT and scanline filters.
   - **Step 4 (Block audio effects)**: FDN reverb, echo and filters processed in blocks on the QPUs (also for nano8).
   - **Step 5 (Ray tracing)**: batched `hit3d` and lines of sight for AI bots.
   - *Toolkit and hardware registers needed (from research 2026-10-12)*:
     - User launch registers: `SRQPC/SRQUA/SRQUL/SRQCS` (0x430..0x43C): write QPU UL, UA, then PC; termination at the count (bits 23:16 of `SRQCS`); requires `VPMBASE > 0` (today 0 in `v3d.c`); reset from mailbox `0x30012` for stuck QPUs; reserve `SQRSV0/1`.
     - Branches in `qpuasm.py` and in the emulator (3 delay slots): per-pixel predicates, branches only on uniform data; VPM DMA with mutex, TMU sampling via memory (`t0s` + `ldtmu0`), semaphores `sacq/srel`.
     - `qpuasm.py` checks: handling of `ra39/rb39` reads, small immediate constraints with `ldtmu`/`thrsw`. Hardware counters `PCTR*` (0x670) for fine profiling.
   - *Completion criterion*: particles and Yharnam's darkness levels handled entirely by the GPU with measurements in the 3D Bench and an identical software fallback in QEMU (`gpu3d=0`).
6. **Dynamic impostor adaptation for crowds and heroes**:
   - Skinned models simplified to billboard quads with `r3d_impostor` when far from the camera; Overbit/Yharnam quality governor to enable `gpu3d(nil, nil, nil, nil, 8)` at low levels.
8. Sprite stacking on the Pi: measured on the Pi Zero W (22 Oct, 128 stacks of 8 slices): 18270 us a frame, about 58k slices/s at most, over the 16600 us of 60 fps. Run again: Dev tab **Stack bench**, or `:stack` (16, 64, 128 stacks of 8 and 16 slices; read `us/frame`, `slices/s`).
