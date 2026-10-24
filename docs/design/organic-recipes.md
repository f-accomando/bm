# Organic character recipes for the assistant: plan

> Written 2026-10-15. Plan by `bm-planner` (read-only, from the code), updated with what the same session learnt:
> [`bm-forge` characters](../../.claude/skills/bm-forge/references/CHARACTERS.md),
> [detail and reduction](../../.claude/skills/bm-forge/references/DETAIL.md), the Animator test pose (`t`).
> Status: **planned, not started.** Nothing here changes the kernel until a Work Order below is run.

## Goal

The assistant (`src/ai`, F6 in every tool) builds 3D models from separate primitives (`mesh_script.c`: box, tube, ell,
prism, one bone per group of faces). It cannot make a **continuous skin**: limbs, ears, tail and fingers that grow
out of vertices shared with the body, so a bent joint stretches instead of opening a gap. Add recipes that do:
`biped` (organic person, 15 bones) and `chameleon` (a biped with a tail, 18 bones), with idle, walk and wave.

## What the code allows (planner, with file:line)

- `mesh_face_t` keeps `p[4][3]` and `b[4]`: **a bone per corner already exists** (`src/ai/mesh.h:25-30`).
- Vertex welding by position and bone already happens on export: `T.encode_mesh` (`src/script/bm3d.lua:148-192`,
  `pkey` at 1e-4; corners of different bones stay apart). Positions are snapped to a 1/256 grid by `mesh_finish`
  (`mesh.c:1138,1162-1167`).
- The only blocker is `face()` (`mesh.c:152-175`): it gives every corner `c->bone`. A quad between two rings with
  different bones cannot be written. `mirror_x` already respects the bone of each corner (`mesh.c:388`).
- `img2mesh` has its own SPEC (`tools/img2mesh.py:45`): extending the script language would not change it by itself.
  And a `loft` that refers to rings is what a language model writes badly; a table of numbers is more reliable.
- Limits: 2048 faces, 40 bones, 4 clips, 6 keys a clip (`mesh.h:19-22`); extra faces are silently dropped
  (`mesh.c:155`). The RGB30 kernel does not build `mesh*.c` or `lua_ai.c` (`rgb30.mk:87-90`): no impact, no new divergence.

## Recommendation

**A C recipe on top of a small reusable skin kit.** Port the `Skin` of `examples/charkit/charkit.py` (vertex table,
rings, bridge, cap, tent, align) to `src/ai/mesh_skin.c`, emit faces through a new `face_b()` that takes a bone per
corner, and write `r_biped` and `r_chameleon` with fixed numbers. Rule for the bone of a shared ring: the **distal**
bone. The script language is not extended for now (WO-7).

Not to do: change the output of `face()` or `biped_bones`/`r_hero` (Overbit uses `bone3d`);
port charkit's Python into the kernel; build +z then flip (build facing -z directly); name Rango or the sheriff in code
or in the KB; put a vertex array on the stack (Pi Zero: `static` only).

## Work Orders

Order: WO-0 -> WO-1 -> WO-2 -> WO-3 -> WO-4 -> WO-5a -> WO-5b -> WO-6; WO-7 only if decided. Branch for the work:
`claude/organic-recipes`, from `bm-core`. Tests are the touched ones only (`make test-ai`, `build/host/meshview`).

