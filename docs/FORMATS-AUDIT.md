# Audit of the formats: `.bm`, `.b16` and the resource files

State as of 2026-10-13 (branch `pdf-manuals`). Nothing here has been changed: **every proposal waits for a
decision** ([open-decisions.md](open-decisions.md)). The format as it is: [BM-FORMAT.md](BM-FORMAT.md). The
structure of the programs: [APP-ARCHITECTURE.md](APP-ARCHITECTURE.md). Each proposal says whether it **breaks**
old kernels, old tools or existing cartridges. Evidence is `file:line` at the time of the audit.

> **Update 2026-10-13:** decided and done: F1 (header comments of `bm.h`), F2(b) (720x720 accepted), the bm_rewrite width bug (480x270 saved as 640x360), B1 (`B16.md` status), B2, B3(a), R1's `extract` fix, S1a. See [open-decisions.md](open-decisions.md).

## 1. What is solid

- One container, parsed in one place (`format.c`), with a CRC, exact-size checks on every section, limits on the
  important fields, unknown sections ignored and kept by the tools. The Python tools (`bmres.py check`) are stricter.
- The edition rules are kept by the build: a `.b16` never replaces a `.bm` (separate slot in `project.c`, separate
  catalog entries in `market.c` and `mkmarket.py`).
- The resource files and INFO already carry author, licence (SPDX), version and origin: the design for reuse exists.

## 2. The `.bm` container

### Inconsistencies

| # | Side A | Side B |
|---|---|---|
| 1 | `bm.h:14,18` and `RISORSE.md:126,130`: offsets 18 and 104 "reserved (0)" | `project.h:15-18`, `project.c:130-143`: bit 0 project, bit 1 `.b16`, built-from name |
| 2 | `mkbm.py:57` and `PROJECT-LAYOUT.md:27`, `B16.md:24`: 720x720 allowed | `format.c:562-565`: the kernel refuses 720x720 (only 5 modes) |
| 3 | `bm.h:25-26`: cover 128x80 | `bm.h:170-173`, `mkbm.py`: 88x88 |
| 4 | `bm.h:41`: mesh names "unique" | `format.c:149`: only "not empty" |
| 5 | `mkbm.py:65`, `bmres.py:271`: INFO over 16 KB is an error | `format.c:588-592`: silently dropped in a cartridge |
| 6 | `bmres.py check`: refuses broken INFO/SPRITES/BOXES and duplicate sections | the kernel plays such a cartridge (drops them; the last duplicate wins) |
| 7 | "is it a sound bank" decided four ways: `format.c:678` (size >= 16), `runtime.c:5284` and `6730` (>= 4), `bmres.py:161`, `bmmesh.py:188` (no size test) | a 4..15 byte blob is audio for the tools and an error for the kernel |
| 8 | `mkbm.py:436,475`: title cut at 47 bytes, can end inside a UTF-8 character | `bmres.py _fixed` avoids it |
| 9 | `bm_rewrite_with` (`format.c`): drops flags and built-from, keeps at most 32 old sections silently, does not map legacy types 6/7 | `cart_put_audio` (`runtime.c:6821`) does map them |
| 10 | `bm.h:109`: leniency documented for BOXES only | in fact INFO, SPRITES, BOXES are lenient; LAYERS, FLAGS, MESH, ANIM are fatal |

### Gaps and risks

- **G1 No versioning of the section layouts.** One global version, hard fail. Using a "reserved" field in a record
  makes an old kernel refuse the cartridge when it checks an exact size.
- **G2 The CRC does not cover the header** (`format.c:543`): resolution, flags, built-from, title, author and the
  section count can change without detection.
- **G3 No per-section CRC**; the menu reads the cover with no CRC.
- **G4 The section table is not checked**: offsets can point into the table, overlap or alias; memory safety holds
  because every read is bounded (the direct pointer casts were not audited: UNVERIFIED).
- **G5 Duplicate sections** behave differently in the kernel (last wins), in `bm_rewrite` (first wins for LUA) and in the
  tools (refused).
- **G6 No "essential" flag** for unknown sections: a future section that changes the meaning of the file would be
  silently ignored by an old kernel.
