# Sync protocol: closeout phases of the SPV

Detail of "SPV closeout" in [`sync.md`](sync.md). The Log action is `CLOSEOUT`. The message starts with the phase name. The script checks each phase with `python tools/sync_check.py --closeout`.

| Phase | SPV does |
|---|---|
| C0 DRAIN | Write row `CLOSEOUT DRAIN` and one inbox line for each active operator. Start no new `ASSIGNED` and no new review. Started WOs and reviews finish. Check: no `WORKING` operator on a WO assigned before DRAIN, and no pending row, except `DEFER`. |
| C1 SIGNOFF | For each active operator, write row `SIGNOFF <ID>: closeout <date>` and the same line in `sync/inbox/<ID>.md` (`sync_watch` then wakes operators with `wakeup no`). Check: every active operator is `SIGNED-OFF` or `FORCED`. |
| C2 READY | Run `python tools/sync_check.py --closeout --strict`. Then write row `CLOSEOUT READY`. Check: exit 0 and last line `CLOSEOUT-READY`. |
| C3 DOCS | Update `docs/progress/<area>.md` (Open work, Rules), `docs/ROADMAP.md` and `docs/agents/ledger.txt` (one line per closed subagent). On `bm-core`, run `python scripts/docs_audit.py score` (no broken link) and `python scripts/consistency.py check` (exit 0). |
| C4 HANDOFF | In `sync/NEXT.md`, write section `## N. CLOSEOUT <date>`. It replaces the previous ones. It holds the 7-block restart prompt of `scripts/ctx_guard.py`: RUOLO, REGOLE, VINCOLI, STRUMENTI, STATO, IN SOSPESO, LEZIONI. Check: the section has today's date and the 7 titles. |
| C5 CLEAN | Commit and push the sync worktree. Empty the read inboxes. Remove clean worktrees (yours, reviews, `SIGNED-OFF` operators) and local branches merged into `origin/bm-core`. Delete remote or unmerged branches only with the user's go. Check: `git status --porcelain` is empty, and `git worktree list` shows no `SIGNED-OFF` or review path. |
| C6 END | Give the user the "What not to lose" list. Write `CLOSEOUT END: session closed`. Mark yourself `DONE`. Check: output `CLOSEOUT-END`. |

Safe cleanup: never remove a worktree with uncommitted changes or a branch with commits that exist nowhere else.

**Operator states** (the script computes them; they are not `state` values):

| State | Condition |
|---|---|
| `ACTIVE` | No DRAIN row after its last handshake. |
| `DRAINING` | DRAIN row exists. Operator is `WORKING` or `WAITING`. |
| `SIGNOFF-SENT` | SIGNOFF row exists. `ack` is older than it. |
| `SIGNOFF-ACK` | `ack` at or after SIGNOFF row. `state` is not `DONE`, or `CLOSED` line is missing. |
| `SIGNED-OFF` | `ack` at or after SIGNOFF row, `state DONE`, valid `CLOSED` line. |
| `UNRESPONSIVE` | Timeout (below). |
| `FORCED` | Row `SIGNOFF <ID> FORCED: <reason>`, written after the user's go. |

A new `ASSIGNED` row or a new handshake cancels a SIGNOFF. The operator is then `ACTIVE`.

**Timeouts**: `SIGNOFF-SENT` or `SIGNOFF-ACK` becomes `UNRESPONSIVE` after `eta` + 10 minutes if `WORKING`, else 30 minutes after the SIGNOFF row. Give the user a ready prompt. 30 minutes after the warning, ask the user's go and write `SIGNOFF <ID> FORCED: <reason>`. Do not touch its worktree.

**DEFER**: row `DEFER <WO or review>: reason` (action `CLOSEOUT`, phase DRAIN) moves a WO or long review to the next session.

**What not to lose** (`--closeout` prints it):
- inboxes not empty or not confirmed with `ack`;
- `ASSIGNED` WOs with no `DELIVERED`;
- `DELIVERED` rows with no `REVIEW`;
- `operators.toml` branches not merged into `origin/bm-core`;
- worktrees that still exist;
- `## Closing` lines with `open:` other than `none`.

Put each item in `NEXT.md` (C4) or get the user's decision. Never close an item in silence.
