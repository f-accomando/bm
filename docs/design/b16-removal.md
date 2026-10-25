# Removing the `.b16` system

State of 25 Oct 2026 (plan only; nothing of this is built, except where a line says "exists"). Decision of the user (25 Oct): one cartridge format, `.bm`, for the Pi and the RGB30; games adapted over time; resolutions: only the black-bars version is built and enabled (G1). Design background: [`single-bm-and-adaptive-screen.md`](single-bm-and-adaptive-screen.md) (questions 3, 7, 8, 9 and 10), [`../progress/b16.md`](../progress/b16.md).

## Facts (from the code)

- The Pi already boxes a square header: `bm_video_enter` (`src/bm/runtime.c`) puts 360x360 in 640x360, which gives bars of 140 pixels on each side.
- The RGB30 already letterboxes any header: `fb_init_game` fills with the same shape (`src/rgb30/fb.c`), so 640x360 shows as 720x405. The centring is not verified.
- Only the `screen()` list on a square panel (`square_modes`) has no 16:9 entry. `safe_area()` and `aspect()` do not exist.
- The compat block, the menu greying and the Market `targets` line exist. A `.b16` gets Pi plus RGB30 only through the extension and flag fallback (`src/bm/format.c`). So a rename to `.bm` without `targets` would make the game Pi only.
- No `bm.info` declares `targets:` today and no Makefile rule passes `--targets`.
- 1453 hits of `b16` in 237 files (counted without build, dist and .git): the Makefile (63), `rgb30.mk`, `scripts/mkbm.py`, `mkmarket.py`, `mklint.py`, the kernel menus (`carts.c`, `market.c`, `devtool.c`, `src/rgb30/ui.c`), `format.c`, `project.c`, `lint.c`, `catalog.c`, the editors (`carts/editor`, `carts/sdk_b16`), the games (Overbit, Titan EX, Stages 3D, Yharnam), the tests, the specs (`compat-policy.toml`, `lint-rules.toml`, `config-keys.toml`), the web player (`web/bmcart.js`, `app.js`), the bm-forge skill, the docs and the manuals (en and it). CI has none.

## Policy

- **Old `.b16` files:** a read-only legacy loader. The kernels keep listing, loading and installing `.b16` and read them as targets Pi plus RGB30. No tool writes `.b16` any more. No rename on install (no SD write the user did not ask for; FAT 8.3 names). `webpolicy.c` keeps refusing `.b16` writes. Review the loader at v1.0.0.
- **Market:** one `.bm` per game id, filtered by `targets`. The old `.b16` entries stay in `bm-market` for one release for old kernels. New kernels key the owned records by id only. After one release the user deletes the `.b16` files from `f-accomando/bm-market`.

## Work orders (host H, QEMU Q, device D)

