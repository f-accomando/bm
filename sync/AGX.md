---
schema: 1
id: AGX
state: FREE
task: libero, AGX-1 consegnato; pronto per AGX-2
waiting_on: 
eta: 2026-10-16T13:50+02:00
wakeup: no
ack: 2026-10-16T13:13+02:00
updated: 2026-10-16T13:20+02:00
---
# AGX — Antigravity (AGX)

> **Scrive solo AGX** (gli altri leggono). Il supervisore (SPV) legge questo file e aggiorna la tabella §1b di `SYNC.md`.
> Aggiornare il blocco con `python tools/sync_hdr.py AGX --state ... --task ... [--waiting-on] [--progress] [--eta]`, poi commit e
> `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`. Almeno ogni 15 min e a ogni task conclusa.

## Profilo
- Dove gira: Google Antigravity IDE (sessione locale Windows 11 su host `ROCKET`, PowerShell; build e test via WSL2 Ubuntu tramite `scripts/wsl-run.ps1`). Worktree dedicati: `bm33-agx-sync` (`C:\Users\ROCKET\Documents\GitHub\bm33-agx-sync`) per `sync/protocollo-ottobre` e `bm33-agx` (`C:\Users\ROCKET\Documents\GitHub\bm33-agx`) per sviluppo. Punti di rottura: chiusura sessione / timeout contesto / rate limit.
- Si risveglia da solo: NO (`wakeup no`); lavora a turni con tool esecutivi e subagent, dichiara `eta` nel blocco di stato.
- Modelli: Gemini 3.8 Flash (coordinamento ed esecuzione); subagent nativi / routing secondo AGENTS.md (`pro`, `flash`, `flash_lite`, `research`).
- Competenze: programmazione C bare-metal (kernel, driver, fat32), Lua, Python, Makefile, QEMU runner. Limiti: nessun Pi o RGB30 fisico raggiungibile direttamente (prove hardware delegate a UTE).
- Strumenti: Python 3.12.10 (Windows), WSL Python 3.14.4, GNU Make 4.4.1 (Windows e WSL), QEMU arm/aarch64 (qemu-system-arm, qemu-system-aarch64 10.2.1), arm-none-eabi-gcc 14.2.1, aarch64-linux-gnu-gcc 15.2.0 con picolibc/libnewlib, git 2.53.0.windows.4.

## Requisiti
- `python` (3.x) nel PATH: SÌ (Python 3.12.10 Windows, Python 3.14.4 WSL Ubuntu).
- WSL con toolchain per `make` e QEMU (`scripts/wsl-run.ps1`): SÌ (GNU Make 4.4.1, qemu-system-arm 10.2.1, qemu-system-aarch64 10.2.1, arm-none-eabi-gcc 14.2.1, aarch64-linux-gnu-gcc 15.2.0, build-essential).
- `git` con push su `origin`: SÌ (`git version 2.53.0.windows.4`, push dry-run `Everything up-to-date` su `origin/sync/protocollo-ottobre`).
- Nessun Pi/RGB30 raggiungibile dagli operatori: SÌ (confermato, nessun hardware fisico collegato direttamente; prove hardware delegate a UTE).

## Task
- AGX-1 completato e consegnato. Pronto per presa in carico AGX-2.

## Consegne
- DELIVERED AGX-1 @ claude/sdk-b16-host-stubs@f56d92f5 · verifica: make test-sdk-b16 -> exit 0, 298 checks, 0 failed (1 min) · 2 file, 10 righe · non verificato su hardware · 2026-10-16T13:20+02:00

## Progressi
- Eseguito handshake protocollo-ottobre per AGX sul nuovo computer (Windows 11 host `ROCKET`).
- Configurato worktree dedicato `C:\Users\ROCKET\Documents\GitHub\bm33-agx-sync` su `sync/protocollo-ottobre`.
- Abilitata virtualizzazione AMD SVM Mode in BIOS e installato WSL2 con distribuzione Ubuntu.
- Installata ed eseguita verifica deterministica reale di tutti i requisiti:
  - `python --version` -> Python 3.12.10 (Windows) / Python 3.14.4 (WSL)
  - `make --version` -> GNU Make 4.4.1 (Windows e WSL)
  - `qemu-system-arm --version` -> QEMU 10.2.1 (WSL)
  - `qemu-system-aarch64 --version` -> QEMU 10.2.1 (WSL)
  - `arm-none-eabi-gcc --version` -> arm-none-eabi-gcc 14.2.1 (WSL)
  - `aarch64-linux-gnu-gcc --version` -> aarch64-linux-gnu-gcc 15.2.0 (WSL)
  - `git push (dry-run)` -> OK, accesso a origin verificato
  - `scripts/wsl-run.ps1` -> verificato ed operativo
- Letti `sync/inbox/AGX.md`, `sync/wo/AGX-1.md` e `sync/wo/AGX-2.md`.

## Problemi
- Nessuno al momento. Tutti i requisiti hardware/software e le toolchain di compilazione sono pienamente operativi.

## Richieste
- A SPV: registrazione operatore AGX completata; procedo con la presa in carico del primo Work Order (AGX-1).
