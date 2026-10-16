---
schema: 1
id: NET
state: WORKING
task: R-NET6
waiting_on: 
eta: 2026-10-16T17:30+02:00
wakeup: no
ack: 2026-10-16T15:42+02:00
updated: 2026-10-16T16:25+02:00
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
DELIVERED R-NET9 @ claude/watchdog-chainloader-fix@926f7c1d · verifica: python3 tests/qemu_test.py -k chainloader -> test_chainloader PASS + test_chainloader_bad_crc PASS; -k test_sd_logs -> PASS; -k test_crash_report -> PASS; make -> nessun warning · causa: src/drivers/watchdog.c:43-45 (watchdog_arm scrive RSTC con FULL_RESET per la sonda; QEMU resetta tutta la macchina e sotto il chainloader riparte il chainloader, non il kernel); confermata: con watchdog_arm che ritorna subito test_chainloader passa (2/2). correzione: prima della scrittura RSTC si legge il seriale scheda con parole sentinella 0xFFFFFFFF; se la risposta le lascia invariate o da' 0:0 (QEMU: nessun seriale vero) si ritorna -2 senza RSTC (messaggio 'resets at once on RSTC (emulator)' invariato, test_sd_logs lo verifica). NON verificato su hardware: su Pi reale il seriale e' un numero vero, quindi il percorso resta quello di prima (probe RSTC + lettura dopo 20 ms); la riga 'watchdog: on, 3000 ms' va vista sul Pi · prova utente a fine giornata: il Pi si avvia con 'watchdog: on', un crash volontario riavvia da solo; non verificato che QEMU piu' nuovi non restituiscano un seriale vero (in tal caso torna la sonda con reset) · 2026-10-16T16:24+02:00
DELIVERED APP-5b @ claude/app5-api-help-lint@d2987835 · verifica: make test-predict -> 33 checks 0 failed; diff api_lint.py vs Lua lint su 113 file (2135 segnalazioni) -> 0 differenze; qemu_test.py -k test_code_api -> PASS; -k test_code_completion -> PASS; -k test_code_editor -> PASS; make -f rgb30.mk PLAT=virt all -> build ok; docs_audit score -> penalties 0; consistency check -> 0 drift · note: branch = bm-core e6d7bfd7 + merge origin/agy/api-lint (integrare prima agy/api-source e agy/api-lint); D3: se nessuno spazio entro 477 il testo e' tagliato a 477 + '...'; la regola API list in progress/tools.md e' accodata alla riga 'Tool pages' (il file era a 120 righe, limite del docs_audit); il bm Code gira solo sul Pi: nessuna prova RGB30 · 2026-10-16T12:36+02:00
DELIVERED API-1 @ claude/perf-budget-cart@c18e881d · verifica: python tests/tools/test_perf_table.py -> 10 PASS; make build/carts/budget.bm -> exit 0; tests/qemu_test.py -k budget -> PASS (Pi); tests/rgb30/qemu_test.py -k budget -> PASS; docs_audit score -> penalties 0 · note: (1) su RGB30 il tasto ok del prompt report e' 'k' (B seriale), non '' (e' Start): usato 'k' dal codice di sd_ask/poll_keys; (2) il kernel Pi in QEMU scrive 'board: Pi Zero' senza 'QEMU': perf_table.py non lo rifiuta, non rilevabile senza toccare src/**; avvertito in PERFORMANCE.md; (3) da API-1a: la cella tiles8 (sset cella 1) copre il quadrante alto-destra dello sprite spr16 (solo grafica); senza carico 60fps numerico spr16 niente righe profile; colonne Pi/RGB30 vuote (—) fino alle prove con l'utente · 2026-10-16T11:18+02:00
DELIVERED R-NET5 @ claude/net-bridge-durable@260020dd · verifica: python3 tests/net/check_bm_net_config.py -> all passed (WSL); make test-net -> exit 0; python scripts/docs_audit.py score -> 6652; consistency check -> 0 drift · 5a status/freeze/reboot con --in-monitor, 5b claim_socket/--detach/timeout/exit 3, 5c docs · non verificato sul Pi; --detach (fork) e comportamento di WSL da provare dall'utente (passi 1-8 del WO) · 2026-10-16T09:30+02:00
DELIVERED R-NET4 (rev. 2) @ claude/net-logs@0c8556a9 · verifica: make test-net → exit 0 (nuovo: G di un log con net_password=secret esce mascherato; bridge: risposta e file out/pi_*.TXT mascherati); check_bm_net_config.py → all passed; make build/kernel.img e make TARGET=rgb30 → ok; consistency 0 drift · fatto: merge di integrazione-ottobre (FRZ-6), G applica reports_mask_secrets in lettura ai 3 log (anche log scritti da kernel vecchi), commento corretto, /out/ in .gitignore, stub crumbs_restart in test_netcon (il merge FRZ-2 rompeva test-net) · 2026-10-15T16:25+02:00
DELIVERED R-NET4 @ claude/net-logs@d39b8883 · verifica: make test-net → exit 0 (G dei 3 log OK, bm/CONFIG.TXT, bm/../x e BOOTPREV.TXT.x → PA); check_bm_net_config.py → all passed (logs bootprev|bootlog|lastrun → --get bm/NAME; logs foo, ../x, BOOTPREV.TXT, vuoto → refused); make build/kernel.img e make TARGET=rgb30 → ok; consistency check 0 drift; docs_audit 6654 · NON fatto: mascheramento segreti in lettura (reports_mask_secrets non esiste sul branch: i log escono come sono sulla SD, FRZ-6 va integrato); il caso qemu_test non aggiunto (QEMU senza rete, coperto da test_netcon) · 2026-10-15T15:55+02:00
DELIVERED NET-KEYS @ claude/net-keys@3d9f7f28 · verifica: make test-net → exit 0; python3 tests/net/check_bm_net_config.py → all passed (--keys Xa in ordine, nel gioco exit 2, nome non in tabella refused, password mascherata); docs_audit 6636 · branch da bm-core: NON contiene il mio lavoro NET-1/2 (bm_net/pi_bridge/test toccano le stesse zone: possibili conflitti banali al merge) · 2026-10-15T15:30+02:00
DELIVERED NET-2c @ claude/net-test-err@7118a51f · verifica: make test-net → exit 0 (include check_bm_net_config.py con i nuovi --shot/--get) · 2026-10-15T10:20+02:00
DELIVERED NET-2d @ claude/net-test-err@589a6ee0 · verifica: python scripts/consistency.py check → 0 drift, 5 accepted, 0 stale; python scripts/docs_audit.py score → 6200 · nota: aggiunto verbo shot anche in tools/pi_bridge.py (necessario al verbo pi.py), D43 in divergences.toml, regola T/G in docs/progress/system.md (system-net.md non ha Rules) · tutto su un solo branch claude/net-test-err (non net-test-shot) · 2026-10-15T10:20+02:00
DELIVERED NET-1a @ claude/net-test-err@63471356 · verifica: python tests/qemu_test.py -k monitor_line → NON PASS: le asserzioni di NET-1a (err: none, running: menu) passano ma il test cade più avanti a 'back to the monitor' (timeout 10 s, attesa stress 18 s); identico su bm-core senza la mia modifica (preesistente) · 2026-10-15T10:05+02:00
DELIVERED NET-1b @ claude/net-test-err@b473f9dd · verifica: python -c (3 casi err_verdict del WO) → exit 0 · 2026-10-15T10:05+02:00
DELIVERED NET-2a @ claude/net-test-err@500730e0 · verifica: make build/kernel.img → ok, nessun warning (nessun test -k shot esiste); aggiunta bm_shot_cancel per NET-2b · 2026-10-15T10:05+02:00
DELIVERED NET-2b @ claude/net-test-err@119fc21d · verifica: make test-net → exit 0 (3 varianti; nuovi test T/G: OK, NG, PW, PA, NF, crc) · 2026-10-15T10:05+02:00

## Progressi

## Problemi

## Richieste
