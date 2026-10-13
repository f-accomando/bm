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
- Choose among A (paper/orange), B (night/cyan), C (blueprint/datasheet): previews in `manuals/themes/preview*.pdf` and `preview.png`.

## Manuals structure
- Which manuals, in which order, and what the editor chapters cover.
- Where the screenshots live in the repository (`manuals/shots/`?).

## Assets
- `res:` list in `bm.info` with SHA-256 and a licence check in `mkbm.py`: approved in
  principle, not yet specified.
- Resources and the `.b16` limits (256 colours, 8 MiB): to verify.

## Project layout
- License id: `LicenseRef-BM-Community-1.0` is used for "BM Community License 1.0" (not an
  SPDX id). Confirm the string.
- Next migrations: the other games (`snake`, `shooter`, `astrowing`, `kitchen`, `titan`, `village`, `nano8`, `overbit`, `typing`, `write`) and the apps of the suite.
- `mkcover.py` of `yharnam` gives a different `cover.png` with this Pillow (12.3.0): pin the version or leave the cover as it is?
