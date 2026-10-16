# R-NET5: bridge durevole per i test sul Pi Zero W (operatore NET)
Assegnato a NET (16 ott). Decisioni adottate: D1, D2, D3 come raccomandate; D4 diventa il WO R-NET6 (dopo questo, stesso operatore). Sequenza di NET: R-NET5, API-1, APP-5b, R-NET6.
Inizia dopo: DELIVERED R-NET4. Il WO resta valido anche se R-NET4 è già integrato: i file stanno già su `claude/integrazione-ottobre`, cioè il worktree `C:\Users\Utente\Documents\GitHub\bm33-int` con HEAD = `claude/integrazione-ottobre`, che contiene `keys` e `logs`.
Base: `claude/integrazione-ottobre` aggiornato. Branch di lavoro: `claude/net-bridge-durable`. Mai push su bm-core.
Perimetro: `tools/pi_bridge.py`, `tools/bm_net.py` (solo `main()`, il blocco `__main__` e la nuova opzione `--in-monitor`), `tools/pi.py` (solo la docstring/help), `tests/net/check_bm_net_config.py` (solo la sezione del bridge e i nuovi casi), `docs/design/pi-test.md`, `docs/progress/tools.md` (solo la riga "Pi tests from the PC" di Open work).
NON toccare: `src/**` (nessuna modifica al kernel), `tools/pi_test.py`, `tests/qemu_test.py`, `tests/net/test_netcon.c`, `SYNC.md`, `sync/*`, `.github/workflows/*`.
Vincoli: la password non va mai su disco, in un keyring o in una variabile d'ambiente persistente. Nessun tasto libero e nessun comando arbitrario. Il WO non ordina prove sul Pi: quelle sono elencate in fondo e le fa l'utente, ogni crash/freeze/reboot/kernel solo dopo il suo "ok" esplicito.

Fatti verificati dal planner:
- Il bridge apre già una connessione nuova per ogni richiesta (`subprocess` di `bm_net.py`), quindi un riavvio del Pi non lo uccide. Muore invece con la shell, e se lo si avvia una seconda volta cancella alla cieca il socket del primo (`pi_bridge.py:67-68`).
- In `bm_net.py:444` `create_connection` non è protetto: con il Pi spento esce un traceback.
- In `pi_bridge.py:53` un `TimeoutExpired` diventa un generico "error: ...".
- Nel menu `X` apre le opzioni e `r` è un'azione (`carts.c:1442,1446`). Freeze (`X f`, `monitor.c:454`) e reboot (`r`, `monitor.c:442`) valgono solo nel monitor, e lo stesso vale per i `crash-*` di oggi.
- Al login la console dice dove vanno i tasti ("the keys go to the monitor ..." in `monitor.c:321`).
- Il saluto senza password dà la versione (`bm_net.console_version`, `bm_net.py:296`).
- `BOOTPREV.TXT` contiene la riga `--- the run before <stato>; the last lines ...` (`sdlog.c:28`), dove lo stato è `crumbs_prev_state()` (`crumbs.c:159-177`).

