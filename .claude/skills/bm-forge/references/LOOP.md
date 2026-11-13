# bm-forge: improvement loop and keep-in-sync

Commands run from `.claude/skills/bm-forge`. CLI details: `references/CLI.md`.

## Loop

1. Generate the file (`mesh`, `skel`, `anim`, `export`).
2. `python core/scripts/bm_forge.py check FILE...` and the tests that cover what changed.
3. `python core/scripts/bm_forge.py preview FILE -o OUT.png` (or `.gif`), writing into the scratchpad.
4. Read the image (the Read tool shows PNG/GIF) and judge it against the request.
5. Fix the source description or the command, go back to 1.

At most 3 iterations. Then stop with a report holding:

- what is still missing;
- why (the rule or limit that blocks it, or what could not be verified);
- the last `check --json` output;
- the paths of the images looked at.

Exit codes of the CLI: 0 OK, 1 rule broken, 2 usage/IO, 3 similarity below `--min-iou`, 4 check drift.

## Metrics

- **Green tests**: `python -m unittest discover tests/golden`.
- **Budget respected**: `check --budget N` and/or `check --draw NAME=COUNT`.
- **Conformity to goldens**: `test_roundtrip.py`, `test_schema.py` and `test_crosscheck.py` stay green.
- **Similarity** (only when a reference is given): `check --reference REF`, mean IoU >= 0.75 by
  default (`--min-iou F` changes it). It is a silhouette measure: scale-invariant, ignores colour.

## Optional tools

`python core/scripts/bm_forge.py doctor [--json]` reports what is available. Blender, MCP servers,
trimesh, numpy and PIL are optional help only: the core (`bm_core`) never needs them.

## Keep in sync

`bm_core` was ported from repo files; `bm_core/SYNC.json` records their hashes.

```
python -m bm_core.syncwatch check     # lists drift, exit 1
python -m bm_core.syncwatch update    # records the current hashes
```

It watches (`WATCH` in `bm_core/syncwatch.py:20`): `core/src/bm/bm.h`, `core/src/bm/format.c`,
`core/scripts/bmmesh.py`, `core/src/script/bm3d.lua`, `core/src/gpu/version3d.h`, `core/src/bm/runtime.c`,
`core/src/bm/r3d.c`, `bm/animator/main.lua`; plus `BM3D_VERSION` (`syncwatch.py:57-58`).
Hashes ignore CRLF (`syncwatch.py:23-30`). A whole-file hash on `runtime.c`, `r3d.c` and
`main.lua` can flag changes unrelated to the format (the CLI check tolerates this, exit 4).

On drift:

1. Read the diff of each listed file.
2. Port what touches the format or the playback port (`bm_core/skin.py`).
3. If a writer changed, re-make the goldens (`tests/golden/README.md`).
4. Run `python -m unittest discover tests/golden`.
5. Then `python -m bm_core.syncwatch update`.

Tests that go red, and why:

- `test_syncwatch.py`: any watched file changed.
- `test_crosscheck.py`: `core/scripts/bmmesh.py` changed.
- `test_roundtrip.py` and `test_schema.py`: format changes.
- `test_cli_check.py::test_check_sync`: tolerates exit 4 (drift). NON VERIFICATO until that file exists.
- `test_bmhost_demo.py`: runtime changes; opt-in (`BM_FORGE_HOST=1`), needs a rebuilt
  `build/host/bmhost-bin` (`make build/host/bmhost-bin`).
