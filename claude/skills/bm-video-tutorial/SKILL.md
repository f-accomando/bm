---
name: bm-video-tutorial
description: Generates YouTube video tutorials (storyboard, script, QEMU recording, editing) showing how to use the bm console tools (SDK, bm Code, Pixel, Sound, Mesh, Studio, Animator, AI assistant) to create .bm and .b16 games and apps. Use this skill whenever the user talks about video, tutorial, YouTube, showreel, QEMU recording, console screenshots or "showing how to use" a bm editor, even without naming the skill. Always shows which tabs, keys and buttons are pressed.
---

# bm: video tutorial (bmhost or QEMU)

This skill produces YouTube videos that teach how to use the bm platform
(bare-metal console on Raspberry Pi Zero W) by recording the real console, not
mockups: the console runtime (`bmhost`, on the PC) or the kernel in QEMU. Every
video shows **what happens on the screen and which key, tab or button causes it**.

## Which recorder

- **`bmhost`** (`make bmhost build/<editor>.bm`): the real console runtime (Lua,
  gfx16, sound) on the PC, virtual clock (frame n = n/60 s: same script, same
  video). It only needs `gcc` and `ffmpeg`; it is the one that works in cloud sessions, where
  **QEMU and `arm-none-eabi-gcc` are not available**. Options: `--input FILE --video FILE
  --shots DIR --wav FILE --sd DIR --seconds S --tool` (`--tool`: the cartridge is a bm
  tool, as in the kernel, and can save onto a `.bm` already present: without it, the SDK says "a cartridge
  cannot change a .bm that is there already"). It has no kernel menu (Market/Games/Dev
  tabs) and no GPU. The AI assistant (the `ai` table: F6, and bm Code needs it to start) is only in
  **`build/host/bmhost-ai`** (`make bmhost-ai`; loads `build/assist.bin`, to be launched from the repo root or with
  `BMHOST_AI_WEIGHTS`): with the normal `bmhost` bm Code does not start (`attempt to index a nil value (global 'ai')`).
- **QEMU** `-M raspi0` (`tests/qemu_test.py`): the whole kernel, tabs included. It is
  preferred when available and the video must show the menu.
- `tools/bmplay/video.sh`: a game played by a bot, with sound (for the episodes on the
  finished game).
- Ready-made library: **`video/lib/bmvideo.py`** (key script → `--input` file, keyboard
  overlay, 1920×1080 page with side panel, ffmpeg). An episode is
  `video/NN-nome/{record.py,art.py,storyboard.md,copione.md}`; see `video/01-pixel/`.
  `out/` does not go in the repo (`.gitignore`).

### Lessons learned (do not redo them)

- Keys for bmhost: line `FRAME type CODES`, with `\xNN` for the `keyp()` codes:
  arrows `\xf0 up, \xf1 down, \xf2 left, \xf3 right`, `\xf6` PgUp, `\xf7` PgDn, `\xf8` Del,
  F1–F5 `\xf9…\xfd`, F6 `\xe6`, Esc `\x1b`, Tab `\x09`, **space `\x20`** (a bare space in
  `type` gets lost), Ctrl+letter = code 1–26 (`\x13` Ctrl+S), Ctrl+Shift+S =
  `\xee\x13`. Lines must be in **frame order**: a misplaced line blocks the others.
- An editor remembers on the SD the page and the file opened: for a repeatable video
  **clean SD at every recording** (`record.py` deletes it).
- bm Pixel: a paste (`Ctrl+V`) starts from where the pointer is: bring it to (0,0) first;
  the 16×16 sprite has an even number (8-cells). `Esc` outside a list opens the menu.
- `bmhost --video` writes raw rgb24 640×360: ~700 KB per frame, do not keep it on disk for
  long episodes without space (use a fifo).
- bm Pixel's "Save as" asks for a name that is **already filled in** ("SPRITES.BM"): the typed text is
  appended and then the FAT 8.3 name is truncated (`SPRITESB.BM`). First 10 Backspace (`\\x7f`).
- Verify without looking at images: `video/01-pixel/verify.py` reads the saved cartridge
  (`scripts/bmres.py`, `sheet_get`) and compares every sprite pixel by pixel with `art.py`;
  then `ffprobe` for the duration. If the user asks "no screenshots", work this way.
