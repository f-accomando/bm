# Consistency between the kernels, the formats and the versions

One product on two kernels (Raspberry Pi Zero W and PowKiddy RGB30) built at different times: the menus, the Settings, the
keys and the formats drifted apart, and nothing noticed. This page is the rule that stops it and the list of what is still
apart. **Everything must converge before version 1.0.0.**

## The principle

> The two kernels behave **the same**, and the formats mean the same on both, **unless a capability of the hardware
> really differs** (a square screen, a pad and no keyboard, no mouse, no USB host, a different GPU...). A difference
> that comes from a listed capability is accepted; every other difference is a defect.

A defect is written down in `docs/spec/divergences.toml` the day it is found and **blocks 1.0.0** until it is resolved.

### Which kernel is the template (user, 25 Oct)

The Pi Zero W is the template: a feature is born there. Some of its work was lost on the RGB30 (`cause = "history"`): that is a defect, port it. Some work was born on the RGB30 and had to be different (`cause = "hardware"`), and stays: the square 360x360 screen is one; multitasking is another (the Pi Zero W has one core and the hardware limits it; the RGB30 has several cores, so its multitasking may be optimised further, SYS-10 phase 6). Decide each divergence one by one: port it to the other kernel, keep it as a hardware difference (write the reason in `note`), or drop it. One game runs on both devices (see `docs/design/single-bm-and-adaptive-screen.md`, question 10).

## Three numbers, three meanings

| Number | What it says | Where | Changes when |
|---|---|---|---|
| **Kernel version** | which build is on the card | `git describe` (`bm_version`); after 1.0.0 a `vX.Y.Z` tag | every release |
| **Specification revision** (`BM_SPEC_REV`, now 5) | what a cartridge or a tool may rely on: formats, API, system keys, config keys, platform behaviour | `src/bm/bm.h`, `bmSPEC=N` in `kernel.img`, Settings > System, `docs/spec/specs.toml` | something a cartridge can depend on is added |
| **Container version** (header offset 8, now 1) | the layout of the file | `src/bm/bm.h` | only when an old kernel must refuse the file: a break |

A cartridge that uses something newer than revision 1 writes `spec: N` in its `bm.info`; a kernel with a lower revision
refuses it with *"this game needs revision N of the bm specification: update the kernel"* instead of failing half way
(`make test-spec`). The history before 2026-10-11 was reset: the old tags `v0.1.0`-`v0.2.5` are not in the current line, so
today the kernel version is a commit hash; revision 1 is the baseline of that day.

## The files

| File | What it holds |
|---|---|
| `docs/spec/specs.toml` | the revisions and the items of the specification, each with its revision, status (`implemented`, `partial`, `planned`), files and platforms |
| `docs/spec/config-keys.toml` | every key of `bm/config.txt` the kernel reads (60 keys and 3 patterns): scope, values, default, the Settings row |
| `docs/spec/platforms.toml` | the two platforms and the **capabilities** in which they really differ |
| `docs/spec/compat-policy.toml` | per kernel, whether a cartridge shows, greys or hides (editable rows); read by `scripts/bm_weight.py` and, as the C table `src/bm/compat_table.h` (`scripts/mkcompat.py --check`), by both kernels; see [devkit-weight](../progress/devkit-weight.md) |
| `docs/spec/divergences.toml` | every known difference between the kernels, with its cause: `hardware`, `history`, `todo` or `unknown` |
| `docs/spec/known.toml` | the differences found by the automatic rules that are accepted for now, with a reason |
| `scripts/consistency.py` | the rules, run by `make test-consistency` |

## What the script checks

