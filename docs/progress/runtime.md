# runtime.c: where things are

`src/bm/runtime.c` is ~352 KB and 9282 lines: never read it whole. Find the section here,
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

Order in the file. Sizes are a guide: `editor` and `the debugger` are the big ones.

- `helpers`: small shared helpers (colour, value readers, sheet commit).
- `API`: the bulk of the `l_*` bindings: 2D, text, tables, `stat`, `later`.
- `the map`: map layers of equal size, drawn per frame (R11).
- `named zones of the sheet`: SPRITES section zones (bm Pixel).
- `prompts`: on-screen prompts and dialogs used by the system.
- `3D (software rasterizer`: `l_*` for meshes, camera, `flush3d`, `draw2d`; GPU hand-off.
- `documents`: `/docs` files for bm Write.
- `the web`: `web_get`, `image_load`, `file_write` for a browser cart.
- `sound`: audio bindings, bank, `au_parse`.
- `keys`, `light`: key/pad/mouse reading, nano8 light.
- `video clips (.bmv)`: clip player and recorder clips (`vid.c`, `rec.c`).
- `state`, `input`, `typing`: game state, input, typing.
- `loading`: cartridge load, `.bm` sections, resources.
- `editor`: the in-console code editor (largest block).
- `player`: the cartridge loop: `_init`, `_update`, `_draw`, page flip, frameskip.
- `the dev kit`, `F12: the keys`: profiler, session report, debug keys.
- `permissions`: `allow_…` questions (network, `report()`).
- `the debugger`: breakpoints, inspector.
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

## Other big files

See [reading](reading.md) for `menu_ui.c`, `settings.c`, `bm3d.lua`, `bmlib.lua`.

## Keeping this file

A new banner in `runtime.c` gets one line here, in file order. Renamed or removed banner:
fix the line in the same commit. No line numbers, no history.

## Open work (Runtime APIs)

- **Accented letters in `print()`**: text is rendered in CP437 (`g16_text` in `gfx16.c`); implement transparent decoding of multibyte UTF-8 sequences into CP437 glyphs to avoid wrong glyphs with accents ("città", "perché").
- **Battery handling in games**: integrate the `battery()` and `battery_low()` states with the runtime library to provide `lib.autosave` at the first critical-battery warning in long games.

