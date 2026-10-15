---
schema: 1
id: AGY
state: WORKING
task: AGY-1
waiting_on: 
progress: 0
eta: 2026-10-15T18:30+02:00
wakeup: no
ack: 2026-10-15T17:48+02:00
updated: 2026-10-15T17:55+02:00
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
DELIVERED FRZ-6 @ agy/freeze-reports@803b53f6 · verifica: make test-github && python tests/qemu_test.py -k sd_logs && python tests/qemu_test.py -k reports → PASS (segreti mascherati in LASTRUN, BOOTPREV, BOOTLOG), exit 0 · 2026-10-15T15:17+02:00
DELIVERED FRZ-1 @ agy/freeze-reports@deb6196d · verifica: make build/kernel.img && make -f rgb30.mk PLAT=virt all && python tests/qemu_test.py -k sd_logs → exit 0, PASS · 2026-10-15T15:21+02:00
DELIVERED FRZ-2 @ agy/freeze-reports@5536f070 · verifica: grep -rn "watchdog_reboot\|watchdog_halt" src && python tests/qemu_test.py -k sd_logs && python tests/qemu_test.py -k update → PASS, exit 0 · 2026-10-15T15:28+02:00
DELIVERED FRZ-3 @ agy/freeze-reports@5d6754ae · verifica: python tests/qemu_test.py -k sd_logs && python tests/qemu_test.py -k crash_report && python tests/qemu_test.py -k boot_banner → 3/3 PASS, exit 0 (watchdog arming vero testato in QEMU con flag .noinit, log emulatore verificato) · 2026-10-15T15:33+02:00
DELIVERED FRZ-4 @ agy/freeze-reports@63e10ff0 · verifica: python tests/qemu_test.py -k crash_report && python tests/qemu_test.py -k sd_logs && make -f rgb30.mk PLAT=virt all → PASS (kernel version nel report crash e bootprev, no byte nulli, anello ripulito), exit 0 · 2026-10-15T15:38+02:00
DELIVERED FRZ-7 @ agy/freeze-reports@16736b2f · verifica: python scripts/docs_audit.py score && python scripts/consistency.py check → score 6203, penalties 0, 0 drift · 2026-10-15T15:40+02:00

## Progressi
- Completata l'intera sequenza SDLOG-1 → SDLOG-5 su agy/sdlog.
- Completato D28 su agy/d28-games-carts: Games (RGB30) elenca sia /bm sia /carts; test_games_in_carts e regressioni passati.
- Completato SDLOG-FIX su agy/sdlog-prev-fix: crumbs_boot() preserva stato e snapshot ring prima del memset/reset; bootprev.txt descrive la run precedente e non il boot corrente; test_sd_logs e regressioni PASS.
- Completato FRZ-6 su agy/freeze-reports: reports_mask_secrets() pubblica e applicata a copia buffer prima di scrivere LASTRUN.TXT, BOOTPREV.TXT e BOOTLOG.TXT su SD.
- Completato FRZ-1 su agy/freeze-reports: introdotto helper comune crumbs_restart(halt) (wifi_leave + clean_exit + uart_flush + watchdog) e usato in l_reboot.
- Completato FRZ-2 su agy/freeze-reports: migrati tutti i chiamanti di riavvio/spegnimento a crumbs_restart (monitor.c, settings.c, update.c, netxfer.c).
- Completato FRZ-3 su agy/freeze-reports: watchdog_arm() abilita il conteggio vero con RSTC; gestito probe_flag in .noinit per evitare loop di reset su QEMU; watchdog_arm spostato prima di crumbs_boot() in main.c.
- Completato FRZ-4 su agy/freeze-reports: versione kernel tracciata in crumbs_t (MAGIC 0xBA5EC0DFu) e nei report; ring_putc scrive zero-terminatore e lettori scartano byte nulli e righe spezzate su wrap.
- Completato FRZ-7 su agy/freeze-reports: aggiornato docs/progress/system.md (rimossi punti 1-3 da Open work, aggiunte regole crumbs_restart e reports_mask_secrets).
- Conclusa l'intera sequenza richiesta per freeze reports (FRZ-6, FRZ-1, FRZ-2, FRZ-3, FRZ-4, FRZ-7); FRZ-5 non avviato come da direttiva.

## Problemi
- Non verificabile senza hardware fisico Pi (FRZ-3): conteggio reale del watchdog su hardware e reset da freeze (monitor 'X f'); testato e verificato in QEMU il reset immediato con flag .noinit.
- Non verificabile senza hardware fisico Pi: tempo di scrittura di ~60 KiB su SD reale prima del reset col watchdog a 3 s; lastrun saltato su un crash reale con IRQ spenti; SD lenta o piena. Test QEMU verificati con successo; prova su Pi delegata a UTE.
- Non verificabile senza hardware fisico RGB30: comportamento FAT reale SD / LFN; test QEMU virt verificati con successo; prova su RGB30 fisica delegata a UTE.

## Richieste
