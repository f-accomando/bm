# smw: native port of Super Mario World for the RGB30

Personal, educational port. The user owns the cartridge. The ROM, the extracted data, and
the finished cartridge stay on the user's machine and SD card.

## State

- `scripts/smw_extract.py`: reads the Europe Rev 1 ROM (SHA-1 checked). Decompresses the 50
  graphics files (LZ2, table at `$00B9A6`) and writes PNG sheets (2bpp and 3bpp).
- `scripts/smw_capture.py`: runs the ROM in snes9x (`stable-retro`, venv). Dumps VRAM, CGRAM,
  OAM, and WRAM. `Game.poke` writes WRAM through the savestate.
- `scripts/smw_trace.py`: records Mario's physics for five input scripts (CSV, per frame).
- `scripts/smw_map16.py`: runs the first level and collects the Map16 grid, the four tile
  words of each Map16 (VRAM tilemap `$5000`, characters at `$0000`), and the 8x8 sheet.
- `carts/smw/src/physics.lua`: movement at 50 Hz ticks. `scripts/smw_physics_check.py`
  scores it against the traces in `bmhost`. First jump: error under 3 pixels.
- `carts/smw/src/game.lua` and `carts/smw/build/build.py`: the playable level, 360x360 `.b16`
  and `.bme` for the RGB30. Output goes to `build/smw/` (ignored by git).

## Rules (do not break)

- Keep the ROM, `*.sfc`, `*.smc`, and `carts/smw/{rom,extracted}/` out of git.
- Do not upload the cartridge to the Market. Do not put Nintendo assets in the repository.
  The PDF guide of the user uses real screenshots as a personal reference
  (`tests/smw/guide_shots.py --real`, into `carts/smw/extracted/shots/`). Keep that PDF out of git.
- Start each change from the `.bme`. Build the `.b16` from it (RGB30 edition, 360x360).
- Measure the fidelity of the physics against the emulator trace, frame by frame.

## Open work

- Solidity: the table is a guess (`0x100`-`0x1BF`, dirt `0x3F`-`0x41`). Read the real
  "acts like" table from the ROM.
- Mario sprites: the game draws a placeholder. Capture the frames (OAM and VRAM) per pose.
- Background layer 2 (parallax forest): the game uses one colour. The TM mirror poke does not
  hide layers. Read the BG2 tilemap from VRAM or stitch it from frames.
- Camera: use the SMW scroll rules. Now `x - 108`.
- Tune the physics: start-up acceleration, air control, P meter decay, terminal speed. Slopes.
- Enemies, blocks (hit and bump), items, coins, goal. Death and lives.
- More levels. Map16 for other tilesets comes from the ROM table at `$0D8000`.
- Sound: SPC700. Music last.
- Measure the frame time on the RGB30. The host run takes 0.04 ms a frame.
- Write the PDF guide (EN and IT, with `bm-manuals`): how the `.bme` is built, which APIs it
  uses, the game structure, and the asset pipeline. Use original diagrams and code excerpts
  and the real screenshots of the user, for personal use. Do not commit that PDF.
- Run `python scripts/consistency.py check` at closure.

## Guide edition

- It is the same game on the same level. The Map16 grid, the collision layer, the camera, and
  the physics are the real ones. Only the pixels change.
- `carts/smw/guide/build.py` reads the files of `scripts/smw_map16.py`. From the level picture it
  keeps the shape and a colour role (green, earth, yellow, red, blue, light, grey, dark). It paints
  each role with its own colours, light, shadow, and earth speckle. No ROM pixel reaches the
  output. It builds `build/smw-guide/guide.b16`.
- `tests/smw/guide_shots.py` plays input scripts in `bmhost` and writes
  `manuals/shots/smw-guide-NN-name.png`. Use these images only if the PDF is shared. The real build
  (`carts/smw/build/build.py`) stays for the benchmark. `--real` writes the real screenshots
  of the same scenes to `carts/smw/extracted/shots/` (ignored by git).
- Open: restyle Mario's real sprites the same way when they are captured. The two-tone bush
  becomes one green.
