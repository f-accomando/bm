# bm SNES: DSP-1, DSP-2, DSP-3 and DSP-4 (uPD77C25)

Area sheet for the DSP co-processors of the bm SNES core. Code: `src/bm/snes_dsp77.c`, `src/bm/snes_dsp77.h`, the wiring in `src/bm/snes_bus.c`, the firmware call in `src/bm/api_snes.c` and `carts/snes/src/main.lua`.
Tests: `make test-snes-dsp77` (`tests/snes/dsp77_test.c`).
Back to [snes.md](snes.md). The other cartridge chip: [snes-superfx.md](snes-superfx.md).

All four chips are the same NEC uPD77C25 core with a different internal program. The program is the firmware. It is not in the repository (copyright).
Games: DSP-1 Super Mario Kart, Pilotwings and others; DSP-2 Dungeon Master; DSP-3 SD Gundam GX; DSP-4 Top Gear 3000.

## Sources

Each fact has a mark.

- **[M]**: the memory of the author about the public NEC uPD7725/uPD77C25 description and about public SNES documents. No document was open while the code was written. Unchecked.
- **[?]**: not sure. The sheet gives the choice of the code. Check it with a real firmware and a real game.
- **[T]**: checked by `tests/snes/dsp77_test.c`. The test checks that the code does what this sheet says. It does not check that the chip does it. A synthetic program cannot do that.

The code is original. No GPL emulator was read (bsnes/higan, snes9x, ZSNES, Mesen, ares, MAME).

## The chip

- Program ROM: 2048 words of 24 bits. Data ROM: 1024 words of 16 bits. Data RAM: 256 words of 16 bits. Return stack: 4 levels. [M]
- Registers: A, B (accumulators with their own flags), TR, TRB, K, L, DP (8 bits), RP (10 bits), PC (11 bits), DR, SR, SI, SO. M and N are the product of K and L: M = (K x L) >> 15 and N = (K x L) << 1 (16 bits each). K and L are signed. [M][T]
- The clock is 7.6 MHz and one instruction takes one clock. [?] The code runs 23191/65536 DSP clocks for each SNES master clock (21.477 MHz).
- Instruction word, top 2 bits: `00` OP, `01` RT (OP, then return), `10` JP, `11` LD. [M][T]
  - OP: PSEL[21:20] ALU[19:16] ASL[15] DPL[14:13] DPH[12:9] RPDCR[8] SRC[7:4] DST[3:0].
  - JP: BRCH[21:13] NA[12:2]. LD: ID[21:6] DST[3:0].
