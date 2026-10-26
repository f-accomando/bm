# Multicore (RGB30 quad core) and the end of the Mali graphics work
Schedule of 26 Oct 2026. Three tracks, in this order: **M** (secondary cores), **G** (bm3D on Mali), **V** (use of four cores by apps, games and graphics). Rule: finish a track before the next one starts. The Pi Zero W has one core and stays the benchmark (`docs/design/multitasking-report.md` section 9).

## State now
- The RGB30 kernel (`kernel8`) runs on core 0 only. The other three Cortex-A55 cores stay in the firmware (TF-A) and no code calls PSCI `CPU_ON`. The reason is not written anywhere: M1 finds it.
- `src/rgb30/start.S` starts one core. `src/rgb30/mmu.c` builds one identity map (RAM normal, inner shareable; framebuffers non-cacheable; devices nGnRE). `src/rgb30/gic.c` sets up one core.
- Mali (`graphics-mali.md`): triangle, batching, `BM_VS3`, async queue, 4 MB arena and `mali3d=1` by default are done on hardware (`bm3d 7.15`). Textures on GPU and hardware Z-buffer are open.

## Track M: secondary cores
Each step ends with a QEMU test (`qemu-system-aarch64 -M virt -smp 4`) and, where it says so, a report from the console.
- M1 - Bring-up [QEMU DONE 26 Oct: `smp_start()` in `src/rgb30/smp.c`, `smp_entry.S`, `mmu_attach()`, test `test_smp_cores_up`; console build compiles; waits for the console run]. `plat_cpu_on(core, entry)` with PSCI `CPU_ON` (SMC on the console, HVC in QEMU). Secondary entry in `start.S`: EL2 to EL1, own stack of 64 KB per core in `.stack`, same `vbar_el1`, same page tables (`mmu_attach()`: `ttbr0`, `tcr`, `mair`, `sctlr`), then park in `wfe` on a mailbox. Core 0 prints `core N up` over the serial port. Done when QEMU shows 3 cores up and the console report shows the answer of `CPU_ON` for each core (the report decides what comes next if TF-A refuses).
- M2 - Safe base. Per-core id (`mpidr` to index), `spin_lock` and `atomic` helpers (exclusive load/store, `dmb ish`), per-core `TPIDR_EL0` for the TLS block, a lock around the heap (`malloc`) or no heap use on cores 1-3. Test: 3 cores add to one counter 1,000,000 times each, the sum is exact.
- M3 - Interrupts per core. GICv3 redistributor of each core, one timer tick per core (not needed on workers), an SGI from core 0 to wake a worker. Test: SGI round trip in QEMU.
- M4 - Pure-function worker. `smp_job_t` (function, input copy, output copy, status) and a lock-free ring of 16 slots per worker. Core 0 posts, a worker runs, core 0 polls. Cache rule: buffers are inner shareable normal memory, so the cores see the same data; a buffer for a device keeps `dc civac`. Done when SHA-256 of 4 MB gives the same digest on core 0 and on a worker (host test plus QEMU).
- M5 - Job API join. A `fiber_job_t` may post its pure part to a worker and yield until the answer (`fiber_job_wait_smp`). `background_stop` cancels a posted job (flag, the worker checks it between pieces). The SD writer lock and the cancel rule carry over. Wire: catalog hash, release verify, cover decode. Pi: no change (jobs run in the fiber).
- M6 - Fault safety. A fault on a worker prints core id, `esr`, `elr`, then parks the worker; core 0 marks the job failed and goes on. Watchdog: a job over 2 s is a failure. Test: a job that divides by zero and a job that loops.
- M7 - Capability and docs. `multicore` capability on in `docs/spec/` (D25 is KEEP, change to a documented divergence with the number of cores), `docs/progress/platforms.md`, `docs/design/multitasking-report.md` section 9.

## Track G: bm3D on Mali (end of the graphics work)
Open items of `graphics-mali.md`, in this order. Each step is verified on the console with 3D Bench (benchmarks, never a live game).
- G1 - Textures on GPU (item 7): `BM_FS_TEX` and UV varying with `tools/mali/mkshader.c`, `Texture` and `Sampler` descriptors in `mali_desc.c`, remove the `colour_only` fallback. Done when `quad_tex` and the textured scenes run on the GPU with the pixel check equal to the ARM result.
- G2 - Hardware Z-buffer and early-Z (item 8): `ZS` buffer in the MFBD, `Z_FUNC = LESS`, remove `sort_tris` on the GPU path. Done when the `spheres` scenes keep the picture and the games score rises.
- G3 - Lit and smooth scenes (`spheres`, `spheres_smooth`, `split`): the numbers of the table show the GPU path below ARM (22.9 fps against 140 n60). Find the cause (vertex count, VS cost, resolve) and fix or document it.
- G4 - Close: games score, GPU score and the table of `graphics-mali.md` updated; roadmap API-5 items removed when done.

## Track V: take advantage of four cores (design first, then build)
Starts when M and G are done. Step V0 is a review with the user; the later steps follow its decision.
- V0 - Review. Measure where a frame spends time on the RGB30 (3D Bench `draws`, `spheres`, a game with Lua load). Choose from the list below and rank by the measured gain.
- V1 - Graphics: split the CPU raster (the ARM path) in screen bands, one band per core; transform and clip of vertices on workers while core 0 builds the Mali descriptors; the Mali queue fill on a worker.
- V2 - Games: opt-in API for a game to post a pure job (`job_run(name, data)`) and read it back next frame (pathfinding, particles, mesh skinning). No shared Lua state: a worker never touches the Lua heap. Lockstep games (Overbit, bmnet) stay on one core.
- V3 - Audio: mixer on a worker, so a long frame never starves the sound.
- V4 - Apps and system: Market download and SD write on a worker core (the SD driver lives on that core), cover decode, hashes, TLS handshakes. The menu pointer stays on core 0.
- V5 - Scheduler policy and a `config.txt` key (`cores=1..4`), measured gain per workload in the docs.

## Rules (do not break)
- Core 0 owns the heap, lwIP, the FAT, the Lua state and every device register until a step of this file moves one.
- A worker runs pure functions only: input copied in, output copied out, no `malloc`, no Lua, no device access.
- One change, one measurement. Keep a step only when the QEMU test passes and, for hardware steps, the console report agrees.
- The Pi kernel is not changed by track M. The `multicore` capability is off on the Pi.

## Open work
- M1 on the console: copy `dist/kernel.img` (`make TARGET=rgb30`) with `./easy_install.sh`, read the boot report: lines `smp: core N up` or `smp: core N: CPU_ON answered <code>` (PSCI codes: -1 not supported, -2 invalid parameters, -4 denied, -5 already on). The MPIDR guess is `core << 8` (RK3568 device tree); if the answer is -2, try `core`.
- M2-M7, G1-G4, V0-V5 as above.
