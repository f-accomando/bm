# bm-forge reference: the console's 3D tools, how they run, what can be reused

Facts about the repo as of branch `claude/bm-forge` (base commit `9cfc424`). Every claim
cites `file:line`; anything inferred is marked **NON VERIFICATO**. Paths are relative to
the repo root. Builds and tests run in WSL through `scripts/wsl-run.ps1` (skill `bm-wsl`,
`.claude/skills/bm-wsl/SKILL.md:9-17`).

## 1. Where the code lives

| Tool | Source | Built into | Shared code |
|---|---|---|---|
| bm Studio | `carts/studio/main.lua` (1799 lines), `sheet.png`, `icon.png` | `build/studio.bm` (`Makefile:152-155`) | `require "bm3d"` (`carts/studio/main.lua:11`) |
| bm Animator | `carts/animator/main.lua` (1577 lines) | `build/animator.bm` (`Makefile:157-159`) | `require "bm3d"` (`carts/animator/main.lua:10`) |
| bm Mesh | `carts/mesh/main.lua` (2440 lines) | `build/mesh.bm` (`Makefile:163-165`) | `bm3d`, plus `cart_meshes()` (`carts/mesh/main.lua:437`) |

- The three `.bm` are embedded in the kernel with `.incbin`, not shipped in the Market
  (`Makefile:100-105`; `docs/progress/tools.md:9`, "tools live in the kernel, not the Market").
- `src/script/bm3d.lua` (1745 lines) is the shared library: MESH (8) / ANIM (9) read and
  written with `string.pack`, project open/save with `cart_write`, the frame of the programs
  (`src/script/bm3d.lua:1-11`). Public encoders/decoders: `T.encode_mesh`
  (`bm3d.lua:148`), `T.encode_anim` (`:195`), `T.split_mesh` (`:224`), `T.split_anim`
  (`:238`), `T.decode_rig` (`:256`), `T.decode_model` (`:290`), `T.reduce_model` (`:345`),
  `T.load_project` (`:572`), `T.save_to` (`:618`), `T.save_project` (`:649`).
- A tool's model is a table of faces `f.p` (3 or 4 corners), `f.c` colour or nil = textured,
  `f.uv`, `f.b` bones per corner; quads become two triangles (`bm3d.lua:142`, `:166-187`).
- Mouse layer `src/script/bmui.lua`, assistant `src/ai/assist.lua` (both `require`d by
  `bm3d.lua:15-19`).
- Tool session in the kernel: `src/kernel/devtool.c` (`docs/progress/tools.md:23-26`);
  `cart_tool(name, path)` and `cart_run` only act in tool mode (`src/bm/runtime.c:6489-6532`).
- Rules of the tool pages (one `do … end`, under 200 locals, 16-px rows; bm Mesh rewrites
  only between `-- [bm Mesh begin]` / `-- [bm Mesh end]`): `docs/progress/tools.md:128-130`.

## 2. Build and run

- `make` builds `build/kernel.img` with the tools inside (`CLAUDE.md`, *Build, test, deploy*;
  `Makefile:100-105`). Single tool cart: `make build/studio.bm` / `build/animator.bm` /
  `build/mesh.bm` (`Makefile:152-165`), all via `scripts/mkbm.py`.
- On the console they open from the Dev tab or a game's options (*Open in bm Studio*);
  a file argument arrives as `cart_arg().path` (`src/bm/runtime.c:6534-6552`), read in
  `bm3d.lua:1616-1630`; without it the tool starts on its menu and file chooser
  (`bm3d.lua:1631-1634`).
- Projects (M47): tools write `.bme` only; bit 0 of the u16 at offset 18 marks a project
  (`src/bm/project.h:15`, `scripts/mkbm.py:418-423`, `CLAUDE.md:159`).

## 3. PC runners: luahost and bmhost

### 3.1 `luahost` (plain Lua 5.4 + test doubles) — headless today

- `build/host/luahost` = plain Lua 5.4 from `third_party/lua` (`tests/kitchen/luahost.c:1-8`,
  rule `Makefile:427-429`).
