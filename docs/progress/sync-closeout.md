# Sync protocol: closeout phases of the SPV

Detail of "SPV closeout" in [`sync.md`](sync.md). The Log action is `CLOSEOUT`; the message starts with the phase name.

| Phase | SPV does | Output (verified by script) |
|---|---|---|
| C0 DRAIN | Row `CLOSEOUT DRAIN` and one line in the inbox of each active operator. From here, no new `ASSIGNED` and no new review. WOs and reviews already started finish. | `sync_check.py --closeout`: no operator `WORKING` on a WO assigned before DRAIN, except `DEFER`; no pending row except `DEFER`. |
| C1 SIGNOFF | One row `SIGNOFF <ID>: closeout <date>` for each active operator, and the same line in `sync/inbox/<ID>.md` (so that `sync_watch` also wakes operators with `wakeup no`). | Every active operator is `SIGNED-OFF` or `FORCED`. |
| C2 READY | Run `python tools/sync_check.py --closeout --strict`, then the row `CLOSEOUT READY`. | Exit 0 and last line `CLOSEOUT-READY`. |
| C3 DOCS | Update `docs/progress/<area>.md` (Open work, Rules), `docs/ROADMAP.md`, and `docs/agents/ledger.txt` (one line for each closed subagent). | On `bm-core`: `python scripts/docs_audit.py score` with no broken link; `python scripts/consistency.py check` exit 0. |
| C4 HANDOFF | In `sync/NEXT.md`, write the section `## N. CLOSEOUT <date>` that replaces the previous ones. It holds the 7-block restart prompt of `scripts/ctx_guard.py`: RUOLO, REGOLE, VINCOLI, STRUMENTI, STATO, IN SOSPESO, LEZIONI. | `sync_check.py --closeout` finds the section with today's date and the 7 titles. |
| C5 CLEAN | Commit and push the sync worktree. Empty the inboxes that were read. Remove clean worktrees (your own, review worktrees, those of `SIGNED-OFF` operators) and local branches already merged into `origin/bm-core`. Delete remote or unmerged branches only with the go of the user. | `git status --porcelain` is empty in the sync worktree; `git worktree list` shows no path of a `SIGNED-OFF` operator or of a review. |
| C6 END | Give the user the list "What not to lose" (below). Write `CLOSEOUT END: session closed`. Mark yourself `DONE`. | `sync_check.py --closeout` prints `CLOSEOUT-END`. |

Safe cleanup: never remove a worktree with uncommitted changes or a branch with commits that exist nowhere else.

**Operator states** (computed by the script; they are not `state` values):
- `ACTIVE`: no DRAIN row after its last handshake.
- `DRAINING`: a DRAIN row exists and the operator is `WORKING` or `WAITING`.
- `SIGNOFF-SENT`: a SIGNOFF row exists and the `ack` is older than it.
- `SIGNOFF-ACK`: `ack` at or after the SIGNOFF row, but `state` is not `DONE` or the `CLOSED` line is missing.
- `SIGNED-OFF`: `ack` at or after the SIGNOFF row, `state DONE`, and a valid `CLOSED` line.
- `UNRESPONSIVE`: timeout (below).
- `FORCED`: the row `SIGNOFF <ID> FORCED: <reason>`, written after the go of the user.

A new `ASSIGNED` row or a new handshake after the SIGNOFF cancels it and the operator is `ACTIVE` again.

**Timeouts and DEFER**: an operator in `SIGNOFF-SENT` or `SIGNOFF-ACK` becomes `UNRESPONSIVE` after `eta` + 10 minutes if it is `WORKING`, otherwise 30 minutes after the SIGNOFF row. Give the user a ready prompt. If it is still `UNRESPONSIVE` 30 minutes after that warning, ask the user for the go and write `SIGNOFF <ID> FORCED`. Do not touch its worktree. The row `DEFER <WO or review>: reason` (action `CLOSEOUT`, phase DRAIN) moves a WO or a long review to the next session.

**What not to lose** (final list for the user, printed by `--closeout`):
- inboxes not empty, or not confirmed with `ack`;
- WOs with `ASSIGNED` and no `DELIVERED`;
- `DELIVERED` rows with no `REVIEW`;
- branches of `operators.toml` not merged into `origin/bm-core`;
- worktrees that still exist;
- `## Closing` lines with `open:` other than `none`.

Put each item in `NEXT.md` (C4) or get an explicit decision from the user. Do not close an item in silence.
