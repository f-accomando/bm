---
name: bm-docs-map
description: Map of which file to open in bm for each area (API, progress, roadmap, kernel, runtime) and how to read large files without burning tokens. Use it before reading docs/API.md, docs/progress*, docs/ROADMAP.md, src/bm/runtime.c or any file over ~15 KB, when you need to understand where something is in the repo, or when you need to update progress after a change.
---

# bm: where to read and how to update (little, by sections)

A single index: `docs/progress.md` (areas, navigation, reference docs). Start from there.
To keep the docs lean after a change: skill `docs-loop`.

Rule: never a large file in full. `Grep -n` on the symbol or on `^##`, then `Read` with
`offset`/`limit` on the span. A file already read in the session is not reread.

## Game API (English)

`docs/API.md` = index + basics (structure, colors, budget). The rest in `docs/api/`:

| Area | File |
|---|---|
| screen, shapes, sprites, map | `draw.md` |
| keys, pad, mouse, touch | `input.md` |
| time, UDP network, saves, documents | `system.md` |
| sound, notes, instruments, bank | `sound.md` |
| keyboard/files for tools, assistant | `tools.md` |
| nano8, light, software 3D, collisions | `light3d.md` |
| bmlib | `bmlib.md` |
| riff | `riff.md` |
| bmnet | `bmnet.md` |

API change: update the file of its area, `docs/GAME-GUIDE.md` (right section),
`src/ai/kb/` (one file per area), then `make ai-model`. Not the `-IT` ones.

## Progress (current state, not chronicle)

Index: `docs/progress.md` (section *Which section to open*: system, multitasking, graphics,
v3d, lua, audio, platforms, tools). One area = one file in `docs/progress/`; each has `## How it is today` (`###` subsections per module with the symbols
to search), `## Open work`, `## Rules (do not break)`.

To update after a change:
1. `Grep -n '^##' docs/progress/<area>.md`, read only the subsection of the touched module.
2. Fix or remove the lines that have become false; add only state (files, symbols), never dates
   or "we did". A closed item is removed from *Open work*.
3. The file exceeds ~120 lines → split it by theme (`<area>-<tema>.md`), link from the parent file
   and add the line in `docs/progress.md`.
4. Milestone opened/closed: only its section in `docs/ROADMAP.md` (`Grep '^## M47'`) and the
   line in *Open milestones* of `docs/progress.md`. New ideas: at the bottom of ROADMAP (R…).

Delegate to `bm-mechanic` if the lines are already decided.

## Large code files (never in full)

`runtime.c` has its section map in `docs/progress/runtime.md`; the commands for every
large file are in `docs/progress/reading.md`.

**Always-fresh index**: `python3 scripts/cindex.py <file> [parola]` prints the section banners and the
functions with the line (C and Lua); with the word, only the lines containing it. Then `Read` with
`offset`/`limit`. For a Lua function exposed to games search `l_<nome>` (the `api[]` table
in `runtime.c` links name → `l_…`).

- `src/bm/runtime.c` (350 KB): `Grep -n 'static int l_<nome>\|"<nome>"'` for an exposed
  Lua function; `Grep -n '^[a-z].*(' ` limited with `head_limit` for the function list.
- `src/kernel/menu_ui.c`, `settings.c`: search for the on-screen line/string, then read
  ±60 lines.
- `src/script/bm3d.lua`, `bmlib.lua`, `bmui.lua`: `Grep -n '^function\|^local function\|^P\.'`
  then `Read` of the span.
- `docs/GAME-GUIDE.md`, `docs/B16.md`, `sdk/README.md`, `docs/RGB30.md`, `docs/DRIVERS.md`:
  `Grep -n '^#'` and read the section.

## Other docs per area

GPU: `docs/DRIVERS.md`, `docs/BENCH3D.md` · RGB30: `docs/RGB30.md` · `.b16`: `docs/B16.md` ·
`BMRES` resources: `docs/RISORSE.md` · riff: `docs/RIFF.md` · Predict and pad: `docs/PREDICT.md`,
`docs/PADTYPE.md` · assistant: `src/ai/kb/README.md`, `AI.md` · Market: `market/README.md`.

## Wide searches

"Where is X / who uses Y" across several files → `Explore` agent (returns with the conclusion, not the files).
