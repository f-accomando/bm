# bm (BareMetal) — Agent Specifications and Routing

This file defines the governance, routing and operating constraints for all AI agents working on the `bm` repository.

---

## 1. The Fundamental Principle: Separate Deciding from Executing

- **Top-tier models (Opus / Gemini Pro)**: High cost on output tokens, but excellent at logical and architectural reasoning. They produce **few tokens**: strategic decisions, schemas and concise Work Orders. **They do not write code.**
- **Fast and cheap models (Haiku / Gemini Flash / Flash-Lite)**: They produce **many tokens**: code implementation, constrained refactoring, rename, formatting. They receive locked-down specs and execute.
- **The Router rule**: Agent selection does NOT depend on the apparent size of the task, but on how rigorously it is already specified.

---

## 2. Role Table

| Agent | Claude Engine | Antigravity Engine | Toolset | Primary Responsibility | Absolute Limits |
|---|---|---|---|---|---|
| **`bm-planner`** | Opus medium | Gemini Pro (`"pro"`) | **Read-Only** (Read, Grep, Glob) | Turns vague tasks into verifiable Work Orders | **Does not write code** |
| **`bm-coder`** | Haiku high | Gemini Flash (`"flash"`) | Full (Read/Write/Exec) | Writes code from a precise Work Order | No design, no open choices |
| **`bm-mechanic`** | Haiku low | Gemini Flash-Lite (`"flash_lite"`) | Full (Atomic) | Rename, formatting, templates, checkmarks in `docs/progress/` | Zero business decisions |
| **`bm-light`** | Sonnet medium | Gemini Flash (`"flash"`) | Full (Read/Write/Exec) | Tasks needing technical judgment, docs verified by reading the C code, 1-2 file fixes | No long or vague tasks |
| **`bm-deep`** | Opus xhigh | Gemini Pro (`"pro"`) | Full (Read/Write/Exec) | A single isolated critical problem with certain reproduction | Refuses tasks without reproduction |
| **`bm-research`** | Haiku low | `research` subagent | Read-Only | Symbol inspection, call flow across many files, 5-10 line summaries | Does not touch files |

---

## 3. The Readiness Gate

Before handing a task to an executing model (`bm-mechanic`, `bm-coder`, `bm-light`), rigorously apply this checklist:

1. [ ] **Files and Functions**: Are the files and signatures/functions to touch explicitly named?
2. [ ] **Determinism**: Is the change described so that two developers would write the same diff (zero open design choices)?
3. [ ] **Deterministic Verification**: Is there a verifiable command (test, build, lint, expected output)?
4. [ ] **Contained Scope**: Does it involve 3 files or fewer?
5. [ ] **Zero Pending Dependencies**: Does it not depend on a decision still to be made?

### Golden Rule
- **5 out of 5 Yes** $\to$ The task is **READY**: assign it to `bm-mechanic`, `bm-coder` or `bm-light`.
- **Even just 1 No** $\to$ The task is **NOT READY**: assign it to **`bm-planner`** to make it ready or split it.
- **Never directly to `bm-deep`** unless it meets the specific criteria for isolated debugging.

---

## 4. Binding Rules for `bm-deep`

`bm-deep` intervenes only on a tight problem statement that includes:
1. A clear, documented symptom.
2. An exact reproduction procedure (failing command/test).
3. An isolated suspected area.
4. Attempts already made (e.g. `FAILED x2` failure).

If the reproduction is missing or the request is generic (*"implement X"*), `bm-deep` refuses the task and sends it back to `bm-planner`.

---

## 5. Closure and Escalation Protocol

Every agent ends its intervention by unambiguously reporting its status:
- **`DONE`**: The verification command passed successfully. Includes a concise diff summary.
- **`BLOCKED: <motivo>`**: Ambiguous specification or missing decision. The task goes back to `bm-planner`.
- **`FAILED x2`**: The test keeps failing after 2 attempts. The task escalates a level (e.g. from `bm-coder` to `bm-light`, or to `bm-deep`).

---

## 6. Standard Work Order Format

Every Work Order issued by `bm-planner` follows this schema:

```markdown
### WORK ORDER: [ID]
- **Obiettivo**: [1 frase chiara]
- **File/Funzioni da toccare**: [Percorsi esatti, es. src/bm/runtime.c:bm_init_cart()]
- **Modifica richiesta**: [Dettaglio univoco senza scelte di design aperte]
- **Comando di verifica**: [Comando e return code/output atteso, es. `python tests/qemu_test.py -k titan`]
- **Agente designato**: [bm-mechanic | bm-coder | bm-light]
```
