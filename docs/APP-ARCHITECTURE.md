# How bm programs are built: games, tools and the pieces around them

Structure at code level as implemented (audit of 2026-10-13, branch `pdf-manuals`). Evidence is `file:line` at the time
of the audit; **UNVERIFIED** marks what was not confirmed. The formats are in [BM-FORMAT.md](BM-FORMAT.md); problems and
proposals (with pros, cons and what they break) are in section 7 and in [FORMATS-AUDIT.md](FORMATS-AUDIT.md). The
folder layout of a game is in [PROJECT-LAYOUT.md](PROJECT-LAYOUT.md).

## 1. One runtime for everything

Games, tools, `.b16` and `bmhost` (the PC host) all run through the same runtime, `bm_run` (`src/bm/runtime.c:9188`).
A program is a cartridge: a Lua chunk with optional assets, no native code.

```
menu / RGB30 ui / monitor / bmhost
        |  play()
        v
   bm_run(data, len)
     1. close the cartridge suspended in memory (one at a time)
     2. bm_parse: magic, version, CRC, sections (format.c)
     3. load_assets: LUA SHEET/SHEET8 MAP LAYERS FLAGS SPRITES BOXES MESH ANIM AUDIO COVER   (INFO is not used here)
     4. new_cart_state: its own Lua state (64 MiB cap), libraries base table string math coroutine utf8,
        no io/os/package/debug, dofile loadfile load removed, the bm API as globals, `require` of the built-in libs
     5. enter the cartridge's screen mode, loading screen, load the sound bank
     6. run the chunk (top level), then _init()
     7. frame loop at 60 fps: _update (several with frameskip) -> _draw -> overlays -> present
     8. leave: Esc / PS / Start+Select / quit() -> suspend (menu games) or release
```

- **Entry points:** the cartridge's globals `_init`, `_update`, `_draw` in one chunk named `main.lua`; a missing one is
  skipped (`runtime.c:4883`).
- **Budget:** 20 million Lua instructions **per callback** (`_init`, `_update`, `_draw` each; 1000 x 20000 hooks,
  `runtime.c:68-69`); past it the cartridge ends with an error. `timeslice(co)` lets a coroutine spread work over frames.
- **Saves:** `save(t, slot)` / `saved(slot)` write a Lua table to `/bm/save/<CRC32(title, author)>.SAV` (slots `.S02-.S08`).
  **The title and the author are the identity of a game's saves**: renaming a game loses them; two games with the same pair
  share them.
- **Errors:** the message and a traceback are printed in red and kept in `bm_last_error()` for the tools.
- **Suspend/resume:** only a cartridge started from the menu is suspended (one global slot); tools and the assistant use
  `bm_play` and are never suspended. `time()` does not count the time spent in the menu.
- **Permissions:** a game asking for the network, `/docs` or `report()` is asked once (`perm_allowed`, answer kept in
  `bm/config.txt`); a tool is never asked.

## 2. Two kinds of program

| | Games | Tools and apps |
|---|---|---|
| What | `.bm` / `.b16` cartridges in `/carts` (Pi) or `/bm` (RGB30), from the Market or by hand | ordinary cartridges written in Lua, **embedded in the kernel** |
| Examples | pong, hunt, yharnam, Overbit... | bm SDK, Sound, Studio, Animator, Mesh, Pixel, Code, Assistant |
| Where they run | the **Games** tab (cover, suspend/resume) | the **Dev** tab (the tools only; the project files `.bme` are not listed: the editors open them) |
| `tool_mode` | 0 | 1 |
| Writing | only **new** `.bm`/`.bme` files in `/carts` (and the ones made in this run); asks for net/docs/report | any path; may `cart_build`, `cart_run`, `cart_tool`; never asked |

**How a tool is embedded** (`Makefile:101-103,131-174`, `src/script/embed.S`, `src/kernel/carts.c:49-55,449-460`):
`mkbm.py --lua carts/<t>/main.lua --cover icon.png --title "bm X"` builds `build/<t>.bm`; `embed.S` `.incbin`s it as
`bm_<t>_cart`; `carts.c` lists it in the Dev tab; `tool_id_of` maps the symbol or an SD file name to a tool id. A
`CLEAN_KERNEL=1` build leaves them out. The RGB30 kernel embeds only `sdk_b16`.

**The tool session** (`src/kernel/devtool.c:53-128`): look for an SD copy of the tool (`/carts/<name>.bm`, or `/bm/...` on
the RGB30), which **replaces the embedded one**; `bm_set_tool(1)`; `bm_play`; `bm_set_tool(0)`. Then, in a loop:

1. `cart_tool(name, path)` asked: start the other tool; `cart_arg().from` says which tool called it.
2. `cart_run(path, {breaks, stop})` asked: play the game **without** tool mode (with the debugger when asked), then reopen the
   tool with `cart_arg() = {path, back=1, error, from, run = {frames, fps, ms...}}`.
3. otherwise the session ends.

