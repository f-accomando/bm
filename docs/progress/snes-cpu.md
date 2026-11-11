# bm SNES: exploration of speed (CPU core, other emulators)

Status: notes from a talk on 2026-11-11. Nothing is built. Pi Zero W only (ARMv6, 32 bit). The numbers are estimates, not measurements.

### Work items (development codes)

Status of every item: idea, not started, except C1 (code done 2026-11-11, the table of the Pi waits for a play with the logger). Codes: C = CPU and system, G = PPU on the GPU. Gate = what must pass before the item counts as done. Detail of G items: [snes-gpu.md](snes-gpu.md).

P = priority 0 to 9 (0 is the highest). T = time to do it 0 to 9 (9 is the longest). Both are estimates (2026-11-11), not measures.

| Code | Item | P | T | Gate |
|---|---|---|---|---|
| C0 | Find the cause of the uneven picture rate on the Pi One (see "Open work" in [snes.md](snes.md)) | 0 | 2 | FPS line read in the scenes that drop |
| C1 | Measure CPU and bus time apart from the PPU and the DSP (test build) | 0 | 2 | a table of ms a frame for each part (done 2026-11-11: CPU and bus 3.0 ms a frame, see [snes-measure.md](snes-measure.md); C2 to C5 free at most 2.4 ms) |
| C2 | Registers in ARM registers, inside the interpreter | 4 | 4 | `make test-snes-cpu`, `make test-snes`. Code done 2026-11-11 (see "C2: profile and result"); the time on the Pi waits for the logger line `cpu` |
| C3 | Threaded interpreter in assembly (each opcode jumps to the next) | 3 | 5 | same tests, and a timing against C1 |
| C4 | JIT of basic blocks, key `(PC, M, X, E)`, A in the top half of a register, flags in the CPSR | 5 | 9 | same tests, and a timing against C1 |
| C5 | Static recompiler for one game (a tool that reads the ROM) | 8 | 9 | same tests, and the game picture byte for byte |
| C6 | Writes to PPU registers queued as events (clock, register, value) | 6 | 5 | `make test-snes`, `make test-snes-ppu` |
| C7 | High-level SPC700 (low value: about 2 to 3% of a core) | 9 | 4 | `make test-snes-dsp`, sound by ear |

## C2: profile and result

Done 2026-11-11 (code and host tests). Profile, change, numbers and what was left out: [snes-cpu-c2.md](snes-cpu-c2.md). The time on the Pi waits for the logger line `cpu`.

## C1: the measure

Done 2026-11-11 (code and first figures of the Pi Zero W): see [snes-measure.md](snes-measure.md).

## Why a ROM cannot be translated once

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

The conversion of a ROM to ARM (items C4 and C5) is in [snes-convert.md](snes-convert.md).

The PPU and the GPU ideas are in [snes-gpu.md](snes-gpu.md).
