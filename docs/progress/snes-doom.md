# SNES: Doom (Super FX 2) reverse-engineering notes

Status: 2026-11-12. The crash at `$7E:5980` is solved (section below). Doom now plays to the first level on the host with an incomplete 3D view. The rest of this sheet is notes from the source of the original port and public documents ([snes-superfx.md](snes-superfx.md) has the chip rules).

Marks: **S** = stated in the original source or its build files (read on 2026-11-12). **D** = deduced by us, not checked. Each **D** line is a lead to test, not a fact.

## Sources

| Source | What it holds | Licence and use |
|---|---|---|
| [RandalLinden/DOOM-FX](https://github.com/RandalLinden/DOOM-FX) (commit 90af514, 2022-06-19) | 65816 and Super FX assembly of Doom/FX, the section map `source/RL.SEC`, the tools that convert the WAD data, the Super FX notes `sfx.txt` and `sfx.guide` of the repository | GPL-3.0. Read it to understand. Do not copy code into bm: bm keeps only original or permissive code |
| [Sunlitspace542/DOOM-FX](https://github.com/Sunlitspace542/DOOM-FX) | A fork of the same project that builds it | Same licence |
| Source backup of 2024 (archive.org, from floppy disks) | Reported to hold the sound driver and tools that the GitHub release lacks | Not read |
| [SNESdev wiki: GSU](https://snes.nesdev.org/wiki/GSU), [SnesLab GSU](https://sneslab.net/wiki/GSU) | Registers, opcode matrix, cycle times, flag rules of the Super FX | Public. Already used for the core |
| Official SNES Development Manual, Book II (Table 2-2-2) | The instruction set of the chip | Public scan |
| Ghidra module for the GSU | Not found: a search found no module, only Ghidra modules for the 65816 | Write one in SLEIGH if a disassembler is needed |

The release is partial: the repository has no sound driver source in `source/`, and one report says the GitHub release lacks tools. **D**: the bytes we run come from a retail ROM, so the tools are not needed.

## Layout of the program (S: `source/RL.SEC`, `rlinit.a`, `rl.a`)

- The engine is called REALITY_ENGINE (`RL`).
- Sections by size (hex): `ENGINE_GSUCODE` `$6C99` (the Super FX program, in ROM bank `$00` of the GSU map, load `$816B`), `ENGINE_RAMCODE` `$31AB` (65816 code that runs from WRAM), `ENGINE` `$0ADA`, `COLOUR_MAPS` `$2200`, `SINCOS` `$1B02`, `INVERSE` `$8000`, `RLSCNPIXSCALE` `$6669`, `RLSTATUS*` pictures, one pair of level sections `ExMy_A` and `ExMy_B` per level, and `MUSIC_C` with `MUSIC_DRIVER` `$2945`.
- Work RAM sections: `RAM0` (direct page, `$8B` bytes), `RAM1` and `RAM1GSUVECTORS` at `$0100` to `$0BBE`, `RAM2` at `$7E2000` (size `$E000` reserved), `RAM3` at `$7F0000` (`$D5E0` bytes), and `RAM7A` to `RAM7D` at `$700000` (cart RAM; the biggest, `RAM7D`, is `$7AD2` bytes at `$703080`).
- Tables made at build time by tools in `binaries/` (`MKSEC`, `MKTEX`, `MKRAY`, `MKTAN`, `MKARCTAN`, `MKFLOOR`, `MKRLPIXSCALE`, `RIPDOOM`) and by `convert/`: the WAD data is converted to the console formats.
- The ROM is bigger than 1 MB (sections reach `$5FFFFF` in the GSU map). **D**: it is a 2 MB cart, so the header and the ROM mapping at banks `$40` to `$5F` must follow the Super FX map exactly.

## The split of work between the 65816 and the Super FX (S unless marked)

- At start the 65816 runs `_RLInitRAMCode`: one `mvn` block move copies `ENGINE_RAMCODE` (`$31AB` bytes) from its ROM bank to WRAM at `_RLRAMCode`, which is the start of `RAM2` (`$7E2000`). So the code that the CPU runs most of the time is in WRAM, from `$7E2000` to about `$7E51AB`.
- `_RLInitGSU` fills a table of pointers (`PTRLDoTasks`, `PTRLPlayer`, `PTRLWeapons`, `PTRLDoSectors`, `PTRLBuildBSP`, `PTRLFireTarget`, `PTRLDamageObject` and more) with the low 16 bits of the entry points of the game logic when the build flag `usePATCH` is on. **D**: the CPU calls the game logic through these pointers, so one bad pointer sends the CPU to a wrong place.
- The Super FX draws the 3D view (walls, floors, sprites in the world; the BSP walk and the ray tables are its input). It sets the screen base with `sfxSCBR` from `_RLGSUChrDef`, so the picture is made in the cart RAM as character data and the CPU sends it to the PPU by DMA. **D**: this is the same split as in Star Fox, with a 2-bit colour plane count chosen for Doom.
- The 65816 runs the rules of the game (players, enemies, sectors, weapons, menus) and the sound: it talks to the sound program through `musicdrv.a` (`__SendDriverCommand`).

## The crash at `$7E:5980`: solved (trace of 2026-11-12)

Cause: a bug of the VRAM read buffer in `snes_bus.c`. The engine does not run code at `$7E:5980`. The values `$5980`, `$59A0`, ... are pointers to 4 bpp character data (32 bytes each, `$7F:5960` + 32 * n, loaded by three DMAs of 1024 bytes from the ROM). They are arguments, not code.

How the engine uses them (read on the trace, no source copied):
- The 65816 code at `$7E:2D0A`-`$7E:2D4D` builds a call list at `$7F:4F72`. Each group is one routine address (`$2D84`, `$2E3E`, ... for 8, 3, ... pointers) followed by its pointers. The list ends with `$2003`, `$25EF`.
- At `$7E:2570`-`$7E:25EF` the engine sends the list to VRAM word `$7500` by DMA (48 bytes), sets the same VRAM address, makes a 16-bit dummy read (`LDA $2139`, which reads `$2139` and `$213A`), then reads 48 bytes back with a DMA (B bus to A bus, mode 1) into WRAM `$0780`. Then it does `TCS` with `$077F` and `RTS`: the list is a stack that the CPU "returns" through. Routine `$2D84` pops the 8 pointers into direct page slots `$02`, `$12`, ... and draws.
- The core refilled the read buffer after the address step. The dummy pair then consumed word 0 and the DMA started at word 1: the first entry, the routine `$2D84`, was lost and the first `RTS` jumped to `$5981`. The hardware refills the buffer from the address before the step: after the dummy pair the buffer still holds word 0, and the DMA reads word 0, 1, 2, ...

The four leads of the earlier note, checked on the trace:
1. ROM banks `$40`-`$5F` and `mvn`: correct. One `mvn` copies `$31AB` bytes from `$5C:0000` to `$7E:2000` (the engine RAM code, as the source says). The three DMAs of 1024 bytes come from `$5F:711D`, `$5F:751D`, `$5F:791D` with `$2183` = 1 (WRAM `$7F`), and the bytes are the ROM bytes.
2. Pointer table read before written: no. The table is written (`$7F:4F72`) at frame 1077 and read at frame 1087.
3. Super FX STOP, GO and IRQ: not involved. The CPU sets SCMR to `$0F` around its ROM DMAs (the CPU owns the ROM) and back to `$1F`: the core handled it.
4. Stack return: yes, the stack is a list built in WRAM `$0780` on purpose (see above).

Trace tool: build the core with `-DSNES_TRACE` (hooks `snes_trace.wr`, `.op`, `.mv`, `.dma`, `.wport` in `snes_int.h`; the Pi and RGB30 builds do not define it) and link `tests/snes/doom_trace.c`. The header of that file has the command. It needs the user's ROM and keeps no ROM data.

## Differences from Doom for DOS (S for the data, D for the rest)

- The map data is converted: one pair of sections per level, and the textures, the floors and the sprites are converted by the tools into tables for the Super FX.
- The ray, tangent, arctangent and distance tables are made at build time (`MKRAY`, `MKTAN`, `MKARCTAN`, `RLSCNPIXSCALE`, `INVERSE`), because the chip has no divide and a small multiply.
- **D**: the view is smaller than the DOS view, the world is drawn without the lights of the DOS engine (it uses `COLOUR_MAPS`), and the number of enemies is lower. Not checked in the source.

## The noisy textures: solved (2026-11-12)

Cause: the ROM buffer of the Super FX. The chip loads the buffer when R14 is written. `ROMB` changes the bank but keeps the buffer, so a `GETB` after `ROMB` without a new R14 returns the byte of the OLD bank. The core reloaded the buffer from the new bank at once. The hot loop of the engine (ROM `$00:E1B6`-`E1C7`, about 230000 runs in 2500 frames) does `FROM R4, ROMB, GETB`, so every texel and colour map byte came from the wrong bank: noisy walls. Fix: `set_rombr` in `snes_gsu.c` loads the buffer with the old bank before the bank changes; the buffer reloads only when R14 changes (C loop, `gsu_c_romb` for the ARM loop, and `tests/snes/gsu_ref.c`). Tests: `romb keeps buffer` and `romb then r14` in `gsu_test.c`. Approximation: a rewrite of R14 with the same value after ROMB does not reload (the chip does).

## The flicker: solved (2026-11-13)

Cause: the V/H-IRQ of the core fired once per line. The game turns the display on at V 23 and off at V 199 with `WAI`. At each IRQ its handler writes HTIME again (`$60`, then `$F8`) in the same line and waits again. The second IRQ of the line was lost. The CPU then woke at the next IRQ of another source (the Super FX STOP) and wrote `$2100` at a wrong line (L96, L110, L151, L220, L235). So the picture lost its top, its status bar, or a whole frame. The host showed the same flicker as the Pi (the host harness at frames 2450-2457: the display on at L22 and off at L198 only in some frames). The old claim "the host picture is static" was wrong: it came from a frame range with no wrong wake.

Fix: `snes_bus.c` reads the IRQ time again while the line runs (`irq_time`, slices of 96 clocks after an IRQ fired). After the fix every frame has `$2100` on at L22 and off at L198, and 8 frames in a row are equal. Other games (Mario Kart, Yoshi's Island, Star Fox, Zelda, Kirby) give the same pictures at frames 400, 900 and 1500. Test: `htime_rewrite_tests` in `tests/snes/core_test.c`.

Speed on the Pi (kernel -92, `line log`): 15-16 pictures a second drawn, 55-57 emulated frames a second, `us frame` 12-18 ms, about 110000 GSU instructions per video frame. One game frame takes 9 video frames, so the game runs at 6-7 fps by design.

## Slowdown and key response after the IRQ fix (host measure, 2026-11-13)

Tool: `tests/snes/doom_input.c` (header has the build line; it needs the user's ROM). It prints the host time of 300 frames, the CPU and GSU instructions in a frame, and the frame at which the ROM reads a pad value.

Measured on the host, frames 1500-3000 of `doom.sfc`, kernel before and after `c6948275`:

| | before the IRQ fix | after the IRQ fix |
|---|---|---|
| GSU instructions a frame | 98000 to 110000 (Pi log: 108000 to 116000) | 187000 |
| 65816 instructions a frame | 25000 to 28000 | 8600 |
| ROM reads of `$4218` in 300 frames | 125 | 225 |
| Host time a frame | about 1.7 to 2.3 ms | the same, within the noise |

Cause of the slowdown: the fix did not add work to the hot loop (the extra IRQ test runs only after an IRQ fired). The game now runs as it should. Before the fix the CPU lost IRQs and waited on wrong wakes, so the Super FX idled about 45% of the time. Now the Super FX works almost all the time (187000 of the 357000 clocks a frame at 21 MHz), as on a real console, and the game takes 1.7 times more GSU work for each emulated second. At 40 to 80 ns for a GSU instruction on the Pi Zero W, the chip alone needs 8 to 15 ms of a 16.7 ms frame. So the Pi can fall below 60 frames a second (not yet measured on the Pi) and the game then runs in slow motion. This is a real cost, not a bug of the IRQ code.

Key response: the core is not late. The pad that `snes_frame` gets is copied at the start of the V-blank (auto-read), and the NMI handler of the ROM reads it in the same frame (test `pad_latency_tests` in `core_test.c`). Doom reads `$4218` at `7E:2818` at line 23 or 24, 3 frames in 4. A key shows in the ROM at the next poll, 1 to 2 frames later, then the game acts on it at its own tick (6 to 10 game frames a second by design). So a key is seen after 150 to 300 ms on a console that holds 60 frames a second, and slower when the Pi falls behind. The cart reads the keys in each `_update` and passes them to `snes_frame` in the same call, so it adds no frame.

Cart fix: `frame_ms` (the predictor of the picture rate in `carts/snes/src/main.lua`) did not count the Super FX time (`us_cpu` leaves it out). For Doom it saw a cheap frame and stepped to a finer picture, which cost more time and slowed the game more. It now adds the `gsu` field of the `us` line.

## Keys that worked only with B held (fixed 2026-11-13, Pi test of kernel 123)

Cause: a core bug in the joypad ports. The auto-read loaded the manual shift register of `$4016` with the pad. Doom reads `$4016` bit 0 after the auto-read to tell a pad from other devices (code at `7E:2818`-`7E:2820`): a 0 sends it to the "no pad" path (`7E:2867`). On the console the register is empty after an auto-read and reads 1. In our core bit 0 was the B button, so the game took the normal path only while B was held (the PS "X" of the Pi pad). Host check: the "no pad" path ran 1442 times in 600 frames with no key and 993 times with B; with the fix it runs 0 times (tool: `WATCHPC=7E2867 doom_input rom 3000 2400 600 <pad>`). Fix in `snes_bus.c`: the auto-read sets both registers to `$FFFF`; a read of `$4016`/`$4017` while the strobe is high returns the B bit. Tests: `manual_pad_tests`; `cpu_golden.c` case 1 hash changed on purpose.

## Music tempo follows the frame time (fixed 2026-11-13, not verified on the Pi)

Cause: the sound CPU and the DSP ran in step with the emulated frames, so a frame of 25 ms made 533 samples while the speakers took 800: the ring ran dry and the music slowed with the game. Fix in `snes_frame_break` (`snes_bus.c`): when an output plays the ring (`snes_audio_queued() >= 0`) and less than two frames of samples wait, the sound runs up to 3 frames more at the start of the frame. `spc_done` keeps the lead, so the CPU's own syncs run the sound only when the CPU passes the lead; the lead is held under 2 seconds. The total sound work per emulated second does not change. The game still slows when the Pi falls below 60 frames; only the music keeps time. Test: `audio_lead_tests`.

## Pi log of kernel 129 (2026-11-13): firing, `spc 1`, `mix 7.4 ms`

- `spc 1` us in the 3D scene was an accounting gap: the sound that runs ahead (section above) ran outside the timers. It is counted in `spc` now. The sound was not starved. Its cost is about the same per second of real time as before (about 2 ms in 16.7 ms), so it adds little to a slow frame.
- Firing: the frame at t=56 had `gins` 218655 and `gsu` 9.7 ms. The Super FX is the load (the chip is the limit in this scene, as in the section on the slowdown). The loop fell to 12 pictures a second because 4 updates run for each draw and `pic` is 1/4.
- `mix 7.4 ms` in scene `m3/13/10`: Doom sets CGWSEL `$A2` (the colour window clips the main screen to black inside, maths with the sub screen), TM `$13`, TS `$10`, windows on BG1, BG2 and OBJ. A clip rule sent the line to the slow per-pixel loop (about 130 cycles a pixel). Now `snes_ppu.c` mixes with the fast paths and `clip_fix` redoes the clipped pixels (black, plus the sub colour when the pixel has the maths flag). Test: `make test-snes-ppu` (4000 cases against the reference) and `test-snes-ppu-arm`. Pi gain not measured: read `mix` in the `ppu us` field of the same scene (7.4 ms before).

## Open work

- Check on the Pi that the flicker is gone (kernel with the IRQ fix).
- The 3D view of the first level shows one band of walls with dark floor and ceiling (indices 156-177 are dark grays in CGRAM). Compare with a picture of a real console: the first room may look like this.
- Speed on the Pi Zero W with the IRQ fix: not measured yet. Run Doom on the next kernel and read the log line (`pi.py line log`): `upd` (emulated frames a second), `pic 1/N`, `gsu` (us), `gins` (about 187000 expected), `us frame`. If `upd` is under 57, the Super FX loop is the limit: the next step is to speed up the hot loop of Doom (ROM `00:E1B6`-`E1C7`: `FROM`, `ROMB`, `GETB`) in `snes_gsu_arm.S` (another session owns that file now, see [snes-superfx.md](snes-superfx.md)).
- Pi: check that the music keeps its tempo in heavy scenes (log: `audio: ... under N` should stay low) and that Up alone moves the player.
- Speed on the RGB30 is not measured for Doom.
- Compare the section map of a retail ROM with `RL.SEC` (the retail build may differ from the source of 1995).
- If a disassembler is needed for the Super FX code of the ROM: write a SLEIGH module or a small script on the opcode table of [snes-superfx-isa.md](snes-superfx-isa.md).
- Read the sound driver in the archive.org backup if the sound or its timing is a suspect.
