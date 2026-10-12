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
- `v3d_async.c/.h` from the proposal are not adopted (`v3d.c` already does it).

## To do, in order

1. Read on the Pi the per-job counters (cache calls cycles, binner tile-list and overflow
   use, overflow jobs; `docs/BENCH3D.md`); then decide: range clean instead of whole-cache
   clean, bigger or dynamic overflow.
2. Decide from the Pi run whether `gpu3d_later` becomes the default.
3. Only if 1 or 2 shows a gain: chain two jobs in hardware (CT0/CT1 queue); needs removing
   `job_wait` before `v3d_start`.
4. `r3d_impostor=N` (0 off; 2..64 px; `gpu3d()` 5th; `make test-impostor`), off; skinned meshes by
   their pose's box (then also culled). After the Pi numbers: Animator bake (atlas colour a mesh)?
5. M39 remaining: 8/16-bit attributes, RGBA5551/4444, ETC1, mipmaps.

Fixed, not yet on the Pi: Overbit forced queue 1 at every start (`gpu3d` queue `true` = 1);
now `true` = the console's level, 5th value the level; bench renderer `ql`; the `.bm`'s
640x360 on the GPU; impostors for skinned meshes (heroes never became one). Market reseed.

## To measure on the Pi

Last Pi numbers: 2026-10-06, bm3d 6.7. Ask the user; reports in branch `reports`.

- `queue=1` vs `2` in real games (bench3d split rows Q/Q2 showed no gain).
- Overbit bench `gpu` column (`stat(16)`) at 1080p EXTREME GPU+VS+Q: near 29.9 ms = GPU-bound.
- Rerun with this kernel and the reseeded Overbit: `./easy_install.sh bench gpu:q+ql` with
  `gpu3d_queue=2` (q vs ql), then a played Overbit with `gpu3d_later=1`: the session report's
  `options` line must say `queue=2 later=1`, `frame queue` line (frames held); no tearing.
- 2D on GPU, tex16, WC, `g` test steps 15-16, `menu_scale=3`.
- Impostors: `far:60` ring (small far heroes): `res:640/gpu:arm+vs/q:4/ring:1/far:60` with `r3d_impostor` 0, 8, 16, 32 (row steps Off, 8, 16, 32): fps, tris, photo of the far heroes.
