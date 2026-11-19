# bm SNES SDK roadmap

Back to [snes-sdk.md](snes-sdk.md) (what is built, the rules, the open work of each tab). This sheet lists what is missing, in the order to do it. Do not start a step before the one above it works, unless the step says it is free.

## 1. Check on the devices

- Run the lesson window, the Cartridge menu and the sprite editor on the Pi Zero W and the RGB30. Only the host runs them today.
- Check the Shift, Ctrl and Alt keys of the editor (`M.mods` reads `keydown`) on a real keyboard. The host test uses a stand-in.
- Measure the build time and the colour speed of the Code tab on the Pi Zero W.
- Open each link of the lessons (SMW Central maps, SNESdev Wiki pages) once. They come from memory.
- Find the fault in the sprite pixels. The reader must say the sprite size, the depth and what is wrong.
- Open a `.sfc` of `/snes` beside a project (the ROM view of the Code window, its tiles in Sprites), check the speed of the tiles render target, and draw with Space or A held, on both devices.

## 2. Code tab

- Typing with the pad (an on-screen keyboard).
- A list of the local labels (`@name`) in Go to.
- A check that the map, the `.org` lines and the ROM size of the Cartridge menu agree. The error must say what to change.
- A warning before the build when the code is larger than the chosen ROM.
- A project template for each lesson step, so a reader can start from any lesson.

## 3. Course, part 2

Write each lesson as a step of the same program, in `core/scripts/snes_lessons.py`. Each one must pass `make test-snes-learn`.

1. Subroutines and the stack (`jsr`, `rts`): move the code of lessons 11 to 14 into routines.
2. A background layer: tiles, tilemap, and scrolling.
3. A second sprite, an enemy and a collision test.
4. Many sprites, priority, and the OAM order.
5. HDMA: a colour gradient and a wave on the background.
6. Sound: load a small SPC700 program.
7. A final mini game that joins all the steps.

## 4. Teaching tools of the Run tab

- Step by step with coloured registers: the register that changed shows.
- A disassembler that follows M and X: done for a ROM opened read only (Code tab). Still to do: the true flags from `snes_regs` when the CPU stops, and the project's own ROM.
- An OAM view: the table of 128 sprites and boxes on the screen, with a click to select.
- A watch on the writes to one address. Measure its cost on the Pi Zero W first.

## 5. Sprites tab

- Sizes 16x32 and 32x64 (OBSEL 6 and 7).
- Recolour of the OAM preview by sub-palette.
- Import of `.chr` files.
- A rectangle select.
- Animation.
- `.pal` and the sub-palettes of the tiles as files of the project (when the project has a `gfx/` folder).
- Export of the hero of the course from this tab, so the reader sees the whole path from drawing to game.

## 6. Map and Music tabs (Phase 2 and 3)

- Map: a tilemap editor for the BG layers, with an export that the code reads.
- Map, first step: a live view of the BG layers of a running ROM, as the OAM view of the Sprites tab. Read the tilemaps from the video RAM with `BGnSC`, `BG12NBA`, `BG34NBA` and `BGMODE`. Add `BG12NBA`, `BG34NBA` and the Mode 7 registers to `snes_debug().regs` (`core/src/bm/api_snes.c`). Copy a part into your map. This works for every ROM, but it shows only the part in the video RAM.
- Map, second step: a capture that follows the scroll (`hofs`, `vofs`) frame after frame and puts the tiles together into the whole level. Follow the change of the scroll from frame to frame, because the scroll registers wrap at 1023. Find the 16x16 blocks from the 2x2 groups that repeat.
- Do not write a level reader for each commercial game now. Each game keeps its levels in its own format. Add one only on request, as an extra.

### The BMAP table: the resources of the SDK's own ROMs

User decision, 2026-11-19. The SDK reads the whole level of a ROM that it built, not only the part in the video RAM. The BMAP table is the one table of all the resources of the ROM: the maps and the sound (see "Sound" below). Do not add a second table.

- Keep the maps in the project. The Map tab saves them as files (for example `level1.map`), as the Sprites tab saves the sprites.
- Add the directive `.map "name"` to the assembler. It includes the data of the map. It adds an entry to the BMAP table. It makes the symbols `name_map`, `name_w`, `name_h` and the others.
- The builder writes the BMAP table at the start of a bank. The SDK looks for the table only at the start of each bank.
- The table starts with the signature `"BMAP"`, a version byte and the count of entries. A CRC32 of the table follows the entries. Ignore a table with a bad CRC32.
- Do not change the SNES header or the expanded header at `$FFB0`. The ROM must run the same on hardware and in other emulators.
- Each entry starts with its type: map, sound driver, sample bank, song or sound effect. The SDK skips an entry of a type that it does not know.
- A map entry has the name (16 characters), the type, the BG layer, the width, the height, the compression, the size, and 24-bit pointers to the map data, the 16x16 blocks, the CHR tiles (with their bpp), the palette, the collision map and the object list. A pointer of 0 means "none".
- Types of map data, all in the native SNES format, so that a DMA sends them to the video RAM with no conversion:
  - tilemap: SNES words (tile, palette, priority, flip X, flip Y), in screens of 32x32;
  - blocks: 4 words for each 16x16 block, and the map as block numbers;
  - Mode 7: 128x128 bytes.
