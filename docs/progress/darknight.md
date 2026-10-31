# Dark Night

A new, independent game: the Hunter in the Clinic of a Victorian, dystopian town (a fan study). The cartridge is `carts/darknight/`.
The project (`.bme`) is `carts/darknight/darknight.bme`. The game (`.bm`) is `build/carts/darknight.bm`.

## How it is today (first slice)

- Build: `make build/carts/darknight.bm build/carts/darknight.bme` (rule in the Makefile, like Island Bench). The code is one file,
  `carts/darknight/main.lua`. The map is `art/maps/clinic.bmk` ([map kit](../design/map-kit.md), builder and checks in
  `art/maps/clinic/`). The Hunter is `art/models/dnhunter.bmm` (and the enemies `art/models/dnenemy.bmm`): models `hunter_c` (saw closed) and `hunter_o` (saw open), each with the
  saw in the right hand, the pistol in the left, and the 17 clips (made by `art/models/hunter/armed.py --game`; the saw is on its own bone `grip.R` and the pistol on `grip.L`, children of the hands, so bm Animator turns a weapon as one part; low poly, about 1300
  triangles). `bmres.py add` merges both files into the cartridge.
- The `.bme` is kept in the repository. To change it: edit `main.lua` or a resource, run `make build/carts/darknight.bme`, then
  `cp build/carts/darknight.bme carts/darknight/darknight.bme`. The `.bm` is the same bytes with bit 0 of the u16 at 18 cleared (`mkbm.py --from`).
- Screen: `screen(640, 360)` at the start, as the header says (the Resolution row of the menu goes to 1280x720). The HUD scales with the screen.
- Keys (`rawkeys` and `keydown`, a pad has the same moves): W A S D move (relative to the camera; locked on, around the target);
  SPACE dodges by the keys held (none: backstep, W: roll, A or D: side jump, S: backstep); SHIFT opens and closes the saw (a model change);
  E fires the pistol (no bullets: a shot costs stamina, `GUN.sta`, and does little damage, `GUN.dmg`); Q locks on and off; U attacks, and U again in the chain window gives the next hit of the combo (4 hits
  for each form, `ATK`); I and O are the second and third hits; SHIFT in a combo is the transform attack (`TRANSFORM`);
  P is the charged attack (hold, then let go); ESC opens the options menu (resume, controls, volume, effects, music, resolution, textures, brightness, performance, restart, quit).
  The Performance row turns on the dev kit's overlay at the top right (`devkit(1)`: fps, ms, triangles, CPU). The Controls page
  remaps each action of `REMAP` to one key (a key of another action is swapped) and `save()` keeps the changed keys and the sound options in slot 1;
  the arrows turn the camera and zoom; F9 is a developer key: the next place of the walk.
- The pistol hits what the bones say: `fire_bullet` puts the enemy in the pose it has now (`animate`, the mesh is shared by its kind) and asks
  `hit3d` for the bone the bullet meets (a capsule for each bone, as wide as its vertices). The head counts double (`headshot`), a limb 0.6, the
  rest 1. The bullet aims at the chest of the locked enemy, or of the first one in a narrow cone in front of the Hunter, and stops at a wall.
  The lock (Q) takes only an enemy in sight (`in_sight`: no wall at chest height between them) and goes after 1.5 s out of sight.
- The weapons are hitboxes on bones. The saw: points along the axis of `hand.R` over `SAW_SPAN` (closed: the folded blade beside the handle; open: the blade beyond the hinge) in the pose of that
  moment (`weapon_points`: `animate`, `bone3d`, and the turn of `draw3d`, yaw + PI about y), against a cylinder round each enemy, during the
  frames of the cut. An enemy blow lands when its weapon bone (`TYPES` `weapon`: bone, length, thickness) touches the Hunter in a window round the
  hit time of the clip, so a Hunter who has moved is missed. The dog's leap still uses a distance on the landing.
- Combat: each attack and dodge costs stamina (`STAMINA`, the green bar). A hit stops the game for a few frames (`G.stop`) and shakes the
  camera. Keys pressed during an attack go to a buffer and play when the chain window opens. The Hunter has hurt and death clips.
- The camera is low and over the right shoulder (`Cam`: pitch -0.36, distance 5.4). Next to a wall the shoulder offset shrinks. A wall or a door
  leaf in the way first tilts the view down a little, then brings the camera closer. F9 and a new start put the camera behind the Hunter.
- The night: a low ambient, a cold sky (`sky3d`), a near fog and a moon high up (`light3d`), so all the shadows fall the same way. The shadows
  lie on the floor height (`map_height`). The three nearest lamps or lanterns are warm, flickering point lights (`lamp3d`).
- Sound: the bank `carts/darknight/art/sounds.py` makes the samples (saw, steps, cloth, pistol, enemies, bells) and two songs in D minor
  (`CLINIC` for the walk, `HUNT` for a fight). `make` builds it into `build/darknight/sounds.json` and `mkbm.py --audio` packs it.
