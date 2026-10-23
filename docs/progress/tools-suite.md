# Tools: the on-console suite

Cut from [tools](tools.md): the suite and its editors. Open work and rules stay in tools.

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
  Tiled import (R11, both SDKs): menu *Import a Tiled map*, a `.tmj`/`.json` in `/docs`;
  `bm3d.tiled_import` and `tiled_apply` (`src/script/bm3d.lua`): orthogonal, CSV layers, 8x8 or
  16x16 tiles (a 16x16 tile is 2x2 cells), groups, boolean tile properties to flags 0-4, add or
  replace; host tests in `sdk_host.lua` and `sdk_b16_host.lua`. Not undoable (reopen the project).
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
- **bm Pixel** (`carts/pixel`): F1 draw, F2 sheet, F3 palette, F4 stack (stack zones, both layouts `stack` and `stackv`: slices one by one, the stack turning, make or change a zone, draw a slice on F1, onion skin `k`, copy a slice to the next `c` (one undo step), delete a zone `Del` twice; mouse: pick zone or slice, wheel, drag on the view turns it, click stops or starts it, right menu; save writes SPRITES with `cart_write` `sections[11]`, the layout is bit 0 of byte 27 of the zone). The page keeps its state in the table `sp` (the main chunk has 200 locals at most). Host test: the end of `tests/studio/pixel_host.lua` (`make test-studio`); hitboxes of BOXES on the canvas, previews and sheet view (toggle j).
- **bm Studio** (`carts/studio`): models from tiles and blocks (block, tile, select, vertex,
  paint), reduce. **bm Animator** (`carts/animator`): rig and skin (`t` test pose: [tools-assistant](tools-assistant.md)), keyframes, sprites from an
  animation (F4: bone hitboxes previewed and baked into SPRITES and BOXES). **bm Mesh** (`carts/mesh`): vertices and faces of MESH models and of meshes the
  game builds (`cart_meshes()`).
- **bm Sound** (`carts/sound`): SOUNDS, SFX, PATTERN, SONG pages; music assistant on F6
  ([audio](audio.md)).
- **bm Write** (`carts/write`, a Market app): `.BMD` documents in `/docs`, A4 pages,
  exports `.TXT`/`.MD`/`.HTM`/`.PDF`. The PDF equals the screen: host test `tests/studio/write_pdf_host.lua`
  (in `make test-studio`) exports a five-page document with every style, alignment, bold, italic, underline,
  accents, brackets and a backslash. It reads the PDF with its own small reader (xref, pages, streams) and
  compares each page with what the cart draws on a tall screen: the same runs of letters, the same place (screen
  pixels times 595/540 points, within 0.01 pt), the same Courier face, size and width, the underlines, quote bars,
  bullets and page numbers (493 checks, 389 runs). A change to the baseline or a bullet makes it fail. Not
  checked: a real PDF viewer (no `pdftotext` or `pdftoppm` in WSL; the test keeps the file with `luahost ... ROOT
  OUT.pdf`), the Pi and the RGB30.
- **Lib tab** (`src/kernel/lib.c`, `libview.c`): resources `.bmm .bmi .bms .bmt .bmc .bmk`,
  hidden until `lib_tab=1`.
- Shared Lua: `bm3d` (Studio/Animator; Mesh loads it only for the pure geometry `T.G`: `model_item`, `encode_model`, `orient`, `compact`... on the shape `{v, f, vb}`), `bmui` (mouse: zones registered while drawing,
  chips that press keys, `U.press` before `keyp()`, a context menu of keys).
- Projects (M47): tools edit `.bme` in `/bme`, never a `.bm`/`.b16`; saving a game asks for a copy. `bm_build` and
  `bm_build_b16` build from a `.bme` or a `.bm` into the games folder, never over the other edition. Layout, pickers and
  old cards: [`PROJECT-LAYOUT.md`](../PROJECT-LAYOUT.md). Runtime side in [lua](lua.md).

