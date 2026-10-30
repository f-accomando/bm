# Hunter (Bloodborne fan study)

Two resource files: `art/models/hunter.bmm` (high poly) and `art/models/hunter_lo.bmm` (low poly).
Both hold the same four models, the same 24 bones, and the same clips.

| Model | High | Low | Rig |
|---|---|---|---|
| `hunter` (T pose, one continuous skin) | 3278 vertices, 6372 triangles | 503 vertices, 994 triangles | 24 bones, 16 clips |
| `saw_c` (Saw Cleaver, closed) | 564 triangles | 177 triangles | 1 bone `saw` |
| `saw_o` (Saw Cleaver, open) | 688 triangles | 199 triangles | 1 bone `saw` |
| `pistol` | 462 triangles | 182 triangles | bones `pistol`, `hammer`; clip `fire` |

## Clips of `hunter` (12 frames a second, linear, a key on every frame)

`idle`, `walk`, `run`, `gun`, `saw_c1`, `saw_c2`, `saw_c3`, `saw_c_charge`, `saw_o1`, `saw_o2`, `saw_o3`,
`saw_o_charge`, `backstep`, `dodge_l`, `dodge_r`, `roll`.
All clips are in place. The game moves the character. The clips move only the body.

## Weapons

The weapons are separate models. Their frame: origin at the grip, tip toward -z, up +y.
Draw the saw with the matrix of `hand.R` and the pistol with the matrix of `hand.L`.
The grip turns the weapon about y by +90 degrees (`hand.R`) or -90 degrees (`hand.L`).
Then it moves to the palm centre of the T pose: x = -0.80 (`hand.R`) or +0.80 (`hand.L`), y = 1.48.
`armed.py` bakes this grip into a preview cart.

## Rebuild

```bash
python3 art/models/hunter/build.py art/models
```

`hunter_build.py` builds the models (`HUNTER_LOD=lo` selects low poly). `hunter_clips.py` builds the clips.
Each pose comes from a few numbers: two-bone IK for legs and arms, degrees for the body.

## Not verified

- No file was opened in bm Mesh or bm Animator, and none was run on a device (NON VERIFICATO).
- The previews come from `bm_core` (bm-forge), not from the console.
- The model is a fan study of a third-party character. It is not for the Market without permission.
