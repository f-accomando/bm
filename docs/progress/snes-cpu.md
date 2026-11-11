# bm SNES: exploration of a faster CPU core

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