- To know what an editor does without images: copy of the editor with `log(...)` in `_update` (page state, `S.msg`), compiled with `scripts/mkbm.py`, and `bmhost` without `--quiet`: the log arrives on the terminal. F5 inside `bmhost` ends the run (the cartridge under test is another recording, appended with `cat` of the raws).
- A code editor: type without indentation (bm Code indents by itself), Ctrl+K cuts a line to clean up; the test (F5) ends the recording, so the game is a second recording with a key script (`pad 1 right a`), its wav appended to the first (`wave`); jump timings are computed with a log (copy of the cartridge with a wrapped `_update`) until the game does what the narrator says; `video/04-code/verify.py` checks it.
- Several editors in the same episode: same SD, one after the other (`input-pixel.txt`, `input-code.txt`: `split_input` by frame), then the game; raw and wav appended. bm Code Replace: Ctrl+H = Backspace on the serial, so menu Esc → 12× Down → Enter. Cut all the old lines with Ctrl+K (exact count). A block inserted by the assistant (F6, Enter) leaves the cursor at the end of the block: Enter before writing the next line.
- To tune a game: log in a copy of the cartridge, then search by trial (jump frame × duration) with `bmhost` without video; a patrolling enemy can only be stomped in a narrow window, so search for the timing instead of guessing it.
- "Played" game: a Lua robot added to a copy of the cartridge (`video/07-play/bot.py`) that records the keys as a `bmhost` script; replay it (`replay.py`) and compare the result. Named zones and collision boxes cannot be made with a console editor (`bmres.py`/`mkbm.py`): do not use them in a "console tools only" series. A summary episode can reuse the hooks already made (`ffmpeg -t`, `anullsrc` where audio is missing).
- The sprites of a platformer are **in profile** and the animations are truly different frames
  (legs, tail, one-pixel bounce), not the same sprite repeated: `art.py`
  checks that every pair of frames differs (`art.check()`). Show the complete cycle
  running for a long time (animation panel of bm Pixel) and the F2 page with all of them.
- Fixed-box text fields (bm Sound names: 8 boxes, you **overwrite**): fill with spaces (`"JUMP    "`) to erase the proposed name. The cursor of an effect's steps stays where it was when changing effect: go back left before writing notes. Audio: `bmhost --wav` + `encode(audio=wav)`; to verify it without listening, RMS of the wav in 5 s windows (`video/03-sound/verify.py`).
- Fill (`g`) is 4-neighbor: it only works on a closed outline; `art.regions()`
  finds the regions and the starting points. With an open outline it fills the background.
- Before Ctrl+V the pointer goes to (0,0); keys repeated 1 frame apart
  are fine (time-lapse); a `shot` consumes a frame (keys in the same frame end up in the shot).
- Characters, names and graphics of a game "in the manner of" a classic are **original**:
  style and mechanics yes, characters, logos, music and sprites of the inspiring game no.

## Fixed rules

1. **Only editors inside the console** (Dev tab): SDK, bm Code, bm Pixel, Sound editor,
   bm Mesh, bm Studio and bm Animator *of the console*, AI assistant.
2. **One video = one main tool**, about 3 minutes (confirm the duration with the user
   if not already fixed). You can move from one editor to another
   and back, in the order needed.
3. **No invented data.** Every key, tab, menu and button name must be verified
   in the repo (see "Sources to read"). If a datum cannot be found, write in the
   storyboard `[DA VERIFICARE: ...]` and ask the user: do not guess.
4. **Formats**: `.bm` is the cartridge (Lua code, sprite sheet, tile map, 3D models,
   animations, sound bank, cover). The `.b16` format must be described only after
   finding in the repo what it is: if it is not documented, `[DA VERIFICARE: .b16]`.
5. **Video language**: English, unless otherwise requested. Part of the repo documentation
   is in Italian, so key names must be copied from there; if available, use English.
6. **Code always as text** (in the script and in the cards), never only as an image.
7. The console's AI assistant has an Italian knowledge base: if you show it,
   use questions in Italian; if the English version is available, use English.

## Sources to read (in this order, before writing anything)

Branch `bm-core`. Read with the available tools; do not write from memory.

- `CLAUDE.md` and `AI.md`: project rules and conventions for agents.
- `README.md`: overview, tools on the console, Dev tab.
- `docs/GAME-GUIDE.md` and `docs/API.md`: how to write a game, Lua API.
- `docs/PREDICT.md`: autocompletion in bm Code (Tab accepts the suggestion).
- `Makefile`: targets that record videos or screenshots (e.g. `showreel`,
  `overbit-reel*`, `test-studio-ui`). Read what they do before reusing them.
- `tests/qemu_test.py`: existing QEMU harness (option `--shots DIR` for
  screenshots). Understand how it starts QEMU (`-M raspi0`), how it sends keys and how it
  captures frames **by reading the code**, then reuse it instead of writing a new one.
- `carts/` and `sdk/`: example cartridges and tool READMEs.

## Workflow

### 1. Choose the tool and the goal

