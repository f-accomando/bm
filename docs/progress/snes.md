# bm SNES

An emulator of the Super Nintendo. The machine is written in C and is part of the kernel (`src/bm/snes_*.c`, one
global machine). The cartridge `carts/snes/` is its user interface in Lua: it lists the ROMs, passes the pads and
draws the picture. Both kernels have it (Pi and RGB30). Status (2026-11-08): Super Mario World plays on the Pi Zero W with the backgrounds, the overworld map and sound (the user's ROM,
checked with the bridge; the sound is heard by the user). The picture is drawn at 25 to 50 frames a second and the game logic runs at
about 60 updates a second. The C core passes its tests (`make test-snes`, 175 checks). A first version in Lua ran at 2 fps, so the
machine was written again in C. The RGB30 is not measured.

## How to use it

1. Make the folder `/sfc` at the root of the SD card and copy the ROM there. The name can be long. A file has at most 4 MB.
   (The cartridge reads it with `doc_list("sfc")` and `doc_read(name, "sfc")`; the permission is the one of the documents.)
2. Start "bm SNES" from the menu. The first time, the console asks if the cartridge may use the files.
3. Choose the ROM with the arrows. Press A.
4. Keys: the pad keys are the SNES keys (A, B, X, Y, L1, R1, Start, Select, the cross). The SNES buttons are
   A right, B bottom, X top, Y left. The RGB30 and Nintendo pads have the same names. On a keyboard or an Xbox pad the
   cartridge swaps A with B and X with Y, so the bottom button is the SNES B. The keyboard: arrows or WASD, Space or Z
   (B), X (A), C (Y), V (X), Q and E (L and R), Enter (Start), Tab (Select).
   Hold L2 and R2 (keyboard: Q and E) for half a second to open the emulator menu (resume, reset, FPS, scale, back to the list).
   Scale in the menu: "sharp" (default), "native 256x256", "GPU 2x (540p)", "GPU 3x (720p)", "fit 4:3". "native" asks for `screen(256, 256)` (the 8th mode of the Pi; the console
   puts a square screen in the middle of a 480x270 one): the picture is 1x in a 256x256 page, and a 1080p TV shows it 4x at whole pixels (1024 x 896), with no GPU work. Pi One: 59-60 updates a second
   and a picture every frame (the controller reaches 1/1); on a 720p TV the 480x270 page is 2.67x, not whole. The GPU modes ask for a bigger screen (`screen(960, 540)` or
   `screen(1280, 720)`), copy the picture into the sprite sheet (`snes_to_sheet`) and draw it with `sspr` at zoom 2 or 3, after a tiny 3D triangle that opens the GPU's
   job (the 2D that follows is drawn by the GPU: `gpu3d_2d`). 960x540 shows at whole pixels on a 1080p TV (each pixel of the game is 4 x 4 pixels of the TV), 1280x720 on a 720p
   TV; a console that cannot give the size keeps the mode it had (the RGB30 has other sizes).
   Scale "sharp" (default) uses a whole-number scale (1x on a 360-line screen, 2x from 448 lines), so every pixel has the same size; the
   picture follows the line count (224, or 239 for PAL and overscan). Scale "fit 4:3" fills the screen height; its pixels are uneven.
   A steps-of-0.5 scale (1.5x) was tried on the Pi One on 2026-11-09: the pixels still look uneven (2 and 1 in turn).
5. The battery RAM goes to `/docs/<name>.srm` (the first 8 letters of the ROM name). The cartridge saves it every 10 seconds,
   in the menu and at exit.
