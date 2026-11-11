# bm SNES: exploration of a ROM converted to ARM (items C4 and C5)

Work-item codes are in [snes-cpu.md](snes-cpu.md).

Status: notes from a talk on 2026-11-11. Nothing is built. The figures are estimates. A "converted ROM" means a static recompilation of the 65C816 code to ARM.

| 65C816 | On ARM | Difficulty |
|---|---|---|
| `LDA`, `ADC`, `SBC`, `AND`, `ORA`, `CMP`, `INC`, `DEC` | almost 1 to 1; N, Z, C and V match the ARM flags (A kept as `A<<16`) | low |
| Branches (`BNE`, `BCC`, ...) | the same ARM condition | low |
| 8 and 16 bit registers (flags M and X) | the block is specialised on M and X | medium |
| Direct page and bank registers (`D`, `DB`, `PB`) | often constant in a block, not always | medium |
| Stack in page 1 and the wrap in emulation mode | address masks | medium |
| Decimal mode (`SED`) | no ARM equivalent: stays in the interpreter | high, but rare |
| `MVN` and `MVP` (block copy) | one ARM loop | medium |
| Computed jumps (`JMP ($xxxx,X)`, `RTS` with a built address) | cannot be found ahead of time | high |
| Code in RAM, or loaded into the SPC700 | does not exist at conversion time | high |
| Accesses to `$21xx`, `$42xx`, `$43xx` | a bus call, with side effects and clocks | medium |

Problems:
- Computed jumps and code in RAM force an interpreter or a JIT as fallback. So C4 (JIT) comes before C5.
- Code growth: one 65C816 instruction (2 to 3 bytes) becomes about 5 to 8 ARM instructions (20 to 32 bytes), about 10 times more. The I-cache of the ARM1176 is 16 KB (from memory, check it), so more misses. Part of the gain is lost.
- Timing: games with raster effects count cycles. Converted code must add the clocks and stop where NMI, IRQ and HDMA arrive. A check at the end of each block delays the event by a few instructions, and some games may notice.
- Licence: converted code is derived from the ROM. Make it on the console from the user's ROM (`/sfc`) and never distribute it.

C4 and C5 share one translator. The only difference is when it runs: at the start (static) or at the first run (JIT). C4 gives most of C5. A static cache of translated blocks on the SD card would remove the translation pause and allow stronger optimisation (unused flags dropped, `D` and `DB` propagated). Decide it after you see the translation cost at run time. Not measured.

| Approach | Sense | Reason |
|---|---|---|
| Static conversion alone | low | computed jumps and code in RAM still need a JIT or an interpreter |
| JIT (C4) with the interpreter as fallback | good | one translator, no ROM analysis, handles dynamic cases |
| JIT and a static cache on the SD card | medium, later | only if the translation pause shows in the measures |

The extreme case is SNES16: the *game* is rewritten as native code. It is a port by hand, not a translation, and it does not apply to any `.sfc`.
Measure C1 first. If the CPU costs 4 to 5 ms of 22, a JIT frees about 15% (3 to 3.5 ms).
