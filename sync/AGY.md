---
schema: 1
id: AGY
state: WORKING
task: AGY-09 passo 4 (P.sprite & P.map affiancati)
waiting_on: 
progress: 50
updated: 2026-10-14T12:07+02:00
---
# AGY — Antigravity (AGY)

> **Scrive solo Antigravity (AGY)** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e lo riassume in `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`. Un file per autore: nessun conflitto.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 15 min** (o a ogni task conclusa); senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 12:07 +02:00
**Stato:** `WORKING AGY-09 passo 4 (P.sprite & P.map affiancati)`
**Heartbeat:** (1) Sì, subagent e schedulazione dedicati attivi; (2) Aggiornamento di sync/AGY.md garantito a ogni passo concluso (<=35 min) e prima/dopo ogni operazione estesa con orario previsto.

## Task
- **AGY-09 Passo 3 COMPLETATO**:
  - Cornice globale (`R.tabs/body/hints/status/strip/view`) parametrizzata in `carts/sdk_b16/main.lua` con `R.panel, R.work` derivati da `L.side_by_side` quando `SCREEN_W >= 480`. Coordinate a 360x360 rigorosamente identiche.
  - Rifiniture Passo 2 integrate: rimossa autoreferenza circolare in `L.side_by_side`, gestione graceful `SKIP (no lupa)` in `test_layout_side_by_side.py`.
  - Verifiche: `build.py` OK (148 KB), `consistency.py check` (0 drift, 5 accepted), `test_layout_side_by_side.py` PASS. Pushato al commit `ed0e130` su `origin/agy/sdk-720`.
- **AGY-09 Passo 4 in corso (P.sprite & P.map)**:
  - Adattamento visualizzazioni 2D per layout affiancato (pannello a sinistra, work a destra quando `SCREEN_W >= 480`).
- Branch: `agy/sdk-720`.

## Progressi
- Passi 1, 2 e 3 completati e pushati.
- Passo 4 in corso (ETA ~35 min, fine prevista ~12:45).

## Problemi
- Nessuno.

## Richieste
- Nessuna (procedo in sequenza verso il Passo 5).