- Order inside an OP: the source goes on the bus, the ALU runs (PSEL picks the second operand: RAM[DP], the bus, M or N), the destination takes the bus value, then DP and RP change. A store to RAM[DP] uses the DP from before the change. [?][T]
- ALU codes: 1 OR, 2 AND, 3 XOR, 4 SUB, 5 ADD, 6 SBB, 7 ADC, 8 DEC, 9 INC, 10 CMP (1's complement), 11 SHR1 (the sign stays), 12 SHL1, 13 SHL2, 14 SHL4, 15 XCHG (swap the bytes). Code 0 does nothing. ADC, SBB and SHL1 use the carry of the other accumulator. SHL2 and SHL4 fill with ones. [M][T]
- Source codes: 0 TRB, 1 A, 2 B, 3 TR, 4 DP, 5 RP, 6 data ROM[RP], 7 SGN (0x8000 minus S1), 8 DR (and RQM = 1), 9 DR (no flag change), 10 SR, 11 and 12 SI, 13 K, 14 L, 15 RAM[DP]. [M]
- Destination codes: 0 none, 1 A, 2 B, 3 TR, 4 DP, 5 RP, 6 DR (and RQM = 1), 7 SR (only the bits outside mask 0x907C), 8 and 9 SO, 10 K, 11 KLR (K = bus, L = data ROM[RP]), 12 KLM (L = bus, K = RAM[DP or 0x40]), 13 L, 14 TRB, 15 RAM[DP]. [M][T]
- DPL: 1 increment, 2 decrement, 3 clear the low nibble of DP (the high nibble does not change). DPH: the high nibble of DP is XORed with the 4-bit field. RPDCR: RP = RP - 1 (10 bits). [M][T]
- Flags of each accumulator: C, Z, OV0, OV1, S0, S1. [M]
  - S0 is bit 15 of the result. Z is result = 0. C is the carry (ADD) or the borrow (SUB); for the shifts the last bit out.
  - OV0 is the signed overflow of ADD and SUB. The logic ops, the shifts, CMP and XCHG clear OV0 and OV1.
  - **[?] S1 and OV1.** The code does: OV1 = OV1 xor OV0 on ADD and SUB; S1 = S0 when OV1 was 0 before the instruction. After one overflow S1 is the wrapped sign, so SGN gives 0x7FFF after a positive overflow and 0x8000 after a negative one. [T] The rule for a second overflow in a row is a guess. The firmware of the chip uses SGN to saturate. If a game shows wrong signs, look here first.
- Jump conditions (BRCH): 0x100 JMP, 0x140 CALL (pushes the next PC). 0x80 to 0xAF: the group is `(BRCH - 0x80) / 8` = C, Z, OV0, OV1, S0, S1; bit 2 picks accumulator A (0) or B (1); bit 1 picks "flag set" (1) or "flag clear" (0). 0xB0 DP low nibble = 0, 0xB1 not 0, 0xB2 = 15, 0xB3 not 15. 0xB4 JNSIAK, 0xB6 JSIAK, 0xB8 JNSOAK, 0xBA JSOAK. 0xBC JNRQM, 0xBE JRQM. Another value: no jump. [M][T]
- The serial port (SI, SO and the acknowledges) is not emulated. SIACK and SOACK stay 0. No SNES cartridge uses it. [?]

## The SNES side

- SR bit 15 is RQM. The SNES reads only the high byte of SR (bit 7 = RQM). SR bit 12 DRS, bit 10 DRC (1 = 8-bit transfers). [M][T]
- DR in 16-bit mode (DRC = 0): the first byte takes the low half and sets DRS; the second byte takes the high half, clears DRS and clears RQM. In 8-bit mode (DRC = 1) each byte clears RQM. A read does the same. [M][T]
- The chip sets RQM when it reads DR (source 8) or writes DR (destination 6). The firmware then waits in a one-instruction loop `JRQM $` until the SNES has taken its turn. [M]
- **Idle rule.** When the chip takes a jump to itself on JRQM or JNRQM, it stops. Nothing changes until the SNES touches DR, so the emulator does not run it. This is what makes the chip cheap: it costs time only while it computes.
- No firmware: SR reads $80 (RQM set, so a game does not wait for ever) and DR reads 0. The game runs without the chip. The picture is then wrong where the game uses the chip (Super Mario Kart does not draw the track).

## Where the ports are

The header gives the chip kind: chipset byte $FFD6 = $03 (DSP), $04 (DSP + RAM), $05 (DSP + RAM + battery). [M] The header does not give the number. The loader reads the title: "DUNGEON MASTER" is DSP-2, "GUNDAM GX" is DSP-3, "TOP GEAR 3000" or "PLANETS" is DSP-4, else DSP-1. [?] A ROM with a changed title (a translation) can pick the wrong number; the firmware file then has the wrong program. Rename the firmware or the ROM title.

| Cartridge | DR | SR |
|---|---|---|
| LoROM, ROM up to 1 MB (Super Mario Kart, Pilotwings, DSP-2, DSP-3, DSP-4) | $20-$3F and $A0-$BF, $8000-$BFFF | the same banks, $C000-$FFFF |
| LoROM, DSP-1, ROM over 1 MB | $60-$6F and $E0-$EF, $0000-$3FFF | the same banks, $4000-$7FFF |
| HiROM, DSP-1 | $00-$1F and $80-$9F, $6000-$6FFF | the same banks, $7000-$7FFF |

[M][T for the code, ? for the addresses]. A write to SR does nothing. The pages of the ports are on the slow path of the map (`map_ptr` returns NULL for them).

## Firmware files

The firmware is read from the `sfc` folder of the SD card, next to the ROMs, by `load_dsp_fw` in the cart. The file name is not case sensitive.

| Chip | File names, in the order tried |
|---|---|
| DSP-1 | `dsp1b.rom`, `dsp1.rom`, `dsp1a.rom`; else `dsp1.program.rom` with `dsp1.data.rom` |
| DSP-2, 3, 4 | `dsp2.rom`, `dsp3.rom`, `dsp4.rom`; else `dspN.program.rom` with `dspN.data.rom` |

Layouts accepted (`dsp77_load`):

1. **One file of 8192 bytes**: the program first (2048 words of 3 bytes = 6144 bytes), then the data (1024 words of 2 bytes = 2048 bytes). This is the form of `dsp1.rom` and the like.
2. **Two files**: the program (6144 bytes) and the data (2048 bytes) in the same word format.

The byte order of the words is found from the program: a jump word (type `10`) has its two low bits clear. The code reads the program once as little endian (the first byte is the low byte of the word) and once as big endian, and takes the order that gives more jump words with clear low bits. A tie gives little endian. The data uses the same order. [?] The little endian form is the one I know (the dumps that the main emulators read). The detection exists because I am not sure. If a good dump gives a black picture, the order is the first suspect: tell me the first 8 bytes of the file.

Any other size is refused with a message ("the DSP firmware file must be 8192 bytes"). The message goes to the log and to the pause menu of the cart. 

## Lazy run

The wiring in `snes_bus.c` copies the design of the Super FX. `dsp_sync_pos` brings the chip to the CPU time when the CPU reads or writes DR or SR. The chip runs the instructions of that time (7.6 MHz) and stops when it idles. At the end of the frame (line 261) a chip that still computes catches up. A catch-up is at most one frame. `dsp_done` is shifted by one frame at the end of each frame, as `gsu_done`. [T]
The time of one instruction is a switch of about 20 cases on the decoded word. M and N are computed only when an instruction reads them. The interpreter is small (one `step` function and the `do_op` function) so that it fits the 16 KB instruction cache of the Pi Zero W with the CPU loop.

## Status of the code

Done 2026-11-12 (host only; no firmware and no ROM on the machine). 187 checks in `tests/snes/dsp77_test.c`:

- every ALU code with its flags, A and B, the carry of the other accumulator, SGN after an overflow;
- the multiplier (M, N, signed), KLR and KLM, every source and destination, SR write mask, DP and RP changes (DPL, DPH, RPDCR);
- JMP, CALL and RT, four nested calls, 100 flag jump cases (A and B, five flag set-ups, five groups, set and clear), the DP nibble jumps, the serial jumps, JRQM and JNRQM;
- the DR and SR handshake in 16-bit and 8-bit mode, the idle rule (no instruction runs while the chip waits), a firmware that adds 1 to a word;
- the firmware loader: both byte orders, one file and two files, wrong sizes;
- the speed: 7600 instructions in 21477 master clocks, and three frames of a chip that never waits;
- the cartridge: chip detection (the four titles, a Super FX and a RAM-only cartridge are not DSP), the three port maps with their mirrors, the page table, the lazy run (the SNES sees RQM only when the time has gone), no firmware, a reset, a ROM with no chip.

### What cannot be checked here

- **No real firmware and no real ROM was used.** The core is tested only on programs that I wrote. A program of mine proves that the code does what this sheet says. It does not prove that the sheet is what the chip does. Every [?] above can be wrong, and each wrong point can hide in a game as a wrong track, a wrong angle or a hang. The points that matter most: the S1/OV1 rule, the order of DST and ALU in one instruction, the byte order of the firmware file, the clock, the title rule that picks DSP-1 to DSP-4, the addresses of the ports for the ROM above 1 MB and for HiROM.
- No game was run. Super Mario Kart, Pilotwings, Dungeon Master, SD Gundam GX and Top Gear 3000 are untested.
- The ARM1176 speed is not measured. The chip is idle most of the time. When it computes at the full rate it runs 126000 instructions a frame; at about 30 ns each that is 4 ms.

## Open work

- Test with a real firmware: put `dsp1b.rom` (or `dsp1.rom`) in `/sfc` next to Super Mario Kart. Look at the log line `snes dsp1 firmware ...`. If the karts and the track draw, the core is right. If it hangs on a black screen, read `snes_debug().pc` and the SR reads.
- Test DSP-2 (Dungeon Master), DSP-3 (SD Gundam GX) and DSP-4 (Top Gear 3000) the same way. The DSP-2 to DSP-4 firmware may use the serial port or a DR mode that the code does not do.
- Measure `dsp77_run` on the Pi Zero W: add the chip time to `snes_debug` as the GSU does (`us_gsu`) if a game keeps it busy.
- A small time check of the real chip (clock, one instruction per clock) is needed only if a game depends on it.

## Rules (do not break)

- The chip is plain C in `snes_dsp77.c`. It has no knowledge of the SNES map. The map and the sync are in `snes_bus.c` (`dsp_port`, `dsp_sync_pos`).
- Run the chip only through `dsp_sync_pos`. A new CPU access to DR or SR that skips `raw_read` and `raw_write` must call `dsp_sync` first.
- The pages of the ports stay on the slow path: `map_ptr` returns NULL for them.
- No firmware and no ROM byte goes into the repository. The tests use only programs written in `tests/snes/dsp77_test.c`.
- `snes_dsp_firmware` is called after `snes_load` (it resets the machine). A new `snes_load` drops the firmware.
- Keep the interpreter small: the hot code is `step` and `do_op`. Do not add a per-instruction multiply (M and N are computed when read).
