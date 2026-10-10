# SKYVALE WORLD  ·  Episode 2: bm SDK, tiles and the map — narrator's script

Each line starts at the time shown (final video).

- **0:12** The SDK is the hub of a .bm project: the project, the code, the 2D sprites and the map, the 3D. In this series we only use its 2D side.
- **0:18** Ctrl+O opens a cartridge from the card: the one we saved in episode 1, with Kip.
- **0:22** F1 again is the dev kit: the size of the file, the memory the data takes, the tokens of the code, and the numbers of the last try.
- **0:28** F3 is the 2D page, first the sprites. A map is made of tiles: little 8 by 8 squares of the sheet. Z switches to 8 by 8.
- **0:33** Tab chooses the cell on the sheet: two rows down, under Kip's frames, is cell 64.
- **0:36** The grass: fill the whole tile with brown, then the pencil for the green on top. Dot and comma walk through the palette.
- **0:42** The digit keys set the flags of the tile: what it is for the game. Flag 0 means solid: Kip will stand on it.
- **0:44** The dirt under it: a brown fill and a few darker pixels.
- **0:51** A wooden plank for the platforms: only the top half of the tile is drawn, the rest stays transparent.
- **0:56** Flag 1 is a platform: solid from above only, Kip jumps through it from below.
- **0:59** A cloud puff for the sky: three of them side by side make a cloud.
- **1:04** And spikes for the bottom of a pit.
- **1:07** Flag 4 hurts. bmlib, the game library, reads these flags.
- **1:10** F3 again is the map. Tab chooses the tile from the sheet: arrows, then Enter. We start with the grass.
- **1:16** Arrows move the cursor, Space places the tile. The ground first: a row of grass, with a gap for the pit.
- **1:20** The dirt: Tab, one tile to the right, Enter, and two more rows, painted back and forth.
- **1:27** The spikes at the bottom of the pit.
- **1:29** Planks for the platforms, three of them at different heights.
- **1:33** A tile in the wrong place? U undoes the last strokes.
- **1:37** A map can have up to eight layers, drawn one over the other. Shift+L adds one: the clouds go on their own layer, behind the ground.
- **1:45** L moves between layers, O shows only the one you are editing.
- **1:50** C shows the flags over the map: a coloured frame on every tile with a flag, red for solid, green for the platforms, light blue for water.
- **1:56** Ctrl+S saves everything in the .bm: sheet, flags, map and layers.
- **1:59** To see the level we need a little code. F2 is the code page: Ctrl+K cuts a line, and a game that draws the map is five lines.
- **2:02** map draws a part of the map: from cell 0,0 at the bottom of the screen, 80 by 34 cells, layer by layer. spr draws Kip, one frame of the run every tenth of a second.
- **2:09** Ctrl+S, then F5 runs the cartridge.
- **2:11** Skyvale World, first level: the ground, the pit with spikes, three platforms, clouds behind, and Kip running on the spot. Next time: sounds.

## Chapters (YouTube)

0:12 The SDK: the hub of a project
0:28 Tiles: draw them in the sheet
1:10 The map
1:37 Layers and flags
1:56 Save and try the game

Skyvale World is a 2D platformer built only with the tools inside the bm console. In this episode we use the bm SDK to draw the tiles, set their flags, build the first level in the map with layers, and try it.

bm: https://github.com/f-accomando/bm
