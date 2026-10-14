# Video 6 (bm SDK: light, fog, shadow): what the recording taught (2026-10-14)

- **Open the project `TROPIC.BME`, not the game `TROPIC.BM`.** Saving a game asks "Save an editable copy: TROPIC.BME?" (a kernel modal that reads the keyboard, not the typed queue; video 5 NOTES: `FRAME keys 0x28`). In `bmhost --tool` it hangs the run. `merge.py` writes `out/start.bm` and `out/start.bme` (the same file with the project flag), the SD gets the `.bme`.
- Save with `Ctrl+S` (the name is known). `F5` ends a `bmhost --tool` run: it is only an overlay caption. `Ctrl+B` does not build the game in bmhost: `record.py` writes `out/06-sdk.bm` (the project without the project flag) and runs it as R2 (12 s, no keys).
- The cursor reaches each marker of `preview.lua` by counting lines (`-- [sky]`, `-- [fog]`, `-- [lamp]`, `-- [shadow]`, `-- [shadow-draw]`); the typed text gets auto-indent by itself.
- The map of video 4 has a 3-unit-wide corridor: Pico (the cross with spread arms of video 1, x -1..4, not centred on its origin) is drawn at scale 0.5, shifted by half its width; the camera is `camera3d(px, py + 3.2, pz - 6, 0, -0.35, 60)`. The wall of video 4 stands in the corridor and sometimes hides Pico: choose the camera or the wall when the real map comes.
- `verify.py` (ALL OK): sections kept from `start.bm`, the six lines under their markers, a frame of R2 is 480x270 with the fog colour at the top-left pixel and dark shadow pixels, `06-sdk.bm`.
- Anything that writes input files from Windows Python writes CRLF (a CR is typed as Enter): `record.py` rewrites them with LF.