| WO | Goal | Files | Verify | Agent |
|---|---|---|---|---|
| 0 | Snapshot of every recipe's output and of the training before any change | none | the 53 recipes dumped with `meshview json`; baseline of `trainassist.py` kept | bm-ops |
| 1 | `face_b`: a bone for each corner of a face | `src/ai/mesh.c`, `mesh_int.h` | the dumps of all recipes are **identical** to WO-0 | bm-coder |
| 2 | Skin test | `tests/ai/test_ai.c` (`test_skins`) | one bone per position; the hips component reaches head, hands, feet (and `tail3`); at most 640 triangles, 512 distinct positions; skips when the recipe is absent | bm-coder |
| 3 | Skin kit and `biped` recipe, 15 bones, clips idle walk wave | `src/ai/mesh_skin.c`, `mesh_int.h`, `mesh.c` `recipes[]`, `Makefile` (`AI_SRCS`) | `make test-ai`; `meshview one biped` prints 15 bones, 3 clips | bm-light |
| 4 | Tail and `chameleon`, 18 bones (`tail1..3`, 2 rings of 6 sides each) | `mesh_skin.c`, `mesh.c` | `make test-ai` (checks `tail3`); 18 bones, 3 clips | bm-light |
| 5a | KB entries `mesh.biped`, `mesh.chameleon`, `tip.skinning` and six held-out questions | `src/ai/kb/meshes.txt`, `tips.txt`, `tests.txt` | `make build/assist.bin` | bm-mechanic |
| 5b | Retrain, compare with the baseline | `src/ai/assist.weights` | `make test-ai`; `held-out first three >= 90%`; commit the weights | bm-ops |
| 6 | Docs | `docs/progress/tools.md` (Open work, Rules), `tools-assistant.md`, `CHARACTERS.md` | `docs_audit.py score`, `consistency.py check` clean | bm-mechanic |
| 7 | (deferred) `skin` instructions in the part language for `ai.script` and `img2mesh` | `mesh_script.c`, SPEC, `docs/api/tools.md`, replays in `tests/ai/img2mesh/` | back to bm-planner | - |

Keywords of the KB entries: never `coda` (it means "queue" in the audio and 3D API), never `scheletro` (it is the
enemy `mesh.skeleton`).

## Decided

- **Retraining**: accepted. The last one cost about 11 old held-out questions (first-place 77.3%, first three 92.6%);
  do it once, at WO-5b, and compare with the WO-0 baseline.
- **Presets**: the Crash and Rango specs in `examples/charkit/presets/` stay (they are the templates).

## Still to decide

1. **Triangle ceiling of a recipe**: the planner set 640 for the skin test (kernel memory: 512 distinct positions, a
   `static` table of about 6.5 KB). The charkit Crash is 750 triangles and the detail test shows 1200 holds a 1.8 m figure
   at 0.46 cm. Proposal: keep 640 for `biped` (a low recipe) and say so in the KB; revisit if the table grows.
2. **Names**: `biped` and `chameleon`; the body numbers come from the charkit spec, with no hat, no sheriff, no
   third-party look.
3. **Tail**: fixed numbers (length 1.0, tip radius a quarter of the root, a curl of 270 degrees, no sway); the seed
   changes only the colours. Words such as "long tail" in `mesh_parse` would be another Work Order.

## What the later work adds to the acceptance of WO-3 and WO-4

- The recipes' skin must pass Animator's test pose: with `t` on the rig page, few faces marked red (the charkit skin:
  58 of 1334 on the mannequin, against 126 for smooth auto skin and 416 for rigid).
- Generalise the shoulder patch and the pelvis arc to a variable number of sides as `examples/mannequin/mannequin.py`
  does (column of `4 * round(4 * detail)` sides, an odd patch width, legs from the two half-rings): the same code can
  give several resolutions of one recipe.
- Known cosmetic defect to avoid: thin sliver triangles where the pelvis half-ring meets the thigh at high detail.

## Risks

- A retraining reshuffles the whole network: new words ("personaggio", "organico") can steal questions from
  `mesh.hero`, `mesh.dog` or the audio entries. Measured by the diff of the "not first" lines before and after.
- Memory: `mesh_model_t` is about 152 KB, allocated and freed per call (`lua_ai.c:428`): unchanged. The kit adds about
  6.5 KB of BSS and 5 KB of code to the Pi kernel.
- Bones: 18 of `MESH_MAX_BONES` 40 (the 3D engine takes 64); clips 3 of 4.

## NON VERIFICATO

That the build directory is `build/`; that `meshview sheet` takes any `.ppm` and `meshview json` works on all 53
recipes; the exact line of `test_meshes` in `test_ai.c`; the shape of `tail` and `bones` in the presets beyond the lines
read; that none of these models opens in bm Mesh or bm Animator on the console; that `consistency.py` does not track
the recipe ids.