## Editor additions of 23 Oct (APP-17 to APP-28)
- **bm Code:** Ctrl+Shift+F (menu *Find in the project*) lists the hits of every open tab and module as "module:line text", 200 at most, Enter jumps (`U.find_all` in `bmui.lua`, `MOD.find_all`, `MOD.jump`). Ctrl+Shift+H (menu *Rename a word*) renames a whole word in all modules; strings, comments and numbers stay; one undo step per module; it does not check scope (`U.rename_word`, `U.lua_name_ok`).
- **bm Pixel:** `q` tile view (the sprite 3 x 3 with `spr`; sprites up to 32x32 fit). `w` wand select: `dp.sel.mask` (set keyed `y*rs+x`); copy, cut, clear, lift and flip act on the mask; `m` clears it. Ctrl+E (menu *Export a PNG to /docs*) exports the sprite, the animation strip or the sheet with `sheet_png` and `doc_write`; the name is 8.3, cut to 8 letters. The draw hint bar shows `b e g m w` and `Ctrl+E PNG`; the other tool keys are in the F12 help.
- **`sheet_png`** (`src/bm/shot.c`, `l_sheet_png`): PNG bytes with alpha of sheet rectangles; `make test-sheet-png`. `shot_png` and `shot_png_rgba` share `png_encode`.
- **bm Mesh:** Shift+O pivot of rotate and scale (centre, origin, base; `ed.pivot`, `ed.pivot_of`; not saved). Shift+V smooth (`T.G.smooth` in `bm3d.lua`, `ed.smooth`): one pass moves each chosen vertex 0.5 toward the mean of its edge neighbours; open borders and hidden vertices stay; live mirror; one undo step.
- **bm Animator:** Ctrl+Shift+I (menu row in the animate page) copies the animations of another model by bone name (`T.retarget`, `T.rig_height`): exact name, else lower case without `. _ -`; translations scaled by the rig height ratio; unmatched bones get the rest pose.
- **bm Write:** Table style (last in `ORDER`): `|` between cells, shared column widths, rows wider than 72 letters wrap as body text. Export to MD, HTML, TXT and PDF; MD import. The `.bmd` saves a row as `body table` with U+2502 bars, so an older kernel reads body text.
- **bm SDK:** `t` terrain brush on the map page (menu *Terrain brush*): the 4 x 4 tile block of the sheet from the current tile; mask N=1 E=2 S=4 W=8; Space paints, Backspace erases; each stroke re-tiles the cell and its 4 neighbours; one stroke is one undo (`T.autotile_*` in `bm3d.lua`). The `m overview` hint chip became `t terrain`. The RGB30 SDK has the same brush (APP-22): key `t`, a row in the X actions list, the `c` flags chip became `t terrain`.
- **bm Sound:** solo of a track (`a`, pad B on the track name; uses `mute()`, never saved, "S" mark and "SOLO" line) and a piano-roll view (`f`, pad B on a step; one track as pitch against time, 20 semitones, same data and same file bytes as the step grid; state in `pp`). No change to the AUDIO format or the C mixer.
- **`sheet_gif`** (`src/bm/shot.c` `shot_gif`, `l_sheet_gif`): animated GIF89a of sheet rectangles (own LZW, loop, delay, transparency, median cut above 255 colours, 262144 pixels at most, 1 to 64 frames); `make test-sheet-gif`. The encoder runs only when the user exports. bm Pixel Ctrl+E offers "the animation as a GIF" (delay `floor(100/fps + 0.5)` hundredths, 8.3 name `.gif`).
- Rules: the cart chunks of Code, Mesh and Pixel are near the 200-locals limit: put new functions in the existing tables (`MOD`, `ed`, `MK`, `shp`). Every hint bar ends with a fixed chip "F12 keys" at the right (`U.hint_row` in `bmui.lua` reserves its width first and cuts the other chips to what fits; a click calls `showkeys()`, which opens the key list until a click, F12 or Esc; hidden with a pad). A new key goes in the F12 help (`keyhelp`) and the commands list; the bar shows the top keys and a new feature never removes a chip. bm Code keeps its own F12 chip in the tab bar; bm Write shows the chip on its status line. The tests of bm Mesh read the commands list by position (loop cut near the top, ghost near the tail).
- Not verified on hardware: all of the above (host tests and QEMU only).
