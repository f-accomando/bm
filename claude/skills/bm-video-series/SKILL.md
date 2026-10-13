---
name: bm-video-series
description: Skeleton of the reusable skill that builds a whole YouTube tutorial series for bm (plan, per-video script, shot list, cover, metadata, automated input, recording, montage), one make target per video and one for the series. Use it for the 3D series "Tropic Trail" (docs/video/) or any new series; the single-episode recipe stays in bm-video-tutorial. Skeleton: recording and overlay are specified, not implemented yet.
---

# bm-video-series (skeleton)

A series = a game built one editor per video. This skill is the *series layer* over
`claude/skills/bm-video-tutorial` (which knows how to record one episode: bmhost, QEMU, key scripts,
lessons learned). Read that skill first; do not repeat its rules here.

## Layout (one series)

| Where | What |
|---|---|
| `docs/video/PIANO.md` | vision, final list, dependencies, open questions |
| `docs/video/STATUS.md` | checklist per video, blocking dependencies |
| `docs/video/NN-name/` | `script.md`, `shot-list.md`, `youtube.md`, `input.md`, `cover.json` (`cover.png` generated) |
| `video/3d/series.json` | the single source of truth: names, resolutions, chain of cartridges |
| `video/3d/NN-name/` | `record.py`, `input.txt`, `start.bm`, `verify.py` (written when the episode is recorded) |
| `video/3d/chain/` | `NN-name.bm`: output of video NN = input of NN+1 (generated, not in git) |
| `video/3d/video3d.mk` | `make video-3d-NN` (one video), `make -k video-3d` (the series; a blocked video stops only itself), `make video-3d-covers`, `make video-3d-check` |

## Procedure for a new episode (in order)

1. Read `docs/video/PIANO.md` and the episode's `input.md`; check `STATUS.md` for blockers.
2. Real keys only: copy them from `manuals/content/en/editor-*.md` or the editor's source. Unknown: `[DA VERIFICARE: ...]`.
3. `script.md` and `shot-list.md` first; times of the two must agree; captions in English.
4. `input.txt` from the shot list (frame = second * 60), SD cleaned at each recording, `start.bm` = the previous output.
5. `record.py`: `bmhost --tool --input ... --video ... --wav ...` through `video/lib/bmvideo.py`, then `verify.py`
   (reads the saved cartridge with `scripts/bmres.py`, `scripts/bmaudio.py`, `bm_forge.py check`).
6. Cover: `python video/lib/cover.py docs/video/NN-name/cover.json -o docs/video/NN-name/cover.png`;
   real screenshots are added to `cover.json` (`screenshot`) when the video exists.
7. Montage, then tick `STATUS.md`. Before ending: `python video/3d/check_series.py` and `python scripts/docs_audit.py score`.

## Rules

- One editor per video; only editors of the console (Dev tab). No dev-side file made outside the console in the final video.
- Names, resolutions, keys must match `series.json` and `PIANO.md` in every document (`check_series.py`).
- Original characters and sounds. Video text, subtitles and captions in English.
- 1920x1080 60 fps page; the game 480x270 (3D). Anything slower than 60 fps in QEMU is re-timed from the virtual clock.

## Key overlay (specification, not implemented)

See [`overlay.md`](overlay.md): what is drawn and where, and which data it reads.

## Not implemented yet

- `record.py` of each episode, the overlay renderer, the montage step, `verify.py` of each episode.
- The 3D episode 4 (map editor) is BLOCKED until `SYNC.md` AGY-01..03 are DONE.
