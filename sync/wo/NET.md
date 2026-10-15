# Work order operatore NET — R-NET1/2/3 (test del Pi dal PC)

Base: `bm-core`; branch di lavoro `claude/net-test-err` (NET-1) poi `claude/net-test-shot` (NET-2..), sempre da `bm-core` aggiornato.
Design: `docs/design/pi-test.md`. Regole: `## Rules` di `docs/progress/system-net.md` e `tools.md`. Ordine: NET-1a → NET-1b → NET-2a → NET-2b → NET-2c → NET-2d. Ogni WO ≤45 min.
Fatto già su `bm-core` (verificato da planner): riga `err` in `src/kernel/monitor.c:273-281`, test `tests/qemu_test.py:test_monitor_line`.

## Decisioni di SPV (2026-10-15)
- **D1**: lo screenshot via rete NON è una riga monitor `shot` (la riga `:` va solo al menu, `bm_net.py:278`; `shot_plan/shot_grab` valgono solo nel frame di una cartuccia). Si usa una op **`T`** (take) sulla porta di trasferimento, letta anche durante il gioco (`net.c:169-170`).
- **D2**: `netxfer.c` e `runtime.c` sono condivisi con RGB30: T/G valgono anche lì; finché non provato sulla console, voce in `docs/spec/divergences.toml`.
- **D3**: file di T sempre in `/bm/shots`, nome dal titolo del gioco come per i tasti.
- **D4 (R-NET3 token di test)**: NON iniziare finché NET-1/2 non sono consegnati; SPV scrive prima i WO (token nel campo password o flag header; righe permesse proposta `err,gpu,b3d,log`; op `S/P/T/G`, `S` solo in `/carts`; verificare che `config_list` nasconda le chiavi `_token`).
- Dopo i WO kernel: `python scripts/consistency.py check` e `python scripts/docs_audit.py score`.

## NET-1a — riga `running: menu` (bm-mechanic)
- File: `src/kernel/monitor.c` (ramo `"err"`), `tests/qemu_test.py:test_monitor_line`.
- Modifica: se `bm_suspended()` è falso o il titolo è vuoto, la riga `err` stampa `running: menu`. Nel test, dopo il primo `:err\r`, `assert "running: menu" in out`.
- Verifica: `python tests/qemu_test.py -k monitor_line` → PASS.

## NET-1b — `pi_test.py` da "started" a PASS/FAIL (bm-light)
- File: `tools/pi_test.py` (`main()`, nuova `err_verdict(out) -> (bad, text)`).
- Modifica: con `--cart`, dopo lo step wait aggiungere sempre lo step `("err", ["line", "err"])`. `err: none` o "back to the menu first" (gioco ancora in esecuzione) → ok; `err: <testo>` ≠ none → FAIL con il testo nel log. Aggiornare la docstring (togliere "is NOT seen here").
- Verifica: `python -c "import sys;sys.path.insert(0,'tools');import pi_test as p;assert p.err_verdict('err: none')[0]==False;assert p.err_verdict('err: attempt to call a nil value')[0];assert p.err_verdict('back to the menu first')[0]==False"` → exit 0.

## NET-2a — richiesta di screenshot dal runtime (bm-coder)
- File: `src/bm/runtime.c`, `src/bm/runtime.h`.
- Modifica: flag statico `shot_net`; `void bm_shot_ask(void)` lo mette a 1 e azzera l'ultimo nome; nel poll dei tasti (`runtime.c:~5065`) se `shot_net`==1 lo azzera e chiama `shot_plan(NULL)`; `int bm_shot_done(char *name, size_t n)` → 1 e copia `shot_file` quando `shot_grab` ha scritto il file, -1 se la scrittura è fallita, 0 se in attesa.
- Verifica: `make build/kernel.img` senza warning nuovi; `python tests/qemu_test.py -k shot` → PASS (se il test esiste).

