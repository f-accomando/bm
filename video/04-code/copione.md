# SKYVALE WORLD  ·  Episode 4: bm Code — narrator's script

Italian version: [copione-IT.md](copione-IT.md).

Each line starts at the time shown (final video).

- **0:12** bm Code is the code editor of the console: tabs for several cartridges, a small sharp font, and only the code of a .bm changes when you save.
- **0:18** Ctrl+O lists the cartridges on the card: we open Skyvale World, with its tiles, its map and its sounds.
- **0:23** The code of episode 2 only drew the map. Ctrl+K cuts a line: we start again, this time with a game.
- **0:24** bmlib is the game library of the console: require it, and a body is just a table with x, y, w and h, and a speed.
- **0:34** lib.tiles tells bmlib how the map stops bodies: flag 0 is solid, flag 1 a platform. The flags we set in the SDK in episode 2. And music zero is the song from episode 3.
- **0:38** In update: left and right set the speed, A jumps if Kip is on the ground, with the jump sound from the bank. Let go of A early and the jump is cut short: a variable jump.
- **0:59** lib.step does the rest: gravity, and the collisions against the solid tiles and the platforms. And a tile with flag 4, the spikes, sends Kip back to the start.
- **1:04** Draw: the sky, the camera moved down so the map ends at the bottom of the screen, the layers of the map, and Kip: a frame of the run every four steps, the jump frame in the air, mirrored when he goes left.
- **1:18** Ctrl+S saves the code into the .bm: the sprites, the map and the sounds stay as they are. F5 runs it.
- **1:21** Kip runs, hops up through a platform that is solid only from above, falls on the spikes, and jumps the pit: a variable jump, and the music of episode 3.

## Chapters (YouTube)

0:12 What is bm Code
0:24 The body of Kip and the tiles
0:38 Update: run, jump and collide
1:04 Draw
1:18 Save and play

Skyvale World is a 2D platformer built only with the tools inside the bm console. In this episode, bm Code: Kip runs and jumps with bmlib, stopped by the tiles of the map, a variable jump, spikes, and the sounds of the last episode.

bm: https://github.com/f-accomando/bm
