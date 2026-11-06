# bm SNES

An emulator of the Super Nintendo, as a `.bm` cartridge written in Lua: `carts/snes/`. It plays `.sfc`
and `.smc` files. Status (2026-11-06): the first version runs a test ROM in bmhost. No commercial ROM
was tried. The speed on the Pi Zero W and the RGB30 is not measured.

## How to use it

1. Copy the ROM to the `/docs` folder of the SD card. Use an 8.3 name (`game.sfc`). A file has at most 4 MB.
2. Start "bm SNES" from the menu. The first time, the console asks if the cartridge may use the files.
3. Choose the ROM with the arrows. Press A.
4. Keys: the pad keys are the SNES keys (A, B, X, Y, L1, R1, Start, Select, the cross).
   Hold L2 and R2 for half a second to open the emulator menu (resume, reset, FPS, back to the list).
5. The battery RAM goes to `/docs/<name>.srm`. The cartridge saves it every 10 seconds, in the menu and at exit.

## What is in it

| File | Content |
|---|---|
| `carts/snes/src/01_core.lua` | memory, PPU registers, APU stub, CPU registers `$42xx`, joypads, bus (LoROM, HiROM, SRAM), DMA and HDMA |
| `carts/snes/src/02_cpu.lua` | the 65C816: all 256 opcodes, emulation and native mode, decimal mode, interrupts |
| `carts/snes/src/03_ppu.lua` | the picture: tile cache in the sprite sheet, bands, BG modes 0-4, sprites |
| `carts/snes/src/04_main.lua` | ROM list, header detection, frame loop, screen, SRAM, menu |
| `carts/snes/build/build.py` | joins the sources, makes the blank sheet, packs `.bme` and `.bm` (`make build/carts/snes.bm`) |
| `tests/snes/` | `cpu_test.lua` (21 checks), `make_testrom.py` (a ROM made for the test), `render_test.py`; run `make test-snes` |

## Rules (do not break)

- The Lua sources are in `carts/snes/src/` and `build.py` joins them. Edit the sources. Do not edit `build/snes/main.lua`.
- A Lua function has at most 200 local variables. The CPU and the PPU files wrap their locals in a `do ... end`
  block. Keep that when you add code.
- The ROM comes only from `doc_read`. The cartridge never writes a ROM. The only file it writes is the `.srm`.
- The picture is drawn at the end of the frame from the state of VRAM, CGRAM and OAM. A change of the
  registers in the middle of the frame makes a new band (`ppu_line`). A change of VRAM, OAM or CGRAM in the
  middle of the frame does not show until the next frame.
- A tile is decoded once into a cell of the 1024 x 1024 sheet. The cache key holds the address, the depth and
  the first colour. A version number of the VRAM block and of the CGRAM row tells when to decode again. Do not
  clear the sheet in the middle of a frame.
- No SNES firmware, ROM data or game code goes into the repository. The test ROM is made by `make_testrom.py`.
- The test ROM and `cpu_test.lua` are the gate for a change of the CPU or the PPU. Run `make test-snes`.

## Open work

- Sound: the SPC700 and the DSP are not emulated. `apu_write` plays the boot ROM handshake and then echoes
  the ports. Games that wait for a real answer from the sound program hang.
- Speed: measure on the Pi Zero W and the RGB30 with a real game. The line budget is `LINE_CYC` (190).
  `frameskip(4)` keeps the game speed when a frame is late. Ideas: a CPU loop without the `ops[op]()` call
  for the most common opcodes, a ROM table of 64 KB pages, no band split below 8 lines.
- Picture: mode 7 (the rotating plane), hires modes 5 and 6, windows, mosaic, color math (transparency),
  the subscreen, offset-per-tile (modes 2 and 4), 8x8 sprites with the sprite limits per line, direct color.
- Timing: the instruction costs are one flat value (4 cycles). Games that count cycles (raster tricks) break.
  Add a real cost table and the memory speed of each bank.
- Cartridge chips: SuperFX, SA-1, DSP-1 and the other co-processors are not emulated. ExHiROM, ExLoROM
  and interleaved ROMs are not detected.
- Input: player 2 reads `btn(name, 2)`. The multitap, the mouse and the light gun are not emulated.
- Put the cartridge in `GAMES` and in the Market after a test on both devices.
- Save states and a speed-up key.
