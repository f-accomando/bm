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
- `scripts/smw_sprites.py`: plays random input and rebuilds Mario's picture each frame from OAM
  and VRAM. The palette comes from the screenshot pixels. It writes one PNG per pose and facing,
  `poses.json`, and `frames.csv` (pose against speed, air, and keys). 30 poses of small Mario.
- `scripts/smw_spawns.py`: lists the sprites of the level as they appear (number, place, speed).
  `scripts/smw_blocks.py`: bumps each block kind from below and records the result (new tile,
  coins, sprite). 0x124 gives a coin, 0x126 a Yoshi egg (sprite 0x2C), 0x11A an item (0x79), 0x11E
  is a turn block, all become 0x132.
- `scripts/smw_enemies.py`: per frame, the state and the picture of every sprite slot (OAM range from
  `$15EA`). `states.csv` has the picture name per row. Koopa Troopa (sprite 05): walks at 8, two
  frames of 8 ticks per facing, turns at walls, stomp gives a shell that sleeps 330 ticks.
- `scripts/smw_goal.py`: the goal tape picture. Measured: the tape (sprite 0x7B at x 4832) goes up
  one pixel a tick from y 368 to 244 and back (248 ticks). Touching it starts the end: Mario walks right.
  The clock drops one every 35 ticks from 400. Mario starts with 5 lives.
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

- Test start: `SMW_START=x,y python3 carts/smw/build/build.py` starts Mario elsewhere.
- Solidity: the table is a guess (`0x100`-`0x1BF`, dirt `0x3F`-`0x41`). Read the real
  "acts like" table from the ROM.
- Mario poses: small Mario only. Capture big, fire, cape, Yoshi, swim, climb, death, and the
  walk speed steps. The animation rate is one rule (`anim += speed`, a frame per 122).
- Background layer 2 (parallax forest): the game uses one colour. The TM mirror poke does not
  hide layers. Read the BG2 tilemap from VRAM or stitch it from frames.
- Camera: use the SMW scroll rules. Now `x - 108`.
- Tune the physics: start-up acceleration, air control, P meter decay, terminal speed. Slopes.
- Done: question blocks with coins (bump, used tile), coin pickup, coin counter.
- Items from blocks (egg, 0x79), turn blocks (break when big), Yoshi/dragon coins, score.
- Done: the goal tape and the course clear banner, 5 lives, the clock, death (hop and fall), game over,
  restart of the level (blocks and coins put back), score for coins, stomps, and the time bonus.
- Done: Koopa Troopas (walk, turn, stomp, kick, slide, kill each other), Mario dies on touch.
- Enemies still open: the level has sprites 00, 01, 04, 05 (Koopas), 3E (P switch), 4D, 4E (Monty Mole),
  4F (jumping piranha plant), 91 (Chargin' Chuck), B9 (message), 7B (goal). `spawns.json` has the
  places. Bonus by the tape height, the end screen of SMW, the overworld, 1-ups at 100 coins.
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
