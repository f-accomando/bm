# bm-forge: lessons

Verified lessons only. Each has the lesson, the proving case, the rule.

## L1 Executors wrote outside the skill root

**Lesson**: a delegated executor can create files outside the skill folder.
**Case**: in phase 3 (2026-10-13) the executor of the demo build wrote `examples/` and
`tests/golden/` at the repo root; they showed up as untracked directories in `git status` and
were moved into the skill by the orchestrating session.
**Rule**: absolute paths in every work order; `tests/golden/test_cli_build.py::test_no_stray_files`
(line 269) checks that a full run creates only output files.

## L2 `make test-studio` does not complete

**Lesson**: the Makefile target cannot make the goldens.
**Case**: `test_meshcap` link error `undefined reference to g16_to_rgb24` at 287d05f
(`tests/golden/README.md` lines 11-12; `references/OPEN_QUESTIONS.md` item 20).
**Rule**: goldens come from the hand-run luahost steps listed in `tests/golden/README.md`.

## L3 Tool limit and kernel limit differ

**Lesson**: the Lua tools cap a model lower than the kernel.
**Case**: `src/script/bm3d.lua:34` has `T.LIMIT_V, T.LIMIT_T = 4096, 16384`; `src/bm/bm.h:175-176`
has `BM_MODEL_VERTS`/`BM_MODEL_FACES` 65535. `bm_core/mesh.py:23-24` holds both and `limits()`
(line 31) picks; `references/TOOLS.md:183-184` records the mismatch;
`tests/golden/test_negative.py:288-298` (`test_oversized_model`) asserts 4097 vertices fail by
default and pass with `kernel_limits=True`.
**Rule**: default to the tool limits; `--kernel-limits` only when asked.

## L4 The bmhost real-runtime run passed

**Lesson**: the example game runs in the repo's own runtime on the PC.
**Case**: `forgedemo: 7/7 checks passed`, exit 0, 420 frames, for `.bm` and `.b16` (`--square`),
2026-10-13 (`references/OPEN_QUESTIONS.md`, section "Verified in the real runtime").
**Rule**: it proves the PC runtime, not the console look (NON VERIFICATO); rebuild
`build/host/bmhost-bin` when bm-core moves.

## L5 Per-vertex `nearest_bone` splits box limbs

**Lesson**: rigid skinning (one bone per vertex) cuts boxes at joints.
**Case**: 24 of the 104 humanoid vertices land on the wrong bone (`references/OPEN_QUESTIONS.md`
item 40, `src/bm/format.c:266-269`).
**Rule**: `nearest_bone_part` by default (one bone per connected part).

## L6 luahost `.bme` goldens fail `check_path`

**Lesson**: the luahost container is a stand-in, not the kernel's writer.
**Case**: `pack_cart` in `tests/studio/tools3d_host.lua:49-66` writes flags 0 and 640x360 for
anything not 320x180; `tests/golden/test_schema.py:33,61-69` asserts the four `.bme` fail
`check_path`.
**Rule**: `check` exit 1 on those files is expected, not a bm_core bug.
