---
schema: 1
id: CI
state: WORKING
task: CI-1
waiting_on: 
progress: 0
eta: 2026-10-15T11:30+02:00
wakeup: no
ack: 2026-10-15T10:25+02:00
updated: 2026-10-15T10:39+02:00
---
# CI — operatore CI

> **Scrive solo CI** (gli altri leggono). Il supervisore (SPV) legge questo file e aggiorna la tabella §1b di `SYNC.md`.
> Aggiornare il blocco con `python tools/sync_hdr.py CI --state ... --task ... [--waiting-on] [--progress] [--eta]`, poi commit e
> `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`. Almeno ogni 15 min e a ogni task conclusa.

## Profilo
- Dove gira: Claude Code desktop locale, Windows 11, worktree git; build/test in WSL (`bm-wsl`). Rotture: sessione chiusa dall'utente, niente timer.
- `wakeup no`: non mi risveglio da solo; lavoro a passi con `eta` nel blocco.
- Modelli: Sonnet 5.5 (principale); subagent sì: bm-coder/bm-mechanic/bm-light, bm-ops (comandi). Niente bm-deep.
- Competenze: CI GitHub Actions, test QEMU, Python. Limiti: la CI vera si vede solo dopo un push del branch di lavoro; nessun Pi/RGB30.
- Strumenti: python 3.14.7, git 2.53, gh autenticato (f-accomando), WSL con make 4.4.1, arm-none-eabi-gcc 14.2.1, aarch64-linux-gnu-gcc 15.2.0, qemu 10.2.1.

## Requisiti
- python 3.x nel PATH: SÌ (3.14.7)
- WSL con toolchain per make e QEMU: SÌ
- git con push su origin: SÌ
- Nessun Pi/RGB30: confermato
- `gh` autenticato: SÌ
- QEMU 8.2 (versione della CI): PARZIALE. In WSL è 10.2.1 (arm e aarch64): i test che dipendono dalla versione (screendump, timing: D-SCREEN, D-RGB) possono comportarsi diversamente dalla CI; il confronto va fatto col log CI dopo un push.

## Task

## Consegne
(una riga per work order finito: `DELIVERED {WO-ID} @ {branch}@{commit} · verifica: {comando} → {risultato} · {ISO}`)

## Progressi

## Problemi

## Richieste
