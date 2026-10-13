# Consistency between the kernels, the formats and the versions

One product on two kernels (Raspberry Pi Zero W and PowKiddy RGB30) built at different times: the menus, the Settings, the
keys and the formats drifted apart, and nothing noticed. This page is the rule that stops it and the list of what is still
apart. **Everything must converge before version 1.0.0.**

## The principle

> The two kernels behave **the same**, and the formats mean the same on both, **unless a capability of the hardware
> really differs** (a square screen, a pad and no keyboard, no mouse, no USB host, a different GPU...). A difference
> that comes from a listed capability is accepted; every other difference is a defect.

A defect is written down in `docs/spec/divergences.toml` the day it is found and **blocks 1.0.0** until it is resolved.

## Three numbers, three meanings

| Number | What it says | Where | Changes when |
|---|---|---|---|
| **Kernel version** | which build is on the card | `git describe` (`bm_version`); after 1.0.0 a `vX.Y.Z` tag | every release |
| **Specification revision** (`BM_SPEC_REV`, now 2) | what a cartridge or a tool may rely on: formats, API, system keys, config keys, platform behaviour | `src/bm/bm.h`, `bmSPEC=N` in `kernel.img`, Settings > System, `docs/spec/specs.toml` | something a cartridge can depend on is added |
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
(only `hardware` may stay), no item of the specification that is not `implemented`, no accepted drift except hardware and
documentation noise. From 1.0.0 on: container version 1 stays readable by every 1.x kernel; a new revision only **adds**;
removing or changing a promise is a 2.0.0.

## Where we are (2026-10-13, read from the code; `untested` = fixed and compiled, not tried on the hardware)

**1. First: devices that silently stop working**

| Id | Status | Cause | Area | What differs | What to do |
|---|---|---|---|---|---|
| D01 | untested | history | input / system keys | Pi: BT pad PS button, BT keyboard Ctrl+Esc and Esc reach the menu (home) and the games. RGB30: only Start+Select leaves; PS/Esc are never read | forward hid_quit_pressed() in input_players; make the menu honour PS = home |
| D02 | untested | history | bluetooth | Pi: bt_start() at boot when a pad is paired: the pad reconnects. RGB30: BT starts only when a pairing row is opened: a paired pad, keyboard or mouse does not reconnect after a reboot | call bt_start() at boot if bt_paired() |

**2. Data-loss risks in the menu**

| Id | Status | Cause | Area | What differs | What to do |
|---|---|---|---|---|---|
| D06 | untested | history | launch | Pi: asks to close a game that is frozen (suspended) before starting another. RGB30: closes it silently: the frozen game's progress is lost | add the question |
| D07 | untested | history | frozen game | Pi: shows a 'running' badge and a Close row. RGB30: no badge, no Close row | set the badge and add the row |
| D08 | untested | history | delete a game | Pi: closes the suspended copy; refuses while a download is updating it. RGB30: does neither | port the two checks |

**3. One key, one default**

| Id | Status | Cause | Area | What differs | What to do |
|---|---|---|---|---|---|
| D16 | untested | history | Market formats and show_bm / show_b16 | Pi: shows .bm; .b16 only without a .bm twin, or show_b16=1. RGB30: shows .b16; show_bm=1 adds .bm; in Games the same key show_bm has the opposite default | one key, one default; document it in RGB30.md and B16.md |

**4. Developer workflow**

| Id | Status | Cause | Area | What differs | What to do |
|---|---|---|---|---|---|
| D09 | open | history | game list | Pi: sorted by title; scans / and /carts; shows .bme projects in Dev. RGB30: FAT order; only /bm; ignores .bme | sort by title; decide on /carts and on projects |
| D13 | untested | history | Dev tab and Market Apps | Pi: tools recognised by name (tool_id_of), apps installed from the Market appear in Dev. RGB30: 7 fixed entries; a Market App installed on the RGB30 is not recognised as a tool and shows up as a game | same recognition on the RGB30, or filter apps out of Games |

**5. Robustness**

| Id | Status | Cause | Area | What differs | What to do |
|---|---|---|---|---|---|
| D10 | open | history | covers | Pi: loaded in the background, on-screen covers first. RGB30: all loaded synchronously at each scan (up to 64) | port the background cover job |
| D18 | open | history | crash screen | Pi: red screen, restart after 60 s (after 20 s of uptime), report at the next boot. RGB30: red screen, LED code, halts for ever; no report | port the restart and the report |

**6. Decide first whether it is wanted on that console**

