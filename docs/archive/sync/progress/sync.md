# Sync protocol

This file defines the rules of the multi-agent sync. Several operators work in parallel on one project. One operator is the supervisor (SPV). Section 2 applies to all operators, SPV included. Section 3 applies to SPV only.

`SYNC.md` is the template of a project: contracts, work orders, and the Log. Copy it for each new project. Archive the copy in `docs/archive/` when the project ends.

## 1. Files and branches

- Each project has one sync branch, `sync/<name>`. `SYNC.md` and `sync/*` live on this branch only, not on `bm-core`. The tools (`tools/sync_hdr.py`, `sync_log.py`, `sync_check.py`) live on `bm-core`. Parallel syncs then do not touch each other.
- Code lives on working branches from `bm-core`. Only sync files go to the sync branch.
- Each file has one writer. SPV writes `SYNC.md` and `sync/inbox/<ID>.md`. Operator `<ID>` writes `sync/<ID>.md`. Single writers prevent merge conflicts.
- The inbox has at most 5 lines.
- Before you push a sync file, run `git pull --rebase origin <sync branch>`.
- Write the status block of your file only with `python tools/sync_hdr.py`. The block has `state`, `task`, `waiting_on`, `progress`, `eta`, `wakeup`, `ack`, and `updated`. Use ISO 8601 times with the time zone. Every call of the script sets `ack` to the last row of `SYNC.md`.
- A Log row has at most 600 characters. Closed sync files and the older tools use Italian labels: see [`sync-labels.md`](sync-labels.md).

## 2. Rules for all operators

### Handshake
The user says "Fai handshake `<branch>`" ("Do the handshake"). Act on the branch name alone. Do these steps:
1. Run `git fetch origin`. Check out `<branch>` in a separate worktree or clone, so that the working branch stays unchanged. Run `git pull --rebase`.
2. Read `SYNC.md` and `sync/operators.toml`. Find SPV and your ID. If you have no ID, propose one to SPV.
3. Create only `sync/<ID>.md`, from `sync/_template.md`. Write the status block with `python tools/sync_hdr.py <ID> --state FREE --task handshake --wakeup yes|no`. Write `## Profile` in at most 10 lines: where you run and where you break, the models and what each one does, the subagents (yes or no, which), your strengths and limits, and the tools with their versions. SPV copies `place`, `models`, `subagents`, `strengths`, and `weak_points` into `sync/operators.toml`. Write `## Requirements` if `SYNC.md` lists requirements.
4. Commit. Run `git pull --rebase origin <branch>`, then `git push origin HEAD:<branch>`. Then wait. SPV assigns no work before the handshake. Start work only when SPV or the user wakes you.

### States
- `WORKING`: You execute an assigned WO. Declare `eta` as a time `HH:MM` (`--eta 18:00`); a time already past means the next day.
- `WAITING`: A peer blocks you. Name the peer in `waiting_on` and declare `eta`. The allowed values are in `sync/operators.toml`. If the work takes longer, move `eta` with a progress row before it expires.
- `FREE`: All your assigned WOs are delivered. You wait for new work.
- `DONE`: Your session is finished. You claim no more work.

### Work order cycle
Use no timers. A beat is `git fetch origin` plus reading your inbox. Do these steps in this order for each work order (WO):
1. Take. When SPV or the user wakes you ("controlla l'inbox", "check the inbox"), run a beat. Then run `python tools/sync_hdr.py <ID> --state WORKING --task <WO-ID> --eta HH:MM`. Commit and push. Do this before any work on the WO.
2. Work. Do the WO. Keep your main agent available: give long work to subagents. If a third party blocks you, run the same command with `--state WAITING --waiting-on <peer>`, push, and ask in chat. Run it again with `--state WORKING` when the block ends.
3. Deliver. Run `python tools/sync_hdr.py <ID> --deliver <WO-ID> --branch <branch> --commit <sha> --verify "<command> → <result>" --tokens T --min M` (see Delivery). Push after step 4.
4. Beat. Run a beat. If your inbox holds another WO, run the command of step 1 for it. Then push once. Write in chat the line that `--deliver` printed: `done WO <X> FREE: <files> files +<a> -<d>, <N> tokens, <M> min`. If your inbox holds no other WO, your state is `FREE`. Wait for a wake.
5. Review. SPV answers with a `REVIEW` row. After `CHANGES REQUESTED`, do step 1 again before you change any file.

