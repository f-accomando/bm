# bm SNES: I-cache work (the 16 KB instruction cache of the Pi Zero W)

Part of [snes-cpu.md](snes-cpu.md).

## I-cache work

Branch `claude/snes-icache`, 2026-11-13. Aim: fewer misses in the 16 KB instruction cache (4 ways, 32-byte lines) of the Pi Zero W, with no change of behaviour. The stage figures below come from a model on the PC. The first Pi read does not agree with it: see "The Pi result". The CPU loop, the SPC700, the S-DSP and the PPU run in turn in every line and push each other out of the cache. One miss costs about 0.06 us.

### The Pi result (kernel 123, 2026-11-13): the PC model did not carry over

Status: the first version of this work (stages 1 to 3 below, bm-core `612e494a`) gave no stable gain on the Pi Zero W. Read the stage table as what the PC model said, not as what the Pi does.

SMW2, same scene `m1/15/02` (gameplay), kernel of before this work (`pi_run4`) against kernel 123 (`pi_run6`):

| Field | Before | After | Change |
|---|---|---|---|
| fps | 47 | 48 | |
| `us frame` | 12061 | 12435 | +3% |
| `us ppu` | 10080 | 9784 | -3% |
| `us spc` | 3024 | 3068 | +1% |
| `us cpu` | 2037 | 2131 | +5% |
| `icache miss: cpu` | 55963 | 53839 | -4% (the model said about -36%) |
| `icache miss: spc` | 16247 | 23793 | +46% (worse) |

What this says:
- The model counts 2000 to 4000 misses a frame in the CPU and SPC parts of SMW2. The Pi counts 56000 and 16000. The Pi misses are not the capacity and conflict misses between the SNES functions that the model measures. A better order of those functions moves the Pi counters by a few percent at most. The cause of the other 90% is not found. Candidates, none tested: fetches after a wrongly predicted indirect jump (the CPU loop dispatches with `ldr pc`), interrupts and other kernel code that run between the parts, a cache smaller than 16 KB in practice, the replacement policy of the ARM1176.
- The SPC part got worse (+46% misses) although `step` is 2.5 times smaller. Two changes touch it: the `-Os` build of `snes_spc.c` and the new place of the SPC700 functions in the block. The Pi data cannot tell them apart. `-Os` code has more calls and jumps and 12% more instructions; the block puts `step_c`, `dsp_tick` and `voice_run` at addresses that the search chose for the model, not for the Pi.
- The `ppu` gain (-3%) is the only one that is near what the model promised, and it comes at least partly from `pfill` (fewer instructions), not from the layout.

Decision (this branch, `claude/snes-icache-pi`): the SPC700 file is back at `-O2` (`SNES_SPC_OS=1` builds the `-Os` version). The order of the block was searched again on the `-O2` code. Both changes are guesses until the Pi has read them. To find which of the three things matters, build the four images and read the same scene:

| Image | Command | What it shows |
|---|---|---|
| A | `make build/kernel.img` | the default: block, SPC700 at `-O2` |
| B | `make build/kernel.img SNES_SPC_OS=1` (delete `build/k/src/bm/snes_spc.c.o` first) | the first version, the one that was read |
| C | `make build/kernel.img SNES_HOT=0` | no block; `exec_fast` and `pfill` stay |
| D | `SNES_HOT=0` and `git revert` of `exec_fast` | only if C is also worse than the kernel of before this work |

Sizes of the default image (A): block from `__snes_hot_start` to `__snes_hot_end` 67 KB with the gaps (67 sections), `step` 12 KB. In the model this image is not better than the first version: the search found 60154 misses on the subset (SMW + SMW2), the first version 49228 with the SPC700 at `-Os`. That is the price of the revert; the Pi had said the other way.

Keep the one with the best `us frame` in 2 minutes of the same scene. The miss counters alone are not a reason to keep a change: `us` is.

## The footprint (ARM build, `-O2`, before the work)