- Collision map: one byte for each block. Object list: x, y, type and one parameter for each object.
- Use one compression only (RLE or a simple LZ: decide when the Map tab starts). Give its 65C816 decompressor with the SDK.
- Give a 65C816 routine `load_map` with the SDK. It reads the BMAP table and loads a map by its number. The games load their levels with this routine, so the table always agrees with the game.
- The Map tab opens a ROM with a BMAP table as a project of maps. Without a table, it uses the live view of the first step.
- Tests: build a ROM with two maps, find the table, check the CRC32, read the two maps back the same as the project, and refuse a table with a bad CRC32.
- Music: patterns, BRR samples and the limits of the SPC700.

### Sound: the ROMs of other games

- Add `aram` (64 KB) and `dsp` (the 128 registers of the DSP) to `snes_mem` (`core/src/bm/api_snes.c`). Update `docs/api/` and the host stubs.
- A live view of the 8 voices in the Music tab: the pitch, the volume, the ADSR and the sample of each voice, from the DSP registers.
- Read the BRR samples from the audio RAM. The DIR register of the DSP gives the sample table (start and loop of each sample). This works for every game, but it gets only the samples in the audio RAM at that time.
- Save a `.spc` file: the 64 KB of audio RAM, the DSP registers and the SPC700 registers. Use the common `.spc` format, so that other SPC players play it.
- Record a song as notes: in each frame, record the key-on, the pitch and the sample of each voice. Make the pitch a note and the sample an instrument. The result is near the original: vibrato and slides become small pitch changes. This works for every sound driver.
- Show the commands that the game sends on the ports `$2140-$2143`. Their meaning comes from the driver of each game.
- Do not write a song reader for each sound driver now. Add one only on request, as an extra. N-SPC is the first choice, because many Nintendo games use it.

### Sound: the SDK's own ROMs

- Give one sound driver with the SDK. Write it as original code (the license rules of `CLAUDE.md`). Give it a fixed protocol on the ports `$2140-$2143`.
- Give the 65C816 routines `snd_init`, `play_song`, `play_sfx` and `stop` with the SDK. They read the BMAP table. The games play their sound with these routines, so the table always agrees with the game.
- Put the sound in the BMAP table, in the native format:
  - the sound driver: the SPC700 program and its address in the audio RAM;
  - the sample bank: the BRR samples and their DIR table;
  - a song: its patterns, its instruments and its speed;
  - a sound effect: a short sequence of the driver, with its voice and its priority.
- Add the directives `.song "name"` and `.sfx "name"` to the assembler, as `.map`. Each one adds an entry to the BMAP table and makes the symbols of the entry.
- The Music tab opens a ROM with a BMAP table as a sound project. Without a table, it uses the live view of the voices and the recording of notes.
- Tests: build a ROM with a song and two sound effects, read them back from the BMAP table the same as the project, and play them in the Run tab.

## 7. Coprocessors

- DSP-1: load the firmware in the Run tab for a DSP-1 project.
- Super FX: opcodes of the GSU in `snes_asm`, then a lesson.
- SA-1: examples and a lesson.
- Save RAM: keep it between two runs.
- Chips that the core does not run (S-DD1, OBC-1, S-RTC, Cx4, SPC7110): the order is in [snes.md](snes.md). The Cartridge menu writes only their header today.

## 8. Code assistant for 65816

Not now (decision of 2026-11-13). The plan is in the open work of [snes-sdk.md](snes-sdk.md). The lessons 6 to 14 are the first examples for it.

## 9. Docs

- Split [snes-sdk.md](snes-sdk.md): it has more than 120 lines.
- Add the new editor keys and the lesson window to `docs/spec/ui-reference.md` when the Pi and RGB30 runs pass.

## 10. ROM projects (`.bmr`): save, then export to .sfc or .smc

Last in the queue (user request, 2026-11-18). Today a ROM opens read only and nothing is saved.

- The format is `.bmr`, "bm ROM" (user decision, 2026-11-18): the project of a ROM that the SDK opened. `.bms` was the first choice, but it is the resource file of sounds (`docs/BM-FORMAT.md`): do not use it here. A `.bmr` is a project, not a cartridge: the only cartridge type stays `.bm`.
- Put the `.bmr` files in their own folder `/bmr` at the root of the SD card. Do not write them to `/docs`.
- A `.bmr` keeps the name, the size and the CRC32 of the original ROM, and the changes as a patch (byte ranges, tiles, palettes). Keep the original file as it is.
- Save as, on a ROM opened read only, makes a `.bmr` in `/bmr`. Ctrl+O lists the `.bmr` files with the projects. Opening one reads the original ROM from `/snes`, checks the CRC32 and applies the patch. If the CRC32 is not the same, say which ROM is needed.
- The Code and Sprites tabs edit the `.bmr`, not the ROM: the tiles of a page, the palettes and bytes written as assembly.
- Export to `.sfc` (no copier header) or to `.smc` (the 512-byte copier header first), in `/snes`, so bm SNES and the SNES tab of the menu list it. Do not write the export to `/docs`. Write the checksum and its complement in the header again. Do not write over the original ROM: ask for a new name.
- The kernel: `doc_list`, `doc_read`, `doc_write` and `doc_delete` take the folders `docs`, `snes` and `nes` today (`core/src/bm/api_docs.c`). Add `bmr`, with the same permission. Update `docs/api/system.md` and the host stubs.
- Do not put the bytes of a commercial ROM in the repository or in the `.bmr`: the patch holds only the changes.
- Tests: open, change, save to `/bmr`, open again, export both formats to `/snes`; the export has the patch and a correct checksum, `/docs` gets no file, and the original ROM is not changed.
