# bm SNES GPU: stage 1 on the Pi, the numbers to read and the plan for G3 to G6

Part of [snes-gpu.md](snes-gpu.md) (items G1 to G11, rules and open work are there).

## Stage 1 (2026-11-13): what runs in the kernel

Status: built and tested on the PC (V3D emulator and QEMU build). Nothing is measured on the Pi yet. Default off.

- Config key `snes_gpu_m7=1` (Pi only; the RGB30 and the host never see the code). At boot `snes_gpu_enable` (`src/kernel/snes_gpu.c`) starts the V3D, loads `qp_snes_mode7` (`src/gpu/qpu_snes.c`) and runs a probe: a synthetic Mode 7 picture of 24 lines, QPU against the ARM version. If the probe fails, the log says why and the ARM PPU draws everything.
- `snes_ppu.c` keeps the lines of a frame that show BG1 Mode 7 alone (`bgmode` 7, `tm` = 1, no window on BG1, no colour math, no clip rule). It writes X and Y of the line (the ARM set-up of `draw_mode7`) in a table. A run of lines ends, and the QPUs draw it, when the matrix words or `m7sel` change, the palette or the brightness change, the video RAM is written, the lines are not in sequence (forced blank), or the frame ends (`ppu_frame_end` in `snes_bus.c`). Every other line goes through the normal ARM path (rule G8: the ARM picture is the reference and the fallback).
- A copy of the 64 KB of video RAM is made only when `ppu_vram_dirty` was called since the last copy (a Mode 7 game that writes VRAM only in the blank pays the copy once or never).
- `qpu_snes_m7` runs the lines in up to 12 jobs. If a run fails (timeout, queue error), the ARM version of the same lines draws them from the same buffers (the picture is correct). Two failed runs in a row turn the QPUs off for the session. A run skipped because a 3D job runs is not a failure.
- Log: the `snes t=` line has, after the `ppu us:` part, ` | gpu_m7 job N us copy N us frames gpu N arm N lines gpu N arm N copies N`. `job` is the time of the QPU run of a frame (average over the frames the GPU drew, 2 samples a step), `copy` is the 64 KB copy. `frames gpu` counts rendered frames with at least one GPU line, `frames arm` the other rendered frames. `lines arm` counts lines that the ARM version drew after a failed run. The `ppu` time of the line no longer holds the lines the GPU takes: read `job` + `copy` as their cost and `frame` for the sum.
- Reasons (second `gpu_m7` field): ` | gpu_m7 seen lines N runs N kept by arm: other-bg N sprites N window N math N`. `seen lines` are Mode 7 lines with the helper on; `runs` are QPU runs; the other four count Mode 7 lines that stayed on the ARM, the first failing rule wins (order: other BG or BG1 off, sprites, window on BG1, colour math or clip rule). All are cumulative. First Pi log (SMW2, `m7/11/00`): `tm` = $11, so the sprites rule keeps the scene on the ARM. Mode 7 with sprites is the common case (SMK, SMW2, F-Zero): the next real step is a path with the sprites (G4 plus the BG1 index, so that the ARM can lay the sprites over the GPU picture).
- 3D Bench: with `snes_gpu_m7=1` the report (and the kernel log) gets one line `snes gpu bench: ...` from `qpu_snes_bench`. The numbers are in the table below.
- Tests: `make test-snes-g2` (the 56 states of G2, `test_snes_gpu 0` with the states as whole frames, frames with changes in the middle (HDMA on the matrix, palette, brightness, video RAM, forced blank, sprites, colour math, a Mode 1 end), the micro-benchmark programs on the emulator, and a Mode 7 ROM when `BM_SNES_ROMS` holds one; `test_snes_gpu 1` QPUs that stop; `test_snes_gpu 2` QPUs that store wrong data), `make test-snes-ppu`, `make test-snes`, `make test-qpu` (the generated header is current).
- Measured on the PC: the program is 143 instructions for each block of 32 pixels (256732 instructions for 224 lines). No time of the Pi is known.

### The numbers to read on the Pi

| Where | Field | What it decides |
|---|---|---|
| 3D Bench row | `job` | Fixed cost of one QPU job (launch, whole-cache clean and invalidate, wait). Above 1000 us: every GPU pass must be a single job a frame. |
| 3D Bench row | `jobs12` | The same with 12 jobs (what Mode 7 launches). |
| 3D Bench row | `copy64k` | The ARM copy of the video RAM for the V3D. Above 400 us: G7 needs a list of changed ranges. |
| 3D Bench row | `gather64k` and `gather8M` ns/word | Time for a word that one lane gathers, all 12 QPUs together. Mode 7 reads 170000 words: its QPU time is about `jobs12` + 170000 x `gather64k`. |
| 3D Bench row | `stream` MB/s | Sequential reading of SDRAM by the QPUs. |
| 3D Bench row | `arm chunks/ms` alone, qpu-l2, qpu-sdram, `arm-slowdown` | How much work the ARM does with no QPU job, with the QPUs busy in the L2 cache, and with the QPUs reading SDRAM. The slowdown is the loss of the second against the first. Above 25%: the ARM loses what the GPU saves, so stop after Mode 7. |
| `snes t=` line | `gpu_m7 job`, `copy`, `frames gpu/arm`, `lines gpu/arm` | The real gain of Mode 7 on a Mode 7 scene: compare `frame` (and `fps`) with `snes_gpu_m7=0`. |

