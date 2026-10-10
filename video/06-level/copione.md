# SKYVALE WORLD  ·  Episode 6: the last level — narrator's script

Each line starts at the time shown (final video).

- **0:12** The last level. First the pieces it needs, in bm Pixel: trees for the far background, and a flag to end the level.
- **0:21** The assistant draws the tree: F6, one word.
- **0:28** The flag by hand, with the filled rectangle: next sprite, a grey pole, a red cloth.
- **0:34** And the next sprite is only the pole, to stack under the flag.
- **0:40** Now the map, in the SDK. The level of episode 2 was one screen; this one is twice as long, so the ground carries on, with two more pits, planks and clouds.
- **0:51** Grass first, carried on to the right: the view scrolls with the cursor.
- **0:56** Then the dirt under it, two rows, leaving the gaps of the pits.
- **1:03** Spikes at the bottom of each new pit.
- **1:05** More planks, higher and lower.
- **1:10** Layer two holds the clouds: more of them along the way.
- **1:13** A third layer, with Shift+L, for the trees. Each tree is four tiles, two by two: so one pass for each of the four tiles.
- **1:28** O shows one layer alone: here the trees, behind everything. In the game each layer will move at its own speed.
- **1:34** Then the code, in bm Code: edits, not a new program. Ctrl+L goes to a line, and we begin from the end of the file.
- **1:42** The drawing changes the most: the whole of draw goes, with Ctrl+K, one line at a time.
- **1:44** The new draw: a function to draw one layer at a speed, so the clouds go by slowly, the trees at half the speed of Kip, and the ground with him: parallax.
- **2:17** In update, after lib.step: the camera follows Kip, kept inside the level, and reaching the flag ends it.
- **2:22** At the top of update, the two screens: while on the title nothing moves until A; after the flag, nothing moves.
- **2:27** More coins and more slimes, spread along the whole level: Replace, from the menu.
- **2:43** And the new state: the camera, the screen we are on, and where the flag is.
- **2:49** The title, then the level scrolls with Kip: the clouds slowly, the trees at half speed, the ground at his. The last episode plays it from the start to the flag.

## Chapters (YouTube)

0:12 The flag and the trees: bm Pixel
0:40 A longer level: the SDK map
1:13 A layer of trees far behind
1:34 Camera, parallax and the flag: bm Code
2:46 Save and play

Skyvale World is a 2D platformer built only with the tools inside the bm console. In this episode the last level: a flag and trees in bm Pixel, a map twice as long in the SDK, and in bm Code a camera that follows Kip, parallax layers, a title screen and the end of the level.

bm: https://github.com/f-accomando/bm
