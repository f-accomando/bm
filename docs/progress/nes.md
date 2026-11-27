# bm NES

An emulator of the Famicom and the NES. The machine is written in C and is part of the kernel (`core/src/bm/nes.c`, one
global machine). The cartridge `bm/nes/` is its user interface in Lua: it lists the ROMs, passes the pads and draws the
picture. It is built like [bm SNES](snes.md). Both kernels have it (Pi and RGB30). Status (2026-11-11): the core passes
its tests (`make test-nes`) and the CPU matches the log of the public test ROM nestest instruction by instruction, with
the cycles. The speed on the Pi Zero W and the RGB30 is not measured. The first commercial game tried (a European Super Mario Bros, 2026-11-27) ran too fast as NTSC: PAL was added for it.

## How to use it

1. Make the folder `/nes` at the root of the SD card and copy the `.nes` files there. The name can be long. A file has at most 2 MB.
   (The cartridge reads it with `doc_list("nes")` and `doc_read(name, "nes")`; the permission is the one of the documents.)
2. Start "bm NES" from the menu. The first time, the console asks if the cartridge may use the files.
3. Choose the ROM with the arrows. Press A.
4. Keys: the pad directions are the NES cross. The bottom and right buttons are the NES A. The left and top buttons are the NES B.
   Start and Select are the same. The keyboard: arrows or WASD, O (B), P (A), `,` (Select), `.` (Start).
   Press Esc (keyboard, only for the `.bm` menu; Ctrl+Esc exits to bm), or hold L2 and R2 for half a second (pad), to open the emulator menu (resume, reset, FPS, scale, Log, back
   to the list); Esc or B closes it.
5. Region in the menu: "Auto" (default), "NTSC" (60 frames a second), "PAL" (50 frames a second). A European game runs 20% too fast as NTSC.
   Auto takes the header of the file, then the tags of its name (`(E)`, `(Europe)`, `(PAL)`, a European country; a name that also says USA or Japan is NTSC); else NTSC.
   Most European dumps have no region in the header: choose PAL in the menu, or put `(E)` in the file name. A change of region starts the game again.
   The choice is kept for each ROM in `/docs/nesreg.txt`. On the 60 Hz screen a PAL game shows one picture of every five twice (a slight judder in the scrolling).
6. Scale in the menu: "sharp" (default whole-number scale), "native 256x256" (the 8th screen mode of the Pi: 1x in a 256x256 page, 4x at
   whole pixels on a 1080p TV), "GPU 2x (540p)", "GPU 3x (720p)", "fit 4:3". The rules are the same as in bm SNES.
7. The battery RAM goes to `/docs/<name>.nsv` (the first 8 letters of the ROM name). The cartridge saves it every 10 seconds,
   in the menu and at exit.

## What is in it

| File | Content |
|---|---|
| `core/src/bm/nes.h` | the public calls of the core and the pad bits |
| `core/src/bm/nes.c` | the whole machine: the cartridge and the mappers, the picture unit, the sound and the 6502 (see below) |
| `core/src/bm/api_nes.c` | the Lua calls (`nes_load`, `nes_frame`, `nes_draw`, ...): [api/nes.md](../api/nes.md) |
| `bm/nes/src/main.lua` | the user interface; `bm/nes/build/build.py` packs it (`make build/bm/nes.bm`); `bm/nes/nes.bme` is the project |
| `tests/nes/core_test.c` | the CPU on small programs, the PAL frame, header and sound, the picture, the NMI, the sprite 0 hit, the sound, MMC1, MMC3 and its IRQ, MMC2 and its latches, FME-7 and its cycle IRQ, bad files; with `build/nestest.nes` and `build/nestest.log` it runs nestest |
| `tests/nes/make_testrom.py`, `render_test.py` | the cartridge in bmhost with a test ROM made by the script: the picture, a moving sprite, the pad, the menu, the PAL speed and the saved region |

The parts of `nes.c`:

