# bm SNES: the SA-1 co-processor

Back to [snes.md](snes.md). The same kind of chip: [snes-superfx.md](snes-superfx.md). The 65C816 core that runs it: [snes-cpu-ref.md](snes-cpu-ref.md).

The SA-1 is a second 65C816 at 10.74 MHz on the cartridge, with 2 KB of I-RAM, the BW-RAM (the cart RAM, 32 KB to 256 KB), a ROM mapper, a DMA engine with
character conversion, a 16 x 16 math unit, a variable-length bit processor, a timer and interrupt messages between the two CPUs. Games: Super Mario RPG,
Kirby Super Star, Kirby's Dream Land 3, Jikkyou Oshaberi Parodius, others.

## Sources

- The public register descriptions (SNESdev wiki "SA-1 registers") and the SA-1 module of ares (ISC licence, read only, outside this repository). The code is original.
- Snes9x and bsnes are GPL: they may be read, never copied.

## Code

| File | What it holds |
|---|---|
| `src/bm/snes_sa1.c`, `snes_sa1.h` | the chip: registers, I-RAM, BW-RAM access rules, ROM mapper, DMA, math, bit processor, timer, interrupts, the bus of the SA-1 CPU, the S-CPU side of the map |
| `src/bm/snes_sa1_cpu.c` | the second 65C816: `snes_cpu.c` included again with the global names changed (`sa1c_run`, `sa1_rtab`, `sa1_clk`, `sa1_s`). C loop only |
| `src/bm/snes_bus.c` | the hooks: `raw_read`, `raw_write`, `map_ptr`, the IRQ line (`$4211`, `$4200`), the line loop (`sa1_line_end`), the header, the reset |
| `tests/snes/sa1_test.c` | `make test-snes-sa1`: a made-up ROM, an S-CPU program that releases the SA-1, an SA-1 program (math, bit processor, DMA, bitmap window, messages, IRQ both ways) |

## Memory map

- S-CPU: registers `$2200-$23FF`, I-RAM `$3000-$37FF`, BW-RAM at `$6000-$7FFF` (window by BMAPS) and at `$40-$4F:0000-FFFF`, ROM at `$00-$3F` and `$80-$BF:$8000-$FFFF` and at `$C0-$FF`.
- SA-1: registers `$2200-$23FF`, I-RAM at `$0000-$07FF` and `$3000-$37FF`, BW-RAM at `$6000-$7FFF` (BMAP), `$40-$5F` (linear) and `$60-$6F` (4 bit or 2 bit pixels), the same ROM.
- ROM: four 1 MB windows. `$C0-$FF` always follow CXB, DXB, EXB, FXB; the LoROM half follows them only when the mode bit (bit 7) is set; else the 4 MB of LoROM order.
- The SA-1 takes its vectors from registers: NMI from CNV, IRQ from CIV (both at the native and the emulation address), RESET from CRV. The S-CPU reads its NMI and IRQ vectors from SNV and SIV when SCNT bit 4 or bit 6 is set.
- Write protection: SIWP and CIWP protect the I-RAM in 256 byte steps (all protected after a reset: a game sets them). BWPA protects the first `$100 << n` bytes of the BW-RAM only while both SWBE and CWBE are off.

## Time

- The S-CPU runs the frame. The SA-1 runs lazily: `sa1_line_end` at the end of each line, and `sa1_sync` before the S-CPU touches a register, the I-RAM or the BW-RAM.
- The clock of the SA-1 CPU is kept in units of 1/3 master clock, so the 65C816 core runs unchanged: 6 units are one bus cycle (2 master clocks). A BW-RAM access costs two cycles.
- Not emulated: bus conflicts and wait states between the two CPUs, the extra cycle of a jump, the exact moment of the timer IRQ inside a line.

## Status

- Done and tested with made-up programs: registers, both CPUs' view of the map, ROM windows, I-RAM and BW-RAM protection, normal DMA (ROM, BW-RAM, I-RAM), character conversion (code from the register description, no test yet), math (multiply, divide with a remainder that is not negative, accumulate, overflow), bit processor, timer, messages and IRQ and NMI in both directions, S-CPU vector override.
- No commercial game was run: no ROM is on this machine. First step with a real cartridge: look at the title and the first frames, then at `sa1 instructions` in the debug line (not wired yet).

## Open work

- Run Super Mario RPG and Kirby Super Star (the user's ROMs, `BM_SNES_ROMS`); fix what shows.
- Speed: the SA-1 CPU runs the C loop with a function call for each I-RAM and BW-RAM access (about 3 times the work of the S-CPU). Measure on the Pi Zero W and the RGB30. Ideas: fast pages for the I-RAM, the ARM assembly loop for the second CPU, run the SA-1 only when it is not waiting.
- A differential test against ares for the math unit, the bit processor and the two character conversions (like `tests/snes/ref_diff.cpp` for the 65C816).
- Tests for character conversion type 1 and 2, the timer and the NMI.
- Debug line: SA-1 instructions in a frame.

## Rules (do not break)

- `snes_sa1_cpu.c` includes `snes_cpu.c`. A new global in `snes_cpu.c` needs a `#define` there or the link fails or the two CPUs share state.
- The page tables of the SA-1 (`sa1_rtab`) hold ROM only. Everything else is the slow bus, because the I-RAM and the BW-RAM are shared and protected. A change of CXB-FXB rebuilds both tables (`sa1_map_build`, `snes_map_build`).
- The S-CPU IRQ line is a level: `sa1_irq_line()`. A write to `$4200` or a read of `$4211` must keep it (`S.irq_pending = gsu_irq_line() | sa1_irq_line()`).
- `S.sram` is 256 KB for the BW-RAM. The golden test of the CPU hashes the first 128 KB only.
- The SA-1 interrupt is `flag AND enable` (CFR bits and CIE), not the "message bit and not cleared" of ares: an IRQ that is not enabled does not fire.
- The SA-1 reads the emulation-mode vectors `$FFFA` and `$FFFE` from CNV and CIV (a BRK in emulation mode gets CIV; ares reads it from the ROM). The native BRK and COP vectors come from the ROM.
