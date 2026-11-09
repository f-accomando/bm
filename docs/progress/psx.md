# bm PSX

An emulator of the PlayStation 1. The machine is written in C and is part of the kernel (`src/bm/psx_*.c`, one
global machine). The cartridge `carts/psx/` is its user interface in Lua: it lists the games, passes the pads and
draws the picture. Both kernels have it (Pi and RGB30). Status (2026-11-09): the core and the cartridge exist and
pass their tests on the PC (`make test-psx`, 38 checks; `make test-psx-cart`, the cartridge in bmhost with a test
PS-EXE). On the Pi Zero W (2026-11-09) a real BIOS boots to the PlayStation logo and the "Licensed by" screen, reads the disc, and starts the game's code, at about 8 to 11 frames a second. Crash Bandicoot 2 loads from the disc and plays its logo animation (see Open work). The RGB30 is not measured.

The core is original code (clean room). The hardware facts come from public documentation. No code of pcsx-rearmed
(GPL-2.0) or of any other emulator is copied: the repository license does not allow it. The module split follows the
usual one of PS1 emulators (R3000A, GTE, GPU, SPU, CD-ROM, DMA, timers).

## How to use it

1. Make the folder `/psx` at the root of the SD card. Copy the BIOS there (a 512 KB file, for example `scph1001.bin`).
   The BIOS is not part of bm. Use the one of your own console.
2. Copy the games to `/psx`: a `.cue` with its `.bin` (the cartridge reads the first FILE line of the cue), a `.bin`
   or `.img` alone (2352 bytes a sector), an `.iso` (2048 bytes a sector), or a PS-EXE (`.exe`, `.psx`).
   A disc is read sector by sector from the card, so its size is free. A PS-EXE can run without a BIOS.
3. Start "bm PSX" from the menu. The first time, the console asks if the cartridge may use the files.
4. Choose the game with the arrows. Press A. X refreshes the list.
5. Keys: the pad keys are the PS1 keys. On the RGB30 and Nintendo pads the bottom button is Cross, the right one
   Circle, the top one Triangle and the left one Square. On a keyboard or an Xbox pad the cartridge swaps them
   (bm A is the bottom button there). Hold Select and Start for 0.75 s to open the menu (resume, reset, FPS, back to the list).
6. The menu item "FPS" shows the frame time, the PC, the interrupt registers and the number of bus accesses that
   nothing answers (`unhandled`, with the last address). Send those lines when a game stays black.

## What is in it

| File | Content |
|---|---|
| `src/bm/psx.h` | the public calls of the core, the pad bits, the disc callback |
| `src/bm/psx_int.h` | the machine state shared by the files; every global symbol starts with `psx_` (the kernel links the SNES core too) |
| `src/bm/psx_cpu.c` | the R3000A: MIPS I with the branch and load delay slots, COP0 (SR, CAUSE, EPC, exceptions, interrupts), the call into COP2, cache isolation. An instruction costs 2 cycles |
| `src/bm/psx_jit.c` | the dynamic recompiler MIPS to ARMv6 (Pi Zero W only): blocks of up to 48 instructions to a branch with its delay slot; RAM and scratchpad loads and stores inline; the GTE, add with trap and the rest through helpers; code pages of 256 bytes; `psx_jit_selftest` runs random programs both ways |
| `src/bm/psx_gte.c` | the GTE: RTPS, RTPT, NCLIP, OP, DPCS, INTPL, MVMVA, NCDS, CDP, NCDT, NCCS, CC, NCS, NCT, SQR, DCPL, DPCT, AVSZ3, AVSZ4, GPF, GPL, NCCT, with the division table and the saturation flags |
| `src/bm/psx_bus.c` | the memory map (RAM, scratchpad, BIOS, I/O), I_STAT/I_MASK, the 3 timers, DMA channels 2, 3, 4, 6 (instant), the digital pad port, the frame loop (263 lines of 2154 cycles, vertical blank at line 240), the PS-EXE loader, the BIOS calls for a run without BIOS (putchar, puts, printf) |
| `src/bm/psx_gpu.c` | the software GPU: GP0 polygons (flat, Gouraud, textured 4/8/15 bit, semi-transparent), lines and polylines, rectangles, fills, VRAM copies, texture window, mask bit, dither; GP1; the picture from the VRAM (15 and 24 bit) |
| `src/bm/psx_cd.c` | the CD-ROM controller: registers, FIFOs, interrupts, the commands a BIOS and a game use, the sectors from a callback |
| `src/bm/psx_spu.c` | the SPU: 24 ADPCM voices, pitch (and modulation), linear interpolation, ADSR, volumes, the 512 KB sound RAM, DMA; 44.1 kHz into a ring, `psx_audio_mix` resamples it to the output |
| `src/bm/api_psx.c` | the Lua calls (`psx_list`, `psx_boot`, `psx_frame`, `psx_draw`, ...): [api/psx.md](../api/psx.md). It reads the BIOS, the cue sheet and the sectors with `fat_read_at` |
| `src/fs/fat.c` | `fat_read_at`: a range of a file, read in place, with a cluster cursor |
| `carts/psx/src/main.lua` | the user interface; `carts/psx/build/build.py` packs it (`make build/carts/psx.bm`) |
| `tests/psx/core_test.c` | 38 checks: the CPU on small programs, BIOS calls, interrupts, GTE, GPU, DMA, CD-ROM, SPU |
| `tests/psx/boot_probe.c` | a PC probe: boots a BIOS with a disc given on the command line and prints the CD traffic (no BIOS or disc in the repository) |
| `tests/psx/jit_unicorn.py` | the recompiler on the PC: the ARM code runs in the Unicorn emulator (`pip install unicorn`), the helpers are the real C ones, the programs are those of the self test; `python3 tests/psx/jit_unicorn.py 1000` (in WSL or Linux) |
| `tests/psx/jit_host.c`, `jit_dump.c` | the PC library and the dump tool of the translator (`-DPSX_JIT_HOST`) |
| `tests/psx/render_test.py` | the cartridge in bmhost with a test PS-EXE made by `make_testexe.py`: the list, the fill, the Gouraud triangle, the rectangle |

