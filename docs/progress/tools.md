# Tools and SDK

The on-console suite (bm SDK and editors), the assistant, the PC scripts, the Market, the
tutorial videos and CI. User guide: [`sdk/README.md`](../../sdk/README.md); resources:
[`docs/RISORSE.md`](../RISORSE.md).

## How it is today

### The suite (`carts/`, opened from the Dev tab or installed from the Market Apps page)
- **bm SDK** (`carts/editor`): projects, templates (Empty 2D, Platform 2D, Top-down 2D,
  Shooter 2D, Versus 2D, Online 2D, 3D scene, 3D with models, Stages 2D), budgets (tokens, Lua, assets),
  the dev kit report of a test run, jumps to the other tools (`cart_tool`), `Build .bm` (`Ctrl+B`,
  or builds a real `.b16` when `target = "b16"`) and `Export .b16` (`Ctrl+Shift+B`,
  `cart_build(path, "b16")`). Map page (`P.map`): layers, flags, `z` a 16×16 brush (2×2 cells,
  cursor and tile on even cells; fill on whole tiles), automatic 16×16 brush and first object
  placeholder tile (`>= 1024`) when switching to the `objects` layer, zoom in/out (`-`/`+`: 1, 2,
  4, 8, 16 px/cell), `m` (*Overview*) full 256×256 map at 1 px/cell with viewport box and
  click/arrow jump, `Tab` compact palette of used 16×16 tiles (`z` toggles full sheet), Home/End
  and PgUp/PgDn a page. Template *Stages 2D* as on the RGB30: `test_sdk_stages_pi`.
