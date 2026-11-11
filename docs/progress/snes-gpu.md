# bm SNES: exploration of the PPU on the GPU

Status: notes from a talk on 2026-11-11. Nothing is built. Pi Zero W only (V3D). The figures are estimates, not measurements. Work-item codes C0 to C7 are in [snes-cpu.md](snes-cpu.md).

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

Status of every item: idea, not started.

P (priority) and T (time) are explained in [snes-cpu.md](snes-cpu.md).

| Code | Item | P | T | Gate |
|---|---|---|---|---|
| G1 | Measure which part of `ppu_line` costs most in which scene (`bg`, `obj`, `win`, `comp`, `mix`). Done: the logger prints them as averages of a drawn frame; first table in `snes-measure.md` (layers 43%, colour math 29%, sprites 16%, priority list 10%). Left: one play for each scene (Mode 7, HDMA, title) | 0 | 2 | a table for the scenes that drop |
| G2 | Prototype: Mode 7 shader for one frame, checked in `v3d_emu.c` | 3 | 6 | byte for byte against `ppu_ref.c` |
| G3 | Register log for each line (fixes HDMA) | 3 | 6 | same, with a test ROM that uses HDMA |
| G4 | Sprite lists for each line made by the ARM and given to the GPU | 4 | 6 | same, with the sprite limits (32 sprites, 34 tiles a line) |
| G5 | Mode 1 shader (BG1 to BG3, sprites) | 4 | 8 | same, Super Mario World frames |
| G6 | Colour math and windows in the shader | 5 | 8 | same, `make test-snes-ppu` states |
| G7 | Pipeline with a frame of delay and a VRAM copy | 7 | 7 | no tearing; the time of a frame measured on the Pi |
| G8 | Fallback to the ARM PPU for each frame | 5 | 5 | a frame with an unported feature gives the ARM picture |
| G9 | Modes 0, 2, 3, 4 (offset-per-tile) | 8 | 8 | same |
| G10 | Modes 5, 6, interlace, mosaic: decide if they stay on the ARM | 8 | 1 | a written decision |
| G11 | The GPU writes the 256x224 target directly, no blit (native mode only) | 7 | 5 | the draw cost measured before and after |

Order: G1, G2, G3, G4, G5, G6, G8, G7, G11, then G9 and G10.
