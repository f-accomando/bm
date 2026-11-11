# bm SNES: Super FX (GSU-1 and GSU-2)

Area sheet for the Super FX co-processor of the bm SNES core. Code: `src/bm/snes_gsu.c`, `src/bm/snes_gsu.h`, the wiring in `src/bm/snes_bus.c`.
Tests: `make test-snes-gsu` (`tests/snes/gsu_test.c`), `make bench-snes-gsu`, and the local smoke test `tests/snes/superfx_roms.py`.
Back to [snes.md](snes.md). Timings of the machine: [snes-measure.md](snes-measure.md).

Games for this work (the user owns them): Star Fox (GSU-1), Yoshi's Island (GSU-2, SP1 board), Doom (GSU-2). No ROM and no ROM byte enters the repository.

## Sources

Each fact has a mark.

- **[F]**: the fullsnes document by nocash (problemkaputt.de/fullsnes.htm, section "SNES Cart GSU-n"). Read for this sheet.
- **[W]**: the SNESdev wiki page "Super FX". It is a stub: 14 registers, 98 instructions, a 512-byte cache. It adds no facts.
- **[M]**: the memory of the author about public Super FX documents (the Super FX documentation of the community). Unchecked.
- **[?]**: not sure. Give the choice of the code and the reason. Check it with a game when a ROM is on the machine.

The code is original. No GPL emulator was read (bsnes/higan, snes9x, ZSNES, Mesen, ares).

## Registers seen by the SNES CPU

The registers are in banks $00-$3F and $80-$BF, addresses $3000-$34FF. [F]

| Address | Name | Use |
|---|---|---|
| $3000-$301F | R0-R15 | 16-bit registers, low byte at the even address |
| $3030 / $3031 | SFR | status flags (below) |
| $3033 | BRAMR | backup RAM enable, bit 0. No known board has the chip. It has no effect here. [F] |
| $3034 | PBR | program bank (the code bank of the GSU) |
| $3036 | ROMBR | ROM bank for GETB (read only) |
| $3037 | CFGR | bit 5 MS0 (fast multiplier), bit 7 IRQ mask (1 = no IRQ at STOP) |
| $3038 | SCBR | screen base: byte address in the cart RAM = SCBR x 1024 |
| $3039 | CLSR | bit 0: 0 = 10.74 MHz, 1 = 21.48 MHz |
| $303A | SCMR | bits 0-1 MD (0 = 2 bpp, 1 = 4 bpp, 3 = 8 bpp), bit 2 HT0, bit 5 HT1, bit 3 RAN, bit 4 RON |
| $303B | VCR | version. 4 for GSU-2. The value of the GSU-1 is not known [?]: the code returns 4 |
| $303C | RAMBR | RAM bank, bit 0 (read only; the RAMB instruction sets it) |
| $303E / $303F | CBR | cache base (read only), the low 4 bits are 0 |
| $3100-$32FF | cache | 512 bytes of instruction cache. The CPU can read and write it |

- A write to an even address of R0-R15 only keeps the byte. A write to the odd address makes the 16-bit value (kept low byte plus new high byte). [F]
- A write to $301F (the high byte of R15) sets GO and starts the program at R15. [F]
- SFR low byte: bit 1 Z, bit 2 CY, bit 3 S, bit 4 OV, bit 5 GO, bit 6 R. High byte: bit 0 ALT1, bit 1 ALT2, bit 2 IL, bit 3 IH, bit 4 B, bit 7 IRQ. [F]
- A read of SFR clears IRQ (bit 15) and releases the IRQ line. [F] The code does it at the read of the high byte [M].
- A write of GO = 0 to SFR stops the GSU. It also sets CBR to 0 and empties the cache. A write that sets GO = 1 starts it (Dirt Trax FX does it). [F]
- The SNES must touch only SFR, SCMR and VCR while the GSU runs. [F] The code does not check it.
- GSU-2 mirrors $3000-$303F at $3040-$30FF and the cache at $3300-$34FF. [F] The code mirrors the registers (`& $3F`) and the cache (`& $1FF`) for both chips.
- RON and RAN (SCMR bits 4 and 3): 1 = the GSU owns the ROM (RAM). A GSU that reads a bus owned by the SNES waits. **Not emulated**: the GSU never waits. [?] The programs set both bits before GO.

