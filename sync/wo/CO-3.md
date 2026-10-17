# CO-3 - "leftovers" list for the closing hand-off (bm-coder)

Order: after CO-2 is APPROVED (same file chain: CO-1..CO-4 all touch `tools/sync_check.py`).
Branch: not set by this WO; SPV sets it at assignment.

Objective: `--closeout` prints the list of what must not be lost (CLOSEOUT.md §2c) and the branch and worktree list (§2e).

Files: `tools/sync_check.py`: new `leftovers(log, ops) -> list[str]`. It imports `pending` from `tools/spv_brief.py` only if that file exists (it is missing on bm-core).

Change:
- One entry per line, prefixed with `  > `, for each item of §2c and for the §2e list.
- Git commands run through subprocess: `git branch -r --no-merged origin/bm-core` and `git worktree list --porcelain`.
- If git fails, print `git: n/d`.

Verification command: `python -m unittest tests.tools.test_sync_check_closeout -k leftovers` → OK (git is mocked).
Designated agent: bm-coder.
