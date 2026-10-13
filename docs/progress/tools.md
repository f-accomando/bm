# Tools and SDK

The on-console suite (bm SDK and editors), the assistant, the PC scripts, the Market, the
tutorial videos and CI. User guide: [`sdk/README.md`](../../sdk/README.md); resources:
[`docs/RISORSE.md`](../RISORSE.md).

## How it is today

### The suite (`carts/`, opened from the Dev tab or installed from the Market Apps page)
- **bm SDK** (`carts/editor`): projects, templates (Empty 2D, Platform 2D, Top-down 2D,
  Shooter 2D, Versus 2D, Online 2D, 3D scene, 3D with models), budgets (tokens, Lua, assets),
  the dev kit report of a test run, jumps to the other tools (`cart_tool`), `Build .bm` (`Ctrl+B`,
  or builds a real `.b16` when `target = "b16"`) and `Export .b16` (`Ctrl+Shift+B`,
  `cart_build(path, "b16")`). Map page (`P.map`): layers, flags, `z` a 16×16 brush (2×2 cells,
  cursor and tile on even cells; fill on whole tiles), automatic 16×16 brush and first object
  placeholder tile (`>= 1024`) when switching to the `objects` layer, zoom in/out (`-`/`+`: 1, 2,
  4, 8, 16 px/cell), `m` (*Overview*) full 256×256 map at 1 px/cell with viewport box and
  click/arrow jump, `Tab` compact palette of used 16×16 tiles (`z` toggles full sheet), Home/End
  and PgUp/PgDn a page.