## Decisioni per SPV (i WO sotto applicano la raccomandazione: se SPV decide diversamente, riscrivere a/b)
- **D1 (tasti solo nel monitor)**: aggiungere `bm_net.py --in-monitor`, che con `--keys` esce con 2 e non manda niente se il focus non inizia con "the monitor". Il bridge lo usa per `crash-*`, `freeze` e `reboot`, non per `monitor`. **Raccomandato**: senza questo controllo, `keys freeze` lanciato dal menu apre le opzioni e poi esegue `f`. Alternativa per il reboot: esiste già `line reboot`, che funziona da menu e da monitor.
- **D2 (sopravvivere alla shell)**: **raccomandato** `pi_bridge.py --detach` (fork + setsid, SIGHUP ignorato, log in `out/pi_bridge.log`, password solo in RAM). Escluse: un'unità systemd user (all'avvio servirebbe la password salvata), un keyring o una variabile d'ambiente persistente (vietati dall'utente), tmux (dipendenza esterna, ma resta citato come alternativa manuale nei docs). Limite inevitabile: a ogni `wsl --shutdown`, riavvio o sospensione di Windows il bridge muore e l'utente riscrive la password. Da verificare (non sicuro): WSL potrebbe fermare la VM quando si chiude l'ultima finestra anche con un processo staccato (`instanceIdleTimeout`/`vmIdleTimeout` in `%UserProfile%\.wslconfig`). Lo verifica il passo 7 dell'utente.
- **D3 (contenuto di `status`)**: raggiungibilità e versione dal saluto (senza login), poi lo stato del run precedente da `BOOTPREV.TXT`, poi l'ora del controllo. Non c'è l'uptime del boot corrente, perché richiederebbe una nuova riga monitor `uptime` nel kernel (con parità RGB30 e `consistency.py`): eventuale WO separato. **Raccomandato**: accettare.
- **D4 (falla trovata, fuori perimetro)**: il filtro di `line` (`pi_bridge.py:39`) accetta qualunque testo alfanumerico con " _-.=". Quindi `pi.py line set wifi_psk=x` (o `save`, `play`) passa, contro quello che dice `pi-test.md:18` ("--config, secrets, WiFi refused"). **Raccomandato**: un WO R-NET6 a parte con una lista ammessa per la prima parola di ogni comando separato da `;` (proposta: `gpu,b3d,render,room,log,err,send,reboot`). Non va in R-NET5.