## NET-2b — op `T` e `G` in `netxfer.c` (bm-coder)
- File: `src/net/netxfer.c` (`parse_header`, `feed`, `netxfer_poll`), `tests/net/test_netcon.c` (stub fat e runtime).
- Modifica: header accetta `T`/`G` con size 0 e crc 0 (il controllo `size==0 → SZ` vale solo per S/P/K/C). **T**: chiama `bm_shot_ask()`, attende al massimo 3 s in `netxfer_poll`, risponde `reply_text("OK", nome)` o `NG` se nessun gioco ha disegnato un frame. **G**: solo path in `bm/shots/` (case-insensitive come `same_path`), un livello, senza `..`, altrimenti `PA`; assente `NF`; oltre `MAX_SIZE` `SZ`; ok → `OK` + len32 + crc32, poi dati a pezzi di `tcp_sndbuf(peer)` con offset avanzato da `netxfer_poll` (`reply_text` fa un solo `tcp_write`, `TCP_SND_BUF`=8*1460). Stessa password di oggi.
- Test host: G ok; G fuori da shots → PA; password sbagliata → PW; T senza gioco → NG.
- Verifica: `make test-net` → exit 0.

## NET-2c — `bm_net.py --shot` e `--get` (bm-light)
- File: `tools/bm_net.py` (`open_request`, `main`), `tests/net/check_bm_net_config.py` (il server finto accetta T/G).
- Modifica: `--get PATH OUT` verifica il crc e scrive OUT; `--shot OUT` fa T poi G su `bm/shots/<nome>`; su `BH` stampa "kernel too old" ed esce con 2.
- Verifica: `make test-net` → exit 0.

## NET-2d — `pi_test.py --shot` e docs (bm-light)
- File: `tools/pi_test.py`, `tools/pi.py` (verbo `shot`; aggiornare la regola "four requests" di `docs/design/pi-test.md`), `docs/SCREENSHOTS.md`, `docs/progress/system-net.md`, `docs/spec/specs.toml`, `docs/spec/divergences.toml` (D2), `docs/progress/tools.md` (togliere R-NET1/2 da Open work).
- Verifica: `python scripts/consistency.py check` e `python scripts/docs_audit.py score` senza errori nuovi.

