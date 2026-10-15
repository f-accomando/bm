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

## D28 — la lista Games dell'RGB30 legge anche `/carts` (bm-light, 30-45 min; decisione SPV 2026-10-15: strada A)
- Causa dimostrata (planner): il difetto è SOLO dell'RGB30. La voce D28 in `docs/spec/divergences.toml:318-326` ha le colonne invertite e `action = "R41"` è sbagliata. Pi: `rescan()` (`src/kernel/carts.c:415`) legge `/` e `/carts`, nessun difetto. RGB30: `scan_games()` (`src/rgb30/ui.c:239-275`) apre solo `bm` (:248), mentre Ctrl+B del bm SDK (`carts/sdk_b16/main.lua:724,849`) compila in `/carts` (`src/bm/project.c:162-193`).
- Perimetro (esteso per questo WO): `src/rgb30/ui.c`, `tests/rgb30/qemu_test.py` (SOLO un test nuovo), `docs/spec/divergences.toml` (SOLO D28), `docs/progress/tools.md` riga 86. NON toccare `carts/sdk_b16/*`, `carts/editor/*`, `tests/qemu_test.py` (test_projects del Pi resta com'è), `runtime.c`, `monitor.c`, `netxfer.c`, `mkbm.py`.
- Modifica: (1) in `struct game` (ui.c:182) aggiungere `char dir[8];` ("/bm" o "/carts"). (2) spostare il ciclo di `scan_games()` in `static void scan_dir(const char *open, const char *dir)`; filtri invariati (`bm_tool_id_of_file`, `show_bm`, `n_hidden`, `MAX_GAMES`); `scan_games()` azzera i contatori, chiama `scan_dir("bm", "/bm")` (lascia invariato `sd_state`, anche se `bm/` manca), poi sempre `scan_dir("/carts", "/carts")` (se non si apre, esce in silenzio senza toccare `sd_state`); poi `load_cover` e `qsort` come ora. (3) ogni `ksnprintf(..., "/bm/%s", games[X].name)` (ui.c righe 197, 289, 378, 794, 817, 979, 1151, 1287) diventa `ksnprintf(..., "%s/%s", games[X].dir, games[X].name)`. (4) riga 1020: `"bm/%s: ..."` → `"%s/%s: ..."` con argomenti `games[*s].dir + 1, games[*s].name` in entrambi i rami del ternario. (5) `game_of_path()` (:295-303): costruisce `"%s/%s"` (dir, name) per ogni gioco e confronta con `same_ci`; toglie il controllo sul prefisso `/bm/`. (6) NON cambiare `is_suspended`/`susp_name` né `.path = games[i].name` (:982); limite accettato: stesso nome in `/bm` e in `/carts` → entrambi "running" se uno è sospeso (scriverlo nella nota di D28). (7) test `test_games_in_carts` sul modello di `test_menu_games_and_hidden_bm` (`tests/rgb30/qemu_test.py:256`): `make_sd(tmp, {"bm/config.txt": b"game_intro=0
", "bm/tryme.b16": <come test_sdk_tool :761>, "carts/built.bm": mkbm.pack(TRY_ME.encode(), title="Built in carts", author="tests", res=(360, 360))})`; verifica `"cartridge menu: 2 games"` in `boot(q)` e `"Built in carts"` e `"Try me"` in `screen_all(q.screendump())`; secondo avvio con `show_bm=0`: `"cartridge menu: 1 games"` e `"(1 .bm hidden by show_bm=0"` sul seriale. (8) D28: `pi = "-"`, `rgb30 = "Games read only bm/: a game built by bm SDK's Ctrl+B in /carts was not listed"`, `cause = "history"`, `status = "untested"`, `evidence = "src/rgb30/ui.c:248; src/bm/project.c:162-193; carts/sdk_b16/main.lua:724,873"`, `action = "scan_games lists /carts too (as carts.c rescan)"`, `note` = commit e test QEMU, non provato su RGB30. (9) `docs/progress/tools.md:86`: togliere "A game built in /carts is not in Games (R41)." e aggiungere "Games (RGB30) lists bm/ and /carts (D28)".
- Verifica: `make -f rgb30.mk PLAT=virt all && python tests/rgb30/qemu_test.py --build build/rgb30-virt -k games_in_carts` → returncode 0; regressione `-k menu_games_and_hidden_bm` e `-k sdk_tool` → 0; `python scripts/consistency.py check` senza errori su D28; `python scripts/docs_audit.py score` senza link rotti.
- Non verificabile senza console: lettura di `/carts` sulla SD vera RGB30 (nomi 8.3, maiuscole), e il flusso a mano (Ctrl+B nell'SDK → Start+Select → il gioco in Games): lo prova l'utente, D28 resta `untested`.
- Fuori dal WO (li gestisce SPV): la frase "(the Games tab lists bm/)" in `carts/sdk_b16/main.lua:873` e il controllo `tests/studio/sdk_b16_host.lua:1038` diventano falsi; il Dev RGB30 non elenca i `.bme` come il Pi (nuova divergenza da registrare).