| Order | What | Verification | Needs |
|---|---|---|---|
| W1 | Inventory table and key facts into `docs/progress/b16.md` | docs audit | none |
| W2 | `screen()` gets the 720x405 mode on a square panel (third entry; `screen(1)` stays 360x360, `screen(2)` 720x720 because Overbit indexes the list) | H: `tests/host/screen_modes.lua`; Q: screendump test on the RGB30 virt build | none |
| W3 | Lua `safe_area()` and `aspect()` (`rt.design_w/h` from the header) | H: the same cart, 360x360 design at 640x360 gives 140,0,360,360 | none |
| W4 | The build lint runs for any cart whose compat block lists the RGB30; the "a .b16 is 360x360 or 720x720" and "targets list only ..." warnings go (bars make any size valid) | H: `make test-lint`, `test_mkbm_b16`, `test_mklint` | none |
| W5 | `mkbm.py` reads `targets`, `input`, `aspect`, `needs`, `min_ram`, `min_spec`, `min_width` from `bm.info` (a flag wins; a key without `targets` is an error) | H: `test_mkbm_compat` | none |
| W6 | A `.bme` carries `targets`; `bm_build` writes the compat block into the `.bm` | H: `make test-bm` | W5 first |
| W7 | Yharnam, Titan EX and Stages 3D become single `.bm` for both kernels (G1 bars): `targets: pi,rgb30`, key config redone to ok/back | Q: `test_yharnam_bars` (Pi screendump: columns 0-139 and 500-639 black), RGB30 start without confirm; D: a 5 minute hunt on both kernels | W5; user device run |
| W8 | Market: one entry per id; `catalog.c` dedupes by id only; delete `show_b16` and `want_b16`; `market-seed` adds `.bm` for overbit, yharnam, titanex, stages3d | H/Q: `test-catalog`, `run_catalog_test.py`, Market QEMU tests; the fixture `pocket.b16` still lists | W7 |
| W9 | Remove "Export .b16" and the edition labels from both menus; the RGB30 lists `.bm` always (compat greys them); the Type row shows the targets | Q: RGB30 and Pi menu tests (change the asserts on `racer.b16`, keep one legacy test) | W5 |
| W10 | One `overbit.bm`: header 480x270, `targets: pi,rgb30`, run-time `aspect()=="wide"` replaces `OVERBIT_TV_BM`, HUD and pause laid out in `safe_area()` | Q: HUD screendump; D: a match on both kernels | decision 4; user device run |
| W11 | The RGB30 SDK becomes a `.bm` with `targets: rgb30` (Market id `sdk-rgb30`); the target toggle bm/b16 becomes a targets row; "Export .b16" goes from both SDKs | H/Q: `test-sdk-b16` (renamed `test-sdk-rgb30`), `test-studio`, SDK QEMU tests | W6, decision 5 |
| W12 | Delete every `.b16` writer: `bm_build_b16`, `BM_FLAG_B16` (kept as a read constant), `--b16`, `mark_b16`, the Makefile rules, `carts/stages3d/b16/`, `test_mkbm_b16.py` (renamed) | `make`, `make TARGET=rgb30`, `test-lint test-compat test-catalog test-sdk-rgb30`, QEMU `-k "sdk or compat"` | W7, W9, W11 |
| W13 | The web player and the bm-forge skill write `.bm` only and keep reading `.b16` | `make test-webplayer`; bm-forge golden tests | W12 |
| W14 | Docs, the `CLAUDE.md` rule ("One cartridge type, `.bm`. The header declares the kernels."), `docs/B16.md` to the archive, manuals en and it, `src/ai/kb/guide_sdk.txt` and `make ai-model` | docs audit, `manuals.py check`, `make test-ai`, `consistency.py check` and `gate` | W12 |
| W15 | The user deletes the `.b16` files from `bm-market` one release after W8 | `mkmarket.py ... --check` returns 0 | the user |

W1 to W5 are independent and safe. Order: W1-W5, then W6, then W7, W9, W11, then W8, W12, then W13, W14, then W15.

## Decisions of the user (recommended default first)

1. Legacy `.b16` loader: keep it read-only until v1.0.0, then review it. No rename on install.
2. Lint target: the lint runs for a cart that lists the RGB30; the screen-size and one-kernel warnings are deleted (W4).
3. Where a `.bme` stores targets: in the INFO key `targets:`, as `bm.info` does (W5, W6).
4. Overbit on the RGB30: the 480x270 game shows 16:9 at 720x405 with bars and the HUD sits in `safe_area()` (the other choice, a native 720x720 HUD, is G2, for new games only).
5. RGB30 SDK: keep two cartridges until one elastic SDK exists; `carts/sdk_b16` becomes a `.bm` with `targets: rgb30` and the Market id `sdk-rgb30`.
6. Titan: `titan_ex` (360x360) becomes the Pi plus RGB30 `.bm` with the id `titanex`; `carts/titan` stays.
7. Stages 3D: keep the `bm/` 640x360 front; delete the `b16/` front and `test-stages3d-b16` after W7.
8. Config keys: delete `show_bm`, `show_b16` and `market_bm` (greying replaces them); keep `compat=off` as the owner override.