## R-NET5a: tasti `freeze` e `reboot` e operazione `status` (bm-light, 40 min)
- `tools/pi_bridge.py`:
  - `KEYS` diventa `{"monitor": "q", "crash-dabt": "Xa", "crash-undef": "Xu", "crash-prefetch": "Xb", "freeze": "Xf", "reboot": "r"}`. Nuovo `MONITOR_ONLY = {"crash-dabt", "crash-undef", "crash-prefetch", "freeze", "reboot"}`.
  - In `run()`, il ramo `keys` costruisce `base + ["--in-monitor", "--keys", SEQ]` se il nome è in `MONITOR_ONLY`, altrimenti `base + ["--keys", SEQ]` (l'ordine conserva i test `[-2:]` esistenti).
  - Nuova op `status`, solo con richiesta esattamente uguale a `"status"`; `status x` dà `refused`. Il modulo importa `bm_net` (`sys.path.insert(0, HERE)`), poi:
    - `v = bm_net.console_version(host)`. Se è None, risponde `status: down - no answer from <host>:3333 (off, restarting, frozen, or another client connected)` senza lanciare subprocess.
    - Altrimenti lancia `base + ["--get", "bm/BOOTPREV.TXT", ROOT/out/pi_status_BOOTPREV.TXT]` con lo stesso env e timeout di oggi, maschera la password nel file come fa `logs`, e prende l'ultima occorrenza di `--- the run before (.*?); the last lines`.
    - Risposta su 3 righe: `status: up - bm <v> at <host>`, `previous run: <stato>` (oppure `previous run: unknown` seguito dalla prima riga dell'output di bm_net), `checked: <datetime.now().isoformat(timespec="seconds")>`. Tutto passa dal `replace(pw, "***")`.
- `tools/bm_net.py`: `--in-monitor` (store_true, help "with --keys: only when the monitor has the keys").
  - In `main()`, prima di `run_keys`: se `args.in_monitor` e `FOCUS.search(answer)` è assente o il suo gruppo non inizia con `b"the monitor"`, stampa `[bm_net] the keys go to <where o "an unknown place">: these keys work in the monitor only (keys monitor first)`, chiude il socket e ritorna 2.
  - `--in-monitor` senza `--keys` viene ignorato.
- `tools/pi.py`: nella docstring aggiungere `status`, oltre ai nuovi nomi in `keys NAME (monitor, crash-*, freeze, reboot)`.
- Test in `tests/net/check_bm_net_config.py`:
  - `keys freeze` → `cmd[-3:] == ["--in-monitor","--keys","Xf"]`.
  - `keys reboot` → `["--in-monitor","--keys","r"]`.
  - `keys crash-dabt` contiene `--in-monitor`; `keys monitor` non lo contiene.
  - `--in-monitor --keys Xf` contro `NetConsole("the menu (test)")` → rc 2 e `keys == b""`. Contro `NetConsole("the monitor ('h' for help, M the menu)")` → rc 0 e `keys == b"Xf"`.
  - `status` con `pi_bridge.bm_net.console_version` sostituito: None → l'output inizia con `status: down` e nessun comando lanciato; `"v9"` con un `subprocess.run` finto che scrive `x\n--- the run before ended in a crash while: t, 12 s after boot, kernel k; the last lines it printed (RAM) ---\nnet_password=s3cret` → contiene `status: up - bm v9` e `previous run: ended in a crash while: t, 12 s after boot, kernel k`, e non contiene `s3cret`.
  - `status x` → refused. "keys crash-freeze" resta nella lista dei rifiutati.
- Verifica (in WSL): `python3 tests/net/check_bm_net_config.py` → "all passed"; `make test-net` → exit 0.

## R-NET5b: bridge durevole: istanza unica, `--detach`, messaggi chiari (bm-light, 40 min)
- `tools/pi_bridge.py`:
  - Nuova funzione `claim_socket(path)`: se `path` esiste, prova `connect` AF_UNIX con timeout 1 s. Se riesce, chiude e fa `sys.exit("a bridge is already running on <path> (stop it: pkill -INT -f pi_bridge.py)")`. Se dà `ConnectionRefusedError`/`FileNotFoundError`, fa `unlink`. Poi `bind`, `chmod 0o600`, `listen(1)`, e ritorna il socket.
  - `main()`: argomenti `--detach` se presente in `sys.argv[1:]`; host = primo argomento che non inizia con `--`, default `192.168.1.108`. Ordine: `claim_socket(SOCK)`, poi la password (getpass o `BM_PASSWORD` del processo, come oggi).
  - Se `--detach`, `os.fork()`: il padre stampa `bridge started in the background (pid N), log out/pi_bridge.log; stop: pkill -INT -f pi_bridge.py` e fa `os._exit(0)`. Il figlio fa `os.setsid()` e `signal.signal(SIGHUP, SIG_IGN)`, porta stdin su `/dev/null` e stdout/stderr su `ROOT/out/pi_bridge.log` (`os.open` in append con mode 0o600, `os.dup2`).
  - In entrambi i modi, un handler di SIGTERM solleva `KeyboardInterrupt`, così il `finally` rimuove il socket. Il loop resta quello di oggi.
  - In `run()`: `subprocess.TimeoutExpired` → `timeout: no end after 300 s (the Pi may be frozen or restarting: python3 tools/pi.py status)`.
- `tools/bm_net.py`: nel blocco `if __name__ == "__main__":`, `sys.exit(main())` va avvolto in `try/except OSError as e`, che stampa `[bm_net] the console does not answer (<e>): off, restarting or frozen; try again in a minute` ed esce con 3. Gli `OSError` già gestiti dentro (file locali, righe 125 e 208) restano come sono.
- Test in `tests/net/check_bm_net_config.py`:
  - `claim_socket` su un percorso in `tempfile.mkdtemp()`: la prima chiamata riesce. La seconda, con la prima ancora aperta, solleva `SystemExit`. Chiuso il primo socket (il file resta), la terza riesce perché il file stantio viene rimosso.
  - `subprocess.run` finto che solleva `subprocess.TimeoutExpired(cmd, 300)` → l'output inizia con `timeout:`.
  - `bm_net.py 127.0.0.1 -P <porta libera> -p x --line err` (porta presa con bind e close di un socket) → rc 3 e "does not answer" nell'output.
  - `--detach` non si testa in automatico (fork): è nella prova dell'utente.
- Verifica (in WSL): `python3 tests/net/check_bm_net_config.py` → "all passed"; `make test-net` → exit 0.

## R-NET5c: documenti (bm-mechanic, 15 min)
- `docs/design/pi-test.md`:
  - Riga `keys NAME` della tabella: aggiungere `freeze` = `Xf` (il watchdog riavvia il Pi in circa 3 s) e `reboot` = `r`, con la nota "crash-*, freeze, reboot only when the monitor has the keys (`keys monitor` first), else exit 2, nothing pressed".
  - Nuova riga `status`.
  - Sotto la tabella, un paragrafo "How long the bridge lives": sopravvive ai riavvii del Pi (una connessione per richiesta, password in RAM); con `--detach` sopravvive alla chiusura della shell; non sopravvive a `wsl --shutdown`, al riavvio o alla sospensione di Windows, né allo stop della VM WSL (nota su `.wslconfig` da confermare con il passo 7); una seconda istanza viene rifiutata; errori: "does not answer" con exit 3, "timeout: ..." dopo 300 s.
  - In "Rules of the tools", una riga: "The bridge never stores the password: no file, no keyring, no persistent environment variable; after a WSL or PC restart the owner types it again."
- `docs/progress/tools.md`: nella riga "Pi tests from the PC" aggiungere che R-NET5 (bridge: `status`, `freeze`/`reboot`, `--detach`) è fatto e che resta la prova sul Pi.
- Verifica: `python scripts/docs_audit.py score` senza link rotti né orfani.

## Readiness Gate
| Domanda | 5a | 5b | 5c |
|---|---|---|---|
| File/funzioni nominati | SÌ | SÌ | SÌ |
| Diff deterministico | SÌ se SPV conferma D1/D3 | SÌ se SPV conferma D2 | SÌ |
| Verifica deterministica | SÌ | SÌ (tranne `--detach`: prova dell'utente) | SÌ |
| ≤3 file circa | 4 (3 tool + 1 test) | 3 | 2 |
| Nessun blocco | dipende da D1, D3 | dipende da D2 | dopo 5a e 5b |
Finché D1-D3 non sono confermati, 5a e 5b non sono pronti per un agente basso. D4 non blocca.

Criterio di fine: le verifiche di 5a, 5b e 5c sono OK. Consegna: una riga DELIVERED standard nella sezione `## Consegne` (anche una sola per i tre passi, con l'elenco), poi FREE. Nella consegna va dichiarato "non verificato sul Pi; `--detach` e il comportamento di WSL da provare dall'utente".

## Prova sul Pi vero (la fa l'utente: ogni passo con freeze/reboot solo dopo il suo "ok")
1. In WSL: `python3 tools/pi_bridge.py --detach 192.168.1.108`, poi scrivi la password e chiudi la finestra.
2. In una nuova shell WSL: `python3 tools/pi.py status` → `status: up - bm <versione>` e `previous run: ...`.
3. Con il Pi sul menu: `python3 tools/pi.py keys freeze` → "these keys work in the monitor only", e il Pi non cambia.
4. ("ok" dell'utente) `keys monitor`, poi `keys freeze` → il Pi si blocca e si riavvia da solo in circa 3 s. Dopo circa 60 s, `status` → `previous run: ended without a clean exit ... while: freeze test (X f) ...`.
5. ("ok" dell'utente) `keys monitor`, poi `keys reboot` → riavvio; poi `status` → up.
6. Stacca l'alimentazione del Pi: `python3 tools/pi.py line err` → "does not answer" (exit 3) entro pochi secondi, e il bridge resta vivo. Riattacca: dopo circa 60 s, `status` → up, senza riscrivere la password.
7. Chiudi tutte le finestre WSL, aspetta 2 minuti, apri una nuova shell: `python3 tools/pi.py status`. Se risponde "the bridge is not running", WSL ha fermato la VM: riferiscilo a SPV (D2).
8. Avvia una seconda volta `python3 tools/pi_bridge.py --detach 192.168.1.108` → "a bridge is already running". Chiudi con `pkill -INT -f pi_bridge.py`: il socket `~/.pi_bridge.sock` sparisce.
