# Aligning the RGB30 kernel (kernel8) with the Pi kernel

State of 25 Oct 2026, checked against the code. Policy (user, 25 Oct, `docs/spec/CONSISTENCY.md`): the Pi Zero is the template. Port what the RGB30 lost. Keep what the RGB30 needs to be different (the square 360x360 screen, the pad and no keyboard, multicore multitasking). Decide each divergence one by one. The list is `docs/spec/divergences.toml`; the gate is `python3 scripts/consistency.py gate` (12 blockers: D03, D05, D10, D14, D15, D17, D23, D24, D51 and the specs compat-policy, cart-type, folders-bm-bme).

Classes: PORT (the RGB30 gets what the Pi has), KEEP (a need: write the reason in `note` and use `cause = "hardware"`), DROP (gone from both), ASK (a product decision of the user).

## Table

| Id | Area | Pi | RGB30 now | Class | Action | Size | Check |
|---|---|---|---|---|---|---|---|
| D03 | players | 4 BT pads plus USB (`src/usb/hid.c:445`, `src/kernel/input.c:225`) | all pads merge into player 1 (`src/rgb30/bm_input.c:95-103`); Settings shows "Player 1" only | ASK 1 | port the Pi slots: built-in pad is P1, BT pads P2-4 | S-M | device; host test of the merge |
| D05 | menu pointer | mouse block in the menu (`src/kernel/carts.c`) | no pointer in `src/rgb30/ui.c`, but Settings shows the Mouse rows | ASK 2 | port the pointer block (`pointer.c` is built) or hide the rows | M / S | QEMU, device |
| D10 | covers | background fiber job, visible covers first (`carts.c`) | every cover decoded in `scan_games` (`ui.c:280-292`) | PORT | keep the header read synchronous (sort needs titles); move the cover decode into a `fiber_job_t`, visible covers first; stop it before a game starts | M | QEMU; load time on the device |
| D14 | DMA test | Settings > System > Utilities | not built | ASK 4 (default KEEP) | note: the DMA test exercises the BCM2835 DMA engine; capability `soc-dma` in `platforms.toml` | S | `consistency.py check` |
| D15a | Settings > System "3D driver" row | shows the bm3d version | missing | PORT | row from the backend text of `src/rgb30/bm_port.c` | S | QEMU screen text |
| D15b | CPU clock and temperature | `prop_query` | CPU name only | ASK 9 | read the RK3566 TSADC and the clock | M | device |
| D17 | WiFi password | typed on a keyboard | `config.txt` only | PORT (blocked) | after the on-screen keyboard (R1, R2) | L | QEMU, device |
| D23 | 3D on the GPU | V3D | `mali3d=1` path exists | done 25 Oct: closed, merged into D45 | close D23 as merged into D45 | L | device |
| D24 | Overbit `.b16`, Mali benchmark | | superseded by decisions 9 and 10 of the single-bm design | ASK 5 (default remove) | move to the game backlog (`docs/progress/graphics-mali.md`) | S | |
| D51 | cartridge type app/utility | `is_dev()`, no splash | no type field; `loading_begin` always runs | PORT | store `bm_type_of_head` during the header read; list non-game types in Dev, not in Games; skip the splash | S-M | QEMU |
| spec compat-policy | targets line, load message, RGB30 `market_bm` | | all three exist (`market.c`, `runtime.c`) | done 25 Oct: `implemented` | run the tests, then `implemented` | S | QEMU Market tests |
| spec cart-type | type grouping | | note fixed 25 Oct | ASK 7 | set `implemented` after D51 | S | QEMU |
| spec folders-bm-bme | `/carts`, `/glb`, `/bmm` | | same on both kernels | ASK 8 | move `/glb` and `/bmm` to APP-9 | S | |
| D01 | PS, Ctrl+Esc, Esc | reach home | code done | untested | try on the device | | device |
| D02 | BT at boot | `bt_start` | code done | untested | WiFi and BT share the RTL8821CS | | device |
| D18 | crash screen | 60 s restart | code done | untested | force one crash | | device |
| D26 | SDK 720x720 | | layout in the code | untested | run `SDK_B16_RES=720x720` | | device |
| D43 | network ops T and G | shared | G worked on 20 Oct | untested (G proven 25 Oct) | T open | | device |
| D45 | Mali `mali3d=1` | V3D | flat-colour phase 2 | untested | triangle test and 3D Bench | L | device |
| D50 | `web_get` | shared | same sources | untested | real HTTPS server | | device |
| D22, D48 | monitor | Monitor in Dev and System | `.no_monitor = 1` | ASK 3 | the "no keyboard" cause is weak: BT keyboard, serial and TCP console exist | L | QEMU, device |
| D52 (new) | Dev tab | tools only; 3D Bench in Settings > Screen | Dev also holds 3D Bench, Render bench, Display, Input test, Boot log (`ui.c:709-714`); the last three duplicate Settings rows | PORT | Dev = bm SDK plus installed tools; 3D Bench through an RGB30 `home_util_*` table in Settings > Screen | S-M | QEMU |
| D53 (new) | Settings > System > Utilities | lists the DMA test | shows "0" and an empty page | PORT | fixed by D52; hide the row when the count is 0 | S | QEMU |
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

## Decisions of the user (one line each, recommended default in brackets)

1. D03 players on the RGB30: A port the Pi slots (built-in pad is P1, BT pads P2-4); B single player as a hardware difference. [A]
2. D05 menu pointer: A port the BT mouse pointer to the RGB30 menu; B hide the mouse rows; C A plus the right stick as the pointer. [A]
3. D22, D48, D54 monitor: A port the terminal monitor to both kernels as a 1.0 blocker; B after 1.0; C never on the RGB30. [B]
4. D14 DMA test: A Pi only as a hardware difference; B remove from the Pi too. [A]
5. D24 Overbit: A remove from the divergences and move to the game backlog; B keep as a 1.0 blocker. [A]
6. D11 Publish on the RGB30: A port it; B keep it off. [A]
7. Utility cartridges: A the Dev tab on both kernels; B Settings > System > Utilities on both. [A]
8. `/carts` folder: A read `/carts` on both until 1.0 and move `/glb` and `/bmm` to APP-9; B stop scanning `/carts` now. [A]
9. D15b CPU clock and temperature on the RGB30: A write the TSADC and clock readback; B CPU name only, as a hardware difference. [A]
10. D17 WiFi password typing: A wait for the on-screen keyboard; B `config.txt` only, as a hardware difference. [A]
