# R-NET6 - LINE-ALLOW: lista ammessa per l'operazione `line` del bridge (D4 di R-NET5)
Assegnato a NET (16 ott), ultimo della sequenza: R-NET5, API-1, APP-5b, R-NET6. Decisioni adottate: D1-D5 come raccomandate (sotto).
Inizia dopo: DELIVERED APP-5b e R-NET5 integrato in `claude/integrazione-ottobre` (stessi file `tools/pi_bridge.py`, `tests/net/check_bm_net_config.py`, `docs/design/pi-test.md`: perimetri non disgiunti, quindi R-NET5 va prima).
Base: `claude/integrazione-ottobre` aggiornato con R-NET5 (worktree `C:\Users\Utente\Documents\GitHub\bm33-int`). Branch: `claude/net-bridge-line-allowlist`. Mai push su bm-core.
Perimetro: `tools/pi_bridge.py` (docstring del modulo riga "Operations", nuove costanti `LINE_CMDS`, `LINE_CHARS`, `B3D_ARG`, nuova funzione `check_line(text)`, e in `run()` solo il ramo `op == "line"`: oggi righe 39-40, dopo R-NET5 cercarlo per contenuto), `tests/net/check_bm_net_config.py` (solo la sezione "the bridge" in `main()`, nuovi casi), `docs/design/pi-test.md` (solo la riga `line TEXT`, la riga "anything else" e una riga in "## Rules of the tools"), `docs/progress/tools.md` (una riga in `## Rules (do not break)`).
NON toccare: `src/**` (il kernel non cambia), `tools/bm_net.py`, `tools/pi.py`, `tools/pi_test.py`, `tests/net/test_netcon.c`, `tests/qemu_test.py`, `SYNC.md`, `sync/*`, `.github/workflows/*`, le altre operazioni di `run()` (`send`, `play`, `kernel`, `shot`, `keys`, `logs` e quelle di R-NET5).
Vincoli: nessuna prova sul Pi (la lista cambia solo cosa il bridge inoltra: bastano test host con mock). Ogni `file:riga` citato in consegna è una riga aperta che dice quello che si afferma; un punto non verificabile si scrive `non verificato`.
Obiettivo: `pi.py line ...` inoltra al Pi solo righe di comandi della lista ammessa, separati da `;`, con argomenti controllati. Tutto il resto riceve `refused: line: <motivo>` senza lanciare `bm_net.py`.

## Prove dal kernel (lette dal planner)
- Dispatcher unico: `src/kernel/monitor.c:206 line_command()`, chiamato da `line_commands()` (`monitor.c:297-313`: spezza su `;` a `:305`, si ferma al primo errore a `:308`); lo usano il monitor (`monitor.c:466`, `case ':'`) e il menu (`src/kernel/carts.c:1448-1450`). Comandi (`monitor.c:291`): `gpu, b3d, play, set, save, render, room, log, err, send, reboot`; confronto con `strcmp` (sensibile alle maiuscole, `:221` e seguenti).
- Ammessi: `gpu` (`:221-224`, test GPU, report), `b3d` (`:225-232`, legge solo `tests=` e `profiles=`, `argv_copy` `:227-228`), `render`/`room` (`:260-270`, benchmark, report), `log` (`:271-272`), `err` (`:273-283`, sola lettura), `send` (`:284-286`, `reports_send_pending` `reports.c:289`), `reboot` (`:287-289`, `crumbs_restart(0)`). I report vanno in `/bm/reports` (`reports.c:29`, `:225`) con i segreti mascherati (`github_token`, `wifi_psk`, `net_password`, `meshy_key`, password console: `reports.c:186-189`, `:221`); `b3d` scrive anche in `/bm/bench` (`b3dpi.c:28`, `:113`).
- Rifiutati: `set` (`:239-256`, `config_set` su qualunque chiave, anche `wifi_psk` e `net_password`), `save` (`:257-259`, scrive `bm/config.txt`), `play` (`:233-238`, avvia un gioco per titolo; il bridge ha già `play FILE`).
- Caratteri: bm_net chiude la riga con `\r` (`tools/bm_net.py:389`); un `\r` dentro il testo chiuderebbe la riga prima e il resto arriverebbe come tasti (dedotto, `input_read_line` non letto: `non verificato`): vietati tutti i caratteri di controllo. Nomi dei test di `b3d` in `src/bm/b3d.c:737-772`: solo `[a-z0-9_]`; i profili (`GPU`, `FS2`, `ARM`, `2D`, `VSF`) visti solo nella documentazione (`BENCH3D.md:206`): vedi passo 0.
- Il filtro di oggi rifiuta già `;` e `,`: la riga documentata `gpu; b3d tests=...; send` oggi non passa. `pi_test.py --line` passa dal bridge: le sue righe con `set`, `save` o `play` d'ora in poi saranno rifiutate (solo documentazione, il file è fuori perimetro).

