# AGY — Antigravity (AGY)

> **Scrive solo Antigravity (AGY)** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e lo riassume in `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`. Un file per autore: nessun conflitto.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 15 min** (o a ogni task conclusa); senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 10:57
**Stato:** `WAITING SPV (AGY-09 passo 2 completato)`
**Heartbeat:** (1) Sì, subagent e schedulazione dedicati attivi; (2) Aggiornamento di sync/AGY.md garantito a ogni passo concluso (<=35 min) e prima/dopo ogni operazione estesa con orario previsto.

## Task
- **AGY-09 Passo 2 COMPLETATO**:
  - Implementato helper `L.side_by_side(r, panel_cols)` in `carts/sdk_b16/layout.lua`.
  - Comportamento: a 360x360 (<480 px) 16 colonne panel (128 px) e 232 px work; a 720x720 (>=480 px) default 30 colonne panel (240 px) e 480 px work; supporto tabelle `{ narrow = ..., wide = ... }` e stringhe `"20c"`.
  - Fix compatibilità Lua 5.4+ su variabile loop in `L.wrap`.
  - Creato test unitario eseguibile: `python tests/tools/test_layout_side_by_side.py` (PASS, 0 errori).
  - Aggiunte asserzioni di modulo in `tests/studio/sdk_b16_host.lua`.
  - Verifiche: `python scripts/consistency.py check` (0 drift, 5 accepted).
  - Pushato su branch `agy/sdk-720` (commit `c43efe1`).
- Branch: `agy/sdk-720`.

## Progressi
- Passo 1 (design) e Passo 2 (layout.lua + test) completati al 100%.

## Problemi
- Nessuno.

## Richieste
- A SPV (Claude local): Passo 2 concluso. Richiesto OK su `c43efe1` per avviare il Passo 3 (`main.lua` hub e sezioni ad altezze/larghezze parametriche).
