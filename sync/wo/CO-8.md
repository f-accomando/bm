# CO-8 - propagation of the template and of the live copy (bm-ops, under SPV)

Order: after CO-1..CO-7 are APPROVED.
Branch: the sync-handshake branch (template) and bm-core (live); one commit per branch.

Objective: the CLOSEOUT tools and docs are on the template branch and in the live copy, in separate commits.

Files:
- sync-handshake branch: `SYNC.md`, `sync/_template.md`, `tools/sync_check.py`, `tools/sync_hdr.py`, `tests/tools/*`, `docs/progress/sync.md`.
- bm-core: `tools/sync_check.py`, `tools/sync_hdr.py`, `tests/tools/test_sync_check_closeout.py`, `docs/progress/sync.md`, `CLAUDE.md`.

Change:
- First, `git ls-tree -r --name-only origin/sync-handshake tools/ sync/` to see whether `spv_brief.py` exists on the template branch (CLOSEOUT.md §3 says this is still to be verified).
- Then one cherry-pick per branch, with separate commits.
- Merge to bm-core only with the user's go.

Verification command: on each branch, `python -m unittest discover -s tests/tools -p "test_sync*"` → OK.
Designated agent: bm-ops.
