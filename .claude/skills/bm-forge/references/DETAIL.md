# Detail and polygon reduction: what bm-forge can hold and what 1200 triangles keep

Method: `examples/mannequin/mannequin.py` builds a plain male mannequin as one continuous rigged skin (base shapes
only: egg head, neck, torso, tapering limbs, mitten hands, simple feet) with a `detail` knob;
`examples/mannequin/detail_test.py OUT_DIR` (needs numpy and Pillow, optional) builds a reference at the highest detail
that fits the tools' limits, reduces it with the repo's reducer (`tools/bmreduce.py`, the same code as bm Studio's
reduce), builds direct low-poly versions, and measures each against the reference: error in cm (surface to surface,
20000 points, both directions, by body part) and silhouette IoU (front, side, top). Run on 2026-10-15; figure 1.8 m.

## Limits (checked in the code)

| What | Value | Where |
|---|---|---|
| Tools (bm Studio, Animator) and bm-forge default | 4096 vertices, 16384 triangles a model | `T.LIMIT_V/T`, `bm_core/mesh.py` |
| Kernel and `--kernel-limits` | 65535 / 65535 | `src/bm/bm.h:185` |
| Drawn at 60 fps on the console | about 1200 triangles a scene | `T.TRIS_60FPS` |
| Assistant recipes and img2mesh | 2048 faces, 40 bones, 4 clips | `src/ai/mesh.h` |

A continuous skin has about half as many vertices as triangles (the mannequin: 0.5), so the 4096-vertex limit is
reached at about 8000 triangles: that is the real ceiling of detail for a model made in the tools (`detail` 2.6
here: 3785 vertices, 7602 triangles; 2.65 already has 4339 vertices). A flat-shaded model with duplicated corners reaches it earlier. Rigs obey the same limit (the ANIM section).

## Results (reference: detail 2.6, 3785 vertices, 7602 triangles)

| Candidate | Tris | Mean error cm | p95 cm | IoU front/side/top | Head | Hands | Feet |
|---|---|---|---|---|---|---|---|
| reduced to 400 | 399 | 0.67 | 1.35 | .932/.947/.910 | 0.84 | 0.83 | 0.70 |
| reduced to 600 | 599 | 0.58 | 1.12 | .950/.963/.928 | 0.66 | 0.72 | 0.64 |
| built direct, detail 0.5 | 742 | 0.68 | 1.92 | .928/.949/.920 | 1.45 | 0.50 | 0.39 |
| reduced to 800 | 798 | 0.51 | 0.96 | .959/.967/.954 | 0.55 | 0.60 | 0.54 |
| built direct, detail 0.8 | 894 | 0.57 | 1.06 | .949/.964/.928 | 0.94 | 0.43 | 0.40 |
| **reduced to 1200** | 1200 | 0.46 | 0.88 | .970/.974/.961 | 0.44 | 0.55 | 0.53 |
| reduced from an ultra source (40318 triangles, kernel limits only) to 1200 | 1200 | 0.46 | 0.88 | .961/.978/.960 | 0.50 | 0.52 | 0.59 |
| built direct, detail 1.0 | 1334 | 0.46 | 0.85 | .966/.976/.937 | 0.57 | 0.41 | 0.35 |
| reduced to 2000 | 2000 | 0.39 | 0.73 | .979/.986/.979 | 0.42 | 0.55 | 0.48 |
| built direct, detail 1.5 | 2710 | 0.39 | 0.75 | .968/.987/.977 | 0.40 | 0.34 | 0.33 |
| reduced to 4000 | 4000 | 0.34 | 0.65 | .991/.994/.996 | 0.33 | 0.37 | 0.33 |

## What it says

1. **1200 triangles is enough for base forms**: 0.46 cm mean error on a 1.8 m figure, silhouette IoU about 0.97.
   Above 2000 the error falls by less than 15% (0.39 to 0.34 cm).
2. **Reduce or build low?** At 1200 and above they tie (0.46 against 0.46). Below about 900 reduction wins
   (0.51 at 800 against 0.57 at 894; 0.58 at 600 against 0.68 at 742), mostly on the head (a plain low build loses the
   head's shape first: 1.45 cm at 742). Hands and feet built directly stay better than reduced ones.
3. **A richer source does not help**: reducing a 40318-triangle source gives the same 0.46 cm. A source at the tools'
   limit (4096 vertices) is enough, and it is the only one Studio can open.
4. **The reducer keeps the rig**: 15 bones and each vertex's bone survive (half-edge collapse keeps one end's position
   and bone). The skin check (Animator `t`) is still worth running after a reduction.

## Lessons (from the failed first tries)

- **Small separate details look crude and are lost first.** A face made of loose pieces (nose, lips, eyes, brows),
  abs as blocks and ball joints were judged unnatural, and the reducer dropped them before the body: a nose is a few
  millimetres of surface error. The mean error hides them, so measure a feature region on its own. Base forms first;
  a detail needs its own budget (a 300-triangle face rebuilt after reducing the body held the face at 0.48 cm where
  plain reduction gave 0.76, in an earlier run: commits `c564e70`, `44fddd7`).
- **Loose parts are a defect for this user** (see LESSONS L7): the mannequin is one skin; arms grow from a patch of
  shoulder vertices, legs from the two halves of the pelvis ring. The hip still shows thin sliver triangles at high
  detail where the pelvis half-ring meets the thigh ellipse (cosmetic).
- **`bm_core` bug found**: `export_models(..., kernel_limits=True)` did not pass the limits to the ANIM section, so a
  rig over 4096 vertices could not be written (fixed, `test_export_kernel_limits_rig`).
- Face angle: for a tube along +y, ring angle 90 degrees must be +z (the front); the first version had it on the back
  and every bump was on the wrong side.

## Recipe for a 1200-triangle character

1. Model the base forms as one continuous skin at the highest detail the tools allow (about 4000 vertices).
2. In bm Studio, models page F2, key `-`: the triangles wanted (half by default), at most 1200 a scene at 60 fps;
   Ctrl+Z undoes. On the PC: `python tools/bmreduce.py CART.bm --faces 1200 -o OUT.bm`.
3. Check the silhouette from the side and the top, and that hands and feet still read.
4. Open it in bm Animator, F2 rig, `t`: the faces marked red tear or stretch.
5. Below about 900 triangles keep the head from the reduction and build hands and feet low from the start.

NON VERIFICATO: nothing here was opened in bm Studio or Animator on the console; the reducer is the repo's C code run
through ctypes, the renders are bm_core's.
