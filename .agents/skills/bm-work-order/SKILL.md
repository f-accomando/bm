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
