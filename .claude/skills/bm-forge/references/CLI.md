# bm-forge: CLI reference

Entry: `python .claude/skills/bm-forge/scripts/bm_forge.py <sub> ...` (`build_parser`, `scripts/bm_forge.py:28`).
Paths below are relative to `.claude/skills/bm-forge`; `<ABS>` is an absolute directory. Errors go to
stderr as `error: ...`. Cart editions (`.bm`, `.b16`, `.bme`) are never converted.
Launchers `scripts/bm-forge` (sh) and `scripts/bm-forge.cmd` (Windows) run `doctor` (checked 2026-10-13: WSL and Windows).

## mesh

```
mesh INPUT [INPUT ...] -o OUT [--title T] [--author A] [--max-tris N] [--kernel-limits]
```

- Inputs: `.json` boxes (`bm_core/shapes.py:44` `load_boxes`: `{"model": name, "boxes": [{"lo": [x,y,z], "hi": [x,y,z],
  "colour": 0xRRGGBB or "#RRGGBB", "bone": optional}]}`, `shapes.py:27`) or untextured `.glb` (`glb.glb_models`).
- Output `.bm` (models cart) or `.bmm` (BMRES); any other extension exits 2 (`bm_forge.py:155`). Input equal to output exits 2.
- Textured `.glb` faces are refused (exit 1, `bm_forge.py:208`): use `scripts/mkbm.py --models`.
- `--max-tris`: total triangles over all inputs, over it exits 1 (`bm_forge.py:219`).
- `--kernel-limits` only matters for `.glb` inputs: box JSON is validated against 4096 vertices while loading, so a larger
  one exits 2 before the flag is read (`references/OPEN_QUESTIONS.md`, section CLI).
- Output line: `wrote OUT: N models, T triangles`.
- Exit: 0; 1 validation, budget, textured glb, inset mismatch; 2 usage, missing input, bad JSON.

```
python .claude/skills/bm-forge/scripts/bm_forge.py mesh examples/humanoid_boxes.json -o <ABS>/humanoid.bm --max-tris 300
```

## skel

```
skel SKELETON.json --in FILE -o OUT [--strategy nearest_bone|nearest_bone_part]
```

- Schema: `bm_core/skelgen.py` module docstring (`{"model", "bones": [{"name", "parent", "head", "tail" | "length" +
  "direction", "connect"}]}`, 1..64 bones, one root, unknown keys refused). `skelgen.skeleton` reads it.
- `FILE` is a cart holding the model named by `"model"` (missing: exit 1). Default strategy `nearest_bone_part`.
- Replaces the rig of that model if one exists, printing `replaced rig M (N clips dropped)` (`bm_forge.py:324`).
- `OUT` must have the same edition as `FILE`, else `error: editions are never converted` (exit 2).
- Output line: `wrote OUT: rig M, B bones, root R`.
- Exit: 0; 1 validation, model not in cart; 2 usage, missing file, no MESH, edition mismatch.

```
python .claude/skills/bm-forge/scripts/bm_forge.py skel examples/humanoid.json --in <ABS>/humanoid.bm -o <ABS>/rig.bm
```

## anim

```
anim CLIP.json [CLIP.json ...] --in FILE -o OUT [--rig MODEL] [--replace]
```

- Schema: `bm_core/animgen.py` module docstring (`{"name", "loop", "frames" | "length", "curve", "keys": [{"frame" | "time",
  "pose": {bone: {"rot" | "q" | "axis"+"angle", "move"}}, "mirror": frame}]}`). Keys sit on a 12 frames/s grid.
- `FILE` needs a rig (else `error: no rig: run skel first`, exit 1). With more than one rig `--rig` is required.
- Duplicate clip names exit 1 unless `--replace`.
- Output lines: `clip NAME: F frames, mode M, K keys, baked yes|no` per clip (plus `(snapped N times)` when keys were
  snapped), then `wrote OUT: rig M, N clips`.
- Exit: 0; 1 no rig, duplicate clip, validation; 2 usage, missing file, unreadable clip JSON, edition mismatch.

```
python .claude/skills/bm-forge/scripts/bm_forge.py anim examples/idle.json examples/walk.json examples/jump.json --in <ABS>/rig.bm -o <ABS>/anim.bm
```

## export

```
export models FROM -o OUT.bm [--title T] [--author A]
export bmm    FROM -o OUT.bmm [--model NAME ...]
export cart   FROM --lua MAIN.lua -o OUT.bm|OUT.b16 --title T [--author A] [--res WxH] [--cover-model NAME]
              [--draw NAME=COUNT ...] [--budget N]
export folder FROM --lua MAIN.lua --root DIR --game ID --title T [--author A] [--res WxH] [--cover-model NAME] [--overwrite]
export market --root DIR --id ID --version V --license L --about TEXT [--bm PATH] [--b16 PATH]
```

- `FROM` is a cart (`.bm`/`.b16`/`.bme`) with MESH (and ANIM, carried over). No MESH: exit 1.
- `models`: `wrote OUT: N models, T triangles` (`bm_forge.py:518`). `bmm`: same line; `--model` filters models and their rigs.
- `cart`: default `--res` 360x360 for `.b16`, 320x180 otherwise (`bm_forge.py:628-631`). `--draw` lists how many times each
  model is drawn a frame; the budget defaults to `export.TRI_BUDGET_B16` / `TRI_BUDGET_PI`. `--cover-model` renders an 88x88
  cover. Output line: `wrote OUT: bm|b16 WxH, T triangles a frame (budget B)`.