`tools/sync_watch.py` is optional (see Heartbeat cost).

### Delivery
`--deliver` writes one row in `## Deliveries` of your own file, in this format, and sets your state to `FREE`:

`DELIVERED {WO-ID} @ {branch}@{commit} · verify: {command} → {result} · {ISO}`

- `--deliver` also adds the cost row to `sync/<ID>.ledger.jsonl` and prints the chat line. `--close <WO>` does the ledger row and the chat line only.
- `DONE` is not a delivery: it is the last state of a session.
- SPV answers in the Log with `REVIEW: APPROVED` or `REVIEW: CHANGES REQUESTED`. Only after `APPROVED` is the work ready to integrate.
- A signal in another form (free text, `DONE {task}`) starts nothing. SPV asks you to write it again as a `DELIVERED` row.

### Chat
Write in chat only these lines, and nothing else (no report, no summary):
- `done WO <X> FREE: <files> files +<a> -<d>, <N> tokens, <M> min` when you finish a WO. Use `waiting on <Y>` in place of `FREE` only when a third party blocks you (for example an answer from SPV), never to name the next WO. If your inbox holds the next WO, close the line without that part. `--deliver` prints this line.
- A short request when the user must act.

Write one line for each WO, when the WO starts or ends. The status of your work goes in chat only when the user asks. To contact another operator, ask SPV. A problem or a request for help is a pushed row (`WAITING`, `waiting_on`): SPV reads it in the repository. Chat alone does not count.

### Inbox and check
- SPV writes new tasks and urgent notes in `sync/inbox/<ID>.md` (at most 5 lines, one task per line).
- At each beat, read the `git diff` of your inbox, not the whole Log. Then run `python tools/sync_hdr.py <ID> --ack auto`.
- `--ack auto` proves that the delivery arrived, not that you understood the row. Whoever acts on a row writes `read <time of the row>` in its own Log or file.
- `python tools/sync_check.py` checks age, `eta`, `ack`, unread rows, deliveries in a wrong format, inboxes over 5 lines, and Log rows over 600 characters. SPV reports to the user the state that the script prints, with its time.
- A rule change is a `RULE` row. It binds an operator only after the `ack` of that operator.

### Heartbeat cost
A beat is `git fetch`, a look at the inbox, and `--ack auto`. What costs is who does it. The main agent re-reads the whole conversation at each beat, so give the beat to a small subagent (about 5 to 15 thousand tokens) or to no model. `python tools/sync_watch.py --id <ID> [--branch <sync branch>] [--every 5] [--beat 15] [--wake-cmd "..."]` runs in your own clone. It fetches, compares your inbox and the Log rows addressed to you with the last look, and wakes the model only if something changed (`.sync_wake`, `--wake-cmd`). It writes the heartbeat itself (`updated`, `ack`) and never changes the state or the task. `--once --no-push` is a dry run. Details and costs: `docs/design/sync-heartbeat-cost.md`.

### Fixed rules
- One writer for each file (see "1. Files and branches").
- Write your status block only with `python tools/sync_hdr.py`. Times are ISO 8601 with the time zone.
- A Log row has at most 600 characters. Put the rest in the WO or in the inbox. Say `IPOTESI` for a guess; do not send a guess as an instruction.
- Declare the limits of your place early (missing toolchain, a container that stops, wifi) in the profile and in the status block.
- `wakeup yes|no` in the profile says if you wake up on your own. For `no`, SPV gives WOs with an explicit `eta` and says when to call you (see "Wake phrase" in `sync-spv.md`).
- `active = false` in `sync/operators.toml` only when an operator leaves the project for good. At the end of a session the block stays `active`.
- Split a task over 45 minutes into steps of at most 45 minutes. A stand-in file is declared as such; when the real file arrives, check again what used it.

