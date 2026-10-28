# Overbit

The game: `carts/overbit/` (`build.py`, `art/`, sources in `src/`). There are no host tests of the
game any more (removed upstream); a change needs `make market-seed MARKET=<clone>` for the Market.

## Backlog

- RGB30 `.bm` (26 Oct 2026): `bm.info` has `targets: pi,rgb30`; the pad uses the Nintendo layout by position (`Input.nintendo`, also in the pause CONTROLS page). Not verified on the device: the user runs a match on the RGB30 and sends a report.
- Overbit on the RGB30 at 360×360 and 720×720, with an ARM benchmark: see the roadmap in [graphics-mali](graphics-mali.md). It was divergence D24 until 25 Oct 2026.

## How it is today

- Quality and screen (`src/85_quality.lua`): the `.bm` on a TV offers 640×360 (ARM or GPU),
  720p and 1080p (GPU); `OVERBIT_TV_BM` from the Makefile. On the RGB30: 640×360 (ARM)
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
- COMMANDS (28 Oct 2026, `src/87_keys.lua`, keys in `src/10_input.lua`): the title menu and the pause menu open
  a page with the 14 keyboard actions (move, fire, abilities, jump, crouch, reload, melee, change hero) and the pad's
  buttons beside them (the pad does not change). Up and down choose a row, ok arms it, the next key pressed is the
  new key (Esc or Backspace stops). A key that another action had goes to a swap: that action takes the old key. The
  game keeps Enter, Space and Backspace in the menus, the arrows and F1-F6. RESET TO DEFAULT asks first (NO is the
  default answer). The keys are in the game's slot 1 as `keys = { fire = { 0x0D } }`, only the actions that differ
  from the default. `Hud.key` shows the player's key. Checked in `bmhost` with a script of key presses (remap, swap,
  reset, the pause path); not checked on a Pi or an RGB30. The system's Settings > Controls (`controls=`) does not
  touch Overbit: it reads the keys with `keydown`.
- The V3D side (queue, impostors, measurements): [v3d](v3d.md).
- Overbit's mech pilots from pictures (D.Va `ref/rally_pilot.png`, D.Mon `ref/kaiju_pilot.png`):
  run `tools/meshy_text.py` with your own key (the `request.txt` lines), then `pack.py out rally_pilot kaiju_pilot`;
  optionally `rig=` lines with the ids of `out/NAME.task.txt` and `pack.py --rig`. Until then
  the text-to-3D pilots stay. The image-to-3D heroes are not rigged (no task ids kept).
