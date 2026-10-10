# Lua and runtime

The Lua 5.4 VM, the cartridge runtime, the libraries built into the kernel and the dev kit.
The API itself (every function, arguments, examples) is in [`docs/API.md`](../API.md) and
its copies (`docs/API-IT.md`, `docs/GAME-GUIDE.md`, `docs/GUIDA-GIOCHI.md`): not repeated
here.

## How it is today

### VM (`third_party/lua/`, `src/script/luavm.c`)
- Stock Lua 5.4 with one patch: `luai_cprof` in `third_party/lua/ldo.c` (a hook on C calls
  for the function profiler; NULL costs one test).
- `luavm.c` is the kernel's own state (boot script, REPL), capped at `LUA_MEM_LIMIT` 64 MiB.
- Each cartridge gets a fresh state in `src/bm/runtime.c`: generational GC (`LUA_GCGEN`),
  an instruction-count hook, globals `SCREEN_W`/`SCREEN_H`, wave constants, the API table.

### Runtime (`src/bm/runtime.c`, `runtime.h`)
- One file holds the cartridge loop and almost every Lua binding (`l_*`). Loop: input,
  `_update` (fixed 1/60 s), `_draw`, 3D flush, page flip; `_init` once.
- `frameskip(n)`: up to `n` `_update` before a `_draw` when a frame is late (`stat(15)` how
  many ran). `stat()` indices are documented above `l_stat`.
- `bm_stats_t` (`runtime.h`) is what a run returns: times, slow frames, Lua peak, assets,
  tokens, `left` (the player left, not an error).
- Tokens (`src/bm/tokens.c`, `stat(11)`, `code_tokens()`): information only, never a limit
  (`docs/B16.md` §2.4).
- Square cartridges (256×256, 360×360) are boxed on the 16:9 screen; `bm_video_enter` /
  `bm_video_leave` give the console's screen back as it was (`console_suspended()`).
- Projects and games (M47): `.bme` projects in `src/bm/project.c`; in `runtime.c`
  `write_target`, `copy_question`, `cart_build`; `cart_save`/`cart_write`/`cart_put_audio`
  return the file written. Rules in `CLAUDE.md`; test doubles
  `tests/studio/project_rules.lua`.
- `cart_save` writes back the file's SHEET8 section byte for byte (`proj_sheet8`, kept by
  `cart_load`) until `sset`/`cart_sheet` draw on the sheet (`proj_sheet_drawn`), then the whole
  sheet as SHEET: a big SHEET8 (Yharnam's) stays small. Test: `tests/gameapi/cart.lua`.
- nano8 (`src/bm/n8*.c`, `carts/nano8`): a PICO-8-compatible machine for `.p8` carts, its
  own Lua bindings in `n8lua.c` and sound in `src/audio/n8snd.c`.

### Libraries built in (`src/bm/require.c`, sources in `src/script/`, `src/ai/`)
`require` serves, once per cartridge: `bmlib` (games' shared kit: maths, collisions with map
flags, `lib.move`/`lib.step`/`lib.ray`, `lib.hits`, tweens and timers, particles, camera,
states, menus, `lib.split`/`lib.party`, saves), `bmnet` (lockstep over LAN or relay
`tools/overbit_relay.py`: `net.input`, `net.frames`, `net.check`), `bm3d` (Studio/Animator
toolkit), `bmui` (mouse in the tools), `riff` (music patterns, see
[audio](audio.md)), `predict`, `padtype`, `assist` (see [tools](tools.md)).

### Dev kit (`runtime.c`, `src/bm/profile.c`)
- Overlay: F11 cycles; `devkit(mode)` 0 off, 1 simple, 2 detailed, 3 functions. Settings
  sets the starting mode; F11/`devkit()` change only the current run.
- Function profiler (mode 3 or `profile(true)`): self and total ms per frame over the last
  second, C functions marked; uses the instruction hook plus `luai_cprof`.
- Debugger: `breakpoint([why])` and F8 in bm Code, in a game tried from a tool.
- Session report: at the end of a run one file per game,
  `reports/<branch>/session_<game>_<board>.txt`, replaced by the next run (`session_report`,
  `reports_session`): frame-time classes, the 12 longest frames with their parts, the game's
  `devinfo()` lines. `session_report=0|1` in `bm/config.txt` (default: only with a
  `github_token`). Sent from the menu.

## Open work (`docs/ROADMAP.md`)

- **M42** `.b16` profile: profile field and `mkbm.py --b16` (not there yet), fixed screen,
  palette, memory and sandbox, CPU budget 60 → 30 fps, SDK target, Yharnam then Overbit.
- **M39 step 10**: hot Lua of the games in C (rays, bot paths, particles), fewer allocations.
- **M47** projects: done on the PC, to verify on the Pi.
- Yharnam checks without a milestone ("Da fare" in the roadmap): frame cost on Pi and RGB30
  via the session report and `devinfo()`, a glitch in the first frames; its drawn map edited in
  the SDK on the Pi ([tools](tools.md)).

## Rules (do not break)

- New or changed API: all four API docs (same tables and examples) and `src/ai/kb/` (entries
  with `title_en:`/`text_en:`), then `make ai-model` and commit `assist.weights`; examples
  and `sdk/README.md` if needed.
- A new built-in library: an entry in `src/script/embed.S` and in `libs[]` of
  `src/bm/require.c`.
- In Lua `cond and nil or x` is always `x`.
- Tool pages: one `do … end` exporting into `P`, under 200 locals; a mouse function outside
  it does not see its locals (export them).
- Lockstep games (`bmnet`, Overbit): randomness with `grandom()`; no `G.frame`, camera,
  quality or state changed in `_draw`.
- System keys belong to `src/kernel/syskeys.c` (F12, Esc, Ctrl+Esc, F11, F6, F5/Ctrl+R): apps
  do not reuse them.
- Tests: `make test-gameapi`, `test-bm`, `test-profile`, `test-bmnet`, `test-online`,
  `test-loading`; QEMU `-k <name>` for the piece touched.
