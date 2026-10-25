# Aligning the RGB30 kernel (kernel8) with the Pi kernel

State of 25 Oct 2026, checked against the code. Policy (user, 25 Oct, `docs/spec/CONSISTENCY.md`): the Pi Zero is the template. Port what the RGB30 lost. Keep what the RGB30 needs to be different (the square 360x360 screen, the pad and no keyboard, multicore multitasking). Decide each divergence one by one. The list is `docs/spec/divergences.toml`; the gate is `python3 scripts/consistency.py gate` (12 blockers on 25 Oct before B1; after B1, B2 and B3: D03, D05, D14, D15, D17, D24 and the spec folders-bm-bme; after the answers of 25 Oct, 5: D03, D05, D15, D17 and folders-bm-bme).

Classes: PORT (the RGB30 gets what the Pi has), KEEP (a need: write the reason in `note` and use `cause = "hardware"`), DROP (gone from both), ASK (a product decision of the user).

## Table

| Id | Area | Pi | RGB30 now | Class | Action | Size | Check |
|---|---|---|---|---|---|---|---|
| D03 | players | 4 BT pads plus USB (`src/usb/hid.c:445`, `src/kernel/input.c:225`) | all pads merge into player 1 (`src/rgb30/bm_input.c:95-103`); Settings shows "Player 1" only | ASK 1 | port the Pi slots: built-in pad is P1, BT pads P2-4 | S-M | device; host test of the merge |
| D05 | menu pointer | mouse block in the menu (`src/kernel/carts.c`) | no pointer in `src/rgb30/ui.c`, but Settings shows the Mouse rows | ASK 2 | port the pointer block (`pointer.c` is built) or hide the rows | M / S | QEMU, device |
| D10 | covers | background fiber job, visible covers first (`src/kernel/library.c`, shared by both menus since 25 Oct) | the same job (`lib_covers_tick`); a placeholder until the cover is read | done 25 Oct (QEMU) | the header read stays in the scan (the sort needs titles); the job stops before a game starts | M | QEMU; load time on the device |
| D14 | DMA test | Settings > System > Utilities | not built | dropped 25 Oct (removed) | note: the DMA test exercises the BCM2835 DMA engine; capability `soc-dma` in `platforms.toml` | S | `consistency.py check` |
| D15a | Settings > System "3D driver" row | shows the bm3d version | missing | done 25 Oct (QEMU) | row from the backend text of `src/rgb30/bm_port.c` | S | QEMU screen text |
| D15b | CPU clock and temperature | `prop_query` | CPU name only | ASK 9 | read the RK3566 TSADC and the clock | M | device |
| D17 | WiFi password | typed on a keyboard | `config.txt` only | PORT (blocked) | after the on-screen keyboard (R1, R2) | L | QEMU, device |
| D23 | 3D on the GPU | V3D | `mali3d=1` path exists | done 25 Oct: closed, merged into D45 | close D23 as merged into D45 | L | device |
| D24 | Overbit `.b16`, Mali benchmark | | superseded by decisions 9 and 10 of the single-bm design | ASK 5 (default remove) | move to the game backlog (`docs/progress/graphics-mali.md`) | S | |
| D51 | cartridge type app/utility | `is_dev()`, no splash | no type field; `loading_begin` always runs | done 25 Oct (QEMU) | store `bm_type_of_head` during the header read; list non-game types in Dev, not in Games; skip the splash | S-M | QEMU |
| spec compat-policy | targets line, load message, RGB30 `market_bm` | | all three exist (`market.c`, `runtime.c`) | done 25 Oct: `implemented` | run the tests, then `implemented` | S | QEMU Market tests |
| spec cart-type | type grouping | | note fixed 25 Oct | done 25 Oct: `implemented` | set `implemented` after D51 | S | QEMU |
| spec folders-bm-bme | `/carts`, `/glb`, `/bmm` | | same on both kernels | done 25 Oct (QEMU) | neither menu scans `/carts`; `/glb` and `/bmm` are APP-9's folders (the menus never list them) | S | QEMU |
| D01 | PS, Ctrl+Esc, Esc | reach home | code done | untested | try on the device | | device |
| D02 | BT at boot | `bt_start` | code done | untested | WiFi and BT share the RTL8821CS | | device |
| D18 | crash screen | 60 s restart | code done | untested | force one crash | | device |
| D26 | SDK 720x720 | | layout in the code | untested | run `SDK_B16_RES=720x720` | | device |
| D43 | network ops T and G | shared | G worked on 20 Oct | untested (G proven 25 Oct) | T open | | device |
| D45 | Mali `mali3d=1` | V3D | flat-colour phase 2 | untested | triangle test and 3D Bench | L | device |
| D50 | `web_get` | shared | same sources | untested | real HTTPS server | | device |
| D22, D48 | monitor | Monitor in Dev and System | `.no_monitor = 1` | ASK 3 | the "no keyboard" cause is weak: BT keyboard, serial and TCP console exist | L | QEMU, device |
| D52 (new) | Dev tab | tools only; 3D Bench in Settings > Screen | Dev also holds 3D Bench, Render bench, Display, Input test, Boot log (`ui.c:709-714`); the last three duplicate Settings rows | done 25 Oct (QEMU) | Dev = bm SDK plus installed tools; 3D Bench through an RGB30 `home_util_*` table in Settings > Screen | S-M | QEMU |
| D53 (new) | Settings > System > Utilities | lists the DMA test | shows "0" and an empty page | done 25 Oct (QEMU) | fixed by D52; hide the row when the count is 0 | S | QEMU |
| D54 (new) | Dev "Assistant" and "Lua" | present | none | KEEP (done 25 Oct) | no keyboard: the Lua prompt is on the serial line; the assistant runs inside the SDK; capability `no-keyboard`; revisit with D22 | S | |
| D55 (new) | Settings > Screen | Test pattern | Screen modes with test images, Low battery icon | KEEP (done 25 Oct) | capabilities `square-panel`, `battery` | S | |
| D25 | multicore | 1 core | 1 core, cause `todo` | KEEP (done 25 Oct) | `cause = "hardware"`, capability `multicore` (SYS-10 phase 6) | S | `consistency.py check` |
| D49 | `mo` / `mc` remap | A and B fixed | remap, default B and Y | KEEP (done 25 Oct) | `cause = "hardware"`, capability `pad-only`; fix `docs/progress/platforms.md:73-76` | S | |
| D11 | Publish | yes | off by owner decision (22 Oct) | ASK 6 | | S-M | QEMU, device |

