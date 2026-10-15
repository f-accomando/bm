# Work order operatore AGY — D19: log su SD anche per il Pi (SDLOG-1..5)

Base: `bm-core`; branch di lavoro `agy/sdlog` da `bm-core` aggiornato. Verifica solo con QEMU/host (nessun Pi): `python tests/qemu_test.py -k <nome>` e `python tests/rgb30/qemu_test.py -k <nome>`, mai l'intera suite. Ordine: SDLOG-1 → 2 → 3 → 4 → 5. Ogni WO ≤45 min.
Perimetro: `src/kernel/sdlog.c|.h` (nuovi), `src/rgb30/main.c`, `rgb30.mk`, `src/kernel/main.c`, `src/drivers/watchdog.c|.h`, `tests/qemu_test.py` (solo `test_sd_logs`), `docs/spec/divergences.toml` (SOLO voce D19), `docs/progress/system.md`. NON toccare: `src/kernel/monitor.c`, `src/bm/runtime.c|.h`, `src/net/netxfer.c`, `scripts/mkbm.py`, `carts/editor/*` (sono di NET e B16).
Perché: chiude la divergenza D19 del gate 1.0.0 e dà al Pi senza seriale un post-mortem leggibile dal PC. I riavvii da `monitor.c:448` e `netxfer.c:538` passano già da `watchdog_reboot()`: l'hook va messo lì, non in quei file.

## Decisioni di SPV (2026-10-15)
- (a) nomi dei file quelli di D19: `bm/bootlog.txt`, `bm/bootprev.txt`, `bm/lastrun.txt`.
- (b) in `docs/spec/divergences.toml` AGY tocca solo D19 (NET aggiunge una voce propria per T/G: righe diverse, il merge è di SPV).
- (c) `lastrun` scritto anche prima di un riavvio da kernel ricevuto in rete (`netxfer.c:538`) e dopo un aggiornamento (`update.c:457`), come già fa RGB30: basta l'hook in `watchdog_reboot()`.
- (d) il traceback Lua di R3 è fuori scope (è `runtime.c`, di NET).

## SDLOG-1 — modulo comune (bm-coder)
- File: nuovi `src/kernel/sdlog.c|.h`; da `src/rgb30/main.c` si spostano `save_prev()` (169-209), `save_bootlog()` (214-225), corpo di `rgb30_save_lastrun()` (245-262); aggiungere `src/kernel/sdlog.c` a `rgb30.mk:61` (il Makefile del Pi lo prende da solo, `find src` riga 71).
- Modifica: `void sdlog_prev(void)`, `void sdlog_boot(int say)`, `void sdlog_lastrun(void)` con testo identico all'originale; `sdlog_boot` e `sdlog_lastrun` tornano subito se `!sd_blocks()`. Il controllo DAIF e `fat_writing()` resta nel wrapper `rgb30_save_lastrun()`, che poi chiama `sdlog_lastrun()`. In `main.c` le chiamate diventano `sdlog_*`; i `kprintf` restano uguali.
- Verifica: `make TARGET=rgb30` e `python tests/rgb30/qemu_test.py -k bootlog_on_sd` → exit 0.

## SDLOG-2 — salvataggi del Pi all'avvio (bm-coder)
- File: `src/kernel/main.c`, funzione che contiene le righe 260-295.
- Modifica: dopo `carts_init(); config_load();` (riga 261) `sdlog_prev()` poi `sdlog_boot(0)`; subito prima di `carts_menu(&fb)` (riga 295) `sdlog_boot(1)`. Il guard su `sd_blocks()` copre l'avvio senza SD.
- Verifica: `make` senza errori; `python tests/qemu_test.py -k boot_banner` e `-k crash_report` → exit 0.

## SDLOG-3 — lastrun a ogni riavvio/spegnimento del Pi (bm-coder)
- File: `src/drivers/watchdog.c|.h` (`watchdog_reboot()` riga 72), `src/kernel/main.c`.
- Modifica: in `watchdog.h` `void watchdog_before_reboot(void (*fn)(void));`; `watchdog.c` tiene un puntatore statico e `watchdog_reboot()` lo chiama prima del primo `mmio_write`, solo se impostato. In `main.c` `static void pi_lastrun(void)`: legge `cpsr`, torna subito se bit 7 (I) è 1, se `fat_writing()` o se `!sd_blocks()`; altrimenti stampa "restarting: this run's log in bm/lastrun.txt", chiama `sdlog_lastrun()` e `uart_flush()`. Registrata subito dopo `sdlog_prev()`. `src/rgb30/glue.c:96` resta com'è.
- Verifica: `make` senza errori; `python tests/qemu_test.py -k crash_report` → exit 0 (il crash ha gli IRQ spenti: il salvataggio è saltato).

## SDLOG-4 — test QEMU del Pi (bm-coder)
- File: `tests/qemu_test.py`, nuova `test_sd_logs` sul modello di `test_crash_report` (~5241) e `_sd_files` (~5728).
- Modifica: mksd con config `layout=us
wifi_boot=0
` e `demo.bm`; attendi MENU, invia `q`, PROMPT, poi `r`; attendi "restarting: this run's log in bm/lastrun.txt" e di nuovo MENU; chiudi QEMU. Con `mtype` verifica: `::/BM/LASTRUN.TXT` contiene "rebooting..."; `::/BM/BOOTPREV.TXT` contiene "--- the run before"; `::/BM/BOOTLOG.TXT` contiene "cartridge menu".
- Verifica: `python tests/qemu_test.py -k sd_logs` → exit 0.

## SDLOG-5 — documentazione e gate (bm-light)
- File: `docs/spec/divergences.toml` (solo D19), `docs/progress/system.md` (riga R3 di Open work: resta aperto solo il traceback Lua; in `## Rules`: "all SD logs only via src/kernel/sdlog.c").
- Modifica: D19 risolta secondo `docs/spec/CONSISTENCY.md`.
- Verifica: `python scripts/consistency.py check` con D19 non più bloccante; `python scripts/docs_audit.py score` senza link rotti.

## Non verificabile senza hardware
Tempo di scrittura di ~60 KiB su SD vera prima del reset col watchdog a 3 s; `lastrun` davvero saltato su un crash vero; SD lenta o piena. Dichiararlo nella consegna; la prova sul Pi la fa l'utente (o AGY quando l'accesso al Pi sarà completo).

## Seguito (dopo SDLOG-5, WO da scrivere da SPV)
D28: un gioco compilato in `/carts` non compare in Games dopo Ctrl+B dall'editor: `rescan()` in `src/kernel/carts.c` + test QEMU sul modello di `test_projects`; l'etichetta "R41" su D28 è sbagliata (R41 = scaling). 45-60 min.
