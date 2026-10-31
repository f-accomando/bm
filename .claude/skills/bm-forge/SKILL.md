---
name: bm-forge
description: Headless stdlib Python toolkit for bm 3D assets and games. Use it to build MESH models from box JSON or untextured .glb, generate skeletons and animation clips from JSON, export models.bm / .bmm / .bm carts and repo/Market folders, validate any .bm/.bme or BMRES file (.bmm .bmi .bms .bmt .bmc .bmk), render PNG/GIF previews, and compare a model to a reference silhouette. Not for kernel, GPU or Lua runtime changes.
---

# bm-forge

## What it is

A command line (`scripts/bm_forge.py`) over `bm_core`, a stdlib-only Python port of the bm
container, MESH and ANIM formats. It builds, exports, validates and previews 3D assets and
example games without the console, a GPU or any third-party package.

## Fixed rules

- Names (models, bones, clips): 15 bytes + NUL.
- Organic subjects (characters, creatures) are never boxes/voxels and never loose pieces: one continuous
  flat-shaded skin with shared vertices at the joints, rigged (one bone per vertex). Template
  `examples/charkit/` (presets crash, rango); `references/CHARACTERS.md`, `references/LESSONS.md` L7.
- Default limits 4096 vertices / 16384 triangles a model; `--kernel-limits` raises them to 65535.
- Validation is strict: stricter than the kernel reader on purpose (`references/OPEN_QUESTIONS.md`).
- A `.bm` cartridge says in its header (`targets`: pi, rgb30) which kernels it is for. `export cart --targets` writes it.
  `skel`/`anim` refuse an output kind different from the input (`.bm` to `.bme`) (`scripts/bm_forge.py:336`).
- Something not checked is written NON VERIFICATO, never stated as fact.
- No network, no third-party tool needed (`doctor` lists the optional ones).
- Always pass absolute `-o` paths. Never write outputs to the repo root.
- The real console/kernel was never run on generated files. Only the PC runtime `bmhost`
  was, in an opt-in test (`tests/golden/test_bmhost_demo.py`, `BM_FORGE_HOST=1`).

## CLI at a glance

Command: `python .claude/skills/bm-forge/scripts/bm_forge.py <sub>`. Launchers `scripts/bm-forge` (sh)
and `scripts/bm-forge.cmd` (Windows) run `doctor` (checked 2026-10-13: WSL and Windows).
Details, syntax and output lines: `references/CLI.md`.

- `mesh` boxes `.json` / untextured `.glb` -> models `.bm` or `.bmm`.
- `skel` skeleton `.json` + a cart with the model -> cart with a bound rig.
- `anim` clip `.json` files + a cart with a rig -> cart with the clips.
- `export` `models | bmm | cart | folder | market`: models cart, BMRES model, game `.bm`, repo folder, Market layout.
- `check` validate files; budget, drift, `--reference` silhouette similarity, `--json`.
- `preview` render PNG/GIF (still, `--turntable`, `--clip`, `--mask`, `--compare`).
- `gpuskin` say which faces keep a skinned model off the console's GPU (three bones, glossy on two bones) and fix them:
  `--two-bones` (bm3d 7.33 draws the rest), `--palette X,Y[,W] --palette-png STRIP.png` (kernels before 7.33: the plain
  faces on two bones become texels of a strip; grey light and no fog, as textured faces).
- `doctor` report optional tools and bm_core drift.

| Exit | Meaning |
|---|---|
| 0 | OK |
| 1 | rule broken (validation, budget, missing rig or model) |
| 2 | usage or I/O error |
| 3 | similarity below `--min-iou` (`check`, `preview --compare`) |
| 4 | bm_core drift against the repo sources (`check`) |

## Improvement loop

1. Generate (`mesh`, `skel`, `anim`, `export`).
2. `check` the result.
3. `preview` it to PNG/GIF in the scratchpad.
4. Look at the image (Read shows PNG/GIF) and judge it against the request.
5. Fix the source JSON or command and repeat. At most 3 iterations, then stop and report
   what is missing and why. Metrics and report content: `references/LOOP.md`.

