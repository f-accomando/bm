# AGY-12 - SDLOG-SCAN: `scripts/sdlog_scan.py`, lettura dal PC dei log su SD dopo una sessione lunga (BETA-1, SYS-2)
Assegnato ad AGY (16 ott), indipendente da AGY-10, BETA-7, AGY-11 e AGY-13. Decisioni D1-D4 adottate come raccomandate.
Base: `claude/integrazione-ottobre` aggiornato (worktree `C:\Users\Utente\Documents\GitHub\bm33-int`). È l'unico ramo con `src/kernel/sdlog.c` e i formati FRZ (`, kernel %s`); su bm-core `sdlog.c` non c'è. Branch `agy/sdlog-scan`. Mai push su bm-core.
Perimetro: `scripts/sdlog_scan.py` (nuovo), `tests/tools/test_sdlog_scan.py` (nuovo), `docs/progress/system.md` (solo la riga 78 di `## Open work`, modificata, nessuna riga nuova).
NON toccare: `src/**` (il kernel non cambia; i formati si leggono, non si modificano), `tools/pi_bridge.py`, `tools/bm_net.py`, `tools/pi*.py`, `tests/net/*`, `tests/qemu_test.py`, `tests/rgb30/*`, `docs/design/pi-test.md` (NET), `docs/spec/*`, `docs/progress/tools.md`, `scripts/consistency.py`, `manuals/*`, `.github/workflows/*`, `SYNC.md`, `sync/*`.
Obiettivo: dopo una sessione lunga su Pi o RGB30, l'utente copia `bm/` della scheda sul PC e `python scripts/sdlog_scan.py <cartella>` dice in una schermata come è finita la run precedente (pulita, freeze, crash), se il watchdog era attivo, se `lastrun.txt` c'è, e se una password data compare in chiaro. Esce con 0 solo se è tutto a posto.
Regola di evidenza: come AGY-11; inoltre nessuna prova sul Pi (solo test host con fixture).

## Stato reale (verificato dal planner, su claude/integrazione-ottobre)
- `bootprev.txt` = vecchio `bootlog.txt` (oppure `(no bm/bootlog.txt)`, `src/kernel/sdlog.c:42`), poi l'intestazione `\n--- the run before %s; the last lines it printed (RAM) ---\n` (`sdlog.c:28`), poi l'anello (oppure `(nothing in RAM)`, :51). Scrive `BOOTPREV.TXT` (:54), `BOOTLOG.TXT` (:72), `LASTRUN.TXT` (:109), tutti in `/bm`.
- Lo stato `%s` viene da `crumbs_prev_state()`:
  - `ended cleanly, or the power went (no record in RAM)` (`src/kernel/crumbs.c:165`);
  - altrimenti `ended in a crash while: <w>, <n> s after boot, kernel <k>` oppure `ended without a clean exit (a freeze, or a restart) while: ...` (:174-176).
- Boot log dopo un freeze: `the Pi froze last time and the watchdog restarted it:` e `  doing: ...` (`crumbs.c:100-102`), con escape ANSI `\x1b[91m` / `\x1b[0m`. Dopo un crash: `the last run ended in a crash: saved as a report` (:98). Se il buffer del log conserva gli escape: `not verified` (lo scanner li toglie comunque).
- Watchdog (solo Pi): `watchdog: resets at once on RSTC (emulator), no freeze guard` (`src/kernel/main.c:259`), `watchdog: not available, no freeze guard (...)` (:261), `watchdog: on, 3000 ms (...)` (:263). In `src/rgb30` non c'è nessun `kprintf` con `watchdog` (grep del planner).
- `lastrun.txt`: `restarting: this run's log in bm/lastrun.txt` (`sdlog.c:85`), stampato prima della copia del log (:86); che finisca dentro il file: `not verified`. Log pieno: `--- (the log is full) the last lines printed ---` (:90).
- Mascheratura: `mask()` (`src/kernel/reports.c:165-179`) sostituisce con `*` solo segreti di almeno 4 caratteri, e solo se il carattere prima e quello dopo non sono alfanumerici; chiavi `github_token`, `wifi_psk`, `net_password`, `meshy_key` e la password console (:186-189). Nessuno script in `scripts/` legge oggi questi file (grep `bootprev|lastrun|freeze`: solo `render_game_shots.py`, fuori tema).

