# bm SNES: exploration of the PPU on the GPU

Status (2026-11-13): G1 done; G2 runs in the kernel behind the key `snes_gpu_m7` (default off) and in a V3D micro-benchmark, tested on the PC only: see [snes-gpu-pi.md](snes-gpu-pi.md). Nothing is measured on the Pi. Pi Zero W only (V3D). The figures in the design notes are estimates, not measurements. Work-item codes C0 to C7 are in [snes-cpu.md](snes-cpu.md).

Start point: the "native 256x256" mode (no GPU scale). The GPU would write the 256x224 RGB565 picture itself.

## The PPU has no opcodes

Status: notes from a talk on 2026-11-11. Nothing is built. The figures are estimates.

- The CPU runs a program, so its instructions can be translated. The PPU is fixed-function hardware. The game writes registers, VRAM, OAM and CGRAM, and the PPU makes pixels from them. There is nothing to map to ARM opcodes.
- The work is to find, for each line, the tile under each pixel of each layer, the winning sprite, the priority, the windows, the colour math and the brightness (`ppu_line` in `snes_ppu.c`).
- The real SNES has two PPU chips (PPU1 and PPU2). The repo has one `snes_ppu.c` for both. The chip names are from memory: check a public source before you quote them.
- "Closer to the metal" means to use units that do this kind of work. The ARMv6 SIMD (UADD8, SEL, UQADD8, UQSUB8, UHADD8) is in use already.

### Ideas (GPU V3D of the Pi)

The kernel drives the V3D already ([v3d.md](v3d.md): textured fill 1.65 ns a pixel, a QPU driver that passes on the Pi Zero W). Today the GPU only scales the finished picture (GPU 2x and 3x).

1. Each layer as a texture: a BG is a quad, sprites are small quads, the palette is a lookup texture. Mode 7 fits best: it is an affine transform of a texture.
2. Colour math as blending. The SNES colour math depends on the sub screen, the windows and one bit a pixel, so the standard blend is not enough without a shader.
3. A `ppu_line` in QPU code (`qpuasm.py` exists and a test passes).
4. The DMA of the Pi for copies of line buffers. It helps only for copies.

Risks: HDMA changes scroll, mode and colour on each line (224 strips for the GPU); the sprite limits (32 sprites and 34 tiles a line); four interleaved priority levels (many passes); the exact result (`make test-snes-ppu` compares byte for byte, a GPU path needs the same gate); the fixed cost of one GPU job (about 0.3 ms for a fill).
Best first candidate: Mode 7. A full GPU path is a project on its own. Measure first which part of `ppu_line` costs most in which game: the test build prints `bg`, `obj`, `win`, `comp` and `mix` apart.


## The PPU is a fixed-function GPU

It makes the picture of a SNES, so it plays the role of a GPU. It is not a modern GPU: the game cannot load programs into it, it draws tiles and sprites a line at a time while the signal goes to the TV, and it has its own 64 KB of VRAM. A modern GPU (the V3D of the Pi) runs shaders and draws a whole frame to a buffer.

## Idea: V3D as a co-processor, like the Super FX

Today the PPU runs on the ARM (`snes_ppu.c`, SIMD and assembly). The V3D only scales the picture in GPU 2x and 3x. The ARM and the V3D share the same RAM, so the GPU can write the RGB565 buffer directly. The draw path costs 5.7 ms at 1x (copy 1.0, blit 4.6, from [snes.md](snes.md)), and part of it could go. Not verified.

Design sketch:
- One full-screen shader does what `ppu_line` does for each pixel: tile, map, palette, priority and colour math. VRAM, CGRAM and OAM are buffers the GPU reads. The kernel has a VS, an FS2 and an assembler (`tools/qpuasm.py`).
- A register log for each line fixes HDMA. While it emulates, the ARM saves the PPU registers that changed on each line. The shader reads the settings of its own line. No strips are needed.
- The ARM does the branchy part: it evaluates the OAM into a list of up to 32 sprites a line. The GPU does the work for each pixel.
- Pipeline with one frame of delay: the ARM emulates frame N+1 while the GPU draws frame N. It needs a copy of the VRAM (64 KB) for each frame.
- Fallback for each frame: a frame that uses a feature not yet ported goes to the ARM PPU, which stays the reference.
- A test on the PC: `tests/gpu/v3d_emu.c` emulates the V3D. Compare the shader with `tests/snes/ppu_ref.c` byte for byte, as `make test-snes-ppu` does.

