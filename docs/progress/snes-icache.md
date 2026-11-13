# bm SNES: I-cache work (the 16 KB instruction cache of the Pi Zero W)

Part of [snes-cpu.md](snes-cpu.md).

## I-cache work

Branch `claude/snes-icache`, 2026-11-13. Aim: fewer misses in the 16 KB instruction cache (4 ways, 32-byte lines) of the Pi Zero W, with no change of behaviour. Nothing here is measured on the Pi: the figures come from a model on the PC (below). The CPU loop, the SPC700, the S-DSP and the PPU run in turn in every line and push each other out of the cache. One miss costs about 0.06 us.

### The footprint (ARM build, `-O2`, before the work)

| Code | Bytes | Remark |
|---|---|---|
| 65816 loop `snes_cpu_hot` (assembly) | 4404 | hot |
| `exec_op` (C opcodes) | 32716 | cold section. Its helpers `cpu_sr`, `cpu_sw`, `cpu_cold` and `cpu_attn` were inside it, so a register access of the bus ran 30 KB away from the loop |
| SPC700 `step` | 12000 to 13900 | one switch, every case with its own `rd`/`wr` calls |
| `voice_run`, `dsp_tick` (S-DSP) | 2560, 1736 | 1.5 million ARM instructions a frame: more than the SPC700 |
| `ppu_line`, `draw_bg`, `compose`, `ppu_mix_sub_arm` | 5320, 1736, 716, 2120 | PPU |
| bus (`raw_read`, `raw_write`, `snes_ppu_write`, `snes_frame_break`, ...) | about 11000 | with the load, debug and DMA code between the hot functions |
| libc `memset` | 256 | the PPU calls it about 15 times a line, about 170 ARM instructions for 256 bytes |

The hot code is about 45 KB, three times the cache. The address of a function modulo 4 KB decides which functions fight for the same sets.

### The measure (PC and QEMU)

`python3 scripts/icache_sim.py ROM` (Linux or WSL; needs `arm-none-eabi-gcc`, `qemu-system-arm`, `gcc`) builds the SNES core with `tests/snes/icache_run.c`, plays the ROM in QEMU, switches the trace of every fetched instruction on after the warm-up, and sends it to `tests/snes/icache_sim.c`, a model of the cache (round-robin replacement). It prints the misses a frame by area and the worst functions. `scripts/icache_search.sh TAG` does SMW (900 frames of warm-up) and SMW2 (1200 frames) with 10 traced frames each, then searches the layout. `tests/snes/icache_layout.c` moves the sections inside a recorded trace and counts again, so an order is searched in minutes (simulated annealing over the order and the gaps of the sections).

Limits. The model has no prefetch and no branch predictor. It counts 3 to 10 times fewer misses than the Pi log (SMW: model 9300 in all, Pi 24000 in the CPU part alone). The Pi counter probably also counts the fetches that follow a wrongly predicted indirect jump (the CPU loop dispatches with `ldr pc`). Use the model to compare two builds. The ROMs are read from your disk (folder `SFC=`). The scene is the title and the first level with a held button, not a full play. A relink of the same code in another object order moved the model by 10%, so a gain below that is noise.

### Stages

Misses a frame in the model, SMW / SMW2 (all areas). `ins` = ARM instructions a frame in millions.

| Stage | Change | Hot block | SMW | SMW2 | ins SMW / SMW2 | Expected on the Pi |
|---|---|---|---|---|---|---|
| 0 | bm-core as it was (objects linked in file order) | none | 9301 | 20863 | 9.09 / 11.70 | the reference |
| 1 | `src/bm/snes_hot.ld`: the hot sections in one block at a 4 KB address, order and gaps searched. `cpu_sr`, `cpu_sw`, `cpu_cold`, `cpu_attn` in their own sections. libc `memset` and `memcpy` in the block | 64.8 KB with the gaps (45 sections) | 6283 | 13889 | same | `icache miss` of the three parts about a third lower |
| 2 | `exec_fast`: REP, SEP, PHP, PLP, PHB, PLB, XBA, BIT #, the transfers with D and S in a 2.3 KB function in front of the 32 KB `exec_op` (one macro `FAST_OPS` serves both). `pfill`: word stores instead of libc `memset` for the PPU line buffers | in the block | 8170 (before a new search) | 18216 (before a new search) | 8.96 / 11.59 | `icache miss: cpu` lower in games with many REP, SEP, PLP, PLB. `ppu`: about 2000 ARM instructions a line less |
| 3 | SPC700 file at `-Os` (32-bit ARM only): `step` from 12 KB to 4.8 KB, 12% more SPC700 instructions (about 40000 ARM instructions a frame) | in the block | 5896 (before a new search) | 23302 (before a new search) | 9.00 / 11.64 | `icache miss: spc` about a third lower, `us spc` equal or a little lower |
| final | rebased on bm-core of 2026-11-13, order searched again on this code | 54.8 KB with the gaps (71 sections), from `__snes_hot_start` to `__snes_hot_end` | 4920 | 13277 | 8.95 / 11.64 | see "What to read on the Pi" |

