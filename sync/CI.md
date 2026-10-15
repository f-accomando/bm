---
schema: 1
id: CI
state: WORKING
task: D-SCREEN
waiting_on: 
progress: 0
eta: 2026-10-15T12:45+02:00
wakeup: no
ack: 2026-10-15T10:25+02:00
updated: 2026-10-15T10:56+02:00
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
DELIVERED CI-1 @ claude/ci-repair@3486018c · verifica: parse YAML in WSL (push ignora sync/**, concurrency, timeout 5/30/45/30/30) → OK; effetto reale visibile solo nella CI · 2026-10-15T10:55+02:00
(una riga per work order finito: `DELIVERED {WO-ID} @ {branch}@{commit} · verifica: {comando} → {risultato} · {ISO}`)
DELIVERED D-MON @ claude/ci-repair@9fe1f325 · verifica: make test selettivo -k test_monitor_line (WSL, qemu 10.2.1) → 1/1 passed · 2026-10-15T11:20+02:00
DELIVERED D-RES @ claude/ci-repair@2c0a91b9 · verifica: make test-res (WSL) → 34/34 (prima 33/34) · 2026-10-15T11:35+02:00
DELIVERED D-LOAD @ claude/ci-repair@c8e2cf22 · verifica: tests/qemu_test.py -k test_projects (WSL) → 1/1 passed · 2026-10-15T12:00+02:00

## Progressi

## Problemi
- D-LOAD: causa = non Makefile:195 né overbit. Dal 2026-10-10 (carts.c, aeb0a14b) la suite non mostra lo splash, quindi "bm: loaded in" non arriva mai per un progetto aperto nell'SDK; falliva anche in locale. Tolta l'attesa, il test controlla lo schermo. Lo stesso vale per altri test che aspettino lo splash dopo l'apertura di un tool (nessun altro caso in qemu_test.py).
- D-MON: la diagnosi del WO (timeout 10 s troppo corto) non basta: alzare a 40 s falliva ancora. "back to the monitor" è stampato solo da carts.c:2037 (uscita dal menu), mai dalla riga b3d; l'attesa a tests/qemu_test.py:5155 era obsoleta e l'ho tolta (le attese successive coprono lo stesso flusso). Da rivedere in CI.
- Overbit Makefile:195/:645: fuori perimetro (decisione 2), da segnalare a SPV quando D-LOAD lo conferma.

## Richieste
