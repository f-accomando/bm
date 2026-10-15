---
schema: 1
id: AGY
state: FREE
task: 
waiting_on: 
progress: 0
eta: 
wakeup: no
ack: 2026-10-15T14:25+02:00
updated: 2026-10-15T14:34+02:00
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
DELIVERED SDLOG-4 @ agy/sdlog@8b96e916 · verifica: python tests/qemu_test.py -k sd_logs → PASS test_sd_logs (5.5s), exit 0 · 2026-10-15T10:40+02:00
DELIVERED SDLOG-5 @ agy/sdlog@8123e2c2 · verifica: python scripts/consistency.py check && python scripts/docs_audit.py score → 0 drift, D19 resolved, penalties 0 (score 6206) · 2026-10-15T10:43+02:00
DELIVERED D28 @ agy/d28-games-carts@32c4ec4b · verifica: make -f rgb30.mk PLAT=virt all && python tests/rgb30/qemu_test.py --build build/rgb30-virt -k games_in_carts && python tests/rgb30/qemu_test.py --build build/rgb30-virt -k menu_games_and_hidden_bm && python tests/rgb30/qemu_test.py --build build/rgb30-virt -k sdk_tool && python scripts/consistency.py check && python scripts/docs_audit.py score → 3/3 PASS, 0 drift, score 6636 penalties 0 (console fisica non provata: hardware di UTE) · 2026-10-15T12:35+02:00
DELIVERED SDLOG-FIX @ agy/sdlog-prev-fix@46e1c4d8 · verifica: make -f rgb30.mk PLAT=virt all && python tests/rgb30/qemu_test.py -k bootlog_on_sd && python tests/qemu_test.py -k sd_logs → 2/2 PASS, exit 0 (RAM sopravvive in QEMU: bootprev contiene solo la run prima; prova su Pi fisico di UTE) · 2026-10-15T14:34+02:00

## Progressi
- Completata l'intera sequenza SDLOG-1 → SDLOG-5 su agy/sdlog.
- Completato D28 su agy/d28-games-carts: Games (RGB30) elenca sia /bm sia /carts; test_games_in_carts e regressioni passati.
- Completato SDLOG-FIX su agy/sdlog-prev-fix: crumbs_boot() preserva stato e snapshot ring prima del memset/reset; bootprev.txt descrive la run precedente e non il boot corrente; test_sd_logs e regressioni PASS.

## Problemi
- Non verificabile senza hardware fisico Pi: tempo di scrittura di ~60 KiB su SD reale prima del reset col watchdog a 3 s; lastrun saltato su un crash reale con IRQ spenti; SD lenta o piena. Test QEMU verificati con successo; prova su Pi delegata a UTE.
- Non verificabile senza hardware fisico RGB30: comportamento FAT reale SD / LFN; test QEMU virt verificati con successo; prova su RGB30 fisica delegata a UTE.

## Richieste
