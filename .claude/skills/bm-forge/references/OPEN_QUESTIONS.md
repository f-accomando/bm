# bm-forge: open questions met while building bm_core (phase 1a)

Each item: the doubt, what bm_core does now (the safest choice, usually a strict
refusal), and where it is in code. Items closed by the decisions of phase 1a
(15-byte names, tool limits 4096/16384, material bits and `lit` kept, duplicate
names and sections refused, ANIM omitted with 0 rigs, zero padding, original
header kept, `.bm`/`.b16` never converted, no profile field) are not repeated.

## Format: reading stricter than the kernel

1. **16-byte names** (bm3d.lua writes them without NUL, `src/script/bm3d.lua:135-138`):
   read, but a write refuses them unless `keep_long_names=True` (`bm_core/binary.py`
   `name_bytes`). Should bm-forge ever rewrite them to 15 bytes (renaming a model breaks
   the rig link by name)?
2. **UTF-8**: names and INFO must be valid UTF-8, or the file is refused (C never
   checks). Title/author are decoded with replacement and never refused (mkbm cuts the
   title at 47 bytes and may split a character, `scripts/mkbm.py:413`).
3. **Bytes after the NUL** of a char[16] name must be zero, or refused (no writer seen
   leaves garbage; a non-zero tail would not round-trip).
4. **Reserved fields** non-zero are refused on read: MESH head u32, ANIM head u16+u32,
   rig head u16, bone u16. C ignores them. NON VERIFICATO that no tool writes there (no
   golden does).
5. **Broken INFO / SPRITES / BOXES in a cartridge**: refused (C drops them and plays,
   `src/bm/format.c:575-607`).
6. **A section overlapping the header or the table**: refused (C only checks
   `offset+size <= len`, `src/bm/format.c:568`).
7. **A rig with no MESH model of its name**: refused. The kernel only skips it
   (`src/bm/runtime.c:2161`). NON VERIFICATO whether a rig may legitimately move a mesh
   built in Lua code (`mesh()`), which would make this refusal wrong.
8. **Rig vertex count != model vertex count**: refused (kernel skips the rig).
9. **Empty bone or clip names**: refused (C does not check them).

## Format: kept as-is, meaning unknown

10. Header bits 1..15 of the u16 at 18, bytes 120..127, bytes after the title's NUL:
    kept byte for byte, never refused, never interpreted.
11. Clip flags bits 1..7 (only bit 0 = loop is documented): kept.
12. Face u16 at offset 6 (`extra`) in a model that is not lit: kept, no meaning.
13. Quaternions are not checked to be unit length (C does not either).
14. AUDIO bank: only the `BMAU` magic and size >= 16 are checked; the bank itself
    (`scripts/bmaudio.py` unpack, `src/audio/player.h:169` au_parse) is not ported.
15. `bm_rewrite_with` keeps at most 32 sections (`src/bm/format.c:879`); bm_core allows
    255 (the u8 count). Should the writer cap at 32 so the console can re-save?

## Editions and paths (`bm_core/container.py` `check_path`)

16. **360x360 is treated as the `.b16` edition**: a `.bm` at 360x360 is refused, a `.b16`
    must be 360x360. But `mkbm.py --res 360x360` makes a `.bm` (`scripts/mkbm.py:443`)
    and the parser accepts 360x360 for any cartridge. Is a 360x360 `.bm` legitimate?
17. **BMRES extension vs kind**: a mismatch is refused; C lets the kind win
    (`docs/RISORSE.md:133-134`).
18. 720x720 for `.b16` (`docs/B16.md:24`) is still refused, as by the parser.

## Golden files and tools

19. **The luahost container is a stand-in** (`tests/studio/tools3d_host.lua:49-66`
    `pack_cart`): it never sets the project bit of a `.bme` (the kernel sets it,
    `src/bm/runtime.c:6467-6468`) and writes 640x360 for anything that is not 320x180
    (`studio_copy3d.bme` is the 320x180 village saved as 640x360). So the four luahost
    goldens fail `check_path` while their bytes are valid. Their MESH/ANIM bytes are the
    real tools' (`bm3d.lua`, bm Mesh); the container is not the kernel's
    `bm_rewrite_with`. A bmhost `--arg` run would give real containers (TOOLS.md §3.2).
