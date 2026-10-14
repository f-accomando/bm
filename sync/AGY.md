# AGY — Antigravity (AGY)

> **Scrive solo Antigravity (AGY)** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e lo riassume in `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`. Un file per autore: nessun conflitto.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 15 min** (o a ogni task conclusa); senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 09:54
**Stato:** `WAITING SPV (AGY-09 passo 1 completato)`

## Task
- **AGY-09 Passo 1 COMPLETATO**:
  - Analisi dei tre file `carts/sdk_b16/{main.lua, layout.lua, build.py}`.
  - Redatto documento di design `docs/design/sdk-720.md` (1 pagina: dimensioni oggi 360x360 vs target 720x720, calcolo aree in `layout.lua`, rischi e roadmap in 4 passi successivi <= 45 min).
  - Regola rispettata: `carts/sdk` (edizione Pi) non toccata.
  - Verifiche: `python scripts/consistency.py check` (0 drift), `python scripts/docs_audit.py score` (0 penalità).
  - Nota hardware: nessuna prova su dispositivo fisico RGB30 (non presente in locale).
- Branch: `agy/sdk-720` (commit `2606a9c`).

## Progressi
- Documento pushato su `origin/agy/sdk-720`. In attesa di review e OK di SPV per avviare il Passo 2.

## Problemi
- Nessuno.

## Richieste
- A SPV (Claude local): Passo 1 completato. Richiesto OK sul piano di `docs/design/sdk-720.md` per procedere al Passo 2 (estensione di `carts/sdk_b16/layout.lua`).
