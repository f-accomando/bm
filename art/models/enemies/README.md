# Enemies of Dark Night (fan study)

Two resource files, built from the sources in this folder:

| File | Models | Use |
|---|---|---|
| `art/models/enemies.bmm` | `villager`, `huntsman`, `dog` (rigged, with clips) and the weapons `cleaver`, `torch`, `haxe` as separate models | the editable file: bm Mesh and bm Animator open it |
| `art/models/dnenemy.bmm` | `villager` (the cleaver in his right hand), `huntsman` (the torch in the left, the axe in the right), `dog` | the game's file: the weapons are baked on the hands, each model has its clips |

| Enemy | Triangles (body) | Skeleton | Clips |
|---|---|---|---|
| `villager`: a shambling man, wide hat, shaggy hair and beard, green waistcoat, one arm wrapped in rags | 950 | 24 bones, the Hunter's | `idle`, `walk`, `run`, `attack1` (a chop), `attack2` (a sweep), `hit`, `death` |
| `huntsman`: top hat, long frock coat with a ragged hem, torn scarf, wrapped hands | 1020 | 24 bones, the Hunter's | the same names; the torch stays raised; `attack1` swings the axe, `attack2` chops overhead |
| `dog`: a rabid dog, long snout, jaw bone, tail | 622 | 20 bones (its own) | `idle`, `walk`, `run`, `bite`, `leap`, `hit`, `death` |

The two men have the SAME skeleton as the Hunter (`art/models/hunter/hunter_build.py` `BONES`), so a clip of the Hunter works on them
and the other way round. The dog has four legs, a jaw and two tail bones (`DOG_BONES` in `enemy_build.py`).
All clips are in place (the game moves the body) and run at 12 frames a second.

## How they are made

- `enemy_build.py` makes the models: one continuous flat-shaded skin for each body (a loft of rings with shared vertices, limbs grown
  from a patch of the torso), one bone for each vertex. The dog is a loft along its back with legs that start inside the body.
  The weapons are separate models with the origin at the grip, the tip toward -z, up +y.
- `enemy_clips.py` makes the clips. The men use the solver of `hunter_clips.py` (IK for legs and arms) with a hunched base pose.
  The dog uses local rotations of the bones (no IK), so its feet slide a little.
- `build.py OUT_DIR` writes both files.

```bash
python3 art/models/enemies/build.py art/models
```

## Check that the editors read them

```bash
make build/host/bmhost-bin
python3 scripts/check_bmm.py art/models/enemies.bmm art/models/dnenemy.bmm
```

`scripts/check_bmm.py` runs the import code of bm Mesh and bm Animator (`require "bm3d"`) and the kernel's `file_write` and `res_read` in
bmhost. Result on 2026-10-30: 163 checks passed on `enemies.bmm`, `dnenemy.bmm` and `hunter.bmm`.

## Not verified

- No file was opened by hand in bm Mesh or bm Animator, and none was run on a device (NON VERIFICATO).
- The enemies in the game have simple moves: they wait, chase, strike once and stagger. The game uses every clip of the enemies.
- The shapes follow the reference pictures (hat, coat, rags, hunched body, long snout) in low poly; the fur and the fraying are tents and flat colours.
- A fan study of third-party characters. It is not for the Market without the owner's decision.
