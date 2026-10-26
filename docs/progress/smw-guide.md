# smw-guide: PDF guide and guide edition of the SMW port

Part of [smw.md](smw.md).

## State

- The manual `smw-guide` (EN and IT, `bm-manuals`, theme paper) explains the `.bme`, the APIs used,
  the game structure and the asset pipeline. It has only original text, code excerpts and the redrawn
  screenshots (`manuals/shots/smw-guide-*.png`). `python3 scripts/manuals.py build smw-guide` makes it.
- The personal edition with the real screenshots is made outside the repository: copy the two `.md`
  files to a scratch folder, replace `smw-guide-0` with `smw-real-0`, link `shots` to
  `carts/smw/extracted/shots`, and run `scripts/mkmanual.py`. Do not commit that PDF.

## Rules (do not break)

- No ROM pixel and no real screenshot goes into git or into a shared PDF.

## Guide edition

- It is the same game on the same level. The Map16 grid, the collision layer, the camera, and
  the physics are the real ones. Only the pixels change.
- `carts/smw/guide/build.py` reads the files of `scripts/smw_map16.py`. From the level picture it
  keeps the shape and a colour role (green, earth, yellow, red, blue, light, grey, dark). It paints
  each role with its own colours, light, shadow, and earth speckle. No ROM pixel reaches the
  output. It builds `build/smw-guide/guide.bm`.
- `tests/smw/guide_shots.py` plays input scripts in `bmhost` and writes
  `manuals/shots/smw-guide-NN-name.png`. Use these images only if the PDF is shared. The real build
  (`carts/smw/build/build.py`) stays for the benchmark. `--real` writes the real screenshots
  of the same scenes to `carts/smw/extracted/shots/` (ignored by git).
- Open: restyle Mario's real sprites the same way when they are captured. The two-tone bush
  becomes one green.

## Open work

- Add a vine scene to the guide screenshots.
- Fill the frame time of the RGB30 in the guide (Open points).