## R-NET6a - filtro in `tools/pi_bridge.py` (bm-light, 30 min)
- Passo 0: con un grep in `src/bm/b3d.c` trovare la tabella dei nomi dei profili accettati da `profiles=` e controllare che usino solo `[A-Za-z0-9_]`. Se un nome contiene altri caratteri: `BLOCKED: profilo <nome>`.
- Costanti a livello di modulo, accanto a `KEYS` e `LOGS`, ciascuna con un commento di una riga che cita `monitor.c`:
  - `LINE_CMDS = ("gpu", "b3d", "render", "room", "log", "err", "send", "reboot")`
  - `LINE_CHARS = set(string.ascii_letters + string.digits + " _,=;")` (aggiungere `import string`)
  - `B3D_ARG = re.compile(r"(tests|profiles)=[A-Za-z0-9_,]+")` (aggiungere `import re`), usata con `fullmatch`.
- Nuova `check_line(text)` → `(riga_normalizzata, None)` oppure `(None, motivo)`; regole in ordine, la prima che fallisce vince:
  1. `text` vuoto o `len(text) >= 200` → `"empty or longer than 199 characters"`.
  2. Un carattere fuori da `LINE_CHARS` → `"character %r not allowed" % c` (primo carattere che fallisce; `repr` per rendere leggibili `\t`, `\r`, `\x1b`, `²`; non usare `str.isalnum`, accetta cifre Unicode).
  3. `for seg in text.split(";")`, `words = seg.split()`: se `words` è vuoto → `"empty command between ';'"`.
  4. `words[0] not in LINE_CMDS` → `"'%s' is not allowed (allowed: gpu, b3d, render, room, log, err, send, reboot)" % words[0]` (le maiuscole non si convertono: `GPU` è rifiutato).
  5. `words[0] != "b3d"` e `len(words) > 1` → `"'%s' takes no arguments" % words[0]`.
  6. `b3d`: ogni parola dopo la prima passa `B3D_ARG.fullmatch`, altrimenti → `"b3d: '%s' is not tests=... or profiles=..." % w` (i duplicati non si controllano: il kernel prende la prima occorrenza, `monitor.c:181-185`).
  7. Riga normalizzata: `"; ".join(" ".join(words) for ogni segmento)`.
- In `run()`, il ramo `line`: `line, why = check_line(arg)`; se `why` → `return "refused: line: " + why`; altrimenti `cmd = base + ["--line", line]`. Le operazioni seguenti restano nella stessa catena, senza altri cambiamenti.
- Docstring, riga "Operations": `line TEXT (monitor commands separated by ';', only gpu, b3d tests=.. profiles=.., render, room, log, err, send, reboot; set/save/play and anything else refused)`.
- Verifica (WSL, dopo 6b): `python3 tests/net/check_bm_net_config.py` → exit 0 e `all passed`.

