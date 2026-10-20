# Cost of the sync heartbeats

Why the protocol (`docs/progress/sync.md`) gives the beat to a script or a small subagent. Orders of magnitude, not measures.

A heartbeat is only `git fetch`, a look at the inbox and `sync_hdr.py <ID> --ack auto`. What costs is *who* does it (order of magnitude, no measure yet: the cost ledger is still missing):

| Who does the beat | Cost of one beat | Note |
|---|---|---|
| The **main agent** (a timer that re-sends a prompt) | re-reads the whole conversation: hundreds of thousands of tokens read from the cache (a tenth of the price) plus a few thousand of output and the output of the commands; the full price if the cache has expired | the most expensive; it also breaks if the session has no timer |
| A **small subagent** (e.g. Haiku, empty context, a short prompt) | about 5-15 thousand tokens | tens of times cheaper; the protocol default for agents with subagents |
| A **script, no model**: `tools/sync_watch.py` | 0 tokens | the model wakes up only if the inbox or a row addressed to the operator changes |

`python tools/sync_watch.py --id <ID> [--branch <sync branch>] [--every 5] [--beat 15] [--wake-cmd "..."]`, run in the operator's own clone (a terminal or a background process): every `--every` minutes it fetches, compares the inbox and the Log rows addressed to the operator with the last look, and **only if there is news** writes `.sync_wake`, prints it and runs `--wake-cmd`; every `--beat` minutes it writes the heartbeat itself (`updated` and `ack`; state and task stay as the agent wrote them), commits and pushes. `--once --no-push` is a dry run. It does not replace the agent's own state changes (`WORKING`, `DELIVERED`, `FREE`): those stay the agent's job. An agent without a timer (profile `wakeup no`) still needs something to start it: the watcher's `--wake-cmd`, or the user.
