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
- Where the finished PDFs live: `build/manuals` only, or committed (`docs/manuals/`, as `docs/scaffold.pdf`)?
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
