# Sync protocol: rules for the SPV

Part of [`sync.md`](sync.md) (rules for all operators, labels). The headings below keep their names: `SYNC.md` points to them.

### Assignments

1. Write each WO so that it takes at most 45 minutes. Split larger work into steps, one WO per step. Give an operator with `wakeup no` WOs with an explicit `eta`.
2. Write the WO file. Push the `ASSIGNED` rows and the inbox lines first. The inbox holds at most 5 lines, so queue at most 5 WOs in the order of execution.
3. Wake the operator only when it is `FREE`, or when it is new. An operator that is `WORKING` finds the queued WOs at its next beat. Wake the operator yourself when you can (for example with `send_message`). When you cannot, tell the user which operator to wake and the phrase to say: "controlla l'inbox".
4. Wait for the take (`WORKING` on the WO). Do not poll. When the user wakes you, run `python tools/sync_check.py`. It flags a WO that nobody took within 10 minutes (the operator is not `WORKING`). Then wake the operator again or tell the user.

### Review and integration

1. Review by tier. For code on shared files or the kernel, start an independent subagent for a full review. For documents and mechanical WOs, rerun the verify command and read the diff yourself. Do not trust the report of the operator. The reviewer is not the subagent of the operator. It works in a temporary worktree of the delivered commit and does not touch the worktree of the operator. The full review has these steps:
   - Scope: `git diff <base>...<commit> --stat` against the files that the WO names. Report a file outside the scope (accept it only if a written decision covers it).
   - Commands: rerun the verify command of each WO and report the real result. A failure is *existing* (the same error on a clean `bm-core`) or *new*. Only a new failure blocks.
   - Code: read the diff for logic errors, security problems (external input, paths, overflow), and missing tests.
   - Result for each WO: `APPROVABLE`, or `CHANGES` with the reason and `file:line`. Say what you cannot verify without hardware.
2. Write the `REVIEW` row: `REVIEW: APPROVED` or `REVIEW: CHANGES REQUESTED`. After `CHANGES REQUESTED`, add a correction WO to the inbox of the operator.
3. Integrate only after `APPROVED`. The user approves the merge into `bm-core`.

### Quick jobs

For a small job on workflow, tools, docs, or project structure that the user asks for in chat, write no WO. Use subagents. The merge into `bm-core` still needs the explicit OK of the user. `src/`, the kernel, games, jobs over 45 minutes, and hardware keep the WO.

### Monitoring

- Read the state of the operators from `python tools/sync_check.py`, never from memory. Run it when the user or a session notice wakes you. Report the status to the user only when the user asks.
- `STALE` is a note, not a state. The script shows it for an operator that is `WORKING` or `WAITING` more than 10 minutes past its `eta` without an update, or 60 minutes without `eta`. `FREE` and `DONE` are never `STALE`.
- The script also shows unread rows (a row addressed to an operator is older than 30 minutes and newer than its `ack`) and rows in a wrong format.
- An operator is blocked and silent when it is `STALE`, or when a row or an inbox line stays unconfirmed for more than 30 minutes and the operator is not `WAITING user`. Tell the user in chat with the ID and the awaited row.

### Operators without a direct channel

Some operators (for example AGY) cannot be started or messaged by SPV. For each of them, write in chat one self-contained prompt for the user. The prompt has the ID, the sync branch, the handshake, the inbox, the WO, the branch and scope, `--wakeup`, and the delivery command. The user pastes the prompt into the session. Before the prompt, push the inbox, the WO file, and the `sync/operators.toml` block of the operator. After the paste, the user does nothing more: run `python tools/sync_check.py` (state, `ack`). If the operator is silent for more than 30 minutes, warn the user. For an operator that you start yourself, the prompt is the brief of `start_session`.

### SPV closeout

1. Open CLOSEOUT only when the user asks. Announce it with one `CLOSEOUT` Log row. Each phase starts only after a script verifies the output of the previous phase.
2. Before the `DRAIN` row, write in the Log the permissions that the operators need (`sync_hdr.py`, push to the sync branch, bridge edits), so that the user grants them all at once. `python tools/sync_check.py --closeout` checks this list.
3. The Log action is `CLOSEOUT`. The message starts with the phase name: `DRAIN`, `SIGNOFF`, `READY`, `DOCS`, `HANDOFF`, `CLEAN`, `END`.

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

To release an operator at any time, write the Log row `SIGNOFF {ID}: {reason}`.