6. A game that keeps the screen off shows two orange lines at the bottom after 3 seconds: the PC of the CPU, the last registers it
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
| `src/bm/snes_dsp.c` | the S-DSP: 8 voices (BRR, pitch, pitch modulation, cubic interpolation, ADSR and GAIN, noise, volumes), a ring of 32 kHz samples, and `snes_audio_mix`, which the console's output calls (`audio_set_ext` in `src/audio/audio.c`) |
| `src/bm/snes_ppu_arm.S` | the colour mix with a sub screen for the ARM1176 (Pi): the same result as the C loop in `snes_ppu.c`, check on the console |
| `src/bm/api_snes.c` | the Lua calls (`snes_load`, `snes_frame`, `snes_draw`, ...): [api/snes.md](../api/snes.md) |
| `carts/snes/src/main.lua` | the user interface; `carts/snes/build/build.py` packs it (`make build/carts/snes.bm`) |
| `tests/snes/core_test.c` | 92 checks: the CPU on small programs, DMA, the SPC700 and its boot protocol |
| `tests/snes/dsp_golden.c` | the S-DSP on random voices: the output of `snes_audio_mix` and the registers hashed, against the table of the code with the integer cubic (`make test-snes-dsp`, `make bench-snes-dsp`); the float cubic that came before differs by a few steps of 16 bit (8 of 10 cases within 4 steps; two cases with pitch modulation drift apart in time) |
| `tests/snes/cpu_golden.c` | the CPU and the memory map on random programs (LoROM and HiROM, ROMs of odd sizes, with and without battery RAM): a hash of the registers, the clock and the memories after each frame, against the table of the code before the speed work (`make test-snes-cpu`) |
| `tests/snes/ppu_equiv.c`, `ppu_ref.c` | the fast PPU against the reference copy of the old one: random states, every line equal (`make test-snes-ppu`); `make bench-snes-ppu` times both |
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
- The core tests are the gate for a change of the CPU, the PPU or the SPC700. Run `make test-snes`. After a change of `snes_ppu.c` run `make test-snes-ppu` too: it must stay bit for bit equal to `tests/snes/ppu_ref.c` (change the reference only for a new PPU feature, in both files).
- The CPU reads and writes memory through the page tables (`snes_read_f`, `snes_write_f` in `snes_int.h`); `snes_map_build` makes them and runs after a reset and a write to $420D. A page is fast only when its first and last byte are plain memory at the same distance, so a ROM that is not a multiple of 4 KB still mirrors right. A change of the memory map (a new chip, a bank rule) goes in `map_ptr` and in `raw_read` together; `make test-snes-cpu` must pass with the same hashes.
- The BG scroll registers ($210D to $2114) keep the 16 bits of the write; the picture uses the low 10 bits. The second write of a pair takes bits 0 to 2 from the first one (`bgofs_latch`, bits 8 to 10 of the old value). A mask to 10 bits at the first write loses bit 2: the map was then up to 4 pixels off from the sprites (Mario not centred on the house of the SMW overworld). `core_test.c` checks all 1024 values.
- Every write to the video RAM goes through `vwrite` in `snes_bus.c`, which calls `ppu_vram_dirty`: the PPU keeps decoded tiles. A new way to write the video RAM must call it too (a test that fills `S.vram` calls `ppu_vram_dirty_all`). `ppu_equiv.c` writes the video RAM through the registers in the middle of a case to check it.
- The lane helpers of `snes_ppu.c` (`nz_mask`, `lane_uqadd`, ...) use the ARMv6 SIMD32 instructions on the console and plain C on the host. Keep the C version in step: `make test-snes-ppu` runs only the C one, so check an ARM change on the console (the test build with `SNES_NOHUD=1` stops at frame 3200: compare two screenshots byte for byte).
- The PPU reads and writes its line buffers a 32-bit word at a time from arrays aligned to 4 bytes (`_Alignas(4)`); keep every access aligned (the ARM1176 kernel).

## Open work