**The API of the tools** (`runtime.c`, [api/tools.md](api/tools.md)): `cart_load` (replaces the caller's sheet, map,
models with the file's), `cart_read`, `cart_audio`, `cart_data(type[, bytes])` (types 8, 9, 11, 14), `cart_sheet`,
`cart_new`, `cart_save`, `cart_write` (MESH, ANIM, `lua`, `sheet`, `title`, `from`), `cart_put_audio`, `cart_build(project[, "b16"])`,
`cart_run`, `cart_tool`, `cart_arg`, `cart_config`. `cart_load`, `cart_read`, `cart_audio`, `cart_data`, `cart_sheet` have no
`tool_mode` check (UNVERIFIED for the last three).

**Project and game.** A `.bme` is the same container with bit 0 set. A tool writing to a game path makes an **editable
copy** (`.bme`, after the question "Save an editable copy?"); `cart_build` copies a project to a game `.BM` next to it
(the project's name goes to offset 104; a rebuild replaces only that file); `cart_build "b16"` writes a `.B16`, never
touching the `.bm` (`project.c`).

## 3. Shared libraries

`require` loads only the **built-in** libraries (`src/bm/require.c:24-37`, embedded with `.incbin` from `src/script` and
`src/ai`); any other name is an error listing them. A library is loaded once per cartridge state.

| Name | File | Used by |
|---|---|---|
| `bmlib` | `src/script/bmlib.lua` | games (maths, collisions, tweens, states, saves, menus, particles), SDK templates |
| `bmnet` | `src/script/bmnet.lua` | online games |
| `bm3d` | `src/script/bm3d.lua` | Studio, Animator, SDK |
| `bmui` | `src/script/bmui.lua` | the editors (mouse, menus) |
| `riff` | `src/script/riff.lua` | Code (Ctrl+Enter), Sound, games with music |
| `assist`, `predict`, `padtype` | `src/ai/*.lua` | the assistant, completion, typing with the pad |
| `words` | `build/words.lua` (generated) | dictionaries |

A game **cannot `require` its own files**: there is no module system for cartridges (nothing like `package`).

## 4. Games made of several files

- Single-file games: `carts/<id>/main.lua` + assets; the generic Makefile rule calls `mkbm.py` with `--info bm.info` (3 games
  migrated) or the `title_<id>`/`res_<id>` variables.
- Several files (titan, titan_ex, kitchen, overbit, nano8, sdk_b16): one `build.py` **per game** joins `src/*.lua` in name order
  into a single `main.lua`: `00_*.lua` stays at the top level and declares the shared locals; every other file goes into
  `do -- name ... end` (so its locals do not count against Lua's 200 limit); an `OUT.map` file maps `main.lua:LINE` back to a
  source file. titan and overbit append a build tag (SHA-1 of the sources); overbit also has `--minify`, `--start`, `--hero`...
  A `local` of file 02 is invisible to file 03: shared state must be a global or declared in `00_`.
- At runtime there is **one LUA section and one chunk**.

## 5. Resources and metadata

- Game parts go into sections by `mkbm.py` flags (`--sheet --map --flags --sprites --models --audio --cover`). Resource files
  (`BMRES`) are made and merged by `scripts/bmres.py`; no cartridge in `carts/` uses them and the game has no API to read
  them ([FORMATS-AUDIT.md](FORMATS-AUDIT.md) section 4).
- Metadata: the **header** (title, author, resolution) is what the menu shows; the **INFO** section (`bm.info`) holds
  `name desc author license version tags date` and is read by the runtime only for the Lib tab.

## 6. The pieces and what crosses between them

```
carts/<id>/ (main.lua, assets/, bm.info)  --mkbm.py-->  build/carts/X.bm  --copy-->  /carts/X.BM on the SD --> bm_run
carts/<tool>/main.lua                      --mkbm.py-->  build/<t>.bm      --embed.S--> kernel.img (Dev tab)

tool (tool_mode)  --cart_load/save/write/data/audio--> .bme on the SD   (editable copy of a game: bm_make_copy)
.bme  --cart_build-->  X.BM        .bme/.bm  --cart_build "b16"-->  X.B16
tool  --cart_run(path)--> game (tool_mode 0) --exit--> cart_arg() back into the tool
tool  --cart_tool(name, path)--> another tool (cart_arg().from = the caller)

resource files  <--bmres.py extract/add/convert-->  .bm/.bme       Lib tab (kernel) reads and previews them
Market: repo bm-market  games/<id>/{id.bm, id.b16, info.txt} --CI: mkmarket.py--> index.txt (SHA-256) signed ECDSA P-256
        console: verifies the signature (keys/market-pub.pem) then each file's SHA-256, then writes /carts or /bm
        Apps page: installs tools on the SD, which override the embedded ones (load_sd_tool)
monitor (UART/keyboard): play NAME, set key=value (bm/config.txt)
chainloader + tools/bm_load.py (serial) and tools/bm_net.py (network): kernel.img, .bm, config to a console
bmhost (PC): the same runtime with stubs: --tool, --sd DIR, --input, --shots, --video (tests, reels, screenshots)
files on the card: /carts, /bm (config.txt, save, shots, lib, sounds), /docs
```

**Menu tabs** (`carts.c:190`): Market (Games / Apps), Games, Dev (tools, then `home.c` `tools[]`: Code, Assistant, Monitor, Lua,
System, benches, then projects), Lib (resource files, hidden unless `lib_tab=1`), Settings (`bm/config.txt`).

**Roles:** the **SDK** is the hub (projects, templates, build/export, jumps to the other tools); **Code, Pixel, Studio,
Animator, Mesh, Sound** edit one kind of data each, in the same project; **Write** and the **Assistant** are apps outside the
project cycle; the **Market** delivers games and apps; the **Lib** tab is meant as the shelf of shared resources; the
**monitor** and `bmhost` are for developers.

## 7. Problems and proposals

**S1 - A game can plant a tool with full rights (security).** `write_target` lets a game create **any new** `.bm` file
in `/carts` (`runtime.c:5395-5420`: it refuses only existing files); `tool_id_of` and `load_sd_tool` recognise
`/carts/<tool>.bm` (`sdk`, `sound`, `studio`, `animator`, `mesh`, `pixel`, `code`, `assistant`; `/bm/...` on the RGB30)
**by file name only** (`carts.c:389-420`, `devtool.c:20-48`); `devtool_session` runs the SD copy instead of the embedded
tool with `bm_set_tool(1)` (`devtool.c:67-81`), i.e. with write access to any path and no permission questions. A game
that created `/carts/pixel.bm` would be run as bm Pixel the next time the user opens it (unless a real SD copy already exists, which
a game cannot replace). Read from the code; **not tried**. The Market is signed and curated, which is the mitigation today;
a game from another source is not.

| Option | Pros | Cons | Breaks? |
|---|---|---|---|
| S1a Refuse the reserved tool names in `write_target` (also `.b16`, 8.3 aliases such as `assistan`) | a few lines; closes the hole | a list to keep in step with the tools (see 2) | no |
| S1b A header flag `BM_FLAG_TOOL` (bit 2 at offset 18) set only by `mkbm.py --tool` and by the build of the embedded tools, checked by the runtime | the right travels with the file | a new bit; the SD copies of the tools must be rebuilt | no (old kernels ignore it) but old SD tools lose their rights until rebuilt |
| S1c Run an SD copy as a tool only if its SHA-256 is in the signed Market catalog | no format change | needs a trust store on the console | no |

Other points:

| Id | Problem | Option | Breaks? |
|---|---|---|---|
| A2 | The list of apps and tools was in 7 places; since 2026-10-13 the **file names of the tools are one list**, `src/kernel/toolnames.h` (used by the Pi's menu, the RGB30's and `reserved_tool_name`); still apart: the Market's two lists (`catalog.c`, `mkmarket.py`), `EMBED_TOOLS` and `embed.S`, all checked against it by `scripts/consistency.py` | generate the Market's lists from the same table | no |
| A3 | `devtool_session` finds the tool by comparing the display string (`"SDK"` on the Pi, `"bm SDK"` on the RGB30): rename the string and the SD override stops silently | pass the tool id | no |
| A4 | Six near-identical `build.py` (kitchen, titan, titan_ex, overbit, nano8, sdk_b16) with different features | one `scripts/joinlua.py`; or the SDK joins the project's files itself from `bm.info` | no |
| A5 | No module system for the game's own files (only a convention of `do ... end` blocks) | keep it; a runtime module system would need several LUA sections | yes, for old kernels |
| A6 | `bm.info` adopted by 3 games of ~13; INFO is stored but the menu does not read it | migrate the rest; read `name` from INFO in the Lib and the Market | no |
| A7 | Save identity is CRC32(title, author), checked only for Market games (`mkmarket.py:253`) | add an `id` key to INFO and use it for new saves, falling back to the CRC | no (old saves keep working) |
| A8 | The tools do not agree on their libraries: Code requires `assist`, `predict`, `padtype`, `bmui` without `pcall` (it fails to start if one is missing); SDK, Mesh, sdk_b16 wrap them | one pattern for the optional ones | no |
| A9 | Suspend only for menu-started games; a game tried from a tool is not suspendable; one global slot | document it; a second slot is a design change | no |
| A10 | `cart_load` and others read any `.bm` a game can name, so a Market game could read other cartridges' Lua (UNVERIFIED how `fat_find` restricts the path) | check, then restrict reads to the game's own file for `tool_mode` 0 | possibly |
| A11 | Docs: `GAME-GUIDE.md` §2 names `carts/ball/main.lua`, which does not exist; `games-architecture.md:168-173` links `docs/progress/suite-*.md`, the files are in `docs/design/` | fix | no |

**Suggested order:** S1a now (security, trivial); A3, A11, A6; then A2 and A4; S1b/S1c and A10 need a decision.

## 8. Not verified

How Market ids become file names and whether a Market game called `studio` is blocked; the `tool_mode` checks of
`cart_read`, `cart_audio`, `cart_sheet`; the contents of `docs/design/suite-*.md` and the lifecycle sections of
`GAME-GUIDE.md` (not read in full); `sdk_b16/build.py` and `nano8/build.py` internals; `scripts/market.sh`, `mksd.py`, `mkrelease.py`.
