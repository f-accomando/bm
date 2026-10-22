# V3D driver (Pi Zero W, ARM1176 only)

Tracking for bm3D 3.0-7.3. Rest of the graphics stack: [graphics](graphics.md). Numbers: `docs/DRIVERS.md`, `docs/BENCH3D.md` (none here).

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
- Recommendation (the user decides): keep `gpu3d_attr` default OFF. A8 gains only 3.8% on the
  spheres and it is a compact-attribute path with its own probe. Turn it ON only for the
  heavy-mesh games where the Pi reports above (heroes, match) show a gain.
- Mali: the RGB30 triangle fails (`JS_STATUS` 58). It is parked. The user will align Mali later.
- Sprite stacking (`stack()`, stack zones, `zstack()`) is done. It runs on the CPU only.
Decisions from the Pi reports, `scaling_kernel` and what to measure on the Pi: [v3d-notes](v3d-notes.md). First QPU run (key `gpu3d_qpu`): same file, "Waiting for the user".

## To do, in order

1. Pixels at 1080p (QPU scaling): the cheap soft enlargement is scaler kernel 6 (config only, no GPU
   cost; below). A QPU bicubic / FSR-style pass stays open, only if 6 is not enough. Cost from
   the Pi 3D Bench: a full-screen 1080p pass with one bilinear read is ~3.5-5 ms of GPU
   (textured fill 1.65 ns/px nearest, 2.3 ns/px bilinear, plus ~0.3 ms fixed); bicubic/FSR
   more, not measured. 2D on GPU: 9.5 ns/px (19.6 ms at 1080p).
2. Impostors: Pi runs at `r3d_impostor` 16, 32 with the new threshold (8 = 2.7 render px
   at 640x360, no effect: session at 8 gave 57.7 fps, GPU 7.8 ms a frame, 8588 tris), photo of
   far heroes; then the default for crowds (billboard impostors).
3. Chain two jobs in hardware (CT0/CT1) only if a measurement shows the GPU idle between jobs.
4. Remaining work of bm3D 5.0-7.3. 16-bit and ETC1 textures: the Pi shows no gain (21 Oct), so
   they stay off. Reopen them only if a played Overbit at 1080p shows a gain. Attributes:
   `vs_tex_rgb` light as bytes (8 -> 6 words), `vs_lit_tex`/`vs_lit_tex2` flags
   and uv as halves (`.16a/.16b`), normals as bytes, then 16-bit positions (scaled per mesh,
   a separate x y z stream for the coordinate shader).
5. **QPUs as a coprocessor: programs on the QPUs**:
   The 12 QPUs run SIMD operations on 16 floats at a time: parallel computation on large masses of data, when 3D does not saturate them (2D games, menus) or by reserving some QPUs in 3D games.
   - **Step 1 (Driver)**: Done on the emulator. Not verified on hardware. `v3d_qpu_run` (`src/gpu/v3d.c`) starts user programs through the SRQ registers and waits with a timeout. `src/gpu/qpu.c` adds the queue, the probe at start and the ARM fallback. Tests: `make test-v3d`, `make test-qpurun`, `make test-qpu`, `make test-qpucheck`.
   - Done on the emulator: key `gpu3d_qpu` (default 0) adds a GPU test step and a `qpu row` line in the 3D Bench report (`src/gpu/qpucheck.c`; PASS/FAIL/SKIP). Key off: no `qpu_init` call. The row is a report line, not a profile column (a profile changes the summary for every user).
   - Open (step 1): the Pi run above. Check on the Pi: SRQCS bits, VPMBASE unit, VPM rows of a user program, TMU raw lookup, DMA store setup words, run time of 12 QPUs. After a failed run the mailbox QPU off/on may power-cycle the V3D, and the `v3d_init` registers (INTDIS, INTCTL, CT0CS, CT1CS, BFC, RFC) are not set again while `ready` stays 1: run that block again before the first caller, and check that `prop_query` has a timeout. `v3d_qpu_run` has no lock: one core only.
   - **Step 2 (Particles on GPU)**: half done. The update is done (`qp_particles` in `tools/qpuprogs.py`, `qpu_particles` with its ARM version `qpu_particles_arm`; blocks of 16 particles with x, y, vx, vy, life; on the emulator the QPU result equals the ARM result to the bit). Open: the Lua `particles()` call and the `lib.particles` module in bmlib (they keep their Lua pool today), and the drawing on the GPU (the ARM draws).
   - **Step 3 (Full-screen 2D effects)**: half done. The darkness levels of Yharnam are done (`qp_fade`, `qpu_fade`, `qpu_fade_apply` for `g16_fade_t`: the same picture as `g16_fade_apply`, tested at 640x360 and with a tail of pixels). The runtime call is done on the emulator (not verified on hardware): `dark_end()` calls `bm_fade_accel` = `qpu_fade_hook` only with `gpu3d_qpu=1` (set in `src/kernel/main.c`; key off = `g16_fade_apply`, no call). The hook runs `qpu_init` on the first call. After a failed `qpu_init`, a failed run or a timeout it uses the ARM for good. Test: `make test-qpurun` (mode 3: the V3D stops answering). Open: the Pi run (time of the Yharnam fade, ARM against QPU), fades, blurs, CRT and scanline filters.
   - **Step 4 (Block audio effects)**: FDN reverb, echo and filters processed in blocks on the QPUs (also for nano8).
   - **Step 5 (Ray tracing)**: batched `hit3d` and lines of sight for AI bots.
   - *Toolkit and hardware registers needed (from research 2026-10-12)*:
     - User launch registers: `SRQPC/SRQUA/SRQUL/SRQCS` (0x430..0x43C): write QPU UL, UA, then PC; termination at the count (bits 23:16 of `SRQCS`); requires `VPMBASE > 0` (today 0 in `v3d.c`); reset from mailbox `0x30012` for stuck QPUs; reserve `SQRSV0/1`.
     - Branches in `qpuasm.py` and in the emulator (3 delay slots): per-pixel predicates, branches only on uniform data; VPM DMA with mutex, TMU sampling via memory (`t0s` + `ldtmu0`), semaphores `sacq/srel`.
     - `qpuasm.py` checks: handling of `ra39/rb39` reads, small immediate constraints with `ldtmu`/`thrsw`. Hardware counters `PCTR*` (0x670) for fine profiling.
   - *Completion criterion*: particles and Yharnam's darkness levels handled entirely by the GPU with measurements in the 3D Bench and an identical software fallback in QEMU (`gpu3d=0`).
6. **Dynamic impostor adaptation for crowds and heroes**:
   - Skinned models simplified to billboard quads with `r3d_impostor` when far from the camera; Overbit/Yharnam quality governor to enable `gpu3d(nil, nil, nil, nil, 8)` at low levels.
8. Sprite stacking on the Pi: the Stack bench exists. Run it on the Pi Zero W and write the numbers here.
   - Run: open the Dev tab and select **Stack bench**, or type `:stack` in the menu. It draws 16, 64 and 128 stacks of 8 and 16 slices. Read `us/frame`, `stacks/s` and `slices/s`. Below 16600 `us/frame` fits 60 fps.