### Closeout
A session closes only through CLOSEOUT (see "SPV closeout"), on the user's request. SPV starts it with `CLOSEOUT DRAIN` or `SIGNOFF <ID>` in your inbox. Then do these steps:
1. Let your own subagents finish. Take no new WO and no new review.
2. Deliver and push. Your working tree must be clean.
3. Remove your own clean worktrees and temporary folders.
4. Write the section `## Closing` in your own file with the line `CLOSED @ {branch}@{commit} · tree clean · open: {WO or none} · {ISO}`.
5. Run `python tools/sync_hdr.py <ID> --ack auto --state DONE`. Commit and push. After `DONE`, do no more heartbeats.

SPV runs the phases (detail and checks: [`sync-closeout.md`](sync-closeout.md)):
- C0 DRAIN: no new WO; WOs already started finish. C1 SIGNOFF: one row for each operator.
- C2 READY: `sync_check.py --closeout --strict` exits 0. C3 DOCS: update progress, ROADMAP, ledger.
- C4 HANDOFF: `sync/NEXT.md` with the 7-block restart prompt. C5 CLEAN: push, empty inboxes, remove worktrees.
- C6 END: the list "What not to lose" goes to the user; SPV marks itself `DONE`.


## 3. Rules for the SPV

### Assignments
1. Write each WO so that it takes at most 45 minutes. Split larger work into steps, one WO per step. Give an operator with `wakeup no` WOs with an explicit `eta`.
2. Write the WO file. Push the `ASSIGNED` rows and the inbox lines first. The inbox holds at most 5 lines, so queue at most 5 WOs in the order of execution.
3. Wake the operator only when it is `FREE`, or when it is new. An operator that is `WORKING` finds the queued WOs at its next beat. Wake the operator yourself when you can (for example with `send_message`). When you cannot, tell the user which operator to wake and the phrase to say: "controlla l'inbox".
4. Wait for the take (`WORKING` on the WO). Do not poll. When the user wakes you, run `python tools/sync_check.py`. It flags a WO that nobody took within 10 minutes (the operator is not `WORKING`). Then wake the operator again or tell the user.

### Review and integration
1. Review by tier. For code on shared files or the kernel, start an independent subagent for a full review. For documents and mechanical WOs, rerun the verify command and read the diff yourself. The steps of the full review are in `sync-spv.md` ("Review steps"). Quick jobs and operators without a direct channel are there too.
2. Write the `REVIEW` row: `REVIEW: APPROVED` or `REVIEW: CHANGES REQUESTED`. After `CHANGES REQUESTED`, add a correction WO to the inbox of the operator.
3. Integrate only after `APPROVED`. The user approves the merge into `bm-core`.

### Monitoring
- Read the state of the operators from `python tools/sync_check.py`, never from memory. Run it when the user or a session notice wakes you. Report the status to the user only when the user asks.
- `STALE` is a note, not a state. The script shows it for an operator that is `WORKING` or `WAITING` more than 10 minutes past its `eta` without an update, or 60 minutes without `eta`. `FREE` and `DONE` are never `STALE`.
- The script also shows unread rows (a row addressed to an operator is older than 30 minutes and newer than its `ack`) and rows in a wrong format.
- An operator is blocked and silent when it is `STALE`, or when a row or an inbox line stays unconfirmed for more than 30 minutes and the operator is not `WAITING user`. Tell the user in chat with the ID and the awaited row.

### SPV closeout
1. Open CLOSEOUT only when the user asks. Announce it with one `CLOSEOUT` Log row. Each phase starts only after a script verifies the output of the previous phase.
2. Before the `DRAIN` row, write in the Log the permissions that the operators need (`sync_hdr.py`, push to the sync branch, bridge edits), so that the user grants them all at once. `python tools/sync_check.py --closeout` checks this list.
3. The Log action is `CLOSEOUT`. The message starts with the phase name: `DRAIN`, `SIGNOFF`, `READY`, `DOCS`, `HANDOFF`, `CLEAN`, `END`.

The phases C0 to C6 (DRAIN, SIGNOFF, READY, DOCS, HANDOFF, CLEAN, END), the operator states, the timeouts, `DEFER`, safe cleanup, and the list "What not to lose" are in [`sync-closeout.md`](sync-closeout.md).
4. To release an operator at any time, write the Log row `SIGNOFF {ID}: {reason}`.