## Rules (do not break)

- The core is plain C with no dependence on the kernel, so the host tests build it alone. Keep `psx_*.c` free of kernel headers.
  Only `api_psx.c` talks to the kernel.
- Every non-static symbol of the core starts with `psx_`. The macros `C`, `G`, `GP`, `I_STAT`, `I_MASK` in `psx_int.h` give the short names inside the core.
- No BIOS, game or console firmware goes into the repository. The BIOS and the games come only from `/psx`. The test PS-EXE is made by `make_testexe.py`.
- No code of a GPL emulator (pcsx-rearmed, DuckStation, Mednafen) goes into the repository. Read documentation, write original code.
- A disc is read one sector at a time with `fat_read_at` (the callback in `psx_disc_t`). Never load a disc image whole.
- The BIOS call vectors 0xA0, 0xB0 and 0xC0 are answered by the core only when there is no BIOS. A test that puts code there fails.
- A load or a GTE read (`MFC2`) has a delay slot: the next instruction sees the old value. The core and the test programs follow it.
- The CD data FIFO: writing the request bit again does not rewind a loaded buffer (a game reads a sector in two DMAs, header then data); a new sector empties it. The CD events go out in due-time order, not in the order they were queued.
- The recompiler is exact except for two cases that real code does not use: a load in the delay slot of a branch lands at once, and a value loaded by LWL, LWR, MFC0 and a fallback lands at once. A change of `psx_jit.c` must pass `jit_unicorn.py` (0 differences in 1000 trials) and the self test on the Pi (the cartridge shows it).
- The generated code reads the CPU state through r11; every field it uses has its offset from `offsetof(cpu_t, ...)`. Keep `jit_exc`, `pc`, `npc`, `next_bd`, `hi`, `lo`, `r` in `cpu_t`.
- A store into RAM must call `psx_jit_ram_written` when the page has compiled code (`psx_jit_pageflags`): the interpreter (`mem_write`), the DMA and the slow path of the compiled code do it.
- The core tests are the gate for a change of the CPU, the GTE, the GPU, the DMA, the CD-ROM or the SPU. Run `make test-psx`.

## Open work

- Crash Bandicoot 2 (USA) runs on the Pi Zero W up to the Naughty Dog logo animation (2026-11-09, 7 frames a second). The title screen and the game are not checked yet: run the probe further (`tests/psx/boot_probe.c`, frames 3000 and more) and look at `build/psx_probe.ppm`. Speed is the next problem: 7 frames a second is far from 60.
- Speed on the Pi Zero W (2026-11-09, Crash Bandicoot 2 title screen): 11 frames a second with the recompiler, 7 without. A frame takes 38 ms: about 21 ms of CPU, 14 ms of GPU drawing (software), 3 ms of sound and the rest. The CPU time is mostly the 54000 blocks a frame (5 instructions each): the dispatcher and the instruction cache (the generated code is too big for 16 KB). Next: chain the blocks (a patched jump to the next block with a cycle budget), put the slow paths of loads and stores out of line or in shared stubs, keep the MIPS registers used most in ARM registers; for the GPU a smaller picture or frame skip, and the V3D for fills and textured polygons.
- The MDEC (FMV) is not done: DMA channels 0 and 1 end at once and the picture stays black in movies. The CD-DA and XA audio are not done.
- The CPU timing is 2 cycles an instruction. Games with exact timing (raster tricks, some timers) may break. Add the real costs of
  memory accesses, the instruction cache, the multiply and divide stalls, and the GTE stalls.
- The GPU: the draw-to-display bit, the interlace fields in 480 line mode (the picture is the whole frame), the 4 pixel dither edge cases,
  the exact rounding of Gouraud colours and texture coordinates, the polygon size limits. The GPU does not stall the CPU (DMA is instant).
- The GTE: the commands are written from the documentation. Check them against a hardware test (the public GTE test programs) when a game shows wrong 3D.
- The SPU: the reverb, the noise generator, the CD audio input, the sweep volumes (taken as half volume), the SPU interrupt, the Gaussian interpolation.
- The pads: a digital pad on port 1 only. Add the analog pad (DualShock), port 2, and the memory cards (a game that saves needs them;
  the BIOS shell lists them as empty today). Save them in `/docs`.
- The cover is generated. Put the cartridge in `GAMES` and the Market (apps/psx) after a test on both devices.