## Batches

1. **B1, docs only (S):** D23 into D45; D24 if ASK 5 says yes; compat-policy `implemented` after its tests; cart-type note; D54 and D55 as KEEP; D25 and D49 re-tagged; D43 G proven; D14 as KEEP after ASK 4. Then `consistency.py check` and `gate`; expected about 7 blockers.
2. **B2, small code, QEMU (S-M):** D52 with D53; D15a; D51.
3. **B3, D10 background covers (M):** QEMU test, then a timing report from the console.
4. **B4, one device session for the untested items:** D01, D02, D18, D26, D43 (T), D45, D50. Numbers through the `reports` branch.
5. **B5, after the user's answers:** D03, D05, D11, D15b, the folders item, D22.
6. **B6, large (L):** D17 (needs the on-screen keyboard); the Mali work (D45 and the D23 merge).

## Decisions of the user (answered 25 Oct 2026)

## Game library (25 Oct 2026, QEMU only)

The Pi menu and the RGB30 menu share one game library: `src/kernel/library.c` and `library.h` (decision D2: this place; decision D3:
the RGB30 menu behaviour may change in the merge). The functions that existed twice are now one:

| Was in `carts.c` and `src/rgb30/ui.c` | Now in `library.c` |
|---|---|
| `title_cmp`, `same_text` / `same_ci` | `lib_text_cmp`, `lib_same_text`, `lib_title_cmp` |
| `scan_dir`, `read_header` / `load_head`, the covers kept by `rescan` | `lib_scan`, `lib_add_builtin` |
| `carts_find_title`, `carts_has_path`, `find_path` / `game_of_path` | the same two names, `lib_find_path` |
| `read_cover`, `next_cover`, `covers_main`, `covers_tick` | the same code, `lib_covers_tick`, `lib_covers_stop`, `lib_cover_focus` |
| `is_suspended`, `susp_path` / `susp_name`, `need_switch`, the two questions | `lib_is_suspended`, `lib_need_switch`, `lib_suspend_*`, `lib_switch_question`, `lib_compat_question` |
| `save_size`, `save_delete` / `game_save_bytes` | `lib_save_size`, `lib_save_delete` |
| `cart_panel` / `game_panel` | `lib_panel`, `lib_panel_open` |
| `cart_act` / `game_act` | `lib_act` (Publish stays in `carts.c`) |
| `play` / `play_bm` | `lib_play` |
| `menu_idle` | `lib_menu_idle` |
| `tab_items`, `cart_at`, `is_dev`, `is_project`, `kind_of` | `lib_tab_items`, `lib_is_dev`, `lib_is_project`, `lib_kind` |

What each console gives (`lib_backend_t`): the folders (Pi: `/`, `/carts`, `/bm`, `/bme`; RGB30: `/bm`, `/carts`), the compat target,
`want` (RGB30: `.b16`, `.bm` unless `show_bm=0`, no tool cartridge), `add_builtins` (Pi: the built-in tools), `stop_background`,
`panel` (`LIB_CAP_*`), `jobs` with their shares, `write_waits`, `before_jobs`, `play_begin`.

