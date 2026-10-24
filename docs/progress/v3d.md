# V3D driver (Pi Zero W, ARM1176 only)

Tracking for bm3D 3.0-7.6. Rest of the graphics stack: [graphics](graphics.md). Numbers: `docs/DRIVERS.md`, `docs/BENCH3D.md` (none here). Triangles at 60 fps, ranked options: [triangle-throughput](../design/triangle-throughput.md).

## Today

- `v3d_start` / `v3d_busy` / `v3d_wait` (`src/gpu/v3d.c`) are async and polled. No IRQ:
  `INTENA` is never written and `src/kernel/irq.h` has no V3D line.
- Bin then render are sequenced in hardware by a semaphore.
- Rule: `gpu3d_queue=1` is the default on the Pi (22 Oct). Without the key the runtime uses 1. `gpu3d_queue=0` is the off switch and the fallback. The queue works only where the boot probe passes (`gpu3d_queue_ok`). The Mali (RGB30) has no queue. The check of Overbit with queue 0 and 1 is not measured (graphics data comes only from benchmarks).
- `queue=2` = two job memory blocks (`JOB_BLOCK` / `JOB2_BLOCK`, `gpu3d.c`): the ARM fills
  N+1 while the GPU renders N. `run_async` / `gpu3d_submit` still call `job_wait()` before
  `v3d_start`, so one GPU job runs at a time (`v3d_start` rejects a second).
- `gpu3d_later=1` (with `queue=2`, off): the frame is held one frame (`held` in `runtime.c`).
  Host test: `make test-queue2d`.
- Cache: `dcache_clean_invalidate_all` 4 times per job. Binner: static 4 MB tile list + 2 MB
  overflow (`BPOA` / `BPOS`); `OUTOMEM` is not handled (200 ms timeout, GPU off, ARM takes over).
- Vertex shader is done (`gpu3d_vs=2` default).
- `stat(16)`: the GPU's job time of the last frame (0 on the ARM). Read with the queue on, it
  includes the wait before the ARM looks: trust it only without the queue.
- `stat(17)`: ms of audio interrupt time in the last frame. `stat(18)`: ms the ARM waited for the GPU
  in the last frame (0 on the ARM, RGB30 and QEMU). Both: `gpu_frame_account` in `runtime.c`.
  Measurements only (QEMU test: `-k stat_perf`, range only). The overlay already shows both as its audio and wait bars.
- `r3d_impostor=N` (off; 2..64 px of a 1080-line screen, threshold N*h/1080: `imp_threshold`
  in `r3d.c`; `gpu3d()` 5th; skinned meshes by their pose box). Host test: `make test-impostor`.
