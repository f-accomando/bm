---
name: bm-video-docs-sync
description: How to carry the corrections found by a recording script (video/3d/NN-name/NOTES.md) back into the episode documents (docs/video/NN-name/{script,shot-list,input}.md) without inventing anything, and how a helper agent (e.g. Grok) asks the supervisor (Claude local) instead of the user. Use for GROK-01 style low-risk documentation fixes in the Tropic Trail series.
---

# bm-video-docs-sync

Low-risk job: the recording script (`record.py`) was run on the real editor and its author wrote what the
documents got wrong in `video/3d/NN-name/NOTES.md`. Make the documents say the same.

## Roles

- **Supervisor: Claude local.** Answers every question and reviews every delivery. The user is NOT asked.
- **Helper (Grok or another agent):** applies the corrections, reports, waits for review.
- Questions go in the SYNC.md Log (section 5) as a row `QUESTION for Claude local` with the file, the line and the
  two options you see. Do not guess while waiting: leave that item untouched and continue with the others.

## Procedure

1. Work on your own branch from `origin/feat/claude-video3d`; never push to `bm-core` or to someone else's branch.
2. For the episode, read `video/3d/NN-name/NOTES.md` (read-only). Make a list: one line per correction.
3. For each correction find the exact row in `docs/video/NN-name/shot-list.md` (and the matching sentence in
   `script.md` / `input.md`). Change only what the correction names: keys, counts, values, names, captions.
4. A correction that closes a `[DA VERIFICARE: ...]` -> remove the marker, write the verified value.
5. Not found, ambiguous, or it needs a design choice -> do not edit; list it as "skipped" with the reason.
6. Never change durations, chapters, titles, the English of the narration beyond the corrected fact, or any
   file outside `docs/video/NN-name/`. No `Co-Authored-By` lines.
7. Check: `python3 video/3d/check_series.py` prints 0 problems; `git diff --stat` shows only the files above.
8. Deliver: commit, push your branch, Log row `<TASK> DONE` with applied / skipped lists. Then wait for review.

## Review (supervisor)

Compare `git diff origin/feat/claude-video3d..<branch>` with the NOTES.md list: every applied correction is in the
diff, nothing else is, skipped items are justified. Then merge or ask for changes in the Log.
