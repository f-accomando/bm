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
  adopt, the copy on the card); a game's SHA-256 in 64 KiB pieces (`catalog_hash_tick`);
  a game checked and being written (`finishing`) goes on off the tab, nothing else does.
  Separates **Games** and **Apps** pages (`market_page`, toggled with `Y` / `V` or clicking the
  subtabs on `TITLE_ROW`), tracks `.bm` and `.b16` editions independently, and displays `.bm` /
  `.b16` format badges on cards and details panel. The title bar shows the name only.
  A game in memory has a star at the top right of its card; a game with a newer
  Market version has a down triangle at the bottom right (`market_has_update()`). The Market
  cards have no "Installed" or "Update" pill.
  Cards have square corners, a 9 px gap, and a selection ring 3 px out with a 3 px line.
- Updates keep old kernels in `/bm/backup`; nothing is written before files are downloaded
  and checked; the board's own kernel last. Check and install run in a fiber of the menu
  (`update_start`, `update_tick`; the Updates panel lists the files, `update_notice` the
  install's box and its 3 s); publishing too (`publish_tick`, the Send row and its help).
  The monitor's `u` runs `update_check`/`update_install` straight through.
