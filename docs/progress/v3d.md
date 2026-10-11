# V3D driver (Pi Zero W, ARM1176 only)

Tracking for M35, M36, M37, M39. Rest of the graphics stack: [graphics](graphics.md).
Numbers: `docs/DRIVERS.md`, `docs/BENCH3D.md` (none here).

## Today

- `v3d_start` / `v3d_busy` / `v3d_wait` (`src/gpu/v3d.c`) are async and polled. No IRQ:
  `INTENA` is never written and `src/kernel/irq.h` has no V3D line.
- Bin then render are sequenced in hardware by a semaphore.
- `queue=2` = two job memory blocks (`JOB_BLOCK` / `JOB2_BLOCK`, `gpu3d.c`): the ARM fills
  N+1 while the GPU renders N. But `run_async` / `gpu3d_submit` still call `job_wait()` before
  `v3d_start`, so only one GPU job runs at a time (`v3d_start` rejects a second).
- `gpu3d_later=1` (bm3d 6.9, with `queue=2`, off by default): the frame is held one frame
  (`held` in `runtime.c`), so the next `_update` and `_draw` fill the other block while the
  GPU draws it. Host test: `make test-queue2d` (same pictures; jobs filled in overlap).
- Cache: `dcache_clean_invalidate_all` 4 times per job (coarse; `dcache_clean_range` exists).
- Binner: static 4 MiB tile allocation + 2 MiB overflow per job (`BPOA` / `BPOS`). `OUTOMEM`
  is not handled: a stall hits the 200 ms timeout, the GPU is disabled, the ARM takes over.
- Vertex shader is done (`gpu3d_vs=2` default).

## Decisions

From the v3d-async-proposal review (branch `origin/v3d-async-proposal`,
`docs/v3d-async/01*`, `02*`):

- Interrupts `FLDONE` / `FRDONE`: skip. Polling costs only the final wait, the ARM is the
  bottleneck, there is no scheduler to give time to.
- Triple buffering: skip until a measurement shows GPU time > ARM time.
- Impostors (1–2 big triangles for far small meshes): allowed only as a flag (config key,
  Settings row, `gpu3d()` argument), off by default; after the Pi run, with Animator bake.
- `v3d_async.c/.h` from the proposal are not adopted (`v3d.c` already does it).

## To do, in order

1. Measurement counters (done, bm3d 6.8 code; read them on the Pi): cycles of the 4 cache calls per job; binner tile-list and overflow
   use, count of overflow jobs. See `docs/BENCH3D.md` columns.
2. Pi run to read them (bench3d and Overbit with GPU+VS+Q and Q2, GPU+WC); then decide: range
   clean instead of whole-cache clean, bigger or dynamic overflow.
3. M39 step 4, runtime: done as an option (`gpu3d_later`, bm3d 6.9 code); left: the Pi
   run below, then decide whether it becomes the default.
4. Only if 2 (or 3 on the Pi) shows a gain: chain two jobs in hardware (CT0/CT1 queue);
   needs removing `job_wait` before `v3d_start`.
5. Impostor flag (see Decisions), after 2.
6. M39 remaining: 8/16-bit attributes, RGBA5551/4444, ETC1, mipmaps.

## To measure on the Pi

Last Pi numbers: 2026-10-06, bm3d 6.7. Ask the user to run them; reports go in branch
`reports`.

- Overbit benchmark: GPU+VS, GPU+VS+Q, Q2.
- `queue=1` vs `2` in real games (bench3d split rows Q/Q2 showed no gain).
- `gpu3d_queue=2` with `gpu3d_later=0` vs `1` (bm3d 6.9): Overbit (no saved RENDERER: its
  `q` sets the queue to 1) and a 3D game, F11 ms and fps; the session report's `frame queue`
  line (jobs filled in overlap, frames held); no tearing on the TV.
- 2D on GPU, tex16, WC, `g` test steps 15-16, `menu_scale=3`.
