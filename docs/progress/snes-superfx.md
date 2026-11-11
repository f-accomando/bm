# bm SNES: Super FX (GSU-1 and GSU-2)

Area sheet for the Super FX co-processor of the bm SNES core. Code: `src/bm/snes_gsu.c`, `src/bm/snes_gsu.h`, the wiring in `src/bm/snes_bus.c`.
Tests: `make test-snes-gsu` (`tests/snes/gsu_test.c`), `make bench-snes-gsu`, and the local smoke test `tests/snes/superfx_roms.py`.
Back to [snes.md](snes.md). The instruction set: [snes-superfx-isa.md](snes-superfx-isa.md). Timings of the machine: [snes-measure.md](snes-measure.md).

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

The instruction set, the fetch and the delay slot, the pixel cache, the instruction cache and the timing are in [snes-superfx-isa.md](snes-superfx-isa.md).

## Status of the code

Done 2026-11-11 (host only, no console test yet). Files: `src/bm/snes_gsu.c`, `snes_gsu.h`, the map and the frame loop in `snes_bus.c`, `tests/snes/gsu_test.c` (166 checks), `tests/snes/superfx_roms.py`.

- Every opcode of the table above is in the core. The checks cover each instruction and its flags, the delay slot, LOOP, LINK, JMP and LJMP, the RAM and ROM access, PLOT and RPIX in 2, 4 and 8 bpp with the heights 128, 160, 192 and OBJ, the dither and the transparent colour, the cache upload by the SNES, the IRQ at STOP and the mask, the memory map, the vector pattern, the RAM size and the battery rule. A golden hash of the cart RAM after a 2800 instruction program (`golden_code` in the test) stops a speed-up from changing the result.
- Wiring: the CPU runs a whole line. The GSU runs lazily, as the sound CPU does (`gsu_sync_pos` in `snes_bus.c`). It catches up to the CPU time when the CPU reads or writes a GSU register ($3000-$34FF), reads or writes the cart RAM, or reads a vector ($FFE0 and up). A read of a register or of the RAM that finds the GSU less than `GSU_QUANT` (1364) master clocks behind does nothing: a poll loop on SFR would run the GSU in very small pieces, and each piece pushes the CPU code out of the instruction cache. A write to a register always catches up first. At the end of a line the GSU catches up when the CPU waits for an interrupt (WAI), else every `GSU_LAG_LINES` (4) lines, and at the end of the frame. The page table of the cart RAM is off while the GSU runs (`gsu_tab_update`), so the CPU reads of the RAM come to `raw_read`. This took the GSU calls from 212 to 66 in a frame in Yoshi's Island (host count).
- The timed frames add `us_gsu` and `gsu_instr` to `snes_debug`. The `us` line of the Lua debug table shows ` gsu <us> gins <instructions>` when a GSU cart runs. The timed part is the sum of the GSU runs of the frame.

### What plays (PC, bmhost, local ROMs, 30 s of scripted pad input)

| Game | Result |
|---|---|
| Star Fox (GSU-1) | boots, the title with the moving star field, the controls page, the Arwing in flight over the terrain (the GSU draws the 3D), the planet map. Not played for long: the pad script only presses START and A. |
| Yoshi's Island (GSU-2, SP1) | boots, the intro and a level scene with the scaled and morphed background (the GSU effects). Not played for long. The intro needs HDMA that the game switches on a few lines into the frame: `$420C` sets up at once the channels that are new (`hdma_init_ch`), else the layers do not scroll at different speeds. |
| Doom (GSU-2) | boots, the legal screen, and the sound CPU loader (a transfer of its own) passes. The GSU runs (2 million instructions in the first load, CFGR $20, 21 MHz). At frame 1084 the CPU returns through a table of code stubs at $7E:5980-$7E:5B80 that is still zero, runs into BRK and the picture stays black or garbage. **Cause not found.** What is known: the table `$7F:4F72` holds the 16-bit pointers $5980, $59A0, ..., so the stubs are code that something has to make. The Doom boot has not made them. Not the word access at an odd address (tested with +1 and with xor 1: same failure). Next: find who writes $7E:5980 on a real console (a DMA from the cart RAM, or the CPU from the output of the GSU) and compare with the DMA list of the machine. |

