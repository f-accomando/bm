---
schema: 1
id: AGX
state: WORKING
task: WO AGX-5 fix: togliere riga vuota extra Makefile
waiting_on: 
eta: 2026-10-17T09:45+02:00
wakeup: no
ack: 2026-10-17T09:27+02:00
updated: 2026-10-17T09:31+02:00
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
- Nessun Pi/RGB30 raggiungibile dagli operatori: PARZIALE (Pi 1 su rete locale a 192.168.1.208, kernel aggiornato via rete a bm dev con successo tramite bm_net.py; nessun monitor o tasto fisico collegato direttamente, verifiche fisiche delegate a UTE).

## Task
- AGX-1..AGX-15 completati e consegnati. Evasa proposta AGX-PWD.

## Proposta password bridge (AGX-PWD)
- Accesso AGX attuale: usa `bm_net.py` su porte 3333/3334; il segreto vive solo nella memoria RAM del processo chiamante (passato come variabile di processo o prompt interattivo da UTE), mai salvato su file, git o keyring.
- Confronto opzioni:
  1. File 0600 fuori repo: Rischio alto su host condivisi/Windows (NTFS non mappa 0600 nativo); rompe la regola "mai su disco"; ~5 righe (`open(p).read().strip()`).
  2. Windows Credential Manager / DPAPI: Rischio persistenza e dipendenze esterne (pywin32/keyring); rompe la regola "mai keyring/persistente"; ~20 righe.
  3. Script demone utente 1 volta al giorno (--detach): Rischio minimo (segreto solo nella RAM di `pi_bridge.py` su socket `~/.pi_bridge.sock` 0600); NON cambia la regola ("mai su disco"); ~15 righe in `pi_bridge.py`.
  4. Metodo attuale AGX (sessione effimera in memoria): Rischio nullo; richiede avvio o variabile a ogni avvio sessione; 0 righe codice.
- Raccomandazione: opzione (3) integrata con (4). L'utente avvia `tools/pi_bridge.py <IP>` una volta sola a inizio giornata digitando la password una tantum. Il bridge serve NET, AGX e tutti gli operatori via socket locale `tools/pi.py` senza mai toccare disco né keyring.

