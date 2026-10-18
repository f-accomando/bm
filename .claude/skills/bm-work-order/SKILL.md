---
name: bm-work-order
description: Generator and validator of Work Orders for bm. Applies the Readiness Gate (5 questions) to determine whether a task is ready to be executed by bm-mechanic, bm-coder, bm-light or must be split by bm-planner.
---

# bm Work Order & Readiness Gate Protocol

This skill guides the generation and validation of Work Orders to ensure **maximum token efficiency**, code determinism and zero unplanned decisions during writing.

## 1. The Readiness Gate (5 Questions)
Before assigning a task to execution, answer these 5 questions:

1. **Explicit files/functions**: Are the file paths and the exact names of the functions/symbols to touch named?
2. **Diff determinism**: Is the change described such that two programmers would make the same diff (no open design choice)?
3. **Verifiable criterion**: Is there a deterministic verification command (e.g. `python tests/qemu_test.py -k <nome>`, compilation, assertion)?
4. **Contained scope**: Does it involve roughly 3 files or fewer?
5. **Zero pending dependencies**: Does it not depend on an architectural or product decision still to be made?

### Gate Outcome
- **5 out of 5 YES** $\to$ Task **READY**. Select the executing agent (`bm-mechanic`, `bm-coder`, `bm-light`).
- **At least 1 NO** $\to$ Task **NOT READY**. Invoke `bm-planner` (Gemini Pro / Opus) to investigate, resolve the ambiguities and produce one or more Work Orders.

---

## 2. Official Work Order Template

```markdown
### WORK ORDER: [ID-BREVE]
- **Obiettivo**: [1 frase chiara]
- **File/Funzioni da toccare**: [Es: src/bm/runtime.c:bm_init_cart()]
- **Modifica richiesta**: [Dettaglio del diff atteso, parametri, logica senza ambiguità]
- **Comando di verifica**: [Es: python tests/qemu_test.py -k titan]
- **Esito atteso**: [Exit code 0 / output specifico]
- **Agente designato**: [bm-mechanic | bm-coder | bm-light]
```

---

## 3. Closing Protocol
The executing agent returns one of the following states:
- **`DONE`**: Verification command run and passed successfully. Brief diff summary.
- **`BLOCKED: <motivo>`**: Incomplete specification, mismatched signature or missing decision $\to$ back to `bm-planner`.
- **`FAILED x2`**: Two consecutive fix attempts failed $\to$ escalation to `bm-light` or `bm-deep`.

---

## 4. Sync work orders (files in `sync/wo/`)
A WO for an operator in the sync protocol (`SYNC.md`) has this shape. `bm-planner` writes the text; SPV saves it as `sync/wo/<ID>.md` (a planner cannot write files) and assigns it with a Log row (`python tools/sync_log.py log ...`). A draft that is not assigned yet lives in `sync/wo/drafts/`.

```markdown
# <ID> - <NAME>: <one-line goal> (<roadmap ID>)
Starts after: <DELIVERED of the previous WO, or "now">.
Base: <branch to start from>; branch <agy|claude>/<descriptive-name>. Never push to bm-core.
Perimeter: <files and functions that may be touched>. DO NOT touch: <explicit list>.
Goal: <what is true when it is done>.

## <ID>a - <section> (<agent>, <minutes> min)
- <change, with real file and function names, no open design choice>
- Verification: <command> -> <expected result>
(sections b, c ... same shape; each section 45 minutes or less)
Completion criterion: <all verifications OK>
Delivery: the standard DELIVERED row with the real results, then FREE.

## Decisions for SPV
<open choices, each with the planner's recommendation; sections that depend on them say so>

## Proof for the user
<steps that need hardware or the user's go, as a list; the WO never orders them>
```

Rules learned from the first reviews:
- **Evidence is code that was read.** Say in the WO that every `file:line` cited must be a line that was opened and says what the claim says; a point that cannot be verified is written `not verified`.
- **Test what the WO prescribes.** A regex, a signature pattern or a filter written in a WO is run against the real files first (the `mkapi.py` pattern missed `a, b = name(...)`; the `lstrip("./")` fix stripped characters, not a prefix). When the WO cannot test it, it says so and asks for a check of the real format as step 0.
- **A test for a fix fails on the old code.** Ask for a test that fails with the previous behaviour and passes with the fix.
- **Perimeters of two WOs that run in sequence stay disjoint**, and a WO that depends on another says which one is integrated first.
- **The time estimate is a check**: a delivery ten times faster than the estimate is looked at twice (see `bm-review`).
- **Chat lines**: `preso WO <X> eta HH:MM` at the start of each WO and `concluso WO <X>: ...` at its end (the strings of the protocol, as in `SYNC.md`), one per WO, in real time (rule in `SYNC.md`).
