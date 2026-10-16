title: Your first game in bm
accent: first game
lang: en
kicker: A bm quick start
subtitle: Make a 2D game with a title, three stages, an ending and saved records on the RGB30 and the Pi Zero W.
footer: bm manuals - quickstart

::: readme
**What this is.** A one-page guide that takes a developer who has never used bm from the menu to a complete 2D game with the built-in suite.

**How to use it.** Follow the seven numbered chapters in order. Every screen action is shown as a key or menu step, followed by what appears on screen.

**What is missing.** Gaps waiting for upcoming templates, tables, or hardware checks are tagged with highlighted TO FILL markers.
:::

# Before you start

bm creates cartridges directly on the console or from source.

| You need | For |
|---|---|
| PowKiddy RGB30 with a bm card | making the game and playing it on the handheld |
| USB keyboard | comfortable typing (the gamepad uses the on-screen keyboard) |
| Raspberry Pi Zero W with its bm card | testing and modifying code on the Pi (optional, chapter 5) |
| A PC with the bm repository | copying files between cards or over the network |

You will build a 2D platformer with a title screen, 3 stages, an ending, and a saved high score.

# Make the game on the RGB30

1. From the main menu, go to Dev and open bm SDK.
2. Press `Ctrl+N` to open `new project from a template`, pick **Stages 2D**, and press Enter.
3. Press `F5` to test the game; press Start + Select to return to the editor.
4. Press `Esc` to open the project menu, choose `Save as...`, enter `MYGAME.BME`, and press Enter.

[TO FILL: the Stages 2D template is not in bm SDK (RGB30) yet: check these steps when it is]

# Make it yours

1. Press `F2` to open the code editor, and change the title text in the Lua source.
2. Press `F3` twice to open the map editor: stage n occupies 45 rows starting at row (n-1)×48 across the first 128 columns; tile 7 is the exit.
3. Press `Ctrl+S` to save your changes.

[TO FILL: the Stages 2D template is not in bm SDK (RGB30) yet: check these steps when it is]

# Play it from the menu

1. In bm SDK, press `Ctrl+Shift+B`: the compiled `.b16` goes to `/bm/` and appears in the Games tab.
2. Press `Ctrl+B` to build `MYGAME.BM` in `/carts/`, with the message `(the Games tab lists bm/)`: this is the file for the Pi.

# On the Pi: change the code with bm Code

1. Copy `MYGAME.BME` and `MYGAME.BM` from the `carts/` folder of the RGB30 card into the `carts/` folder of the Pi card.
2. In the Pi main menu, go to Dev and open Code.
3. Press `Ctrl+O`, choose `/carts/MYGAME.BME`, and press Enter.
4. Modify the code and press `F5` to test: if an error occurs it jumps to the line, and `F9` explains it.
5. Press `Ctrl+S` to save. The Pi SDK does not have the Stages 2D template.

[TO FILL: a project made on the RGB30 opened in bm Code on the Pi: not tried yet]
[TO FILL: Ctrl+Q, the help on the function under the cursor in bm Code: not there yet]

# Keep it at 60 fps

1. Keep each frame within the 16.7 ms budget to maintain a steady 60 fps.
2. Press `F11` to toggle the developer overlay and check frame times.
3. Use `profile(true)` and `profile()` to measure routines; see `docs/PERFORMANCE.md` for details.

[TO FILL: the measured budget table for Pi and RGB30 is not published yet]

# Where to go next

1. Read `docs/GAME-GUIDE.md` and `docs/design/games-architecture.md` to explore the template state machine.
2. Consult the *bm SDK* and *bm Code* manuals and the API sheets in `docs/api/`.
3. If anything blocks you, tell the person who gave you the card what stopped you, and where.