- Speed: the PPU is done in C with ARMv6 SIMD (the layers merged 4 pixels at a time with UADD8 and SEL, the colour math of the three channels in one
  UQADD8, UQSUB8 or UHADD8, a table of mixed colours for the fixed colour, shifts in place of the divides: the ARM1176 has no divide), the colour mix with
  a sub screen is a hand-written loop (`src/bm/snes_ppu_arm.S`, 28 instructions a pixel), and the tiles are decoded once: a cache of chunky pixels for each
  depth (450 KB of bss), dropped by `ppu_vram_dirty` when the video RAM is written, a row of a tile is two loads. Pi One, Super Mario World, the test build
  draws every frame from 600 to 3200 and sums the parts: the PPU costs 19162 us a drawn frame with the code of before, 16863 with the SIMD in C, 16152 with
  the loop in assembly, 14804 with the tile cache (-23%); each timer read costs about 6 us, so the real figures are 12100, 9800, 9100 and 7700 us (-36%).
  The memory map is a table of 4 KB pages (`snes_rtab`, `snes_wtab`, `snes_ctab` in `snes_bus.c`): the CPU reads ROM, work RAM and battery RAM with one
  table lookup, the registers take the slow path. On the Pi One a frame of the test build (update, with the machine and the Lua of the test) went from
  25.25 ms to 22.69 ms (-10%) with the PPU unchanged: the CPU part is about 37% lower. The sound measured 3017 us a frame in the test build (the timer's own cost is about 1400 us of it). The DSP makes the envelope steps from a table (the modulo was a library call on the ARM1176) and the cubic in integers (three 32 x 32 to 64 bit products): 2746 us measured, 22.66 to 22.27 ms a frame (Pi One, 2026-11-09). With `dsp_tick` switched off in a test kernel, `spc` fell to 1368 us and the frame to 20.51 ms: the DSP costs about 1.4 to 1.9 ms a frame (8 voices, 533 samples a frame, about 300 cycles a voice), and the SPC700's own loop is almost nothing, so it is not worth a rewrite. A further gain needs fewer operations in `dsp_tick` (a voice at a time, in 16-bit integers), or a lower rate for voices that are silent.
  The picture is identical byte for byte at frame 3200 after each step (checked on the Pi One).
  GPU 2x on the Pi One (2026-11-09, test build, picture 1 in 4): 60.6 updates and 30 loop frames a second at 960x540 (512 x 448 of the picture); GPU 3x at
  1280x720: 49.5 updates and 13 frames a second (the empty 720p frame alone costs 44 ms and the job 8 ms: the 720p output is the cost, not the picture).
  The cart makes a picture every 1 to 4 emulated frames, by the speed it measures (`adapt` in `main.lua`: a window of 90 updates; under 57 a second, a
  picture less often; three windows at 58.8 or more, a picture more often). Pi One, 2026-11-09: 58 updates and 30 pictures a second (`SNES_ADAPT=1` makes
  a test build with a one-line readout: fps, updates a second, the interval).
  On a PC the PPU is 3 times faster than the first C version (`make bench-snes-ppu`). Next: the mix (about 3950 us a frame), the sprites and the layer
  merge, the CPU's instruction decode (the flags as bits in one byte, a table of opcodes). Measure on the RGB30. The draw of the picture is 5.7 ms at 1x (copy 1.0, blit 4.6).
- A test build: `SNES_TEST=1 python3 carts/snes/build/build.py build/snes-test` makes a cartridge that opens the first ROM of /sfc by
  itself and shows the FPS, the times of the parts of a frame, the PPU registers and the first sprites; it draws every frame from 600 to 3200, sums the PPU parts
  (the line `PPU n fr`) and stops at frame 3200 (`SNES_NOHUD=1`: no text, for a picture to compare). With the bridge (`pi.py play`, `pi.py shot`) it runs a ROM and reads the
  screen with no key press.
- Sound: the echo and its FIR filter are not done (the echo buffer is not written; games that use echo sound drier), the Gaussian table is a
  cubic interpolation, a voice has no start delay, the envelope timing follows the public tables from memory. The output is 32 kHz
  resampled to 48 kHz by linear interpolation. If the game is slower than real time the sound has gaps (the debug line counts them).
- Sprite priority rotation ($2103 bit 7) is done: Super Mario World uses it (first sprite 120 on the overworld; without it the Mario head icon was hidden under 12 blank sprites). Found with the test build's OAM and register lines and checked on the Pi One (2026-11-09).
- Picture: hires modes 5 and 6, mosaic, offset-per-tile (modes 2 and 4), direct colour, interlace, mode 7 EXTBG, the sprite
  limits of 34 tiles a line.
- Timing: the cost of an instruction is the sum of its accesses plus 6 clocks. Games that count cycles (raster tricks) may break.
  Add the exact costs and the DMA and HDMA stalls.
- Cartridge chips: SuperFX, SA-1, DSP-1 and the other co-processors are not emulated. ExHiROM and interleaved ROMs are not detected.
- Input: player 2 reads `btn(name, 2)`. The multitap, the mouse and the light gun are not emulated.
- The viewer of tiles, sprites and colours of the Lua version is gone. Add `snes_vram()` and friends to bring it back.
- Save states and a speed-up key.
- The GPU 3x mode runs at about 50 updates a second on the Pi One: a smaller cost for the 720p frame, or drawing less often, would help. The `dsp_tick` skip of silent voices is exact and gives no measurable gain (spc 2746 against 2776 us).
- Put the cartridge in `GAMES` after a test on both devices. It is in the Market (apps/snes).
