# CO-4 - checks for the DOCS, HANDOFF, CLEAN phases and the DRAIN permissions (bm-coder)

Order: after CO-3 is APPROVED (same file chain: CO-1..CO-4 all touch `tools/sync_check.py`).
Branch: not set by this WO; SPV sets it at assignment.

Objective: `--closeout` checks the handoff file and the clean tree, and phase C0 (DRAIN) requires the permissions list to be ready before it starts.

Files: `tools/sync_check.py`: new `closeout_phase_checks(now) -> list[str]`, called by `--closeout` after READY.

Change:
- HANDOFF check: `sync/NEXT.md` has the heading `## <n>. CLOSEOUT <today's date YYYY-MM-DD>` and, below it, the seven block titles `1 RUOLO` ... `7 LEZIONI` (the 7-block restart template of `scripts/ctx_guard.py`, commit 3ed826af).
- CLEAN check: `git status --porcelain` is empty in the sync worktree.
- INBOX check: every `sync/inbox/*.md` file (excluding README and .gitkeep) has no non-empty lines.
- WORKTREE check: no dirty worktree (CLOSEOUT.md §2e).
- Each failed check prints `! C4: ...` (documents) or `! C5: ...` (clean tree) and counts as a problem.
- Permissions requirement for C0: `--closeout` in DRAIN mode prints the permissions list that SPV must have shown to the user (rule in CO-7). If the list is missing from the log, print `! C0: permissions list missing` and count it as a problem.

Verification command: `python -m unittest tests.tools.test_sync_check_closeout -k phase` → OK.
Designated agent: bm-coder.
