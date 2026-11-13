# Super FX speed (bm SNES)

Part of [snes-superfx.md](snes-superfx.md).

## Measurement (2026-11-12)

Pi Zero W, kernel v0.3.0-83, Yoshi's Island scene `m1/10/00`: 95330 GSU instructions in a frame cost 7183 us (75 ns each). QEMU count of the ARM instructions for one GSU instruction (`make test-snes-gsu-arm`, `-icount shift=0`: one ARM instruction is 1 ns, the board timer is read in the test), code running from the code cache:

| Program | C loop before | C loop now | ARM assembly now |
|---|---|---|---|
| ALU (FROM, TO, ADD, SUB, AND, OR) | 42.5 | 27.6 | 21.5 |
| ALU with ALT prefix | 45.1 | 29.5 | 22.4 |
| IBT and IWT | 53.3 | 32.7 | 27.9 |
| LDW and STW | 55.8 | 33.6 | 28.9 |
| LMS and SMS | 53.0 | 33.4 | 27.1 |
| MULT, FMULT | 45.0 | 28.5 | 22.5 |
| Branches with the delay slot | 47.3 | 29.4 | 25.2 |
| INC and DEC | 45.9 | 26.8 | 23.2 |
| GETB | 52.9 | 38.9 | 35.2 |
| Mix of the bench | 51.2 | 33.4 | 28.7 |
| PLOT | 79 to 87 | 63 to 66 | 62 to 65 |

Without the CACHE opcode the loop went through the slow fetch for each byte: 72 to 125 ARM instructions. The games use CACHE.
Where the instructions went (C loop before): the fetch checked the range and the valid bit of the line for each byte (14 instructions with spills), the delay slot flag and the cycle count were kept in separate variables, the registers were 16-bit slots that needed a mask at each load, and PLOT turned the 8 pixels into bit planes with a loop of 8 times the number of planes.
What changed: a pointer into the code cache (`ip`; the low 4 bits tell a line start, a jump sets it again), the master clocks go straight into `budget`, R0-R15 are 32-bit slots, the delay slot has its own entry (`slot`) so the loop head has no test for a pending jump, a dispatch with one jump, a pixel cache that keeps the bit planes (PLOT updates 4 bytes with a multiply). On the ARMv6 console the loop is assembly (`core/src/bm/snes_gsu_arm.S`, `-DSNES_GSU_C` selects the C loop); PLOT, COLOR, GETB, GETC, RAMB and STOP call small C routines.
Opcodes in Star Fox (host, 3000 frames): INC 9%, LOOP 7%, NOP 5%, STW 4.5%, ALT1 4%; PLOT only 1.7%. Yoshi's Island: INC and DEC 8%, LOOP 5%, ALT1 to ALT3 8%, PLOT 2%. So the cost of the loop head matters more than PLOT.


## Rules (do not break)

- The hot loop keeps the code pointer `ip`: any change of PC other than +1 (a jump, CACHE with a new CBR, LJMP) must set `ip` to 0 or to a valid byte; any change of `cvalid` clears it too.
