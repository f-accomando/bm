# Deflate on the Pi Zero W (5 Nov 2026)

Question: can a `.bm` section be stored compressed? Measured with the Deflate Bench utility (`src/kernel/defbench.c`, Settings > Screen) on the Pi Zero W, kernel `dev`. The corpus is `defbench.bm`, made by `scripts/defbench_make.py` from real cartridges: 24167 KB raw, 7031 KB compressed (zlib level 6). The decoder is `png_inflate` (`src/bm/png.c`). Every CRC matched.

| Blob | Raw KB | Zip KB | Ratio | Inflate ms | Inflate KB/s |
|---|---|---|---|---|---|
| lua | 6143 | 1678 | 3.6 | 445 | 13775 |
| sheet | 5904 | 1868 | 3.1 | 532 | 11087 |
| sheet8 | 1672 | 743 | 2.2 | 185 | 9038 |
| mesh | 4535 | 1516 | 2.9 | 426 | 10637 |
| anim | 3655 | 573 | 6.3 | 191 | 19059 |
| map | 1527 | 51 | 29.7 | 39 | 38974 |
| audio | 727 | 600 | 1.2 | 155 | 4668 |
| all | 24167 | 7031 | 3.4 | 1976 | 12225 |

SD read of the 7032 KB file: 445 ms, 15790 KB/s. The `memcpy` line of this run is not valid (0 ms: the compiler removed the loop); the utility now copies through a volatile pointer.

## What the numbers say

- Inflate runs at about 12 MB/s, a little below the SD read speed of this card (15.8 MB/s).
- Load time: reading the 24167 KB raw takes about 1.5 s. Reading the compressed 7031 KB takes 0.45 s and inflating takes 1.98 s, so 2.4 s in all. Compressed storage is slower to load on this card.
- By blob, reading the raw bytes beats reading and inflating for lua, sheet, sheet8, mesh and audio. `anim` is level. `map` (and `layers`, 30x or more) gains.
- Compression pays for size, not for speed: the Market limit (100 MB a file), the download (the Pi receives about 0.5 MB/s over Wi-Fi: 7 MB against 24 MB) and the room on the SD card.

## Decision

Open (the user decides). If sections are stored compressed, do it per section, keep INFO and COVER in clear, raise the header version so an older kernel refuses the file, and inflate once at load (not while the game runs). Candidates that gain on this card: MAP, LAYERS, ANIM. Candidates for size only: LUA, SHEET, MESH. AUDIO needs a codec, not deflate.
