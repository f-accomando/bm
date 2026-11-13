# bm SNES: item C3, the hot path of the 65C816 in ARM assembly

Part of [snes-cpu.md](snes-cpu.md). Done 2026-11-11 (code, QEMU tests, ARM builds). The time on the Pi Zero W is not measured yet.

## Why

The first play with the instruction-cache meter (SMW, Pi Zero W): the CPU part has about 17000 instruction-cache misses for about 11850 emulated instructions. That is 1.5 misses an instruction. The CPU, the SPC700 and the PPU run in turn and evict each other from the 16 KB cache. The C interpreter of C2 had 29.8 KB of code. So the code size matters as much as the instruction count.

## What was done

- `core/src/bm/snes_cpu_arm.S` (ARMv6 only) runs the common opcodes. `exec_op` in `snes_cpu.c` runs every other opcode, out of line, in its own section `.text.snes_cpu_cold`. The assembly calls it with the registers written back to the struct.
- Registers: r4 PC (in the high half, so an add of 0x10000 wraps in the bank), r5 PB, r6 A, r7 X, r8 Y, r9 the clock minus the limit, r10 N and Z (lazy: one `orr` after an operation), r11 P (C, I, D, X, M, V), r12 the page table. D, DB, S and E are in the stack frame.
- An opcode has two words in the table `optab`: a mode routine (it computes the address of the operand) and an operation routine. A routine of each kind is shared by all opcodes that use it.
- Done in assembly: LDA, STA, ORA, AND, EOR, ADC, SBC, CMP, LDX, LDY, STX, STY, STZ, CPX, CPY (immediate, direct page, absolute, long, indexed, `(dp),y`), ASL, LSR, ROL, ROR, INC and DEC (accumulator and memory), INX, INY, DEX, DEY, the transfers TAX, TAY, TXA, TYA, TXY, TYX, CLC, SEC, NOP, the 9 branches, JMP, JSR, RTS, JSL, RTL, PHA, PLA, PHX, PLX, PHY, PLY.
- Left to C (about 100 opcodes): decimal ADC and SBC, REP, SEP, PLP, PHP, SEI, CLI, BIT, TSB, TRB, the indirect modes other than `(dp),y`, the stack-relative modes, PEA, PEI, PER, block moves, XBA, XCE, WAI, STP, BRK, COP, RTI, and the register transfers with S and D.
- Interrupts, WAI, STP and an immediate 16-bit operand across the end of a bank go to C (`cpu_attn`, `cpu_cold`). A register access of the bus goes to C (`cpu_sr`, `cpu_sw`) with the clock of the moment.
- The C loop stays. It runs on the PC, on the RGB30 (AArch64) and with `-DSNES_CPU_C`.

## Sizes (ARM, `-O2`)

| Part | Before (C2) | After |
|---|---|---|
| Hot code (the loop, the modes, the operations) | 29825 bytes (`snes_cpu.o` text) | 4172 bytes of code and 2048 bytes of table |
| Cold code `exec_op`, `cpu_cold`, `cpu_attn` | in the hot code | about 31 KB, own section |

## Tests

- `make test-snes-cpu-arm` (needs `qemu-system-arm` and `arm-none-eabi-gcc`): the golden hashes on an ARM1176 in QEMU, and 20000 random instructions against the C reference (`tests/snes/cpu_diff.c`): registers, clock and the whole machine state equal.
- Case 8 of the golden table differs on every ARM build, also in C. The assembly and the C give the same hash there. The cause is not found (a difference of ARM and x86 in the C core). The test lists it as known.
- `make test-snes-cpu`, `make test-snes`, `make test-snes-ppu` pass on the PC. The Pi kernel and the RGB30 kernel build.

## What to read on the console

Build `make build/kernel.img`. Play SMW and SC4 for 2 minutes with the logger. In the line `us ...` read `cpu` (before: 2.7 to 3.4 ms) and, at the end of the line, `icache miss: cpu` (before: about 17000 on SMW). The instruction count of a frame is in the same line.

## Left out

- A faster path for the opcodes in the cold list. Measure first which of them are frequent in a game (the count per opcode is not in the logger).
- A code-page pointer for the fetch (one table lookup for many bytes).
- Flags in the ARM condition flags.
