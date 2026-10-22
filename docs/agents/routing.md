# Agent routing

Detail of the subagent routing summarised in the `CLAUDE.md` Glossary. Search with `grep -n -A 14 '^### <Term>' docs/agents/routing.md`. The autonomy threshold line lives in `CLAUDE.md` only.

**Principle: separate deciding from executing.** Top-tier models (*Opus*) are expensive on output tokens: they must produce concise decisions and plans (few tokens). Cheap models (*Haiku/Sonnet*) must write the code (many tokens). The router picks the agent not by the apparent size of the task, but by how well it is already specified.

### Roles (definitions in `.claude/agents/`)
| Agent | Model / effort | Does | Out of scope |
|---|---|---|---|
| **`bm-planner`** | Opus medium (read-only) | Turns vague/large tasks into Work Orders with verifiable criteria | Write code |
| **`bm-coder`** | Haiku high | Writes code from a precise, exhaustive Work Order | Design, resolve ambiguity |
| **`bm-mechanic`** | Haiku low | Already-specified zero-risk tasks (rename, format, template, checkmarks in `docs/progress/`) | Logic decisions |
| **`bm-light`** | Sonnet medium | Simple tasks needing technical judgment, docs verified against real code, 1–2 file fixes | Long and vague jobs |
| **`bm-deep`** | Opus xhigh | A single isolated, critical problem (bug with reproduction, architectural decision) | Implement whole features |
| **`bm-research`** | Haiku low (read-only) | Read-only exploration, symbol/definition search across many files, compact summaries | Modify code |
| **`bm-ops`** | Haiku low (Bash, no code changes) | Already-decided shell commands: `verify.py`, `make`, git fetch/status/push, checking a resume | Decide, fix, start resumes, `kill`, merge |
| **`bm-runner`** | Sonnet medium (all tools) | Executes and verifies in one pass: reviews that run tests, git/worktree work, builds, log extraction; prints the ready ledger row | Design, widen the scope, push to `bm-core`, merge |

### Readiness Gate
Before assigning a task to an executing model (`bm-mechanic`, `bm-coder`, `bm-light`), apply this 5-question checklist. A task is ready for a low-tier agent ONLY if ALL answers are **YES**:
1. **Files/functions**: Are the files and functions to touch explicitly named?
2. **Determinism**: Is the change described so that two people would produce the same diff (no open design choices)?
3. **Verifiable criterion**: Is there a deterministic verification command (test, build, lint, expected output)?
4. **Limited scope**: Does it involve roughly 3 files or fewer?
5. **No blockers**: Is it independent of other decisions still pending?

> **Rule**: If even one answer is missing, the task goes to **`bm-planner`**, which makes it ready or splits it into atomic Work Orders.

### bm-deep
- Invoke only with necessity 4-5 (section above). Accepts **only** a tight problem statement: symptom + exact reproduction + suspected area + attempts already made.
- If the reproduction is missing or the request is generic (e.g. *"implement X"*), **send it back to `bm-planner`**.
- Output: root cause diagnosis and minimal fix; the implementation goes back to `bm-coder` or `bm-light`.

### Necessity
Every request for a model more expensive than the standard one (`bm-deep`, heavy tier) declares **necessity N out of 5 + the reason + estimated cost relative to the cheap model** (e.g. "about 5×"). There is only one reason, and it must be written:
- **Prior failure**: "necessity 4 out of 5: a previous model has already failed (`FAILED x2`)".
- **High cost of error**: "necessity 4 out of 5: the cost of resolving/developing an error is high". Here the judgment belongs to the user for cost only, without a prior failure.

| N | Meaning |
|---|---|
| 1 | The cheap model can do it; requested only for convenience |
| 2 | Almost certainly can do it; the expensive one saves at most one round |
| 3 | Likely error or 2 rounds with the cheap model: `bm-planner` starts |
| 4 | The cheap model has already failed, or the error is costly |
| 5 | Almost a necessity: the error is irreversible or the alternative is giving up on solving it |

### Ledger
after each finished subagent, SPV/the main agent appends a row to `docs/agents/ledger.txt` (append only); `python scripts/agent_audit.py` for tuning. `python scripts/ledger_repeats.py` lists tasks repeated 3 or more times: on the third repeat they become a skill or a script. To add a row, run `python scripts/ledger_add.py --agent ... --model ... --task ... --area ... --tokens N --tools N --secs N`. The script validates the row like `agent_audit.py` and skips duplicates. `--no-write` shows a preview. Edit `ledger.txt` only through this script.

### Escalation
Agents always close with an explicit status:
- **`DONE`**: The verification command passes successfully.
- **`BLOCKED: <motivo>`**: Ambiguous specification or missing decision $\to$ goes back to `bm-planner`.
- **`FAILED x2`**: The test keeps failing after 2 attempts $\to$ moves up one level (e.g. from coder to light, from light to deep).

### Work Order Format
Every order generated by `bm-planner` contains:
- Objective in one sentence
- Files/functions to touch
- Change described with no open design choices
- Verification command and expected result
- Recommended agent (`bm-mechanic` / `bm-coder` / `bm-light` / `bm-deep`)
