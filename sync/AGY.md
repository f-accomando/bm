---
schema: 1
id: AGY
state: FREE
task: AGY-09 & AGY-10 merged in bm-core; ready for new tasks
waiting_on: 
progress: 100
updated: 2026-10-14T17:00+02:00
---
# AGY — Antigravity (AGY)

> **Scrive solo Antigravity (AGY)** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e lo riassume in `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`. Un file per autore: nessun conflitto.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 15 min** (o a ogni task conclusa); senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 17:00 +02:00
**Stato:** `FREE AGY-09 & AGY-10 merged in bm-core; ready for new tasks`
**Heartbeat:** (1) Sì, subagent e schedulazione dedicati attivi; (2) Aggiornamento di sync/AGY.md garantito a ogni passo concluso (<=35 min) e prima/dopo ogni operazione estesa con orario previsto.

## Profilo
- **Ambiente/Luogo**: Locale (Windows 11, Machine A). Punti di rottura: dipendenza da alimentazione/connessione locale, assenza di toolchain cross-compilazione ARM baremetal / QEMU / `bmhost` su Windows locale.
- **Modelli**: Antigravity (DeepMind Gemini / Claude orchestration), subagenti paralleli (Flash/Pro/self) per task in background e verifiche.
- **Subagent**: Sì, supporto completo per subagenti concorrenti e task schedulati (cron liveness / heartbeat attivo ogni 15m; agente principale sempre libero).
- **Competenze**: Sviluppo C e Lua (runtime bm, 3D engine `map3d`, rasterizzatore, formati `mkbm`), editor di sistema (`carts/editor`, `carts/sdk_b16`), scripting Python, verifiche di coerenza/audit (`consistency.py`, `docs_audit.py`).
- **Limiti e strumenti**: Non esegue riprese QEMU né build baremetal C direttamente in locale su Windows; esegue suite Python, script di coerenza, unit test Lua (con `lupa`) e mock network.

## Task
- **AGY-10 COMPLETATO & MERGIATO in `bm-core` (R-NET1: comando monitor err)** (commit `173019e`).
- **AGY-09 COMPLETATO & MERGIATO in `bm-core` (Passi 1-6: adaptive layout 720x720 SDK b16)** (commit `028f853`).

## Progressi
- `AGY-10` (R-NET1) e `AGY-09` (R39) mergiati su `bm-core` dall'utente.
- Audit di coerenza su `bm-core` aggiornato: `consistency: 0 drift, 5 accepted, 0 stale`, `docs_audit: score 5083, 0 penalità`.
- Stato: `FREE` (pronto per nuove assegnazioni da SPV o utente).

## Problemi
- Nessuno.

## Richieste
- A SPV (Claude local): AGY-09 e AGY-10 mergiati in `bm-core`. Antigravity è `FREE`.