## Come chiedere

The field names COSA / VINCOLI / OUTPUT / VERIFICA stay in Italian as requested; everything else is English.
Template: COSA (what to make), VINCOLI (limits, budget, names), OUTPUT (absolute paths), VERIFICA (checks to run).
Example paths are relative to `.claude/skills/bm-forge`; `<ABS>` is an absolute directory.

```
COSA: the humanoid model from examples/humanoid_boxes.json
VINCOLI: at most 300 triangles, model name "humanoid"
OUTPUT: <ABS>/humanoid.bm
VERIFICA: mesh ... --max-tris 300; check <ABS>/humanoid.bm; preview PNG
```

```
COSA: bind the humanoid skeleton to the model above
VINCOLI: strategy nearest_bone_part, bones as in examples/humanoid.json
OUTPUT: <ABS>/rig.bm
VERIFICA: skel examples/humanoid.json --in <ABS>/humanoid.bm -o <ABS>/rig.bm; preview --bones
```

```
COSA: add the walk clip to the rig
VINCOLI: 12 fps grid, loop on, clip as in examples/walk.json
OUTPUT: <ABS>/anim.bm
VERIFICA: anim examples/walk.json --in <ABS>/rig.bm -o <ABS>/anim.bm; preview --clip walk -o <ABS>/walk.gif
```

```
COSA: a .bm cart for Pi and RGB30 from the animated model
VINCOLI: 360x360, lua examples/game_demo/bm/main.lua, targets pi,rgb30
OUTPUT: <ABS>/forgedemo.bm
VERIFICA: export cart <ABS>/anim.bm --lua ... -o <ABS>/forgedemo.bm --res 360x360 --targets pi,rgb30 --title "Forge Demo"; check <ABS>/forgedemo.bm
```

```
COSA: keep the skinned models of a game on the GPU
VINCOLI: continuous skin, no seams but at the faces on three bones
OUTPUT: <ABS>/game_gpu.bmm
VERIFICA: gpuskin <ABS>/game.bmm; gpuskin <ABS>/game.bmm --two-bones -o <ABS>/game_gpu.bmm; check <ABS>/game_gpu.bmm
```

```
COSA: validate an existing asset
VINCOLI: kernel limits, budget 2000 triangles a frame, no edits to the file
OUTPUT: pass/fail lines and the last check --json
VERIFICA: check .claude/skills/bm-forge/tests/golden/village_models.bm --kernel-limits --budget 2000 --json
```

```
COSA: build the example game
VINCOLI: .bm from its Lua source (bm/main.lua)
OUTPUT: <ABS>/demo/ (forgedemo.bm, HUMANOID.BMM, repo/, market/, previews)
VERIFICA: python examples/game_demo/build.py --out <ABS>/demo; check <ABS>/demo/forgedemo.bm
```

## Keep in sync

`bm_core` was ported from repo files; drift is detected with `python -m bm_core.syncwatch check|update`,
run from the skill folder. Procedure on drift: `references/LOOP.md` `## Keep in sync`.

## References

- `references/FORMATS.md`: bm native formats (container, MESH, ANIM, BMRES) as bm_core reads them.
- `references/API.md`: the game author's API and pipeline.
- `references/TOOLS.md`: the console's 3D tools, how they run, what can be reused.
- `references/OPEN_QUESTIONS.md`: format and tooling doubts, choices made, what is NON VERIFICATO.
- `references/CLI.md`: exact syntax, output lines and exit codes of every subcommand.
- `references/LOOP.md`: improvement loop, metrics, optional tools, keep-in-sync procedure.
- `references/LESSONS.md`: cases and rules learnt while using the skill.
- `references/DETAIL.md`: limits, polygon reduction against building low, what 1200 triangles keep (mannequin test).
- `references/CHARACTERS.md`: shared parts of characters and the `examples/charkit` spec (presets Crash, Rango).