- `tests/studio/tools3d_host.lua` loads `carts/studio/main.lua` and
  `carts/animator/main.lua` into an environment `E` of stand-ins for the bm API
  (`tests/studio/tools3d_host.lua:197-551`), calls `E._init()` and then drives frames and
  keys (`:553-581`, `:642-643`, `:935-936`). Drawing is a no-op; text is captured
  (`:211-217`), so the "screen" is checkable as strings (`:559-563`).
- The SD card is a host folder (`:172`); `E.cart_write` rewrites sections like the kernel's
  (`:459-513`, packing header/CRC in Lua at `:49-66`) and runs the project rule from
  `tests/studio/project_rules.lua` (`:461`).
- **This is a working headless path to produce files from the real tool code**:
  `test-studio` already makes Studio save `BLOCKS.BME`, Animator add a skeleton and a clip,
  bm Mesh edit Astro Wing / the test kit, and checks the outputs with Python and the C parser
  (`Makefile:1237-1258`, `tests/studio/tools3d_host.lua:1-11`,
  `tests/studio/mesh_host.lua:1-10`).
- Limits of that path (fidelity of a golden file):
  - `E.mesh_reduce` is a stand-in that keeps the first N triangles, not the real decimator
    (`tools3d_host.lua:320-322`, `:370-395`).
  - `E.cart_write`'s header supports only 320x180 / 640x360 (`tools3d_host.lua:60-61`).
  - `picture3d` / `cutout3d` / `ai` are fakes (`:326-369`, `:520-540`).
  - So bytes written there are the **tool's** MESH/ANIM bytes (built by `bm3d.lua` itself,
    real) wrapped in a stand-in container. **NON VERIFICATO** that the container is
    byte-identical to `bm_rewrite_with` (`src/bm/bm.h:360-362`); `test_bm` and
    `check_files.py` only check it parses (`Makefile:1256-1258`).

### 3.2 `bmhost` (the console's real runtime on the PC)

- `tests/host/bmhost.c`: `src/bm/runtime.c` with kernel services stubbed
  (`tests/host/stubs.c`), virtual clock, PNG shots, raw video, WAV, input script
  (`tests/host/bmhost.c:1-41`). Sources linked: runtime, r3d, format, meshcap, decimate,
  cutout, glb, project… (`Makefile:584-605`). Target `make bmhost` →
  `build/host/bmhost-bin` (`Makefile:600-605`). Variants `bmhost-gpu` (V3D emulator,
  `Makefile:607-619`) and `bmhost-ai` (`Makefile:621-630`).
- Options: `--sd DIR`, `--seconds`, `--shots`, `--every`, `--from`, `--video`, `--wav`,
  `--input`, `--quiet`, `--tool`, `--realtime`, `--clock-scale`, `--square`
  (`tests/host/bmhost.c:404-424`). Input lines: `pad`, `stick`, `keys`, `type`, `shot`,
  `source`, `device`, `quit`, `ps` (`:30-40`).
- `--tool` = `bm_set_tool(1)` (`bmhost.c:421`): the cart may write files like a kernel tool
  (`src/bm/runtime.c:5280-5289`).
- Precedent of a tool run headless with real file writes: `test-write` runs
  `build/carts/write.bm` with `--sd` and an input script, then a Python checker reads
  `/docs` (`Makefile:513-519`).
- **Gap**: bmhost never calls `bm_set_arg` (only `bm_set_tool` appears in
  `tests/host/*.c`), although the runtime exports it (`src/bm/runtime.c:5253-5257`). So a
  tool started in bmhost has `cart_arg() == nil` and opens on its chooser
  (`bm3d.lua:1631-1634`); the file must be picked by typed keys. A `--arg PATH` option
  calling `bm_set_arg(PATH, NULL)` would be a few lines in `bmhost.c:404-424`
  (**NON VERIFICATO**: not tried).
