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

## Non verificabile senza Pi
QEMU non ha rete: T/G si coprono solo con `test_netcon` (host). Scoperti: PNG end-to-end, invio a pezzi su lwIP reale con file ≥100 KB, tempi dell'attesa 3 s in un gioco 3D, `pi_test.py --cart X --shot out.png` via `pi_bridge`, verdetto `err` dopo un crash reale. Dichiararlo nella consegna; la prova sul Pi la fa l'utente.