| Id | Status | Cause | Area | What differs | What to do |
|---|---|---|---|---|---|
| D03 | suspended | unknown | players | Pi: up to 4 BT pads plus USB: local multiplayer. RGB30: one player only; Settings shows only Player 1 | decide whether the RGB30 has local multiplayer; if so map BT pads to players |
| D04 | open | history | menu keys | Pi: Start confirms; Tab and 1-5 jump tabs; U uploads a cart. RGB30: Start does nothing in the menu; no keyboard keys | align Start and Tab for BT keyboards |
| D05 | suspended | unknown | menu pointer | Pi: mouse: hover, click, wheel, right-click for options. RGB30: none; Settings still offers the mouse rows | port the block (pointer.c is built into the RGB30) or hide the mouse rows |
| D11 | open | history | game options (X / C) | Pi: Open in the tools, editable copy, Build, Export .b16, Publish, Send to a nearby console, Close. RGB30: Play, Open in bm SDK, Export .b16, info, delete save, delete | add Send and Publish; the tool rows depend on D13 |
| D12 | open | history | cover details | Pi: author, kind, size, path; a performance note after a game. RGB30: only the hint line; no performance note | add the note |
| D14 | open | todo | Dev tab extras | Pi: CPU bench, Stress test, Texture Room, Demo. RGB30: not built (stress.c and roombench.c are excluded) | port when the RGB30 3D bench matters |
| D15 | open | todo | Settings > System | Pi: CPU clock and temperature; 3D driver row; open the monitor. RGB30: CPU name only | add what the RK3566 can report |
| D17 | open | todo | WiFi connect | Pi: scan and type the password with a keyboard. RGB30: only wifi_ssid / wifi_psk from config.txt; no on-screen keyboard | needs the on-screen keyboard (R1/R2) |
| D19 | open | history | SD logs | Pi: none (only the crumbs report). RGB30: bootlog.txt, bootprev.txt, lastrun.txt in bm/ | give the Pi the same three files (R3) |
| D20 | open | unknown | release content | Pi: manifest.txt: kernel.img, kernel7.img, games in /carts, ca.pem. RGB30: manifest-rgb30: kernel8.img and ca.pem only | confirm it is intended |
| D21 | open | todo | run from the PC without saving | Pi: bm_net.py --play wired (GO_NETPLAY). RGB30: not wired | wire it into ui_home (developer feature) |
| D22 | open | history | monitor | Pi: text console and Dev tab entry. RGB30: none by design (no_monitor); the serial line has a Lua REPL | keep; tell developers |
| D23 | open | todo | 3D on the GPU | Pi: V3D draws the 3D. RGB30: Mali-G52 present but the 3D is on the ARM | Mali driver (M41) |
| D24 | open | todo | Overbit .b16 and Mali benchmark | Pi: -. RGB30: not done | M41 |
| D25 | open | todo | multicore, fibers | Pi: -. RGB30: RGB30 uses one core; fibers in the menu only | later |
| D26 | open | todo | SDK layout 720x720 | Pi: -. RGB30: not done (R39) | R39 |
| D27 | open | todo | screenshot of the menu's own screens | Pi: not done. RGB30: not done | extend the screenshot to the menu |
| D28 | open | todo | a game built in /carts and the Games tab | Pi: a game built in /carts is not in Games (R41). RGB30: - | R41 |

The differences that are hardware, with the capability that explains each:

| Id | Area | Capability | What differs |
|---|---|---|---|
| D30 | screen | `square-panel` | Pi: 16:9 HDMI: menu 640x360, 6 covers a row, menu_scale. RGB30: square 720x720 panel: menu 360x360 shown x2, 3 covers a row |
| D31 | game screen | `square-panel` | Pi: square carts boxed in 16:9 black. RGB30: fb_init_game fills the panel; screen() offers only 360x360 and 720x720; bm_scale, bm_smooth |
| D32 | confirm / back | `pad-only` | Pi: A confirms, B backs. RGB30: B confirms, A backs; confirm=a swaps |
| D33 | game buttons | `pad-only` | Pi: DS4 by position. RGB30: built-in pad by the letters printed on it; game_buttons=position |
| D34 | system keys | `no-keyboard` | Pi: F11, F12 (held: the key list), Print Screen, Ctrl+Esc. RGB30: only on a BT keyboard; the pad has Select+L1+R1 (screenshot) and Start+Select (leave) |
| D35 | text input | `no-keyboard` | Pi: USB or BT keyboard and mouse. RGB30: BT only, no USB host; no on-screen keyboard |
| D36 | storage paths | `storage-path` | Pi: cartridges in /carts and /, data in /bm. RGB30: everything in /bm; the mapping is done by hand in 4 places |
| D37 | 3D settings | `no-3d-gpu-yet` | Pi: 12 rows (AA, VS, queue, textures...). RGB30: '3D on the ARM' only |
| D38 | audio | `audio-codec` | Pi: HDMI only, stereo. RGB30: I2S to a codec: speaker or headphones, hardware volume keys |
| D39 | battery | `battery` | Pi: none; battery() is nil. RGB30: icon, low-battery overlay, Settings row, LED |
| D40 | network chip | `wifi-chip` | Pi: BCM43438 and Pi 1 Ethernet. RGB30: RTL8821CS: 2.4 GHz, open and WPA2-PSK, no Ethernet |
| D41 | boot and update path | `board-boot` | Pi: firmware boots kernel.img; manifest.txt. RGB30: U-Boot boots kernel8.img; manifest-rgb30; a wrong-board kernel is refused |

Also open in the specification (`gate`): resource files are managed only from the PC (the editors do not open them, the
RGB30 has no Lib tab), and the common project layout is migrated for 3 games out of about 13.

## Decisions needed

- Version numbering: a baseline tag for the current line (for example `v0.3.0`) and `v1.0.0` when `gate` is empty. Tagging is
  outward-facing: waiting for a yes.
- D03 and D05: should the RGB30 have local multiplayer and the menu pointer, or are they hardware choices (then the Settings rows go)?
- D16 is decided: `show_bm` is the Games tab, `market_bm` the Market (RGB30), `show_b16` the Market (Pi).
- D13 and the Lib tab: the tools on the RGB30 come from Market > Apps; they must be recognised as tools, and the Lib tab needs a pad UI.
