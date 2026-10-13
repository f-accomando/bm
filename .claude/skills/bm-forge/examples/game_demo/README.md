# forgedemo: the bm-forge example game

A humanoid (7 bones) on a flat ground, cycling through three clips (idle, walk, jump)
while its head turns. Two separate editions, each with its own source:

- `bm/main.lua`: Pi, 320x180, `.bm`.
- `b16/main.lua`: RGB30, 360x360, `.b16` (written on its own, never made from the `.bm`).

Assets: `../humanoid.json` + `../humanoid_boxes.json` (rig and body), `ground_boxes.json`,
clips `../idle.json`, `../walk.json`, `../jump.json`. `input.txt` is the bmhost script
(one screenshot per clip).

## Build

    python examples/game_demo/build.py --out DIR

Opt-in bmhost run (needs `BM_FORGE_HOST=1`), from Windows through WSL:

    scripts/wsl-run.ps1  BM_FORGE_HOST=1 python3 -m unittest discover tests/golden -p test_bmhost_demo.py

## Runtime APIs used

`model`, `models`, `bones3d`, `clips`, `animate`, `bone3d`, `bone_turn`, `draw3d`,
`camera3d`, `light3d`, `zclear`, `stat`, `log`, `btn`/`btnp` (Pi: numbers 4, 5, 0, 1;
RGB30: names `"ok"`, `"back"`, `"left"`, `"right"`).

## Self-checks

At frame 10 each edition logs `forgedemo: N/M checks passed` (7 checks: models, bones,
clips, `animate` length, head height) and `forgedemo: tris N`; bmhost reads those lines.

## Budget

Triangles: 168 of 1200 (156 humanoid + 12 ground).

## Status

Never run on a console: NON VERIFICATO.
