# Overbit

The game: `carts/overbit/` (`build.py`, `art/`, sources in `src/`). Host tests: `make
test-overbit`. A change needs `make market-seed MARKET=<clone>` for the Market.

## How it is today

- Quality and screen (`src/85_quality.lua`): the `.bm` on a TV offers 640×360 (ARM or GPU),
  720p and 1080p (GPU); `OVERBIT_TV_BM` from the Makefile. The `.b16` on a TV: 640×360 (ARM)
  and 1080p (GPU). The game picks the rest (calibration, governor); quality and screen never
  touch the simulation (lockstep).
- Bench with flags: `cart_config("overbit_bench")`, `./easy_install.sh bench FLAGS` (flags in
  the header of `src/84_bench.lua`: `res`, `gpu`, `q`, `secs`, `warm`, `ring`, `far`, `save`,
  `stay`). Each step line has `update`, `3D` (the ARM's wait) and `gpu` (`stat(16)`).
- The V3D side (queue, impostors, measurements): [v3d](v3d.md).
