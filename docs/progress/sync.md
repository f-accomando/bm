# Sync protocol

This file defines the rules of the multi-agent sync. Several operators work in parallel on one project. One operator is the supervisor (SPV). Section 2 applies to all operators, SPV included. Section 3 applies to SPV only.

`SYNC.md` is the template of a project: contracts, work orders, and the Log. Copy it for each new project. Archive the copy in `docs/archive/` when the project ends.

## 1. Files and branches

- Each project has one sync branch, `sync/<name>`. `SYNC.md`, `sync/*`, and `tools/sync_hdr.py` live on this branch only, not on `bm-core`. Parallel syncs then do not touch each other.
- Code lives on working branches from `bm-core`. Only sync files go to the sync branch.
- Each file has one writer. SPV writes `SYNC.md` and `sync/inbox/<ID>.md`. Operator `<ID>` writes `sync/<ID>.md`. Single writers prevent merge conflicts.
- The inbox has at most 5 lines.
- Before you push a sync file, run `git pull --rebase origin <sync branch>`.
- Write the status block of your file only with `python tools/sync_hdr.py`. The block has `state`, `task`, `waiting_on`, `progress`, `eta`, `wakeup`, `ack`, and `updated`. Use ISO 8601 times with the time zone. Every call of the script sets `ack` to the last row of `SYNC.md`.
- A Log row has at most 600 characters.

## 2. Rules for all operators

### Handshake

The user says "Fai handshake `<branch>`" ("Do the handshake"). Act on the branch name alone. Do these steps:
1. Run `git fetch origin`. Check out `<branch>` in a separate worktree or clone, so that the working branch stays unchanged. Run `git pull --rebase`.
2. Read `SYNC.md` and `sync/operators.toml`. Find SPV and your ID. If you have no ID, propose one to SPV.
3. Create only `sync/<ID>.md`, from `sync/_template.md`. Write the status block with `python tools/sync_hdr.py <ID> --state FREE --task handshake --wakeup yes|no`. Write `## Profile`: where you run, the limits, the models, the subagents, and the tools with their versions. Write `## Requirements` if `SYNC.md` lists requirements.
4. Commit. Run `git pull --rebase origin <branch>`, then `git push origin HEAD:<branch>`. Then wait. SPV assigns no work before the handshake. Start work only when SPV or the user wakes you.

### States

- `WORKING`: You execute an assigned WO. Declare `eta`.
- `WAITING`: A peer blocks you. Name the peer in `waiting_on` and declare `eta`. The allowed values are in `sync/operators.toml`.
- `FREE`: All your assigned WOs are delivered. You wait for new work.
- `DONE`: Your session is finished. You claim no more work.

### Work order cycle

Use no timers. A beat is `git fetch origin` plus reading your inbox. Do these steps in this order for each work order (WO):
1. Take. When SPV or the user wakes you ("controlla l'inbox", "check the inbox"), run a beat. Then run `python tools/sync_hdr.py <ID> --state WORKING --task <WO-ID> --eta HH:MM`. Commit and push. Do this before any work on the WO.
2. Work. Do the WO. Keep your main agent available: give long work to subagents. If a third party blocks you, run the same command with `--state WAITING --waiting-on <peer>`, push, and ask in chat. Run it again with `--state WORKING` when the block ends.
3. Deliver. Run `python tools/sync_hdr.py <ID> --deliver <WO-ID> --commit <sha> --verify "<command> → <result>" --tokens T --min M`. Push after step 4.
4. Beat. Run a beat. If your inbox holds another WO, run the command of step 1 for it. Then push once. Write in chat the line that `--deliver` printed: `done WO <X> [waiting on <Y> | FREE]: <files> files +<a> -<d>, <N> tokens, <M> min`. If your inbox holds no other WO, your state is `FREE`. Wait for a wake.
5. Review. SPV answers with a `REVIEW` row. After `CHANGES REQUESTED`, do step 1 again before you change any file.

`DONE` is not a delivery. A non-standard signal starts nothing. `tools/sync_watch.py` is optional: it wakes you when your inbox changes. Its cost is in `docs/design/sync-heartbeat-cost.md`.

### Chat

