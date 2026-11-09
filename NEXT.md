# Next tasks

State on 9 Nov 2026: `bm-core` holds the Mali work up to bm3d 7.36 plus the face cache of 96 meshes (8 MB) and the resolve in bands on the worker cores. The performance work on Dark Night and Yharnam (Pi and RGB30) is closed. The console runs a test kernel with `mali_split=1`, `gpu3d_queue=2`, `gpu3d_later=1`, `perf=top`.

## 1. The language of bm: stage 1 (learning project)
- Branch: open `claude/bm-language-front-end` from an up-to-date `bm-core`. The roadmap branch is merged already.
- Read in this order: the section "A language of bm" in `docs/progress/typed.md` (decisions, nine stages, the program `sum`, the ARMv6 words); `docs/design/typed-native.md` sections 5, 8 and 9 (what `bmt.py` does, where the C is compiled, open questions); the docstring of `scripts/bmt.py` (the subset of Lua and its rules); `src/bm/bmvm.h` (the register machine, for stage 3).
- Decided by the user: the syntax is the syntax of Lua; the first back end is a bytecode and a VM; the goal is ARMv6 32 bit (A32) for the Pi Zero W; the RGB30 is out; time does not matter. The compiler is new and small: `tools/bml/` in Python, separate from `scripts/bmt.py`.
- To do, stage 1: lexer, parser, syntax tree and type check for `i32` and `bool`, `local`, `if`, `while`, numeric `for`, `return`, functions that call functions. An error names the line. Write programs with an error and the program `sum` as tests.
- Then stage 2: a stack VM in Python, `sum(10)` gives 55, random programs give the same result as Lua. Do not start the ARM back end before stage 2 passes.
- Check the ARMv6 words of `sum` in the roadmap with `qemu-system-arm` or an assembler before you trust them (they were encoded by hand).
- Open: the overflow of `i32` (C wraps at 32 bit, Lua at 64 bit), and `bmvm` or a new VM for the kernel (stage 3).
- Close the task as `CLAUDE.md` says: update `## Open work` of `docs/progress/typed.md`, then run `python scripts/docs_audit.py score`. `docs/progress/graphics.md` is over its line limit (122 lines): not part of this work.

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
