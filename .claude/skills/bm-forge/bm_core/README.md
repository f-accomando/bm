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
| `skin.py` | playback: `pose_at`, `skin` (port of `src/bm/runtime.c:2198-2321`), `playback()` -> per-frame bone matrices, posed bones, vertex bounds |
| `preview.py` | software rasterizer (flat light, z-buffer, back faces, SHEET/SHEET8 textures): `render_mesh`, `render_turntable`, `render_anim`, `anim_gif`, `texture_of` |
| `skelgen.py` | skeleton description (JSON) -> `Rig`: `skeleton`, `describe`; rigid binding `assign_vertices` (`nearest_bone`, `nearest_bone_part`), `bind`; `mirror_name` (bm Animator's) |
| `animgen.py` | keyframe description (JSON) -> `Clip` on the 12 fps grid: `clip`, `add_clips`, `write_anim`; native modes or baked keys |
| `shapes.py` | box builder with the villager's winding: `box`, `boxes_model`, `load_boxes` |
| `imageio.py` | `Image`, PNG writer, animated GIF writer (own LZW, median-cut palette), `contact_sheet` |
| `export.py` | `export_models`, `export_bmm`, `export_cart`, `export_folder`, `export_market`, `about_line`; `frame_triangles`, `check_budget`, `cover_section`, `models_cart` |
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

## Preview (headless images)

```python
import bm_core as B
from bm_core import preview as P, skin as S
cart = B.read_file("village.bm")
mesh, anim = cart.mesh(), cart.anim()
tex, inset = P.model_texture(cart)               # the sheet as a texture (or None), MESH inset in px
P.render_mesh(mesh.get("house"), size=(256, 256), yaw=0.6, pitch=0.35,
              texture=tex, inset=inset).save_png("house.png")      # ortho=True, fov=60, detail=3 ...
P.save_gif("turn.gif", P.render_turntable(mesh.get("well"), frames=24, texture=tex, inset=inset))
imgs, frames = P.render_anim(mesh.get("villager"), anim.get("villager"), "walk", fps=12,
                             show_bones=True, include_end=False, texture=tex, inset=inset)
P.contact_sheet(imgs, cols=5).save_png("walk.png")
P.save_gif("walk.gif", imgs, delay_cs=8)
for fr in S.playback(anim.get("villager"), "walk", fps=12, model=mesh.get("villager")):
    fr.t, fr.mats, fr.heads, fr.tails, fr.bounds  # no image: data for checks
```

Each image has `covered` (pixels drawn by faces). What follows r3d and what is
approximated (flat light only, no lamps/fog/specular, LOD level 3, screen-door phase,
no-sheet grey) is listed in the `preview.py` docstring and OPEN_QUESTIONS.md 25-36.
Speed (Python 3.14, Windows and WSL): 1560 triangles, 24 frames at 256x256 in 0.6-1.0 s;
the village models at 256x256 in ~0.01-0.02 s each. Samples: `../tests/golden/preview_samples/`
(`make_samples.py` rewrites them).

## Skeletons and clips from descriptions (phase 2)

```python
import bm_core as B
from bm_core import skelgen as SG, animgen as AG, shapes
rig = SG.skeleton("examples/humanoid.json")              # villager layout; checks names, roots, cycles, orphans
model, _ = shapes.load_boxes("examples/humanoid_boxes.json")
rig = SG.bind(model, rig, strategy="nearest_bone_part")   # one bone per vertex (the format is rigid)
reports = []
rig = AG.add_clips(rig, ["examples/idle.json", "examples/walk.json", "examples/jump.json"], reports)
cart = B.new_cart(b"function _draw() cls(0) end
", 320, 180, "hero")
AG.write_anim(cart, [rig], B.Mesh([model]))
B.write_file("hero.bm", cart)
```

The format has one interpolation mode per clip (linear, smooth, step) and no
per-vertex weights. Description rules, ranges and when a clip is **baked** (one key per
12 fps frame, mode 0) are in the `skelgen.py` / `animgen.py` docstrings; a `report`
dict says whether a clip was baked and why. Examples: `../examples/*.json`. Samples:
`../tests/golden/preview_samples/forge_*` (`make_forge_samples.py`). The generated
`.bm` passes bm_core's checks only: it was **never run on the kernel or opened in bm
Animator**. Doubts: OPEN_QUESTIONS.md 37-44.

## Export and the example game (phase 3)

All in `export.py`; every function raises `ValidationError` on bad input.

- `export_models(path.bm, mesh, anim, title, author)`: a cartridge with a stub Lua
  (`MODELS_LUA`) + MESH + ANIM if there are rigs (`export.py:92-132`).
- `export_bmm(path.bmm, ...)`: a BMRES file of kind `RES_MODEL` with MESH + ANIM
  (`export.py:135-163`).
- `export_cart(path, lua, mesh, anim, title, author, res, cover, draws, budget)`: a game
  `.bm` or `.b16` (never `.bme`); title required; optional COVER first, then LUA; checks the
  frame triangles (`draws`, default each model once) against the budget, then `check_path`, and
  8 MiB for `.b16` (`export.py:166-229`).
- `export_folder(root, game, lua, mesh, anim, title, author, res, cover, overwrite)`: writes
  `<root>/<game>/main.lua`, `models.bm`, optional `cover.png`, and `make.txt` (lines to add to
  the Makefile by hand; bm-forge never edits it). `game` matches `^[a-z][a-z0-9_]{0,14}$`;
  refuses an existing `main.lua` unless `overwrite` (`export.py:232-308`).
- `export_market(root, game_id, version, license, about, bm_path, b16_path)`: copies the given
  `.bm` / `.b16` byte for byte into `<root>/<id>/` with `info.txt` (`version:`, `license:`,
  `about:`); refuses a folder holding `<id>.bme`; `.b16` over 8 MiB refused
  (`export.py:311-402`).
- `about_line(id, license, about)` -> `id: license | about` for `market/about.txt`
  (`export.py:405-416`).
- Budgets: `TRI_BUDGET_PI = 1200` (`docs/GAME-GUIDE.md:321`); `TRI_BUDGET_B16 = 1200` is a
  placeholder, the real `.b16` cap is **NON VERIFICATO** (`export.py:21-22`).
- Editions are never converted: `.bm` and `.b16` are exported from separate sources.
- Example game: `../examples/game_demo/` (id `forgedemo`, `bm/main.lua` and `b16/main.lua`
  written separately). Build with `python examples/game_demo/build.py --out DIR`; `build.py`
  exists. See `../examples/game_demo/README.md`.
- Export never uses `cart_data`. Doubts: OPEN_QUESTIONS.md 47-53.

## Tests

From `.claude/skills/bm-forge` (on Windows, in WSL through `scripts/wsl-run.ps1`):

```sh
python3 -m unittest discover tests/golden                  # all but the slow test
BM_FORGE_SLOW=1 python3 -m unittest discover tests/golden  # + QEMU (needs qemu-system-arm, build/kernel.img)
```

Opt-in bmhost run of the example game: `BM_FORGE_HOST=1` with `tests/golden/test_bmhost_demo.py`
(2 tests, run 2026-10-13 in WSL: bm and b16 editions give "7/7 checks passed", exit 0).

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
- AUDIO bank and LUA are not decoded; SHEET/SHEET8 pixels only for the preview.
- Preview: flat shading only, see OPEN_QUESTIONS.md 25-36.
- Generated rigs/clips: checked by bm_core and its playback port only, not by the kernel
  or bm Animator (OPEN_QUESTIONS.md 37-44).
- Stricter than the kernel on several points (see OPEN_QUESTIONS.md items 1-9).
