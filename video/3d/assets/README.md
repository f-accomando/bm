# Tropic Trail: reference assets (bm-forge)

`python video/3d/assets/build.py [OUTDIR]` builds `PICO` (16 boxes, 192 triangles), `APPLE` (60) and `CRATE` (36), the 9-bone
skeleton of PICO (`root, spine, head, tail1, tail2, arm.L, arm.R, leg.L, leg.R`) and the clips `idle, run (loop), jump, spin, hit`,
then checks them (`bm_forge.py check`). Output in `video/3d/out/assets/` (not in git): `tropic_assets.bm`.

- PICO **faces +z** (it runs toward +z, `ry = 0`); `.L` is on +x; the tail goes to -z; height about 1.2 units.
- These are the *answers* for `verify.py` of videos 1 and 3 and a visual reference. The videos build the models by hand
  in bm Studio/bm Animator; the shapes are boxes on purpose (the same blocks).
- Budgets (PIANO.md section 2): PICO <= 300, APPLE <= 80, CRATE <= 40 triangles: met (288 in total).
- Clip timing is on the 12 fps grid of bm Animator: idle 24 frames, run 6, jump 8, spin 6 (one turn), hit 6.
- Previews: `bm_forge.py preview video/3d/out/assets/tropic_assets.bm --model PICO --yaw 2.6 -o front.png` (yaw 2.6 = from the front).
- `bm_forge.py check` reports DRIFT on `src/bm/bm.h`, `format.c`, `runtime.c`, `carts/animator/main.lua` (forge's recorded hashes are older
  than the repo): not caused by these assets; the forge's `bm_core` should be re-synced.
