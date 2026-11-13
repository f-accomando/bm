# Next tasks

State on 9 Nov 2026: `bm-core` holds the Mali work up to bm3d 7.36 plus the face cache of 96 meshes (8 MB) and the resolve in bands on the worker cores. The performance work on Dark Night and Yharnam (Pi and RGB30) is closed. The console runs a test kernel with `mali_split=1`, `gpu3d_queue=2`, `gpu3d_later=1`, `perf=top`.

## 1. The language of bm: stage 6 (learning project)
- Done in stages 1 through 5 (`core/tools/bml/bml.py`, `core/src/bm/bmnative.c`, `core/src/bm/api_vm.c`, `tests/bml/run.py`, `make test-bml`, manual in `docs/design/bml-manual.md`):
  - Stage 1 front end: lexer, parser, syntax tree (AST), type checker (`i32`, `bool`, `local`, `if`/`elseif`/`else`, `while`, `break`, numeric `for`, `return`, `abs`/`min`/`max`, functions calling functions, recursion), and Lua stripper (`--lua`). 23 error checks verify exact line numbers.
  - Stage 2 stack bytecode and host `StackVM`: `sum(10)` gives 55, `sum(-5..100)`, `tests/bml/features.b` (347 calls), and 200 random programs (1000 calls) match `luahost`.
  - Stage 3 kernel VM back end (`BMVMCompiler`, `--bmvm`): compiles to `BMV1` blobs executed by `core/src/bm/bmvm.c` (`vm_load`, `vm_run`); `sum`, `features`, and 200 random programs match `luahost`.
  - Stage 4 ARMv6 A32 native back end (`ARMv6Assembler`, `ARMv6Compiler`, `--armv6`, `--dump-armv6`): emits the exact 9 hand-verified words for `sum` plus general AAPCS frames (`r4`-`r10` register allocation, stack spills, `__bml_idivmod`); verified on `qemu-system-arm -M versatilepb -cpu arm1176` across all 1,559 calls against `luahost`.
  - Stage 5 native Pi loader (`compile_native_blob`, `--native`, `core/src/bm/bmnative.c`, `native_load`, `native_fn`, `native_run`): validates `BMN1` or raw ARMv6 words, copies to 32-byte-aligned executable RAM, cleans D-cache (`dcache_clean_range`), and invalidates I-cache (`mcr p15, 0, a, c7, c5, 1`) and branch predictor (`c7, c5, 6`) + `DSB`/`ISB`. Verified in Lua (`native_load`/`native_run`) and on bare-metal ARM1176 QEMU.
- Next, stage 6: arrays and structs, finalize `i32` overflow rule (32-bit wrap vs 64-bit Lua), and `f32` on the ARM1176 VFP.

## 2. Mali: other open items (`docs/progress/graphics-mali.md`)
- See the faces `R3D_SCREEN` of a model drawn by the GPU (Overbit with `RENDERER: GPU`); the 3D Bench has no GPU row for them.
- Fog and gloss on a textured face.
- A lit draw before a textured one removed the whole map in Hero Stress: the draw order avoids it, the cause is not found.
- The style 0 shadow (blend, `R3D_KIND_SHADE`) is not run on the hardware.
- The 3D Bench score with queue 2 and later 1 (the pixel check of 6 Nov equals the 29 Oct one); the suite of 29 tests of Hero Stress with `mali_alpha`.

## 3. Fill the hero table in the manual
- The suite of 29 tests ran on the Pi Zero W (`hero_test=all hero_prof=arm.gpu`) and on the RGB30 (the same keys; `mali_alpha` is on by default now).
- Get the reports from the branch `reports` (files `*_hero_pi-zero-w_*` and `*_hero_rgb30_*`). A complete report has the line `suite done tests=29`.
- Save the newest report of each board in one folder.
- Run `python core/scripts/herotable.py <folder> --update manuals/content/en/graphics-techniques.md manuals/content/it/graphics-techniques.md`.
- Run `python core/scripts/manuals.py check graphics-techniques`, then `stamp`, then `build`. Look at the PDF.
- Commit the two `.md` files and `manuals/sync.json`. Merge into `bm-core` with a fast-forward and push.
- Tell the user how the ARM and the GPU compare: Pi (ARM and V3D) and RGB30 (ARM and Mali).

## 4. Rename hero in bm-market
- `bme/herocart/herocart.bme` becomes `bme/hero/hero.bme`.
- `games/hero-stress/herocart.bm` becomes `hero.bm`, built from the current `bm/hero`. Raise the version.
- Update the two `net fetch` lines in `README.md` (section "Projects and kits"): they name `bme/herocart/herocart.bme` now.
- Run the market check (22 games, all right) before the push.

## 5. Restore the devices
- RGB30: it runs the kernel of the branch `claude/mali-skin-behind-camera` now (config keys `mali_split=1`, `perf=top`); install the default kernel when the branch is in `bm-core`.
- Pi Zero W: not touched in this work; install the default kernel `v0.2.5-1984-gf07d085c` if it still has the test one.
- The cart clears the keys `hero_test` and `hero_prof` by itself. Check with `core/tools/pi.py --dev <board> status`.

## 6. Small checks
- Ask the user if `crash.bmm` opens in bm Mesh and bm Animator (file in `/bmm` on both boards).
- Ask the user if `bmk/island` stays in bm-market (Island Bench is removed).

## 7. Typed code and the Pi Zero W (state of 8 Nov 2026; the list is in `docs/NEXT.md`, "Typed native code")
- Later: a typed API for other games and its recipe in the GAME-GUIDE; structs and the integer overflow rule in the language; the manuals `api-3d` (chapters 1 and 9, stamp); the levers of the PS1 comparison with the V3D on.
- The Pi has the clean `bm-core` kernel. Its `config.txt` still has `hero_test=all` and `hero_prof=arm.gpu`.
- The folder `../bm33-core` is an old worktree that git does not know. It holds one file that is not in the repository (`docs/archive/sync/sync-tropic-trail/AGY.md`). Look at it, then delete it.