- **bm SDK (RGB30)** (`carts/sdk_b16`, built into `kernel8.img` unless `CLEAN_KERNEL=1`, and
  published as `sdk.b16` in the Market): the SDK's pages at 360×360 (`layout.lua`: `split` on
  the 8×16 grid, joined before `main.lua` by `build.py`), the pad by names (`ok`/`back`, L1/R1,
  Start, X = the page's actions as a list, L2/R2), `padtype` for code and names, page `snd` (F7: Sounds with 7 fields and Lib import `i`, Effects with 32 steps; QEMU `test_sdk_pad_*` drive it by keyboard), map zoom in/out
  (`-`/`+`), `Overview` (`m`, full 256×256 at 1 px/cell), `Tab`/`Y` compact palette of used 16×16
  tiles, automatic 16×16 object brush on the `objects` layer, `Build .bm` (`Ctrl+B`) and
  `Export .b16` (`Ctrl+Shift+B`; on the RGB30, exporting a `.b16` from `/carts` also copies it to
  `/bm/` so it appears immediately in Games). Template *Stages 2D* (3 stages in map bands, record in slot 1): `test_stages_template`, `test_sdk_stages_build`.
- **Tool session** (`src/kernel/devtool.c`): the tool, the games it tries (`cart_run`, back with
  `cart_arg()`), the tools it opens (`cart_tool`); prefers SD-installed Market apps (`/carts` or
  `/bm/`) over embedded kernel copies so tools can be updated from the Market and kernels can be
  built clean with `CLEAN_KERNEL=1`.
- **bm Code** (`carts/code`): tabs, two pages, 6×12/8×14/8×16 fonts; Ctrl+Q / Y+X help on the API function, API lint on save (`predict.lint`, same rules as `scripts/api_lint.py`); completion
  (`predict`, [`docs/PREDICT.md`](../PREDICT.md)), pad typing (`padtype`,
  [`docs/PADTYPE.md`](../PADTYPE.md)), F6 assistant and `#entry: … #` lines, F8
  breakpoints, Ctrl+Enter plays riff.
- **bm Pixel** (`carts/pixel`): F1 draw, F2 sheet, F3 palette; hitboxes of BOXES on the canvas, previews and sheet view (toggle j).
- **bm Studio** (`carts/studio`): models from tiles and blocks (block, tile, select, vertex,
  paint), reduce. **bm Animator** (`carts/animator`): rig and skin (`t` test pose: [tools-assistant](tools-assistant.md)), keyframes, sprites from an
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

### Assistant (`src/ai/`)
Details: [tools-assistant](tools-assistant.md).

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

### Tests on a real Pi (`tools/pi_bridge.py`, `tools/pi.py`, `tools/pi_test.py`)
The owner types the console password once in `pi_bridge.py`; a client (Claude Code, `pi_test.py`) then sends `line`, `send`, `play`, `kernel` through a local socket without seeing the password. It can run `gpu`/`b3d`, send a cartridge or a kernel and restore the normal one. It sees the error of a stopped cartridge (`err`, R-NET1) and the screen (`shot OUT.png`, R-NET2: transfer ops `T`/`G`): [`design/pi-test.md`](../design/pi-test.md).

### CI (`.github/workflows/ci.yml`)
Build; host tests (`make test-host`); QEMU in four shards (`make test-qemu SHARD=K/4`); RGB30 build and QEMU
tests; signed release on a `v*` tag of a green commit. A new test over 20 s goes in `SLOW`. Three QEMU tests were steadied against flaky timing: `test_studio_animator` (short gap, the toast is brief), `test_usb_mouse` (wait for one icon), `test_code_completion` (wait for the colour). `test_code_completion` still failed once on CI before PR 52. On a miss it now prints the screen text and colour counts and saves `build/diag/code-complete-miss.png`; CI uploads `build/diag/`. Read these files when it fails again. The artifact upload may fail without failing the job.

## Open work (`docs/ROADMAP.md`)

- **Web browser cart** ([`design/web-browser-cart.md`](../design/web-browser-cart.md)): stage 1 is done on branch `claude/bmweb-stage1` (PR to `claude/web-browser-design`): pure Lua URL, HTML, CSS and layout in `carts/bmweb/src/`, host test `make test-web` (or `sh tests/web/run.sh`; `--update` rewrites `tests/web/expected/`). No network, no kernel change, no `main.lua` yet. Next: stage 2a (kernel `web_get`), stage 2b (cart), after the owner answers Q1-Q6. Open points of stage 1: (1) the host test uses `build/host/luahost`, a Lua in the PATH, or a private Lua built in a temp folder; (2) the modules use `require`, so the stage 2 build must bundle them into one `main.lua` (as `carts/kitchen/build/build.py`); (3) memory: about 1.2 KiB of Lua per laid out line (3 000 lines: 3.7 MiB with streaming, 7.5 MiB when the tree is kept); measure on the Pi (stage 1b) and pack the runs if needed; (4) streaming lays out a `<style>` that follows some content for the later content only; (5) a text run is `chars * 6` px, a UTF-8 to CP437 map is stage 2b; (6) forms: GET only, a password input is never sent; `not verified on hardware`.
- **Pi tests from the PC** ([`design/pi-test.md`](../design/pi-test.md)): R-NET3 a limited test token (R-NET1 `err`, R-NET2 screenshot via ops `T`/`G` and R-NET5 bridge `status`/`freeze`/`reboot`/`--detach` are done; the test on the real Pi is the owner's). To plan with `bm-planner` (kernel change, branch `claude/net-test-*`).
- **PDF manuals** ([`MANUALS.md`](../MANUALS.md)): 17 manuals in en and it are written and checked against the code; the pictures (`manuals/shots/`) are to take on the console and the gaps listed in each manual's last chapter to fill; the Assistant app has no manual yet.
- **Project layout** ([`PROJECT-LAYOUT.md`](../PROJECT-LAYOUT.md)): `pong`, `hunt` and `yharnam` migrated; next
  the other games; `res:` with SHA-256 and licence check in `mkbm.py`. Open points: [`open-decisions.md`](../open-decisions.md).
- **M45** bm Write: try on the Pi (keyboard and pad), open the PDF on the PC, publish in the Market.
- **M47** projects: on the Pi, the copy question, copy from the menu, build twice.
- **M48** mouse in the suite: try every tool with a USB/Bluetooth mouse; then maybe an
  I-beam cursor and dragging panels.
- **Sprite stacking, step 3**: a *Stack* page in bm Pixel.
- Yharnam's map on the Pi: open `yharnam.bm` in the SDK, F3 F3, edit ground,lamps/barricades/gates/bosses on `objects`,
  Ctrl+S (`YHARNAM.BME`, about 6 MB), F5; then `mkmap.py --from` and commit the CSVs. A house
  stays in its 16×16-tile block.
- Overbit's mech pilots from pictures (D.Va `ref/rally_pilot.png`, D.Mon `ref/kaiju_pilot.png`):
  run `tools/meshy_text.py` with your own key (the `request.txt` lines), then `pack.py out rally_pilot kaiju_pilot`;
  optionally `rig=` lines with the ids of `out/NAME.task.txt` and `pack.py --rig`. Until then
  the text-to-3D pilots stay. The image-to-3D heroes are not rigged (no task ids kept).
- **3D maps in bm SDK** (Tropic Trail, completed): C module `src/bm/map3d.c` with Lua API `map3d_*` (mesh in 16×16-tile pieces with frustum culling `r3d_visible`, floor height queries, wall/step collisions, object placeholders); 3D view in the editor with the `v` key and an orbit camera; straight path tools `Shift+P` (width 1–5 with `Shift+W`), apple rows `Shift+A`, crates `Shift+C`, lamps `Shift+O`, entity count and triangle budget in the status bar.
- **Live reload / auto-watch from the PC**: `bm_net.py --watch` to automatically send and relaunch the cartridge at every local save of `main.lua`.
- **Native hardware mouse in nano8**: `mouse(true)` support for `.p8` cartridges that enable the pointer with `poke(0x5f2d, 1)`, exporting coordinates and wheel on `stat(32)`–`stat(36)`.

## Rules (do not break)

- Tool pages: one `do … end` exporting into `P`, under 200 locals, drawn on 16-px rows; a dialog frame never crosses the title row (QEMU tests read it). API list: one source `src/ai/api.tsv` → `build/words.lua` (`api`, `allow`), never edited by hand; the Lua lint in `src/ai/predict.lua` keeps the output of `scripts/api_lint.py` (diff check APP-5b-2). `api.tsv` has one line per signature of a name (`music(n, ...)`, `music(-1)`, `music()`); `predict.api(name).forms` lists them and Ctrl+Q shows all. CI: `make test-api` (`mkapi.py --check`, tests of mkapi and api_lint). The help screen is not verified on hardware.
- bm Mesh rewrites only the lines between `-- [bm Mesh begin]` and `-- [bm Mesh end]`.
- Mouse (`require "bmui"`, `src/script/bmui.lua`): the mouse does what the keys do: zones
  registered while drawing, chips that press the key, `U.press` before `keyp()`, a context menu
  of keys on the 8x16 grid. A mouse function outside a page's `do … end` does not see its
  locals: export them. Tests `bmui_host.lua`, `MS` in the test doubles, QEMU `test_editor_mouse`.
- Predict/pad tables `CROSS`/`FACE`/`KB` change in `src/ai/padtype.lua`.
- Skin recipes (`mesh_skin.c`): one bone per position, one connected body (`test_skins`), shared ring to the distal
  bone; `face_b` for a bone per corner, `face()` output unchanged; `static` vertex tables.
- kb comments `#` only at the top of a file; every entry has `title_en:` and `text_en:`.
- bm SDK (RGB30): never edit `carts/editor` for it (two editions); never in the Pi's kernel nor
  in the Market. Its test `make test-sdk-b16` (not in `HOST_TESTS`); QEMU `test_sdk_tool`.
- Tests: `make test-studio`, `test-write`, `test-res`, `test-ai`, `test-predict`,
  `test-padtype`, `test-img2mesh`; QEMU `test_projects`, `test_bm_write`.
- **The kernel embeds the editor** (`build/editor.bm` is inside `kernel.img`): after any change of `carts/editor` rebuild the kernel (`make build/kernel.img`) before a QEMU run; a stale kernel runs the old editor.
- **The 3D map** (map3d, layers `floor`, `height`, `walls`, `objects`): `map3d_build` reads the floor only at the **top-left tile of each 2x2 block** (cell = 2x2 tiles, `cell` 1.0): a 16x16 brush with tile 0 gives a floor of holes. Object placeholders are the **even** ids >= 1024 (1024 start, 1026 apple, 1028 crate, 1030 goal, 1032 lamp); the tile chooser stops at the sheet's last tile, so the markers have their own keys (`Shift+S/G/A/C/O` in the map page). `lamp3d` slots are 1..4.
- **bmhost and saving a game**: opening a `.bm` and saving asks "Save an editable copy" through a kernel modal that reads the keyboard, not the typed queue: in `bmhost --tool` it hangs a recording unless the input line `FRAME keys 0x28` presses Enter. A recording script opens the project (`.bme`) instead; `Ctrl+B`/`F5` do not build/run a game inside `bmhost --tool`; input files written from Windows Python get CRLF (a CR is typed as Enter): write them with LF.
- **Agent ledger** (`docs/agents/ledger.txt`, format in its `#` header): append-only, one line per subagent run; never read or edit it by hand, only `scripts/agent_audit.py` reads it.
