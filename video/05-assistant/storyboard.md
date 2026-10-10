# SKYVALE WORLD  ·  Episode 5: the assistant — storyboard

Italian version: [storyboard-IT.md](storyboard-IT.md).

Time is in the final video (hook and title card included: +12 s).

| # | Time | Where | Key / button | What you see | Narrator |
|---|------|-------|--------------|--------------|----------|
| 1 | 0:17 | Dev ▸ bm Pixel | `Ctrl+O` | open a cartridge | Episode 5: enemies, coins, and a score. The assistant of the console helps: it knows the API, how-tos and sprite recipes, and runs on the console itself. |
| 2 | 0:19 | Dev ▸ bm Pixel | `Enter` | choose skyvale.bm | Episode 5: enemies, coins, and a score. The assistant of the console helps: it knows the API, how-tos and sprite recipes, and runs on the console itself. |
| 3 | 0:21 | Dev ▸ bm Pixel | `PgDn ×8` | next sprite | First an enemy. In bm Pixel, PgDn goes to the first free sprite, after the frames of Kip and the coin. |
| 4 | 0:23 | Dev ▸ bm Pixel | `F6` | the assistant | F6 is the assistant: say what you want, a word is enough. A green slime. |
| 5 | 0:25 | Dev ▸ bm Pixel | `“green slime”` | what to draw | F6 is the assistant: say what you want, a word is enough. A green slime. |
| 6 | 0:27 | Dev ▸ bm Pixel | `Enter` | draw it | F6 is the assistant: say what you want, a word is enough. A green slime. |
| 7 | 0:30 | Dev ▸ bm Pixel | `Enter` | put it down | The base of the sprite floats on the canvas, in the colours of the palette: Enter puts it down, and it can be edited like any other. |
| 8 | 0:33 | Dev ▸ bm Pixel | `Ctrl+S` | save | The base of the sprite floats on the canvas, in the colours of the palette: Enter puts it down, and it can be edited like any other. |
| 9 | 0:39 | Dev ▸ bm Code | `Ctrl+O` | open a cartridge | Now the game, in bm Code. We keep the movement of episode 4 and add a score, lives, a list of coins and a list of slimes. |
| 10 | 0:41 | Dev ▸ bm Code | `Enter` | choose skyvale.bm | Now the game, in bm Code. We keep the movement of episode 4 and add a score, lives, a list of coins and a list of slimes. |
| 11 | 0:44 | Dev ▸ bm Code | `Ctrl+K ×41` | cut a line | Now the game, in bm Code. We keep the movement of episode 4 and add a score, lives, a list of coins and a list of slimes. |
| 12 | 0:47 | Dev ▸ bm Code | `“local lib = require "bmlib"”` | typing code | The state of the game: score, lives, and a timer for the seconds after a hit. Coins and slimes are lists of tables, the same shape as Kip's body. |
| 13 | 0:48 | Dev ▸ bm Code | `Enter ×7` |  | The state of the game: score, lives, and a timer for the seconds after a hit. Coins and slimes are lists of tables, the same shape as Kip's body. |
| 14 | 0:56 | Dev ▸ bm Code | `“function _init()”` | typing code | In init, the coins at their places and two slimes, each with a speed and the point it patrols around. |
| 15 | 0:57 | Dev ▸ bm Code | `Enter ×11` |  | In init, the coins at their places and two slimes, each with a speed and the point it patrols around. |
| 16 | 1:13 | Dev ▸ bm Code | `“function _update()”` | typing code | In update, Kip as before, and the hurt timer counting down. |
| 17 | 1:14 | Dev ▸ bm Code | `Enter ×13` |  | In update, Kip as before, and the hurt timer counting down. |
| 18 | 1:29 | Dev ▸ bm Code | `“for _, c in ipairs(coins) do”` | typing code | A coin that Kip touches is marked dead and gives ten points, with the coin sound. lib.sweep takes the dead ones out of the list. |
| 19 | 1:30 | Dev ▸ bm Code | `Enter ×8` |  | A coin that Kip touches is marked dead and gives ten points, with the coin sound. lib.sweep takes the dead ones out of the list. |
| 20 | 1:35 | Dev ▸ bm Code | `“for _, s in ipairs(slimes) do”` | typing code | The slimes: lib.step gives them gravity too, they turn at the ends of their walk. Coming down on a slime is a stomp: it dies, Kip bounces, a hundred points. Touched any other way, it costs a life, and for one and a half seconds nothing hurts. |
| 21 | 1:36 | Dev ▸ bm Code | `Enter ×17` |  | The slimes: lib.step gives them gravity too, they turn at the ends of their walk. Coming down on a slime is a stomp: it dies, Kip bounces, a hundred points. Touched any other way, it costs a life, and for one and a half seconds nothing hurts. |
| 22 | 1:52 | Dev ▸ bm Code | `“if mflags(kip.x, kip.y, kip.w, kip.h) & 16 ~= 0 or kip.y > 400 then”` | typing code | The spikes and the pit now cost a life as well, and with no lives left the game starts again. |
| 23 | 1:55 | Dev ▸ bm Code | `Enter ×8` |  | The spikes and the pit now cost a life as well, and with no lives left the game starts again. |
| 24 | 2:02 | Dev ▸ bm Code | `“function _draw()”` | typing code | Draw: the map, the coins, the slimes, and Kip, who blinks while he is hurt. Then the score and the lives on the screen: we ask the assistant. |
| 25 | 2:02 | Dev ▸ bm Code | `Enter ×9` |  | Draw: the map, the coins, the slimes, and Kip, who blinks while he is hurt. Then the score and the lives on the screen: we ask the assistant. |
| 26 | 2:20 | bm Code ▸ F6 the assistant | `F6` | the assistant | Draw: the map, the coins, the slimes, and Kip, who blinks while he is hurt. Then the score and the lives on the screen: we ask the assistant. |
| 27 | 2:22 | bm Code ▸ F6 the assistant | `“show score and lives”` | the question | Draw: the map, the coins, the slimes, and Kip, who blinks while he is hurt. Then the score and the lives on the screen: we ask the assistant. |
| 28 | 2:29 | bm Code ▸ F6 the assistant | `Enter` | insert the code | It finds the how-to and shows the code. Up and down choose among the answers. Enter inserts the code where the cursor is. |
| 29 | 2:31 | bm Code ▸ F6 the assistant | `Esc` | the menu | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 30 | 2:32 | bm Code ▸ F6 the assistant | `arrows + Space (12 presses)` | move the pointer / paint | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 31 | 2:33 | bm Code ▸ F6 the assistant | `Enter` | Replace (Ctrl+H) | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 32 | 2:35 | bm Code ▸ F6 the assistant | `⌫ ×36` |  | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 33 | 2:35 | bm Code ▸ F6 the assistant | `“48, SCREEN_W - 12 * i - 4, 8”` | what to find | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 34 | 2:37 | bm Code ▸ F6 the assistant | `Enter` | next | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 35 | 2:38 | bm Code ▸ F6 the assistant | `“0, SCREEN_W - 20 * i, 8, 2, 2”` | with what | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 36 | 2:39 | bm Code ▸ F6 the assistant | `Enter` | replace all | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 37 | 2:42 | bm Code ▸ F6 the assistant | `Esc` | the menu | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 38 | 2:43 | bm Code ▸ F6 the assistant | `arrows + Space (12 presses)` | move the pointer / paint | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 39 | 2:44 | bm Code ▸ F6 the assistant | `Enter` | Replace (Ctrl+H) | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 40 | 2:45 | bm Code ▸ F6 the assistant | `⌫ ×36` |  | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 41 | 2:46 | bm Code ▸ F6 the assistant | `“PUNTI”` | what to find | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 42 | 2:46 | bm Code ▸ F6 the assistant | `Enter` | next | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 43 | 2:47 | bm Code ▸ F6 the assistant | `“SCORE”` | with what | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 44 | 2:47 | bm Code ▸ F6 the assistant | `Enter` | replace all | The code uses score and lives, as we named them, but it draws a heart that is not in our sheet, and the label is in Italian. Replace, in the menu, or Ctrl+H on a keyboard: the heart becomes Kip's face, twice as big. |
| 45 | 2:50 | bm Code ▸ F6 the assistant | `Enter` |  | The end of draw, and the assistant has more: ask it about invincibility after a hit, and the answer is there to read. |
| 46 | 2:50 | bm Code ▸ F6 the assistant | `“end”` | end of draw | The end of draw, and the assistant has more: ask it about invincibility after a hit, and the answer is there to read. |
| 47 | 2:50 | bm Code ▸ F6 the assistant | `Enter` |  | The end of draw, and the assistant has more: ask it about invincibility after a hit, and the answer is there to read. |
| 48 | 2:51 | bm Code ▸ F6 the assistant | `F6` | the assistant | The end of draw, and the assistant has more: ask it about invincibility after a hit, and the answer is there to read. |
| 49 | 2:53 | bm Code ▸ F6 the assistant | `“invincible after a hit”` | the question | The end of draw, and the assistant has more: ask it about invincibility after a hit, and the answer is there to read. |
| 50 | 2:59 | bm Code ▸ F6 the assistant | `Esc` | close | The end of draw, and the assistant has more: ask it about invincibility after a hit, and the answer is there to read. |
| 51 | 3:00 | bm Code ▸ save and play | `Ctrl+S` | save | Ctrl+S saves the code into the .bm, and F5 runs the game. |
| 52 | 3:03 | bm Code ▸ F5: the game | `F5` | try the game | Coins to collect, a slime to stomp, the score and the three faces of Kip: his lives. Next time: bm Studio is 3D, so we stay in 2D: a flag, a second layer, and the last level. |
