# bm SNES: item C2, registers in ARM registers (profile and result)

Part of [snes-cpu.md](snes-cpu.md).

Done 2026-11-11 (code, host tests, ARM build). The time on the Pi Zero W is not measured yet.

Profile (ARM code of the old `cpu_step`, `arm-none-eabi-gcc -O2 -mcpu=arm1176jzf-s`, read in the disassembly):

- Each call to `cpu_step` pushed 9 registers and popped them. The loop in `snes_bus.c` called it for every instruction.
- The state was 17 `static` variables. Each one has its own literal-pool address, so one instruction loaded about 10 addresses and then the values. After every call to `snes_read` the registers were loaded again.
- The clock `snes_clk` was a global: each bus access did load, add and store.
- About 55 ARM instructions ran before the dispatch of the opcode. The work of a simple opcode (LDA dp) was about the same again.
- `alu`, `mode01` and `mode11` were out of line: 3 calls for an ALU opcode.
- The measured 280 cycles an instruction is more than this code explains (about 100 to 150 cycles). The rest is probably misses of the 16 KB data cache (the page tables `snes_rtab` and `snes_ctab` are 20 KB, the ROM data) and of the instruction cache. The change below does not reduce those.

What was done (`snes_cpu.c`, `snes_int.h`, two lines in `snes_bus.c`):

- `cpu_run(limit)` runs instructions while the clock is under `limit` and returns the count. `snes_bus.c` calls it twice a line (to the IRQ point and to the end of the line). `cpu_step()` stays for the tests: it runs one instruction.
- The registers, the flags and the clock are the fields of one struct `cpu_t`. `cpu_run` copies the struct to a local variable. All helpers are `always_inline` and take a pointer to that local. The address does not leave the function, so GCC keeps the fields in ARM registers (the rest in the stack frame).
- The flags M, X, D, I and E are bit-fields of one word. N, V, Z, C are separate ints.
- The clock is a field of the struct. A plain-memory access adds the page cost to the field. A slow access (a register) stores the clock to `snes_clk`, calls `snes_read` or `snes_write`, and reads it back.
- Memory operands (shifts, TSB, TRB, BIT, LDX, LDY, STX, STY, STZ, CPX, CPY and the ALU families) decode to a mode and a kind. The address is computed in one switch and the operation in a second one. This keeps the code from doubling.
- `S.line_clk` is still set before each instruction (the slow bus reads it for the H and V counters).

Result:

| Measure | Before | After |
|---|---|---|
| `make test-snes-cpu` (12 hashes), `make test-snes`, `make test-snes-ppu` | ok | ok, the table is not changed |
| `make bench-snes-cpu` on the PC (x86, game-like program, ns an instruction, 3 runs each) | 26 to 32 | 22 to 25 (about 15% less) |
| ARM instructions in `cpu_step` / `cpu_run` | 2214 | 7144 |
| `snes_cpu.o` text (ARM) | 21110 bytes | 29825 bytes |
| ARM instructions from the loop head to the dispatch | about 55 | about 25 |

The ARM gain is an estimate: the PC is x86 and has other limits. From the instruction counts the CPU part should fall by 10 to 20%, that is 0.3 to 0.6 ms of the 3.0 to 3.4 ms. The code is 8.7 KB larger and the 16 KB instruction cache is shared with the PPU and the DSP: if the console shows no gain, the cause is the instruction cache.

To measure on the console: build `make build/kernel.img` (Pi) or `make TARGET=rgb30`. Play Super Mario World and SC4 for 2 minutes with the logger. Read the field `cpu` in the line `us ...` (see [snes-measure.md](snes-measure.md)). Before: 3.0 to 3.4 ms.

Left out: a cache of the code page for the fetch (one lookup for many bytes), lazy N and Z flags, a table of labels (computed goto) in place of the `switch`, and the register pressure (about 20 live values on 12 ARM registers: some fields are in the stack frame). The assembly of C3 is not part of this item.
