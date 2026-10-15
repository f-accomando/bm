---
name: bm-translate
description: Translate repo documents between Italian and English (docs, CLAUDE.md-style notes, agents, skills, progress files, protocol files) and verify the translation with scripts/translate_check.py. Use for any IT to EN or EN to IT document translation in bm, before spawning a translating agent.
---

# bm-translate: translate documents, check them deterministically

The first IT to EN pass (merge of PR 19, branch `claude/docs-english`) translated CLAUDE/AGENTS/GEMINI,
progress b16/yharnam, STRESS, the design docs, agents and skills, and later ROADMAP, with six `bm-light` runs.
This skill keeps those choices so the next pass starts from them.

## When to use

- A document of the repo changes language (IT to EN, or EN to IT with `--reverse`).
- A translated file drifted from its source and needs a parity check.

## Style rules (taken from PR 19)

- Keep the structure line for line: the same headings and levels, the same list items, the same tables with the same columns,
  the same fenced code blocks.
- Keep commands, file paths, identifiers, function names, flags and captured output as written. Backtick spans that are
  labels or placeholders for the reader (`<comando>`, a UI label, a sample argument name) may be translated, as PR 19 did
  (`<Windows path>`, `zboxes(animation_name, frame)`); any span a script or tool reads literally stays as written.
  The checker pairs a translated span with its source by shape (punctuation kept, words replaced) and reports the rest.
- Translate comments inside code blocks (leading and trailing) and the prose around them; the code itself stays identical.
  Mermaid diagrams: node and edge labels are translated, the graph structure stays.
- Keep relative links valid: the target stays, only the link text is translated. When a heading is translated, update the
  internal links that point to its anchor (`#slug` follows the new title).
- Keep numbers, units, versions and milestone ids (M42, R36, §3b) exactly.
- Keep role names and states in English as they are (`bm-planner`, `Work Order`, `DONE`, `BLOCKED`, `FAILED x2`, SPV).
  In running prose `ordine di lavoro` becomes `work order`; in titles and table cells `Work Order` keeps its capitals.
- Keep titles in the title case of the source: "Gate di Prontezza" becomes "Readiness Gate", sentence-style titles stay sentence-style.
- Use the pairs of `glossary.tsv` for recurring terms. A term not in the glossary gets one translation used everywhere in the file.
  Add a pair when it recurs in real work (with an example file); delete a retired pair.
- Write rules as positive instructions: each rule says what to do. When a rule is retired, delete it. State a prohibition only
  as an exception, with the harm noted next to it (the CLAUDE.md "Writing rules" rule, which holds for translations too).
- Keep a literal user phrase that triggers behaviour in its original language: `"Fai handshake <branch>"` stays Italian in the English CLAUDE.md.

## Splitting the work

One agent per group of files, the way PR 19 ran `g1`/`g2` (for example g1 = CLAUDE/AGENTS/GEMINI + agents, g2 = skills + design docs).
`bm-light` is enough because the check below is deterministic. Give each agent: the file list, this skill, the glossary path and the
verify command. A group is about 3 to 6 files; a very large file (STRESS, SYNC.md) gets its own agent.

## Flow

1. Translate the group (when translating in place, save the original with `git show <rev>:<path>` in a temp folder first).
2. For each pair run
   `python scripts/translate_check.py SRC DST --glossary .claude/skills/bm-translate/glossary.tsv`
   (`--strict` turns glossary warnings into errors, `--lenient-code` also ignores text inside quotes in code blocks,
   `--reverse` is for EN to IT). Exit 0 is required; every line `SRC:line: DST:line: reason` is a difference to fix.
   Typical true findings: a placeholder in backticks that changed, a number or link target retyped, a line left in Italian.
3. `python scripts/docs_audit.py score`: no broken links, no orphans (penalty 0).
4. `python scripts/consistency.py check` when the change touches `docs/spec/`.

Report the status (`DONE` / `BLOCKED: <reason>`), the check output per pair and the glossary warnings left.

## Sync protocol files: one pass with the RULE row

`SYNC.md`, `sync/*`, `docs/progress/sync.md` and the protocol strings translate together in one pass, with one `RULE` row in the
SYNC.md Log that announces the new strings, because scripts read some of them. Strings the tools search for (change them in the
file and in the script in the same commit, or leave them as they are):

| File | What it reads |
|---|---|
| `tools/sync_check.py` | section header `## Consegne`; the delivery line `DELIVERED {WO-ID} @ {branch}@{commit} · verifica: ... → ... · {ISO}` (the word `verifica:`); old-format lines `**Ultimo aggiornamento:**` and `**Stato:**`; Log rows `\| YYYY-MM-DD HH:MM \| ...`; `TUTTI`/`Tutti`/`tutti` as the broadcast addressee; states `DONE WORKING WAITING IDLE FREE`; `sync/operators.toml` keys |
| `tools/sync_hdr.py` | front matter keys (`state`, `task`, `waiting_on`, `eta`, `wakeup`, `ack`, `updated`), the same two old-format lines, Log row timestamps for `--ack auto`; `waiting_on` values (`utente`, `Antigravity`, `Claude cloud`, `Claude local`) in its docstring |
| `tools/sync_watch.py` | the broadcast addressee `TUTTI`/`Tutti`/`tutti` in Log rows, the inbox path `sync/inbox/<ID>.md`, the Log row shape |
| `sync/_template.md`, `sync/operators.toml` | headers `## Profilo`, `## Requisiti`, `## Consegne` (the template feeds new operator files; `operators.toml` cites `## Profilo`) |

Chat-protocol strings that stay together: `taken WO <X>` and `done WO <X> [waiting on <Y> | FREE]: <files> files +<a> -<d>, <N> tokens, <M> min`
(from `preso WO` / `concluso WO` in the Italian rules), `da <ID> a <ID>:` for the inter-operator block, `DELIVERED`,
`REVIEW: APPROVED|CHANGES REQUESTED`, `SIGNOFF <ID>: motivo`, `RULE`. Before changing any header or label above, grep `tools/sync_*.py`
and `scripts/` for the old text and update both sides together, then run `python tools/sync_check.py` on a synced worktree.
