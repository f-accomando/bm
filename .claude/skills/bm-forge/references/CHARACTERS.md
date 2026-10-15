# Characters: common parts and the charkit template

Built with `examples/charkit/charkit.py` from a JSON spec (`examples/charkit/presets/`): `crash.json`
(750 triangles), `rango.json` (984), `rango_sheriff.json` (1036: hat, vest, scarf, trousers, boots). Run:
`python examples/charkit/charkit.py examples/charkit/presets/rango.json <ABS>/rango.bm`, then
`bm_forge.py check|preview|anim`. Why one continuous rigged skin: `references/LESSONS.md` L7.

## What Crash and Rango share (the template)

A biped is always the same chain, only the numbers and a few feature switches change:

| Part | Table in the spec | Crash | Rango |
|---|---|---|---|
| Column pelvis -> neck -> head | `column` (rings: y, rx, rz, bone), `segments` (colours per ring pair), `idx` | short neck, round head, long snout (`push`) | long thin neck, wide flat head, no snout, mouth = one thin dark band |
| Eyes | `eyes` mode `front` (flat discs) or `turret` (short limb + pupil) | front discs | big turrets looking up/out |
| Ears / crest / hair | `ears`, `hair`, `tents` (nose, horns) | wide ears, 5 spikes, nose | none |
| Hat | `hat` (rings on a head ring) | none | sheriff hat (variant) |
| Arms | `arms` (first ring = patch of torso vertices, rings, `fingers`) | long, big gloves | thin, long fingers |
| Legs and feet | `legs` (first ring = half pelvis), `foot` slices, `toes` | shorts, big shoes | bare, long feet with toes |
| Tail | `tail` (patch of back vertices + path [y, z, r, colour, bone, x?]: the optional x curls it sideways) | none | thick curling tail |
| Clothes | colours by ring pair and by sector (front/back/sides); sleeves = arm ring colours | shorts, belly | Hawaiian shirt, sleeves |
| Decals | `decals` (ring, sector, colour, radius): flowers, badge | none | flowers, star |
| Skeleton | `bones` (`{S}` = L/R, `mirror`), tail bones | 15 bones | 17 bones |

Shared facts: big head or big eyes; thin long limbs; torso as an inverted trapezoid; big hands and feet;
8-12 flat colours from the colour chart; a neutral A/T pose; three-view reference (front, side, back).
The knobs a person (or an assistant) really chooses: head width/height, snout length, eye mode and size,
neck length, torso width, arm and leg length/thickness, hand size and fingers, foot length, tail yes/no,
clothing colours by zone, hat yes/no, palette.

## Spec keys (charkit.build)

`name`, `title`, `palette` {key: "#RRGGBB"}, `idx` {pelvis, eye_lo, eye_hi, crown, top: ring numbers},
`column` [[y, rx, rz, bone, (angle deg of ring 0)]], `push` [{ring, dz {sector: dz}}] (pull front vertices
forward: the snout), `segments` [{base, front?, back?, sides {sector: key}}] (one per ring pair),
`top_cap` {colour, apex}, `tents` [{base [[ring, sector]], at, off, colour}], `eyes`, `ears`, `hair`, `hat`,
`arms`, `legs`, `tail`, `decals`, `bones`. Sector 3 is straight ahead, 0 is +x (.L), 6 is -x (.R).
Built facing +z and mirrored to -z at the end (bm Studio's recipes, `src/ai/mesh_chars.c`).

## For an AI assistant

- A request becomes a spec: pick the closest preset, change only the knobs above, build, `check`, `preview`.
  An assistant that edits numbers in a table is far more reliable than one that writes geometry.
- The console's own assistant (`src/ai/mesh.c`, recipes `hero`, `robot`, `mech`, `dog`; KB entries in
  `src/ai/kb/meshes.txt` with `gen:`) builds from separate boxes/tubes (`src/ai/mesh_script.c`): it cannot
  express a shared-vertex skin. The continuous recipes are `biped` and `chameleon` (`src/ai/mesh_skin.c`, KB
  `mesh.biped`, `mesh.chameleon`, plan in `docs/design/organic-recipes.md`): the charkit shapes with fixed numbers.
- NON VERIFICATO: none of these files was opened in bm Mesh / bm Animator or run on the console; only
  bm_core (check, preview, a walk clip) was.
- Copyright: Crash and Rango are third-party characters; the presets are personal fan studies, not for
  the Market without permission.
