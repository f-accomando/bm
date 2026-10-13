# Golden files of bm_core

Files written by the **real** tools of the repo, never by bm_core. All made at commit
`287d05f43e8be6ace62443dfa7ba8e3759596c22` (`git rev-parse HEAD`, branch
`claude/bm-forge`) in WSL (Python 3.14.4, `build/host/luahost` = Lua 5.4 of
`third_party/lua`). Tests: `python3 -m unittest discover tests/golden` from
`.claude/skills/bm-forge`.

## How each file was made

`make test-studio` does **not** complete at this commit: `build/host/test_meshcap` fails to
link (`undefined reference to g16_to_rgb24`, `Makefile:1092`), before any luahost step.
So the luahost steps of the rule (`Makefile:1239-1257`) were run by hand, without
`test_meshcap`, into `build/forge-gold/`:

```sh
make -j$(nproc) build/host/luahost build/host/test_bm build/carts/village.bm build/carts/astrowing.bm build/demo.bm
O=build/forge-gold; rm -rf $O; mkdir -p $O
mkdir -p $O/studio3d-sd/carts && cp build/carts/village.bm $O/studio3d-sd/carts/
build/host/luahost tests/studio/tools3d_host.lua . $O/studio3d-sd      # 171/171 checks passed
mkdir -p $O/mesh-sd/carts && cp build/carts/village.bm build/carts/astrowing.bm $O/mesh-sd/carts/
build/host/luahost tests/studio/mesh_host.lua . $O/mesh-sd             # 78/78 checks passed
mkdir -p $O/pixel-sd/carts && cp build/carts/village.bm build/demo.bm $O/pixel-sd/carts/
build/host/luahost tests/studio/pixel_host.lua . $O/pixel-sd           # 68/68 checks passed
python3 tests/studio/anim_figure.py $O/anim-figure.bm
python3 tests/studio/check_files.py $O/studio3d-sd $O/mesh-sd $O/pixel-sd build/carts/village.bm   # 62/62
build/host/test_bm build/demo.bm $O/anim-figure.bm $O/studio3d-sd/carts/blocks.bme                  # 163/163
```

| Golden | Copied from | Writer |
|---|---|---|
| `village_models.bm` | `carts/village/models.bm` (tracked, last changed in `14718bb`) | bm Studio / bmmesh history; reference file |
| `anim_figure.bm` | `$O/anim-figure.bm` | `tests/studio/anim_figure.py` = `scripts/bmmesh.py` `encode_faces`/`encode`/`encode_anim` + `scripts/mkbm.py` `pack` |
| `bmmesh_lit.bm` | `python3 gen_bmmesh_lit.py bmmesh_lit.bm` (in this folder) | `scripts/bmmesh.py` `encode`/`encode_anim` + `scripts/mkbm.py` `pack`: lit model, corner and 5-6-5 lights, material bits, step/linear clips |
| `studio_blocks.bme` | `$O/studio3d-sd/carts/blocks.bme` | bm Studio + bm Animator (`carts/studio`, `carts/animator`, `src/script/bm3d.lua`) under luahost: new project, skeleton, clip |
| `studio_copy3d.bme` | `$O/studio3d-sd/carts/copy3d.bme` | bm Studio under luahost: the village saved with no edits |
| `mesh_village.bme` | `$O/mesh-sd/carts/village.bme` | bm Mesh (`carts/mesh/main.lua`) under luahost: villager edited with its skeleton (`meshcopy.bme` is byte-identical, not copied) |
| `mesh_astrowin.bme` | `$O/mesh-sd/carts/astrowin.bme` | bm Mesh under luahost: Astro Wing models from the game's meshes |

SHA-256 at copy time: `village_models.bm 696fd1b2…`, `anim_figure.bm ae8faf77…`,
`studio_blocks.bme cda056d6…`, `studio_copy3d.bme f1cdd674…`, `mesh_village.bme ecfcd3e4…`,
`mesh_astrowin.bme 2a78a7f4…`.

## What the luahost files are and are not

The MESH and ANIM bytes are the real tools' (`bm3d.lua` encoders, bm Mesh's own). The
**container** around them is the test stand-in `pack_cart` of
`tests/studio/tools3d_host.lua:49-66`, not the kernel's `bm_rewrite_with`: it writes flags 0
(no project bit in a `.bme`) and 640x360 for anything that is not 320x180. So those four
`.bme` fail `bm_core.check_path` (asserted in `test_schema.py`), while `parse` accepts them.
No golden comes from the kernel's own writer (bmhost has no `--arg`, QEMU is not installed):
see `references/OPEN_QUESTIONS.md` items 19-22.

## Tests here

- `test_roundtrip.py`: parse -> build byte-identical for every golden; MESH and ANIM
  decode -> encode byte-identical; JSON `to_dict`/`from_dict` round trip; a rewrite equals
  `scripts/bmmesh.py:rewrite_cart`.
- `test_schema.py`: header, sections, models, rigs of each golden; edition/path checks.
- `test_negative.py`: truncated, bad magic/CRC/version/resolution, section out of bounds or
  over the table, duplicate sections, 65 bones, 256 clips, 1025 keys, vertex mismatch, rig
  without model, duplicate names, oversized models, bad floats, editions never converted,
  BMRES kinds. All assert `ValidationError`.
- `test_crosscheck.py`: bm_core decoding == `scripts/bmmesh.py` decoding on every golden
  (village included); bm_core encode == bmmesh encode; glb port == `bmmesh.glb_models`.
- `test_syncwatch.py`: the watched sources still match `bm_core/SYNC.json`.
- `test_conformance_slow.py`: skipped unless `BM_FORGE_SLOW=1` and `qemu-system-arm`.
