# Sync protocol: closeout phases of the SPV

Detail of SPV closeout in [`sync.md`](sync.md). Log action: `CLOSEOUT`. Check each phase: `python tools/sync_check.py --closeout`.

| Phase | SPV does |
|---|---|
| C0 DRAIN | Write `CLOSEOUT DRAIN` and an inbox line per active operator. Start no new `ASSIGNED` or review. Check: no `WORKING` operator on a pre-DRAIN WO, no pending row, except `DEFER`. |
| C1 SIGNOFF | Write `SIGNOFF <ID>: closeout <date>` and in `sync/inbox/<ID>.md` (`sync_watch` wakes `wakeup no`). Check: all are `SIGNED-OFF` or `FORCED`. |
| C2 READY | Run `sync_check.py --closeout --strict`. Write `CLOSEOUT READY`. Check: exit 0, last line `CLOSEOUT-READY`. |
| C3 DOCS | Update `docs/progress/<area>.md`, `docs/ROADMAP.md`, `docs/agents/ledger.txt`. On `bm-core`, run `python scripts/docs_audit.py score` and `python scripts/consistency.py check`. |
| C4 HANDOFF | In `sync/NEXT.md`, write `## N. CLOSEOUT <date>` (replaces older) with the `scripts/ctx_guard.py` prompt: RUOLO, REGOLE, VINCOLI, STRUMENTI, STATO, IN SOSPESO, LEZIONI. Check: date, 7 titles. |
| C5 CLEAN | Push the sync worktree. Empty read inboxes. Remove clean worktrees and local branches merged into `origin/bm-core`. Delete remote or unmerged branches only with the user's go. Check: `git status --porcelain` is empty. |
| C6 END | Give the user "What not to lose". Write `CLOSEOUT END: session closed`. Mark yourself `DONE`. Check: `CLOSEOUT-END`. |

Safe cleanup: keep worktrees with uncommitted changes and branches with unique commits.

| Operator state | Condition |
|---|---|
| `ACTIVE` | No DRAIN row after its last handshake. |
| `DRAINING` | DRAIN row exists; operator `WORKING` or `WAITING`. |
| `SIGNOFF-SENT` | SIGNOFF row exists; `ack` is older. |
| `SIGNOFF-ACK` | `ack` at or after SIGNOFF; `state` not `DONE` or `CLOSED` line missing. |
| `SIGNED-OFF` | `ack` at or after SIGNOFF, `state DONE`, valid `CLOSED` line. |
| `UNRESPONSIVE` | Timeout (below). |
| `FORCED` | Row `SIGNOFF <ID> FORCED: <reason>`, after the user's go. |

A new `ASSIGNED` row or handshake cancels a SIGNOFF.

**Timeouts**: `SIGNOFF-SENT` or `SIGNOFF-ACK` becomes `UNRESPONSIVE` after `eta` + 10 minutes if `WORKING`, else 30 minutes after SIGNOFF. Give the user a prompt. 30 minutes after the warning, ask the user's go and write `SIGNOFF <ID> FORCED: <reason>`. Leave its worktree.

**DEFER**: `DEFER <WO or review>: reason` (action `CLOSEOUT`, phase DRAIN) moves it to the next session.

**What not to lose**:
- inboxes not empty or without `ack`;
- `ASSIGNED` WOs without `DELIVERED`;
- `DELIVERED` rows without `REVIEW`;
- `operators.toml` branches not merged;
- worktrees that still exist;
- `## Closing` lines with `open:` not `none`.

Put each in `NEXT.md` (C4) or get the user's decision.
