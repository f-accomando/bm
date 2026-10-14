# Video 1 (bm Studio): corrections for docs/video/01-studio/shot-list.md (found by running the editor)

Run: `powershell -NoProfile -File scripts/wsl-run.ps1 "python3 video/3d/01-studio/record.py && python3 video/3d/01-studio/verify.py --chain"`
Script length 12601 frames = 210.0 s (target 3:30). Dev helpers: `--log` (instrumented cartridge, out/run.log), `navsearch.py`
(finds the arrows of the select/vertex pointer with the real editor: the camera eases, so a transition table is not enough).

1. bmhost starts Studio on its MENU with an unsaved project already (no Dev tab): rows 2-4 become Esc (to F1), Esc (menu), Ctrl+N.
2. Rename box holds the old name: `model` = 5 Backspace (not 16); Save box holds `MY3D.BME` = 8 Backspace (the list said 10).
3. `n` on F2 jumps to the build page of the new model: rows 28/47 are `n`, `F2`, `r`, name, `Enter`.
4. After Ctrl+Z (undo of the removal) and the colour pick, the cursor is on level 1: row 23 needs `PgDn` before `Backspace`, `Space`.
5. The colour picker remembers the Colours page: `c` only the first time. Swatch numbers (6 per row, Up/Down = 6): brown 14 (Down, Down, Right
   from the start), red 9, orange 11, mint 26 (60D0C0), yellow 12, cream 13. Cursor resets to cell 0,0,0 on a new model.
6. APPLE layer 2 needs all 9 blocks (the plus cells too, not only the 4 corners). Counts confirmed: 54 faces, 108 tri, 56 vertices.
7. Vertex tool: the pointer already stands on a corner; arrows found with navsearch (APPLE_CORNERS in record.py). `Tab` twice = step 1/4;
   `PgDn` moves 0.25. Reduction 108 -> 80 reaches exactly 80 ("reduced to 80 triangles  (Ctrl+Z undoes)"); undo/redo OK.
8. Default camera looks from -z: Pico's belly (+z) is hidden. Row 61 needs `3`, `q` x4 (turn 180), `z` (frame), `-` x3 (zoom out); then `e` x4 back
   before the tail-end face (-z). Release the six faces with `a` (one key), not six Space.
9. Select-move step is remembered from the vertex move (1/4): the tail tip rises 0.25 with one `PgUp`. PICO = 58 faces, 116 tri, 64 vertices.
10. F12 held works through the input line `keys 0x45` (key `?` is not a held key in bmhost). Verified shot r10_help.
11. `build/studio.bm` in this checkout is broken ("main.lua:1: unexpected symbol near '#'", 39 KB): record.py makes its own out/studio.bm with
    scripts/mkbm.py from carts/studio (same command as the Makefile).
12. Still open: 0:00 hook and 0:08 Dev tab are post/QEMU cards (the run shows the menu 0:00-0:11); silhouette IoU vs reference is 0.0 (different scale:
    informational only, not a pass criterion).
13. Chain: `chain/01-studio.bme` = the saved project; `chain/01-studio.bm` = `bm_forge.py export models` (3 models, 208 triangles).
