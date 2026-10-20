# Agent workflow: review, audit, barriers and confidence (draft Work Orders)

> Draft of 2026-10-14 by SPV (Claude local), to be planned with `bm-planner` before anyone codes it. Context: the owner asked how our subagents compare with another tool's autonomous workflow (one orchestrator, eight steps with a confidence gate, specialised agents including a reviewer, hard guardrails). What we already have is in `docs/agents/routing.md` (CLAUDE.md Glossary: Roles) and `.claude/agents/`; lessons from running three operators are in [`progress/sync-workflow.md`](../progress/sync-workflow.md).

## What we already have that looks like review/audit

| Tool | What it checks | Used by |
|---|---|---|
| `scripts/manuals.py` (`status`, `check <id>`, `stamp <id>`, `build`) and the skill `bm-manuals` | each manual against the code, that English and Italian say the same, and what is behind; `stamp` records the state that was checked | the PDF manuals work: *write, `check` until it passes, then `stamp`* is a review loop with a recorded result |
| `scripts/docs_audit.py score` | broken links, orphan files, tokens always loaded | every task closing (`CLAUDE.md` §3) |
| `scripts/consistency.py check` / `gate` | formats, API, config keys, `divergences.toml` against the code | every task touching formats, API or config |
| `video/3d/check_series.py`, `video/3d/NN/verify.py` | the series docs against each other, and each recording against what it must contain | the video series |
| `/code-review` | a diff, by a model | on request |

What is missing is **a step and an agent that ties them together**: today whoever finishes a task chooses which of these to run, and the review of a branch (for example AGY-09, 2026-10-14: build, `consistency`, "is the old 360×360 drawing unchanged?") is done by hand by the supervisor. It found a real hole (the build declared 360×360, so the new layout never ran), which is the argument for making it repeatable.

## WO-REV-1 — `scripts/review.py`: which checks a diff needs, and run the cheap ones

- **Objective**: one command that looks at a branch's diff and runs (or lists) the checks that apply, so a review does not depend on memory.
- **Files**: new `scripts/review.py`; a table `scripts/review.toml` (path glob → checks); `docs/progress/tools.md` (one line).
- **Change**: `python scripts/review.py [BASE]` runs `git diff --name-only BASE...HEAD` (default `origin/bm-core`), maps each file to checks with `review.toml`, prints the plan, runs the ones marked `cheap = true` and exits non-zero if one fails. Initial table:
  - any file: `docs_audit.py score` (penalties must be 0);
  - `src/**`, `docs/spec/**`, `carts/**` that touch formats, API, config keys: `consistency.py check`;
  - `manuals/content/**`: `manuals.py check <id of the manual>`;
  - `video/3d/**`, `docs/video/**`: `video/3d/check_series.py`; and `verify.py` of the video touched (listed, not run: they need a recording);
  - `carts/sdk_b16/**`: `make TARGET=rgb30` (listed, expensive);
  - `src/kernel/**`, `src/net/**`, `src/bm/runtime.c`: the QEMU test selected by `-k` (listed; `CLAUDE.md` says never to run the whole suite).
  The script prints a line per check: `run`, `listed`, `skipped (why)`.
- **Verification**: `python scripts/review.py origin/bm-core` on a branch that only touches `docs/` runs `docs_audit` and nothing else; on a branch touching `carts/sdk_b16/` it lists `make TARGET=rgb30`. Two small tests in `tests/tools/`.
- **Agent**: `bm-coder` once `review.toml` is agreed (one open decision: the table; ask `bm-planner` or the owner).

## WO-REV-2 — agent `bm-reviewer` (`.claude/agents/bm-reviewer.md`)

- **Objective**: a read-only reviewer that turns a diff into a verdict, using `review.py` and a fixed checklist.
- **Files**: new `.claude/agents/bm-reviewer.md`; a mention in the roles table of `docs/agents/routing.md` (`### Roles`, one row) and §5 (step 0: "before closing a branch, `bm-reviewer`").
- **Definition**: model Sonnet, effort medium; tools `Read`, `Grep`, `Glob` and Bash limited to `python scripts/review.py`, `git diff`, `git log`, `make` and the verification commands of the Work Order. No `Edit`/`Write`.
- **Input**: the Work Order (or a one-line goal) and the branch. **Checklist** (each item answered `ok`, `not ok: …` or `not checked: why`):
  1. the diff touches only the files the Work Order names (or says why more);
  2. the verification command of the Work Order was run and its result is in the report (not "should pass");
  3. `review.py` plan: every `run` check passed, every `listed` check was run or is declared not run;
  4. the **old behaviour is unchanged**: for a change that adds a case (a new layout, a new key, a new target), the old case has a test or the same output; when it cannot be tested the report says so;
  5. the new code is **reachable**: something builds or calls it by default or by a documented option (the 360×360 hole of AGY-09 was code that nothing activated);
  6. rules of the area (`## Rules (do not break)` of `docs/progress/<area>.md`) is respected;
  7. docs: the area's `## Open work` is updated and `docs_audit.py score` has no penalties;
  8. no secrets, no GPL code, no file larger than the repo's limits.