- **NON VERIFICATO**: no test runs `studio.bm` / `animator.bm` / `mesh.bm` in bmhost today
  (no such rule in `Makefile`; the tool tests are all luahost, `Makefile:1237-1258`).
  With bmhost the bytes would come from the real `cart_write` → `bm_rewrite_with`, and
  frames could be rendered to PNG by the real r3d — the best candidate for true golden files.

### 3.3 Other host programs useful for checks

- `build/host/test_bm FILE.bm …`: the kernel parser on files (`Makefile:1131-1135`,
  `:1258`; `tests/bm/test_bm.c:1`).
- `build/host/test_meshcap src/bm/runtime.c CART name[:faces],…`: runs a cart's code with
  stand-ins for every API in `runtime.c`'s `api[]` and lists the meshes it builds; also
  checks each model's skeleton fits (`tests/bm/test_meshcap.c:1-13`, `Makefile:1143-1147`,
  `:1246-1247`).
- `build/host/test_decimate`, `libbmdecimate.so`, `libbmcutout.so` (the real C reducer and
  cutout as shared libraries: `Makefile:1125-1126`, `scripts/bmdecimate.py:1-9`,
  `scripts/bmcutout.py:1-9`).
- `build/host/meshview` (assistant 3D recipes → PPM, `Makefile:1175-1179`).

### 3.4 QEMU

- `tests/qemu_test.py --build build -k NAME` (`tests/qemu_test.py:7674-7680`),
  `--update-ref` rewrites references (`:7675`, `:410`). QEMU has no GPU, so 3D is software
  (`docs/progress/platforms.md:62-64`).
- Relevant tests: `test_studio_animator` (`tests/qemu_test.py:4079`),
  `test_studio_assistant` (`:4247`), `test_img2mesh` (`:4369`), `test_meshy2mesh` (`:4425`),
  `test_mesh_reduce` (`:4487`), `test_picture_model` (`:4565`), `test_mesh` (`:4670`),
  `test_textured_mesh` (`:6060`), `test_models` (`:6102`), `test_animation` (`:6169`),
  `test_sdk_keeps_models` (`:6200`), `test_projects` (`:7354`).
- `qemu-system-arm` is **not installed** in this WSL (`which` found nothing, checked
  2026-10-13); CI installs it (`.github/workflows/ci.yml:55`, `:85`).

## 4. Existing tests on mesh / anim / bm3d / cart format

| Make target | What | Where |
|---|---|---|
| `test-studio` | Studio, Animator, Mesh, Pixel, SDK under luahost; files re-read by `check_files.py`, `bmmesh.py`, `test_meshcap`, `test_bm`; `anim_figure.py` builds a rigged figure | `Makefile:1237-1258` |
| `test-bm` | parser, tokens, decimator, glb import, cutout, `bmreduce.py`, meshcap on Astro Wing / kitchen | `Makefile:1131-1147` |
| `test-res` | `BMRES` resource files (`.bmm` …) by `bmres.py` and C `test_res` | `Makefile:555-562` |
| `test-img2mesh` | `img2mesh.py` from recorded replies, `meshy2mesh.py` on a test .glb, `local2mesh` | `Makefile:1223-1232` |
| `test-ai` | assistant incl. `meshview sheet` of all 3D recipes | `Makefile:1212-1218` |
| `test-b3d` | the 3D Bench (the only GPU test to run in development, `CLAUDE.md`, *Come si lavora*) | `Makefile:1337` |

- `test-host` = `HOST_TESTS` minus `HOST_SKIP` (`Makefile:825-836`); `test-studio`,
  `test-bm`, `test-res`, `test-ai` are in it (`Makefile:825-828`).
- Rules list the tool tests: `docs/progress/tools.md:139-140`.
- Built host binaries already present in `build/host/` (luahost, bmhost-bin, bmhost-gpu,
  test_bm, test_meshcap, test_decimate, libbmdecimate.so, libbmcutout.so, meshview…;
  `ls build/host`, 2026-10-13).

## 5. Formats (for a Python core)