- Draw cost: the fog is whole at `FAR` (22). `map3d_draw(world, 0, FAR + 1)` skips the map pieces beyond it, and the objects and enemies beyond
  it are not drawn. A shadow (a second draw) is drawn only within `SHADOW` (11) of the camera. Models of 12 pixels or less are impostors
  (`gpu3d`). The game asks `gpu3d(gpu3d(), nil, 2, true, 12)`: the vertex shader at 2 and the console's queue, as the Hero cart
  measured them (about 10k hero triangles at 60 fps on the Pi Zero W). The map, the props and the characters take the GPU's mesh
  path: the characters' joints are plain faces on two bones (a continuous skin), drawn by `vs_lit2` (bm3d 7.33), and the 4 faces on
  three bones of the coat's seams are split by `gpuskin.two_bones_max` (`armed.py --game`, `enemies/build.py`). `make test-darknight`
  checks every character on the V3D emulator. A split to one bone a face (as the Hero cart) opened a seam at every bent joint. Shadows are dithered (`shadow3d(1)`): the darkening style darkens again where two triangles of one shadow overlap on the GPU.
  On the fixed walk of bmhost: 6.8k triangles a frame (at most 9.7k) and 316K pixels, against 14.2k (26k) and 1.27M before.
- The Hunter carries the weapons at his sides: `idle`, `walk` and `run` swing the arms beside the body; with the open saw the game plays
  `idle_o`, `walk_o` and `run_o` (the long blade low and back, off the floor). The closed saw has a long handle and the blade folded along it.
- The walls of the map repeat their 16x16 texture in bands (`map3d_build{ wall_tile = 1.0 }`). The tile textures have soft stains, no loose pixels.
- The HUD has no help lines. It shows health, stamina, and a silhouette of the saw in its form (`SAW_ICON`), which flashes at a change.
- The cover is `carts/darknight/cover.png` (made by `carts/darknight/art/cover.py` from a bmhost screenshot).
- The enemies are the models of `art/models/dnenemy.bmm` ([enemies](../../art/models/enemies/README.md)): villagers in the clinic, then huntsmen and
  dogs in turn in the courtyard, dogs in the alley (`enemy_kind`). They wait, chase within their sight, strike when the attack clip reaches its hit time and stagger when hit (`TYPES` has health, speed and, for each
  attack, the clip, the hit time, the damage, the reach and how often it is chosen). The villager and the huntsman have two attacks (a chop or an axe
  swing, then a sweep or an overhead chop); the dog bites and, from 2.6 to 6.5 units away, leaps at where the Hunter stood and bites on the landing.
  The choice of the attack follows the frame number, so a run of the game is always the same. The Hunter has 100 health.
  A lantern lights (health back, a new place to start); a death shows YOU DIED and starts again at the last lantern.
- Doors that are not locked open when the player is near; the locked ones and the gate stay closed (the gate is the end of this slice).
- Test: `make test-darknight` plays a script of keys in bmhost and reads the event lines of the game (97 events: the four dodges, the
  three attacks, the saw, the charge, the shot, the door, the lock, damage, death, a new start, and the attacks of the villagers,
  huntsmen and dogs, the leap included). It also checks that the ids and radii of
  `main.lua` are those of the map kit.

## Open work

- Not run on a Pi or an RGB30. The cost of a frame on the consoles is not measured: run it with the dev kit on (Performance row) and read the session report.
- The enemies have no tactics: they do not dodge, wait for a gap or keep apart in a group. The pickups of the reference map are not in the kit.
- The sound and the music are heard only in bmhost renders (`make wav`). Listen to them on a device.
- The enemy clips are the first ones. They have no hurt reaction by direction.
- The rest of the reference map (Gilbert's House, the Overpass, Main Street) is not built.
- The weapons are baked into the Hunter (two models, one for each form of the saw). The pistol hammer bone has no use.
- The hitboxes are one segment along the hand bone and one cylinder for each body: no boxes for each bone of the Hunter's body, and no hitbox
  for the dog's leap. The length of each weapon is a number (`SAW_SPAN`, `TYPES`), not read from the model.
- The pad buttons (`PAD`) cannot be remapped in the menu. Only the keyboard keys can.

## Rules (do not break)

- Read the keys with `rawkeys(true)` and `keydown`; keep the action table `BIND` the only place that names a game key.
- Read the menu with its own fixed keys (`MK`: arrows, W S, ENTER, SPACE, ESC), so a remapped key never locks the player out of the menu.
- Keep the object ids and radii of `main.lua` equal to `art/maps/clinic/build_clinic.py` (`tests/darknight/check.py` checks it).
- The camera's right is `(fz, -fx)` of its forward: the Hunter's right hand (local -x) when he looks away from the camera. A move by keys uses it.
- Take the weapon hitboxes from the bones (`bone3d` after `animate`), with the turn of `draw3d`; do not place them by hand.
- Take the hurtboxes of the enemies from their skeleton with `hit3d`, after `animate` of that enemy; do not keep a second set of boxes.
- Keep the Hunter's clips in place (the game moves the body); the clips carry no net travel.
- Write a game event as one line `darknight: ...` (`ev()`), because the test reads them.
- Keep the `SFX` table of `main.lua` in the order of the effects of `carts/darknight/art/sounds.py`. An effect is played by its index.
- Draw a shadow (`draw3d` flag 8) on the floor height under the figure, not on the figure's own height.
- Keep the skin of a character continuous (faces across a joint share its vertices). A split to one bone a face opens seams when the joint bends.
- Export the characters with no face on three bones (`gpuskin.two_bones_max`) and no glossy face on two bones. Such a face sends the whole model to the ARM.
- Keep the draw distance `FAR` equal to the fog's far distance, and draw the shadows only within `SHADOW`. A thing beyond the fog costs triangles and shows nothing.
- Make an open shell of a model (coat skirt, cape, strap) two-sided (`twosided` in `hunter_build.py`). A one-sided shell is not seen from inside.
