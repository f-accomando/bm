# Open decisions

One section per area. Remove an item when it is decided and record the result in the
document of the area.

## Formats (`.bm`, `.b16`, `.bme`)
Audit and options: [FORMATS-AUDIT.md](FORMATS-AUDIT.md); the format as it is: [BM-FORMAT.md](BM-FORMAT.md). Decided 2026-10-13:
- **720x720**: accepted by the kernel (for the RGB30 panel and its GPU); on the Pi it is shown in the middle of a 1280x720 screen (not tried on a Pi). Done in `format.c`, `runtime.c`, `bm.h`; `test_bm` checks it.
- **`.b16` is a target** (360x360 or 720x720, 8 MiB), not a sandbox: `B16.md` now says so; Yharnam and Titan EX are built with `mkbm.py --from X.bm --b16` (flag set). A packaging lint of the forbidden APIs (B3b) stays possible, not done.
- **Sharing assets is free**: no licence blocking. The licence is information that travels (`bmres.py extract` now keeps the source's `license`, `version`, `tags`); credits in `bm.info` with `credit:` lines (see PROJECT-LAYOUT.md). A `res:` list with hashes is only for reproducible builds, never a check that refuses.
- Still open: header integrity (second CRC in the reserved bytes, F3a); the bigger writer unification (F6).

## API
- Screenshot `shot([prefix])`, Print Screen and Select held 3 s: done ([SCREENSHOTS.md](SCREENSHOTS.md)).
  Open: the menu's own screens cannot take one yet; a picture for Print Screen in the F12 list
  of keys (`syskeys.c`); is Select held 3 s free on every pad?
- Pictures for the manuals: folder (`manuals/shots/`?) and naming rule, and a script that copies
  them from the SD.
- `shot()` asks the player nothing (it writes only in `bm/shots/`): keep it so, or ask like `doc_write`?

## Theme (PDF manuals)
- Decided: theme A (paper / orange), the default. Themes B (night) and C (blueprint) stay in `manuals/themes/` as options.
- A pixel font for the titles (as in the example PDF) needs a TTF with a licence that allows shipping it: which?
- Print edition: a variant of A with a white page (less ink)?

## Manuals structure
- 17 manuals ([MANUALS.md](MANUALS.md)): the API in five volumes (core, sound, 3D, lib, tools), one manual per editor. One big API manual instead?
- The finished PDFs are committed in `manuals/pdf/` (2.4 MB for the 34): keep them in the repository, or only publish them with a release?
- Pictures: `manuals/shots/<manual>-<what>.png`, taken on the console; the ones not taken yet are listed in each manual's "Open points".
- The Assistant app has no manual yet (it is not an editor): postponed, see the end of this file.

## Assets
- `res:` list in `bm.info` with SHA-256 and a licence check in `mkbm.py`: approved in
  principle, not yet specified.
- Resources and the `.b16` limits (256 colours, 8 MiB): to verify.

## Project layout
- License id: `LicenseRef-BM-Community-1.0` is used for "BM Community License 1.0" (not an
  SPDX id). Confirm the string.
- Next migrations: `texroom`, `demo`, `stress` (test carts), `sdk_b16` and `titan_ex` (own layouts); the suite apps have their `bm.info` but their `icon.png` and `demo.json` stay at the top.
- `mkcover.py` of `yharnam` gives a different `cover.png` with this Pillow (12.3.0): pin the version or leave the cover as it is?

## Found while checking the manuals (to fix in the source documents)
- bm Pixel only SHOWS zones and boxes (toggle `j`, commits of 2026-10-12); it does not make them. Zones and boxes are made by bm Animator's bake and by `mkbm.py --sprites` / `bmres.py`; `docs/design/suite-pixel.md` proposes the editing tools (F4 zones page, box editor), not built yet. (`GAME-GUIDE.md` and `GUIDA-GIOCHI.md` said Pixel makes zones: fixed.)
- `sdk/README.md` says bm Studio and bm Animator do not use the mouse (they do) and gives Animator's copy/paste as `c` `v` (it is Ctrl+C / Ctrl+V).
- `docs/RISORSE.md` says `mkbm.py --res FILE` integrates resources at build time: `--res` is the resolution there.
- `src/bm/bm.h` comments: COVER 128x80 (it is 88x88) and offsets 18 and 104 "reserved" (used by the project and `.b16` flags).
- `docs/B16.md` "not yet implemented" against `project.h`, `mkbm.py` and a committed `titan_ex.b16`.
- LED of the RGB30: solid green (`docs/RGB30.md`) or blinking (`boot/rgb30/LEGGIMI.txt`).
- `sdk.b16` in the Market: `sdk/README.md` says no, `docs/progress/tools.md` says yes.
- The 5 api manuals list the gaps the repository cannot answer; each manual's last chapter ("Open points" / "Punti aperti") has them.

## Consistency and 1.0.0 ([spec/CONSISTENCY.md](spec/CONSISTENCY.md))
- Everything converges before 1.0.0: `python3 scripts/consistency.py gate` lists the blockers (27 on 2026-10-13: differences between the Pi and the RGB30 that are not hardware, 2 accepted drifts and 2 specification items); 5 more are fixed in the code and wait for a try on an RGB30 (D01, D02, D06, D07, D08).
- A baseline tag for the current line (`v0.3.0`?) and `v1.0.0` when `gate` is empty: waiting for a yes (a tag is outward-facing).
- D03/D05 (RGB30 multiplayer and menu pointer): **suspended** by the owner (the stick may be the mouse unless the RGB30 has its own system; he is working on the on-screen keyboard and pad typing). D16 (`show_bm`/`show_b16`), D13 (tools from Market > Apps on the RGB30).

## Postponed on purpose (to do in the future, decided 2026-10-13)
- Manual for the Assistant app (`carts/assistant`).
- Manual of the specifications (the `docs/spec/` registry as a PDF).
- Asset exchange on the console (sharing resources between games from the console itself; the PC side, `bmres.py`, exists).
- Owner's tests on the hardware: D01, D02, D06, D07, D08, D13, D16, the screenshot (Select held 3 s on the RGB30) and a `.b16` 720x720 on the Pi; then mark them `resolved` in `docs/spec/divergences.toml` and run `consistency.py check`.
- Test carts `demo`, `texroom`, `stress`: to be removed later, not migrated to the common layout.
- Formats F3 (header integrity, variant (a): a second CRC in bytes 120..127, checked only when nonzero) and F6 (one writer in C and one in Python): do them when a header change is needed, not before; F6 must keep the bytes identical (`test_bm.c`, `test_bmres.py`).
- D09: `/carts` on the RGB30 is accepted (storage-path); `.bme` projects in Dev are still to do.

## Left for later (2026-10-13, at the end of the pdf-manuals session)
- Manuals: the `[DA COMPLETARE]` gaps that need the hardware or a measure; about 60 pictures (`manuals/shots/`, taken on the console).
- D09 `.bme` projects in Dev on the RGB30, D17 and D35 (after the on-screen keyboard), D03 and D05 (suspended).
- Migration to the common layout: `sdk_b16` and `titan_ex` (own layouts).
- Warnings seen but not mine: none open on the Pi or RGB30 builds.
- Tags `v0.3.0` and `v1.0.0`: only with an explicit yes.