## AGY-12a - lo scanner (bm-coder, 40 min)
- CLI: `python scripts/sdlog_scan.py DIR [--secret S]...`. `DIR` è la radice della scheda copiata oppure la sua cartella `bm/`: se `DIR` contiene una sottocartella che si chiama `bm` (senza distinzione di maiuscole), si usa quella. I tre file si cercano senza distinzione di maiuscole (`bootprev.txt`, `bootlog.txt`, `lastrun.txt`), letti con `encoding="utf-8", errors="replace"`, dopo aver tolto `\x1b\[[0-9;]*m`. `DIR` inesistente: messaggio su stderr, exit 2.
- Righe di uscita, in quest'ordine:
  1. `bootprev: <CLEAN|UNCLEAN|CRASH|NO HEADER|MISSING> <stato>`. Si prende l'ultima riga che corrisponde a `^--- the run before (.*); the last lines it printed \(RAM\) ---$`. Il gruppo che inizia con `ended cleanly` → CLEAN, con `ended in a crash` → CRASH, con `ended without a clean exit` → UNCLEAN, altro → NO HEADER. Il file c'è ma senza intestazione → NO HEADER; il file non c'è → MISSING.
  2. `bootlog: watchdog <ON|OFF|EMULATOR|NOT REPORTED>; previous run <FROZE|CRASHED|->` dalle righe che iniziano con `watchdog: on`, `watchdog: not available`, `watchdog: resets at once` e da `the Pi froze last time` / `the last run ended in a crash`. File assente: `bootlog: MISSING`.
  3. `lastrun: <PRESENT (<n> lines[, log full])|MISSING>`.
  4. Per ogni `--secret` (numerato da 1): `secret #i: OK`, oppure `secret #i: LEAK in <file> (<k> times)` (una riga per file), oppure `secret #i: shorter than 4 characters, the kernel does not mask it` (nota, non errore). Conta come occorrenza solo una corrispondenza con prima e dopo un carattere non alfanumerico o il bordo del testo, la stessa regola di `reports.c:173-176`. Il segreto non si stampa mai.
  5. `verdict: <OK|FREEZE|CRASH|INCOMPLETE>`: CRASH se bootprev CRASH o previous run CRASHED; altrimenti FREEZE se bootprev UNCLEAN o previous run FROZE; altrimenti INCOMPLETE se bootprev è MISSING/NO HEADER o bootlog MISSING; altrimenti OK.
- Exit: 0 se verdict OK e nessun LEAK; 1 altrimenti; 2 per uso o I/O.
- `tests/tools/test_sdlog_scan.py` (unittest + subprocess, cartella temporanea, fixture scritte nel test con le stringhe esatte citate sopra). Casi:
  - clean → 0 / OK;
  - unclean → 1 / FREEZE;
  - crash → 1 / CRASH;
  - bootprev clean ma bootlog con `\x1b[91mthe Pi froze last time` → 1 / FREEZE;
  - `BOOTPREV.TXT` mancante → 1 / INCOMPLETE;
  - nomi minuscoli e `DIR` = radice con `bm/` → trovati;
  - le tre righe watchdog e nessuna → ON/OFF/EMULATOR/NOT REPORTED;
  - `lastrun` con la riga di log pieno → `log full`;
  - `--secret test-pass-4242` in chiaro → 1 LEAK;
  - lo stesso mascherato `**************` → 0;
  - `xtest-pass-4242y` → non è LEAK;
  - `--secret abc` → nota, exit 0;
  - `DIR` inesistente → 2.
- Verifica: `python tests/tools/test_sdlog_scan.py` → OK.

## AGY-12b - documento (bm-mechanic, 5 min)
- `docs/progress/system.md:78` (Freeze reports hardware checks): in coda alla riga ` After a long session, copy \`bm/\` from the card and run \`python scripts/sdlog_scan.py <dir> --secret <net_password>\` (verdict OK = no freeze, no crash, password masked).`
- Verifica: `python scripts/docs_audit.py score` senza righe `!` nuove (prima/dopo).

Criterio di fine: verifiche di 12a e 12b OK.
Consegna: `DELIVERED AGY-12 @ agy/sdlog-scan@<commit> · verifica: test_sdlog_scan -> OK; docs_audit score -> <esito> · <ISO>`, poi FREE. Agente consigliato: bm-coder (12a), bm-mechanic (12b).

## Decisioni (proposte del planner)
- D1 base `claude/integrazione-ottobre` (formati FRZ e `sdlog.c`); rebase su bm-core quando sarà integrato. Raccomandata: sì.
- D2 UNCLEAN conta come FREEZE nel verdetto: dopo FRZ ogni riavvio pianificato passa da `crumbs_clean_exit()` (`sync/wo/AGY.md`, sezione FRZ). Raccomandata: sì.
- D3 la regola del LEAK copia quella del kernel (almeno 4 caratteri, bordi non alfanumerici), per non segnalare come fuga ciò che il kernel lascia apposta. Raccomandata: sì. Alternativa: qualunque occorrenza.
- D4 niente lettura di `bm/config.txt` per trovare i segreti da solo: la password si passa con `--secret`, così lo script non tocca file con segreti veri. Raccomandata: sì.

## Prove per l'utente (passi; il WO non le ordina)
- Pi Zero W e RGB30: una sessione lunga (menu, 2-3 giochi, un `reboot` dalla console), poi copia di `bm/` sul PC e `python scripts/sdlog_scan.py <cartella> --secret <la tua net_password>`. Atteso: `verdict: OK`, `secret #1: OK`, sul Pi `watchdog ON`.
