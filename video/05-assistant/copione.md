# SKYVALE WORLD  ·  Episode 5: the assistant — narrator's script

Italian version: [copione-IT.md](copione-IT.md).

Each line starts at the time shown (final video).

- **0:12** Episode 5: enemies, coins, and a score. The assistant of the console helps: it knows the API, how-tos and sprite recipes, and runs on the console itself.
- **0:21** First an enemy. In bm Pixel, PgDn goes to the first free sprite, after the frames of Kip and the coin.
- **0:23** F6 is the assistant: say what you want, a word is enough. A green slime.
- **0:30** The base of the sprite floats on the canvas, in the colours of the palette: Enter puts it down, and it can be edited like any other.
- **0:36** Now the game, in bm Code. We keep the movement of episode 4 and add a score, lives, a list of coins and a list of slimes.
- **0:47** The state of the game: score, lives, and a timer for the seconds after a hit. Coins and slimes are lists of tables, the same shape as Kip's body.
- **0:56** In init, the coins at their places and two slimes, each with a speed and the point it patrols around.
- **1:13** In update, Kip as before, and the hurt timer counting down.
- **1:29** A coin that Kip touches is marked dead and gives ten points, with the coin sound. lib.sweep takes the dead ones out of the list.
- **1:35** The slimes: lib.step gives them gravity too, they turn at the ends of their walk. Coming down on a slime is a stomp: it dies, Kip bounces, a hundred points. Touched any other way, it costs a life, and for one and a half seconds nothing hurts.
- **1:52** The spikes and the pit now cost a life as well, and with no lives left the game starts again.
- **2:02** Draw: the map, the coins, the slimes, and Kip, who blinks while he is hurt. Then the score and the lives on the screen: we ask the assistant.
- **2:26** It finds the how-to and shows the code. Up and down choose among the answers. Enter inserts the code where the cursor is.
- **2:31** The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big.
- **2:50** The end of draw, and the assistant has more: ask it about invincibility after a hit, and the answer is there to read.
- **3:00** Ctrl+S saves the code into the .bm, and F5 runs the game.
- **3:03** Coins to collect, a slime to stomp, the score and the three faces of Kip: his lives. Next time: bm Studio is 3D, so we stay in 2D: a flag, a second layer, and the last level.

## Chapters (YouTube)

0:12 The assistant draws a sprite
0:36 Coins and slimes in bm Code
1:13 Update: coins, stomp and lives
2:02 Draw, and the assistant for the HUD
3:00 Save and play

Skyvale World is a 2D platformer built only with the tools inside the bm console. In this episode the assistant draws the slime in bm Pixel and gives the HUD in bm Code, and we add coins, stompable enemies, a score and lives.

bm: https://github.com/f-accomando/bm