| Code | Bytes | Remark |
|---|---|---|
| 65816 loop `snes_cpu_hot` (assembly) | 4404 | hot |
| `exec_op` (C opcodes) | 32716 | cold section. Its helpers `cpu_sr`, `cpu_sw`, `cpu_cold` and `cpu_attn` were inside it, so a register access of the bus ran 30 KB away from the loop |
| SPC700 `step` | 12000 to 13900 | one switch, every case with its own `rd`/`wr` calls |
| `voice_run`, `dsp_tick` (S-DSP) | 2560, 1736 | 1.5 million ARM instructions a frame: more than the SPC700 |
| `ppu_line`, `draw_bg`, `compose`, `ppu_mix_sub_arm` | 5320, 1736, 716, 2120 | PPU |
| bus (`raw_read`, `raw_write`, `snes_ppu_write`, `snes_frame_break`, ...) | about 11000 | with the load, debug and DMA code between the hot functions |
| libc `memset` | 256 | the PPU calls it about 15 times a line, about 170 ARM instructions for 256 bytes |

The hot code is about 45 KB, three times the cache (PC model). The address of a function modulo 4 KB decides which functions fight for the same sets.

### The measure (PC and QEMU)

`python3 core/scripts/icache_sim.py ROM` (Linux or WSL; needs `arm-none-eabi-gcc`, `qemu-system-arm`, `gcc`) builds the SNES core with `tests/snes/icache_run.c`, plays the ROM in QEMU, switches the trace of every fetched instruction on after the warm-up, and sends it to `tests/snes/icache_sim.c`, a model of the cache (round-robin replacement). It prints the misses a frame by area and the worst functions. `core/scripts/icache_search.sh TAG` does SMW (900 frames of warm-up) and SMW2 (1200 frames) with 10 traced frames each, then searches the layout. `tests/snes/icache_layout.c` moves the sections inside a recorded trace and counts again, so an order is searched in minutes (simulated annealing over the order and the gaps of the sections).

Limits (the Pi read proved them important: the model counts the PC cache, not the ARM1176). The model has no prefetch and no branch predictor. It counts 3 to 10 times fewer misses than the Pi log (SMW: model 9300 in all, Pi 24000 in the CPU part alone). The Pi counter probably also counts the fetches that follow a wrongly predicted indirect jump (the CPU loop dispatches with `ldr pc`). Use the model to compare two builds. The ROMs are read from your disk (folder `SFC=`). The scene is the title and the first level with a held button, not a full play. A relink of the same code in another object order moved the model by 10%, so a gain below that is noise.

### Stages

Misses a frame in the model, SMW / SMW2 (all areas). `ins` = ARM instructions a frame in millions.

| Stage | Change | Hot block | SMW | SMW2 | ins SMW / SMW2 | Expected on the Pi |
|---|---|---|---|---|---|---|
| 0 | bm-core as it was (objects linked in file order) | none | 9301 | 20863 | 9.09 / 11.70 | the reference |
| 1 | `core/src/bm/snes_hot.ld`: the hot sections in one block at a 4 KB address, order and gaps searched. `cpu_sr`, `cpu_sw`, `cpu_cold`, `cpu_attn` in their own sections. libc `memset` and `memcpy` in the block | 64.8 KB with the gaps (45 sections) | 6283 | 13889 | same | `icache miss` of the three parts about a third lower |
| 2 | `exec_fast`: REP, SEP, PHP, PLP, PHB, PLB, XBA, BIT #, the transfers with D and S in a 2.3 KB function in front of the 32 KB `exec_op` (one macro `FAST_OPS` serves both). `pfill`: word stores instead of libc `memset` for the PPU line buffers | in the block | 8170 (before a new search) | 18216 (before a new search) | 8.96 / 11.59 | `icache miss: cpu` lower in games with many REP, SEP, PLP, PLB. `ppu`: about 2000 ARM instructions a line less |
| 3 | SPC700 file at `-Os` (32-bit ARM only): `step` from 12 KB to 4.8 KB, 12% more SPC700 instructions (about 40000 ARM instructions a frame) | in the block | 5896 (before a new search) | 23302 (before a new search) | 9.00 / 11.64 | `icache miss: spc` about a third lower, `us spc` equal or a little lower |
| final | rebased on bm-core of 2026-11-13, order searched again on this code | 54.8 KB with the gaps (71 sections), from `__snes_hot_start` to `__snes_hot_end` | 4920 | 13277 | 8.95 / 11.64 | see "What to read on the Pi" |

Final model by area, SMW: cpu 329, bus 682, spc 1167, dsp 815, ppu 1060, other 863. SMW2: cpu 1822, bus 2966, spc 1604, dsp 1203, ppu 2431, other 3248. That is 47% (SMW) and 36% (SMW2) fewer misses than stage 0, with 1.6% and 0.5% fewer ARM instructions.

