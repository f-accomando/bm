---
schema: 1
id: AGY
state: WORKING
task: SDLOG-4
waiting_on: 
progress: 0
eta: 2026-10-15T10:55+02:00
wakeup: no
ack: 2026-10-15T10:25+02:00
updated: 2026-10-15T10:34+02:00
---
# AGY — Antigravity

> **Scrive solo AGY** (gli altri leggono). Il supervisore (SPV) legge questo file e aggiorna la tabella §1b di `SYNC.md`.
> Aggiornare il blocco con `python tools/sync_hdr.py AGY --state ... --task ... [--waiting-on] [--progress] [--eta]`, poi commit e
> `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`. Almeno ogni 15 min e a ogni task conclusa.

## Profilo
- Dove gira: Google Antigravity IDE (sessione locale Windows 11, PowerShell; build e test via WSL Ubuntu tramite `scripts/wsl-run.ps1`). Punti di rottura: chiusura sessione / timeout contesto / rate limit.
- Si risveglia da solo: NO (`wakeup no`); lavora a turni con tool esecutivi e subagent, dichiara `eta` nel blocco di stato.
- Modelli: Gemini 3.8 Flash (agente principale coordinatore ed esecutore); subagent nativi / routing secondo AGENTS.md (`pro`, `flash`, `flash_lite`, `research`).
- Competenze: programmazione C bare-metal (kernel, driver, fat32), Python, Makefile, QEMU runner. Limiti: nessun Pi o RGB30 fisico raggiungibile direttamente (prove hardware delegate all'utente).
- Strumenti: Python 3.14.7 (Windows), WSL Python 3.14.4, GNU Make 4.4.1, QEMU arm/aarch64 (qemu-system-arm, qemu-system-aarch64), arm-none-eabi-gcc, aarch64-linux-gnu-gcc (picolibc), git 2.53.0.

## Requisiti
- `python` (3.x) nel PATH: SÌ (Python 3.14.7 Windows, Python 3.14.4 WSL).
- WSL con toolchain per `make` e QEMU (`scripts/wsl-run.ps1`, skill `bm-wsl`): SÌ (GNU Make 4.4.1, qemu-system-arm, qemu-system-aarch64, arm-none-eabi-gcc, aarch64-linux-gnu-gcc con picolibc).
- `git` con push su `origin`: SÌ (remote https://github.com/f-accomando/bm.git; branch di sync gestito da worktree separato `bm33-sync-agy`).
- Nessun Pi/RGB30 raggiungibile dagli operatori: SÌ (confermato, nessun hardware fisico collegato; prove hardware delegate a UTE).

## Task

## Consegne
(una riga per work order finito: `DELIVERED {WO-ID} @ {branch}@{commit} · verifica: {comando} → {risultato} · {ISO}`)
DELIVERED SDLOG-1 @ agy/sdlog@2bb7ba64 · verifica: make TARGET=rgb30 && python tests/rgb30/qemu_test.py -k bootlog_on_sd → PASS test_bootlog_on_sd, exit 0 · 2026-10-15T10:24+02:00
DELIVERED SDLOG-2 @ agy/sdlog@2e0f747d · verifica: make build/kernel.img && python tests/qemu_test.py -k boot_banner && python tests/qemu_test.py -k crash_report → 2/2 PASS, exit 0 · 2026-10-15T10:29+02:00
DELIVERED SDLOG-3 @ agy/sdlog@88482272 · verifica: make build/kernel.img && python tests/qemu_test.py -k crash_report → PASS test_crash_report (87.5s), exit 0 · 2026-10-15T10:34+02:00

## Progressi

## Problemi

## Richieste