Costs on the host (x86, `make bench-snes-gsu`, a mix of PLOT, MULT, RAM moves and loops, 21 MHz): about 100 to 150 million instructions a second, so 350000 instructions (the most a GSU-2 can run in a frame) cost 2.3 to 3.5 ms. In Star Fox the GSU runs about 22000 instructions a frame on average (32.6 million in 1500 frames). A GSU-1 at 10.74 MHz runs at most about 100000 instructions a frame.
The ARM1176 has no indirect branch prediction for a `switch`, and the loop head has about 35 instructions, so expect 10 to 20 times the host time: 25 to 50 ns an instruction. That is 3 to 5 ms for the average Star Fox frame and 10 ms or more for a frame that keeps the GSU busy.

### Not emulated, or approximate

- The GSU never waits for RON or RAN. The SNES can read and write the cart RAM and the ROM while the GSU runs; the fixed vector pattern is the only effect of RON.
- The cycle counts are estimates (the cache hit is 1 cycle, a RAM read 6 cycles at 21 MHz). A game that depends on the exact speed of the GSU can run too fast or too slow.
- One pixel cache of 8 pixels, flushed at the end of a row, at RPIX and at STOP. The real chip has two caches.
- The ROM buffer reloads when R14 or ROMBR changes, not at each write of R14. ROMB does not keep the old bank for a pending read.
- BRAMR, the backup RAM at $78-$79 and the GSU-1 or GSU-2 differences (VCR, the clock of the GSU-1) are not emulated. The clock follows CLSR for both chips.

## Open work

- Doom (GSU-2): boots and passes the sound loader, then the CPU returns into a table of code stubs at $7E:5980 that is still zero (BRK, garbage picture). Find who writes those stubs on a console: a DMA from the cart RAM, or the CPU from the output of the GSU. Compare with the DMA list of the machine.
- Play Star Fox and Yoshi's Island to the end with a real pad script, or on the console. Read the pictures for flicker, wrong colours and speed.
- Speed on the Pi Zero W is not measured. Copy the kernel and the cart (the user does it), play Star Fox into a stage, and read `gsu` (microseconds in a frame) and `gins` (instructions in a frame) in the `us` debug line. If `upd/s` is under 60 and `gsu` is over 8000, speed up `gsu_run` again: a pointer into the cache line instead of the check at each byte, a `goto` table, fused pairs (FROM+op, TO+op). The cache misses are in `icache miss: cpu` of the `ppu us` line.
- Exact cycle counts, the wait of the GSU on RON and RAN, two pixel caches: only if a game needs them.

## Rules (do not break)

- The GSU is plain C in `snes_gsu.c` and owns the cart RAM (`S.sram`). A change of the cart map goes in `gsu_map_ptr`, `raw_read` and `raw_write` together. The pages $F000-$FFFF of the banks $00-$3F stay slow (the vector pattern).
- `make test-snes-gsu` holds a golden hash of a program. A speed-up of `gsu_run` must keep it.
- The GSU IRQ is a level (`gsu_irq_line()`, cleared by a read of the high byte of SFR). A write to $4200 and a read of $4211 set `S.irq_pending = gsu_irq_line()`, not 0.
- The GSU and the sound CPU run lazily (see Wiring). Do not call `gsu_run` from anywhere but `gsu_sync_pos`. A new CPU access to the cart RAM that skips `raw_read` and `raw_write` (a page table, a DMA fast path) must call `gsu_sync` first while `gsu_tab_off` is set.
- `gsu_run` has common ends (`wr_d`, `wr_n`, `clr`, `fin`) and out-of-line slow fetch. Keep the hot loop small: it must stay near 4 KB of ARM code.
- No ROM byte goes into the repository. `tests/snes/superfx_roms.py` reads ROMs from the folder in `BM_SNES_ROMS` and keeps nothing.
