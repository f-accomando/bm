# Skyvale World — the video series

Italian version: [SERIE-IT.md](SERIE-IT.md).

An original 2D side-scrolling platformer in the style of the early-'90s 16-bit games,
made **only with the tools of the bm console** (2D: no bm Mesh, 3D Animator,
`picture3d`, GPU). The purpose of the videos is to show what bm can do, the editors one
by one, and in the end the game playable in a `.bm`. Hero: **Kip**, a fox (an original
character: no characters, sprites or music from existing games).

Every episode: ~3 minutes, one editor as the protagonist, keys always overlaid
(panel on the right: where we are, key pressed, keys of the scene), narrator subtitles,
a hook with the result at the opening. Skill: `claude/skills/bm-video-tutorial`.
Library: `video/lib/bmvideo.py`. One episode = `video/NN-name/` (`record.py`, `art.py`,
`storyboard.md`, `copione.md`); generated files go in `out/` (not in the repo).

| # | Tool | What is done | Status |
|---|------|--------------|--------|
| 1 | bm Pixel | Kip in profile: 6-frame run cycle, jump, coin; pencil, fill, undo, animation, ovals and lines, mirror, palette, save `skyvale.bm` | done |
| 2 | SDK (2D) | 8×8 tiles in the sheet, flags (solid, platform, ladder, water, hurts), layered map, the first level; five-line code and a try (F5) | done |
| 3 | Sound | sounds (jump with bend, coin, bounce), three effects with the piano, level music written with riff (F7) and put in the bank as a song, pattern and song pages | done |
| 4 | bm Code | Kip runs and jumps: `lib.tiles`, `lib.step`, variable-pressure jump, spikes (flag 4), sounds and music from episode 3; the game played by a key script | done |
| 5 | bm Pixel + bm Code + assistant | the slime drawn by the assistant (F6 in bm Pixel), coins and slimes that can be stomped, score and lives; the HUD is an answer of the assistant (F6 in bm Code) adapted with Replace; question about invincibility | done |
| 6 | bm Pixel + SDK + bm Code | the last level: tree (assistant) and flag (rectangles) in bm Pixel, a 160-cell level with three pits and a third layer of trees in the SDK, camera following Kip, three-speed parallax, flag, title and end screens in bm Code | done |
| 7 | the game | Skyvale World played from the title to the flag by a key script (made by a robot), recap of the six episodes, and what is in the `.bm` (project page and SDK dev kit) | done |

Notes for whoever continues:

- Recording on the PC with `bmhost` (QEMU is not available in cloud sessions): it has no
  kernel menu and no AI assistant; for those parts QEMU or the real console is needed.
- Kip's sheet is created in bm Pixel and saved as a `.bm`; episodes 2–6 pick up that
  cartridge (`out/sd/carts/`) and add map, sounds and code to it. To repeat the episodes
  in order, the `record.py` of each episode must start from the file left by the previous
  one (to be kept in the repo when ready: `carts/skyvale/`).
- No characters, sprites or music from existing games are used.
- **bm Studio and bm Animator of the console are 3D**: they are not used. The 2D tiles and map live in the SDK (page F3, F3 again for the map).
- The tools are recorded with `bmhost --tool` (like the tools built into the kernel: they can save to an existing `.bm`); F5 inside `bmhost` ends the recording, so the game try is a second, appended recording.
- Every episode starts from the `.bm` left by the previous one: episode 2 starts from `video/02-sdk/start.bm` (the cartridge saved in episode 1).
- Audio: `bmhost --wav` and `encode(..., audio=wav)` put the sound in the video; the cards and the hook have a silent track. Check: `video/03-sound/verify.py` reads the saved bank (`scripts/bmaudio.py`) and checks that the wav is not silence.
- Keys: F6–F10 and Ctrl+Enter reach `bmhost` only as ESC sequences (`\x1b[18~` is F7, `\x1b[28~` is Ctrl+Enter): the raw bytes 0xE5–0xEF are discarded.
- bm Code (and the F6 assistant, episode 5) need the `ai` table: they are recorded with `build/host/bmhost-ai` (`make bmhost-ai`: links `src/ai/lua_ai.c` and loads `build/assist.bin`); the normal `bmhost` does not have it.
- The cartridge of each episode starts from the one left by the previous one (`video/NN-name/start.bm`); new code is written **without indentation**: bm Code indents by itself with Enter and `end`.
- Ctrl+H is the same byte as Backspace on the serial line: in `bmhost` Replace is opened from the menu (Esc, 12 times Down, Enter). New code is written after cutting **all** the old lines (Ctrl+K once per line: count them).
- A recording can have several editors in a row on the same SD (episode 5: bm Pixel, then bm Code, then the game): `record.py` splits the key script by frame (`split_input`) and appends raw and wav.
- The game played by a key script is tuned with a log (a debug cartridge with `_update` wrapped) and a trial-and-error search of the jump frames (`scratchpad/search.py` of episode 5: jump time and press duration), then `verify.py` checks it (coins, stomps, lives).
- Named zones and collision boxes (SPRITES and BOXES sections) are made only with `scripts/bmres.py` / `mkbm.py`: they have no editor on the console, so the series does not use them.
- The final game is played by a robot (`video/07-play/bot.py`): it overrides `btn`/`btnp` in a copy of the cartridge, decides from the game state (pit ahead, slime, coin, only if there is ground to land on) and records the keys as a script for `bmhost --input`; `replay.py` replays it with a log and gives the same result (flag frame, score, lives). Two mistakes not to repeat: the ground ahead is checked at ground level (not under the feet) and taking the platforms into account (`mflags & 3`).
- A recap episode reuses the first seconds of the videos already made (`ep0N.mp4`, which open with the hook): missing audio tracks → silence, everything brought back to 1920×1080, 60 fps, stereo 48 kHz.