How to run (the user): put `snes_gpu_m7=1` in `bm/config.txt`, install `dist/kernel.img` with `./easy_install.sh`, boot, run the 3D Bench once (its row and the report `bm/bench/3dNNNN.txt` hold the line), then play a Mode 7 scene (Super Castlevania IV rotating room, F-Zero, Super Mario Kart) for 20 seconds with Log on and send the `snes t=` lines. Then do the same with the key off, same scene, for the A/B. If the boot log says `snes gpu: Mode 7 on the QPUs is off (...)`, send that line.

Risk: the V3D start changed the boot of the Pi before (see `v3d-flags.md`; an off/on power cycle of the QPUs hung `probe_index`). The key is the guard: with the key off nothing starts.

## Pi numbers and decision (2026-11-13, Pi Zero W, kernel v0.3.0-112)

3D Bench row, run by the lead session through the bridge: `job 4 us, jobs12 5 us, copy64k 186 us, gather64k 8 ns/word, gather8M 17 ns/word, stream 2904 MB/s, arm chunks/ms alone 51 qpu-l2 47 qpu-sdram 46, arm-slowdown 10%`.

| Gate | Value | Result |
|---|---|---|
| `job` above 1000 us | 4 us | passes (the estimate was 300 us) |
| `copy64k` above 400 us | 186 us | passes |
| `arm-slowdown` above 25% | 10% | passes |
| Mode 7 gain per frame, at least 1.5 ms | 3.7 ms (ARM) - (5 us + 170000 x 8 ns + 186 us) = about 2.1 ms | passes, as an estimate (not measured in play) |
| G5 gate, `gather64k` under 2.7 ns/word | 8 ns/word | FAILS: Mode 1 on the GPU would take about 9 ms against `bg` 3 to 4.5 ms on the ARM |

Decision (step 1): the Mode 7 path stays (key `snes_gpu_m7`, default off). G5 and G6 are not built as planned: the gather speed is three times too slow. They return only with a different way to read the tiles (for example the tiles decoded into the 4 bpp texture format, or a cache in the QPU's own memory). G3 stays useful only for Mode 7 with HDMA on the matrix.

Play test, SMW2 (kernel 112, 76 log lines): the GPU drew 0 frames. The Mode 7 scene has `tm=$11` (BG1 and sprites) and stage 1 takes BG1 alone, so every line stays on the ARM. SMW2, Mario Kart and F-Zero have sprites in Mode 7. A frame drawn by the GPU is not measured yet: it needs a Mode 7 scene without sprites (Super Castlevania IV, the rotating room).

Next version (the user keeps it as a to-do, 2026-11-13): the QPU draws BG1 and gives the colour index of each pixel, and the ARM lays the sprites over it (G4 with a smaller scope), so Mode 7 scenes with sprites use the GPU. Build it only after a pure Mode 7 scene shows the real gain in play.

## Plan for G3 to G6 (gates use the numbers above)

All steps keep: the ARM PPU is the reference, `make test-snes-ppu` states compare byte for byte, no frame of delay (G7 only after a measured latency cost), RGB30 and host stay on the ARM.

1. Decision after the Pi numbers. Mode 7 gain per frame = `ppu` ARM Mode 7 time (3.7 ms) - (`job` + `copy`). If the gain is under 1.5 ms, or `arm-slowdown` is above 25%, the GPU path stops at G2 and the code stays as an option. Otherwise go on.
2. G3, register log for each line. Today a run ends when a register changes, so an HDMA effect on the matrix (F-Zero, Mario Kart) gives one job per change. Build: a per-line array of the registers that the shader reads (matrix words, `m7sel`, scroll, `inidisp`), filled by `ppu_line` instead of ending the run. The QPU program reads `a` and `c` from the table (two more words a line) in place of uniforms. Gate: the mid-frame states of `test_snes_gpu` with an HDMA on every line equal the reference; one job per frame whatever the HDMA does. Cost to compare: `jobs12` x number of runs against one job.
3. G4, sprite lists. The ARM walks the OAM (branchy, 128 entries) and gives each line up to 32 sprites with x, tile address, palette, flips and priority; the QPU program draws them. Build only when the Pi shows `obj` above 1.5 ms in the target scene. Gate: the sprite-limit states of `ppu_equiv` (32 sprites, 34 tiles) equal the reference. Read `gather64k` first: a sprite pixel costs 2 gathers.
4. G5, Mode 1 (BG1 to BG3 with the priority lists of `ord1`). One program for the three layers and the sprite list result of G4: per pixel the tile map and the tile data of each layer, then the priority pick. Build only if `gather64k` x (about 20 words a pixel x 57344 pixels = 1.1 million) is under 3 ms, i.e. `gather64k` under 2.7 ns/word, against `bg` 3 to 4.5 ms on the ARM. Gate: Mode 1 states equal the reference; Super Mario World frames from the ROM test.
5. G6, colour math and windows. The compose step and the mix move into the same program. Build after G5, because colour math needs the layers as an input. Gate: all of `test-snes-ppu` equals the reference on the GPU path. Compare with `comp` + `mix` (2.5 to 4 ms on the ARM).
6. G7 (pipeline) and G11 (the GPU writes the target) stay behind a measured latency cost: do not enable G7 without a measurement of the input-to-picture time on the Pi.

If any step fails its gate on the Pi, its frames go back to the ARM by the G8 rule that stage 1 already has for Mode 7.