### What the SNES reads while the GSU runs

When GO = 1 and RON = 1, a read of the ROM at $FFE0-$FFFF (banks $00-$3F and $80-$BF) gives a fixed pattern: byte at an odd address = $01;
even address: $04 at $xxx4 (COP), $08 at $xxxA (NMI), $0C at $xxxE (IRQ), $00 at the others. So the vectors point to $0100, $0104, $0108, $010C. [F]
The game puts jump stubs there in the work RAM. The code gives this pattern. The other reads of an owned ROM or RAM are not emulated [?]: they give the plain data.

## Memory map (SNES side)

| Banks | Addresses | Content |
|---|---|---|
| $00-$3F, $80-$BF | $8000-$FFFF | ROM, LoROM style: 32 KB of the ROM per bank [F] |
| $40-$5F, $C0-$DF | $0000-$FFFF | ROM, 64 KB per bank, the same bytes as the LoROM banks [F] |
| $70-$71, $F0-$F1 | $0000-$FFFF | cart RAM, 64 KB per bank, mirrored when the RAM is smaller |
| $00-$3F, $80-$BF | $6000-$7FFF | the first 8 KB of the cart RAM |

- $78-$79: backup RAM, no known board has it. Not mapped. [F]
- The chipset byte at $FFD6 is $13 to $1A for a Super FX cart (header at $7FC0, LoROM). $13 = Star Fox (no battery). $14 = GSU with RAM. $15 and $1A = with battery. [F]
- RAM size: the extended header byte at $FFBD (1 KB x 2^n) if $FFDA = $33; else $FFD8 if it is not 0; else 64 KB. The size is raised to 32 KB at least and cut to 128 KB.
  [F] says 32 KB for Star Fox and Yoshi's Island and 64 KB for Doom. A bigger RAM than the real one is safe; a smaller one is not. [?]
- The cart RAM is `S.sram` (the same array as the battery RAM of other carts), so the `.srm` file works. The cart saves it only for a battery chipset ($15, $1A).

## The GSU

The state: R0-R15, SFR flags (Z, CY, S, OV, GO, IRQ, ALT1, ALT2, B), the register selects SREG and DREG, PBR, ROMBR, RAMBR, CBR, SCBR, SCMR, COLR, POR, CFGR, CLSR, the ROM buffer, the pixel cache.

### Instruction fetch and the delay slot

- R15 is the address of the next byte while an opcode runs. An operand byte is read at R15 and R15 moves on. So `LINK #4` before an `IWT R15,#addr` and one delay byte gives the byte after the call. [F][?]
- A jump (JMP, LJMP, a taken branch, LOOP, a write to R15 by any instruction) does not act at once. The GSU still runs the next opcode (the delay slot), then goes to the target. The opcode byte of the delay slot is read before the jump; its operand bytes come after it. [F]
- A branch target is R15 after the operand plus the signed operand. [?]
- Prefixes: ALT1 ($3D), ALT2 ($3E), ALT3 ($3F), TO ($1n), FROM ($Bn), WITH ($2n). Each is one instruction. Every other instruction clears B, ALT1, ALT2, and sets SREG and DREG to R0. The branches ($05-$0F) do not clear them. [F]
- ALT3 uses the ALT1 form if the ALT3 form does not exist; ALT1 and ALT2 are ignored if their form does not exist. [F]

### Instruction set

Notation: Rs = R[SREG], Rd = R[DREG]. Flags: S bit 15 of the result, Z result = 0 (16 bit).

