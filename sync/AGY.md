---
schema: 1
id: AGY
state: WAITING
task: AGY-09 concluso con passo 6 (SDK_B16_RES, R39)
waiting_on: Claude local
progress: 100
updated: 2026-10-14T12:45+02:00
---
# AGY — Antigravity (AGY)

> **Scrive solo Antigravity (AGY)** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e lo riassume in `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`. Un file per autore: nessun conflitto.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 15 min** (o a ogni task conclusa); senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 12:45 +02:00
**Stato:** `WAITING AGY-09 concluso con passo 6 (SDK_B16_RES, R39)`
**Heartbeat:** (1) Sì, subagent e schedulazione dedicati attivi; (2) Aggiornamento di sync/AGY.md garantito a ogni passo concluso (<=35 min) e prima/dopo ogni operazione estesa con orario previsto.

## Task
- **AGY-09 COMPLETATO (Passi 1, 2, 3, 4, 5, 6)**:
  - **Passo 1**: Documento di analisi e architettura `docs/design/sdk-720.md` (commit `afafc02`).
  - **Passo 2**: Helper `L.side_by_side` in `carts/sdk_b16/layout.lua` e unit test `tests/tools/test_layout_side_by_side.py` (commit `c43efe1`).
  - **Passo 3**: Cornice globale `R.tabs/body/hints/status/strip/view` con `R.panel, R.work` in `carts/sdk_b16/main.lua` e rifiniture Passo 2 (commit `ed0e130`).
  - **Passo 4**: `P.sprite` e `P.map` adattivi: pannello laterale a sinistra e area lavoro espansa a destra su schermi $\ge 480\text{ px}$, coordinate identiche su 360x360 (commit `f9ea98d`).
  - **Passo 5**: `P.d3` con partizione 3D parametrizzata tramite `LY.side_by_side(R.body)` (commit `76a3bcc`).
  - **Passo 6**: Parametrizzata risoluzione in `Makefile` e `rgb30.mk` via `SDK_B16_RES ?= 360x360` passata a `mkbm.py --res $(SDK_B16_RES)`. Verificato build sia 360x360 che 720x720 (entrambi OK). R39 chiuso e spostato in stato in `docs/progress/platforms.md` (commit `7ceacb1`).
  - **Note ambiente**: `test_layout_side_by_side.py` gestisce graceful skip se manca `lupa` (testato in locale con lupa `PASS`); `bmhost` non presente su Windows locale (toolchain Linux host), verifica finale sul dispositivo RGB30 a carico dell'utente.
  - **Verifiche complessive**: `build.py` OK (150 KB), `consistency.py check` (0 drift, 5 accepted), `docs_audit.py score` (0 penalità).
- Branch: `agy/sdk-720` (commit `7ceacb1`).

## Progressi
- AGY-09 concluso in ogni sua parte (passi 1-6).
- R39 chiuso nella documentazione delle piattaforme.
- Stato: `FREE` (pronto per nuove assegnazioni; merge di `agy/sdk-720` demandato all'utente/SPV).

## Problemi
- Nessuno.

## Richieste
- A SPV (Claude local): Passo 6 completato e pushato. AGY-09 chiuso al 100%. Disponibile per i prossimi task.