## Consegne
- DELIVERED AGX-15 @ claude/rgb30-cover-details@c0c77719 · verifica: python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k cover_details_perf -> PASS test_cover_details_perf · 3 file · base = claude/rgb30-menu-start-tab · non verificato su hardware · 2026-10-16T17:38+02:00
- DELIVERED AGX-14 @ claude/rgb30-qemu-delete-tools@6dbf3908 · verifica: python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k delete_frozen_and_tools -> PASS test_delete_frozen_and_tools · 2 file · non verificato su hardware · 2026-10-16T16:13+02:00
- DELIVERED AGX-13 @ claude/rgb30-menu-start-tab@288931f9 · verifica: python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k menu_start_confirms -> PASS test_menu_start_confirms (prima del fix: timeout) · 3 file · Tab/1-2-3-5 non verificati (nessun test), non verificato su hardware · 2026-10-16T16:08+02:00
- DELIVERED AGX-12 @ claude/test-qemu-slow@54d4c5d2 · verifica: python tests/tools/test_qemu_slow.py -> Ran 1 test OK, exit 0 · 1 file nuovo · non verificato su hardware · 2026-10-16T16:00+02:00
- DELIVERED AGX-11 @ claude/test-ci-audit@43917130 · verifica: python3 tests/tools/test_ci_audit.py (WSL) -> Ran 4 tests OK, exit 0 · 1 file nuovo · non verificato su hardware · 2026-10-16T15:58+02:00
- DELIVERED AGX-10 @ claude/test-psf2c@1c20b784 · verifica: python tests/tools/test_psf2c.py -> Ran 5 tests OK, exit 0 · 1 file nuovo · non verificato su hardware · 2026-10-16T15:57+02:00
- DELIVERED AGX-9 @ claude/test-mkbm-map@ac71c987 · verifica: python tests/tools/test_mkbm_map.py -> Ran 9 tests OK, exit 0 · 1 file nuovo · non verificato su hardware · 2026-10-16T15:55+02:00
- DELIVERED AGX-8 @ claude/test-manuals@31105761 · verifica: python tests/tools/test_manuals.py -> Ran 6 tests OK, exit 0 · 1 file nuovo · non verificato su hardware · 2026-10-16T15:54+02:00
- DELIVERED AGX-7 @ claude/code-lint-stale@710bcc09 · verifica: make exit 0; qemu_test -k code_editor -> 1/1 passed; code_api rosso anche su stash base (?ls invece di cls in OCR); comportamento nuovo non verificato da test · 1 file, 2 righe · non verificato su hardware · 2026-10-16T15:52+02:00
- DELIVERED AGX-6 @ claude/budget-map-clean@d9a87694 · verifica: sha256 di build/carts/budget.bm -> identico prima/dopo (a59a5e4065d9a83c706460eb25b1e21686e3f3718e298393264f63391a1db3fa); map.csv 46 righe, 0 vuote · 1 file, 23 righe tolte · non verificato su hardware · 2026-10-16T15:40+02:00
- DELIVERED AGX-5 @ claude/make-test-tools@7f306907 · verifica: make test-tools -> exit 0 (3 file di test); make test-docs -> exit 0 (check, 0 broken) · 1 file · non verificato su hardware · 2026-10-16T15:31+02:00
- DELIVERED AGX-4 @ claude/test-cindex@c11f0768 · verifica: python tests/tools/test_cindex.py -> Ran 5 tests OK, exit 0 · 1 file nuovo · non verificato su hardware · 2026-10-16T14:34+02:00
- DELIVERED AGX-3 @ claude/qemu-test-tmp-cleanup@3d556134 · verifica: python3 tests/qemu_test.py --build build -k monitor_line -> 1/1 passed, 0 cartelle bm-* lasciate in /tmp (prima: 2); pad_typing 1/1; sd_cartridges 1/1; sd_sdhc non verificato (QEMU non parte con immagine da 4 GiB) · 1 file, 5 righe · non verificato su hardware · 2026-10-16T14:33+02:00
- DELIVERED AGX-2 @ claude/stages-flow-checks@51cf9957 · verifica: make test-sdk-b16 -> exit 0, 318 checks, 0 failed (1 min) · 1 file, 30 righe · non verificato su hardware · 2026-10-16T13:24+02:00
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
- Connessione e aggiornamento kernel Pi 1 (192.168.1.208): compilato build/kernel.img e caricato con successo via tools/bm_net.py con password. Pi 1 riavviato su 'bm dev network console' e test GPU su hardware reale eseguito via monitor di rete.

## Problemi
- Nessuno al momento. Tutti i requisiti hardware/software e le toolchain di compilazione sono pienamente operativi.