- **G7 Six writers** of the header and the table (3 C, 3 Python): any header change goes in six places.
- **G8 Reserved bytes are erased** by "Make editable copy" and "Build" (`project.c:143,178,225`): a future use of 120..127
  or of other flag bits would be lost unless those functions are updated.

### Options

| Id | Proposal | Pros | Cons | Breaks? |
|---|---|---|---|---|
| F1 | Document the flags (18) and built-from (104) in `bm.h` and `RISORSE.md`; say writers must keep bits 2..15 | zero risk; closes inconsistency 1 | none | no |
| F2 | 720x720: **(a)** remove it from `mkbm.py` and the docs, **(b)** accept it in `format.c` and the runtime | (a) trivial, honest now; (b) matches `B16.md` | (b) runtime work | (a) no; (b) old kernels refuse the new files |
| F3 | Header integrity: **(a)** a second CRC of the header in bytes 120..127, checked only when nonzero; **(b)** extend the CRC to the header | (a) old kernels ignore it | (a) `project.c` must recompute it at every patch; (b) simpler to explain | (a) no; (b) **yes**: old kernels and tools reject new files |
| F4 | Table sanity (offset after the table, no overlap) and duplicate sections: **(a)** refuse in the kernel, **(b)** warn in `mkbm.py` and `bmres.py` | (a) closes G4, G5 | (a) could refuse a hand-made file; the unit tests build files by hand | (a) only malformed files; (b) no |
| F5 | Version a section layout with the table's reserved `u32` (written 0 today) | old kernels already ignore it | an old kernel cannot tell "version 2 of this section"; only a new reader benefits. Today's practice, new type numbers for new layouts, works too | no |
| F6 | One C writer in `format.c` used by `cart_save`, `new_pack`, `cart_put_audio`, and one Python writer (`bmres.dump`) used by `mkbm.py`/`bmmesh.py` | one place to change | the biggest diff; risk to the menu/GPU tests (`test_bm.c`, `test_bmres.py` check bytes) | no, if the bytes stay equal |
| F7 | `bm_rewrite_with`: keep flags and built-from, error (not silent) over the section limit; one sound-bank predicate; the title cut with `_fixed` | cheap fixes of 8, 9, 7 | none | no |
| F8 | Write the leniency table in `bm.h`; give `bmres.py check` a lenient mode like the kernel | closes 6, 10 | none | no |
| F9 | Mark "essential" sections with bit 31 of the type | future-proofing | helps only kernels that know it | no |
| F10 | Stop emitting the legacy types 6/7 for ever; convert once, drop the "BMAU" sniffing after a deprecation | removes the ambiguity | legacy bm Studio files stop loading | **yes**, for legacy files |

**Suggested order:** F1, F7, F8 now (no risk); then F2(a) and F4(b); F3(a), F5, F6 when a header change is needed
anyway; F4(a), F10 only with a scan of the shipped cartridges.

## 3. The `.b16` edition

