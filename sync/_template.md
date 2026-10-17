---
schema: 1
id: {ID}
state: FREE
task: handshake
waiting_on: 
progress: 0
wakeup: {yes|no}
ack: 
updated: {ISO 8601 with zone}
---
# {ID} — {name}

> **Scrive solo {ID}** (gli altri leggono). Il supervisore (SPV) legge questo file e aggiorna la tabella §1b di `SYNC.md`.
> Aggiornare il blocco con `python tools/sync_hdr.py {ID} --state ... --task ... [--waiting-on] [--progress] [--eta]`, poi commit e
> `git pull --rebase origin sync/{nome} && git push origin HEAD:sync/{nome}`. Almeno ogni 15 min e a ogni task conclusa.

## Profilo
(max 10 righe: dove gira e i suoi punti di rottura, **se si risveglia da solo (timer/scheduler: `--wakeup yes|no`)**, modelli e per cosa, subagent sì/no, competenze e limiti, strumenti con versione)

## Requisiti
(solo se `SYNC.md` §2 li elenca: per ciascuno `SÌ`, `NO` o `PARZIALE {nota}`)

## Task

## Consegne
(una riga per work order finito: `DELIVERED {WO-ID} @ {branch}@{commit} · verifica: {comando} → {risultato} · {ISO}`)
Prima del DELIVERED: `python tools/sync_hdr.py <ID> --close <WO> --commit <sha> --tokens T --min M` accoda i numeri del WO a `sync/OP-<ID>.ledger.jsonl` (solo tu lo scrivi) e stampa la riga di chat `done WO ...`; non cambia lo stato.

## Progressi

## Problemi

## Richieste