## R-NET6b - test host con mock (bm-coder, 25 min)
- In `tests/net/check_bm_net_config.py`, sezione "the bridge" (oggi `:300-335`): nuovi casi dentro il `try` esistente, prima del `finally`, con lo stesso mock `subprocess.run` che registra in `seen`.
- Ammessi (richiesta → riga attesa, controllo `seen[-1][-2:] == ["--line", <atteso>]`): `line gpu` → `gpu`; `line err` → `err`; `line render; room; log; reboot` → identica; `line gpu; b3d tests=match,quad_tex profiles=GPU,FS2; send` → identica (**fallisce sul codice vecchio**, che rifiuta `;` e `,`); `line   gpu ;b3d   tests=quad_flat  profiles=FS2 ;  send ` → `gpu; b3d tests=quad_flat profiles=FS2; send`.
- Rifiutati (l'output inizia con `refused: line:` e alla fine `len(seen)` è invariato): `line set wifi_psk=x` (deve contenere `'set' is not allowed`; **fallisce sul codice vecchio**, che la inoltra); `line save`; `line play pong`; `line gpu; set wifi_psk=x`; `line gpu; save`; `line err; play pong`; `line GPU`; `line Gpu`; `"line gpu\tb3d"`; `"line gpu\rsave"`; `"line gpu\x1b"`; `"line err\u00b2"`; `line gpu;`; `line ; gpu`; `line gpu;;err`; `line gpu x`; `line b3d tests=a.b`; `line b3d foo=1`; `line b3d tests=`; `"line gpu " + "a" * 200`; `line`; `line gpu,err`.
- Prima di consegnare eseguire i nuovi casi sul `pi_bridge.py` di prima di 6a (`git stash` del solo 6a) e annotare quanti falliscono (almeno i due marcati).
- Verifica: `python3 tests/net/check_bm_net_config.py` → exit 0 e `bm_net --config, --line: all passed`.

## R-NET6c - documenti (bm-mechanic, 15 min)
- `docs/design/pi-test.md`, tabella delle richieste. Riga `line TEXT`: Does = "monitor commands separated by `;`: `gpu`, `b3d tests=.. profiles=..`, `render`, `room`, `log`, `err`, `send`, `reboot`"; Limits = "first word of each command from `LINE_CMDS` of `pi_bridge.py` (case-sensitive); only `b3d` takes arguments (`tests=`/`profiles=` with `A-Z a-z 0-9 _ ,`); characters `A-Z a-z 0-9 space _ , = ;`, under 200; `set`, `save`, `play` refused (`pi_test.py --line` too)". Riga "anything else": "anything else (`--config`, `set`/`save`/`play` lines, secrets, WiFi) | **refused** (`refused: line: <reason>` for lines)". In "## Rules of the tools" una riga: "A `line` is checked command by command against a fixed list (`LINE_CMDS`); a command that changes the configuration (`set`, `save`) or starts a game (`play`) never passes."
- `docs/progress/tools.md`, `## Rules (do not break)`: "`tools/pi_bridge.py` `line`: allowlist `LINE_CMDS` per `;`-command, no `set`/`save`/`play`; widening it needs SPV (R-NET6)."
- Verifica: `python scripts/docs_audit.py score` senza link rotti né file orfani.

Criterio di fine: verifiche di 6a/6b e 6c OK e i casi marcati falliscono sul codice vecchio.
Consegna: riga DELIVERED standard (una per i tre passi, con l'elenco e il numero di casi falliti sul codice vecchio), poi FREE. Nessuna prova sul Pi. Agente consigliato: bm-light (6a), bm-coder (6b), bm-mechanic (6c); lo stesso operatore può farle nella stessa sessione.

## Decisioni adottate (SPV, raccomandazioni del planner)
- D1 scritture su SD dei report: ammesse (`gpu`, `b3d`, `render`, `room`, `log` scrivono report con segreti mascherati); vietate solo `set` e `save`.
- D2 `play` per titolo: rifiutato (resta `play FILE` del bridge).
- D3 `reboot`: tenuto; l'ok dell'utente per ogni reboot resta una regola dell'operatore, non del bridge.
- D4 `-` e `.` non ammessi nei caratteri (nessun comando ammesso ne ha bisogno; il passo 0 verifica i profili).
- D5 maiuscole: rifiutate, nessuna conversione.

## Prova per l'utente
Nessuna richiesta dal WO. Facoltativa, con il bridge avviato dall'utente: `python3 tools/pi.py line "set wifi_psk=x"` → `refused: line: 'set' is not allowed ...` senza traffico verso il Pi; `python3 tools/pi.py line "gpu; send"` → passa come prima.
