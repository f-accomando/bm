# Chain files made by Claude cloud (the real ones, not the stand-in)

`video/3d/chain/` is not in git; these are the small files that Claude local needs to recalibrate videos 2 and 3.
Copy them: `cp video/3d/chain-delivered/0[15]-* video/3d/chain/`.

| File | What | Made by |
|---|---|---|
| `01-studio.bme`, `01-studio.bm` | project and cartridge of video 1 (CRATE 12 tri, APPLE 80, PICO 116) | QEMU recording (`rec_qemu.py 01`), `verify.py` PASS; byte-identical to the bmhost run |
| `05-sound.bme`, `05-sound.bm` | bank of video 5 | QEMU recording, `verify.py` ok, identical to bmhost |
| `02-pixel.bm`, `02-pixel.bme` | result of video 2, QEMU recording of the existing take | `verify.py` ALL OK (with local's 3ab6fbf); byte-identical to bmhost |
| `start.bme` | the start project of video 2 (= `01-studio.bme`) | |

`03-animator`: `chain/02-pixel.bm` is now delivered; the script of video 3 must start from it (`--start chain`) and have the skeleton recalibrated.
| `03-animator.bm`, `.bme` | result of video 3 (QEMU recording, verify 46/46, identical to the bmhost run; the QEMU SD also holds a copy `TROPIC1.BME` made by the second Ctrl+S, not in the bmhost run) | `rec_qemu.py 03` |
| 04-map.bm/.bme | QEMU 1080p60 take of 3beab1f, verify.py ALL OK (kernel with the fixed editor) | 547 KB |
| `montage8_ffmpeg_hang.patch` | fix for `montage8.py` (AGY-11): `anullsrc` is endless, so a clip cut with `-t N` never ended; `run_cmd` now adds `-shortest` and the same `-t N` on the output | apply with `git apply` |