**Headline: the `.b16` profile is a label, not a runtime.** The flag `BM_FLAG_B16` is written by the tools and
never read: the edition is decided by the extension (`carts.c:107`, `ui.c:250`, `market.c:233`). A `.b16` runs in the
same Lua environment, with the same limits, as a `.bm`; the code says so (`src/rgb30/ui.c:380`: "its profile's limits
are not applied yet"; `scripts/mkmarket.py:121-125`).

| Rule in `B16.md` | Status today |
|---|---|
| Same container, `.b16` extension, bit 1 | marking only (written, never read) |
| New container `B16CART` (256-byte header, section table of §3) | only designed, contradicted by the real container |
| 360x360 or 720x720 fixed, no `screen()` | partly: the tools force the header; `screen()` still exists; 720 refused (F2) |
| Palette of 256 colours / `SHEET8` | not checked for `.b16`; no `PAL` section exists |
| Lua heap 4 MB | **contradicted**: 64 MB for every cartridge (`luavm.c:13`) |
| Save 64 KB | **contradicted**: 32 KB (`runtime.c:3300`); `B16.md:529` says saves are not designed |
| Budget 1 M instructions, 60 -> 30 fps drop | **contradicted/missing**: 20 M per callback for every cartridge; no throttle. Decided: promise dropped (D4) |
| Sandbox allow-list built from empty; no `math.random`, `time`, files, `screen`, `require` | **contradicted**: a deny list (only `dofile loadfile load` removed); `math`, `time`, `cart_*`, `ls` stay (`runtime.c:4809-4822`) |
| Graphic banks 1024x1024, `gfxbank` | only designed; Yharnam's sheet is 4096x3392 |
| File cap 8 MB | enforced when **building** (`mkbm.py:600`, `project.c:203`, Makefile, `mkmarket.py`), not when the console loads |
| Working folder `dev/<name>/`, `suspend` | only designed; the real flow is `.bme` projects |

How a `.b16` is really made: `mkbm.py --b16`/`--from X --b16`; the SDK's "Export .b16" (`bm_build_b16`); Overbit has its own
rule (minified Lua, header 360x360); **Yharnam and Titan Clash EX `.b16` are made with
`mkbm.py --from X.bm --b16`** (`B16_AS_BM`, Makefile:370): done (B2). The RGB30 lists `.bm` files too by
default (`show_bm`, `ui.c:238`), against "only `.b16`" in the docs.

Documentation inconsistencies: `B16.md:5-7` "not yet implemented" against `progress/b16.md` "done" and the tools above;
`B16.md:3` links `B16-IT.md`, which does not exist; the `RGB30.md` text says the menu lists but does not launch `.b16`,
`ui.c` launches them; the SDK lint says 3D waits for the profile (`carts/editor/main.lua:803`) while Overbit has a
`.b16`.

### Options

| Id | Proposal | Pros | Cons | Breaks? |
|---|---|---|---|---|
| B1 | Rewrite `B16.md` §0 to say what is true: marking + 8 MB + 360x360 target; move the `B16CART`, banks, `dev/` design to a "future design" part; fix the numbers (4 MB, 64 KB, 1 M); create `B16-IT.md` or remove the link | honest spec at zero risk | none | no |
| B2 | Build Yharnam and Titan EX with `mkbm.py --from X.bm --b16` instead of `cp`, so the flag is true (or decide the extension alone is authoritative and drop the flag from the docs) | trivial; makes the flag usable | the `.b16` bytes now differ from the `.bm`; the Market version changes once | no |
| B3 | What a `.b16` promises: **(a)** a *target* only (screen 360x360, 8 MB): nothing else; **(b)** a **packaging lint** in `mkbm.py`/`mkmarket.py`/the SDK that refuses or warns on forbidden APIs (the SDK already lists them: `editor/main.lua:792-830`); **(c)** runtime enforcement keyed on the flag in `new_cart_state` (`runtime.c:4802`): no `math.random`, `cart_*`, `ls`, `screen`, a lower heap and budget | (a) nothing to build; (b) no runtime change; (c) is the real profile, same behaviour on Pi and RGB30 | (a) the "identical on both consoles" promise is not made; (b) a text scan is approximate and does not stop hand-made files; (c) needs B2 first and may break published `.b16` (Yharnam) | (a) no; (b) no; (c) **yes** for any `.b16` that uses them |
| B4 | Check size (8 MB), resolution and the flag when the console **loads** a `.b16` (`B16.md` §7 asks for it), with a message | small | blocks hand-copied oversize files | oversize files only |
| B5 | `show_bm` default 0 on the RGB30 release builds | matches the doc | breaks the testers' workflow | no |
| B6 | Drop the claim of 30 fps throttle and CPU % (keep the dev kit's measures) or build a deterministic frame-cost meter | honest or complete | the meter needs tuning on the Pi; 3D/GPU time is not deterministic | no |

**Suggested order:** B1 and B2 now; then choose B3 (the honest minimum is **a + b**).

## 4. Resource files and the exchange of assets

State: the exchange works **on the PC only** (`bmres.py extract` / `add` / `convert`). The kernel's Lib tab reads and
previews all six kinds (hidden unless `lib_tab=1`); no console editor opens or saves a resource file; the only exchange on
the console is bm Sound's "Import from..." (cartridges and packs, no INFO). `RISORSE.md` is a proposal of 2026-10-03:
the Lib options (Copy into a project, Save to `/bm/lib`, Tags, Rename, Delete) and the editors opening `.bmm/.bmi/.bmk`
are **not built**; "bm Sound saves `.bms` in `/bm/lib`" and "`mkbm.py --res FILE`" are not true.

| Producer / consumer | `.bmm` | `.bmi` | `.bms` | `.bmt` | `.bmc` | `.bmk` |
|---|---|---|---|---|---|---|
| `bmres.py extract` / `add` / `convert` | yes | yes | yes | yes (one map per game) | yes | extract; add not tested |
| `mkbm.py` | no (`--models` takes `.glb` or a `.bm`) | no | no | no | no | no |
| kernel Lib tab | yes | yes | yes (Y plays) | yes | yes | lists the parts |
| `bm-forge` | writes `.bmm` (colour only, no INFO) | check | check | check | check | check |
| bm Sound | - | - | only from `.bm`/packs | - | - | - |
| bm SDK, Studio, Animator, Pixel, Mesh | designed | designed | designed | designed | designed | designed |

**Provenance and licence.** INFO carries `author license version tags origin`. `add` copies the file-level INFO of the
resource into a block per part and sets `origin` = SHA-256 of the resource file. **Lost:** `extract` copies only the
per-part blocks, so the source cartridge's file-level `license`, `version`, `tags` are dropped; bm Sound's import and
bm-forge's `.bmm` carry no INFO; the origin is the hash of a file version (re-saving INFO changes it), and a second hop
overwrites it. **Nothing checks a licence**: not `mkbm.py`, not the Market (`mkmarket.py` requires only a `license` in
`info.txt` for the whole game).

**What is missing for reuse:** a dependency list (`res:` in `bm.info` is only approved in principle); versioning
(nothing compares `version`); conflicts (`add` renames duplicate model/zone/sound names, so game code using the old name
breaks silently; tiles go to free cells and are remapped; flags are copied into the cell and can overwrite); palette
mismatch (more than 256 colours fails only for `.bmc`); the `.b16` limits (no 256-colour or 8 MB check in `add`); the
Market cannot carry resource files.

### Options

| Id | Proposal | Pros | Cons | Breaks? |
|---|---|---|---|---|
| R1 | **Finish copy-on-import** (today's design): `extract` keeps the file-level INFO; the Lib options and the editors open `.bmm/.bmi/.bmk`; bm Sound writes provenance | no format change; one file per game; tested code (`test_bmres.py`) | duplicates data; no update path; licence carried, not checked; names may be renamed under the game | no |
| R2 | **Declarative `res:`** in `bm.info`: `res: res/knight.bmm sha256=... license=CC-BY-4.0`; `mkbm.py` (or a wrapper) checks hashes, integrates in order, refuses licences not on a policy table, fails over 256 colours or 8 MB with `--b16` | reproducible builds; licence check; plain text; the `res/` folder is already in the layout | `mkbm.py` has no integration code and `bmres.py` imports `mkbm.py` (needs a third module); someone must write the licence table; the `.bm` stays a copy; name conflicts still need a rule (rename or error) | no (text file; old kernels ignore INFO) |
| R3 | **Runtime loading** of resource files (`res_load`) | one copy on the card; updates reach every game; smaller cartridges | contradicts rule 7 of `RISORSE.md` (a cartridge stays one file); needs a new API and a path past the sandbox; tile numbers, palette and corners solved at load time; Market games would depend on files that may be missing; two editions need two resource sets | **yes** (old kernels, Market) |

**Suggested order:** R1 (including the small fix in `extract`), then specify R2 together with the licence policy; leave R3.

## 5. Summary of what needs a decision

1. 720x720: remove from the tools (F2a) or accept in the kernel (F2b).
2. What a `.b16` promises: target only, plus packaging lint, or runtime enforcement (B3).
3. Header integrity: second CRC (F3a), full CRC (F3b, breaking) or leave.
4. Resource exchange: finish R1 first, then R2 with a licence policy table. Who owns the table?
5. The `.b16` edition rule for Yharnam/Titan EX: really two editions (B2) or one with a rename.