Checks done: `make test-snes`, `test-snes-cpu` (golden hashes), `test-snes-cpu-arm` (20000 random instructions against the C reference), `test-snes-ppu`, `test-snes-ppu-arm`, `test-snes-gsu-diff`, `test-snes-gsu-arm`, `test-snes-dsp`, `test-snes-dsp77`, `test-snes-sa1`, `test-snes-debug`. With `BM_SNES_ROMS` set: `test-snes-smw2` and `test-snes-smw2-arm` (16 pictures, 0 different, on the host and on the ARM1176). The sound and the picture of 900 frames of SMW and 1200 frames of SMW2 give the same hash with the SPC700 at `-O2` and at `-Os` (`icache_run`; `-DSNES_SPC_O2` builds the old way). The Pi kernel (`make build/kernel.img`) and the RGB30 kernel build.

### What to read on the Pi

Build the four images of "The Pi result" and play SMW and SMW2 (Yoshi's Island) for 2 minutes each with the logger. Read `us frame`, `cpu`, `spc`, `ppu` and `icache miss: cpu N spc N ppu N` (line `ppu us: ...`), median of the lines `m1/15/02` (SMW level) and of the lines with `gins` (SMW2). Reference (kernel before this work, SMW2 `m1/15/02`): frame 12061 us, cpu 2037, spc 3024, ppu 10080, misses cpu 55963, spc 16247. Reference of the first version (kernel 123): frame 12435, cpu 2131, spc 3068, ppu 9784, misses cpu 53839, spc 23793. The earlier expectation of this section (a third to a half fewer misses) was wrong and is removed.

### Rules (do not break)

- `core/src/bm/snes_hot.ld` is made by `core/scripts/icache_search.sh` (`build/icache_TAG/best.ld`; keep the header comment). Do not reorder it by hand. After a change that moves the size of a hot section (`step`, `step_c`, `ppu_line`, `draw_bg`, `voice_run`, `snes_cpu_hot`, the bus functions), run the search again. The old order is then only a guess. A name in the file that no longer exists is harmless: the section goes to the normal place.
- A new hot function needs a line in the file and a new search. Without it the function lands outside the block.
- `snes_spc.c` is built at `-Os` only with `SNES_SPC_OS=1` (off by default: the first Pi read was worse). A change to the SPC700 must keep the hash of `icache_run` equal in both builds (`--filefl snes_spc=-DSNES_SPC_OS` builds the `-Os` one).
- No layout or code-size change for the instruction cache is accepted on the PC model alone. It needs an A/B read of `us frame` on the Pi in the same scene (`SNES_HOT=0` and `SNES_SPC_OS` give the images). The model has been wrong by a factor of 10 on the miss count and by the sign on the SPC700.
- `FAST_OPS` in `snes_cpu.c` holds the cases of `exec_op` that `exec_fast` also runs. Add an opcode only when a trace of a game shows that the assembly loop hands it to C often.
- The run order is not changed by this work: CPU, SPC700 and PPU in the same line, the lazy sound sync, `gsu_sync_pos`, `dsp_sync_pos`, the HDMA and IRQ times.

### Open work

- Pi: read the table above (the numbers are not read yet).
- Run order, not done. The PPU draws line L right after the CPU runs line L, so the two evict each other 224 times a frame. To draw several lines in a row, the PPU registers of every line must be saved first (item C6). The SPC700 and the S-DSP alternate every 16 SPC700 cycles (`spc_run`), so that a KOFF pulse is not lost. A lazy S-DSP that runs only before a register access needs a proof against `tests/snes/dsp_ref.cpp` first (ares, `make test-snes-audio-ref`).
- `exec_op` still holds about 100 opcodes (BIT with a memory operand, TSB, TRB, the indirect modes). Count them in a real play (`tests/snes/ophist.c`, host build) and move the frequent ones into `FAST_OPS` or into the assembly loop. A cold opcode costs a full `flush` and `reload` of the registers.
- The S-DSP uses the most ARM instructions of the sound part (1.5 million a frame, mostly `voice_run`). An idle voice should cost less.
- Hot and cold splitting of the opcode switches with GCC profile feedback (`-fprofile-use` with a committed profile) was not tried.
