# Next tasks

State on 9 Nov 2026: `bm-core` holds the Mali work up to bm3d 7.36 plus the face cache of 96 meshes (8 MB) and the resolve in bands on the worker cores. The performance work on Dark Night and Yharnam (Pi and RGB30) is closed. The console runs a test kernel with `mali_split=1`, `gpu3d_queue=2`, `gpu3d_later=1`, `perf=top`.

## 1. The language of bm: stage 3 and stage 4 (learning project)
- Done in stages 1 and 2 (`tools/bml/bml.py`, `tests/bml/run.py`, `make test-bml`, manual in `docs/design/bml-manual.md`):
  - Stage 1 front end: lexer, parser, syntax tree (AST), type checker (`i32`, `bool`, `local`, `if`/`elseif`/`else`, `while`, `break`, numeric `for`, `return`, `abs`/`min`/`max`, functions calling functions, recursion), and Lua stripper (`--lua`). 23 error checks verify exact line numbers.
  - Stage 2 stack bytecode and host `StackVM`: `sum(10)` gives 55, `sum(-5..100)`, `tests/bml/features.bml` (347 calls), and 200 random programs (1000 calls) match `luahost`.
  - The 9 ARMv6 words of `sum` (`E3A01000 ... E12FFF1E`) are verified with `arm-none-eabi-objdump` and `run_armv6_words` on `-5..100`.
- Next, stage 3: decide the VM of the kernel (`bmvm` in `src/bm/bmvm.h` or a stack VM in C), pack bytecode in a `.bm` section, and load it like `vm_load`.
- Then stage 4: ARMv6 32-bit (A32, AAPCS) back end in `tools/bml/`, tested with `qemu-system-arm` against Lua.
- Open: the overflow of `i32` (C/ARM wraps at 32 bit, Lua at 64 bit; `StackVM` supports `--wrap32`), and `bmvm` vs a new VM for stage 3.

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
- Run `python scripts/herotable.py <folder> --update manuals/content/en/graphics-techniques.md manuals/content/it/graphics-techniques.md`.
- Run `python scripts/manuals.py check graphics-techniques`, then `stamp`, then `build`. Look at the PDF.
- Commit the two `.md` files and `manuals/sync.json`. Merge into `bm-core` with a fast-forward and push.
- Tell the user how the ARM and the GPU compare: Pi (ARM and V3D) and RGB30 (ARM and Mali).

## 4. Rename hero in bm-market
- `bme/herocart/herocart.bme` becomes `bme/hero/hero.bme`.
- `games/hero-stress/herocart.bm` becomes `hero.bm`, built from the current `carts/hero`. Raise the version.
- Update the two `net fetch` lines in `README.md` (section "Projects and kits"): they name `bme/herocart/herocart.bme` now.
- Run the market check (22 games, all right) before the push.

## 5. Restore the devices
- RGB30: it runs the kernel of the branch `claude/mali-skin-behind-camera` now (config keys `mali_split=1`, `perf=top`); install the default kernel when the branch is in `bm-core`.
- Pi Zero W: not touched in this work; install the default kernel `v0.2.5-1984-gf07d085c` if it still has the test one.
- The cart clears the keys `hero_test` and `hero_prof` by itself. Check with `tools/pi.py --dev <board> status`.

## 6. Small checks
- Ask the user if `crash.bmm` opens in bm Mesh and bm Animator (file in `/bmm` on both boards).
- Ask the user if `bmk/island` stays in bm-market (Island Bench is removed).

## 7. Typed code and the Pi Zero W (state of 8 Nov 2026; the list is in `docs/NEXT.md`, "Typed native code")
- Later: a typed API for other games and its recipe in the GAME-GUIDE; structs and the integer overflow rule in the language; the manuals `api-3d` (chapters 1 and 9, stamp); the levers of the PS1 comparison with the V3D on.
- The Pi has the clean `bm-core` kernel. Its `config.txt` still has `hero_test=all` and `hero_prof=arm.gpu`.
- The folder `../bm33-core` is an old worktree that git does not know. It holds one file that is not in the repository (`docs/archive/sync/sync-tropic-trail/AGY.md`). Look at it, then delete it.