`python3 scripts/consistency.py check` (fails on a drift): the cartridge resolutions in the kernel, `mkbm.py`, the header
comment, the docs and `runtime.c`; the section numbers in C and in the three Python tools; the header flags; the functions
the runtime registers against `docs/api/*.md`; the names of the apps and tools in the seven places that list them (and
that a game cannot create a file with a tool's name); the menu tabs, `screen()` modes and embedded tools of the two
kernels; that `BM_SPEC_REV` is the registry's last revision; that every config key the code reads is in the registry with
the right scope; that every divergence names a capability and real evidence files.
`report` adds the accepted ones. **`gate` lists what still blocks 1.0.0.**

## Rules for a change

1. **Shared code first.** Menu drawing, Settings rows, the runtime, the Market, updates and reports are shared
   (`menu_ui.c`, `settings.c`, `runtime.c`...). The menu *logic* is not: the Pi's is `carts.c`, the RGB30's a separate
   `rgb30/ui.c`: a feature added to one must be added to the other **in the same change**, or written in
   `divergences.toml` as `history`/`todo`.
2. **Platform code only for a capability.** If the code differs, name the capability (`platforms.toml`) in a comment and in
   `divergences.toml` (`cause = "hardware"`).
3. **A new API function** goes in `docs/api/*.md`; **a new config key** in `config-keys.toml`; **a format change** in
   `bm.h`, `format.c`, `mkbm.py`, `bmres.py`, `bmmesh.py` and `docs/BM-FORMAT.md` together. `make test-consistency` fails otherwise.
4. **If a cartridge or a tool can depend on it**, add an `[[item]]` to `specs.toml` with a new revision (and raise
   `BM_SPEC_REV`); a cartridge that uses it says `spec: N`.
5. **Inputs through the semantic calls**: `pad_ok` / `pad_back` for confirm and back, never a fixed button
   (RGB30: B confirms, A backs).
6. **Both kernels in the test**: `bmhost` for the shared runtime, `--square` for the RGB30's screen.

## The gate for 1.0.0

`python3 scripts/consistency.py gate` must print **0 blockers**: no open divergence of cause `history`, `todo` or `unknown`
(only `hardware` may stay; a divergence with status `accepted` and a `known.toml` entry of kind `intended` are accepted in
writing, with their reason), no item of the specification that is not `implemented`, no accepted drift except hardware and
documentation noise. From 1.0.0 on: container version 1 stays readable by every 1.x kernel; a new revision only **adds**;
removing or changing a promise is a 2.0.0.

## Where we are (2026-10-22, read from the code; `resolved` = proved by a test or tried, the record's note says which and what is not verified on the hardware; `untested` = fixed and compiled, not tried; `accepted` = intended, with its reason in the record)

**1. First: devices that silently stop working**

| Id | Status | Cause | Area | What differs | What to do |
|---|---|---|---|---|---|
| D01 | untested | history | input / system keys | Pi: BT pad PS button, BT keyboard Ctrl+Esc and Esc reach the menu (home) and the games. RGB30: only Start+Select leaves; PS/Esc are never read | forward hid_quit_pressed() in input_players; make the menu honour PS = home |
| D02 | untested | history | bluetooth | Pi: bt_start() at boot when a pad is paired: the pad reconnects. RGB30: BT starts only when a pairing row is opened: a paired pad, keyboard or mouse does not reconnect after a reboot | call bt_start() at boot if bt_paired() |

**2. Data-loss risks in the menu**

| Id | Status | Cause | Area | What differs | What to do |
|---|---|---|---|---|---|
| D06 | resolved | history | launch | Pi: asks to close a game that is frozen (suspended) before starting another. RGB30: closes it silently: the frozen game's progress is lost | add the question |
| D07 | resolved | history | frozen game | Pi: shows a 'running' badge and a Close row. RGB30: no badge, no Close row | set the badge and add the row |
| D08 | resolved | history | delete a game | Pi: closes the suspended copy; refuses while a download is updating it. RGB30: does neither | port the two checks |

**3. One key, one default**

| Id | Status | Cause | Area | What differs | What to do |
|---|---|---|---|---|---|
| D16 | resolved | history | Market formats and show_bm / show_b16 | Both kernels: one entry per id, the .bm; an old .b16 only without a .bm twin (keys removed 26 Oct 2026) | one key, one default; document it in RGB30.md and B16.md |

**4. Developer workflow**

| Id | Status | Cause | Area | What differs | What to do |
|---|---|---|---|---|---|
| D09 | resolved | history | game list | Pi: sorted by title; scans / and /bm; shows .bme projects in Dev. RGB30: sorted by title; scans /bm; .bme projects are D44 (neither scans /carts since 25 Oct 2026) | try the title order on the RGB30 |
| D13 | resolved | history | Dev tab and Market Apps | Pi: tools recognised by name (tool_id_of), apps installed from the Market appear in Dev. RGB30: 7 fixed entries; a Market App installed on the RGB30 is not recognised as a tool and shows up as a game | same recognition on the RGB30, or filter apps out of Games |

**5. Robustness**

| Id | Status | Cause | Area | What differs | What to do |
|---|---|---|---|---|---|
| D10 | open | history | covers | Pi: loaded in the background, on-screen covers first. RGB30: all loaded synchronously at each scan (up to 64) | port the background cover job |
| D18 | open | history | crash screen | Pi: red screen, restart after 60 s (after 20 s of uptime), report at the next boot. RGB30: red screen, LED code, halts for ever; no report | port the restart and the report |

**6. Decide first whether it is wanted on that console**

| Id | Status | Cause | Area | What differs | What to do |
|---|---|---|---|---|---|
| D03 | untested | todo | players | Pi: up to 4 BT pads plus USB: local multiplayer. RGB30: same since 25 Oct 2026 (built-in pad = P1, BT pads P2-4) | try a Bluetooth pad on the RGB30 |
| D04 | resolved | history | menu keys | Pi: Start confirms; Tab and 1-4 jump tabs; U uploads a cart. RGB30: Start does nothing in the menu; no keyboard keys | align Start and Tab for BT keyboards |
| D05 | untested | todo | menu pointer | Pi: mouse: hover, click, wheel, right-click for options. RGB30: same since 25 Oct 2026 (menu_ui_pointer) | try a Bluetooth mouse on the RGB30 |
| D11 | accepted | history | game options (X / C) | Pi: Open in the tools, editable copy, Build, Publish, Send to a nearby console, Close. RGB30: Play, Open in bm SDK, Send to a nearby console, info, delete save, delete | add Publish (the owner decided Publish stays off on the RGB30); the tool rows depend on D13 |
| D12 | resolved | history | cover details | Pi: author, kind, size, path; a performance note after a game. RGB30: only the hint line; no performance note | add the note |
| D14 | resolved | todo | Dev tab extras | Pi, in Settings: 3D Bench (Screen). RGB30: the 3D Bench in the Dev tab | dropped (25 Oct): the DMA test is removed from the Pi |
| D15 | open | todo | Settings > System | Pi: CPU clock and temperature; 3D driver row; open the monitor. RGB30: CPU name only | write the RK3566 TSADC and clock readback; decided 25 Oct |
| D17 | open | todo | WiFi connect | Pi: scan and type the password with a keyboard. RGB30: only wifi_ssid / wifi_psk from config.txt; no on-screen keyboard | waits for the on-screen keyboard (R1/R2); decided 25 Oct |
| D19 | open | history | SD logs | Pi: none (only the crumbs report). RGB30: bootlog.txt, bootprev.txt, lastrun.txt in bm/ | give the Pi the same three files (R3) |
| D20 | resolved | hardware | release content | Pi: manifest.txt: kernel.img, kernel7.img, games in /carts, ca.pem. RGB30: manifest-rgb30: kernel8.img and ca.pem only | none: intended; kernels ship bare (exception: kernel8.img has the bm SDK; since 26 Oct 2026 kernel.img is built clean and the bm suite comes from the Market); accepted by the user, 21 Oct 2026 |
| D21 | open | todo | run from the PC without saving | Pi: bm_net.py --play wired (GO_NETPLAY). RGB30: not wired | wire it into ui_home (developer feature) |
| D22 | open | hardware | monitor | Pi: text console and Dev tab entry. RGB30: none by design (no_monitor); the serial line has a Lua REPL | the user builds a terminal monitor for the RGB30 after the beta; study: mini-HDMI detection, external monitor, BT keyboard and mouse |
| D23 | open | todo | 3D on the GPU | Pi: V3D draws the 3D. RGB30: Mali-G52 present but the 3D is on the ARM | Mali driver |
| D25 | accepted | todo | multicore, fibers | Pi: -. RGB30: RGB30 uses one core; fibers in the menu only | later |
| D26 | untested | todo | SDK layout 720x720 | Pi: -. RGB30: not done (R39) | R39 |
| D27 | accepted | todo | screenshot of the menu's own screens | Pi: not done. RGB30: not done | extend the screenshot to the menu |
| D28 | open | todo | a game built in /carts and the Games tab | Pi: a game built in /carts is not in Games (R41). RGB30: - | R41 |
| D44 | resolved | todo | Dev tab lists .bme projects | Projects (.bme) are not listed in the menu tabs on either kernel (user decision 21 Oct 2026). Pi Dev tab fixed in PR 58 (src/kernel/carts.c tab_items; test_projects walks the Dev tab). | none |

The differences that are hardware, with the capability that explains each:

| Id | Area | Capability | What differs |
|---|---|---|---|
| D30 | screen | `square-panel` | Pi: 16:9 HDMI: menu 640x360 shown 2x at 1280x720 with the GPU (else 1x), 6 covers a row, menu_scale. RGB30: square 720x720 panel: menu 360x360 shown x2, 3 covers a row |
| D31 | game screen | `square-panel` | Pi: square carts boxed in 16:9 black. RGB30: fb_init_game fills the panel; screen() offers only 360x360 and 720x720; bm_scale, bm_smooth |
| D32 | confirm / back | `pad-only` | Pi: A confirms, B backs. RGB30: B confirms, A backs; confirm=a swaps |
| D33 | game buttons | `pad-only` | Pi: DS4 by position. RGB30: built-in pad by the letters printed on it; game_buttons=position |
| D34 | system keys | `no-keyboard` | Pi: F11, F12 (held: the key list), Print Screen, Ctrl+Esc. RGB30: only on a BT keyboard; the pad has Select held 3 s (screenshot), Start+Select (leave) and Start held + L1+R1 (small dev kit) |
| D35 | text input | `no-keyboard` | Pi: USB or BT keyboard and mouse. RGB30: BT only, no USB host; no on-screen keyboard |
| D36 | storage paths | `storage-path` | Pi: cartridges in /bm and /, data in /bm (since 25 Oct 2026; /carts is not read). RGB30: everything in /bm |
| D37 | 3D settings | `no-3d-gpu-yet` | Pi: no 3D row (the 3D of the games and the 3D on the ARM are hidden keys, defaults GPU and fast since 26 Oct 2026). RGB30: none |
| D38 | audio | `audio-codec` | Pi: HDMI only, stereo. RGB30: I2S to a codec: speaker or headphones, hardware volume keys |
| D39 | battery | `battery` | Pi: none; battery() is nil. RGB30: icon, low-battery overlay, Settings row, LED |
| D40 | network chip | `wifi-chip` | Pi: BCM43438 and Pi 1 Ethernet. RGB30: RTL8821CS: 2.4 GHz, open and WPA2-PSK, no Ethernet |
| D41 | boot and update path | `board-boot` | Pi: firmware boots kernel.img; manifest.txt. RGB30: U-Boot boots kernel8.img; manifest-rgb30; a wrong-board kernel is refused |

Also open in the specification (`gate`): the partial items compat-policy, cart-type and folders-bm-bme (see `specs.toml`).
Resource files and the common project layout are `implemented` since BETA-6 round 2.

## Decisions needed

- Version numbering: a baseline tag for the current line (for example `v0.3.0`) and `v1.0.0` when `gate` is empty. Tagging is
  outward-facing: waiting for a yes.
- D49 is accepted (BETA-6 round 2): the Pi menus keep A and B (D32); only the RGB30 remaps the menus' OK and Cancel with `mo` and `mc`. If you want the Pi menus to follow them, say so: the entry goes back to `todo`.
- D16 is closed (26 Oct 2026): the keys `show_bm`, `market_bm` and `show_b16` are removed; both kernels list one entry per id.
- The Lib tab: removed from the Pi kernel on 2026-10-25 (owner's decision; it was hidden since 2026-10-06). Both kernels have the same tabs. The tools on the RGB30 come from Market > Apps and are recognised as tools (D13).
