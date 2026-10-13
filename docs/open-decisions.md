# Open decisions

One section per area. Remove an item when it is decided and record the result in the
document of the area.

## Formats (`.bm`, `.b16`, `.bme`)
Audit and options with pros and cons: [FORMATS-AUDIT.md](FORMATS-AUDIT.md); the format as it is: [BM-FORMAT.md](BM-FORMAT.md).
- 720x720: remove it from the tools (F2a) or accept it in the kernel (F2b)?
- What a `.b16` promises: only a target (360x360, 8 MiB) + packaging lint (B3 a+b), or runtime enforcement (B3c, breaks published `.b16` that use `math.random`, files...)?
- Yharnam and Titan EX are a plain copy of the `.bm`: really two editions (B2: build with `--from --b16`), or one edition under two names?
- Header integrity: a second CRC in the reserved bytes (F3a), the full CRC (F3b, breaks old kernels) or leave it?
- Safe now, no decision needed: F1 (document flags and built-from), F7/F8 (writer and tool fixes), B1 (make `B16.md` say what is true; `B16-IT.md` is a broken link).

## Structure of the programs
See [APP-ARCHITECTURE.md](APP-ARCHITECTURE.md) section 7.
- **S1 (security): a game can create `/carts/pixel.bm` (any new `.bm`) and the kernel runs an SD file with a tool's name as that tool, with write access and no permission questions.** Read from the code, not tried. S1a (refuse the reserved names in `write_target`) is a few lines: do it?
- One table of apps and tools instead of 7 lists (A2); `build.py` unified (A4); `id` in INFO for the saves (A7).

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
