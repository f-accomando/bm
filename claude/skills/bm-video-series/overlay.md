# Key overlay — specification (not implemented)

Goal: the viewer always sees **which tab or option is selected and which key is pressed**, without covering the console.

## What is drawn (1920x1080 page, 60 fps)

| Zone | Content |
|---|---|
| Console | the 640x360 console frame (editors) or 480x270 game scaled by an integer factor (3x / 4x), left, vertically centred |
| Side panel (right, 480 px wide) | top: the editor name and the **current page/tab** as a row of chips, the active one highlighted; middle: the **option or tool selected** (for example `Tool: Block`, `Layer: objects`, `Wave: FM`); bottom: the **keys of this scene** (3 to 6 chips) |
| Key strip (bottom of the console) | the key pressed *now* as a chip (`F3`, `Ctrl+B`, `Shift+P`, arrows, `Space`): appears on the frame of the press, stays 0.5 s, fades 0.2 s; a held key stays lit; repeated keys show `x3` |
| Caption (lower third) | the English caption of the shot list row, one line, max 70 characters, shown while the action happens |
| Chapter mark | small text top left on the first second of a chapter |

Colors and fonts: the same set as `video/lib/bmvideo.py` (keyboard overlay of the 2D series); keys drawn as in `prompt()` (`docs/api/draw.md`).

## Where the data comes from (nothing is guessed)

1. **Keys**: the `--input` script itself (`FRAME type CODES`): the overlay decodes codes with the table of the skill `bm-video-tutorial` (`\xf0` up ... `\x13` Ctrl+S) at the frame they are sent. One source, so the key shown is the key sent.
2. **Tab / option**: from `docs/video/NN-name/shot-list.md`, columns `Tab / page / menu` and `Tasti`: a small file `overlay.json` is generated from it by `video/3d/overlay_gen.py` (to write): `[ {"frame": 0, "tab": "Models", "option": "Tool: Block", "keys": ["b","Enter"], "caption": "..."} ]`.
3. **Check**: `verify.py` of the episode compares a few frames of the console (`--shots`) with the expected tab (text of the status line), so the overlay cannot contradict the screen.

## Rules

- The overlay is added in post (ffmpeg/ASS or Pillow frames); the console frame is never altered.
- No overlay on the final gameplay video (08), only a small controls box in the first seconds.
- Subtitles = the narration of `script.md`, English; burned in and also exported as `.srt` for YouTube.
- If the console view changes size (480x270 game), the side panel keeps its width and the console scales.

## To implement later

`video/3d/overlay_gen.py` (shot list -> overlay.json), a renderer reusing `make_ass`/`filter_chain` of `video/lib/bmvideo.py`, a `--no-overlay` option for tests.
