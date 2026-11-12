# 65C816 against ares

Back to [snes.md](snes.md). The Super FX side: [snes-superfx-ares.md](snes-superfx-ares.md).

## Rules (do not break)

- The 65C816 is checked against the 65C816 module of ares (ISC licence) one instruction at a time: `make test-snes-ref ARES=/path/to/ares` (`tests/snes/ref_diff.cpp`; the ares tree stays outside the repository, the target skips without `ARES`). About 10 million random instructions a seed (all 256 opcodes in all four M and X modes, emulation mode, decimal mode, MVN and MVP, BRK, COP, WAI, STP, NMI and IRQ entry) give 0 differences in A, X, Y, S, D, DB, PB, PC, P, E and the order of the writes. Cycles are not compared: the core charges an average internal cycle. Run it after every change of an opcode, in C and in the assembly. The harness skips two corners of ares that are not the hardware: `(dp,X)` in emulation mode with DL != 0 and D above $FE00, and a stack that overlaps the operand bytes of JSL or JSR (a,X).
- Direct page and stack modes return a bank-0 address with bit 24 set (`DPF` in `snes_cpu.c`): a 16 bit access wraps its second byte in bank 0. In emulation mode with DL = 0 every direct page access wraps in the page (`DPA`). The assembly leaves the direct page modes, JSL and RTL to C in emulation mode. The new stack opcodes (PEA, PEI, PER, PHD, PLD, PLB, JSL, RTL, JSR (a,X)) do not wrap in emulation mode; a 16 bit read-modify-write writes the high byte first. The tests are in `cpu_tests` of `tests/snes/core_test.c`.