- Rule: `gpu3d_attr` is off by default (Pi, 23 Oct: `herotris` 54 056 plain, 54 013 A8, no gain, the frame is ARM-bound). Without the key the runtime uses floats. `gpu3d_attr=1` turns the compact attributes on. The boot `probe_attr` keeps the compact path only if its pixels match the floats; if it fails, the floats draw. The bench profiles set the key themselves.
- Rule: draw merging, `gpu3d_merge` (bm3d 7.6, hidden key, no Settings row, on by default; `gpu3d_merge=0` is the off switch). A run merges only from `MG_MIN_PIECES` 32 pieces with at most `MG_MEAN_FACES` 150 faces a piece on average (Pi, 23 Oct: 100 pieces of 50 faces +24%, 10 of 500 -26%); any other run is drawn piece by piece. Keep the threshold in `mg_flush`. It lives in `r3d.c` ("draw merging") and needs `r3d_backend_t.merge`, so only the V3D has it (the Mali does not). Rules of the code:
  - A draw is held if its mesh has baked light (or the call has flag 2), no skeleton, no other flag, a positive scale, `r3d_t.impostor` is 0 and `textures3d` is on. Held draws of the same sheet, flags and light kind form a run (at most 256 draws, 16000 faces). A piece out of view is dropped when it is held, as `draw_flags` drops it.
  - The run is drawn before anything else changes the scene or reaches the job: every `r3d_*` call that changes the scene or draws, and the other entry points of `gpu3d.c` (`GPU_PRE`: `gpu3d_flush`, `gpu3d_submit`, `gpu3d_pending`, `gpu3d_rect2d`, `blit2d`, `text2d`, `sync`, `drop`, `page`, the setters). Add `MG_SYNC()` to a new `r3d_*` function and `GPU_PRE()` to a new public `gpu3d_*` function that records or changes the job.
  - The second time the same run comes (same meshes and versions, same sheet and version, same places, turns and scales, same order) its pieces are put in one mesh in world axes (origin: the first piece), once; the run is then one draw of it. `MG_ENTRIES` 48 runs are remembered, 64000 faces of merged meshes are kept. A run seen once or changed is drawn piece by piece. If the backend does not take the merged mesh, the run is marked bad and drawn piece by piece.
  - Pixels: `make test-gpu3d` (`draw merging`) draws 64 pieces merged and piece by piece: 2 of 230400 pixels differ (the vertex sums are made in another order). Host counters: 64 meshes a frame become 2; 100 small pieces merge, 10 big ones (512 faces) do not.
  - Limits: a sheet edited between a held draw and the next flush shows its new texels in the held draws. A mesh freed or changed (`r3d_mesh_free`, `r3d_mesh_normals`) draws the held pieces first.
  - 3D Bench: `ht_p100`, `ht_p10`, `ht_p1` (the map in 100, 10 or 1 pieces, 5000 triangles) and `ht_p100m`, `ht_p10m` (merging on). Pi (23 Oct, before the threshold): `ht_p100m` +24%, `ht_p10m` -26% with the queue; `ht_p10m` re-measure pending ([BENCH3D](../BENCH3D.md) *herotris with the map in pieces*).
Detail of `gpu3d_attr` (compact attributes), `gpu3d_tex16` (RGB565 and RGBA5551 sheets) and `gpu3d_etc1` (ETC1 sheets), with probes and Pi results: [v3d-formats](v3d-formats.md).
Pi Zero W reports of 21-23 Oct (3D Bench, hero path, cel cost) and the user decisions on `gpu3d_attr`: [v3d-pi-measurements](v3d-pi-measurements.md).
- Mali: the RGB30 triangle fails (`JS_STATUS` 58); the variant probe is ready but has no entry point since the GPU test page was removed (24 Oct 2026); see [graphics-mali](graphics-mali.md) `Open work`.
- Sprite stacking (`stack()`, stack zones, `zstack()`) is done. It runs on the CPU only.
Decisions from the Pi reports, `scaling_kernel` and what to measure on the Pi: [v3d-notes](v3d-notes.md). First QPU run (key `gpu3d_qpu`): same file, "First QPU run on the Pi".

## Graphics keys (SYS-9 part 1)

Settings shows five graphics rows; every performance flag is a hidden key. Keys, defaults and Pi numbers: [v3d-flags](v3d-flags.md). Manual: `api-3d`, *The graphics keys of the console*.

## To do, in order

1. Pixels at 1080p (QPU scaling): the cheap soft enlargement is scaler kernel 6 (config only, no GPU
   cost; below). A QPU bicubic / FSR-style pass stays open, only if 6 is not enough. Cost from
   the Pi 3D Bench: a full-screen 1080p pass with one bilinear read is ~3.5-5 ms of GPU
   (textured fill 1.65 ns/px nearest, 2.3 ns/px bilinear, plus ~0.3 ms fixed); bicubic/FSR
   more, not measured. 2D on GPU: 9.5 ns/px (19.6 ms at 1080p).
2. Impostors: Pi runs at `r3d_impostor` 16, 32 with the new threshold (8 = 2.7 render px
   at 640x360, no effect: session at 8 gave 57.7 fps, GPU 7.8 ms a frame, 8588 tris), photo of
   far heroes; then the default for crowds (billboard impostors).
