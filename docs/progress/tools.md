# Tools and SDK

The on-console suite (bm SDK and editors), the assistant, the PC scripts, the Market, the
tutorial videos and CI. User guide: [`sdk/README.md`](../../sdk/README.md); resources:
[`docs/RISORSE.md`](../RISORSE.md).

## How it is today

### The suite (`carts/`, opened from the Dev tab; tools live in the kernel, not the Market)
- **bm SDK** (`carts/editor`): projects, templates (Empty 2D, Platform 2D, Top-down 2D,
  Shooter 2D, Versus 2D, Online 2D, 3D scene, 3D with models), budgets (tokens, Lua, assets),
  the dev kit report of a test run, jumps to the other tools (`cart_tool`).
- **bm Code** (`carts/code`): tabs, two pages, 6×12/8×14/8×16 fonts; completion
  (`predict`, [`docs/PREDICT.md`](../PREDICT.md)), pad typing (`padtype`,
  [`docs/PADTYPE.md`](../PADTYPE.md)), F6 assistant and `#entry: … #` lines, F8
  breakpoints, Ctrl+Enter plays riff.
- **bm Pixel** (`carts/pixel`): F1 draw, F2 sheet, F3 palette.
- **bm Studio** (`carts/studio`): models from tiles and blocks (block, tile, select, vertex,
  paint), reduce. **bm Animator** (`carts/animator`): rig, keyframes, sprites from an
  animation. **bm Mesh** (`carts/mesh`): vertices and faces of MESH models and of meshes the
  game builds (`cart_meshes()`).
- **bm Sound** (`carts/sound`): SOUNDS, SFX, PATTERN, SONG pages; music assistant on F6
  ([audio](audio.md)).
- **bm Write** (`carts/write`, a Market app in Games): `.BMD` documents in `/docs`, A4 pages,
  exports `.TXT`/`.MD`/`.HTM`/`.PDF`.
- **Lib tab** (`src/kernel/lib.c`, `libview.c`): resources `.bmm .bmi .bms .bmt .bmc .bmk`,
  hidden until `lib_tab=1`.
- Shared Lua: `bm3d` (Studio/Animator/Mesh), `bmui` (mouse: zones registered while drawing,
  chips that press keys, `U.press` before `keyp()`, a context menu of keys).
- Projects (M47): tools edit `.bme`, never a `.bm`/`.b16`; saving a game asks for a copy.
  Runtime side in [lua](lua.md).

### Assistant (`src/ai/`, F6 in every tool, `carts/assistant` in Dev)
- Q&A over the knowledge base `src/ai/kb/` (`BMAI` file, `assist.c`, small INT8 network,
  `assist.weights` built by `make ai-model`; format in `src/ai/kb/README.md`).
- Recipes: sprites (`sprite.c`), low-poly models with skeletons (`mesh.c`), music
  (`music.c`). Panel shared by the tools (`require "assist"`).
- Mesh from images: `cutout3d` / `picture3d` in the runtime (`src/bm/cutout.c`), PC tools
  `tools/img2mesh.py`, `cutout2mesh.py`, `local2mesh.py`, `meshy*.py` (`MESHY_API_KEY`
  only in the environment).

### PC scripts and tools
- `scripts/mkbm.py`: builds a `.bm` (`--lua --sheet --sheet8 --map --flags --sprites --audio
  --cover --models --res`…); header `BMCART` with CRC-32 (`src/bm/bm.h`). No `--b16` yet
  (M42).
- `scripts/bmres.py`: extract/convert `BMRES` resources. `scripts/bmaudio.py`: sound banks.
  `scripts/bmmesh.py`, `bmdecimate.py`, `tools/bmreduce.py` (quadric collapse, same code as
  `src/bm/decimate.c`).
- `tools/qpuasm.py` → `src/gpu/shaders.h` ([graphics](graphics.md)).
- `tools/bm_net.py`: kernels, cartridges and files over WiFi (protocol in
  [system](system.md)); `tools/bm_load.py`: serial chainloader and terminal;
  `tools/overbit_relay.py`: the internet relay for `bmnet`.
- `easy_install.sh` (WSL, SD on `/mnt/d`): kernel, `make install`, image, network kernel or
  files, release, Market, monitor, `bm/config.txt`.
- Releases: `scripts/release.sh vX.Y.Z` (signed manifests, `src/net/release.c`).

### Market (`market/`, repo `f-accomando/bm-market`)
Apps in `GAMES` (Makefile) reach the consoles from `bm-market`; copies here are for tests.
`make market-seed MARKET=<clone>` then commit and push there (`scripts/market.sh` from WSL).
A new app: a line in `market/about.txt` (licence required) and a place in `GAMES`.

### Tutorial videos (`video/`, skill `claude/skills/bm-video-tutorial`)
`video/lib/bmvideo.py` drives `bmhost` from a key script and lays out the page with
ffmpeg/libass; storyboard and script come from the same key script. Series *Skyvale World*
(`video/SERIE.md`), episodes 01–07 complete, each with a `verify.py`. `bmhost --tool` runs a
tool; `make bmhost-ai` builds `bmhost` with the real `ai` table. On the PC: no kernel menu.

### CI (`.github/workflows/ci.yml`)
Build; host tests on two machines (`make test-host` with `HOST_SKIP=test-overbit`, and
`test-overbit`); QEMU in four shards (`make test-qemu SHARD=K/4`); RGB30 build and QEMU
tests; signed release on a `v*` tag of a green commit. A new test over 20 s goes in `SLOW`.

## Open work (`docs/ROADMAP.md`)

- **M45** bm Write: try on the Pi (keyboard and pad), open the PDF on the PC, then publish in
  the Market.
- **M47** projects: on the Pi, the copy question, copy from the menu, build twice.
- **M48** mouse in the suite: try every tool with a USB/Bluetooth mouse; then maybe an
  I-beam cursor and dragging panels.
- **M43 step 3**: a *Stack* page in bm Pixel. **M42 step 4**: the SDK saves a real `.b16`.

## Rules (do not break)

- Tool pages: one `do … end` exporting into `P`, under 200 locals, drawn on 16-px rows; a
  dialog frame never crosses the title row (QEMU tests read it).
- bm Mesh rewrites only the lines between `-- [bm Mesh begin]` and `-- [bm Mesh end]`.
- Mouse: the mouse does what the keys do. Tests `bmui_host.lua`, `MS` in the test doubles,
  QEMU `test_editor_mouse`.
- Predict/pad tables `CROSS`/`FACE`/`KB` change in `src/ai/padtype.lua`.
- kb comments `#` only at the top of a file; every entry has `title_en:` and `text_en:`.
- Tests: `make test-studio`, `test-write`, `test-res`, `test-ai`, `test-predict`,
  `test-padtype`, `test-img2mesh`; QEMU `test_projects`, `test_bm_write`.