20. **`make test-studio` fails at 287d05f**: `build/host/test_meshcap` does not link
    (`undefined reference to g16_to_rgb24` from `src/bm/r3d.c`, rule `Makefile:1092`).
    The goldens were made by running the luahost steps of the rule by hand (see
    `tests/golden/README.md`). Needs a fix in the Makefile (outside this skill).
21. `build/host/test_bm` is not a generic file checker (fixed argument positions,
    `tests/bm/test_bm.c:876-884`; it crashed on other files): the kernel's C parser was
    not run on the goldens here, only through `make test-studio`'s own steps.
22. QEMU is not installed: `tests/golden/test_conformance_slow.py` is wired to
    `qemu_test.py -k test_models / test_animation` but has never run here, and it does
    not yet feed a bm_core-written file to the kernel.

## glb import (`bm_core/glb.py`)

23. Names are cut to 15 **characters** like `bmmesh.glb_models`; a multi-byte name can
    still exceed 15 bytes and is then refused at encode time.
24. Textured `.glb` without a bm Studio sheet (all `carts/kitchen/models/chef*.glb`) is
    refused, as by bmmesh. A Meshy texture import path is `tools/meshy2mesh.py`, not ported.

## Preview and playback (`bm_core/preview.py`, `bm_core/skin.py`, phase 1b)

Playback check, `skin.py` against `src/bm/runtime.c:2198-2321` and bm Animator
(`carts/animator/main.lua:772-812` `ease`/`sample`, `Q.slerp` `src/script/bm3d.lua:112-125`):
same wrap/clamp, same key search, same smoothstep, same slerp (0.9995 threshold, shorter
way, normalised), same `M = M_parent T(head+t) R(q) T(-head)`. bm Animator draws through
the kernel's `animate()` (`carts/animator/main.lua:223-225`), so what it shows is the C
maths by construction. Differences found (none changes a pose read from a valid file):

25. Lua `sample` fills missing poses with identity and uses `pb = b.pose[i] or pa`
    (`main.lua:784-786,806-807`); C always reads `nbones` poses per key (the format
    forces them). bm_core refuses a key without one pose per bone (`skin.check_link`).
26. Lua works in doubles, C in f32; `skin.py` in doubles: poses agree to ~1e-6, not
    bit for bit (NON VERIFICATO against a kernel run: no bmhost/QEMU here).
27. bm Animator's info line wraps time with `max(length, 1e-3)` (`main.lua:267-268`),
    `pose_at` with `length > 0 ? length : 1` (`runtime.c:2232`): display only.
28. `bone_turn()` (`runtime.c:2291-2294`) is runtime state, not file data: not modelled.
29. r3d lights a skinned face with the normal turned by the bone of its **first**
    vertex only (`src/bm/r3d.c:2171`); the preview does the same, so a face spanning two
    bones is lit as if it followed one.

Renderer approximations (each also in the `preview.py` docstring):

30. **Flat only.** The console draws Gouraud when a game asks (`R3D_SMOOTH`,
    `r3d.c:1906,2136`); lit models' corner lights are averaged per face. Lamps, fog,
    rim, GLOSSY specular, r3d's dither and fixed-point texel stepping are not drawn.
31. **Default LOD level 3** (`detail` argument): which level a game draws at depends on
    distance and settings (NON VERIFICATO); faces with LOD bits may differ.
32. **SCREEN material phase** `(x + y) & 1`: NON VERIFICATO against r3d's pattern.
33. **Textured face of a lit model**: drawn with its 5-6-5 light and no sun
    (`r3d.c:2170` skips the sun; the rest of the textured lit path past `r3d.c:2219` was
    not read: NON VERIFICATO).
34. **No sheet**: textured faces are flat grey `0xA0A0A0`; the runtime clears their RGB
    (`runtime.c:2140`), so the stored RGB bits mean nothing to draw.
35. **Camera**: the preview turns the model (yaw about y, then pitch about x) in front of
    a fixed camera and fits the camera to the bounding sphere; a game places its own
    camera (`r3d_camera`). Orthographic is a preview extra.
36. **Drift watch**: `SYNC.json` does not track `src/bm/runtime.c`, `src/bm/r3d.c` or
    `carts/animator/main.lua` (big files that change often; a whole-file hash would cry
    wolf). A change of `pose_at`/`skin`/`light_fast` is not detected: add line-range
    hashes to `syncwatch` if this matters.

## Skeletons and clips from descriptions (`bm_core/skelgen.py`, `animgen.py`, phase 2)

