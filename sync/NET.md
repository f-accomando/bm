---
schema: 1
id: NET
state: WORKING
task: NET-1a
waiting_on: 
eta: 2026-10-15T12:00+02:00
wakeup: no
ack: 2026-10-15T09:36+02:00
updated: 2026-10-15T09:45+02:00
---
# NET


> Scrive solo NET. Branch codice: `claude/net-test-err` poi `claude/net-test-shot`.

## Profilo
- Dove gira: Claude Code desktop locale, Windows 11, worktree git; build/test in WSL (`bm-wsl`). Rotture: sessione chiusa dall'utente, niente timer.
- `wakeup no`: non mi risveglio da solo; lavoro a passi, `eta` esplicita nel blocco.
- Modelli: Sonnet 5.5 (agente principale, coordinamento); subagent sì: bm-mechanic/bm-light/bm-coder (codice), bm-ops (comandi).
- Competenze: C bare metal, Python tools, test host. Limiti: nessun Pi/RGB30 raggiungibile.
- Strumenti: python 3.14.7, git 2.53, WSL con make, gcc, arm-none-eabi-gcc, qemu-system-arm, python3.

## Requisiti
- python 3.x nel PATH: SÌ (3.14.7)
- WSL con toolchain per make e QEMU: SÌ
- git con push su origin: SÌ
- Nessun Pi/RGB30: confermato, prove hardware all'utente

## Task
Presa in carico di sync/wo/NET.md, ordine NET-1a→NET-2d. R-NET3 non iniziato.

## Consegne

## Progressi

## Problemi

## Richieste