2a. Stress test at double the load of 60 fps (22 Oct, Pi Zero W, one run, report `20261022-191158_bench3d_pi-zero-w_dev.txt`; `docs/BENCH3D.md`, *stress*): `n60` = 24.8 quarter sets. At 1x (24 quarters, 48 432 triangles in): 13.3 ms a frame, worst 13.5 ms, 0 drops, ARM 13.1 ms, GPU 6.8 ms, pool 122 KB, no overflow. At 2x (48 quarters, 96 864 triangles in): 25.2 ms, worst 25.3 ms, all 116 frames over 16.7 ms (about 40 fps), ARM 25.2 ms (ARM-bound), GPU 16.1 ms, 2 jobs, pool 142 KB, `ovf_kb` 0, `v3d_failed` 0. The Pi did not hang. Tests: `make test-b3d` (PC emulator), `test_monitor_line` (QEMU, no `stress` rows). Open: a second run to confirm the numbers.
3. Chain two jobs in hardware (CT0/CT1) only if a measurement shows the GPU idle between jobs. Read `bin_us`, `rnd_us` first (`b3d tests=herotris`; run on 22 Oct: with the queue the frame is ARM-bound, `arm_ms` 12.6 against `gpu_ms` 7.1, so the GPU is not idle and chaining is not the next step).
4. Remaining work of bm3D 5.0-7.3. 16-bit and ETC1 textures: the Pi shows no gain (21 Oct), so
   they stay off. Reopen them only if a played Overbit at 1080p shows a gain. Attributes:
   `vs_tex_rgb` light as bytes (8 -> 6 words), `vs_lit_tex`/`vs_lit_tex2` flags
   and uv as halves (`.16a/.16b`), normals as bytes, then 16-bit positions (scaled per mesh,
   a separate x y z stream for the coordinate shader).
5. **QPUs as a coprocessor: programs on the QPUs**:
   The 12 QPUs run SIMD operations on 16 floats at a time: parallel computation on large masses of data, when 3D does not saturate them (2D games, menus) or by reserving some QPUs in 3D games.
   - **Step 1 (Driver)**: Done on the emulator. Not verified on hardware. `v3d_qpu_run` (`src/gpu/v3d.c`) starts user programs through the SRQ registers and waits with a timeout. `src/gpu/qpu.c` adds the queue, the probe at start and the ARM fallback. Tests: `make test-v3d`, `make test-qpurun`, `make test-qpu`, `make test-qpucheck`.
   - Done on the emulator: key `gpu3d_qpu` (default 0) adds a `qpu row` line in the 3D Bench report (it added a GPU test step too; the GPU test was removed on 24 Oct 2026) (`src/gpu/qpucheck.c`; PASS/FAIL/SKIP). Key off: no `qpu_init` call. The row is a report line, not a profile column (a profile changes the summary for every user).
   - Pi Zero W result (the GPU test, removed on 24 Oct 2026, with `gpu3d_qpu=1`, 22 Oct, report `20261022-182624_gpu_pi-zero-w_dev.txt` on the `reports` branch): `QPU PASS`. Particles PASS (1024 particles: ARM 158 us, QPU 47 us). Fade PASS (1024 pixels: ARM 79 us, QPU 36 us). Steps 1 to 15 of the GPU test are ok. So the driver, the queue, the SRQ launch, the VPM share, the TMU lookup and the DMA store work on the real V3D for these two programs. The 3D Bench `qpu row` line, the time of 12 QPUs and a full-screen fade on QPU are not measured.
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
8. Sprite stacking on the Pi: measured on the Pi Zero W (22 Oct, 128 stacks of 8 slices): 18270 us a frame, about 58k slices/s at most, over the 16600 us of 60 fps. The Stack bench that took it was removed on 24 Oct 2026; run again with the 3D Bench scene `stack` (`b3d tests=stack`).
