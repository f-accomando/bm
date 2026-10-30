# Dark Night

A new, independent game: the Hunter in the Clinic of a Victorian, dystopian town (a fan study). The cartridge is `carts/darknight/`.
The project (`.bme`) is `carts/darknight/darknight.bme`. The game (`.bm`) is `build/carts/darknight.bm`.

## How it is today (first slice)

- Build: `make build/carts/darknight.bm build/carts/darknight.bme` (rule in the Makefile, like Island Bench). The code is one file,
  `carts/darknight/main.lua`. The map is `art/maps/clinic.bmk` ([map kit](../design/map-kit.md), builder and checks in
  `art/maps/clinic/`). The Hunter is `art/models/dnhunter.bmm` (and the enemies `art/models/dnenemy.bmm`): models `hunter_c` (saw closed) and `hunter_o` (saw open), each with the
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
- The pistol hits what the bones say: `fire_bullet` puts the enemy in the pose it has now (`animate`, the mesh is shared by its kind) and asks
  `hit3d` for the bone the bullet meets (a capsule for each bone, as wide as its vertices). The head counts double (`headshot`), a limb 0.6, the
  rest 1. The bullet aims at the chest of the locked enemy, or of the first one in a narrow cone in front of the Hunter, and stops at a wall.
  The lock (Q) takes only an enemy in sight (`in_sight`: no wall at chest height between them) and goes after 1.5 s out of sight.
- The weapons are hitboxes on bones. The saw: points along the axis of `hand.R` from its head (1.0 closed, 1.35 open) in the pose of that
  moment (`weapon_points`: `animate`, `bone3d`, and the turn of `draw3d`, yaw + PI about y), against a cylinder round each enemy, during the
  frames of the cut. An enemy blow lands when its weapon bone (`TYPES` `weapon`: bone, length, thickness) touches the Hunter in a window round the
  hit time of the clip, so a Hunter who has moved is missed. The dog's leap still uses a distance on the landing.
- The camera looks down at a steep angle behind the player, goes up when a wall is in the way and comes closer only if that is not enough.
- The enemies are the models of `art/models/dnenemy.bmm` ([enemies](../../art/models/enemies/README.md)): villagers in the clinic, then huntsmen and
  dogs in turn in the courtyard, dogs in the alley (`enemy_kind`). They wait, chase within their sight, strike when the attack clip reaches its hit time and stagger when hit (`TYPES` has health, speed and, for each
  attack, the clip, the hit time, the damage, the reach and how often it is chosen). The villager and the huntsman have two attacks (a chop or an axe
  swing, then a sweep or an overhead chop); the dog bites and, from 2.6 to 6.5 units away, leaps at where the Hunter stood and bites on the landing.
  The choice of the attack follows the frame number, so a run of the game is always the same. The Hunter has 100 health.
  A lantern lights (health and bullets back, a new place to start); a death shows YOU DIED and starts again at the last lantern.
- Doors that are not locked open when the player is near; the locked ones and the gate stay closed (the gate is the end of this slice).
- Test: `make test-darknight` plays a script of keys in bmhost and reads the event lines of the game (92 events: the four dodges, the
  three attacks, the saw, the charge, the shot, the door, the lock, damage, death, a new start, and the attacks of the villagers,
  huntsmen and dogs, the leap included). It also checks that the ids and radii of
  `main.lua` are those of the map kit.

## Open work

- Not run on a Pi or an RGB30. The cost of a frame on the Pi Zero W at 1280x720 is not measured (the Resolution row goes to 640x360).
- The enemies have no tactics: they do not dodge, wait for a gap or keep apart in a group. The pickups of the reference map are not in the kit.
- No sound and no music. The saw opening and the pistol have no effects besides sparks and a light.
- The rest of the reference map (Gilbert's House, the Overpass, Main Street) is not built.
- The weapons are baked into the Hunter (two models, one for each form of the saw). The pistol hammer bone has no use.
- The hitboxes are one segment along the hand bone and one cylinder for each body: no boxes for each bone of the Hunter's body, and no hitbox
  for the dog's leap. The length of each weapon is a number (`SAW_LEN`, `TYPES`), not read from the model.
- The key map is a table in `main.lua` (`BIND`, `PAD`). The menu has no row to change a key.

## Rules (do not break)

- Read the keys with `rawkeys(true)` and `keydown`; keep the action table `BIND` the only place that names a key.
- Keep the object ids and radii of `main.lua` equal to `art/maps/clinic/build_clinic.py` (`tests/darknight/check.py` checks it).
- The camera's right is `(fz, -fx)` of its forward: the Hunter's right hand (local -x) when he looks away from the camera. A move by keys uses it.
- Take the weapon hitboxes from the bones (`bone3d` after `animate`), with the turn of `draw3d`; do not place them by hand.
- Take the hurtboxes of the enemies from their skeleton with `hit3d`, after `animate` of that enemy; do not keep a second set of boxes.
- Keep the Hunter's clips in place (the game moves the body); the clips carry no net travel.
- Write a game event as one line `darknight: ...` (`ev()`), because the test reads them.
