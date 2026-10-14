# Video 7 (bm Code, the game logic): state (2026-10-14)

- `game_flat.lua` is the 16 code blocks of `docs/video/07-code/script.md` joined in order (the script and the file are identical: checked). `try.py` builds a cartridge from the map of video 6 and runs it in bmhost with a scripted pad (title, up held, jumps, spins) and saves shots. Result: the game runs, apples are collected (7 in the test), lives go down on the wall, no error.
- **Pico is not centred on its origin** (the cross: x -1..4, z -3..1): `bounds3d` gives the centre, and `draw3d` is called at `pico.x - (cx*cos + cz*sin)*SC`, `pico.z - (cz*cos - cx*sin)*SC` (checked from above with four faces: the rotation is the standard one). Everything is drawn at scale 0.4 to fit the 3-unit corridor.
- `PLAYER_START` and `GOAL` must exist in the map: video 4 places them with `Shift+S` / `Shift+G`.
- The assistant (F6) exists only in `build/host/bmhost-ai` (`make bmhost-ai`, built; needs `build/assist.bin`). Two or three recordings (bm Code with `bmhost-ai --tool`, the SDK for `Ctrl+B`, the game without `--tool`). **Still to do**: `record.py`/`verify.py` for bm Code (open the project, `Ctrl+K` cuts the old lines, type the blocks with auto-indent, F6 question, `Enter` inserts the assistant's block), the build, the game recording.

## Recorder state (2026-10-14 15:30)
- `record.py` (run in WSL: `python3 video/3d/07-code/record.py [--shots]`) builds `out/input-r1.txt` (bm Code, 205 s, 1 LATE of 1 s), `input-r3.txt` (SDK, Ctrl+O, Ctrl+B) and `game.txt`, runs them, saves `out/07-code.bm`. R1 types all 15 blocks correctly (the saved code equals `game_flat.lua` except block 4c).
- **Open problem: block 4c.** With `bmhost-ai`, `third person camera` ranks first "Camminare in prima persona in 3D" and the panel answers in Italian (`IT`, also with `assist_lang=en` in the SD `config.txt`); `guide.3d_camera` is not in the first six answers (`proto.py "question" N` probes it). Needs the assistant's ranking/language fixed (kb or weights) or another question that finds the guide; do not fake the answer.
- Ctrl+O needs >= 140 frames before Enter to open the project (the list is slow).
