# runtime.c: where things are

`src/bm/runtime.c` is ~350 KB and 9400 lines: never read it whole. Find the section here,
then `python scripts/cindex.py src/bm/runtime.c <word>` for the line, then `Read` with
`offset`/`limit` on that stretch only. Line numbers are not kept here: they go stale; the
banner text does not. What the runtime does and its rules: [lua](lua.md) (*Runtime*).

## Find a place

| Looking for | Command |
|---|---|
| a Lua function a game calls (`rect`, `spr`, `btn`…) | `Grep -n 'static int l_<name>'`; the name→`l_…` table is `api[]` (`Grep -n 'luaL_Reg api'`) |
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
- `skeletal animation`: `animate()`; same arithmetic as `carts/animator/main.lua`.
- `collision worlds`: `world3d` boxes, rays, moving bodies.
- `the network for the games`: UDP sockets over `src/net/cartnet.h`, lockstep commands.
- `save() / saved()`: the 8-slot Lua-source saves in `/bm/save`.
- `documents`: `/docs` files for bm Write.
- `sound`: audio bindings, bank, `au_parse`.
- `keys`, `light`, `state`, `input`, `typing`: key/pad/mouse reading, nano8 light, game state.
- `loading`: cartridge load, `.bm` sections, resources.
- `editor`: the in-console code editor (largest block).
- `player`: the cartridge loop: `_init`, `_update`, `_draw`, page flip, frameskip.
- `the dev kit`, `F12: the keys`: profiler, session report, debug keys.
- `permissions`: `allow_…` questions (network, `report()`).
- `the debugger`: breakpoints, inspector.
- `the frame in its parts`: overlay and session report.
- `C bench`: built-in benchmarks.

## Other big files

See [reading](reading.md) for `menu_ui.c`, `settings.c`, `bm3d.lua`, `bmlib.lua`.

## Keeping this file

A new banner in `runtime.c` gets one line here, in file order. Renamed or removed banner:
fix the line in the same commit. No line numbers, no history.

## Open work (Runtime APIs)

- **Accented letters in `print()`**: text is rendered in CP437 (`g16_text` in `gfx16.c`); implement transparent decoding of multibyte UTF-8 sequences into CP437 glyphs to avoid wrong glyphs with accents ("città", "perché").
- **Battery handling in games**: integrate the `battery()` and `battery_low()` states with the runtime library to provide `lib.autosave` at the first critical-battery warning in long games.

