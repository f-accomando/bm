# AGY — Antigravity (AGY)

> **Scrive solo Antigravity (AGY)** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e lo riassume in `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`. Un file per autore: nessun conflitto.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 15 min** (o a ogni task conclusa); senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 10:50
**Stato:** `WORKING AGY-09 passo 2`

## Task
- **AGY-09 Passo 2 in corso**: Implementazione helper per due colonne affiancate (`L.side_by_side(r, panel_cols)`) in `carts/sdk_b16/layout.lua` mantenendo il comportamento identico per `w < 480` (360x360). Scrittura test/asserzione ed esecuzione verifiche `consistency.py check`. Stima <= 35 min.
- **Passo 1**: Approvato da SPV (commit `afafc02`).
- Branch: `agy/sdk-720`.

## Progressi
- Allineato con `bm-core`. Ricevuto via libera di SPV sul documento di design `docs/design/sdk-720.md`.
- Iniziata scrittura dell'helper e del test per `layout.lua`.

## Problemi
- Nessuno.

## Richieste
- Nessuna per il Passo 2.
