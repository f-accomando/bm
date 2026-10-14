# Episode 2: corrections for docs/video/02-pixel/ (found by running the editors)

Rerun: `powershell -NoProfile -File scripts/wsl-run.ps1 "python3 video/3d/02-pixel/record.py" -Tail 20` then `... "python3 video/3d/02-pixel/verify.py"`.
First run replays the editor ~85 times to plan the face pointer (about 3 minutes); out/plan_cache.json makes later runs ~20 s.

## Shot list / input.md corrections
- build/pixel.bm, build/studio.bm (and animator/sound) hold bm.info as their Lua (Makefile passes `$<` = bm.info to --lua):
  "main.lua:1: unexpected symbol near '#'". record.py builds its own copies in out/bin/. Fix the Makefile rules.
- Rows 2-4: with bmhost --tool Pixel starts on the open list; Ctrl+O shows a list with one entry (TROPIC.BME), so no `Down`, only Enter.
  Sprite size is already 16x16 on open; `z` x5 (16>32>64>128>8>16) only shows it. A size change snaps the sprite to that size grid,
  so do it BEFORE moving to the free row.
- Tile position (row 4 [DA VERIFICARE] closed): Studio's starter tiles fill sheet rows 0-47 (256x48, 3 rows of 16). The eight tiles
  go in row y=48 at x=16*n (Pixel sprite numbers 192, 194, ... in 8px cells). Reach it in Pixel: F2, Down x3, Enter.
  In Studio's tile picker (Tab) the same tile is: right x n, down x3 (picker starts at 0,0, remembers its position).
- Palette numbers (Studio palette, 1-based): grass 18/dark 17/light 20, sand 15/dark 14/light 16, grey 4/dark 3/light 5, wood 14/lines 7/grain 15,
  mint 26/dark 25, red 9/shine 10/shade 8, leaf 19, white 6, dark 2. Keys 1-9 and 0(=10), then `.` steps.
- Studio: the project opens on F1 with CRATE first (order CRATE, APPLE, PICO); F2 then Enter; no `Down`. `]` x2 reaches PICO, `[` APPLE.
- Row 46: tile size is already 16 in the picker; `z` x3 only cycles back. Row 53 (exclusion): the face pointer goes to the nearest face
  ON SCREEN and the camera follows it, so reaching all 24 flat faces needs arrows plus `q`/`e` (turn camera 45 deg) because front (+z)
  and bottom faces are hidden from the default view. record.py plans those keys (calib.py + studio_part.py, closed loop on a log copy).
  Every face of PICO is reachable.
- Row 58: `c` list starts on colour 1; white is `Down` x5, dark (1C2030) `Down` x1, Enter. Paint panel cursor starts at (8,8).
- Row 61: `e` clears (transparent) and the whole painting session is ONE undo step, so no Ctrl+Z demo; the video clears the pupil and paints it again.
- Row 66: a Pixel recording ends at 2:22: shot at 2:21.3 (a shot at the last frame is not taken).
- bmhost `type` lines must end LF: a CR is read as Enter.
- art.py: face = skin + eyes (3 white pixels, pupil dark) painted in Studio; verify.py checks them.

## Open
- `--start chain`: the face plan is recomputed for the real model of video 1 (eye faces = the two outer +z faces of the top quarter;
  flat faces = warm colours). Not yet run on the real chain/01-studio.bme.
- ffmpeg is missing in WSL: no mp4 produced (run record.py where ffmpeg exists; the overlay/shots/input are ready).
- docs: tab Dev (row 2) and the hook 0:00-0:08 are not recorded here.

## 2026-10-14 verify.py corrected on the real model
- The real Pico of video 1 spans x -1..4 (middle 1.5), y 0..4, z -3..1 (grid units), not the stand-in centred on x = 0. `verify.py` now takes the middle of the model for the eye symmetry check and the top 20% of the height for the eye faces (a quad's two triangles have centres lower than the quad). Eye faces at x 0.33 and 2.67 are the two outer front faces of the head, which is what the script means. QEMU == bmhost, `verify.py` = ALL OK.
