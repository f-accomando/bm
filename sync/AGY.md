---
schema: 1
id: AGY
state: WORKING
task: AGY-09 passo 3 (cornice sdk_b16)
waiting_on: 
progress: 20
updated: 2026-10-14T12:04+02:00
---
# AGY — Antigravity (AGY)

> **Scrive solo Antigravity (AGY)** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e lo riassume in `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`. Un file per autore: nessun conflitto.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 15 min** (o a ogni task conclusa); senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 12:04 +02:00
**Stato:** `WORKING AGY-09 passo 3 (cornice sdk_b16)`
**Heartbeat:** (1) Sì, subagent e schedulazione dedicati attivi; (2) Aggiornamento di sync/AGY.md garantito a ogni passo concluso (<=35 min) e prima/dopo ogni operazione estesa con orario previsto.

## Task
- **AGY-09 Passo 3 in corso (cornice sdk_b16)**:
  - Adattamento cornice globale in `carts/sdk_b16/main.lua` (`R.tabs/body/hints/status/strip/view`) parametrico per 720x720 e identico su 360x360.
  - Applicate le due rifiniture richieste da SPV su Passo 2 (ritorno pulito `panel, work` senza riferimenti circolari e SKIP se manca `lupa`).
  - Autorizzazione ricevuta: esecuzione in sequenza dei Passi 3, 4 e 5 su `agy/sdk-720`.
- Branch: `agy/sdk-720`.

## Progressi
- Primo blocco di stato `schema: 1` generato con `tools/sync_hdr.py AGY`.
- Ricevuta autorizzazione di SPV per avanzare in sequenza su Passi 3, 4 e 5.
- Passo 3 avviato (ETA ~30 min, fine prevista ~12:35).

## Problemi
- Nessuno.

## Richieste
- Nessuna (SPV ha concesso autonomia per i passi 3-5).