- `folder`: repo layout, one path per line; `--game` is lowercase alphanumeric/underscore; existing folder needs `--overwrite`. Default `--res` 320x180.
- `market`: Market layout from existing `--bm` / `--b16` files; prints one path per line then `about.txt line: ...`.
- Exit: 0; 1 validation or no MESH; 2 usage, missing file, malformed `--res` or `--draw`.

```
python .claude/skills/bm-forge/scripts/bm_forge.py export cart <ABS>/anim.bm --lua examples/game_demo/b16/main.lua -o <ABS>/forgedemo.b16 --title "Forge Demo" --cover-model humanoid
```

(The model must exist in `anim.bm`, and the demo Lua expects its `humanoid` + `ground` models and `--draw`; for a ready
two-edition build use `python examples/game_demo/build.py --out <ABS>/demo`.)

## check

```
check FILE [FILE ...] [--kernel-limits] [--budget N] [--draw NAME=COUNT ...] [--reference REF]
      [--model NAME] [--ref-model NAME] [--view front|side|top] [--min-iou F] [--no-sync] [--json]
```

- Reads any cart or BMRES file. Per file: `OK PATH` / `FAIL PATH`, then `  edition ED WxH` (or `  resource kind K`),
  `  sections ...`, `  model NAME: V verts, F faces (limit mv/mf)`, `  rig M: B bones, clips ...`,
  `  budget: T triangles a frame, budget B` (`bm_forge.py:909-948`).
- The budget fails (exit 1) only when `--budget` or `--draw` is given; otherwise it is reported.
- `--reference REF` (exactly one FILE): `.png` (silhouette from `--view`, default front) or a cart/BMRES (front, side, top, mean).
  Lines `  iou ...`; below `--min-iou` (default `similarity.DEFAULT_MIN_IOU`, 0.75 per `references/LOOP.md`) prints
  `  FAIL iou` and exits 3.
- Unless `--no-sync`, also compares bm_core with the repo sources: `DRIFT REL: recorded X, now Y`, exit 4.
- Exit order when several apply: 2, then 1, then 3, then 4 (`bm_forge.py:973-982`).
- `--json` prints only one object (`bm_forge.py:984`), keys sorted:
  `{"exit": N, "files": [{"path", "ok", "error", "edition", "width", "height", "sections": [], "models": [{"name", "verts",
  "faces"}], "limits": [mv, mf], "rigs": [{"model", "bones", "clips": []}], "triangles", "budget", "budget_enforced"}],
  "similarity": null | {"ref", "model", "iou" | "front"/"side"/"top"/"mean", "min_iou", "pass"}, "sync": {"checked": bool,
  "drift": [[rel, old, new]]}}`.

```
python .claude/skills/bm-forge/scripts/bm_forge.py check tests/fixtures/village/models.bm --budget 2000 --no-sync --json
```

(`tests/fixtures/village/models.bm` is at the repo root; run from there.)

## preview

```
preview FILE -o OUT.png|OUT.gif [--model NAME] [--clip NAME | --turntable N] [--size WxH] [--yaw R] [--pitch R]
        [--ortho] [--bones] [--fps 12] [--cols N] [--mask] [--compare REF] [--ref-model NAME] [--view V] [--min-iou F]
```

- Headless render with the cart's own texture. Default: still 256x256, yaw 0.6, pitch 0.35. `--turntable N`: N frames
  around a circle (128x128). `--clip NAME`: frames of a clip (the model needs a rig; 128x128; `--fps` default 12).
- `.gif` needs `--clip` or `--turntable`; `.png` of those is a contact sheet (`--cols`, clip default 6). `--mask` writes the
  silhouette as `.png` (96x96, `--view`). `--clip` with `--turntable` exits 2.
- `--compare REF` then runs `check --reference` on FILE (same lines, exit 3 below `--min-iou`; sync is skipped).
- Output line: `wrote OUT: N frame(s) WxH`.
- Exit: 0 (or the `--compare` exit); 1 no MESH, no rig, validation; 2 usage, bad extension, missing file or model, bad `--size`.

```
python .claude/skills/bm-forge/scripts/bm_forge.py preview <ABS>/anim.bm --clip walk -o <ABS>/walk.gif
```

## doctor

```
doctor [--json]
```

- Reports (found via `importlib.util.find_spec` / `shutil.which`, nothing imported): Python version (needs 3.8), `blender`,
  modules `numpy` `trimesh` `PIL`, `build/host/bmhost-bin`, `qemu-system-arm`, `wsl`, and bm_core sync. Last line
  `core needs none of the optional tools`.
- `--json` prints one object with keys `python`, `blender`, `modules`, `bmhost_bin`, `qemu_system_arm`, `wsl`, `sync`,
  `core_needs_none`, `exit`.
- Always exit 0.

```
python .claude/skills/bm-forge/scripts/bm_forge.py doctor --json
```

## Exit codes

| Code | Meaning |
|---|---|
| 0 | OK |
| 1 | rule broken: validation, budget, missing rig/model/MESH, textured glb |
| 2 | usage or I/O: bad arguments, missing file, edition mismatch, same input and output |
| 3 | similarity below `--min-iou` (`check --reference`, `preview --compare`) |
| 4 | bm_core drift against the repo sources (`check` without `--no-sync`) |

Known limits: `--kernel-limits` only matters for `.glb` inputs of `mesh`; textured `.glb` is refused; `check` exit 4 can also
fire on changes unrelated to the format (`references/LOOP.md`, Keep in sync).
