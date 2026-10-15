---
name: docs-loop
description: Tidies CLAUDE.md, docs/progress.md and docs/progress/*.md with a Karpathy-style loop (one change, one measurement, keep or discard) to get a single index, lean docs, fewer rereads and skills from repeated workflows. Use it after modifying those files (the crab-ledger mod reminds you), at the end of a milestone, or when the user asks to optimize the docs or tokens.
---

# docs-loop: lean docs, a single index, fewer rereads

Linked to the **crab-ledger** mod: it reads its report (`~/.claude/crab-ledger-report.json`,
written at every update of the `/ledger` panel) to know which files are reread the most.
Measure = `python scripts/docs_audit.py score` (lower is better): always-loaded tokens
(CLAUDE.md + progress.md) + average tokens of the area file + penalties (orphans, broken links, files
over ~120 lines, duplicate lines, CLAUDE.md without the pointer to the index).

## 1. Baseline

```bash
python scripts/docs_audit.py score
python scripts/docs_audit.py rereads      # costo delle riletture, 7 giorni (crab-ledger)
python scripts/docs_audit.py repeats 30   # prompt e comandi ripetuti in ≥3 sessioni
python scripts/docs_audit.py best         # ultimo punteggio tenuto
```

If `rereads` says "no report": open `/ledger` once and retry.

## 2. Loop (at most 8 rounds, without asking for confirmation between rounds)

A round = **a single hypothesis**, chosen in this order:

1. an audit penalty (orphan → link it from the index; broken → fix it; over 120 lines
   → split by theme `<area>-<tema>.md` and add the line to the index; duplicate → keep a single
   copy and point to it);
2. CLAUDE.md is the largest always-loaded file: what applies to a single area goes in the
   *Rules* section of its `docs/progress/<area>.md`, leaving one line in CLAUDE.md;
3. a file among the most reread: if it is a doc, split or index it; if it is large code, one line
   in `docs/progress/runtime.md` or `reading.md` (banner or search command, never line numbers);
4. a line of `docs/progress.md` that does not help to find a file.

For each round:
1. copy the files you touch into `.claude/docs-loop/backup/` (no `git reset`: the tree has a lot
   staged);
2. modify; reread the diff: the information is **moved, not lost**;
3. `python scripts/docs_audit.py score`;
4. score dropped and no information lost → `python scripts/docs_audit.py log <score> keep "<cosa>"`;
   otherwise restore from the backup and `log <score> discard "<cosa>"`.

Stop at 8 rounds, or after 2 discards in a row, or when there are no more hypotheses.

## 3. Repeated workflows → skills

From `repeats`: a candidate is a prompt or a sequence of commands that comes back in ≥3 sessions and
that no existing skill covers (`git status`, `fetch`, `ls` alone are not skills). For each
candidate (at most 2 per round): short name, when it triggers, steps, exact commands. Write
`claude/skills/<nome>/SKILL.md` (under ~60 lines, description with the trigger) and copy it to
`.claude/skills/<nome>/`. One line in `docs/progress/tools.md` only if the skill changes the
way of working on the repo.

## 4. Rules

- A single index: `docs/progress.md`; CLAUDE.md points there and does not list other docs.
- Search with `Grep -n` / `python scripts/cindex.py <file> [parola]`, then `Read` offset/limit.
  Commands per file: `docs/progress/reading.md`; `runtime.c`: `docs/progress/runtime.md`.
- State, not chronicle; short lines; each topic in a single file. Communication in English
  (CLAUDE.md in Italian). Do not touch `*-IT.md`, `OLD*`, the historical docs.
- No commits unless the user asks.

## 5. Closing

Report: score before → after, rounds kept/discarded (`.claude/docs-loop/results.tsv`), cost
of rereads from crab-ledger, skills created or proposed.
