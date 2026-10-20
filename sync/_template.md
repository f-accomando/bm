---
schema: 1
id: {ID}
state: FREE
task: handshake
waiting_on: 
progress: 0
wakeup: {yes|no}
ack: 
updated: {ISO 8601 with zone}
---
# {ID} — {name}

> **Only {ID} writes this file** (the others read it). The supervisor (SPV) reads it and updates table §1b of `SYNC.md`.
> Update the block with `python tools/sync_hdr.py {ID} --state ... --task ... [--waiting-on] [--progress] [--eta]`, then commit and
> `git pull --rebase origin sync/{name} && git push origin HEAD:sync/{name}`. Do it at least every 15 min and after each finished task.

## Profile
(max 10 lines: where you run and your breaking points, **if you wake yourself up (timer/scheduler: `--wakeup yes|no`)**, models and what each is for, subagents yes/no, strengths and limits, tools with version)

## Requirements
(only if `SYNC.md` §2 lists them: for each one write `YES`, `NO` or `PARTIAL {note}`)

## Task

## Deliveries
(one line per finished work order: `DELIVERED {WO-ID} @ {branch}@{commit} · verify: {command} → {result} · {ISO}`)
(close the work order and write the line in one call: `python tools/sync_hdr.py <ID> --deliver <WO> --branch <branch> --commit <sha> --tokens T --min M --verify "command -> result"`. It writes the ledger row, adds the line above, sets the state to FREE and prints the chat line `done WO ...`.)

## Progress

## Problems

## Requests
