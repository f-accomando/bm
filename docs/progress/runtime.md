# runtime.c: where things are

`src/bm/runtime.c` is ~150 KB and 3821 lines: never read it whole. Find the section here,
then `python scripts/cindex.py src/bm/runtime.c <word>` for the line, then `Read` with
`offset`/`limit` on that stretch only. Line numbers are not kept here: they go stale; the
banner text does not. What the runtime does and its rules: [lua](lua.md) (*Runtime*).

## Find a place

| Looking for | Command |
|---|---|
| a Lua function a game calls (`rect`, `spr`, `btn`…) | `Grep -n 'int l_<name>('`; the name→`l_…` table is `api[]` (`Grep -n 'luaL_Reg api'`) |
| any function or banner of the file | `python scripts/cindex.py src/bm/runtime.c <word>` |
| one section (below) | `Grep -n -A3 '^/\* -+ <banner words>' src/bm/runtime.c` |
| who calls a symbol across files | `Grep -n '<symbol>' src/` (one call, `head_limit` 30) |
| a visible string (menu, prompt, error) | `Grep -n '"<text>"' src/` |

## Sections (banner text to grep)

Order in the file. Sizes are a guide: `the frame in its parts` is the big one.

- `helpers`: small shared helpers (colour, value readers, sheet commit).
- `API`: the bulk of the `l_*` bindings: 2D, text, tables, `stat`, `later`.
- `3D (software rasterizer`: `l_*` for meshes, camera, `flush3d`, `draw2d`; GPU hand-off.
- `state`, `input`, `typing`: game state, input, typing.
- `loading`: cartridge load, `.bm` sections, resources.
- `project state`: the project open in the editor (`proj_cover`, `proj_audio`, `proj_sheet8`), `reserved_tool_name` (it reads `bm_toolnames`), `res_name` and `rt_res_name()`, `rt_api()`.
- `player`: the cartridge loop: `_init`, `_update`, `_draw`, page flip, frameskip.
- `the dev kit`, `F12: the keys`: profiler, session report, debug keys.
- `the frame in its parts`: overlay and session report.
- `C bench`: built-in benchmarks.

## Platform interface

- `src/bm/platform.h` lists every kernel, USB, network and AI header that the runtime uses. `runtime.c`, `loading.c`, `rec.c`, `webfile.c` and `webget.c` include only this header. It holds plain declarations: the Pi kernel, the RGB30 port and bmhost link by symbol name. A file that includes it needs `-Ithird_party/lua` (`ai/net.h` includes `lua.h`).
- `src/kernel/crt.h` declares the four `crt_*` functions of `settings.c`. `home.h` and `platform.h` include it.
- `src/bm/rt_internal.h` is private to `src/bm`. It declares `rt` (`rt_t`), `d2` (`d2_t`) and `held` (`held_t`), the shared macros and enums, the helpers `col`, `ival`, `oval` and `sheet_commit`, and the functions that one area calls in another (`l_*`, `draw_prompt`, `perf_end` and others). Those functions are not `static`. The definitions of `rt`, `d2` and `held` stay in `runtime.c`.
- `api[]`, `res_name`, `screen_modes`, `square_modes` and the `bm_toolnames` use stay in `runtime.c`. `scripts/consistency.py`, `tests/tools/test_mkapi.py` and `test_meshcap` read that text.

## Files split from runtime.c

Each file holds one banner range of `runtime.c`, moved as it was. It shares the run state through `rt_internal.h`. `api[]` still lists the `l_*` functions in `runtime.c`.