## NET-KEYS — tasti con nome nel bridge (bm-light, 40-45 min; decisione SPV 2026-10-15, chiesta dall'utente)
- Obiettivo: poter far premere al Pi tasti del monitor (oggi il bridge inoltra solo righe `:`), senza mai dare testo libero e senza mostrare la password. Serve ora per il crash volontario `X` poi `a` (data abort); in futuro per altri comandi a due tasti.
- Perimetro esteso: `tools/pi_bridge.py`, `tools/pi.py` (solo l'help), `tools/bm_net.py`, `tests/net/check_bm_net_config.py`. Branch `claude/net-keys` da `bm-core` aggiornato. Non toccare il kernel.
- Modifica 1, `tools/bm_net.py`: opzione `--keys SEQ` (stringa, solo i caratteri `a-z A-Z 0-9`, max 8). Dopo il login, come `--line` (stessi controlli di `run_line`: se il focus dei tasti è "the buttons", "the application" o "a page" stampa lo stesso avviso ed esce 2), manda i byte di SEQ uno alla volta con una pausa di 0,2 s, poi legge e stampa quello che la console scrive per 3 s (o fino alla chiusura della connessione) ed esce 0.
- Modifica 2, `tools/pi_bridge.py`: nuova op `keys NOME`. NOME deve essere una chiave di una tabella fissa `KEYS = {"monitor": "q", "crash-dabt": "Xa", "crash-undef": "Xu", "crash-prefetch": "Xb"}` nel file; qualsiasi altro nome: `refused`. NON aggiungere `f` (freeze) né sequenze libere. La riga di comando chiama `bm_net.py <host> --keys <SEQ>`; la risposta passa dal solito `replace(pw, "***")`.
- Modifica 3, `tools/pi.py`: nell'help della prima riga aggiungere `keys NOME`.
- Modifica 4, `tests/net/check_bm_net_config.py`: contro la console finta già presente, un caso `--keys Xa`: la console finta riceve `X` poi `a` in quest'ordine e il testo di risposta compare nell'output; un caso con focus "the application": exit 2 e nessun byte mandato; e una prova di `pi_bridge.run` (con `subprocess.run` sostituito) che `keys crash-dabt` produce `--keys Xa` e che `keys foo`, `keys Xa` e `keys` danno `refused`.
- Verifica: `make test-net` → exit 0; `python3 tests/net/check_bm_net_config.py` → tutti i casi OK; `python scripts/docs_audit.py score` senza link rotti. Aggiornare `docs/design/pi-test.md` (tabella delle op del bridge: riga `keys NOME`, elenco dei nomi, nota che `crash-*` fa cadere il Pi e che sul Pi Zero W non si riavvia da solo: stacca l'alimentazione).
- Non verificabile senza Pi: l'effetto reale su un Pi; lo prova SPV con il bridge dell'utente (`keys monitor`, poi `keys crash-dabt`, poi `shot`).
- Consegna: DELIVERED standard; poi FREE.

## R-NET4 — leggere i log su SD dal bridge (bm-light, 45 min; decisione SPV 2026-10-15, via dell'utente)
- Obiettivo: dopo un crash o un riavvio, leggere `BOOTPREV.TXT`, `BOOTLOG.TXT`, `LASTRUN.TXT` dal PC senza togliere la SD. Oggi l'op `G` (get) serve solo `bm/shots/`.
- Branch `claude/net-logs` da `claude/integrazione-ottobre` (non da bm-core: serve `keys` e `sdlog`). Perimetro esteso: `src/net/netxfer.c`, `tools/bm_net.py`, `tools/pi_bridge.py`, `tools/pi.py` (help), `tests/net/check_bm_net_config.py`, `tests/qemu_test.py` (solo un caso), `docs/design/pi-test.md`, `docs/progress/system-net.md`.
- Modifica 1, kernel `netxfer.c` (stato GETFILE): oltre a `bm/shots/<NOME>` accettare SOLO i tre nomi esatti `bm/BOOTPREV.TXT`, `bm/BOOTLOG.TXT`, `bm/LASTRUN.TXT` (confronto esatto, senza `..`, senza altri file di `bm/`). Il contenuto esce già mascherato (FRZ-6 sul ramo di AGY: se non è ancora integrato, applicare `reports_mask_secrets` in lettura se esiste, altrimenti dichiararlo nella consegna). Stessa password e stessi limiti di dimensione dell'op `G`.
- Modifica 2, `tools/bm_net.py`: `--get` accetta anche i tre percorsi; nessun altro percorso fuori da `bm/shots/`.
- Modifica 3, `tools/pi_bridge.py` e `tools/pi.py`: nuova op `logs NOME` con NOME in {`bootprev`, `bootlog`, `lastrun`} (tabella fissa); salva in `~/bmk*/` o nel repo `out/` e stampa il testo (passa dal `replace(pw, "***")`). Qualsiasi altro nome: `refused`.
- Modifica 4, test: `check_bm_net_config.py` (nomi ammessi e rifiutati, `logs foo` refused) e un caso in `qemu_test.py`/`test_netcon` che ottiene `bm/BOOTPREV.TXT` e rifiuta `bm/CONFIG.TXT` e `bm/../x`. Aggiornare `pi-test.md` (tabella op del bridge).
- Verifica: `make` e `make TARGET=rgb30` exit 0; `make test-net` exit 0; `python3 tests/net/check_bm_net_config.py` OK; `python scripts/docs_audit.py score` senza link rotti.
- Non verificabile senza Pi: la lettura reale dalla SD; la prova la fa SPV con l'utente (`logs bootprev` dopo un crash).
- Consegna: DELIVERED standard; poi FREE. R-NET3 resta non iniziato.

## Non verificabile senza Pi
QEMU non ha rete: T/G si coprono solo con `test_netcon` (host). Scoperti: PNG end-to-end, invio a pezzi su lwIP reale con file ≥100 KB, tempi dell'attesa 3 s in un gioco 3D, `pi_test.py --cart X --shot out.png` via `pi_bridge`, verdetto `err` dopo un crash reale. Dichiararlo nella consegna; la prova sul Pi la fa l'utente.
