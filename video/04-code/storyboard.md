# SKYVALE WORLD  ·  Episode 4: bm Code — storyboard

Italian version: [storyboard-IT.md](storyboard-IT.md).

Time is in the final video (hook and title card included: +12 s).

| # | Time | Where | Key / button | What you see | Narrator |
|---|------|-------|--------------|--------------|----------|
| 1 | 0:18 | Dev ▸ bm Code | `Ctrl+O` | open a cartridge | Ctrl+O lists the cartridges on the card: we open Skyvale World, with its tiles, its map and its sounds. |
| 2 | 0:20 | Dev ▸ bm Code | `Enter` | choose skyvale.bm | Ctrl+O lists the cartridges on the card: we open Skyvale World, with its tiles, its map and its sounds. |
| 3 | 0:23 | Dev ▸ bm Code | `Ctrl+K ×7` | cut a line | The code of episode 2 only drew the map. Ctrl+K cuts a line: we start again, this time with a game. |
| 4 | 0:24 | bm Code ▸ typing | `“local lib = require "bmlib"”` | typing code | bmlib is the game library of the console: require it, and a body is just a table with x, y, w and h, and a speed. |
| 5 | 0:26 | bm Code ▸ typing | `Enter ×10` |  | bmlib is the game library of the console: require it, and a body is just a table with x, y, w and h, and a speed. |
| 6 | 0:38 | bm Code ▸ typing | `“function _update()”` | typing code | In update: left and right set the speed, A jumps if Kip is on the ground, with the jump sound from the bank. Let go of A early and the jump is cut short: a variable jump. |
| 7 | 0:39 | bm Code ▸ typing | `Enter ×18` |  | In update: left and right set the speed, A jumps if Kip is on the ground, with the jump sound from the bank. Let go of A early and the jump is cut short: a variable jump. |
| 8 | 1:04 | bm Code ▸ typing | `“function _draw()”` | typing code | Draw: the sky, the camera moved down so the map ends at the bottom of the screen, the layers of the map, and Kip: a frame of the run every four steps, the jump frame in the air, mirrored when he goes left. |
| 9 | 1:05 | bm Code ▸ typing | `Enter ×8` |  | Draw: the sky, the camera moved down so the map ends at the bottom of the screen, the layers of the map, and Kip: a frame of the run every four steps, the jump frame in the air, mirrored when he goes left. |
| 10 | 1:18 | bm Code ▸ save and play | `Ctrl+S` | save | Ctrl+S saves the code into the .bm: the sprites, the map and the sounds stay as they are. F5 runs it. |
| 11 | 1:21 | bm Code ▸ F5: the game | `F5` | try the game | Kip runs, hops up through a platform that is solid only from above, falls on the spikes, and jumps the pit: a variable jump, and the music of episode 3. |
