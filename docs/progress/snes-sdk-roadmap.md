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
- Music: patterns, BRR samples and the limits of the SPC700.

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

## 10. ROM projects (`.bms`): save, then export to .sfc or .smc

Last in the queue (user request, 2026-11-18). Today a ROM opens read only and nothing is saved.

- The format is `.bms` (user decision, 2026-11-18): the project of a ROM that the SDK opened. A `.bms` is a project, not a cartridge: the only cartridge type stays `.bm`.
- Put the `.bms` files in their own folder `/bms` at the root of the SD card. Do not write them to `/docs`.
- A `.bms` keeps the name, the size and the CRC32 of the original ROM, and the changes as a patch (byte ranges, tiles, palettes). Keep the original file as it is.
- Save as, on a ROM opened read only, makes a `.bms` in `/bms`. Ctrl+O lists the `.bms` files with the projects. Opening one reads the original ROM from `/snes`, checks the CRC32 and applies the patch. If the CRC32 is not the same, say which ROM is needed.
- The Code and Sprites tabs edit the `.bms`, not the ROM: the tiles of a page, the palettes and bytes written as assembly.
- Export to `.sfc` (no copier header) or to `.smc` (the 512-byte copier header first), in `/snes`, so bm SNES and the SNES tab of the menu list it. Do not write the export to `/docs`. Write the checksum and its complement in the header again. Do not write over the original ROM: ask for a new name.
- The kernel: `doc_list`, `doc_read`, `doc_write` and `doc_delete` take the folders `docs`, `snes` and `nes` today (`core/src/bm/api_docs.c`). Add `bms`, with the same permission. Update `docs/api/system.md` and the host stubs.
- Do not put the bytes of a commercial ROM in the repository or in the `.bms`: the patch holds only the changes.
- Tests: open, change, save to `/bms`, open again, export both formats to `/snes`; the export has the patch and a correct checksum, `/docs` gets no file, and the original ROM is not changed.
