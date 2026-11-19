# bm SNES SDK: Map and Music tabs, and the BMAP table

Back to [snes-sdk-roadmap.md](snes-sdk-roadmap.md) (step 6).

- Map: a tilemap editor for the BG layers, with an export that the code reads.
- Map, first step (done, 2026-11-19): the BG view of the Map tab ([snes-sdk.md](snes-sdk.md)). Still to do: the Mode 7 matrix registers in `snes_debug().regs` (the map and its tiles show without them), copy into your own map (when the map editor is there), and the 16-pixel-wide tiles of modes 5 and 6.
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
