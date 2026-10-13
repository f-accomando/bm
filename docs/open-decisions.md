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
- Screenshot `shot([name])`: approved; the pad combo is open (check the reserved ones).
- PNG encoder: compressed (fixed Huffman + RLE, recommended) or stored blocks.

## Theme (PDF manuals)
- Choose among A (paper/orange), B (night/cyan), C (white/blue technical).

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
- Order of the next migrations: `hunt`, then `yharnam`.
