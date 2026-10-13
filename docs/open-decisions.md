# Open decisions

One section per area. Remove an item when it is decided and record the result in the
document of the area.

## Formats (`.bm`, `.b16`, `.bme`)
- `docs/B16.md` says "design, not implemented", `docs/progress.md` lists M41/M42 as done and
  `carts/titan_ex/titan_ex.b16` is committed: which is true? Update the stale one.
- Is the `.b16` Lua sandbox (allow-list) enforced in `runtime.c`? (not verified in the audit)
- `.bme` project files: no example in the repository; where do they live on the SD?
- Working folder `dev/<name>/` of `B16.md` §8.1: implemented or dropped?

## API
- Screenshot `shot([prefix])`, Print Screen and Select+L1+R1: done ([SCREENSHOTS.md](SCREENSHOTS.md)).
  Open: the menu's own screens cannot take one yet; a picture for Print Screen in the F12 list
  of keys (`syskeys.c`); is Select+L1+R1 free on every pad?
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
- The Assistant app has no manual yet (it is not an editor).

## Assets
- `res:` list in `bm.info` with SHA-256 and a licence check in `mkbm.py`: approved in
  principle, not yet specified.
- Resources and the `.b16` limits (256 colours, 8 MiB): to verify.

## Project layout
- License id: `LicenseRef-BM-Community-1.0` is used for "BM Community License 1.0" (not an
  SPDX id). Confirm the string.
- Next migrations: the other games (`snake`, `shooter`, `astrowing`, `kitchen`, `titan`, `village`, `nano8`, `overbit`, `typing`, `write`) and the apps of the suite.
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
