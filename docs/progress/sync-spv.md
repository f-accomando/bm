# Sync protocol: more rules for the SPV

Detail of "Review and integration" in [`sync.md`](sync.md), and two more rules.

### Review steps
Do not trust the report of the operator. The reviewer is not the subagent of the operator. It works in a temporary worktree of the delivered commit and does not touch the worktree of the operator.
- Scope: `git diff <base>...<commit> --stat` against the files that the WO names. Report a file outside the scope (accept it only if a written decision covers it).
- Commands: rerun the verify command of each WO and report the real result. A failure is *existing* (the same error on a clean `bm-core`) or *new*. Only a new failure blocks.
- Code: read the diff for logic errors, security problems (external input, paths, overflow), and missing tests.
- Result for each WO: `APPROVABLE`, or `CHANGES` with the reason and `file:line`. Say what you cannot verify without hardware.

### Quick jobs
For a small job on workflow, tools, docs, or project structure that the user asks for in chat, write no WO. Use subagents. The merge into `bm-core` still needs the explicit OK of the user. `src/`, the kernel, games, jobs over 45 minutes, and hardware keep the WO.

### Operators without a direct channel
Some operators (for example AGY) cannot be started or messaged by SPV. For each of them, write in chat one self-contained prompt for the user. The prompt has the ID, the sync branch, the handshake, the inbox, the WO, the branch and scope, `--wakeup`, and the delivery command. The user pastes the prompt into the session. Before the prompt, push the inbox, the WO file, and the `sync/operators.toml` block of the operator. After the paste, the user does nothing more: run `python tools/sync_check.py` (state, `ack`). If the operator is silent for more than 30 minutes, warn the user. The prompt also says: after the handshake, read your inbox and start without waiting; if you are blocked, say so in your file and stop. For an operator that you start yourself, the prompt is the brief of `start_session`.

### Wake phrase
The user pastes this into the session of an operator that has no timer (`wakeup no`): "Sei `<ID>` nel sync del branch `<branch>`. Fai handshake `<branch>`. Poi, a ogni mio messaggio: `git fetch origin`, leggi `sync/inbox/<ID>.md`, esegui `python tools/sync_hdr.py <ID> --ack auto`, commit e push. Non iniziare altro lavoro che quello della tua casella; consegna con una riga `DELIVERED ...`." (In English: you are `<ID>` in the sync of branch `<branch>`; do the handshake; then, at each message from me, run a beat; do only the work in your inbox; deliver with a `DELIVERED` row.)
