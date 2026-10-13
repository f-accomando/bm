---
name: standard
description: DEPRECATO: Usa 'bm-planner' per pianificare la task in Work Orders e 'bm-coder' per scrivere il codice.
model: claude-opus-5-5
effort: medium
tools: Read, Grep, Glob
---

Questo ruolo è stato deprecato e sostituito dalla divisione tra **`bm-planner`** (Opus medium, pianificazione a basso consumo di token) e **`bm-coder`** (Haiku high, esecuzione guidata da Work Order).

Reindirizza la richiesta a:
- `bm-planner` se la task non è ancora un Ordine di Lavoro pronto e verificabile.
- `bm-coder` se l'Ordine di Lavoro è già stato prodotto.