Write in chat only the `done WO` line and a short request when the user must act. Write the status of your work only when the user asks. To contact another operator, ask SPV.

### Closeout

A session closes only through CLOSEOUT (`SYNC.md` §3b), on the user's request. SPV starts it with `CLOSEOUT DRAIN` or `SIGNOFF <ID>` in your inbox. Then do these steps:
1. Let your own subagents finish. Take no new WO and no new review.
2. Deliver and push. Your working tree must be clean.
3. Remove your own clean worktrees and temporary folders.
4. Write the section `## Closing` in your own file with the line `CLOSED @ {branch}@{commit} · tree clean · open: {WO or none} · {ISO}`.
5. Run `python tools/sync_hdr.py <ID> --state DONE`. Commit and push. Then stop.

## 3. Rules for the SPV

### Assignments

1. Write each WO so that it takes at most 45 minutes. Split larger work into steps, one WO per step. Give an operator with `wakeup no` WOs with an explicit `eta`.
2. Write the WO file. Push the `ASSIGNED` rows and the inbox lines first. The inbox holds at most 5 lines, so queue at most 5 WOs in the order of execution.
3. Wake the operator only when it is `FREE`, or when it is new. An operator that is `WORKING` finds the queued WOs at its next beat. Wake the operator yourself when you can (for example with `send_message`). When you cannot, tell the user which operator to wake and the phrase to say: "controlla l'inbox".
4. Wait for the take (`WORKING` on the WO). Do not poll. When the user wakes you, run `python tools/sync_check.py`. If the first WO of an operator has an `ASSIGNED` row older than 10 minutes and the operator is not `WORKING`, wake the operator again or tell the user.

### Review and integration

1. For each `DELIVERED` line, start an independent subagent for the review. The review checks the scope, reruns the commands (it separates failures that existed before from new failures), reads the code, and gives a result per WO. `SYNC.md` §3c has the details.
2. Write the `REVIEW` row: `REVIEW: APPROVED` or `REVIEW: CHANGES REQUESTED`. After `CHANGES REQUESTED`, add a correction WO to the inbox of the operator.
3. Integrate only after `APPROVED`. The user approves the merge into `bm-core`.

### Monitoring

- Read the state of the operators from `python tools/sync_check.py`, never from memory. Run it when the user or a session notice wakes you. Report the status to the user only when the user asks.
- `STALE` is a note, not a state. The script shows it for an operator that is `WORKING` or `WAITING` more than 10 minutes past its `eta` without an update, or 60 minutes without `eta`. `FREE` and `DONE` are never `STALE`.
- The script also shows unread rows (a row addressed to an operator is older than 30 minutes and newer than its `ack`) and rows in a wrong format.
- An operator is blocked and silent when it is `STALE`, or when a row or an inbox line stays unconfirmed for more than 30 minutes and the operator is not `WAITING user`. Tell the user in chat with the ID and the awaited row.
- To change a rule, write a `RULE` row. The operators confirm it with `ack`.

### Operators without a direct channel

Some operators (for example AGY) cannot be started or messaged by SPV. For each of them, write in chat one self-contained prompt for the user. The prompt has the ID, the sync branch, the handshake, the inbox, the WO, the branch and scope, `--wakeup`, and the delivery command. The user pastes the prompt into the session. For an operator that you start yourself, the prompt is the brief of `start_session`.

### SPV closeout

1. Open CLOSEOUT only when the user asks. Announce it with one `CLOSEOUT` Log row.
2. Before the `DRAIN` row, tell the user which permissions the operators need, so that the user grants them at once.
3. Run the phases C0 to C6 (`SYNC.md` §3b). Start each phase only after a script verifies the output of the previous phase: `python tools/sync_check.py --closeout`.
4. To release an operator at any time, write the Log row `SIGNOFF {ID}: {reason}`.

## Reference: English and Italian labels

Closed sync files and the older tools use the Italian labels.

| English | Italian |
|---|---|
| `## Deliveries` | `## Consegne` |
| `## Profile` | `## Profilo` |
| `## Requirements` | `## Requisiti` |
| `## Closing` | `## Chiusura` |
| `verify:` | `verifica:` |
| `open: ... none` | `aperti: ... nessuno` |
| `waiting_on: user` | `waiting_on: utente` |
