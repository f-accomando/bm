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
- Impostors (far small meshes as one quad): only as a flag, off by default (see To do 5).
- `v3d_async.c/.h` from the proposal are not adopted (`v3d.c` already does it).

## To do, in order

1. Measurement counters (done, bm3d 6.8 code; read them on the Pi): cycles of the 4 cache calls per job; binner tile-list and overflow
   use, count of overflow jobs. See `docs/BENCH3D.md` columns.
2. Pi run to read them (bench3d and Overbit with GPU+VS+Q and Q2, GPU+WC); then decide: range
   clean instead of whole-cache clean, bigger or dynamic overflow.
3. M39 step 4, runtime: show the frame one later so `_draw` overlaps the GPU
   (`runtime.c` `submit3d` / `flush3d` / `draw2d`).
4. Only if 2 shows a gain: chain two jobs in hardware (CT0/CT1 queue); needs removing
   `job_wait` before `v3d_start`.
5. Impostor flag: code done (r3d, before the backend: ARM and GPU alike). `r3d_impostor=N`
   (0 off; N px of the mesh's box on screen, 2..64), *Settings > Screen and sound > 3D
   impostors* (Off/2/4/8/16), `gpu3d(nil, nil, nil, nil, N)`; `make test-impostor`. Not a
   3D Bench profile (the summary has no room for a 15th column). Left: the Pi numbers
   below; then decide whether Animator bake (an atlas colour per mesh) is worth it.
6. M39 remaining: 8/16-bit attributes, RGBA5551/4444, ETC1, mipmaps.

## To measure on the Pi

Last Pi numbers: 2026-10-06, bm3d 6.7. Ask the user to run them; reports go in branch
`reports`.

- Overbit benchmark: GPU+VS, GPU+VS+Q, Q2.
- `queue=1` vs `2` in real games (bench3d split rows Q/Q2 showed no gain).
- 2D on GPU, tex16, WC, `g` test steps 15-16, `menu_scale=3`.
- Impostors: Overbit benchmark (GPU+VS and ARM) with `r3d_impostor` 0, 4, 8: fps, tris,
  and a photo of the far heroes / pickups (does the quad pop or show?).