Final model by area, SMW: cpu 329, bus 682, spc 1167, dsp 815, ppu 1060, other 863. SMW2: cpu 1822, bus 2966, spc 1604, dsp 1203, ppu 2431, other 3248. That is 47% (SMW) and 36% (SMW2) fewer misses than stage 0, with 1.6% and 0.5% fewer ARM instructions.

Checks done: `make test-snes`, `test-snes-cpu` (golden hashes), `test-snes-cpu-arm` (20000 random instructions against the C reference), `test-snes-ppu`, `test-snes-ppu-arm`, `test-snes-gsu-diff`, `test-snes-gsu-arm`, `test-snes-dsp`, `test-snes-dsp77`, `test-snes-sa1`, `test-snes-debug`. With `BM_SNES_ROMS` set: `test-snes-smw2` and `test-snes-smw2-arm` (16 pictures, 0 different, on the host and on the ARM1176). The sound and the picture of 900 frames of SMW and 1200 frames of SMW2 give the same hash with the SPC700 at `-O2` and at `-Os` (`icache_run`; `-DSNES_SPC_O2` builds the old way). The Pi kernel (`make build/kernel.img`) and the RGB30 kernel build.

### What to read on the Pi

Build the image of this branch (`make build/kernel.img`, then `./easy_install.sh`). Play SMW and SMW2 (Yoshi's Island) for 2 minutes each with the logger. In the log, read the fields `cpu`, `spc`, `ppu` of the line `us ...` and the numbers `icache miss: cpu N spc N ppu N` of the line `ppu us: ...`. Read the same fields in the last log of the kernel before this branch (`reports/`). Compare the median of the lines `m1/15/02` (SMW level) and of the lines with `gins` (SMW2).

| Game | Field | Before (earlier plays) | Expected after |
|---|---|---|---|
| SMW | `icache miss: cpu` | about 24000 | 12000 to 17000 |
| SMW | `icache miss: spc` | about 13000 | 7000 to 10000 |
| SMW | `icache miss: ppu` | not noted | a fifth to a third lower |
| SMW | `us cpu` / `us spc` / `us ppu` | 3.3 / 1.8 / 9.1 ms | cpu 0.2 to 0.5 ms less, spc equal or 0.2 ms less, ppu 0.3 to 0.6 ms less |
| SMW2 | `icache miss: cpu` | about 41000 (peak 75000) | 22000 to 30000 |
| SMW2 | `icache miss: spc` | about 15000 | 8000 to 11000 |
| both | `ins` (65816 instructions) | unchanged | unchanged (the work is the same) |

The "expected" column is an estimate from the model (a third to a half fewer misses). If the Pi shows less, the likely cause is the wrong-path fetches: no layout removes them. The next step is then fewer indirect jumps in the CPU loop, not more ordering.

### Rules (do not break)

- `src/bm/snes_hot.ld` is made by `scripts/icache_search.sh` (`build/icache_TAG/best.ld`; keep the header comment). Do not reorder it by hand. After a change that moves the size of a hot section (`step_c`, `ppu_line`, `draw_bg`, `voice_run`, `snes_cpu_hot`, the bus functions), run the search again. The old order is then only a guess. A name in the file that no longer exists is harmless: the section goes to the normal place.
- A new hot function needs a line in the file and a new search. Without it the function lands outside the block.
- `snes_spc.c` has `#pragma GCC optimize("Os")` on the 32-bit ARM build. A change to the SPC700 must keep the hash of `icache_run` equal in both builds (`--filefl snes_spc=-DSNES_SPC_O2`).
- `FAST_OPS` in `snes_cpu.c` holds the cases of `exec_op` that `exec_fast` also runs. Add an opcode only when a trace of a game shows that the assembly loop hands it to C often.
- The run order is not changed by this work: CPU, SPC700 and PPU in the same line, the lazy sound sync, `gsu_sync_pos`, `dsp_sync_pos`, the HDMA and IRQ times.

### Open work

- Pi: read the table above (the numbers are not read yet).
- Run order, not done. The PPU draws line L right after the CPU runs line L, so the two evict each other 224 times a frame. To draw several lines in a row, the PPU registers of every line must be saved first (item C6). The SPC700 and the S-DSP alternate every 16 SPC700 cycles (`spc_run`), so that a KOFF pulse is not lost. A lazy S-DSP that runs only before a register access needs a proof against `tests/snes/dsp_ref.cpp` first (ares, `make test-snes-audio-ref`).
- `exec_op` still holds about 100 opcodes (BIT with a memory operand, TSB, TRB, the indirect modes). Count them in a real play (`tests/snes/ophist.c`, host build) and move the frequent ones into `FAST_OPS` or into the assembly loop. A cold opcode costs a full `flush` and `reload` of the registers.
- The S-DSP uses the most ARM instructions of the sound part (1.5 million a frame, mostly `voice_run`). An idle voice should cost less.
- Hot and cold splitting of the opcode switches with GCC profile feedback (`-fprofile-use` with a committed profile) was not tried.