- `src/bm/api_world.c`: collision worlds (`world3d`), `log()` and `report()`.
- `src/bm/api_anim.c`: skeletal animation (`animate()`, same arithmetic as `carts/animator/main.lua`) and the 3D bindings after the rasterizer (`draw3d`, camera, light, particles, `gpu3d()`). `screen()` with `screen_modes` stays in `runtime.c`.
- `src/bm/api_net.c`: UDP sockets for the games over `src/net/cartnet.h`.
- `src/bm/api_save.c`: `save()` and the 8-slot Lua-source saves in `/bm/save`. `saved()` is in the `screenshots` range of `runtime.c`.
- `src/bm/api_shot.c`: `shot()` and the PNG save (`docs/SCREENSHOTS.md`), `config()`, and `saved()`, `saves()` and `delsave()`.
- `src/bm/api_docs.c`: `/docs` files for bm Write (`doc_list`, `doc_read`, `doc_write`, `doc_delete`).
- `src/bm/api_sound.c`: audio bindings (`note`, `sfx`, `music`, `tone`, `play`, `reverb`, the bank).
- `src/bm/api_light.c`: nano8 light and fades (`light_begin`, `light_end`, `fades`, `dark_begin`, `dark_end`, `glow`), and `online()` and `quit()`.
- `src/bm/api_map.c`: map layers (`map`, `mget`, `mset`, `mflags`, `mlayers`), cell flags (`fget`, `fset`) and the named zones of the sheet (`zone`, `zones`, `zboxes`, `zspr`, `zstack`).
- `src/bm/api_prompts.c`: button and key prompts (`prompt`, `lastinput`), buttons by name (`btn`, `btnp`, `keymap`, `controls`, `controller`, `players`, `stick`), and `camera`, `clip`, `rgb`, `time`, `stat`, `frameskip`, `tri`.
- `src/bm/api_keys.c`: keyboard, mouse and pad reading (`keyp`, `keyheld`, `keydown`, `keys`, `rawkeys`, `keyhelp`, `showkeys`, `mouse`, `mousep`, `pad`) and `timeslice()`.
- `src/bm/api_web.c`: the web for a browser cart (`web_get`, `web_poll`, `web_info`, `web_body`, `web_cancel`), images (`image_load`, `image_file`, `sheet_png`, `sheet_gif`) and files (`file_write`, `file_state`).
- `src/bm/api_video.c`: video clips (`.bmv`): `video_open()` and the clip object (`info`, `frame_at`, `draw`, `close`). `video_to_ram` stays in `runtime.c`.
- `src/bm/api_perm.c`: permissions: the `allow_…` questions (network, `report()`, docs), `bm_permissions()`, `perm_allowed()` and `copy_question()`. The screen of the run (`perm_fb`) stays in `runtime.c`.
- `src/bm/api_editor.c`: the cartridge files for the editors: `ls`, `cart_load`, `cart_sheet`, `cart_new`, `cart_save`, `cart_build`, `cart_read`, `cart_write`, `cart_run`, `cart_tool`, `cart_arg`, `cart_audio`, `cart_put_audio`, `cart_data`, `cart_meshes`, `mesh_reduce`, `res_write`, `res_read`, `picture3d`, `cutout3d`, `glb3d`, the `bm_set_arg*`, `bm_take_tool`, `bm_take_run` and `bm_last_error` calls, and `write_target`. The state shared with the player is defined in `runtime.c` (`rt_internal.h` declares it).
- `src/bm/api_text.c`: `sget()`, `sset()`, `print()` and `font()`. `proj_sheet_drawn` stays in `runtime.c` (8 users).
- `src/bm/api_debug.c`: the debugger (R13): breakpoints, `breakpoint()`, the stack and variables over the game. The state `dbg` (type `dbg_t`) is defined in `runtime.c`; the notice, the battery and the F12 keys help, which follow the debugger in the file, stay in `runtime.c`.

## Other big files

See [reading](reading.md) for `menu_ui.c`, `settings.c`, `bm3d.lua`, `bmlib.lua`.

## Keeping this file

A new banner in `runtime.c` gets one line here, in file order. Renamed or removed banner:
fix the line in the same commit. No line numbers, no history.

## Open work (Runtime APIs)

- **Accented letters in `print()`**: text is rendered in CP437 (`g16_text` in `gfx16.c`); implement transparent decoding of multibyte UTF-8 sequences into CP437 glyphs to avoid wrong glyphs with accents ("città", "perché").
- **Battery handling in games**: integrate the `battery()` and `battery_low()` states with the runtime library to provide `lib.autosave` at the first critical-battery warning in long games.