Ask (or infer from context) which tool is the protagonist and what the
viewer will be able to do at the end of the video. If the series has an example game (2D platformer
in the style of Super Mario World), the video's goal is a concrete step towards that
game (e.g. "let's draw the sprites with bm Pixel").

### 2. Write the storyboard as a table

Create `video/<NN>-<strumento>/storyboard.md` with one row per step:

| # | Tempo | Dove (menu → tab) | Tasto / pulsante premuto | Cosa si vede | Frase del narratore |
|---|-------|-------------------|--------------------------|--------------|---------------------|

Table rules:
- One row = one action. Never "and then configure": split until each row is a key
  or a gesture.
- The "Tasto / pulsante" column uses the **exact** names from the documentation
  (keyboard and gamepad, if the console supports both: indicate which you use).
- Each row has a visible result verifiable in a frame.
- Typical ~3-minute structure: 0:00 hook (final result) → 0:15 where the tool
  is (Dev tab) → body (3-6 key actions) → last 20 s:
  try it in game and the arrival point of the next video.

### 3. Prepare the recording script

Reuse the existing harness (`tests/qemu_test.py` or the scripts of the `showreel` targets).
Create `video/<NN>-<strumento>/record.py` (or a Makefile target `video-<NN>`) that:

1. starts QEMU with the same kernel that runs on the Pi;
2. sends the storyboard keys with fixed pauses between actions (leave 0.5-1 s
   after each action so the viewer can read the screen);
3. saves the frames and the video at the resolution the harness already produces (the repo
   produces 1280×720 MP4 for the showreel: verify and keep the same format);
4. is **deterministic**: same storyboard, same video. No eyeballed waits.

If QEMU does not emulate a feature (e.g. V3D GPU, Bluetooth, WiFi), do not show it in
that video and tell the user: in the README the 3D rasterization in QEMU happens
with the ARM rasterizer, not with the GPU.

### 4. Overlays: keys and tabs pressed

The video must show **which key was pressed**. For each storyboard row
generate an overlay card (at the bottom) with:
- the key or button name (e.g. `F6`, `Tab`, `A`), in "key" style;
- the tab or menu you are in (e.g. `Dev ▸ bm Pixel`).

Use `ffmpeg` (`drawtext`/`overlay`) starting from the storyboard timings, or
generate `.ass` subtitles. If `ffmpeg` is not installed, say so and give the user
the command to run.

### 5. Narrator script

Create `video/<NN>-<strumento>/copione.md`: one sentence per storyboard row,
direct tone, in English, explaining *what* the tool does, *why* it is needed and
*what more can be done* (the potential). For each sentence indicate the start
time. After the script, add:

- **Title** (max 70 characters, with the tool name);
- **YouTube description** with chapters (`0:00 ...`) generated from the storyboard and
  link to the repo `github.com/f-accomando/bm`;
- **Thumbnail**: indicate which frame to use.

### 6. Check before delivering

- Every key in the storyboard appears in a repo source (cite the file).
- No reference to bm Studio/Animator PC or web.
- The video lasts about as agreed.
- The `[DA VERIFICARE]` points are listed at the end of the reply.
- Do not run commands that write to `dist/`, the SD or the repo without
  telling the user; new files go only in `video/`.

## What to deliver

1. `storyboard.md`, `copione.md`, `record.py` (or Makefile target), any
   editing script.
2. If the environment allows, also the rendered video; otherwise the exact commands
   to generate it (`make video-<NN>`).
3. List of open doubts (keys not found, `.b16`, non-emulated features).

## The current series: Skyvale World (2D platformer, 2D only)

Original game in the style of early-'90s 16-bit platformers (scrolling
worlds, bounce jump on enemies, coins, end-of-level flag, level
map), to show bm's capabilities. The hero is **Kip**, a fox: an original
character. 2D tools only (no bm Mesh, Animator 3D, `picture3d`, GPU). Chronological
order of use: 1 bm Pixel (hero, enemies), 2 SDK 2D (tiles, flags, layer map, first level; **bm Studio is 3D, not used**),
3 Sound (effects and music), 4 bm Code (movement and jump, `bmlib`), 5 bm Code +
assistant (enemies, objects, HUD), 6 bm Pixel + SDK + bm Code (flag, long level, parallax) and
polish, 7 the game played. (All seven done: see `video/SERIE.md`.) One episode at a time, ~3 minutes, and stop where
the user said to stop. The complete structure and state are in
`video/SERIE.md`.

## Proposing the series structure

If the user asks for the structure of the videos, propose an ordered list of
episodes (one per tool, ~3 minutes) towards the example game, indicating for
each the tool, the goal and the result seen at the end of the episode.
Start from the sprites (bm Pixel) unless otherwise indicated, and have the
structure approved before writing the storyboards.
