# bm SNES

An emulator of the Super Nintendo, as a `.bm` cartridge written in Lua: `carts/snes/`. It plays `.sfc`
and `.smc` files. Status (2026-11-07): version 0.2 runs a test ROM in bmhost. It has the SPC700, the keyboard and a cover. A real game
was shown black on the Pi Zero W with version 0.1 (the sound CPU was a stub); version 0.2 is not yet tried on a real ROM.
The speed on the Pi Zero W and the RGB30 is not measured.

## How to use it

1. Make the folder `/sfc` at the root of the SD card and copy the ROM there. The name can be long. A file has at most 4 MB. (The cartridge reads it with `doc_list("sfc")` and `doc_read(name, "sfc")`; the permission is the one of the documents.)
2. Start "bm SNES" from the menu. The first time, the console asks if the cartridge may use the files.
3. Choose the ROM with the arrows. Press A.
4. Keys: the pad keys are the SNES keys (A, B, X, Y, L1, R1, Start, Select, the cross). The SNES buttons are
   A right, B bottom, X top, Y left. The RGB30 and Nintendo pads have the same names. On a keyboard or an Xbox pad the
   cartridge swaps A with B and X with Y, so the bottom button is the SNES B. The keyboard: arrows or WASD, Space or Z
   (B), X (A), C (Y), V (X), Q and E (L and R), Enter (Start), Tab (Select).
   Hold L2 and R2 (keyboard: Q and E) for half a second to open the emulator menu (resume, reset, sprite viewer, FPS, back to the list).
5. The battery RAM goes to `/docs/<name>.srm`. The cartridge saves it every 10 seconds, in the menu and at exit.

## The viewer

Menu, "Sprite viewer". The emulation is paused. L1 and R1 change the view.
- Tiles (VRAM): 16 x 16 tiles a page, 2, 4 or 8 bits per pixel (Y), the palette (left and right), the page (up, down, L2, R2).
- Sprites (OAM): 32 sprites a page, each as stored (no flips), with the sizes of OBSEL.
- Colours (CGRAM): the 256 colours.
- A saves the view as a PNG in `/docs`. X saves all of it: the whole VRAM (16 tiles a row), all 128 sprites, the colours.
  Names: the first 4 letters of the ROM name, a letter (`t` tile page, `v` all VRAM, `s` sprite page, `o` all sprites,
  `p` colours) and two digits. bm Pixel exports to the same folder. Open the file there as a picture.

## What is in it

| File | Content |
|---|---|
| `carts/snes/src/01_core.lua` | memory, PPU registers, APU stub, CPU registers `$42xx`, joypads, bus (LoROM, HiROM, SRAM), DMA and HDMA |
| `carts/snes/src/02_cpu.lua` | the 65C816: all 256 opcodes, emulation and native mode, decimal mode, interrupts |
| `carts/snes/src/03_ppu.lua` | the picture: tile cache in the sprite sheet, bands, BG modes 0-4, sprites |
| `carts/snes/src/04_viewer.lua` | the viewer of tiles, sprites and colours, and the PNG export |
| `carts/snes/src/05_main.lua` | ROM list, header detection, frame loop, screen, SRAM, menu |
| `carts/snes/build/build.py` | joins the sources, makes the blank sheet, packs `.bme` and `.bm` (`make build/carts/snes.bm`) |
| `tests/snes/` | `cpu_test.lua` (21 checks), `make_testrom.py` (a ROM made for the test), `render_test.py`; run `make test-snes` |

## Rules (do not break)

- The Lua sources are in `carts/snes/src/` and `build.py` joins them. Edit the sources. Do not edit `build/snes/main.lua`.
- After a change in `src/`, run `make build/carts/snes.bme` and copy it to `carts/snes/snes.bme`. That file is the project.
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

- Sound: the SPC700 runs (`carts/snes/src/00_spc.lua`, all opcodes, the timers, the four ports), so the games that wait for the
  sound program go on. The boot ROM is played by `apu_write`: it takes the upload of the sound program and the jump. The
  DSP is not emulated: its registers are stored and no sound is made. The SPC700 runs at about half speed (`APU_LINE`).
  Not done: the DSP and the audio output, the exact cycle cost of the SPC700. Test: `tests/snes/spc_test.lua`.
- Black screen: a game that shows nothing waits for something. The FPS line of the menu shows the INIDISP register
  (`blank 8x`: the screen is off), TM (the layers on) and the state of the sound CPU (`boot`, `run`, `halt`, with its PC).
  Send this line when a game stays black.
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
- Put the cartridge in `GAMES` after a test on both devices. It is in the Market (apps/snes) as version 0.2.
- Viewer: the tile page shows 4 bpp first, also for 2 bpp games (press Y). Add the tilemap view (BG1-4 as the PPU sees them)
  and a sprite sheet with the flips applied. Check that bm Pixel opens the PNG from `/docs` (it exports there).
- Save states and a speed-up key.