- Container `.bm`: 128-byte header, section table, CRC-32 of everything after the header
  (`src/bm/bm.h:1-20`); section types (`bm.h:21-109`, constants `bm.h:143-156`).
- MESH (8): u16 models (1..256), u16 inset (1/256 px), u32 0; per model name[16], u16 nv,
  u16 nf, u32 flags (bit 0 lit), verts f32×3, faces 24 bytes: u16 a,b,c, u16 reserved, u32
  colour (bit 31 textured, bits 24..30 material), u16 uv×6 in sheet px × 8
  (`bm.h:36-60`). Limits 65535 verts/faces, 256 models (`bm.h:174-178`).
- ANIM (9): per rig name[16], u16 bones (1..64), clips (0..255), verts, bones 44 bytes,
  u8 bone per vertex padded to 4, clips with keys of `4 + bones×28` bytes; bone transform
  `M = M_parent * T(head + t) * R(q) * T(-head)` (`bm.h:61-76`, limits `bm.h:179-183`).
- Visible face = clockwise vertices; glTF flips z and order (`CLAUDE.md:134-135`).
  `animate()` in `runtime.c` and the pose in `carts/animator/main.lua` must change together
  (`CLAUDE.md:135-136`).
- Resource files `BMRES` (`.bmm .bmi .bms .bmt .bmc .bmk`): `bm.h:113-131`, `docs/RISORSE.md`.
- `.bme` project flag: `bm.h` reserved u16 at 18, `src/bm/project.h:15`.

## 6. Python already reading/writing bm formats (reusable)

All standard library only unless noted.

