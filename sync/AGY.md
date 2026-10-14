---
schema: 1
id: AGY
state: WORKING
task: AGY-10: R-NET1 monitor err command (branch agy/net-err)
waiting_on: 
progress: 5
updated: 2026-10-14T14:31+02:00
---
# AGY — Antigravity (AGY)

> **Scrive solo Antigravity (AGY)** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e lo riassume in `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`. Un file per autore: nessun conflitto.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 15 min** (o a ogni task conclusa); senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 14:31 +02:00
**Stato:** `WORKING AGY-10: R-NET1 monitor err command (branch agy/net-err)`
**Heartbeat:** (1) Sì, subagent e schedulazione dedicati attivi; (2) Aggiornamento di sync/AGY.md garantito a ogni passo concluso (<=35 min) e prima/dopo ogni operazione estesa con orario previsto.

## Task
- **AGY-10 IN CORSO (R-NET1: comando monitor err)**:
  - Branch: `agy/net-err` creato da `origin/bm-core`.
  - Obiettivo: comando monitor `err` per riportare l'ultimo errore di runtime (`bm_last_error()`) e stato del cartridge.
  - Verifiche: build kernel/rgb30, test comando, audit doc/consistency.

- **AGY-09 COMPLETATO (Passi 1-6)** su `origin/agy/sdk-720` (commit `7ceacb1`).

## Progressi
- Preso in carico `AGY-10` (R-NET1) su branch `agy/net-err`.
- Avviata analisi di `docs/design/pi-test.md`, `src/kernel/monitor.c` e `src/bm/runtime.h`.

## Problemi
- Nessuno.

## Richieste
- A SPV (Claude local): Task AGY-10 avviata su `agy/net-err`.
