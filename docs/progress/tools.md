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
  published as `editor.b16` in the Market): the SDK's pages at 360×360 (`layout.lua`: `split` on
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
- **bm Pixel** (`carts/pixel`): F1 draw, F2 sheet, F3 palette, F4 stack (stack zones: slices one by one, the stack turning, make or change a zone, draw a slice on F1; save writes SPRITES with `cart_write` `sections[11]`); hitboxes of BOXES on the canvas, previews and sheet view (toggle j).
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
Details: [tools-assistant](tools-assistant.md). Tutorial videos (`video/`, series *Skyvale World*): [video](video.md).

### Market (`market/`, repo `f-accomando/bm-market`)
Games (`MARKET_GAMES`: `GAMES` without `MARKET_NOT`, so without `village`) and the bm editors
(`MARKET_APPS`: `editor`, `code`, `pixel`, `studio`, `animator`, `mesh`, `sound`, each with an explicit
`kind: app` line) reach the consoles from `bm-market`. The Pi gets each editor as `.bm` (built as
`build/<id>.bm`); the RGB30 gets the SDK as `.b16` under the same id `editor` (from `carts/sdk_b16`).
`assistant`, `write` and `typing` keep their place in the apps list of `is_known_app`.
`bm-market` has two folders with one layout: `games/<id>/` and `apps/<id>/`. `mkmarket.py games --apps apps`
reads both. An item of `apps/` is always `kind app`; an id exists in one folder only; the catalog keeps
one format (a `kind app` line), so old consoles work. `make market-seed MARKET=<clone>` puts the games
in `games/` and the apps (the editors, `typing`, `write`) in `apps/`. Then commit and push there
(`scripts/market.sh` from WSL). Removing an item from the seed does not remove it from the clone: run
`git rm -r games/<id>` there. The Market UI separates **Games** and **Apps** pages (`Y` / `V` or
subtab click) and shows `.bm` / `.b16` format badges on covers, title row and details panel.

