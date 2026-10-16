---
schema: 1
id: AGX
state: FREE
task: handshake
waiting_on: 
wakeup: no
ack: 2026-10-16T11:06+02:00
updated: 2026-10-16T11:30+02:00
---
# AGX — Antigravity (AGX)

> **Scrive solo AGX** (gli altri leggono). Il supervisore (SPV) legge questo file e aggiorna la tabella §1b di `SYNC.md`.
> Aggiornare il blocco con `python tools/sync_hdr.py AGX --state ... --task ... [--waiting-on] [--progress] [--eta]`, poi commit e
> `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`. Almeno ogni 15 min e a ogni task conclusa.

## Profilo
- Dove gira: Google Antigravity IDE (sessione locale Windows 11, PowerShell; build e test via WSL Ubuntu tramite `scripts/wsl-run.ps1`). Worktree dedicati: `bm33-agx-sync` (sync) e `bm33-agx` (dev). Punti di rottura: chiusura sessione / timeout contesto / rate limit.
- Si risveglia da solo: NO (`wakeup no`); lavora a turni con tool esecutivi e subagent, dichiara `eta` nel blocco di stato.
- Modelli: Gemini 3.8 Flash (coordinamento ed esecuzione); subagent nativi / routing secondo AGENTS.md (`pro`, `flash`, `flash_lite`, `research`).
- Competenze: programmazione C bare-metal (kernel, driver, fat32), Python, Makefile, QEMU runner. Limiti: nessun Pi o RGB30 fisico raggiungibile direttamente (prove hardware delegate a UTE).
- Strumenti: Python 3.14.7 (Windows), WSL Python 3.14.4, GNU Make 4.4.1, QEMU arm/aarch64 (qemu-system-arm, qemu-system-aarch64), arm-none-eabi-gcc, aarch64-linux-gnu-gcc con picolibc, git 2.53.0.

## Requisiti
- `python` (3.x) nel PATH: SÌ (Python 3.14.7 Windows, Python 3.14.4 WSL).
- WSL con toolchain per `make` e QEMU (`scripts/wsl-run.ps1`, skill `bm-wsl`): SÌ (GNU Make 4.4.1, qemu-system-arm, qemu-system-aarch64, arm-none-eabi-gcc, aarch64-linux-gnu-gcc con picolibc).
- `git` con push su `origin`: SÌ (remote https://github.com/f-accomando/bm.git; branch di sync gestito da worktree separato `bm33-agx-sync`, codice in `bm33-agx`).
- Nessun Pi/RGB30 raggiungibile dagli operatori: SÌ (confermato, nessun hardware fisico collegato; prove hardware delegate a UTE).

## Task
- Handshake iniziale con supervisore SPV su `sync/protocollo-ottobre`.
- Disponibile come operatore AGX (FREE).

## Consegne

## Progressi
- Eseguito handshake protocollo-ottobre (ID AGX, supervisore SPV).
- Creati worktree dedicati esterni alla repo: `C:\Users\Utente\Documents\GitHub\bm33-agx-sync` e `C:\Users\Utente\Documents\GitHub\bm33-agx`.
- Inizializzato `sync/AGX.md` e impostato stato FREE con `tools/sync_hdr.py`.

## Problemi
- Nessuno al momento. Hardware fisico Pi/RGB30 non collegato direttamente (prove fisiche delegate a UTE).

## Richieste
- A SPV: configurazione blocco AGX in `sync/operators.toml` ed eventuale prima assegnazione work order su `sync/inbox/AGX.md`.
