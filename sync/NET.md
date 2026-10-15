---
schema: 1
id: NET
state: WORKING
task: NET-2c
waiting_on: 
eta: 2026-10-15T12:00+02:00
wakeup: no
ack: 2026-10-15T09:36+02:00
updated: 2026-10-15T10:06+02:00
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
DELIVERED NET-1a @ claude/net-test-err@63471356 · verifica: python tests/qemu_test.py -k monitor_line → NON PASS: le asserzioni di NET-1a (err: none, running: menu) passano ma il test cade più avanti a 'back to the monitor' (timeout 10 s, attesa stress 18 s); identico su bm-core senza la mia modifica (preesistente) · 2026-10-15T10:05+02:00
DELIVERED NET-1b @ claude/net-test-err@b473f9dd · verifica: python -c (3 casi err_verdict del WO) → exit 0 · 2026-10-15T10:05+02:00
DELIVERED NET-2a @ claude/net-test-err@500730e0 · verifica: make build/kernel.img → ok, nessun warning (nessun test -k shot esiste); aggiunta bm_shot_cancel per NET-2b · 2026-10-15T10:05+02:00
DELIVERED NET-2b @ claude/net-test-err@119fc21d · verifica: make test-net → exit 0 (3 varianti; nuovi test T/G: OK, NG, PW, PA, NF, crc) · 2026-10-15T10:05+02:00

## Progressi

## Problemi

## Richieste