- The 2A03 CPU: all official opcodes, and the unofficial ones (LAX, SAX, DCP, ISB, SLO, RLA, SRE, RRA, the NOPs, ANC, ALR, ARR, AXS). The decimal flag has no effect. The JAM opcodes stop the CPU.
- The picture unit: the registers `v`, `t`, `x`, `w`, the name table mirroring, 33 background tiles and 8 sprites a line, the sprite 0 hit with its dot, the overflow flag, the vblank and the NMI.
- The sound: two pulse channels (envelope, sweep), the triangle, the noise, the DMC, the frame counter (4 and 5 steps) and the two IRQ sources. The mixer uses the non-linear tables; the output is a ring of 48 kHz samples read by `nes_audio_mix`.
- The mappers: 0 (NROM), 1 (MMC1), 2 (UxROM), 3 (CNROM), 4 (MMC3, with the line counter and the IRQ), 7 (AxROM), 9 (MMC2, Punch-Out!!), 11 (Color Dreams), 34 (BNROM), 66 (GxROM), 69 (Sunsoft FME-7, without the 5B sound), 71 (Camerica), 79 (NINA-03/06).

## Rules (do not break)

- The machine is plain C with no dependence on the kernel, so the host tests build it alone. Keep `nes.c` free of kernel headers.
- The time is in CPU cycles. A line is 341 dots. The vblank starts at line 241. The last line of a frame is the pre-render line.
- Keep every value that depends on the region in `set_timing`: NTSC has 3 dots to a cycle and 262 lines; PAL has 3.2 dots to a cycle and 312 lines, no short odd frame, and its own noise, DMC and frame counter tables.
- The sound of a PAL frame is 960 samples (50 frames a second exactly), not the console's 50.007. The caller runs 50 frames a second.
- A visible line is drawn at its start from the registers at that moment. At dot 257 the vertical bits of `v` go up by one line and the horizontal bits come from `t`.
  A write to `$2006` after dot 257 works on the next line. A write in the middle of a line does not change that line.
- The PPU registers are read and written through `ppu_read` and `ppu_write`. A read of `$2002` checks the sprite 0 dot (`s0_dot`) at that moment.
- The mapper IRQ counter of MMC3 is clocked once a line (dot 261) while the picture is on, not by the A12 line. Keep this rule until a game needs the A12 line.
- MMC2 sets its latches from the picture fetches. Fetch the background tiles left to right and the sprite patterns from the first sprite to the last, as the hardware does. Call `mmc2_fetch` after each high plane read. On mapper 9 do the fetches also on the lines that are not drawn.
- The FME-7 counter goes down once a CPU cycle in `step`. The IRQ comes when it goes below 0.
- The ROM comes only from `doc_read(name, "nes")`. The cartridge never writes a ROM. It writes only the `.nsv` files and `nesreg.txt`.
- No NES firmware, ROM data or game code goes into the repository. The test ROMs are made by `core_test.c` and `make_testrom.py`. The nestest ROM and log stay in `build/` (not in git).
- The core tests are the gate for a change of the CPU, the picture unit or a mapper. Run `make test-nes`.
- The 64 colours of `nes_rgb` are the common palette of the emulators. A change of the palette changes the colours in `core_test.c` and `render_test.py`.

## Open work

- PAL (2026-11-27): check a European game on the Pi Zero W (speed, music, the status bar of a game with a sprite 0 split). The Dendy region runs as PAL.
- Test with real games on the Pi Zero W and the RGB30. Measure the time of a frame (the CPU 1.79 MHz, the 33 tiles and 8 sprites a line, the sound loop that runs once for each CPU cycle). A faster sound loop (a step for many cycles) is the first idea if the Pi is too slow.
- Mappers: MMC5, VRC2/4/6/7, Namco 163, the Sunsoft 5B sound of FME-7 (Gimmick!), and the other ones with extra sound chips. A mapper that is not in the list gives the message `mapper N is not supported`.
- Picture: the A12 line for MMC3, the sprite evaluation bugs, the colour emphasis bits, the odd-frame cycle skip is done but the first line timing is not exact, the "left 8 pixels" and the overscan crop (the top and bottom 8 lines).
- CPU: the dummy reads and writes (a few games use them with `$2007` and MMC1), the one-instruction delay of CLI/SEI/PLP, the unstable opcodes (XAA, AHX, TAS, SHY, SHX, LAS only as NOPs or simple forms).
- Sound: the DMC DMA stall is fixed at 4 cycles. The output is 48 kHz fixed. The frame is 60 updates a second, the NES is 60.0988: a small gap in the sound now and then.
- Input: only the two standard pads. The Zapper, the Four Score and the Famicom keyboard are not emulated.
- Formats: NES 2.0 sizes, `.fds` disks, NSF files and `.unf`/UNIF are not read. A header with trainer is skipped.
- Save states and a speed-up key.
- Put the cartridge in `GAMES` after a test on both devices. Add it to the Market (apps/nes).
