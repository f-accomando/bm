# Compressed sections in a `.bm` (design, not built)

State of 5 Nov 2026. Nothing is built. A `.bm` and a `.bme` store every section raw, except SHEET8, which holds RLE runs. This note keeps the design and the measurements for the day the work starts. Roadmap entry: SYS-28.

## Why

- The Market limit is 100 MB a file (`CATALOG_MAX_FILE`). A Pi receives about 0.5 MB/s over Wi-Fi, so a 7 MB file against 24 MB is a large difference in time.
- The SD card is shared by many cartridges.
- Compression does **not** make a cartridge load faster on the Pi Zero W (see the numbers below).

## Measurements

Pi Zero W, `png_inflate` (`src/bm/png.c`), 24167 KB of real cartridge data. Full table: [deflate-pi-2026-11](../bench/deflate-pi-2026-11.md). The bench tool is in commit `bd3214bd` of `bm-core`.

| Item | Value |
|---|---|
| Inflate, all sections | 12.2 MB/s |
| SD read | 15.8 MB/s |
| Ratio, all sections | 3.4x |
| Ratio MAP / LAYERS | 30x or more |
| Ratio ANIM | 6.3x |
| Ratio LUA, SHEET, MESH | 2.9x to 3.6x |
| Ratio AUDIO | 1.2x (use a codec, not deflate) |

On the PC (Python, level 6): zlib 3.3x, lzma 4.2x, bz2 4.0x. lzma and bz2 decode 10 times slower than zlib, so they do not fit the Pi.

Reading the raw bytes is faster than reading and inflating for LUA, SHEET, SHEET8, MESH and AUDIO. ANIM is level. MAP and LAYERS gain.

## Design

1. **Per section, not per file.** The header, INFO and COVER stay in clear. The menu reads the cover without the CRC (`bm_cover_peek`). A file compressed as one block would break that.
2. **Codec: zlib (deflate).** The decoder exists in the kernel. Use level 6 or 9 in the tools. The encoder runs on the PC only.
3. **How to mark a section.** Do not change the type of LUA. An older kernel ignores an unknown type, so a game would start without its code. Raise the header `version` to 2 (the parser accepts version 1 only, `format.c:535-579`). An older kernel then refuses the file with a clear message. Bit 5 of the header flags can say "some sections are compressed".
4. **Layout of a compressed section.** A new type `BM_SEC_Z` holds: `u32 original type`, `u32 raw size`, `u32 CRC-32 of the raw bytes`, the zlib stream. The kernel inflates it into a buffer and parses it as the original type. The normal CRC of the file stays valid.
5. **When to inflate.** Once, at load, never while the game runs. The cost is the load time of a cartridge, shown by the loading screen.
6. **Which sections.** Start with MAP, LAYERS and ANIM (they gain time and size). LUA, SHEET and MESH next, for size, as an option of the tool (`mkbm.py --compress`). Keep SHEET8 as it is. For AUDIO study ADPCM.
7. **Projects.** A `.bme` stays raw. The editors read and write it often.
8. **The Market.** Compress when you publish, not when you edit. The size shown in the catalog is the file size.

## Work to do (in order)

1. Spec: add the type, the version and the flag to `docs/BM-FORMAT.md` and `docs/spec/` (`specs.toml`, `divergences.toml`). Run `python scripts/consistency.py check`.
2. Kernel: read BM_SEC_Z in `format.c` (`bm_parse`) with a size limit and a CRC check. One more writer rule: `bm_rewrite_with`, `cart_save` and `project.c` must keep or expand the section.
3. Tools: `mkbm.py --compress`, `bmres.py`, `bmmesh.py`, `bm-forge` (validate and show the sections).
4. Tests: a host test with a compressed cartridge, a broken stream, a wrong size, a wrong CRC. A test that an older kernel refuses version 2.
5. Hardware: measure the load time of Overbit (6.5 MB) raw and compressed on the Pi and on the RGB30.

## Open questions

- Is the version bump acceptable, or does the user want a file that an older kernel still plays (then only the sections it ignores can be compressed)?
- Is the load time more important than the size for the games? On this card it is slower by about 60% when every section is compressed.
- Is a faster card (the RGB30, a Class 10 or A2 card on the Pi) changing the result? Measure there.
