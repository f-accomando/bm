# System: menu and kernel services

Cut from [system](system.md): the menu and kernel services. Open work and rules stay in system.

### Menu and kernel services (`src/kernel/`)
- `carts.c` (the menu loop, launches, `background_stop()`), `menu_ui.c` (the one menu, Pi
  and RGB30), `settings.c`, `syskeys.c`, `splash.c` / `loading.c`, `market.c`, `update.c`,
  `reports.c`, `ledstate.c`, `notice.c`, `prompts.c`, `monitor.c`.
- Splash only for games: the suite (SDK, editors, Sound, Studio, Animator, Mesh, Pixel,
  projects) opens without it (`suite` in `carts.c`).
- Bar icons: controllers, keyboard and mouse without a player number; Bluetooth keeps a blue
  dot (`ICON_DOT`, `status_icons()` in `menu_ui.c`).
- Market job (`market.c`): `fiber_slice()` between the steps of the catalog (check, parse,
  adopt, the copy on the card); a game's SHA-256 in 64 KB pieces (`catalog_hash_tick`);
  a game checked and being written (`finishing`) goes on off the tab, nothing else does.
  Separates **Games** and **Apps** pages (`market_page`, toggled with `Y` / `V` or clicking the
  subtabs on `TITLE_ROW`), tracks `.bm` and `.b16` editions independently, and displays `.bm` /
  `.b16` format badges on cards and details panel. The title bar shows the name only.
- The Dev tab holds the editors only: bm Code, Assistant, Monitor, Lua (`NDEV` in `home.c`). On the RGB30 (`src/rgb30/ui.c`) it holds the bm SDK, then the cartridges of type app or utility (D51, D52); its utilities are the 3D Bench (Settings > Screen) and the Render bench (Settings > System > Utilities); the Utilities row is hidden when the count is 0 (D53); Settings > System has the "3D driver" row (D15a). The
  benchmarks and tests are utilities (`home_utils()`): the 3D Bench in Settings > Screen;
  DMA test in Settings > System > Utilities. Input test, Audio test, Test pattern, System and Log are
  Settings rows. The GPU test, Texture Room, Video bench, Stack bench and Diagnostics (with the CPU
  bench, the libc and fiber self-tests, the boot demo and the Lua boot script) were removed on 24 Oct 2026.
  The monitor keys `b`, `g` and `R`, and the line words `gpu`, `room`, `video` and `stack`, went with them.
  The monitor keys `k` (CPU bench), `p` (render bench) and `d` (animation demo) stay.
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
  enlarged by the HDMI scaler with zero CPU cost. With `menu_gpu=1`, the GPU (V3D) draws
  the 1920x1080 menu 1:1 directly using `font_console_24x48` and intermediate primitives
  (`m_rect`, `m_text`, `hspan`, `round_rect`, `round_ring`, `card`, `put_background`,
  `put_icon`, `put_prompt`, `m_pointer`), eliminating the 1.84 MB ARM frame copy
  (`enlarge 0 us`, ~35.8 ms draw, verified on hardware). With `menu_gpu=0`, the ARM falls
  back to 1x (or manual `menu_scale=2`/`3`).