## Richieste
- **Richiesta da Utente a SPV: Capacità di test e automazione diretta su Pi 1 fisico (`192.168.1.208`)**:
  - **Cosa AGX può già fare direttamente**:
    1. Flash remoto del kernel via rete (`tools/bm_net.py --kernel build/kernel.img`) con reboot automatico in 5 secondi (testato e operativo).
    2. Upload e avvio istantaneo di cartucce `.bm` e risorse SD via rete (`--send`, `--play`).
    3. Esecuzione di comandi monitor via rete (`--line "gpu; b3d; err; set; render"`): test hardware V3D/GPU e benchmark con generazione automatica report su SD (`bm/reports/`).
    4. Ispezione e modifica remota della configurazione (`bm/config.txt`).
  - **Cosa AGX NON può fare al momento**:
    1. Ispezione visiva / screenshot del framebuffer via rete (manca comando di grab schermo/CRC via socket di rete).
    2. Sequenze automatizzate di input gamepad per test interattivi complessi su hardware fisico (a differenza di QEMU o `bmplay`).
    3. Cattura di crash o kernel panic se avvengono prima dell'inizializzazione dello stack WiFi/rete.
  - **Proposte di sviluppo tooling utile per testing kernel, app e benchmark**:
    1. *Screenshot / Frame CRC via rete*: comando monitor `shot` o estensione `bm_net.py --screenshot` per scaricare il frame buffer o verificare il checksum CRC del render reale.
    2. *Virtual Gamepad Bot*: protocollo per iniettare tasti a frame deterministici via rete per eseguire test bot e replay di gioco su console fisica.
    3. *Automated Pi Benchmark Runner*: script dedicato (`tools/pi_bench.py`) per lanciare benchmark, raccogliere metriche (fps, triangoli, drop) e sincronizzarle con i report di progetto.
  - Se SPV approva o ritiene utile aprire un WO dedicato per questi strumenti, AGX è disponibile a svilupparli.
- **Iniziativa Utente a SPV: Bozza tecnica WO Brief per la Beta/Alfa tramite Gemini Pro (`bm-planner`)**:
  - Su iniziativa dell'Utente per saggiare le capacità di deep reasoning e planning sistemico del modello Gemini Pro (`bm-planner` / Argon tier `pro`), è stata elaborata una bozza tecnica di Work Order conforme rigorosamente ad `AGENTS.md`.
  - Si specifica espressamente che questa bozza è un contributo tecnico propositivo interamente e rigorosamente **soggetto a scrutinio, revisione o scarto da parte di SPV**. AGX non ha iniziato né inizierà alcun lavoro di implementazione sul tooling Pi senza formale approvazione/WO di SPV.

```markdown
### WORK ORDER: PI-SHOT (Proposta Tecnica — Soggetta a Scrutinio SPV)
- **Obiettivo**: Implementare il comando monitor 'shot' e l'opzione '--shot' in bm_net.py per permettere ad AGX e alla CI la verifica visiva deterministica del framebuffer reale di Pi 1 tramite CRC32 IEEE 802.3 senza saturare il buffer console.
- **File/Funzioni da toccare**:
  1. `src/kernel/monitor.c`:
     - riga 44: aggiungere `#include "lib/crc32.h"` tra gli include del kernel.
     - riga 92 (`help_text`): aggiungere il comando `shot` nell'elenco comandi della riga comandi (`"       render  room  log  err  shot  send  reboot\n"`).
     - riga 282 (`line_command`): aggiungere il ramo per il comando `shot` leggendo `console_framebuffer()`, calcolando `crc32()` sui byte della pagina attiva `fb->shown` e stampando `shot: %ux%u %ubpp pitch=%u size=%lu crc32=0x%08lx\n`.
     - riga 292: aggiornare la stringa d'errore del monitor inserendo `shot`.
  2. `tools/bm_net.py`:
     - riga 312: aggiungere argomenti CLI `--shot` e opzionale `--shot-expect CRC`.
     - riga 341: se `--shot`, eseguire `:shot\r`, validare output regex e checksum (exit code 0 su match, 1 su mismatch).
- **File da NON toccare (Perimetro Blindato)**:
  - `src/net/netcon.c` (nessun dump pixel sul ring buffer da 32 KiB)
  - `src/net/netxfer.c` (nessuna modifica al transfer binary)
  - `src/drivers/fb.c`, `src/drivers/fb.h`, `src/bm/runtime.c`, `src/lib/crc32.*`
- **Comando di verifica**:
  1. `python tests/net/check_bm_net_config.py` (exit 0)
  2. `python tools/bm_net.py 192.168.1.208 --shot` (exit 0)
  3. `python tools/bm_net.py 192.168.1.208 --shot --shot-expect <CRC>` (exit 0)
- **Agente designato**: `bm-coder` (Gemini Flash)
```