Napkin maths, with large error: 57344 pixels with about 20 texture reads each is about 1.1 million reads a frame. With about 6 reads a cycle at 250 MHz this is about 1 ms, plus the fixed cost of a job (about 0.3 ms). The PPU costs about 7.7 ms on the ARM today. The limits are more likely latency, cache coherence and long shaders with many branches. The assumptions are mine. Not measured.

| Mode | Chance of a gain | Reason |
|---|---|---|
| Mode 7 | high | affine transform for each pixel, maps to a texture |
| Mode 1 (the most common, used by Super Mario World) | good | 3 tile layers, regular |
| Modes 0, 2, 3, 4 | medium | 4 layers, 8 bpp, offset-per-tile (modes 2 and 4) needs dependent reads |
| Modes 5, 6, interlace, mosaic | low | hires and special cases: leave them to the ARM |

A gain in every mode is not realistic. Go step by step: Mode 7 and Mode 1 first, the ARM for the rest. The RGB30 has a Mali GPU and needs its own work ([graphics-mali.md](graphics-mali.md)).

## Work items (development codes)

Status of every item (2026-11-13): G1 done; G2 built in the kernel behind `snes_gpu_m7=1` and measured with the 3D Bench on the Pi (job 4 us and copy 189 us pass the gates; the second bench run gives: ARM slowdown 63% with QPU reads over SDRAM and 6% over the 64 KB, gather 12 ns/word, Mode 7 gain about 1.5 ms (on the gate); the gain in play is not measured, because SMW2 has sprites in its Mode 7 scene and the GPU drew no frame); G8 done for Mode 7 lines only; G5 and G6 fail the gather gate (12 ns/word, need 2.7) and wait for another way to read tiles; G4 in a smaller scope (the ARM lays the sprites over the GPU picture of BG1) is the next version, to do; G3, G7, G9, G11 are ideas, not started. G10 waits for a decision (ARM or GPU). Nothing here is dropped.

P (priority) and T (time) are explained in [snes-cpu.md](snes-cpu.md).

| Code | Item | P | T | Gate |
|---|---|---|---|---|
| G1 | Measure which part of `ppu_line` costs most in which scene (`bg`, `obj`, `win`, `comp`, `mix`). Done: the logger prints them as averages of a drawn frame; tables of Super Mario World (layers 43%, colour math 29%, sprites 17%, priority list 11%) and of Super Castlevania IV in `snes-measure.md`. Left: Mode 7 and HDMA scenes of SMW | 0 | 2 | a table for the scenes that drop |
| G2 | Mode 7 on the QPUs. Done on the PC and in the kernel behind `snes_gpu_m7=1`; the Pi bench passes the gates (see [snes-gpu-pi.md](snes-gpu-pi.md)); the gain in play waits for a scene without sprites | 3 | 6 | byte for byte against `ppu_ref.c` |
| G3 | Register log for each line (fixes HDMA) | 3 | 6 | same, with a test ROM that uses HDMA |
| G4 | Sprite lists for each line made by the ARM and given to the GPU | 4 | 6 | same, with the sprite limits (32 sprites, 34 tiles a line) |
| G5 | Mode 1 shader (BG1 to BG3, sprites) | 4 | 8 | same, Super Mario World frames |
| G6 | Colour math and windows in the shader | 5 | 8 | same, `make test-snes-ppu` states |
| G7 | Pipeline with a frame of delay and a VRAM copy | 7 | 7 | no tearing; the time of a frame measured on the Pi |
| G8 | Fallback to the ARM PPU for each frame. Done for Mode 7: a line with sprites, windows, colour math or another mode goes the ARM path; a failed QPU run gives the ARM version of the lines. Left: the rule for each new GPU path | 5 | 5 | a frame with an unported feature gives the ARM picture |
| G9 | Modes 0, 2, 3, 4 (offset-per-tile) | 8 | 8 | same |
| G10 | Modes 5, 6, interlace, mosaic: decide if they stay on the ARM | 8 | 1 | a written decision |
| G11 | The GPU writes the 256x224 target directly, no blit (native mode only) | 7 | 5 | the draw cost measured before and after |

## G2 result (prototype, PC only)

