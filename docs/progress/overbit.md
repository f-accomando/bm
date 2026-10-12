# Overbit

The game: `carts/overbit/` (`build.py`, `art/`, sources in `src/`). There are no host tests of the
game any more (removed upstream); a change needs `make market-seed MARKET=<clone>` for the Market.

## How it is today

- Quality and screen (`src/85_quality.lua`): the `.bm` on a TV offers 640×360 (ARM or GPU),
  720p and 1080p (GPU); `OVERBIT_TV_BM` from the Makefile. The `.b16` on a TV: 640×360 (ARM)
  and 1080p (GPU). The game picks the rest (calibration, governor); quality and screen never
  touch the simulation (lockstep).
- Bench with flags: `cart_config("overbit_bench")`, `./easy_install.sh bench FLAGS` (flags in
  the header of `src/84_bench.lua`: `res`, `gpu`, `q`, `secs`, `warm`, `ring`, `far`, `save`,
  `stay`). Each step line has `update`, `3D` (the ARM's wait) and `gpu` (`stat(16)`).
- Mouse (`.bm` only, `src/10_input.lua`): the buttons fire; moving it turns the view only in
  the hero's own first person (`first_person`, `mouse_look`), added to `cmd.look_x/look_y` like
  the stick (on the network the view travels in the command). `mouse()` is absolute, so it
  takes the pointer's moves; pushed into an edge it keeps turning at the stick's top speed until
  moved back. Speed: `MOUSE_TURN` (π per screen width) × `Input.mouse_sens`. The arrow shows
  only in the pause menu.
- The V3D side (queue, impostors, measurements): [v3d](v3d.md).