### Tests on a real Pi (`tools/pi_bridge.py`, `tools/pi.py`, `tools/pi_test.py`)
The owner types the console password once in `pi_bridge.py`; a client (Claude Code, `pi_test.py`) then sends `line`, `send`, `play`, `kernel` through a local socket without seeing the password. It can run `gpu`/`b3d`, send a cartridge or a kernel and restore the normal one. `./easy_install.sh bridge [profile]` (menu 9) starts it for a saved profile with its board (`--board pi|zero2|rgb30`: `kernel FILE` takes only that board's kernel file; the RGB30 has no monitor, so its crash/freeze/reboot keys answer "not for this board"; test `tests/net/check_pi_bridge_board.py`, not tried on the RGB30). It sees the error of a stopped cartridge (`err`, R-NET1) and the screen (`shot OUT.png`, R-NET2: transfer ops `T`/`G`): [`design/pi-test.md`](../design/pi-test.md).

### CI (`.github/workflows/ci.yml`)
Build; host tests (`make test-host`); QEMU in four shards (`make test-qemu SHARD=K/4`); RGB30 build and QEMU
tests; signed release on a `v*` tag of a green commit. A new test over 20 s goes in `SLOW`. Three QEMU tests were steadied against flaky timing: `test_studio_animator` (short gap, the toast is brief), `test_usb_mouse` (wait for one icon), `test_code_completion` (wait for the colour). `test_code_completion`, `test_sd_cartridges` and `test_monitor_line` failed at times on CI and did not fail in 81 local runs (idle and under CPU load). The code read showed keys sent before the screen had changed. The tests now wait for the menu (`wait_menu`), for the cover after each key, and for "Tab:" to leave the status line. On a miss `test_code_completion` prints the screen text and colour counts and saves `build/diag/code-complete-miss.png`; `wait_menu` saves `build/diag/menu-not-drawn.png` and `.txt`; CI uploads `build/diag/`. Read these files when a test fails again. `test_code_editor` (failed once on CI: the two-page screen after F4) and `test_usb_mouse` (failed once on CI) were steadied the same way. Under CPU load `test_code_editor` failed in 7 of 12 local runs, always at "bm: copied": the answer key was sent before the question box was drawn, and the question's first poll ignores a key held at the start. The test now waits for the box (`copy_asked`). It also waits for the file list before Down and Enter and for each new tab before the next key, and `see` waits 20 s. `test_usb_mouse` failed in 1 of 12 runs, idle or loaded: the Left key moved two covers, because a slow menu frame lets the 500 ms key repeat (`hid.c`) fire. The test keeps a strict check (user decision, 22 Oct): the Left key is sent as press and release in one call, and it moves exactly one cover. It waits 15 s for each screen. On a miss `test_code_editor` saves `build/diag/code-editor-miss.png` and `.txt`. The artifact upload may fail without failing the job.

## Open work (`docs/ROADMAP.md`)

- **Web browser cart** ([`design/web-browser-cart.md`](../design/web-browser-cart.md), section 8): stages 1 to 5 are built. Stage 1: pure Lua URL, HTML, CSS and layout in `carts/bmweb/src/` (`make test-web`; `sh tests/web/run.sh --update` rewrites `tests/web/expected/`). Stages 2b to 5: the cart `.bme` project (`main.lua`, loader with pace and `robots.txt`, browser state, view, downloads to `bm/web`); host test `tests/web/cart.lua` (mock kernel), QEMU `test_bmweb`, build `make build/carts/bmweb.bme`. Keys: A open, B back, left/right link, up/down scroll, L1/R1 page, X pictures, Y address (on-screen keyboard), Start bookmark, Select home, L2 reload, R2 forward; keyboard: Tab, Enter, Backspace, Ctrl+L, Ctrl+D, Ctrl+R, Esc; mouse: click, wheel. Open: the Pi and RGB30 runs (list in `docs/NEXT.md`), not verified on hardware; memory per laid out line (about 1.2 KiB) and the TLS stall are not measured on the Pi; the reader of embedded page JSON, `glb_import` and a 3D viewer are not built; the decoder speed on the Pi Zero W and `web_get` on a real server on Pi and RGB30 (D49) are not tried.
- **Pi tests from the PC** ([`design/pi-test.md`](../design/pi-test.md)): R-NET3 a limited test token (R-NET1 `err`, R-NET2 screenshot via ops `T`/`G` and R-NET5 bridge `status`/`freeze`/`reboot`/`--detach` are done; the test on the real Pi is the owner's). To plan with `bm-planner` (kernel change, branch `claude/net-test-*`).
- **Bridge tools ported** (`line` allowlist with `crc`, password from `BM_PASSWORD_FILE`, `--board pi|zero2|rgb30`, `easy_install.sh bridge [profile]`, monitor `crc`): run `tests/net/check_bm_net_config.py` and `tests/net/check_pi_bridge_board.py` in WSL (Unix sockets); add `check_pi_bridge_board.py` to `make test-net`; build the Pi kernel with the monitor `crc` command (not built yet) and run `test_monitor_crc` in `tests/qemu_test.py`; check `crc` and `--board` on the real Pi and RGB30 (not verified on hardware). Decide whether `stack` joins `LINE_CMDS` in `tools/pi_bridge.py`.
- **PDF manuals** ([`MANUALS.md`](../MANUALS.md)): 17 manuals in en and it are written and checked against the code; the pictures (`manuals/shots/`) are to take on the console and the gaps listed in each manual's last chapter to fill; the Assistant app has no manual yet. Install-sd, boot-os, api-3d (`map3d_*`), editor-animator (test pose), levels (Stages 2D) and api-tools (`bm_net --shot/--get`) were already current on bm-core; boot-os now has the SD map and the RGB30 game option *Send to a nearby console*. The other manuals are not re-read against sources changed since their stamp.
- **Project layout** ([`PROJECT-LAYOUT.md`](../PROJECT-LAYOUT.md)): `pong`, `hunt` and `yharnam` migrated; next
  the other games; `res:` with SHA-256 and licence check in `mkbm.py`. Open points: [`open-decisions.md`](../open-decisions.md).
- **M45** bm Write: try on the Pi (keyboard and pad), open the PDF on the PC, publish in the Market.
- **M47** projects: on the Pi, the copy question, copy from the menu, build twice.
- **M48** mouse in the suite: try every tool with a USB/Bluetooth mouse; then maybe an
  I-beam cursor and dragging panels.
- **Stack page of bm Pixel**: try it on the Pi and the RGB30 (keyboard and pad). Not done: delete a stack zone, a vertical layout (`stackv`, branch `claude/stackv`: the page keeps `sp.dir` and `cell()` ready), mouse drag. Pad and mouse are not tested in QEMU.
- **Web player** ([`design/bm-web-player.md`](../design/bm-web-player.md), [`web/README.md`](../../web/README.md)): study done; stage 1 done: `web/bmcart.js` reads a `.bm` or `.b16` in the page (title, author, cover, sections, CRC-32) and the Start tab has an "Open a .bm file" input; test `make test-webplayer` (Node). Next: a wasm toolchain (WSL has no `emcc` or wasm `clang`; the user decides), then a web runner with a frame-step entry, canvas, input, audio. The page is still a mock: no kernel code runs in it. It shows the Start, 2D, 3D and Sound tabs and calls `window.bmLauncher.start(cartUrl)`. React 18.3.1 (MIT) loads from cdnjs without SRI; the 24 PNG previews are placeholders. To do: add SRI or vendor React, set the licence policy for real assets, connect the WebAssembly runtime.
- **Stack to mesh** (`tools/stack2mesh.py`, host tool, tests `tests/tools/test_stack2mesh.py`): done. It reads a sprite sheet PNG and a stack (`--size W H -n N --at X Y`, `--dir h` for `stack`, `--dir v` for one slice under the other), and writes a `.bmm` with one MESH model. Each opaque pixel is a voxel. Voxels of the same colour join into boxes: rectangles in a slice first, then equal rectangles on neighbouring slices. A box costs 8 vertices and 12 triangles. The default limit is 4096 vertices, so 512 boxes; `--kernel-limits` allows 8191 boxes. A stack that is too big exits with 1 and gives the limit. The round trip is tested: mesh back to slices equals the source. Not done: only the outer faces (fewer triangles for a solid volume), a button in bm Pixel and bm Studio, the `stackv` direction flag of the Lua call (the PNG layout `--dir v` exists), a check on the Pi.
- **3D maps in bm SDK** (Tropic Trail, completed): C module `src/bm/map3d.c` with Lua API `map3d_*` (mesh in 16×16-tile pieces with frustum culling `r3d_visible`, floor height queries, wall/step collisions, object placeholders); 3D view in the editor with the `v` key and an orbit camera; straight path tools `Shift+P` (width 1–5 with `Shift+W`), apple rows `Shift+A`, crates `Shift+C`, lamps `Shift+O`, entity count and triangle budget in the status bar.
- **Live reload / auto-watch from the PC**: `bm_net.py --watch` to automatically send and relaunch the cartridge at every local save of `main.lua`.
- **Native hardware mouse in nano8**: `mouse(true)` support for `.p8` cartridges that enable the pointer with `poke(0x5f2d, 1)`, exporting coordinates and wheel on `stat(32)`–`stat(36)`.

## Rules (do not break)

- bm Web (`carts/bmweb`): no site name in the code, the texts or the Market text. Only `https://`. One request at a time, 3 s between two requests to one host, `robots.txt` read once per host and obeyed; no prefetch (pictures load only when the user presses X). Files go to `bm/web` only through `file_write`, after the user presses A; the name comes from `files.name` (8.3, never `.BM`, `.BME`, `.B16`). `browser.lua`, `loader.lua`, `robots.lua`, `files.lua` make no draw call and read no input: the host test runs them on a mock `api`. A module is added in `src/` and needs no change in the Makefile. Edit the sources in `carts/bmweb/`; `build/carts/bmweb.bme` is built.
- Tool pages: one `do … end` exporting into `P`, under 200 locals, drawn on 16-px rows; a dialog frame never crosses the title row (QEMU tests read it). API list: one source `src/ai/api.tsv` → `build/words.lua` (`api`, `allow`), never edited by hand; the Lua lint in `src/ai/predict.lua` keeps the output of `scripts/api_lint.py` (diff check APP-5b-2). `api.tsv` has one line per signature of a name (`music(n, ...)`, `music(-1)`, `music()`); `predict.api(name).forms` lists them and Ctrl+Q shows all. CI: `make test-api` (`mkapi.py --check`, tests of mkapi and api_lint). The help screen is not verified on hardware. `tools/pi_bridge.py` `line`: allowlist `LINE_CMDS` per `;`-command, no `set`/`save`/`play`; widening it needs SPV (R-NET6). Bridge password: only `BM_PASSWORD`, the owner's file `~/.pi_bridge.pw` / `$BM_PASSWORD_FILE` (0600, written by the owner; assistants never create, read or print it) or the prompt (R-NET10).
- bm Pixel writes the zones only when the Stack page changed them (`sp.zdirty`): `cart_write(..., {sections = {[11] = cart_data(11)}})`. A save without a zone change keeps the file's SPRITES as it is.
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
  `test-padtype`, `test-img2mesh`, `test-webp` (run it after each change of `src/bm/webp.c`; the decoder frees all its memory before it returns); QEMU `test_projects`, `test_bm_write`.
- **The kernel embeds the editor** (`build/editor.bm` is inside `kernel.img`): after any change of `carts/editor` rebuild the kernel (`make build/kernel.img`) before a QEMU run; a stale kernel runs the old editor.
- **The 3D map** (map3d, layers `floor`, `height`, `walls`, `objects`): `map3d_build` reads the floor only at the **top-left tile of each 2x2 block** (cell = 2x2 tiles, `cell` 1.0): a 16x16 brush with tile 0 gives a floor of holes. Object placeholders are the **even** ids >= 1024 (1024 start, 1026 apple, 1028 crate, 1030 goal, 1032 lamp); the tile chooser stops at the sheet's last tile, so the markers have their own keys (`Shift+S/G/A/C/O` in the map page). `lamp3d` slots are 1..4.
- **bmhost and saving a game**: opening a `.bm` and saving asks "Save an editable copy" through a kernel modal that reads the keyboard, not the typed queue: in `bmhost --tool` it hangs a recording unless the input line `FRAME keys 0x28` presses Enter. A recording script opens the project (`.bme`) instead; `Ctrl+B`/`F5` do not build/run a game inside `bmhost --tool`; input files written from Windows Python get CRLF (a CR is typed as Enter): write them with LF.
- **Agent ledger** (`docs/agents/ledger.txt`, format in its `#` header): append-only, one line per subagent run; never read or edit it by hand, only `scripts/agent_audit.py` reads it.
- **Agent baseline**: `python scripts/agent_audit.py` compares the ledger with a baseline (`--baseline docs/agents/baseline.txt`). Write a new baseline with `--save-baseline PATH`.
- **Audit checks**: `python scripts/docs_audit.py check` lists broken links (exit 1). `python scripts/quarantine_check.py` checks `tests/quarantine.txt` (format in its `#` header). `python scripts/translate_check.py SRC DST` compares a translation with its source (skill `bm-translate`).