Where the Pi and the RGB30 differed, the Pi won, except for a hardware need. The decisions:

1. The RGB30 skips files whose name starts with a dot (the Pi did). It reads each header once, in the scan (it read the file twice).
2. Titles and authors lose control characters (CP437 font), as on the Pi. A file that is not a cartridge gets no save path.
3. The RGB30 keeps the covers it has read across a rescan (the Pi did), and shows a placeholder until the job reads a cover (it made a
   label for every game at the scan). The count in "first frame, K of N covers read" counts the kept ones.
4. A rescan on the RGB30 stops the same background work as the Pi's `background_stop` (a file from the PC, the covers, the WiFi try,
   the SD map).
5. The frozen game is known by its path (the RGB30 used the file name).
6. The panel of a game on the RGB30 has the Pi's texts and order ("Open in the SDK", "bm (native)"), and the File row. It reads the
   save data once, when it opens (the RGB30 read the SD card on every frame). The rows the RGB30 does not have stay out by
   `LIB_CAP_*` and a hardware reason: the editors (no keyboard), the editable copy (they open from an editor), Publish (D11, owner).
7. Delete and Export stop the background work first, and say the Pi's words ("Delete NAME?", "deleted PATH"). A save that cannot be
   deleted gives the first error.
8. `lib_play` writes both log lines on both consoles ("playing NAME" and "play: PATH (N bytes)"), and sets the crash crumbs (the RGB30
   had none). A file that is not a cartridge shows an error page on the RGB30 (no console to read), a console line on the Pi.
9. The two menus list the tabs with `lib_tab_items` in the order of the library (by title); the RGB30 no longer sorts games first.
10. The shares of the menu's free time stay as each console had them (not measured on a device).

What stays different on purpose: the folders; the tool cartridges (listed in Dev on the Pi, kept out of Games on the RGB30 and opened
through its own pages); `show_bm`; the panel rows by `LIB_CAP_*`; the job shares; the splash call (`play_begin` on the RGB30, the
launch step on the Pi); the main loops (`carts_menu` with the steps `cm_*`, `ui_home` with `uh_*`).

Settings: `settings.c` asks the console through small functions (`home_wireless`, `home_restart`, `home_poweroff`,
`home_poweroff_help`, `home_poweroff_detail`, `home_test_help`, `home_connect_help`, `home_controls_changed`, `home_has_battery`),
defined in `home.c` and `src/rgb30/ui.c`. The `BM_RGB30` blocks of `settings.c`: 42 before, 31 after.

## Decisions of the user (one line each, recommended default in brackets)

1. D03 players on the RGB30: PORT the Pi slots (built-in pad is P1, BT pads P2-4). D03 stays open. The code is a later work order.
2. D05 menu pointer: PORT the pointer block to the RGB30 menu. D05 stays open. The code is a later work order.
3. D22, D48, D54 monitor: after v1.0.0. The cause stays `hardware` for now. The shared command core is `src/kernel/monitor_cmd.c`.
4. D14 DMA test: REMOVE it from the Pi too. The driver `src/drivers/dma.c` stays (the framebuffer, the audio and the runtime use it). D14 closes as dropped. Done 25 Oct 2026. The code removed: `src/kernel/dmatest.c` and `dmatest.h`, the Settings > System > Utilities row, the Monitor `bench dma` (`D`) command, the QEMU test `test_dma`, the docs.
5. D24 Overbit: removed from the divergences. It is in the game backlog (`docs/progress/graphics-mali.md`, `docs/progress/overbit.md`).
6. D11 Publish on the RGB30: stays off (owner decision of 22 Oct). D11 is accepted.
7. Utility cartridges: in the Dev tab on both kernels.
8. `/carts` folder: both kernels stop scanning `/carts` now (done 25 Oct: `carts.c`, `src/rgb30/ui.c`, and every writer of games moved to `/bm`). `/glb` and `/bmm` move to APP-9: they stay `/glb` and `/bmm`, the folders APP-9 defines (`docs/progress/tools.md`); the menus never listed them.
9. D15b CPU clock and temperature on the RGB30: PORT (the RK3566 TSADC and the clock readback, device-tested; a later work order).
10. D17 WiFi password typing: wait for the on-screen keyboard (R1, R2).

Other answers: `gpu3d_later` (queue 2) and `gpu3d_sort` stay off by default. `gpu3d_qpu` becomes on by default on the Pi, with the ARM fallback, after a 10 minute soak of Yharnam (the user runs the soak; the default does not change before it).
