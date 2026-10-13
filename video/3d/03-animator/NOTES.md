# Episode 3 (bm Animator): corrections found by running the editor

To carry into `docs/video/03-animator/{shot-list,script,input}.md` (not edited here). All checked with `record.py --debug`
(the editor with a state log, `dbg.lua`) and the shots in `out/shots/`.

## Keys and numbers (closed `[DA VERIFICARE]`)

- Rows 2-3: bmhost starts in the menu with the file list already open and one file (`/carts/TROPIC.BM`): no `↓`, just `Enter`.
  `0x7f` arrives as Backspace in every name prompt (5 presses clear `bone2`..`bone9` / `anim1`..`anim5`).
- F2 axes (camera yaw 0.5, the default of the page): `d` +x, `a` -x, `w` +y, `s` -y, `r` +z, `f` -z. Pico faces +z, `.L` is on +x.
- `n` on the empty model: `root` from the floor (0, 0, -0.25) to (0, 0.625, -0.25): Pico's boxes are not centred on z (the tail
  sticks out behind). Row 11 becomes `s`×1 (hips at y 0.5), `r`×2 (tail on z 0), `Tab`, `r`×2 (head on z 0), `Tab`.
- Moving an end drags every end at the same point: a tail must not cross another joint (tail1 coming down through the hips
  dragged all the joints; the axis order matters). Counts used (1/8 per press): spine tail `s`×1; head tail `s`×1;
  `arm.L` tail `d`×2 `s`×7; `leg.L` tail `d`×1 `s`×7; `tail1` tail `f`×4 then `s`×5; `tail2` tail `r`×1 then `w`×1.
- New bone = half a unit along the parent's direction, head on the parent's tail. `m` puts the copy at the END of the list and the
  chosen bone stays the same; the message is `1 bones mirrored: the faces on the other side follow them after k or a` (one bone,
  not "2 bones mirrored"). Final order: root, spine, head, arm.L, arm.R, leg.L, leg.R, tail1, tail2 (up/down wrap around).
- Skin `k`: all nine bones get faces (arm tips follow the arms, the tip of the tail follows `tail2`, ...). `K` then `Ctrl+Z` works.
- F3: new clip is `smooth` already (`i` cycles smooth, step, linear: three presses come back to smooth); `Space` plays until any
  other key (all of them stop it, `n` does not: press `Space` before `n`); `<` shortens from 12 frames, `>` lengthens.
- Signs (F3): `w` turns +15 degrees about x. Pico faces +z, so on a bone that hangs down `w` swings the end BACK and `s` FORWARD.
  The shot list's words were reversed: run frame 0 = `leg.L` `s`×2 (forward), `leg.R` `w`×2, `arm.L` `w`×2, `arm.R` `s`×2
  (arms against the legs, same sign as the reference for the legs). Crouch (jump): legs `s`×2 and spine `w`×1 (as the reference);
  stretch: legs `r`, arms `s`×8 (-120 degrees, up); landing: `r` on each moved bone. Lean back (hit): `s`.
- Rows 59-60, spin: 180 degrees twice is NOT safe. The editor stores quaternions and interpolates by the shortest way; at exactly 180
  degrees the two ends are a tie (dot = 0), the turn can go back. Use three turns of 120 degrees (`a`×8 each) at frames 2, 4 and
  End of a 6-frame clip (`<`×6), as the reference clip. script.md ("8 frames, 180 degrees twice") must say 6 frames, three times 120.
- F1: entering the page restarts at the first clip (`idle`); `→` is `run`. `k` was already on from row 29, so row 70 presses it
  twice (off, on). `b` cycles 0, 25, 50, 75, 100 %, then 0 (three more presses to reset). `<` `>` are x0.5 / x2 (1/8 .. 4).
- Rows 73-74: Ctrl+S on a `.bm` opens a box in bmhost too: "TROPIC.BM is a game ... Save an editable copy: TROPIC.BME?
  [Enter] Copy [Esc] No". It blocks the editor until answered; in bmhost the answer is the pad `A` (`pad 1 a`), on a keyboard
  Enter or Space. Not answered, the run ends with "no editable copy made". Then the status line says `saved /carts/TROPIC.BME`.
- Row 76: `Esc` in the menu goes back to the page it came from (F1).
- `build/animator.bm` of this checkout (31 KB, 17:54) stops with `main.lua:1: unexpected symbol near '#'`; `record.py` compiles its own
  `out/animator.bm` with `scripts/mkbm.py`, so it does not depend on it (someone should rebuild it: `make build/animator.bm`).

## Output

- `chain/03-animator.bme` = the file the editor saved. `chain/03-animator.bm` = the same built as bm SDK's Ctrl+B does (project bit off,
  "built from" `TROPIC.BME`): the SDK is another program, so `record.py` does the same step (src/bm/project.c `bm_build`).
- The editor re-encodes MESH: the skin splits the vertices of PICO (128 -> 180); triangles, positions and colours are the same,
  APPLE and CRATE are byte for byte (`verify.py` compares the geometry).
- Timing: the recording is 202.0 s (12120 frames) = 3:30 of the video minus the 8 s hook (a cut of the last seconds). Every row of the
  shot list starts within about 1 s of its time (a warning is printed when it does not).

## Not done / needs others

- The mp4 (`--no-video` is the default without ffmpeg in WSL): the ffmpeg branch of `record.py` (fifo into `bmvideo.encode`) is not tested here.
- Rows 1-3 (hook, Dev tab card) are overlay / cut: bmhost has no Dev tab (`s.mark`), the hook is `bmvideo.hook_cmd` on the last seconds.
- The start is the stand-in (`--start forge`, `models.bm` of `video/3d/assets`, painted by bm-forge, no skeleton) until `chain/02-pixel.bm` exists.
