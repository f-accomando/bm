# System: menu and kernel services

Roadmap line: SYS-21 ([ROADMAP](../ROADMAP.md)).

Cut from [system](system.md): the menu and kernel services. Open work and rules stay in system.

### Menu and kernel services (`src/kernel/`)
- `library.c` (the game library of both menus), `carts.c` (the Pi menu: `carts_menu` calls
  the steps `cm_*`, launches, `background_stop()`), `menu_ui.c` (the one menu, Pi and RGB30;
  `menu_ui_frame` calls the steps `frame_*`), `settings.c` (`home_panel` calls `panel_*`,
  `home_act` calls `act_*`), `syskeys.c`, `splash.c` / `loading.c`, `market.c`, `update.c`,
  `reports.c`, `ledstate.c`, `notice.c`, `prompts.c`, `monitor.c`. The RGB30 menu is
  `src/rgb30/ui.c` (`ui_home` calls the steps `uh_*`).
- The game library (`library.c`, `library.h`): one `library` (`lib_t`, up to 64 `lib_game_t`) for
  the Pi and the RGB30. Each console gives a `lib_backend_t`: the folders, the compat target,
  `want` (which file is listed), `add_builtins`, `stop_background`, the rows of the cartridge
  panel (`LIB_CAP_*`), the jobs of the menu's free time and `play_begin`. The library does the
  scan (`lib_scan`), the type rule (`lib_is_dev`, `lib_tab_items`), the covers (a fiber job, the
  tab's covers first), the frozen game (`lib_is_suspended`, `lib_suspend_*`), the save data, the
  panel (`lib_panel`), the rows that stay in the menu (`lib_act`), `lib_play` and
  `lib_menu_idle`. Still apart: the two main loops (input, questions, launches).
- Splash only for games: the suite (SDK, editors, Sound, Studio, Animator, Mesh, Pixel,
  projects) opens without it (`lib_is_dev`; the Pi menu shows it in `cm_launch`, the RGB30 in
  `rgb_play_begin`).
- Bar icons: controllers, keyboard and mouse without a player number; Bluetooth keeps a blue
  dot (`ICON_DOT`, `status_icons()` in `menu_ui.c`).
- Market job (`market.c`): `fiber_slice()` between the steps of the catalog (check, parse,
  adopt, the copy on the card); a game's SHA-256 in 64 KB pieces (`catalog_hash_tick`);
  a game checked and being written (`finishing`) goes on off the tab, nothing else does.
  Separates **Games** and **Apps** pages (`market_page`, toggled with `Y` / `V` or clicking the
  subtabs on `TITLE_ROW`), tracks one entry per game id, and shows the format badge on cards and details panel. The title bar shows the name only.
- The bottom bar of the menu shows the key chips only (user request, 25 Oct 2026), on the Pi and the
  RGB30. It has no details line, no last-game note, no status and no counters. `menu_view_t` has no
  `details` and no `note`. A failed action shows its reason in the notice box (`notice_fail()`,
  4 s). Every other message goes to the console log (`home_report()` in `settings.c`, `market.c`).
  The help line of a panel row is inside the Settings box, not in the bar: it stays.
- The Dev tab holds the editors only: bm Code, Assistant, Monitor, Lua (`NDEV` in `home.c`). On the RGB30 (`src/rgb30/ui.c`) it holds the bm SDK, then the cartridges of type app or utility (D51, D52); its utilities are the 3D Bench (Settings > Screen) and the Render bench (Settings > System > Utilities); the Utilities row is hidden when the count is 0 (D53); Settings > System has the "3D driver" row (D15a). The
  benchmarks and tests are utilities (`home_utils()`): the 3D Bench in Settings > Screen.
  The DMA test was removed on 25 Oct 2026. Input test, Audio test, Test pattern, System and Log are
  Settings rows. The GPU test, Texture Room, Video bench, Stack bench and Diagnostics (with the CPU
  bench, the libc and fiber self-tests, the boot demo and the Lua boot script) were removed on 24 Oct 2026.
  The monitor keys `b`, `g` and `R`, and the line words `gpu`, `room`, `video` and `stack`, went with them.
  The monitor keys `k` (CPU bench), `d` (animation demo) and `p` (render bench) were removed on 28 Oct 2026 with `bench.c`, `demo.c` and `bm_bench` (the RGB30 Settings row *Render bench* went too).
  The monitor is a table of commands (`commands` in `src/kernel/monitor.c`). Each single key is the shortcut of a
  word (`i` is `sys info`, `W` is `wifi`, `j` is `bench 3d`). The `bm>` prompt (`:` and Enter) stays open and
  has history, Tab and line editing (`src/kernel/monitor_cmd.c`). The old line words stay as aliases.
  A game in memory has a star at the top right of its card; a game with a newer
  Market version has a down triangle at the bottom right (`market_has_update()`). The Market
  cards have no "Installed" or "Update" pill.
  Cards have square corners, a 6 px gap, and a selection ring 3 px out with a 3 px line.
- Updates keep old kernels in `/bm/backup`; nothing is written before files are downloaded
  and checked; the board's own kernel last. Check and install run in a fiber of the menu
  (`update_start`, `update_tick`; the Updates panel lists the files, `update_notice` the
  install's box and its 3 s); publishing too (`publish_tick`, the Send row and its help).
  The monitor's `u` runs `update_check`/`update_install` straight through.
- Menu resolution and scaling (`menu_ui.c`): 1x (640x360) by default on the Pi Zero W,
  enlarged by the HDMI scaler with zero CPU cost. The ARM draws only this 360p menu and never enlarges it.
  `menu_scale=4` (640x360), `2` (1280x720) or `3` (1920x1080), Settings > Screen > Menu size, make the GPU (V3D) draw the menu
  at that size, 1:1 with `font_console_24x48` at 3 and intermediate primitives
  (`m_rect`, `m_text`, `hspan`, `round_rect`, `round_ring`, `card`, `put_background`,
  `put_icon`, `put_prompt`, `m_pointer`), with no frame copy (`enlarge 0 us`, 35.8 ms draw at 1080p in the first test; 73.5 ms on 28 Oct on the cover grid, see below;
  720p not yet verified). When the GPU cannot start, the menu stays at 360p on the ARM (26 Oct 2026; `menu_gpu` is gone).

- **Menu on the GPU, cost per frame (Pi Zero W, 28 Oct 2026, [`docs/bench/menu-1080p-probe/`](../bench/menu-1080p-probe/README.md)).** Cause of the high cost: the V3D texture cache had 8 slots (`NTEX`), and the menu uses more than 8 sheets (background, covers, fonts), so each frame made the textures again in turn; the cost did not depend on the resolution (the background cost 20 ms at 360p and 27 ms at 1080p). Fix: `NTEX` 32, and the background sheet is copied only when what shows changes (`bg_sig` in `menu_ui.c`). Draw time a frame on the cover grid: 360p 31.9 -> 2.9 ms (the ARM draws 360p in 6.6 ms), 720p 47.8 -> 6.3 ms, 1080p 73.5 -> 12.5 ms. Open: the scenes were not seen by eye; Yharnam on the Pi with 32 slots: 52.3 fps against 51.6 with 8 (no slowdown; the 3D Bench and the free memory are not measured); the free time for the jobs (`free time used`) is still about 30 us, check where the ARM waits.
- **Native menu layout (trial, 28 Oct 2026, key `menu_layout=1`, with `menu_scale` 2 or 3).** The layout has the screen's own size (1280x720 or 1920x1080) instead of the 640x360 layout drawn bigger: covers and fonts keep their size in pixels, 8 covers a row (6 at 360p), as many rows as fit, the grid centred, the key chips centred in the bottom bar. The background is still made at 640x360 and drawn bigger (`bgz`). Not done in the native layout: the Settings page and the panels (positions written for 360 high), the title and the tabs (left aligned). Decide the default after a look on the TV. On 29 Oct 2026 the options box of a cartridge and the submenu of Settings are half as wide there and in the middle (`PANEL_W`, `PANEL_X` and `draw_page` in `menu_ui.c`; the 360p layout is unchanged). Checked only at 360p in QEMU (it has no V3D, so it never draws the native layout): look at it on the TV. The question boxes (`ask`) are still wide.
- **Covers in a better resolution (SYS-18, idea).** Today a cover is `MENU_CARD` x `MENU_CARD` (88 x 88) in the cartridge. Plan: the cartridge carries a larger cover (for example 256 x 256); the menu makes the size for the screen from it (88 at 360p, a larger size at 720p and 1080p) and keeps the small one for the lists; the Market and the SD scan give the same cover. Check the cost in the cartridge size and in the cover loader (`covers` job) on the Pi Zero W before the format changes (`docs/BM-FORMAT.md`).

