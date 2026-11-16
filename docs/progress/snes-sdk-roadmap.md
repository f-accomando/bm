# bm SNES SDK roadmap

Back to [snes-sdk.md](snes-sdk.md) (what is built, the rules, the open work of each tab). This sheet lists what is missing, in the order to do it. Do not start a step before the one above it works, unless the step says it is free.

## 1. Check on the devices

- Run the lesson window, the Cartridge menu and the sprite editor on the Pi Zero W and the RGB30. Only the host runs them today.
- Check the Shift, Ctrl and Alt keys of the editor (`M.mods` reads `keydown`) on a real keyboard. The host test uses a stand-in.
- Measure the build time and the colour speed of the Code tab on the Pi Zero W.
- Open each link of the lessons (SMW Central maps, SNESdev Wiki pages) once. They come from memory.
- Find the fault in the sprite pixels. The reader must say the sprite size, the depth and what is wrong.

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
- A disassembler that follows M and X.
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
