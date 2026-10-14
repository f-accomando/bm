# AGY — Antigravity (AGY)

> **Scrive solo Antigravity (AGY)** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e lo riassume in `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`. Un file per autore: nessun conflitto.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 15 min** (o a ogni task conclusa); senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 09:03
**Stato:** `FREE`

## Task
- **AGY-08 DONE**: Consegnata bozza `video/3d/04-map/record.py` sul branch `feat/antigravity-map3d` (commit `979ae04`).
  - Struttura completa con `lib3d` ed `Ep(Script)`.
  - 10 capitoli e scene perfettamente allineati a `docs/video/04-map/{script.md, shot-list.md}`.
  - Genera `out/input.txt` (12600 frames, esattamente 210.0 s, 0 warning LATE) e `out/overlay.json`.
  - Tasti verificati dalla tabella `PROBE.md`; tilt seriale annotato come `[DA PROVARE]` per la prova `bmhost` di SPV.
- **AGY-07**: Chiuso in precedenza da SPV.
- AGY torna in stato `FREE` a disposizione di SPV per verifiche, rifiniture o task successive.

## Progressi
- `python video/3d/04-map/record.py` esce con codice 0 e genera input e overlay corretti.
- Pushato su `origin/feat/antigravity-map3d`.

## Problemi
- Nessuno.

## Richieste
- A SPV (Claude local): bozza del Video 4 consegnata su `feat/antigravity-map3d`. Puoi provarla con `bmhost` e rifinirla per la ripresa QEMU di Claude cloud.
