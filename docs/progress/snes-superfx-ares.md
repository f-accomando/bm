# Super FX against ares: the differences

Back to [snes-superfx.md](snes-superfx.md). The CPU side of the same check is in [snes-cpu-ref.md](snes-cpu-ref.md).

`make test-snes-ref ARES=/path/to/ares` builds `tests/snes/ref_diff_gsu.cpp` and runs it (the target skips with a message when `ARES` is not set). The harness runs the same random program on our GSU and on the GSU module of ares (ISC licence, kept outside the repository; the harness only `#include`s the module and writes the cartridge side again in its own file). The program is 1 to 5 random units: `[ALT1-3] [TO, FROM, WITH] opcode [operands]`, then STOP. The harness compares R0-R15, the flags Z, CY, S, OV, PBR, ROMBR, RAMBR, COLR, POR, CBR and the whole cart RAM (64 KB) after STOP. Cycles are not compared. Snes9x (`fxinst.cpp`, GPL) was read as a second opinion and nothing of it was copied.

Not in the random programs: LOOP, JMP and LJMP (they jump to random places), TO R15, WITH R15, and IBT, IWT, LMS and LM of R15 (the same). Cover those with a directed test when needed.

Result of the run with seeds 1 to 5, about 300000 programs each (all other opcodes, with and without prefixes, all colour depths, the transparent and dither flags, the pixel cache, the ROM and RAM buffers, the multiplies, the code cache): every difference is one of the five below. Do not edit `snes_gsu.c` for them while the speed work runs; the harness keeps them as `known` and fails only for a new kind (exit code 1, line `unexplained`).

| # | Opcode, state | Program (hex) and input | Ours | ares | Who decides |
|---|---|---|---|---|---|
| 1 | `A0-AF` and `F0-FF` with ALT3 (ALT1 and ALT2 set) | `3D 3E A4 0B` (R4 = 0x894C) | R4 = 0x000B (IBT) | R4 = word of the RAM at 0x0016 (LMS) | ares, bsnes and Snes9x agree (the ALT3 table of Snes9x holds LMS and LM; ares tests ALT1 first). Ours decodes ALT3 as ALT0. |
| 2 | `AF` (SMS) and `FF` (SM) with R15, ALT2 | `3E AF 9A` | stores R15 before the operand byte is read | stores R15 after it (one more) | Open. Snes9x stores the value before and has a comment "is the value of r15 before or after the extra byte is read?". No document found. |
| 3 | `WITH Rn`, `TO Rn` or `FROM Rn`, then `ALT1-3`, then an opcode | `2B 3E 10` (R11 = 0xC3AC, R0 = 0x175C) | the B flag stays: `10` is MOVE R0,R11, R0 = 0xC3AC | ALT clears B: `10` is TO R0 only, R0 = 0x175C | ares and Snes9x agree (`fx_alt1/2/3` do `CF(B)`). Ours is wrong. The harness skips this order; `RD_GSU_ALT_AFTER=1` includes it. |
| 4 | `02` CACHE at an address that ends in `F` | `BD 14 14 3E 28 2C A2 65 23 31 B3 A8 4C 2B B0 02` | CBR = 0x8000 (R15 = address of CACHE) | CBR = 0x8010 (R15 = address + 1, the pipeline) | ares and Snes9x use R15 after the fetch (`R15 & 0xFFF0` with R15 already moved). Ours is wrong only at addresses `xxxF`. |
| 5 | `EF` (GETB) or `DF` (GETC) after `ROMB` (ALT3 `DF`) without a write of R14 | `3F DF 10 EF` (R0 = 1, R14 = 0x00EE) | reads the new bank at once | returns the byte buffered from the old bank | Fixed 2026-11-12: ares and Snes9x keep the old buffer; Doom needs it ([snes-doom.md](snes-doom.md)). |

Fix order for the speed session when it is done: 1 and 3 (clear defects, small), then 4. Number 2 needs a hardware document first. After each fix run `make test-snes-ref ARES=...`: the counts of `known` fall to 0.