Nothing generated here was run on the kernel or opened in bm Animator: checks are bm_core's
(`parse`/`validate`/`check_path`) and its playback port (`skin.py`).

37. **One root only**: the format allows several bones with parent -1; skelgen refuses a
    second root as an orphan (bm Animator starts every skeleton from one root,
    `carts/animator/main.lua:389`, but its mirror can copy a root chain).
38. **Bone points** must be in (-1e6, 1e6) (MESH vertex range, `src/bm/format.c:157`) and
    bones at least 1e-3 long (`main.lua:423`); the ANIM check alone allows |v| < 1e9 and any
    length. Pose `move` uses the same 1e6 range; `q` must be unit within 1e-3 (the runtime
    normalises any q, `runtime.c:2201-2206`). All strict choices.
39. **Bones are reordered parents-first** (stable) because the format needs parent < index
    (`format.c:257-259`): bone indices may differ from the description's order.
40. **Rigid skinning only** (one u8 bone per vertex, `format.c:266-269`): no weights exist.
    Per-vertex `nearest_bone` splits a box at a joint (24 of the 104 humanoid vertices go to
    the wrong bone); `bind` defaults to `nearest_bone_part` (one bone per connected part, as
    the villager is made). A one-piece skin needs explicit `vbones`.
41. **Baking**: ease-in / ease-out, mixed curves, or smooth with bones filled between their
    own keys become one key per 12 fps frame, mode 0 linear (the format has one mode per
    clip, `format.c:277`). Exact on the 12 fps frames, linear between them at higher frame
    rates. NON VERIFICATO how usable such a clip is in bm Animator (a key on every frame).
42. **Missing bones in a key** take their own track's value (interpolated between the keys
    that name them); bm Animator instead stores a full sampled pose at each key it sets
    (`set_pose(c, sample(...))`, `main.lua:1012`) and fills new bones with identity
    (`main.lua:398-402`). Equivalent for linear/step; smooth is baked to stay exact.
43. **180 degree rule** is checked only on `rot` inputs (per axis, between consecutive keys
    of a bone, and across a loop's wrap): `q` and axis/angle inputs carry no intent to check.
44. **Euler order and facing**: `rot` = R = Rz Ry Rx (bm-forge's convention; bm Animator
    stores only quaternions). +x on a hanging limb swings it to -z, which is forward for the
    villager (eyes at z = -0.225, toes to -0.16 in `village_models.bm`); "characters face -z"
    is inferred from that one model, NON VERIFICATO as a project rule.
45. **12 fps grid** is an Animator rule (`main.lua:831`), not a format rule: the village
    walk has keys at 0.2 s steps (off the grid). animgen always snaps (times off the grid are
    listed in `report["snapped"]`); reading such files is unaffected.
46. **Sample cartridge** `tests/golden/preview_samples/forge_humanoid.bm`: LUA stub
    (`cls(0)` only), container by bm_core `build`, 320x180. To check on a console: open it in
    bm Animator (models are a `.bm` game, so saving asks for a copy) and play the clips.

## Export and the example game (phase 3)

47. **bmhost-bin may be stale vs bm-core**: a prebuilt bmhost used by the opt-in test may not
    match the current `src/bm/runtime.c`. NON VERIFICATO.
48. **B16 triangle cap unknown**: `TRI_BUDGET_B16 = 1200` is the Pi figure reused
    (`bm_core/export.py:21-22`; `docs/B16.md:28-49` only says "a triangle cap").
49. **RGB30 3D path**: how the RGB30 draws 3D, and whether `forgedemo`'s `.b16` runs there, is
    NON VERIFICATO (see API.md open question 3).
50. **`stat(4)` under bmhost** is logged by the demo, not checked: NON VERIFICATO what it reports.
51. **`cart_data` for games**: may a non-tool game call it? `l_cart_data` has no tool gate
    (`src/bm/runtime.c:6798-6928`); Market sandbox rules NON VERIFICATO. Export never uses it.
52. **Export never writes `.bme`**: `export_cart` refuses it (`export.py:191-192`),
    `export_market` refuses a folder holding `<id>.bme` (`export.py:357-360`). `make.txt`
    from `export_folder` is only a note (`export.py:287-298`); bm-forge never edits the Makefile.
53. **Market title/author uniqueness** is checked only inside the exported folder (the code
    in `export_market`, `export.py:311-402`, checks no uniqueness at all, only non-empty
    title); nothing compares with the Market repo. Whether the Market requires it is NON VERIFICATO.
