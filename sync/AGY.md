---
schema: 1
id: AGY
state: FREE
task: AGY-15
waiting_on: 
progress: 100
eta: 2026-10-16T14:25+02:00
wakeup: no
ack: 2026-10-16T13:53+02:00
updated: 2026-10-16T14:07+02:00
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
DELIVERED AGY-6a @ agy/api-source@05219d60 · verifica: python tests/tools/test_mkapi.py && python scripts/mkapi.py --check -> OK (UNDOCUMENTED 0: []; NOT_IN_RUNTIME 1: [nnet]; test duplicati 9 OK) · 2026-10-15T20:10+02:00
DELIVERED AGY-6b @ agy/beta6-triage@9e517b3a · verifica: python scripts/consistency.py check -> 0 drift (21 righe analizzate con 44 citazioni verificate nel codice) · 2026-10-15T20:23+02:00
DELIVERED AGY-6c @ agy/sync-perimeter-quarantine@56e69f06 · verifica: python tests/tools/test_sync_perimeter.py -> 10/10 OK (prova reale con sync/wo/AGY.md passata) · 2026-10-15T20:29+02:00
DELIVERED AGY-7 @ agy/scaffold-workflow-it@0a3b76d · verifica: python scripts/build_workflow_pdf_it.py -> exit 0, 28 pagine (stesso numero di EN), font set identico ['Courier', 'Helvetica', 'Helvetica-Bold'], colori identici, zero parole inglesi di prosa fuori codice · 2026-10-15T21:12+02:00
DELIVERED AGY-8 @ agy/scaffold-prompt@68e7d53 · verifica: git ls-files && python scripts/selftest.py && python scripts/docs_audit.py score && python scripts/consistency.py check -> 41/41 percorsi, 40/40 selftest, score 3800 penalties 0, 0 drift · 2026-10-15T21:26+02:00
DELIVERED AGY-9 @ agy/beta6-triage@fd80458b · verifica: python scripts/consistency.py check -> 0 drift, 5 accepted, 0 stale (21 righe con citazioni rifinite D32, D35, D04, D34, D13, toml stale specificato sui campi pi/rgb30) · 2026-10-15T21:42+02:00
DELIVERED AGY-10a @ agy/scaffold-prompt@217aa8d · verifica: simulazione cartella vuota (selftest 40/40, score 3761 penalties 0, consistency check 0 drift, gate 0 blockers, agent_audit PASS) && simulazione cartella esistente (diff utente vuoto prima risposte, .scaffold a fianco con opzione b, .gitignore unito con opzione a) && grep parole vietate bm/baremetal -> 0 match, git diff vs origin/main -> solo scaffold_prompt.md · 2026-10-16T09:49+02:00
DELIVERED AGY-10b @ agy/scaffold-workflow-it@a0a938d · verifica: python scripts/build_workflow_pdf_it.py && verifica pymupdf -> exit 0, 28 pagine esatte, font set ['Courier', 'Helvetica', 'Helvetica-Bold'], zero overflow, frasi verificate a p.11 (decidere, correggere, kill, merge), p.12 (poche migliaia di token), p.16 (Quasi una necessità; Una richiesta dichiara N su 5, l'unica motivazione, e...), p.23 (riga REVIEW tradotta), p.24 (TL;DR chiarito; collega suggerito; git pull --rebase origin sync/<nome> e cancellato consegne di due operatori), p.25 (# 0 penalità) · 2026-10-16T09:50+02:00
DELIVERED BETA-7a @ agy/beta-quickstart@1a906463 · verifica: python tests/tools/test_doc_refs.py && python scripts/doc_refs.py manuals/content/en/install-sd.md manuals/content/en/editor-sdk.md -> 11/11 test OK (exit 0); install-sd /dev/sdX e /mnt/d (percorsi host); editor-sdk 0 errori · 2026-10-16T10:11+02:00
DELIVERED BETA-7b @ agy/beta-quickstart@1a906463 · verifica: python scripts/doc_refs.py --max-lines 100 manuals/content/en/quickstart.md -> exit 0 (74 righe <= 100, segnaposto G1-G4 esatti, 0 link markdown) · 2026-10-16T10:11+02:00
DELIVERED BETA-7c @ agy/beta-quickstart@1a906463 · verifica: python scripts/manuals.py check quickstart && python scripts/manuals.py stamp quickstart && python $TMP/translate_check.py manuals/content/en/quickstart.md manuals/content/it/quickstart.md --reverse --lenient-code --glossary $TMP/glossary.tsv && python scripts/doc_refs.py --max-lines 100 manuals/content/it/quickstart.md && python scripts/manuals.py build quickstart -o manuals/pdf -> exit 0 (en/it concordano, 0 errori 1 warning glossario Need/Esigenza, creati quickstart.en.pdf e quickstart.it.pdf) · 2026-10-16T10:11+02:00
DELIVERED BETA-7d @ agy/beta-quickstart@1a906463 · verifica: python scripts/docs_audit.py score && python scripts/doc_refs.py docs/GAME-GUIDE.md docs/GUIDA-GIOCHI.md -> score 6632 penalties 0; righe 1-8 introdotte 0 errori (errori preesistenti altrove documentati); parità translate_check documentata · 2026-10-16T10:11+02:00
DELIVERED AGY-11 @ agy/beta6-gate@a59a54cf · verifica: test_consistency_beta (12/12 test OK); consistency.py beta -> exit 1, 14 blockers 7 accepted 0 resolved 0 errors; consistency.py check invariato (0 drift); docs_audit score -> score 6646 penalties 0 · 2026-10-16T11:27+02:00
DELIVERED AGY-12 @ agy/sdlog-scan@ef9b9564 · verifica: python tests/tools/test_sdlog_scan.py -> 13/13 test OK; python scripts/docs_audit.py score -> score 6653 penalties 0 · 2026-10-16T11:34+02:00
DELIVERED AGY-13 @ agy/carts-layout@7f45e21d · verifica: test_carts_layout -> OK; carts_layout.py -> exit 0, layout: 26 projects, 21 with bm.info, 5 old layout, 0 errors; docs_audit score -> score 6646 penalties 0 · 2026-10-16T11:43+02:00
DELIVERED AGY-14 @ claude/app5b-lint-fixture@3393b811 · verifica: make test-predict -> 37 checks 0 failed; mutazioni M2 M4 M7 M8 -> 2 failed ciascuna · 2 file, 15 righe · non verificato su hardware · 2026-10-16T13:59+02:00
DELIVERED AGY-15 @ claude/rgb30-test-tmp-cleanup@a118467a · verifica: qemu_test.py -k sdk_pad_sprite -> PASS, /tmp/bm64* prima 0 dopo 0 · 1 file, +37 -19 · non verificato su hardware (QEMU) · 2026-10-16T14:07+02:00

## Progressi
- Completato AGY-15 (QEMU-TMP) su claude/rgb30-test-tmp-cleanup@a118467a:
  - In `tests/rgb30/qemu_test.py`, implementato il tracciamento e la pulizia automatica delle cartelle temporanee `bm64*` (~129 MB ciascuna, inclusi `mon.sock`, `screen.ppm` e `sd.img`/`after.img`) create durante i test dell'RGB30.
  - Aggiunti gli helper `mktmp(prefix)` e `rm_tmps()`, sostituiti tutti i 19 siti di allocazione temporanea (`tempfile.mkdtemp(prefix="` -> `mktmp(prefix="`), e agganciato un blocco `finally: rm_tmps()` nel ciclo principale di esecuzione test in `main()`.
  - Verificato con `build/chk.py`: creazione e rimozione automatica OK (`ok True`).
  - Verificato con `tests/rgb30/qemu_test.py -k sdk_pad_sprite` e `-k sdk_pad_build`: entrambi i test passano a verde (`PASS`, `EXIT=0`) e al termine lasciano 0 cartelle `/tmp/bm64*` residue (prima: 0, durante: 2, dopo: 0; uso spazio `/tmp` invariato a 0%). Diff finale: 1 file, 37 inserimenti, 19 cancellazioni. Registrato nel ledger e pushato.
- Completato AGY-14 (APP5B-FIXTURE) su claude/app5b-lint-fixture@3393b811:
  - Estesa la fixture `tests/predict/lint_sample.lua` con 9 righe in fondo per coprire le 4 mutazioni identificate nella review di APP-5b (nomi sconosciuti in stringhe `"` e `'`, chiamata `hurt` consentita da `scripts/api_lint_allow.txt`, dichiarazione `local a, b` a nomi multipli, blocco di commento multiriga `--[[ ... ]]`).
  - Aggiunti 4 check dedicati in `tests/predict/predict_test.lua` (6 righe) verificando che `seen.zzstr`/`seen.zzsq`, `seen.hurt`, `seen.lva`/`seen.lvb` e `seen.blockcall` siano nulli.
  - Verificato: `make test-predict` passa a verde con 37 checks, 0 failed.
  - Verificate le 4 mutazioni con `build/mut.py`: ciascuna mutazione (M2, M4, M7, M8) fa fallire il test con esattamente 2 falliti (`predict: 37 checks, 2 failed`, `EXIT=2`). Diff finale: 2 file, 15 righe aggiunte, 0 tolte. Registrato nel ledger e pushato.
- Completato AGY-13 (CARTS-LAYOUT) su agy/carts-layout@7f45e21d:
  - Creato `scripts/carts_layout.py` per verificare che i progetti in `carts/` rispettino la struttura comune di `docs/PROJECT-LAYOUT.md`: legge `bm.info` (chiavi di testa `name`, `author`, `resolution` tra 640x360, 480x270, 320x180, 256x256, 360x360, 720x720, ignorando blocchi `[part ...]`), controlla i file di testa non consentiti (assets/, build/, res/ con match case-insensitive), segnalando `MISPLACED` (errore) per i progetti con `bm.info` e `note` per quelli a vecchio layout. Verifica la corrispondenza esatta con la tabella `## Migrated so far` in `docs/PROJECT-LAYOUT.md`.
  - Creata suite `tests/tools/test_carts_layout.py` con 10 casi di test unitari via subprocess su repo temporaneo (10/10 test OK).
  - Aggiunta riga di verifica in `docs/PROJECT-LAYOUT.md` dopo riga 4.
  - Verificato: `test_carts_layout.py` 10/10 OK; `carts_layout.py` -> exit 0, `layout: 26 projects, 21 with bm.info, 5 old layout, 0 errors`; `docs_audit.py score` -> score 6646 penalties 0. Registrato ledger e pushato.
- Completato AGY-12 (SDLOG-SCAN) su agy/sdlog-scan@ef9b9564 (base claude/integrazione-ottobre in bm33-int):
  - Creato `scripts/sdlog_scan.py` per l'analisi dal PC della cartella SD (`bm/` o radice con sottocartella `bm/`, case-insensitive): parsing di `bootprev.txt` (CLEAN, UNCLEAN, CRASH, NO HEADER, MISSING), `bootlog.txt` (watchdog ON/OFF/EMULATOR/NOT REPORTED e freeze/crash passati), `lastrun.txt` (conteggio righe e avviso log pieno), controllo segreti con flag `--secret` (regola reports.c:173-176, confini non alfanumerici, nota per lunghezze < 4 senza esporre il segreto), calcolo verdetto (OK, FREEZE, CRASH, INCOMPLETE) ed exit code.
  - Creata suite `tests/tools/test_sdlog_scan.py` con 13 test esaustivi su fixture realistiche (13/13 OK).
  - Aggiornato `docs/progress/system.md:78` con la documentazione d'uso dello script dopo lunghe sessioni hardware.
  - Verificato: `test_sdlog_scan.py` 13 test passati, `docs_audit.py score` -> score 6653 penalties 0. Registrato ledger e pushato.
- Completato AGY-11 (BETA6-GATE) su agy/beta6-gate@a59a54cf:
  - Implementato comando `consistency.py beta [--root DIR]` con controllo toml su `docs/spec/divergences.toml`, `platforms.toml` e `beta.toml`. Valuta ciascuna divergenza dello `scope`: resolved, accepted (hardware con capability da platforms.toml), BLOCKS BETA (hardware senza capability valida), accepted in writing (con [[accept]]), BLOCKS BETA (per tutte le altre). Controlli di validità su [[accept]] (id in scope, campi id/because/by/date non vuoti e data YYYY-MM-DD, duplicati, STALE se resolved).
  - Creato `docs/spec/beta.toml` con scope di 21 divergenze (D01-D13, D28, D30-D36) e commento normativo.
  - Creata suite `tests/tools/test_consistency_beta.py` con 12 test unitari esaustivi (12/12 OK).
  - Documentato in `docs/spec/CONSISTENCY.md` nella tabella dei file e nella sezione di descrizione dei comandi.
  - Verificato: `test_consistency_beta.py` OK; `consistency.py beta` exit 1 con 14 blockers, 7 accepted, 0 resolved, 0 errors; `consistency.py check` invariato a 0 drift; `docs_audit.py score` invariato a score 6646 penalties 0. Registrato ledger e pushato.
- Completato BETA-7 (sezioni 7a-7d) su agy/beta-quickstart@1a906463 (la sezione 7e sarà assegnata da SPV una volta integrate APP-4, APP-5b e API-1):
  - 7a: implementato linter `scripts/doc_refs.py` (regole R1 percorsi repo/glob, R2 console /carts/ e /bm/, R3 comandi make ed easy_install.sh, R4 link markdown relativi, R5 flag --final per gap, R6 --max-lines) e suite unitaria `tests/tools/test_doc_refs.py` (11/11 test passati, exit 0). Passo 0 eseguito su `install-sd.md` (segnalati solo percorsi host /dev/sdX e /mnt/d) ed `editor-sdk.md` (0 errori).
  - 7b: creato manuale `manuals/content/en/quickstart.md` (74 righe <= 100, zero codici milestone, zero link markdown, 7 capitoli numerati con azioni e schermate riscontrate nell'SDK/code, segnaposto G1-G4 esatti) e registrato in `manuals/manuals.toml` (order = 0). Verificato con `doc_refs.py --max-lines 100` (exit 0).
  - 7c: creata versione italiana `manuals/content/it/quickstart.md` con corrispondenza 1-a-1 e segnaposto DA COMPLETARE. Verificato: `manuals.py stamp quickstart` (timbro 7 capitoli), `manuals.py check quickstart` (en/it concordano), `translate_check.py` (0 errori, 1 warning glossario Need/Esigenza), `doc_refs.py --max-lines 100` (exit 0), compilati PDF `manuals/pdf/quickstart.en.pdf` e `quickstart.it.pdf`.
  - 7d: aggiornati collegamenti e indici in `docs/GAME-GUIDE.md` (riga 7-8), `docs/GUIDA-GIOCHI.md` (riga 6-7), `README.md` (riga 180), `docs/progress.md` (riga 61), `docs/MANUALS.md` (tabella manuali), `docs/progress/tools.md` (riga 80, 120 righe totali). Verificato: `docs_audit.py score` -> score 6632 penalties 0; `doc_refs.py` sulle guide -> righe 1-8 con 0 errori; `translate_check.py` parità documentata.
- Completato AGY-10 (correzioni revisioni AGY-7 e AGY-8 su repo scaffold):
  - AGY-10a su agy/scaffold-prompt@217aa8d: impostato SCAFFOLD_REF="main" (rimossa nota PR 1), aggiunta riga 42 examples/demo/ in Tabella 3 (necessaria per selftest 40/40), domanda iniziale 7 per sync multi-operatore opzionale (default no: sync omesso; yes: ramo sync/<progetto>), copertura capitoli 14.3 (progress/core.md), 14.4 (ROADMAP.md), 14.5 (consistency.toml opzionale / match), 14.8 (primo task con planner e gate), 15 (routine, metriche e potatura), esclusione esplicita di build_workflow_pdf.py; separati segnaposto reali di configurazione da argomenti di comando generici e token di log; protocollo pre-scan in sola lettura con consolidamento in un solo messaggio/turno e default opzione (b) per docs/, scripts/, tools/, .claude/; rimozione sicura senza git clean -f. Verificato con simulazioni in cartella vuota (40/40 selftest, score 3761 penalties 0, consistency check 0 drift, gate 0, agent_audit PASS) e cartella esistente (.scaffold a fianco, file utente immutati).
  - AGY-10b su agy/scaffold-workflow-it@a0a938d: corretto p.25 blocco comandi ("# 0 penalità"), ripristinate perdite di dettaglio EN su p.12 ("poche migliaia di token"), p.24 ("git pull --rebase origin sync/<nome>", "cancellato le consegne di due operatori", frase sul collega che suggerisce "non eseguire l'importatore"), stile su p.11 ("decidere, correggere, kill, merge"), p.16 ("Una richiesta dichiara N su 5, l'unica motivazione, e...", tolto "assoluta" da punteggio 5), p.23 riga REVIEW tradotta in italiano, chiarito TL;DR a p.24 sulla potatura regole. Verificato: 28 pagine esatte, font Courier/Helvetica/Helvetica-Bold identici, testi estratti e controllati con pymupdf su ciascuna pagina.
- Completato AGY-9 (TRIAGE-BIS): rifinite citazioni e note in docs/spec/beta6-triage.md (D32 input.c:243/50 per conferma A su Pi, D35 bm_port.c:185/187 per USB non pilotato su RGB30, D04 ui.c:1084 e bm_input.c:92 con accept-hardware contestabile, D03 chiarito players/BT pad, D34 separati F11/F12 da combo pad, D13 carts.c:402 chiamata bm_tool_id_of_file, toml stale specificato sui campi pi/rgb30 di D01, D02, D06, D07, D08, D13 con note gia' allineato, D09 note toml ordinamento per titolo, D28 toml stale su pi ed evidence toml fuori tema). Verificato: 21 righe, solo il file toccato, consistency 0 drift, 5 accepted, 0 stale. Pushed su agy/beta6-triage@fd80458b.
- Completato AGY-8 (scaffold_prompt.md): creato scaffold_prompt.md (288 righe) per la configurazione autonoma e non distruttiva di Claude Code. Verificato: 41/41 file del template censiti e coincidenti con git ls-files, gestione conflitti con protocollo non distruttivo (opzioni a/b/c), zero riferimenti a bm/baremetal, zero segreti, test su cartella esistente superato (diff iniziale vuoto, .scaffold creato a fianco, .gitignore preservato), test su cartella vuota superato (selftest 40/40, docs_audit 0 penalità, consistency 0 drift). Pushed su agy/scaffold-prompt@68e7d53 nel repo scaffold.
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
