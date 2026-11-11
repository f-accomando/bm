# bm SNES: the time of a frame in its parts (C1)

Work-item codes are in [snes-cpu.md](snes-cpu.md). Figures: Pi Zero W, estimates marked as such.

Done 2026-11-11. `snes_frame` (`src/bm/snes_bus.c`) times the parts of a frame with the console timer and averages them. A timer read costs about 6 us on the Pi and the first version read it 3 times a line in every frame (about 4.7 ms a frame, a third of it): now one frame in 7 is timed (7, not 8: a multiple of 8 is always or never a frame with a picture at `pic 1/2` and `1/4`), the cost of one read is measured at the start of that frame and taken off each part, and the other frames read no clock (the instruction count runs in every frame).

Fields of `snes_debug().us`: `frame` (the whole `snes_frame`); `cpu` (the 65C816 with the bus, from the end of the last line's picture to the start of the sound, so it holds the HDMA and the DMA); `dma` (the general DMA at $420B, inside `cpu`); `hdma` (the HDMA alone, inside `cpu`); `ins` (instructions run in a frame); `spc` (the SPC700 and the S-DSP); `ppu` (`ppu_line` of the lines drawn, averaged over the timed frames that made a picture: the cost of a drawn frame). CPU and bus alone = `cpu` - `dma` - `hdma`. Check: `tests/snes/core_test.c` (`timing_tests`, a fake clock).

**Result: third play**, Pi Zero W, 2026-11-11, SMW, native, 93 s, kernel `v0.3.0-12-g4a91a2f5`, timer sampled one frame in 7 with the read cost taken off, `ppu` per drawn frame (log `reports/bm-core/20261111-130841_log_pi-zero-w_v0.3.0-12-g4a91a2f5.txt`), ms a frame:

| `pic` | s | cpu (dma, hdma) | ppu (drawn frame) | spc+dsp | ins | us/ins | fps | update (Lua) | late |
|---|---|---|---|---|---|---|---|---|---|
| all | 93 | 3.3 (0.2, 0.0) | 9.1 | 1.8 | 11804 | 0.28 | 59.4 | 9.3 | 0.0 |
| 1/2 | 67 | 3.3 | 9.6 | 1.8 | 11858 | 0.28 | 59.9 | 10.0 | 0.0 |
| 1/3 | 26 | 3.1 | 8.0 | 1.6 | 11666 | 0.27 | 58.1 | 7.4 | 0.1 |

CPU and bus alone: 3.0 ms (max 3.7 ms in a second), 0.28 us an instruction (about 280 cycles at 1000 MHz). PPU of a drawn frame: median 9.6 ms, 90th percentile 10.2 ms, heaviest second 16.8 ms. Sound and DSP 1.8 ms. Controller: `pic` 1/2 or 1/3, 4 changes in 93 s; fps 59.4 and `late` 0.0. Reading: the PPU costs three times the CPU and the sound as much as half the CPU; a drawn frame at 1/2 is about 3.3 + 9.6 + 1.8 + 3.3 (draw) = 18 ms of two frames' 33 ms. The CPU is on the limit of the rule (about 3 ms): C2 to C5 free at most 2.4 ms of 16.7 ms. The PPU (G1) comes first. The earlier plays (old timer; a biased sampled one) gave CPU 3.2 and 3.3 ms: the same.

First play (old timer, 3 reads a line in every frame; log `reports/bm-core/20261111-115300_log_pi-zero-w_v0.2.5-2272-g99565f62.txt`):

| `pic` | s | frame | cpu (dma, hdma) | ppu | spc+dsp | ins | us/ins | fps |
|---|---|---|---|---|---|---|---|---|
| all | 86 | 8.6 | 3.2 (0.2, 0.0) | 3.7 | 1.7 | 11810 | 0.27 | 59.3 |
| 1/2 | 32 | 10.0 | 3.2 | 5.0 | 1.8 | 11844 | 0.27 | 60.2 |
| 1/3 | 54 | 7.8 | 3.2 | 2.9 | 1.7 | 11790 | 0.27 | 58.9 |

CPU and bus alone: 2.98 ms (35% of the measured frame). A drawn frame has the PPU at about 10 ms (1/2) or 8.7 ms (1/3), 9.2 ms in the heaviest second. The controller never went to 1/1 (8 changes of `pic` in 86 s). These figures hold the timer's own cost (about 1.5 ms in each part, estimate), so the real CPU is lower. Open: play again with the sampled timer and replace the table. Decision rule: if CPU and bus alone are under about 3 ms, C2 to C5 do not pay (a JIT 5 times faster frees about 2.4 ms of 16.7) and the PPU (G1) comes first.


## G1: the parts of the PPU (fourth Pi Zero W play, kernel v0.3.0-18, Yoshi's Island and Super Mario World, 126 log lines)

Each part is the mean for a drawn frame, in ms, with the cost of the timer reads taken off. The five parts add up to the PPU time (9.36 ms against 9.41 ms, median of 110 lines with a drawn frame).

| Part | Median | 90th percentile | Share |
|---|---|---|---|
| bg (layers) | 4.0 | 4.3 | 43% |
| mix (colour math and the line write) | 2.8 | 5.2 | 29% |
| obj (sprites) | 1.5 | 1.5 | 16% |
| comp (priority list) | 0.9 | 1.2 | 10% |
| win (windows) | 0.1 | 0.2 | 1% |

Reading: the layers (`draw_bg`) are the largest part and `mix` the second; `mix` moves most (0.6 ms in a quiet scene, 7.9 ms at most). A shader for the layers (G5) and the mix (G6) would take 72% of the PPU time. The sprites (G4) are 16%. Next: one play per scene (Mode 7, a level with HDMA, a title screen) to see which part grows.