| Opcode | Name | Action |
|---|---|---|
| $00 | STOP | GO = 0, IRQ flag = 1, IRQ line if CFGR bit 7 = 0, R15 = opcode address + 2. Flush the pixel cache |
| $01 | NOP | |
| $02 | CACHE | if CBR differs from the address of the opcode with the low 4 bits cleared, set CBR to it and empty the cache |
| $03 | LSR | Rd = Rs >> 1. CY = bit 0. S = 0. Z |
| $04 | ROL | Rd = Rs << 1 + CY. CY = bit 15. S, Z |
| $05-$0F | BRA, BGE, BLT, BNE, BEQ, BPL, BMI, BCC, BCS, BVC, BVS | signed 8-bit offset. BGE: S = OV. BLT: S != OV |
| $10-$1F | TO Rn | DREG = n. With B set: MOVE Rn,Rs (no flags) |
| $20-$2F | WITH Rn | SREG = DREG = n, B = 1 |
| $30-$3B | STW (Rn) / STB (Rn) (ALT1) | store Rs (word, or low byte) at the RAM address Rn. Word at an odd address: the high byte goes to the address xor 1 [?] |
| $3C | LOOP | R12 = R12 - 1. S, Z of R12. If R12 is not 0: jump to R13 |
| $3D, $3E, $3F | ALT1, ALT2, ALT3 | |
| $40-$4B | LDW (Rn) / LDB (Rn) (ALT1) | Rd = word, or zero-extended byte, at RAM address Rn |
| $4C | PLOT / RPIX (ALT1) | PLOT: pixel at (R1, R2) with COLR, then R1 + 1. RPIX: flush, read the pixel into Rd, S, Z |
| $4D | SWAP | Rd = Rs with bytes swapped. S, Z |
| $4E | COLOR / CMODE (ALT1) | COLR = Rs (with the POR rules); CMODE: POR = Rs & $1F |
| $4F | NOT | Rd = ~Rs. S, Z |
| $50-$5F | ADD Rn / ADC Rn / ADD #n / ADC #n | ALT0 to ALT3. All four flags (OV, S, CY, Z) |
| $60-$6F | SUB Rn / SBC Rn / SUB #n / CMP Rn | CMP: only the flags. SBC: Rs - Rn - (1 - CY). CY = no borrow |
| $70 | MERGE | Rd = R7 high byte, R8 high byte. S = (result & $8080) != 0, OV = & $C0C0, CY = & $E0E0, Z = & $F0F0 [F] (Z is not "result = 0") |
| $71-$7F | AND Rn / BIC Rn / AND #n / BIC #n | S, Z |
| $80-$8F | MULT Rn / UMULT Rn / MULT #n / UMULT #n | 8 x 8 bit to 16 bit, signed or unsigned. S, Z |
| $90 | SBK | store Rs (word) at the last RAM address used |
| $91-$94 | LINK #n | R11 = R15 + n |
| $95 | SEX | Rd = sign-extended low byte of Rs. S, Z |
| $96 | ASR / DIV2 (ALT1) | Rd = Rs >> 1 (signed). DIV2: Rd = 0 if Rs = -1. CY = bit 0, S, Z |
| $97 | ROR | Rd = Rs >> 1 + CY x $8000. CY = bit 0. S, Z |
| $98-$9D | JMP Rn / LJMP Rn (ALT1) | JMP: R15 = Rn. LJMP: PBR = Rn, R15 = Rs, CBR = R15 & $FFF0, empty the cache. n = 8 to 13 |
| $9E | LOB | Rd = Rs & $FF. S = bit 7, Z |
| $9F | FMULT / LMULT (ALT1) | FMULT: Rd = (Rs x R6) >> 16 signed. LMULT: R4 = low word, Rd = high word. CY = bit 15 of the 32-bit product [?]. S, Z of Rd |
| $A0-$AF | IBT Rn,#pp / LMS Rn,(yy) (ALT1) / SMS (yy),Rn (ALT2) | IBT: sign-extended byte. LMS and SMS: RAM word at 2 x yy. No flags |
| $B0-$BF | FROM Rn | SREG = n. With B set: MOVES Rn: Rd = Rn, OV = bit 7, S, Z |
| $C0 | HIB | Rd = Rs >> 8. S = bit 7, Z |
| $C1-$CF | OR Rn / XOR Rn / OR #n / XOR #n | S, Z |
| $D0-$DE | INC Rn | S, Z |
| $DF | GETC / RAMB (ALT2) / ROMB (ALT3) | GETC: COLR = ROM byte (with the POR rules). RAMB: RAMBR = Rs & 1. ROMB: ROMBR = Rs & $7F |
| $E0-$EE | DEC Rn | S, Z |
| $EF | GETB / GETBH / GETBL / GETBS | Rd = ROM byte at ROMBR:R14. ALT1: byte to the high byte of Rs. ALT2: byte to the low byte of Rs. ALT3: sign-extended |
| $F0-$FF | IWT Rn,#xxxx / LM Rn,(xxxx) (ALT1) / SM (xxxx),Rn (ALT2) | 16-bit immediate, or RAM word. No flags |