| Script | Reusable functions | Cite |
|---|---|---|
| `scripts/bmmesh.py` | `encode(models, inset)`, `encode_faces`, `encode_anim(rigs)`, `decode_anim`, `decode`, `cart_sections`, `rewrite_cart`, `read_glb`, `glb_models`, `models_from_file`; limits constants | `scripts/bmmesh.py:1-27`, `:30`, `:100-212`, `:251-400` |
| `scripts/mkbm.py` | `pack(lua, sheet, map_, title, author, res, cover, …)`, `read_png`, `sheet8`, `sheet8_decode`, `make_cover`, `crc32`, `mark_project`; CLI `--models .glb|.bm`, `--audio`, `--res` | `scripts/mkbm.py:1-36`, `:55`, `:242`, `:307-423`, `:439` |
| `scripts/bmres.py` | `load`, `check`, `read`, `write`, INFO parse/dump, `extract` of models/sprites/sounds | `scripts/bmres.py:1-27`, `:133-326` |
| `scripts/bmaudio.py` | `pack(bank)`, `unpack(data)`, `extract(cart)`, WAV read/write (sound bank, 3 places to change together: `CLAUDE.md` *Invarianti*) | `scripts/bmaudio.py:1-10`, `:416-584` |
| `scripts/bmdecimate.py` | `reduce_record`, `reduce_model`, `reduce_cart` via ctypes on `build/host/libbmdecimate.so` (the console's reducer) | `scripts/bmdecimate.py:1-9`, `:41-64` |
| `scripts/bmcutout.py` | `cutout(data, name, lathe, …)` via ctypes on `libbmcutout.so` | `scripts/bmcutout.py:1-9`, `:44` |
| `tools/bmreduce.py` | CLI on top of `bmdecimate` (`--faces`, `--ratio`) | `tools/bmreduce.py:1-12` |
| `tools/bmrender.py` | pure-Python rasterizer: four views per model to PNG (preview without console) | `tools/bmrender.py:1-8`, `:34`, `:108` |
| `tools/meshy2mesh.py` | `glb_scene`, `convert(data, name, height, flat, max_tris, grid)`, `cluster` (glb → console model, reduced) | `tools/meshy2mesh.py:11-12`, `:203`, `:323`, `:421` |
| `tools/cutout2mesh.py`, `tools/img2mesh.py`, `tools/local2mesh.py` | image → model pipelines (img2mesh uses the Anthropic API; local2mesh needs PIL in its venv) | `tools/cutout2mesh.py:1-14`, `tools/img2mesh.py:1-10`, `tools/local2mesh.py:1-12`, `:42` |
| `tests/studio/check_files.py` | reads tool outputs back with `bmmesh` + `bmres` | `tests/studio/check_files.py:1-25` |
| `tests/studio/anim_figure.py` | builds a rigged figure `.bm` from Python | `tests/studio/anim_figure.py:1-10` |

- **Encoder mismatch to watch**: `bm3d.lua` still caps a model at 4096 vertices / 16384
  triangles (`src/script/bm3d.lua:34`), while the kernel and `bmmesh.py` allow 65535
  (`src/bm/bm.h:175-176`, `scripts/bmmesh.py:27`). A Python core that writes bigger models
  produces files the kernel reads but the tools may refuse to re-save (**NON VERIFICATO**
  beyond `bm3d.lua:188-189`).
- `bmmesh.encode` defaults `inset=0.25` (`scripts/bmmesh.py:30`); `T.encode_mesh` builds
  only the model record and dedups vertices by position (+bone) (`bm3d.lua:153-165`), so
  Python and Lua output need not be byte-equal for the same model (**NON VERIFICATO**).

## 7. Python version

- Makefiles use `PYTHON ?= python3` with no pinned version (`Makefile:15`, `rgb30.mk:13`).
- CI runs on `ubuntu-24.04` and installs `python3`, `python3-numpy`, `python3-pil`
  (`.github/workflows/ci.yml:22`, `:55`); Ubuntu 24.04's python3 is 3.12
  (**NON VERIFICATO** from the repo).
- Code uses f-strings (`scripts/bmmesh.py:33`), so ≥ 3.6; no `match`/walrus found by grep in
  `scripts/*.py tools/*.py` (2026-10-13). numpy only for training/renders
  (`scripts/nnetlib.py:15`, `scripts/trainassist.py:23`), PIL only for icons/logo/local2mesh
  (`scripts/mkicons.py:16`, `tools/local2mesh.py:42`): the format scripts are stdlib only.
- Installed here: WSL Python 3.14.4, Git Bash Python 3.14.7, `arm-none-eabi-gcc` 14.2.1,
  `aarch64-linux-gnu-gcc`, `gcc`; **no** `qemu-system-arm` (checked 2026-10-13).
- Safe target for the skill: Python ≥ 3.8, stdlib only (**NON VERIFICATO** as a project rule;
  it is what the format scripts already need).

## 8. Target hardware limits

| | Pi Zero W | RGB30 |
|---|---|---|
| CPU | ARM1176JZF-S, 1 core, raised to 1 GHz (`README.md:64`) | RK3566, 4× Cortex-A55 (`docs/RGB30.md:5-6`) |
| RAM | 512 MB, 448 MB to the ARM (`README.md:67`) | 1 GB LPDDR4 (`docs/RGB30.md:6`) |
| Memory bandwidth | memcpy ~100 MB/s, fill ~430 MB/s, "the real limit" (`README.md:68-69`) | — |
| Screen (game) | 640x360, 480x270, 320x180, 256x256 (`src/bm/bm.h:12-14`; `README.md:70-71`) | 720x720 panel (`docs/RGB30.md:6`); games 360x360 (`bm.h:9-11`) |
| 3D | V3D GPU fills pixels, ARM transforms (`README.md:70-73`); ARM-only in QEMU | software `r3d` ("The 3D on the RGB30 is `r3d`", `docs/progress/graphics-mali.md:11`) |
| Lua memory | — (not found as a number) | — (not found as a number) |

- Triangle budget: "about 1200 triangles drawn at 60 fps" (`docs/GAME-GUIDE.md:321-322`).
- Per-frame budget 16.7 ms for `_update` + `_draw` (`docs/GAME-GUIDE.md:179`).
- Mesh hard limits: 65535 vertices and faces per mesh (`src/bm/r3d.c:1058`,
  `src/bm/bm.h:175-176`, `docs/api/light3d.md:69`); 256 models, 64 bones, 255 clips, 1024 keys
  (`bm.h:177-181`); sheet up to 4096 px (`bm.h:169`). Tool editors: 4096 / 16384
  (`bm3d.lua:34`).
- Defaults of the PC importers: Meshy `--polycount 2000`, `--max-tris 3000`
  (`tools/meshy2mesh.py:11-12`); cutout `faces=1200` (`scripts/bmcutout.py:44`).

## 9. bm3d version and API-change bookkeeping

- `src/gpu/version3d.h`: `#define BM3D_VERSION "7.3"`, `#define BM3D_BLOCK "M39"`
  (`src/gpu/version3d.h:26-27`), plus the table `bm3d_versions()` (oldest first, from
  `:33`). The rule: a new block raises X, a step that changes what the driver can do
  raises Y; change `BM3D_VERSION`, add a row to the table and to `docs/DRIVERS.md`
  (`docs/DRIVERS.md:363-367`; `CLAUDE.md:164-165`).
- Visible at runtime: `gpu3d()` returns a `version` (4th value) = the bm3d version the
  current choice reproduces (`src/bm/runtime.c:2918-2924`, `docs/api/light3d.md:91`); the
  kernel logs "drawn by the ARM as bm3d …" (`src/bm/runtime.c:1896`); the 3D Bench prints
  `BM3D_VERSION` (`src/bm/b3d.c:1308`, `:1677`).
- A skill compatibility check can read `BM3D_VERSION` with a regex on
  `src/gpu/version3d.h` and compare to a stored value (**NON VERIFICATO**: proposal). Note
  that bm3d tracks the **drivers**; the MESH/ANIM **format** has no version field of its own
  (container version 1 at `bm.h:6`); format changes are dated in `bm.h` comments
  (e.g. `bm.h:42-43`, `:175`). So also hash/compare `src/bm/bm.h` and `scripts/bmmesh.py`
  constants (**NON VERIFICATO**: proposal).
- What changes with a new or changed API (`CLAUDE.md:139-142`, `:94-100`):
  1. the area file in `docs/api/` (3D: `docs/api/light3d.md`) and the index `docs/API.md`;
  2. `docs/GAME-GUIDE.md` (same tables and examples);
  3. `src/ai/kb/` (entries with `title_en:` / `text_en:`), then `make ai-model` and commit
     `src/ai/assist.weights` (`Makefile:110-116`);
  4. `sdk/README.md` if needed and the right `docs/progress/<area>.md`;
  5. a possible R… idea in `docs/ROADMAP.md` (via `tracker`).
  Format invariants to keep in step: sections 6/8/9 (`CLAUDE.md:131-133`), face winding and
  `animate()` ↔ Animator pose (`CLAUDE.md:134-136`), sound bank in three places, projects
  `.bme` (`CLAUDE.md:159-163`), GPU options and version (`CLAUDE.md:164-166`).

## Domande aperte

1. Golden files: is the luahost path (real `bm3d.lua` bytes, stand-in container) enough,
   or should bmhost get a `--arg PATH` (calling `bm_set_arg`, `runtime.c:5253`) so the
   real `cart_write` writes them? That is a code change outside this skill's file.
2. Should the Python core cap models at the tools' 4096/16384 (`bm3d.lua:34`) or the
   kernel's 65535 (`bm.h:175-176`)? Files above 4096 may not be re-saved by the tools.
3. Which `targets` the skill aims at by default: Pi (640x360, ~1200 tris), RGB30 (360x360, no
   triangle cap known) or both? `export cart --targets` lets the author choose.
4. Is byte equality between `bmmesh.encode` and `T.encode_mesh` required for the same
   model (inset, vertex dedup differ)? Not tested anywhere I found.
5. QEMU is missing in this WSL: should the skill avoid QEMU entirely (CI only) or ask to
   install `qemu-system-arm`?
6. Minimum Python version for the skill: no project pin exists; confirm ≥ 3.8 stdlib-only.