- **Output**: `APPROVE`, `CHANGES: <list>` or `BLOCKED: <reason>` (the same vocabulary as the other agents), then at most 15 lines. It never edits; fixes go back to the executing agent.
- **Verification**: run it on two past branches: `agy/sdk-720` at `76a3bcc` must give `CHANGES` with item 5 (nothing sets the width ≥ 480), and the same branch at `7ceacb1` `APPROVE`.
- **Agent**: `bm-light` can write the file from this text; the two verification runs are the acceptance test.

## WO-BAR-1 — a single "Barriers" section in `CLAUDE.md`

- **Objective**: one short, checkable list of what no agent does without the owner's explicit yes, also in autonomous runs.
- **Files**: `CLAUDE.md` (new section 2b, ≤ 10 lines); memory notes stay as they are.
- **Text (proposal)**: no push to `bm-core` of code and no merge (the owner merges); no credentials, passwords, tokens or `*_KEY` in files, chat, command lines or logs, and an agent never types or reuses a password (a local bridge lets the owner type it); no change to the device's `bm/config.txt` or its WiFi/secrets over the network; no `deep`/heavy subagent without asking; destructive git (`reset --hard`, force push, branch delete) only on the owner's request; a new branch name is proposed to the owner before work starts.
- **Verification**: `python scripts/docs_audit.py score` (the file is always loaded: it adds tokens; keep the section under 150 tokens) and a read-through by the owner.
- **Agent**: `bm-mechanic` after the owner approves the wording.

## WO-CONF-1 — confidence in the planner's output, tied to evidence

- **Objective**: a second gate after the Readiness Gate that says how sure the plan is and why, so a low-confidence plan stops before execution.
- **Files**: `.claude/agents/bm-planner.md` (output format), `.claude/skills/bm-work-order/SKILL.md` (template and gate).
- **Change**: each Work Order gets `- **Confidenza**: alta | media | bassa — <evidence>`. The planner may write `alta` only if all hold: the files and symbols were read in this session (listed); a verification command exists *and was run on the unmodified code* (its baseline result is quoted); no open question is left. `media` if one of them is missing (named). `bassa` if the cause or the design is a hypothesis. Rule for the router: `alta` → execute; `media` → execute only a probe first (a short run of the verification or of the risky call); `bassa` → back to the owner or `bm-deep` with a problem statement.
- **Verification**: re-plan two past tasks (AGY-09 step 6; the video 4 crash) and check that the first is `alta` and the second `bassa` before the serial log existed.
- **Agent**: `bm-light`.

## Order of work and what needs a decision

| # | Work Order | Ready for a low agent? | Decision needed |
|---|---|---|---|
| 1 | WO-BAR-1 | yes, after the wording is approved | the wording (owner) |
| 2 | WO-REV-1 | not yet | the table `review.toml` (planner + owner) |
| 3 | WO-REV-2 | yes once WO-REV-1 exists | none |
| 4 | WO-CONF-1 | yes | none; the two re-plans are the test |

Each takes under 45 minutes. WO-REV-2 is the one that would have saved most in the last two days (review of five AGY-09 steps, of video 4 and 6 `verify.py`).

## Readiness Gate and confidence gate: how they differ

- The **Readiness Gate** (`docs/agents/routing.md`, `### Readiness Gate`) is five yes/no questions about the **task as written**, asked *before* assigning it: files named, a deterministic diff, a verification command, ≤ 3 files, no pending decision. Anyone can check the answers on the text. Its result is a **routing**: ready → a cheap executor; not ready → the planner.
- A **confidence gate** (the other tool's, as far as can be said from its name and description: we have not seen its definition) asks the **agent how sure it is** of its own analysis or result, at a step of the workflow, and stops or asks when it is low. It is about the output, it is a judgement of the agent and its meaning depends on how it is defined.
- They overlap in the aim (do not go on when uncertain) and differ in the **subject** (the specification versus the agent's understanding), the **time** (before executing versus during/after each step) and the **objectivity** (checkable versus self-reported). A self-reported confidence is the weak one: models are often confident and wrong; this is why WO-CONF-1 ties `alta` to listed evidence and a baseline run, and why the other objective signals we have stay: `FAILED x2` escalation, `verify.py`, and the review.
- Together: Readiness Gate (is it specified?) → planner with evidence-based confidence (do we know?) → a probe when confidence is not high → verification command → reviewer.
