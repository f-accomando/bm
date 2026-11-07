# bm SNES

An emulator of the Super Nintendo. The machine is written in C and is part of the kernel (`src/bm/snes_*.c`, one
global machine). The cartridge `carts/snes/` is its user interface in Lua: it lists the ROMs, passes the pads and
draws the picture. Both kernels have it (Pi and RGB30). Status (2026-11-07): the C core passes its tests
(`make test-snes`) and plays the test ROM. A first version in Lua ran at 2 fps on the Pi Zero W, so the machine was
written again in C. No commercial ROM was tried yet and the speed on the Pi Zero W and the RGB30 is not measured.

## How to use it

1. Make the folder `/sfc` at the root of the SD card and copy the ROM there. The name can be long. A file has at most 4 MB.
   (The cartridge reads it with `doc_list("sfc")` and `doc_read(name, "sfc")`; the permission is the one of the documents.)
2. Start "bm SNES" from the menu. The first time, the console asks if the cartridge may use the files.
3. Choose the ROM with the arrows. Press A.
4. Keys: the pad keys are the SNES keys (A, B, X, Y, L1, R1, Start, Select, the cross). The SNES buttons are
   A right, B bottom, X top, Y left. The RGB30 and Nintendo pads have the same names. On a keyboard or an Xbox pad the
   cartridge swaps A with B and X with Y, so the bottom button is the SNES B. The keyboard: arrows or WASD, Space or Z
   (B), X (A), C (Y), V (X), Q and E (L and R), Enter (Start), Tab (Select).
   Hold L2 and R2 (keyboard: Q and E) for half a second to open the emulator menu (resume, reset, FPS, back to the list).
5. The battery RAM goes to `/docs/<name>.srm` (the first 8 letters of the ROM name). The cartridge saves it every 10 seconds,
   in the menu and at exit.
6. A game that keeps the screen off shows an orange line at the bottom after 3 seconds: the PC of the CPU, the last registers it
   read, and the state of the sound CPU. Send it when a game stays black. The menu item "FPS" shows `fps`, `blank` (INIDISP),
   `tm` (layers on) and the sound CPU.

## What is in it

| File | Content |
|---|---|
| `src/bm/snes.h` | the public calls of the core and the pad bits |
| `src/bm/snes_int.h` | the machine state (`snes_t S`) shared by the files |
| `src/bm/snes_cpu.c` | the 65C816: all 256 opcodes, emulation and native mode, decimal mode, interrupts, WAI, STP |
| `src/bm/snes_bus.c` | the memory map (LoROM, HiROM, WRAM, battery RAM), the PPU and CPU registers, joypads, DMA and HDMA, the frame clock (262 lines of 1364 master clocks), IRQ and NMI, the ROM loader |
| `src/bm/snes_ppu.c` | the picture, a line at a time: BG modes 0-4 and 7, sprites, windows, colour math, brightness |
| `src/bm/snes_spc.c` | the SPC700 (all opcodes, three timers, four ports) and the boot ROM protocol as a state machine |
| `src/bm/api_snes.c` | the Lua calls (`snes_load`, `snes_frame`, `snes_draw`, ...): [api/snes.md](../api/snes.md) |
| `carts/snes/src/main.lua` | the user interface; `carts/snes/build/build.py` packs it (`make build/carts/snes.bm`) |
| `tests/snes/core_test.c` | 92 checks: the CPU on small programs, DMA, the SPC700 and its boot protocol |
| `tests/snes/render_test.py` | the cartridge in bmhost with a test ROM made by `make_testrom.py`: the picture, the sprite, the HDMA bands |

## Rules (do not break)

- The machine is plain C with no dependence on the kernel, so the host tests build it alone. Keep `snes_*.c` free of kernel headers.
- The time is in master clocks. Every bus access adds its cost (`access_clk`), an instruction adds 6 for the internal cycles.
  A line is 1364 clocks. NMI starts at line 224 (239 with overscan), HDMA runs before each visible line.
- The picture is drawn at the end of each line (`ppu_line`) from the registers at that moment, so effects in the middle of the frame show.
- The sound program is uploaded through the boot ROM state machine (`spc_port_write`). A write to port 0 is the command; its data is in
  port 1. A 16-bit store writes port 1 after port 0, so the state machine works at the next read or write of another port.
- The ROM comes only from `doc_read(name, "sfc")`. The cartridge never writes a ROM. The only file it writes is the `.srm`.
- No SNES firmware, ROM data or game code goes into the repository. The test ROM is made by `make_testrom.py`.
- The core tests are the gate for a change of the CPU, the PPU or the SPC700. Run `make test-snes`.

## Open work

- Speed: measure on the Pi Zero W and the RGB30 with a real game (the FPS item of the menu). If it is short: draw only the layers
  that are on, skip the sub screen when no colour math is on, a fast path for the CPU's most common opcodes.
- Sound: the DSP is not emulated, its registers are stored and no sound is made. The SPC700 runs at full speed (65 cycles a line).
- Picture: hires modes 5 and 6, mosaic, offset-per-tile (modes 2 and 4), direct colour, interlace, mode 7 EXTBG, the sprite
  limits of 34 tiles a line.
- Timing: the cost of an instruction is the sum of its accesses plus 6 clocks. Games that count cycles (raster tricks) may break.
  Add the exact costs and the DMA and HDMA stalls.
- Cartridge chips: SuperFX, SA-1, DSP-1 and the other co-processors are not emulated. ExHiROM and interleaved ROMs are not detected.
- Input: player 2 reads `btn(name, 2)`. The multitap, the mouse and the light gun are not emulated.
- The viewer of tiles, sprites and colours of the Lua version is gone. Add `snes_vram()` and friends to bring it back.
- Save states and a speed-up key.
- Put the cartridge in `GAMES` after a test on both devices. It is in the Market (apps/snes).