Status: done on the V3D emulator; the kernel path is in [snes-gpu-pi.md](snes-gpu-pi.md). Nothing runs on the Pi.

- Files: `tools/qpusnes.py` (the QPU program `qp_snes_mode7`), `tests/gpu/test_snes_m7.c`, and `make test-snes-g2`. The program is in `src/gpu/qpuprog_snes.h` (made by `tools/qpusnes.py`, checked by `make test-qpu`).
- What it does: the picture of Mode 7 for a frame with only BG1 on. Each of 16 lanes makes two pixels of a block of 32. The ARM does the set-up of each line (X and Y at the start of the line, as `draw_mode7` does). The `mul24` of the QPU is unsigned and has 24 bits, so the 16-bit by 13-bit products of the set-up stay on the ARM. In the loop `a * xx` is `(a + 32768) * xx - (xx << 15)`.
- Gate: 56 states give the same 57344 pixels as `ppu_line` of `ppu_ref.c`: identity, scroll, rotation, zoom out to the corner, mirrors, the four screen-over modes (wrap, wrap, clear, tile 0), the largest and the smallest matrix words, and 40 random states with any 16-bit matrix (16 fixed states, `make test-snes-g2` runs them and the 40).
- What the QPU needs from the ARM: the 64 KB of video RAM in memory the V3D reads (a copy for each frame, or a change list), the colour table `lut` of `ppu_line` (256 halves, brightness applied), two words for each line, and the matrix words. The VRAM tile map (even bytes) and the 8-bit tiles (odd bytes) are read as they are.
- Cost: 143 instructions for each block of 32 pixels (1792 blocks, 256732 instructions a frame) and 8 TMU reads for each block (6 gathers of 16 words, 2 shared words). The count is of instructions, not of cycles. Estimate for the Pi (not measured): 12 QPUs at 250 MHz give about 90 microseconds of instructions; the 170000 words of the gathers and the fixed cost of a job (about 0.3 ms) put the frame between 0.5 and 1 ms. The Mode 7 layer costs 3.7 ms on the ARM (Super Castlevania IV, `snes-measure.md`), so the gain could be 2.5 ms a Mode 7 frame, less the copy of the VRAM.
- Left out: sprites, windows, colour math, HDMA (G3, G4, G6), the EXTBG mode, mosaic, direct colour, the layer as an input of the compose step (the shader gives the final colour of a frame with BG1 alone). A real path needs the layer with its priority (G6, G8).
- Value: Super Mario World uses Mode 7 only in a few boss fights, so G2 helps few scenes there. The Mode 7 games gain most. The 72% of the PPU time in Super Mario World is Mode 1 (layers and colour math): G5 and G6.

Order: G1, G2, G3, G4, G5, G6, G8, G7, G11, then G9 and G10.

## Stage 1 and the plan for G3 to G6

The kernel path, the log fields, the numbers to read on the Pi and the plan for G3 to G6 with their gates are in [snes-gpu-pi.md](snes-gpu-pi.md).

## Rules (do not break)

- The ARM PPU (`ppu_line`) is the reference and the fallback of every GPU path. Every GPU picture equals it byte for byte (`make test-snes-g2`, `make test-snes-ppu`).
- The key is off by default. With the key off nothing starts the V3D for the SNES. The RGB30 and the host never contain GPU code of the PPU (`src/kernel/snes_gpu.c` and `src/gpu/` are Pi only).
- A failed QPU run draws the same lines with the ARM version. Two failed runs in a row turn the QPUs off for the session.
- No frame of delay (G7) without a measured latency cost.
- The ARM keeps the registers, the video RAM and the timing. The GPU never changes the emulated behaviour.

## Open work

- Install the kernel with the reject counters (25f65724 on) and play a Mode 7 scene without sprites (Super Castlevania IV): read `frames gpu`, `job`, `frame` and `fps` against `snes_gpu_m7=0` (steps in [snes-gpu-pi.md](snes-gpu-pi.md), "Pi numbers and decision").
- Next version (to do, user 2026-11-13): sprites over the GPU picture of BG1, so Mode 7 scenes with sprites (SMW2, Mario Kart, F-Zero) use the GPU. After the play measure above.
- G5 and G6 wait for a faster way to read the tiles (the gather gate failed).
- G7, G9, G10, G11: unchanged (ideas).
