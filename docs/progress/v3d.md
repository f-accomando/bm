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

1. Pixels at 1080p: render 640x360 or 720p and enlarge better (R41: scaler kernel, QPU
   bicubic / FSR-style pass); texture and 2D cost per pixel on the GPU.
2. Impostors: Pi runs at `r3d_impostor` 8, 16, 32 with the new threshold (8 = 2.7 render px
   at 640x360, no effect), photo of far heroes; then the default for crowds (R42).
3. Chain two jobs in hardware (CT0/CT1) only if a measurement shows the GPU idle between jobs.
4. M39 remaining: 8/16-bit attributes, RGBA5551/4444, ETC1, mipmaps.
5. QPU programs outside 3D (M44): the toolkit notes are in `docs/ROADMAP.md`.

## To measure on the Pi

Reports in branch `reports`; ask the user. The kernel must end in the commit tested.

- Impostor 0 / 8 / 16 / 32: `res:640/gpu:arm+vs/q:4/ring:1/far:60`.
- A played Overbit on the GPU at 640x360, 720p and 1080p (session report): fps, `GPU 3D` ms.
- 2D on GPU, tex16, WC, `g` test steps 15-16, `menu_scale=3`, each from all-off.