- **bm SDK (RGB30)** (`carts/sdk_b16`, built into `kernel8.img` unless `CLEAN_KERNEL=1`, and
  published as `sdk.b16` in the Market): the SDK's pages at 360×360 (`layout.lua`: `split` on
  the 8×16 grid, joined before `main.lua` by `build.py`), the pad by names (`ok`/`back`, L1/R1,
  Start, X = the page's actions as a list, L2/R2), `padtype` for code and names, map zoom in/out
  (`-`/`+`), `Overview` (`m`, full 256×256 at 1 px/cell), `Tab`/`Y` compact palette of used 16×16
  tiles, automatic 16×16 object brush on the `objects` layer, `Build .bm` (`Ctrl+B`) and
  `Export .b16` (`Ctrl+Shift+B`; on the RGB30, exporting a `.b16` from `/carts` also copies it to
  `/bm/` so it appears immediately in Games).
- **Tool session** (`src/kernel/devtool.c`): the tool, the games it tries (`cart_run`, back with
  `cart_arg()`), the tools it opens (`cart_tool`); prefers SD-installed Market apps (`/carts` or
  `/bm/`) over embedded kernel copies so tools can be updated from the Market and kernels can be
  built clean with `CLEAN_KERNEL=1`.
- **bm Code** (`carts/code`): tabs, two pages, 6×12/8×14/8×16 fonts; completion
  (`predict`, [`docs/PREDICT.md`](../PREDICT.md)), pad typing (`padtype`,
  [`docs/PADTYPE.md`](../PADTYPE.md)), F6 assistant and `#entry: … #` lines, F8
  breakpoints, Ctrl+Enter plays riff.
- **bm Pixel** (`carts/pixel`): F1 draw, F2 sheet, F3 palette; hitboxes of BOXES on the canvas, previews and sheet view (toggle j).
- **bm Studio** (`carts/studio`): models from tiles and blocks (block, tile, select, vertex,
  paint), reduce. **bm Animator** (`carts/animator`): rig, keyframes, sprites from an
  animation (F4: bone hitboxes previewed and baked into SPRITES and BOXES). **bm Mesh** (`carts/mesh`): vertices and faces of MESH models and of meshes the
  game builds (`cart_meshes()`).
- **bm Sound** (`carts/sound`): SOUNDS, SFX, PATTERN, SONG pages; music assistant on F6
  ([audio](audio.md)).
- **bm Write** (`carts/write`, a Market app): `.BMD` documents in `/docs`, A4 pages,
  exports `.TXT`/`.MD`/`.HTM`/`.PDF`.
- **Lib tab** (`src/kernel/lib.c`, `libview.c`): resources `.bmm .bmi .bms .bmt .bmc .bmk`,
  hidden until `lib_tab=1`.
- Shared Lua: `bm3d` (Studio/Animator/Mesh), `bmui` (mouse: zones registered while drawing,
  chips that press keys, `U.press` before `keyp()`, a context menu of keys).
- Projects (M47): tools edit `.bme`, never a `.bm`/`.b16`; saving a game asks for a copy.
  `bm_build` builds `.bm` and `bm_build_b16` exports `.b16` from either a `.bme` or a `.bm`
  (also in the console menu via *Export .b16*), never overwriting the other edition. Runtime side
  in [lua](lua.md).

### Assistant (`src/ai/`, F6 in every tool, `carts/assistant` in Dev)
- Q&A over the knowledge base `src/ai/kb/` (`BMAI` file, `assist.c`, small INT8 network,
  `assist.weights` built by `make ai-model`; format in `src/ai/kb/README.md`).
- Recipes: sprites (`sprite.c`), low-poly models with skeletons (`mesh.c`), music
  (`music.c`). Panel shared by the tools (`require "assist"`).
- Mesh from images: `cutout3d` / `picture3d` in the runtime (`src/bm/cutout.c`), PC tools
  `tools/img2mesh.py`, `cutout2mesh.py`, `local2mesh.py`, `meshy*.py` (`MESHY_API_KEY`
  only in the environment).
- Overbit's figures (`carts/overbit/art/meshy/`): `pack.py` packs the models of the branch
  `meshy-out` (`SOURCES`: the image-to-3D soldier, mei, junkrat, sojourn, juno, kiriko, dva,
  beast for sarge, frost, fuse, rail, orbit, akari and the two mechs; reduced to 1200 + 450
  when missing); `meshyrig.py` fits them on the skeletons (Meshy's `.rig` if any, `OWN_GUN`
  drops the old gun). `request.txt` + `meshy-overbit.yml` (push to `bm-core` or Run workflow):
  text-to-3D, `rig=`, `image=` (image-to-3D from `ref/`).
- Overbit models: the heroes' bodies are the primitives of `carts/overbit/art/heroes/*.py` on the
  skeletons of `art/rig.py` (guns and props on their own bones: nothing fuses in the poses). The Meshy
  figures (`art/meshy`, `meshyrig.py`) are set aside: their arms and guns fused; `OVERBIT_MESHY=1`
  (`models.py --meshy`) builds them again. `make overbit-heroes`: each hero as `.bmm` in
  `build/overbit/heroes/` for bm Mesh / bm Animator. Skeletons and clips are still written by `rig.py`,
  not made in Animator.
- `scripts/mkbm.py`: builds a `.bm` or `.b16` (`--lua --sheet --sheet8 --map --flags --sprites --audio
  --cover --models --res --from <cart> --b16`…); header `BMCART` with CRC-32 (`src/bm/bm.h`).
- `scripts/bmres.py`: extract/convert `BMRES` resources. `scripts/bmaudio.py`: sound banks.
  `scripts/bmmesh.py`, `bmdecimate.py`, `tools/bmreduce.py` (quadric collapse, same code as
  `src/bm/decimate.c`).
- `tools/qpuasm.py` → `src/gpu/shaders.h` ([graphics](graphics.md)).
- Yharnam's drawn map: `carts/yharnam/map_ground.csv`, `map_overlay.csv` (drawn: `MAP.drawn`)
  and `map_objects.csv` (`layers_yharnam`, 256×256 cells). `objects` is not drawn directly in-game:
  its placeholders (`art/places.py`, `PLACE` in the atlas, `mkassets.py --places` without rendering
  the frames) place the units `gen()` makes (houses: corner H7/H8/CH + roof tiles = width, body
  tiles only shown; trees, bushes, lamps, braziers, pyres, graves, mausoleums, statues,
  fountains, wells, benches, fences, crates, coffins, cages, bollards, carriages, plus hunter's
  lamps `shrine`=1186, barricades `barricade`=1188, gates `gate`=1190, and boss arenas `boss`=1192):
  a unit or gameplay object stays where its placeholder is, goes without it, and a placeholder no
  unit took makes one (hash of its tile). `ch.kind` (road, pave, grass) is derived from the drawn
  ground tiles in `map_ground.csv` so creature spawning follows edits made in the SDK. Beyond the
  edge lies the ring (`MAP.ring`). `carts/yharnam/mkmap.py` makes the first map from the plan
  (`make yharnam-map`, `tests/yharnam/mapgen.lua`: it overwrites the drawn one), takes it back
  (`--from YHARNAM.BME`), draws it (`--png`).
- `tools/bm_net.py`: kernels, cartridges and files over WiFi (protocol in
  [system](system.md)); `tools/bm_load.py`: serial chainloader and terminal;
  `tools/overbit_relay.py`: the internet relay for `bmnet`.
- `easy_install.sh` (WSL, SD on `/mnt/d`): kernel, `make install`, image, network kernel or
  files, release, Market, monitor, `bm/config.txt`.
- Releases: `scripts/release.sh vX.Y.Z` (signed manifests, `src/net/release.c`).

### Market (`market/`, repo `f-accomando/bm-market`)
Games (`GAMES`) and suite apps (`sdk` `.bm`/`.b16`, `code`, `pixel`, `studio`, `animator`, `mesh`,
`sound`, `assistant`, `write`, `typing`, tagged `kind: app`) reach the consoles from `bm-market`;
`make market-seed MARKET=<clone>` packages both games and apps, then commit and push there
(`scripts/market.sh` from WSL). The Market UI separates **Games** and **Apps** pages (`Y` / `V` or
subtab click) and shows `.bm` / `.b16` format badges on covers, title row and details panel.

### Tutorial videos (`video/`, skill `claude/skills/bm-video-tutorial`)
`video/lib/bmvideo.py` drives `bmhost` from a key script and lays out the page with
ffmpeg/libass; storyboard and script come from the same key script. Series *Skyvale World*
(`video/SERIE.md`), episodes 01–07 complete, each with a `verify.py`. `bmhost --tool` runs a
tool; `make bmhost-ai` builds `bmhost` with the real `ai` table. On the PC: no kernel menu.

### CI (`.github/workflows/ci.yml`)
Build; host tests (`make test-host`); QEMU in four shards (`make test-qemu SHARD=K/4`); RGB30 build and QEMU
tests; signed release on a `v*` tag of a green commit. A new test over 20 s goes in `SLOW`.

## Open work (`docs/ROADMAP.md`)

- **Project layout** ([`PROJECT-LAYOUT.md`](../PROJECT-LAYOUT.md)): `pong` has `bm.info`; migrate `hunt`, then
  `yharnam`; `res:` with SHA-256 and licence check in `mkbm.py`. Open points: [`open-decisions.md`](../open-decisions.md).
- **M45** bm Write: try on the Pi (keyboard and pad), open the PDF on the PC, then publish in
  the Market.
- **M47** projects: on the Pi, the copy question, copy from the menu, build twice.
- **bm SDK (RGB30)**: try it on the console (Dev > bm SDK and Open in bm SDK, pad, typing, save,
  copy of a game, Ctrl+B, F5 and Start + Select back); the QEMU test `test_sdk_tool` runs in CI
  (`make TARGET=rgb30 test`). A game built in `/carts` is not in Games (R41).
- **M48** mouse in the suite: try every tool with a USB/Bluetooth mouse; then maybe an
  I-beam cursor and dragging panels.
- **M43 step 3**: a *Stack* page in bm Pixel.
- Yharnam's map on the Pi: open `yharnam.bm` in the SDK, F3 F3, edit ground,lamps/barricades/gates/bosses on `objects`,
  Ctrl+S (`YHARNAM.BME`, about 6 MB), F5; then `mkmap.py --from` and commit the CSVs. A house
  stays in its 16×16-tile block.
- Overbit's mech pilots from pictures (D.Va `ref/rally_pilot.png`, D.Mon `ref/kaiju_pilot.png`):
  run `meshy-overbit` (its `request.txt` lines), then `pack.py out rally_pilot kaiju_pilot`;
  optionally `rig=` lines with the ids of `out/NAME.task.txt` and `pack.py --rig`. Until then
  the text-to-3D pilots stay. The image-to-3D heroes are not rigged (no task ids kept).
- **R15** Live reload / auto-watch dal PC: `bm_net.py --watch` per inviare e rilanciare automaticamente la cartuccia a ogni salvataggio locale di `main.lua`.
- **R26** Mouse hardware nativo in nano8: supporto `mouse(true)` per cartucce `.p8` che abilitano il puntatore con `poke(0x5f2d, 1)`, esportando coordinate e rotella su `stat(32)`–`stat(36)`.

## Rules (do not break)

- Tool pages: one `do … end` exporting into `P`, under 200 locals, drawn on 16-px rows; a
  dialog frame never crosses the title row (QEMU tests read it).
- bm Mesh rewrites only the lines between `-- [bm Mesh begin]` and `-- [bm Mesh end]`.
- Mouse (`require "bmui"`, `src/script/bmui.lua`): the mouse does what the keys do: zones
  registered while drawing, chips that press the key, `U.press` before `keyp()`, a context menu
  of keys on the 8x16 grid. A mouse function outside a page's `do … end` does not see its
  locals: export them. Tests `bmui_host.lua`, `MS` in the test doubles, QEMU `test_editor_mouse`.
- Predict/pad tables `CROSS`/`FACE`/`KB` change in `src/ai/padtype.lua`.
- kb comments `#` only at the top of a file; every entry has `title_en:` and `text_en:`.
- bm SDK (RGB30): never edit `carts/editor` for it (two editions); never in the Pi's kernel nor
  in the Market. Its test `make test-sdk-b16` (not in `HOST_TESTS`); QEMU `test_sdk_tool`.
- Tests: `make test-studio`, `test-write`, `test-res`, `test-ai`, `test-predict`,
  `test-padtype`, `test-img2mesh`; QEMU `test_projects`, `test_bm_write`.
