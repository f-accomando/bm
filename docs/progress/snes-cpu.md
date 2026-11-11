# bm SNES: exploration of speed (CPU core, PPU, other emulators)

Status: notes from a talk on 2026-11-11. Nothing is built. Pi Zero W only (ARMv6, 32 bit). The numbers are estimates, not measurements.

### Why a ROM cannot be translated once

- The length of an instruction depends on the flags M and X, and the game changes them at run time. The bytes `A9 34 12` are `LDA #$1234` with M=0 and `LDA #$34` followed by another instruction with M=1.
- Games jump to computed addresses (`JMP ($xxxx,X)`, an address on the stack). The sound program is loaded into the SPC700 RAM at run time.
- Each `STA $21xx` is a write to a register with side effects. A translated program must still call the bus and count the clocks.

### How one instruction runs today

`LDA $10,X` with 16-bit A goes through `cpu_step`: check NMI, IRQ, WAI and STP, add 6 clocks, `fetch8` (page lookup), the `switch` on the opcode, `a_dpx()` (direct page + X, wrap in emulation mode), `readM` (test of `fM`, two `rd8`, two page lookups), then store in `rA` and set `fN` and `fZ` (separate ints in memory).
Estimate: 50 to 100 ARM instructions for one 65C816 instruction. The first guess of 40 to 60 was low. The 100 comes from the Pi One frame times in "Open work" (the CPU part is about 4 to 5 ms of a 22 ms frame, about 20000 to 25000 instructions a frame). It is an inference, not a count.

### Ideas and estimated cost (ARM instructions for one 65C816 instruction)

| Idea | Estimate | Gain | Note |
|---|---|---|---|
| Today (C interpreter) | 50 to 100 | none | switch, address maths, one lookup a byte, flags in memory |
| 1. Registers in ARM registers, inside the interpreter | 35 to 70 | 20 to 30% | C cannot pin A, X and Y; full gain needs generated code |
| 2. JIT of basic blocks | 6 to 12 | 5 to 10 times | simple op 3 to 8; a variable address +5 for the page lookup; a block adds 10 to 15 (dispatch, clock, IRQ check) |
| 3. Static recompiler for one game (a tool that reads the ROM) | 4 to 8 | 8 to 15 times | no translation at run time; knows D and DB; drops unused flags; covers only traceable code, the rest stays in the interpreter |
| 4. Writes to PPU registers queued as events (clock, register, value) | 120 to 150 down to 4 to 6 | about 25 times for that write | few writes a frame; the PPU work stays |
| 5. High-level SPC700 | small | about 2 to 3% of a core | the S-DSP costs more (about 2.7 ms a frame) |

Example, `LDA $10,X; ADC #1; STA $20`: about 150 to 300 ARM instructions today. A JIT block is about 7 instructions: the 16-bit A lives in the top half of a register (`A<<16`), so `ADDS` sets N, Z, C and V as the 65C816 does (the carry of `SBC` also matches), the work RAM is read from a fixed base register, and the clocks of the block are added once.

### Facts that help

- The kernel runs with no operating system. `bmlc.c` and `bmnative_load()` already write ARMv6 code to memory and invalidate the I-cache, so a JIT needs no new loader.
- A block key must be `(PC, M, X, E)`.
- Decimal mode, WAI, STP and rare opcodes stay in the interpreter.

### Real gain per frame

| Part of a frame (Pi One, Super Mario World, test build) | Time |
|---|---|
| PPU | about 7.7 ms |
| CPU and bus (inferred) | about 4 to 5 ms |
| S-DSP | about 2.7 ms |

A JIT that takes the CPU from about 4.5 ms to about 1 ms frees 3 to 3.5 ms of 22 ms (about 15%). Ideas 1, 4 and 5 alone are not worth the work. The gain is headroom for the GPU 2x and 3x modes and for heavy scenes.

### Next steps (when work starts)

1. Measure CPU and bus time apart from the PPU and the DSP in the test build. The 4 to 5 ms figure is a subtraction.
2. Find the cause of the uneven picture rate on the Pi One (see "Open work" in [snes.md](snes.md)). A JIT does not help if the CPU is not the limit.
3. If the CPU is the limit, build a minimal JIT: registers in ARM registers, flags in the CPSR, clocks added once a block, the interpreter as fallback. Gate: `make test-snes` and `make test-snes-cpu` (the golden hashes).

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

## How other emulators did it

From memory, not from the repo. Check the details before you quote them.

| Emulator | Aim | Technique |
|---|---|---|
| ZSNES | speed | CPU and PPU in x86 assembly, whole-line rendering, shortcuts; wrong timing in some games |
| Snes9x | portability, compatibility | C and C++, separate opcode tables for each M and X state (no flag test at run time), renderers specialised by mode |
| bsnes, higan, ares | cycle accuracy | each chip emulated cycle by cycle and kept in step; slow, but exact |

- The speed contest moved: PCs became fast enough for accuracy, and exact emulators won.
- No JIT for the SNES CPU in these emulators, as far as I know. The 65C816 is 3.58 MHz at most and an interpreter is enough on a PC. JITs are common for heavier consoles (PSX, N64) and for weak hosts. The Pi Zero W is a weak host.
- Catch-up (bsnes): a chip runs only when another chip touches it or a result is needed. It is a cousin of idea 4 above (queue the PPU register writes).
- Closest ARM precedent: Cyclone (a 68000 core in ARM assembly, used in PicoDrive). It maps the 68000 flags to the ARM flags, as the `A<<16` and `ADDS` idea does for the 65C816.

## Assembly or C

Pi One, Super Mario World, real PPU time a frame (from "Open work" in [snes.md](snes.md)):

| Step | Time |
|---|---|
| First C version | about 12100 us |
| SIMD in C | about 9800 us |
| Mix loop in assembly | about 9100 us (about 7% less) |
| Tile cache | about 7700 us (about 15% less) |

The algorithm change gave twice the gain of the assembly. Change the algorithm and the data first. Write assembly after.

Assembly helps when:
- The compiler does not make the instruction itself: UADD8, SEL and the other lane instructions (4 pixels at a time).
- A hot loop needs fixed registers: an interpreter with the CPU registers in ARM registers, or a JIT.
- The flags must live in the CPSR. C cannot do this.
- A threaded interpreter (each opcode jumps to the next) can replace the central `switch`, which is an indirect branch. The ARM1176 predicts such branches badly. Not measured here.

Assembly does not help for the bus, the memory map, DMA and IRQ code. It is logic, and the compiler does well there.

Rule that the repo already follows: a C reference, an assembly version, and an equivalence test (`make test-snes-ppu`). A middle path: C intrinsics (`arm_acle.h` has `__uadd8` and the like). They give less register control and are easier to keep.

Next measurement to consider: a threaded assembly interpreter for the CPU, before a full JIT.
