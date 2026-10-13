# bm_core

Headless Python library of the bm-forge skill: reads, checks and writes bm cartridges
(`.bm` Pi, `.b16` RGB30, `.bme` projects, `BMRES` resource files) and their MESH (8) and
ANIM (9) sections. Python >= 3.8, standard library only, no network. Formats:
`../references/FORMATS.md`; open doubts: `../references/OPEN_QUESTIONS.md`.

## Layout

| File | What |
|---|---|
| `errors.py` | `ValidationError(message, where)`: the only exception for bad input |
| `binary.py` | f32, bounded reader, char[16] names (15 bytes + NUL on write) |
| `container.py` | `Header`, `Section`, `Cart`; `parse`, `validate`, `build`, `new_cart`; editions (`edition_of`, `check_path`, `read_file`, `write_file`); BMRES kinds |
| `sections.py` | checks of LUA, SHEET, SHEET8, MAP, COVER, AUDIO (magic only), INFO, SPRITES, LAYERS, FLAGS, BOXES |
| `mesh.py` | `Face`, `Model`, `Mesh`; `decode`, `encode`, `validate` (tool limits 4096/16384, `kernel_limits=True` for 65535) |
| `anim.py` | `Bone`, `Pose`, `Key`, `Clip`, `Rig`, `Anim`; `decode`, `encode`, `validate`, `validate_links` |
| `glb.py` | `.glb` -> `Mesh`, port of the glTF half of `scripts/bmmesh.py` |
| `syncwatch.py`, `SYNC.json` | drift watch on the sources bm_core was ported from |

Every dataclass has `to_dict()` / `from_dict()` (JSON-safe). Each function's docstring
names the original (`file:function` or `file:line`). bm_core does not import the repo's
scripts; the tests do, by path, to cross-check.

```python
import bm_core as B
cart = B.read_file("game.bme")          # parse + name/edition check
m = cart.mesh()                          # Mesh, or None
m.models[0].verts[0][1] += 0.5
cart.set_mesh(m)                         # replaces in place, header kept
B.write_file("game.bme", cart, source_edition=B.edition_of_cart(cart))
```

## Tests

From `.claude/skills/bm-forge` (on Windows, in WSL through `scripts/wsl-run.ps1`):

```sh
python3 -m unittest discover tests/golden                  # all but the slow test
BM_FORGE_SLOW=1 python3 -m unittest discover tests/golden  # + QEMU (needs qemu-system-arm, build/kernel.img)
```

Goldens and how they were made: `../tests/golden/README.md`.

## Keeping in sync

bm_core is a port, so it drifts when these change:

- `src/bm/bm.h` (layouts, limits, section types)
- `src/bm/format.c` (the kernel's checks)
- `scripts/bmmesh.py` (the Python writer bm_core mirrors)
- `src/script/bm3d.lua` (the tools' encoders and the 4096/16384 limits)
- `src/gpu/version3d.h` (`BM3D_VERSION`, recorded for information)

```sh
python3 -m bm_core.syncwatch check     # exit 1 + list on drift (also a test: test_syncwatch.py)
python3 -m bm_core.syncwatch update    # after porting the change and passing the golden tests
```

Hashes ignore CRLF vs LF. On drift: read the diff of that file, port it, re-make the
goldens if a writer changed (`../tests/golden/README.md`), run the tests, then `update`.

## Known limits

- No golden from the kernel's own writer (`bm_rewrite_with`); luahost goldens use a stand-in
  container. QEMU conformance never run here.
- AUDIO bank, LUA and SHEET pixels are not decoded; no animation playback maths.
- Stricter than the kernel on several points (see OPEN_QUESTIONS.md items 1-9).