- Writes to R14 by any instruction make the ROM buffer reload. GETB reads the buffer. ROMB syncs the buffer first, so a pending read uses the old bank. [F][?]
- A word RAM access uses the RAM bank RAMBR ($70 + RAMBR). PLOT and RPIX always use the bank $70.
- Flag details marked [?] in the table come from one source only.

### Pixel cache, COLR and POR

- POR bits: 0 transparent (1 = plot colour 0 too), 1 dither (2 and 4 bpp: if X and Y have different bit 0, use the high nibble of COLR), 2 high nibble (COLOR and GETC copy the high nibble of the source to the low nibble of COLR), 3 freeze high (COLOR and GETC change only the low nibble). [F]
  The doc has a conflict: the table says bit 4 forces OBJ mode, the text says bit 3. The code does not use the bit. [?]
- Transparent test (POR bit 0 = 0): skip the pixel when COLR (8 bpp, no freeze high) or its low nibble (the other cases) is 0. [M]
- Tile number: (X/8) x $10 + Y/8 for 128 lines, x $14 for 160, x $18 for 192. OBJ mode: (Y/$80) x $200 + (X/$80) x $100 + (Y/8 & 15) x $10 + (X/8 & 15). [F]
- Row address in the cart RAM: SCBR x 1024 + tile x tile size (16, 32, 64 bytes for 2, 4, 8 bpp) + (Y & 7) x 2; plane p adds (p / 2) x 16 + (p & 1). Pixel X & 7 is bit 7 - (X & 7). [F]
- The pixel cache holds one row of 8 pixels. It goes to the RAM when the row changes, when the 8 pixels are set, at RPIX and at STOP. A row not full is merged with the RAM bytes. [F][?] The real chip has two caches and more flush points; the code has one.

### Instruction cache

512 bytes, 32 lines of 16 bytes, base CBR. [F] A fetch at an address in CBR to CBR + 511 uses the cache; a missing line is filled from the ROM or the RAM at once.
The SNES writes the cache at $3100-$32FF; a write to the last byte of a line marks the line valid. CACHE and LJMP set CBR and empty the cache. STOP does not. [F]

### Timing

The unit is a GSU cycle. 1 GSU cycle = 2 master clocks at 10.74 MHz, 1 master clock at 21.48 MHz. One line = 1364 master clocks.
The GSU runs after the SNES CPU in each line, for 1364 master clocks (`gsu_run`). So the CPU sees the GSU results one line late at most.

- An instruction takes 1 cycle when it comes from the cache. [F] says 3 clocks for an uncached byte at 10 MHz and 5 clocks at 21 MHz; the code charges that for each byte fetched from the ROM.
- MULT and UMULT: 2 cycles, 1 with CFGR MS0. FMULT and LMULT: 8 cycles, 4 with MS0. [F]
- Extra cost of a RAM read (LDW, LDB, LM, LMS): about 6 cycles at 21 MHz, 3 at 10 MHz. A RAM write: 1 to 4 cycles. RPIX: 20. These are estimates [?]. The doc gives only ranges.
- The code honours CLSR for both chips. [F] lists GSU-1 as 10.74 MHz, but a note says it may also run at 21 MHz. [?]

## Status of the code

See `## Open work` in [snes.md](snes.md) for the state and `## Rules (do not break)` for the constraints.
