# bm SNES: the time of a frame in its parts (C1)

Work-item codes are in [snes-cpu.md](snes-cpu.md). Figures: Pi Zero W, estimates marked as such.

Done 2026-11-11. `snes_frame` (`src/bm/snes_bus.c`) times the parts of a frame with the console timer and averages them. A timer read costs about 6 us on the Pi and the first version read it 3 times a line in every frame (about 4.7 ms a frame, a third of it): now one frame in 8 is timed, the cost of one read is measured at the start of that frame and taken off each part, and the other frames read no clock (the instruction count runs in every frame).

Fields of `snes_debug().us`: `frame` (the whole `snes_frame`); `cpu` (the 65C816 with the bus, from the end of the last line's picture to the start of the sound, so it holds the HDMA and the DMA); `dma` (the general DMA at $420B, inside `cpu`); `hdma` (the HDMA alone, inside `cpu`); `ins` (instructions run in a frame); `spc` (the SPC700 and the S-DSP); `ppu` (`ppu_line` of the lines drawn, averaged over all frames, so over `pic 1/N` a drawn frame costs N times more). CPU and bus alone = `cpu` - `dma` - `hdma`. Check: `tests/snes/core_test.c` (`timing_tests`, a fake clock).

First play, Pi Zero W, 2026-11-11, SMW, native, 86 s, **with the old timer (3 reads a line, every frame)**, ms a frame (log `reports/bm-core/20261111-115300_log_pi-zero-w_v0.2.5-2272-g99565f62.txt`):

| `pic` | s | frame | cpu (dma, hdma) | ppu | spc+dsp | ins | us/ins | fps |
|---|---|---|---|---|---|---|---|---|
| all | 86 | 8.6 | 3.2 (0.2, 0.0) | 3.7 | 1.7 | 11810 | 0.27 | 59.3 |
| 1/2 | 32 | 10.0 | 3.2 | 5.0 | 1.8 | 11844 | 0.27 | 60.2 |
| 1/3 | 54 | 7.8 | 3.2 | 2.9 | 1.7 | 11790 | 0.27 | 58.9 |

CPU and bus alone: 2.98 ms (35% of the measured frame). A drawn frame has the PPU at about 10 ms (1/2) or 8.7 ms (1/3), 9.2 ms in the heaviest second. The controller never went to 1/1 (8 changes of `pic` in 86 s). These figures hold the timer's own cost (about 1.5 ms in each part, estimate), so the real CPU is lower. Open: play again with the sampled timer and replace the table. Decision rule: if CPU and bus alone are under about 3 ms, C2 to C5 do not pay (a JIT 5 times faster frees about 2.4 ms of 16.7) and the PPU (G1) comes first.

