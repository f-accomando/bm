---
schema: 1
id: AGY
state: WORKING
task: AGY-7
waiting_on: 
progress: 100
eta: 2026-10-15T21:45+02:00
wakeup: no
ack: 2026-10-15T20:34+02:00
updated: 2026-10-15T21:11+02:00
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
DELIVERED AGY-1 @ agy/api-source@29057f75 · verifica: python tests/tools/test_mkapi.py && python scripts/mkapi.py --check → OK (UNDOCUMENTED 2: udp_open, udp_recv; NOT_IN_RUNTIME 4: a, instrument, instruments, nnet) · 2026-10-15T17:58+02:00
DELIVERED AGY-2 @ agy/api-lint@38693bc0 · verifica: python tests/tools/test_api_lint.py && python scripts/api_lint.py docs/GAME-GUIDE.md docs/GUIDA-GIOCHI.md && python scripts/docs_audit.py score → 0 errori, penalties 0 (correzioni a: 0; voci b: hurt, danno in api_lint_allow.txt) · 2026-10-15T18:03+02:00
DELIVERED AGY-3 @ agy/beta6-triage@9bebc945 · verifica: 21 righe analizzate con prove file:riga, python scripts/consistency.py check → 0 drift, 5 accepted, 0 stale · 2026-10-15T18:08+02:00
DELIVERED AGY-4 @ agy/agent-audit-baseline@b920d6a9 · verifica: python tests/tools/test_agent_audit.py && python scripts/agent_audit.py --baseline docs/agents/baseline.txt → 14/14 test OK, tabelle model/area e sezione vs baseline presenti · 2026-10-15T18:13+02:00
DELIVERED AGY-5 @ agy/sync-perimeter-quarantine@dcbc229e · verifica: python tests/tools/test_sync_perimeter.py && python tests/tools/test_quarantine_check.py && python scripts/quarantine_check.py && python scripts/docs_audit.py score → 7/7 test OK, penalties 0 (score 6624) · 2026-10-15T18:16+02:00
DELIVERED AGY-6a @ agy/api-source@05219d60 · verifica: python tests/tools/test_mkapi.py && python scripts/mkapi.py --check → OK (UNDOCUMENTED 0: []; NOT_IN_RUNTIME 1: [nnet]; test duplicati 9 OK) · 2026-10-15T20:10+02:00
DELIVERED AGY-6b @ agy/beta6-triage@9e517b3a · verifica: python scripts/consistency.py check (0 drift), 21 righe analizzate con 44 citazioni verificate nel codice · 2026-10-15T20:23+02:00
DELIVERED AGY-6c @ agy/sync-perimeter-quarantine@56e69f06 · verifica: python tests/tools/test_sync_perimeter.py (10/10 OK), prova reale con sync/wo/AGY.md passata · 2026-10-15T20:29+02:00
DELIVERED AGY-7 @ agy/scaffold-workflow-it@0a3b76d · verifica: python scripts/build_workflow_pdf_it.py → exit 0, 28 pagine (stesso numero di EN), font set identico ['Courier', 'Helvetica', 'Helvetica-Bold'], colori identici, zero parole inglesi di prosa fuori codice · 2026-10-15T21:12+02:00

## Progressi
- Completato AGY-7 (scaffold-workflow-IT.pdf): generato scripts/build_workflow_pdf_it.py e compilato scaffold-workflow-IT.pdf (28 pagine esatte, layout A4, font e colori identici all'originale EN, prosa interamente tradotta in italiano tecnico senza parole inglesi residue). Pushed su agy/scaffold-workflow-it@0a3b76d nel repo scaffold.
- Completato AGY-6c (correzione PERIM): in tools/sync_perimeter.py sostituito lstrip con normalizzazione sicura del prefisso './' (.github/workflows/* correttamente FORBIDDEN), isolata la parte iniziale prima del primo '## ', match per ID intero esatto (FRZ-1 non matcha FRZ-10), exit 2 su sezione inesistente; aggiunti 8 nuovi test unitari (10/10 PASS). Stato impostato su FREE.
- Completato AGY-6b (rifacimento triage BETA-6): riscritto docs/spec/beta6-triage.md con 21 righe, prove verificate nel codice dei due kernel (44 riferimenti reali controllati), indicazione esplicita toml stale e rispetto decisioni toml (D03 sospeso dall'utente, D09 storage-path, D35 causa hardware), branch reale claude/integrazione-ottobre@32c4ec4b per D28.
- Completato AGY-6a (correzione API-SRC): aggiornata regex runtime.c in test_mkapi.py (riconosciuti instrument/instruments), estesa SIG_RE in mkapi.py a prefissi di assegnazione (estratti udp_open e udp_recv), escluso rumore 'a' da riff.md, aggiunto test di regressione sui 9 duplicati attesi.
- Completato AGY-5 (PERIM): tools/sync_perimeter.py (controllo perimetro WO con sezioni ed espansione .c|.h), tests/quarantine.txt con validator scripts/quarantine_check.py e aggiornamento docs/progress/sync.md.
- Conclusa l'intera sequenza dei 5 Work Order assegnati (AGY-1, AGY-2, AGY-3, AGY-4, AGY-5); FRZ-5 non avviato come da direttiva; stato impostato a FREE.
- Completato AGY-4 (AUDIT): agent_audit.py aggiornato con R0 (tabelle model/area, filtri --area e --since) e D-R2 (gestione baseline, delta vs baseline, docs/agents/baseline.txt generato), test estesi a 14/14 passati.
- Completato AGY-3 (TRIAGE): redatta bozza docs/spec/beta6-triage.md con 21 divergenze classificate e motivate con riferimenti codice precisi (8 accept-hardware, 6 fix-before-beta, 6 needs-hardware-test, 1 fixed-on-branch).
- Completato AGY-2 (API-LINT): linter scripts/api_lint.py, api_lint_allow.txt e tests/tools/test_api_lint.py; guide verificate con 0 errori residui (hurt/danno in allowlist come falsi positivi di gioco).
- Completato AGY-1 (API-SRC): generatore mkapi.py, src/ai/api.tsv (337 funzioni da docs/api) e test_mkapi.py (confronto con api[] di runtime.c: rilevati UNDOCUMENTED 2 [udp_open, udp_recv] e NOT_IN_RUNTIME 4 [a, instrument, instruments, nnet]).
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
