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
- `scripts/smw_diff.py`: the exact picture of a sprite, taken out of the screen: it plays two frames twice from
  the same state, once with the sprite's slot cleared, and keeps the pixels that differ. Used for the jumping
  plant (4F), the Chargin' Chuck (91), the P switch (3E), the flowers and tufts (4E, 4D), and the message block
  (B9). The sprite table and the graphics of a savestate do not always match the screen (dynamic graphics).
- `scripts/smw_msgs.py`: bumps each message block and keeps the black box with its text (msg1: the spin jump
  tip; msg2: Yoshi's thanks). The game shows it as zones `msgN_a` and `msgN_b` (128 pixels at most a zone) and
  waits for A, B or Start.
- `scripts/smw_items.py`: bumps a block kind and records the sprites that appear. 0x126 gives an egg
  (sprite 0x2C, 58 ticks) that hatches into Yoshi (0x35); 0x11A gives a growing vine (0x79).
- `scripts/smw_solid.py`: tests every Map16 number for solidity from above, the side, and below.
- `carts/smw/build/sprites.py`: puts the captured pictures under the tile cells as 19 named zones
  (`mario_walk`, `koopa_l`, `shell_slide`, `egg`, `yoshi`, `tape`, ...) with their boxes. `bmres.py info`
  lists them. Mario's boxes: `hurt` (body), kind 3 (feet, he stomps), kind 4 (body, he kicks). A Koopa:
  `hurt` (head), `hit` (body). The Lua gets the offset of each zone from `ZA` (the suite has no anchors).
- `carts/smw/src/physics.lua`: movement at 50 Hz ticks. `scripts/smw_physics_check.py`
  scores it against the traces in `bmhost`. First jump: error under 3 pixels.
- `carts/smw/src/game.lua` and `carts/smw/build/build.py`: the playable level, 360x360 `.b16`
  and `.bme` for the RGB30. Output goes to `build/smw/` (ignored by git).

## Rules (do not break)

- Keep the sprites as SPRITES zones with BOXES (`carts/smw/build/sprites.py`). Draw them with `zspr`, read
  the boxes with `zboxes`, and decide hits with `lib.hits`. Do not draw by pixel coordinates in Lua.
- Keep the map as layers with flags (1 solid, 2 platform): `main` is drawn, `col` is read by `lib.tiles`.

- Keep the ROM, `*.sfc`, `*.smc`, and `carts/smw/{rom,extracted}/` out of git.
- Do not upload the cartridge to the Market. Do not put Nintendo assets in the repository.
  The PDF guide of the user uses real screenshots as a personal reference
  (`tests/smw/guide_shots.py --real`, into `carts/smw/extracted/shots/`). Keep that PDF out of git.
- Start each change from the `.bme`. Build the `.b16` from it (RGB30 edition, 360x360).
- Measure the fidelity of the physics against the emulator trace, frame by frame.

## Open work

- Test start: `SMW_START=x,y python3 carts/smw/build/build.py` starts Mario elsewhere.
- Solidity: tested in the emulator for the first level (`scripts/smw_solid.py`): ledges are
  platforms (a floor from above only), blocks and pipes are solid, hills and posts are scenery.
  For other levels read the "acts like" table from the ROM.
- Mario poses: small Mario only. Capture big, fire, cape, Yoshi, swim, climb, death, and the
  walk speed steps. The animation rate is one rule (`anim += speed`, a frame per 122).
- Background layer 2 (parallax forest): the game uses one colour. The TM mirror poke does not
  hide layers. Read the BG2 tilemap from VRAM or stitch it from frames.
- Camera: use the SMW scroll rules. Now `x - 108`.
- Whole-level check (`tests/smw/compare.py`): the same input in the emulator (Mario unhurtable) and in the port
  (`SMW_GOD=1 SMW_TRACE=1`, a log line a tick). The first 700 ticks (to x 1900) stay within 5 pixels (mean 0.6
  in the first 200). Then one corner of the step at x 1952 is touched in one run and not in the other, and the
  two runs part. Koopa stomps start 6 pixels down (as the emulator shows). The P meter drains in the air.
- Physics: 12 scenarios (`tests/smw/trace_scripts.json`) against the emulator, mean position error
  under 0.8 pixel, worst 3.0 pixels in x and 3.1 in y (`scripts/smw_physics_check.py`; `--fit`
  refits the constants). The game moves first with the last tick's speed, then changes the speed.
  Open: slopes, water, ice, the sub-pixel carry of the real engine, spin jump height.
- Done: question blocks with coins (bump, used tile), coin pickup, coin counter.
- Done: the egg block (the egg rises, cracks, Yoshi hatches), dragon coins (two blocks, 1000 points,
  a 1-up for five).
- Mario's own collision stays in `physics.lua` (it matches the traces). The Koopas use `lib.move`;
  the map uses `lib.tiles` and `lib.solid`.
- Note: `mkbm.py` warns `require: not for a .b16` for `require "bmlib"`. The runtime has it.
- Done: the messages, the spin jump that breaks turn blocks (four pieces fly), the blue P switch (stomp: it goes
  flat, a puff; for 704 ticks turn blocks are coins and coins are turn blocks, then they go back).
- Open: the vine, the Chuck's other moves (hops, the clap, the slide when hit), the shell sprites 00 and 04 of the level.
- Open: riding Yoshi (poses and eating), the vine of 0x11A (growth and climbing poses), big Mario,
  power-ups, the turn block breaking when Mario is big.
- Done: the goal tape and the course clear banner, 5 lives, the clock, death (hop and fall), game over,
  restart of the level (blocks and coins put back), score for coins, stomps, and the time bonus.
- Done: Koopa Troopas (walk, turn, stomp, kick, slide, kill each other), Mario dies on touch.
- Done: the jumping plant (rises along the game's own path from the pipe at x 4368), two Chargin' Chucks
  (look, charge 18 ticks at speed 24, wait, three stomps), the animated flowers and tufts, the message blocks
  and the P switch as solid or still sprites.
- Enemies still open: the level has sprites 00, 01, 04, 05 (Koopas), 3E (P switch), 4D, 4E (Monty Mole),
  4F (jumping piranha plant), 91 (Chargin' Chuck), B9 (message), 7B (goal). `spawns.json` has the
  places. Bonus by the tape height, the end screen of SMW, the overworld, 1-ups at 100 coins.
- More levels. Map16 for other tilesets comes from the ROM table at `$0D8000`.
- Sound: SPC700. Music last.
- Frame time: Select shows the overlay (ms, fps, Lua KB, instructions) and logs a `bench` line every 5
  seconds. On the PC the level takes 0.05 ms a frame on average, 1-2 ms at the worst frame (a restart).
  Run it on the RGB30 and read the log: the number is not known yet.
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
