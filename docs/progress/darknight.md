# Dark Night

A new, independent game: the Hunter in the Clinic of a Victorian, dystopian town (a fan study). The cartridge is `carts/darknight/`.
The project (`.bme`) is `carts/darknight/darknight.bme`. The game (`.bm`) is `build/carts/darknight.bm`.

## How it is today (first slice)

- Build: `make build/carts/darknight.bm build/carts/darknight.bme` (rule in the Makefile, like Island Bench). The code is one file,
  `carts/darknight/main.lua`. The map is `art/maps/clinic.bmk` ([map kit](../design/map-kit.md), builder and checks in
  `art/maps/clinic/`). The Hunter is `art/models/dnhunter.bmm`: models `hunter_c` (saw closed) and `hunter_o` (saw open), each with the
  saw in the right hand, the pistol in the left, and the 17 clips (made by `art/models/hunter/armed.py --game`, low poly, about 1300
  triangles). `bmres.py add` merges both files into the cartridge.
- The `.bme` is kept in the repository. To change it: edit `main.lua` or a resource, run `make build/carts/darknight.bme`, then
  `cp build/carts/darknight.bme carts/darknight/darknight.bme`. The `.bm` is the same bytes with bit 0 of the u16 at 18 cleared (`mkbm.py --from`).
- Screen: `screen(1280, 720)` at the start (the Resolution row of the menu goes to 640x360). The header says 640x360. The HUD scales with the screen.
- Keys (`rawkeys` and `keydown`, a pad has the same moves): W A S D move (relative to the camera; locked on, around the target);
  SPACE dodges by the keys held (none: backstep, W: roll, A or D: side jump, S: backstep); SHIFT opens and closes the saw (a model change);
  E fires the pistol (12 bullets); Q locks on and off; U I O are the three saw attacks (each may follow the one before it in its chain window);
  P is the charged attack (hold, then let go); ESC opens the options menu (resume, resolution, textures, brightness, controls, restart);
  the arrows turn the camera and zoom; F9 is a developer key: the next place of the walk.
- The camera looks down at a steep angle behind the player, goes up when a wall is in the way and comes closer only if that is not enough.
- The enemies are boxes: a placeholder. They wait, chase within 7 units, wind up and strike (14 damage); the Hunter has 100 health.
  A lantern lights (health and bullets back, a new place to start); a death shows YOU DIED and starts again at the last lantern.
- Doors that are not locked open when the player is near; the locked ones and the gate stay closed (the gate is the end of this slice).
- Test: `make test-darknight` plays a script of keys in bmhost and reads the event lines of the game (27 events: the four dodges, the
  three attacks, the saw, the charge, the shot, the door, the lock, damage, death, a new start). It also checks that the ids and radii of
  `main.lua` are those of the map kit.

## Open work

- Not run on a Pi or an RGB30. The cost of a frame on the Pi Zero W at 1280x720 is not measured (the Resolution row goes to 640x360).
- The enemies are placeholders: they need a model, clips and their own moves. The pickups of the reference map are not in the kit.
- No sound and no music. The saw opening and the pistol have no effects besides sparks and a light.
- The rest of the reference map (Gilbert's House, the Overpass, Main Street) is not built.
- The weapons are baked into the Hunter (two models, one for each form of the saw). The pistol hammer bone has no use.
- The hits of the saw are a cone in front of the Hunter during the frames of the cut (the frames are listed in `ATK`), not the blade's real place.
- The key map is a table in `main.lua` (`BIND`, `PAD`). The menu has no row to change a key.

## Rules (do not break)

- Read the keys with `rawkeys(true)` and `keydown`; keep the action table `BIND` the only place that names a key.
- Keep the object ids and radii of `main.lua` equal to `art/maps/clinic/build_clinic.py` (`tests/darknight/check.py` checks it).
- Keep the Hunter's clips in place (the game moves the body); the clips carry no net travel.
- Write a game event as one line `darknight: ...` (`ev()`), because the test reads them.
