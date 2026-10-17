# CLOSEOUT: session closing procedure for the sync

Status: approved by the user. Decisions approved 2026-10-17. The RULE SIGNOFF that was pending is included here (a new handshake replaces a SIGNOFF).

## 1. Name
- **CLOSEOUT**: it covers both levels (operators and SPV) and also the documentation work; it matches the output of the `CLOSEOUT-READY` script.
- WIND-DOWN means "slow down", not "verify and close". STAND-DOWN concerns only the operators. SEAL suggests something immutable and is confused with the sign-off.

## 2. Rule (text for the RULE and for SYNC.md §3b "Chiusura")
CLOSEOUT opens only on the user's request. SPV announces it with one Log line `CLOSEOUT` and runs it in phases. The next phase starts only when the output of the previous phase is verified by a script.

New Log action: `CLOSEOUT`. The message starts with the phase name: `DRAIN`, `SIGNOFF`, `READY`, `DOCS`, `HANDOFF`, `CLEAN`, `END`.

### 2a. SPV phases
| Phase | SPV does | Output (verified by script) |
|---|---|---|
| C0 DRAIN | Line `CLOSEOUT DRAIN`, plus one line in the inbox of each active operator. From here no new `ASSIGNED` and no new reviews. Lets the WOs and reviews already started finish. Before this line SPV lists the permissions the operators need from the user (`sync_hdr.py`, push to the sync branch, bridge edits), so the user grants them all at once. | `sync_check.py --closeout`: no operator `WORKING` on a WO assigned before DRAIN, except those declared `DEFER` (2d); `pending()` empty for everyone or declared `DEFER`. |
| C1 SIGNOFF | One line `SIGNOFF <ID>: closeout <date>` for each active operator, plus the same line in `sync/inbox/<ID>.md` (so `sync_watch` also wakes those with `wakeup no`). | Every active operator is `SIGNED-OFF` or `FORCED` (2b). |
| C2 READY | Run `python tools/sync_check.py --closeout --strict`. | Exit 0 and last line `CLOSEOUT-READY`. Then line `CLOSEOUT READY`. |
| C3 DOCS | Update `docs/progress/<area>.md` (Open work, Rules), `docs/ROADMAP.md`, `docs/agents/ledger.txt` (one line for each closed subagent). Translate SYNC.md and sync/* into English only if the translation is an open item in NEXT.md. | On bm-core: `python scripts/docs_audit.py score` without broken links and `python scripts/consistency.py check` with exit 0. |
| C4 HANDOFF | Write in `sync/NEXT.md` a section `## N. CLOSEOUT <date>` that replaces the previous ones, with the 7-block restart prompt of `scripts/ctx_guard.py` (commit 3ed826af): RUOLO, REGOLE, VINCOLI, STRUMENTI, STATO, IN SOSPESO, LEZIONI. | `sync_check.py --closeout` finds in NEXT.md the section with today's date and the 7 block titles. |
| C5 CLEAN | Commit and push the sync worktree. Empty the inboxes that were read. Branch and worktree cleanup (2e). | `git status --porcelain` empty in the sync worktree; `git worktree list` without paths of SIGNED-OFF operators or of reviews; list 2e delivered. |
| C6 END | Deliver to the user the "do not lose" list (2c). Write the line `CLOSEOUT END: session closed`. Mark itself `DONE`. | `sync_check.py --closeout` prints `CLOSEOUT-END` (END line present and SPV DONE). |

### 2b. Operator level and closing states
When the operator reads `CLOSEOUT DRAIN` or `SIGNOFF <ID>` (at the heartbeat or with sync_watch):
1. Lets its own subagents finish. Takes no new WO and no new reviews.
2. Delivers (`DELIVERED ...`) and pushes. The working tree must be clean.
3. Removes its own clean worktrees (including the temporary review ones) and its temporary folders.
4. Writes in its own file a section `## Chiusura` with the line `CLOSED @ {branch}@{commit} · tree clean · aperti: {WO or nessuno} · {ISO}`.
5. Runs `python tools/sync_hdr.py <ID> --ack auto --state DONE` (the ack confirms the SIGNOFF), commits and pushes, then stops the heartbeats.

Closing state per operator (computed by the script; it is not a new `state` value):
| State | Condition |
|---|---|
| `ACTIVE` | no `CLOSEOUT DRAIN` line after its last handshake |
| `DRAINING` | the DRAIN line exists and the operator is `WORKING`/`WAITING` |
| `SIGNOFF-SENT` | a `SIGNOFF <ID>` line exists and its `ack` is earlier than that line |
| `SIGNOFF-ACK` | `ack` equal to or later than the SIGNOFF line, but `state` is not `DONE` or the `CLOSED` line is missing |
| `SIGNED-OFF` | `ack` equal to or later than SIGNOFF, `state DONE` and a valid `CLOSED` line |
| `UNRESPONSIVE` | timeout (2d) |
| `FORCED` | SPV line `SIGNOFF <ID> FORCED: <reason>`, written after the user's go |

A new handshake (an `ASSIGNED` line or a handshake after the SIGNOFF) cancels the SIGNOFF and puts the operator back to `ACTIVE`. Use `active = false` in operators.toml only for a definitive SIGNOFF (exit from the project); for an end of session the block stays `active`.

### 2c. What not to lose (final list to the user, printed by `--closeout`)
- Inboxes `sync/inbox/<ID>.md` that are not empty or not confirmed with ack.
- WOs with `ASSIGNED` in the Log but without `DELIVERED`.
- `DELIVERED` without `REVIEW` (from `pending()` of spv_brief).
- Branches listed in operators.toml (`branches`) not merged into `origin/bm-core` (`git branch -r --no-merged origin/bm-core`).
- Worktrees still present (`git worktree list`).
- `## Chiusura` lines with `aperti:` other than `nessuno`.
Each item goes into NEXT.md (C4) or receives an explicit decision from the user. No item is closed in silence.

### 2d. Timeout and DEFER
- Operator in `SIGNOFF-SENT` or `SIGNOFF-ACK` beyond `eta + 10 min` if `WORKING`, otherwise 30 min after the SIGNOFF line: it becomes `UNRESPONSIVE`. SPV tells the user with a ready prompt: `da SPV a <ID>: SIGNOFF closeout, consegna, scrivi ## Chiusura, sync_hdr --ack auto --state DONE`.
- Still `UNRESPONSIVE` 30 min after the warning: SPV asks the user for the go and writes `SIGNOFF <ID> FORCED`. The operator's worktree is not touched; its open branches and WOs go into the 2c list.
- `DEFER <WO|review>: reason` (action `CLOSEOUT`, phase DRAIN): a WO or a long review that moves to the next session. The script treats it as closed for DRAIN and lists it in 2c.

### 2e. Branch and worktree cleanup (phase C5)
SPV produces the list of each branch (local and remote) and each worktree left, with status: `merged`, `not merged`, `abandoned` (no commit missing elsewhere, no open WO).
- On its own: removes the clean worktrees (its own, review ones, those of SIGNED-OFF operators) and deletes the local branches already merged into `origin/bm-core`.
- With the user's go: deletion of remote branches and of unmerged branches (`agy/*`, `claude/*` not merged). The user receives the list and SPV deletes only what is approved.
- Never removed: worktrees with uncommitted changes, branches with commits not reachable from other branches without explicit go. These are reported in 2c.
- Check by script: `closeout_phase_checks` prints the 2e list and a warning `! C5` for each dirty worktree.

## 3. Where the files are
- Template (`sync-handshake` branch): `SYNC.md` (§1 States, §3b Chiusura, §5 Log actions), `sync/_template.md` (section `## Chiusura`), `tools/sync_check.py`, `tools/sync_hdr.py`, `docs/progress/sync.md`. That spv_brief and sync_log are on this branch is an assumption to verify: CO-8.
- Live (bm-core): `docs/progress/sync.md` (Sign-off rule, line 71), `CLAUDE.md` §6 (Sign-off paragraph), `tools/sync_check.py`, `tools/sync_hdr.py` (already on bm-core; spv_brief.py and sync_log.py exist only on the sync branch).
- Project sync branch (`sync/protocollo-ottobre`, SPV only): live copy of `SYNC.md`, `sync/_template.md`, `sync/NEXT.md`, `tools/spv_brief.py`.

## 4. Work orders, in order
- CO-1: `sync_check --closeout` closing states (bm-coder). File `sync/wo/CO-1.md`.
- CO-2: tests for the closing states (bm-coder). File `sync/wo/CO-2.md`.
- CO-3: "leftovers" list for the closing hand-off (bm-coder). File `sync/wo/CO-3.md`.
- CO-4: checks for the DOCS, HANDOFF, CLEAN phases and the DRAIN permissions (bm-coder). File `sync/wo/CO-4.md`.
- CO-5: `sync_hdr --closed` (bm-coder). File `sync/wo/CO-5.md`.
- CO-6: operator template, `## Chiusura` section (bm-mechanic). File `sync/wo/CO-6.md`.
- CO-7: text of the CLOSEOUT rule, after the user's go (bm-light + SPV). File `sync/wo/CO-7.md`.
- CO-8: propagation to the template branch and to bm-core (bm-ops, under SPV). File `sync/wo/CO-8.md`.

Order: CO-1, CO-2, CO-5 first; then CO-3, CO-4, CO-6; then CO-7 and CO-8. CO-1..CO-4 all touch `tools/sync_check.py`: in sequence, never in parallel.
