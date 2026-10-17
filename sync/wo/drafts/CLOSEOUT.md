# CLOSEOUT: procedura di chiusura sessione del sync (bozza, non assegnata)

Stato: proposta di SPV per l'utente. Dipende dalla RULE SIGNOFF in attesa (DONE verificato da sync_check; un handshake nuovo sostituisce il SIGNOFF), che qui viene inclusa.

## 1. Nome
- **CLOSEOUT** (scelto): copre i due livelli (operatori e SPV) e anche il lavoro di documentazione; si accorda con l'esito dello script `CLOSEOUT-READY`.
- WIND-DOWN dice "rallentare", non "verificare e chiudere". STAND-DOWN riguarda solo gli operatori. SEAL fa pensare a qualcosa di immutabile e si confonde con la firma (sign-off).

## 2. Regola proposta (testo per la RULE e per SYNC.md §3b "Chiusura")
CLOSEOUT si apre solo su richiesta dell'utente. SPV lo annuncia con una riga di Log `CLOSEOUT` e lo porta avanti in fasi. Si passa alla fase successiva solo quando l'uscita della fase precedente e' verificata da script.

Azione nuova di Log: `CLOSEOUT`. Il messaggio inizia con il nome della fase: `DRAIN`, `SIGNOFF`, `READY`, `DOCS`, `HANDOFF`, `CLEAN`, `END`.

### 2a. Fasi SPV
| Fase | SPV fa | Uscita (verificata da script) |
|---|---|---|
| C0 DRAIN | Riga `CLOSEOUT DRAIN`, piu' una riga nella casella di ogni operatore attivo. Da qui niente nuove `ASSIGNED` e niente nuove review. Lascia finire i WO e le review gia' avviati. | `sync_check.py --closeout`: nessun operatore `WORKING` su un WO assegnato prima di DRAIN, tranne quelli dichiarati `DEFER` (2d); `pending()` vuoto per tutti o dichiarato `DEFER`. |
| C1 SIGNOFF | Una riga `SIGNOFF <ID>: closeout <data>` per ogni operatore attivo, piu' la stessa riga nella casella `sync/inbox/<ID>.md` (cosi' `sync_watch` sveglia anche chi ha `wakeup no`). | Ogni operatore attivo e' `SIGNED-OFF` oppure `FORCED` (2b). |
| C2 READY | Lancia `python tools/sync_check.py --closeout --strict`. | Exit 0 e ultima riga `CLOSEOUT-READY`. Poi riga `CLOSEOUT READY`. |
| C3 DOCS | Aggiorna `docs/progress/<area>.md` (Open work, Rules), `docs/ROADMAP.md`, `docs/agents/ledger.txt` (una riga per ogni subagent chiuso). Traduce in inglese SYNC.md e sync/* solo se la traduzione e' in NEXT.md come lavoro aperto. | Su bm-core: `python scripts/docs_audit.py score` senza link rotti e `python scripts/consistency.py check` con exit 0. |
| C4 HANDOFF | Scrive in `sync/NEXT.md` una sezione `## N. CLOSEOUT <data>` che sostituisce le precedenti, con il prompt di ripartenza a 7 blocchi di `scripts/ctx_guard.py` (commit 3ed826af): RUOLO, REGOLE, VINCOLI, STRUMENTI, STATO, SOSPESI, LEZIONI. | `sync_check.py --closeout` trova in NEXT.md la sezione con la data di oggi e i 7 titoli di blocco. |
| C5 CLEAN | Committa e pusha il worktree di sync. Svuota le caselle lette. Pulizia di branch e worktree (2e). | `git status --porcelain` vuoto nel worktree di sync; `git worktree list` senza percorsi di operatori SIGNED-OFF o di revisione; lista 2e consegnata. |
| C6 END | Consegna all'utente l'elenco "da non perdere" (2c). Scrive la riga `CLOSEOUT END: sessione conclusa`. Si segna `DONE`. | `sync_check.py --closeout` stampa `CLOSEOUT-END` (riga END presente e SPV DONE). |

### 2b. Livello operatore e stati di chiusura
Quando l'operatore legge `CLOSEOUT DRAIN` o `SIGNOFF <ID>` (al battito o con sync_watch):
1. Lascia terminare i propri subagent. Non prende nuovi WO e nuove review.
2. Consegna (`DELIVERED ...`) e pusha. Il working tree deve essere pulito.
3. Rimuove i propri worktree puliti (anche quelli temporanei di revisione) e le proprie cartelle temporanee.
4. Scrive nel proprio file una sezione `## Chiusura` con la riga `CLOSED @ {branch}@{commit} · tree clean · aperti: {WO o nessuno} · {ISO}`.
5. Lancia `python tools/sync_hdr.py <ID> --ack auto --state DONE` (l'ack conferma il SIGNOFF), commit e push, poi ferma i battiti.

Stato di chiusura per operatore (lo calcola lo script; non e' un nuovo valore di `state`):
| Stato | Condizione |
|---|---|
| `ACTIVE` | nessuna riga `CLOSEOUT DRAIN` dopo il suo ultimo handshake |
| `DRAINING` | c'e' la riga DRAIN e l'operatore e' `WORKING`/`WAITING` |
| `SIGNOFF-SENT` | c'e' una riga `SIGNOFF <ID>` e il suo `ack` e' precedente a quella riga |
| `SIGNOFF-ACK` | `ack` uguale o successivo alla riga SIGNOFF, ma `state` diverso da `DONE` oppure manca la riga `CLOSED` |
| `SIGNED-OFF` | `ack` uguale o successivo a SIGNOFF, `state DONE` e riga `CLOSED` valida |
| `UNRESPONSIVE` | timeout (2d) |
| `FORCED` | riga `SIGNOFF <ID> FORCED: <motivo>` di SPV, scritta dopo il via dell'utente |

Un handshake nuovo (riga `ASSIGNED`/handshake successiva al SIGNOFF) annulla il SIGNOFF e riporta l'operatore in `ACTIVE`. Si usa `active = false` in operators.toml solo per un SIGNOFF definitivo (uscita dal progetto); per una fine sessione il blocco resta `active`.

### 2c. Cosa non perdere (elenco finale all'utente, stampato da `--closeout`)
- Caselle `sync/inbox/<ID>.md` non vuote o non confermate con ack.
- WO con `ASSIGNED` nel Log ma senza `DELIVERED`.
- `DELIVERED` senza `REVIEW` (dal `pending()` di spv_brief).
- Rami elencati in operators.toml (`branches`) non mergiati in `origin/bm-core` (`git branch -r --no-merged origin/bm-core`).
- Worktree ancora presenti (`git worktree list`).
- Righe `## Chiusura` con `aperti:` diverso da `nessuno`.
Ogni voce va in NEXT.md (C4) oppure riceve una decisione esplicita dell'utente. Nessuna voce si chiude in silenzio.

### 2d. Timeout e DEFER
- Operatore in `SIGNOFF-SENT` o `SIGNOFF-ACK` oltre `eta + 10 min` se `WORKING`, altrimenti 30 min dopo la riga SIGNOFF: diventa `UNRESPONSIVE`. SPV lo dice all'utente con un prompt pronto: `da SPV a <ID>: SIGNOFF closeout, consegna, scrivi ## Chiusura, sync_hdr --ack auto --state DONE`.
- Ancora `UNRESPONSIVE` 30 min dopo l'avviso: SPV chiede il via all'utente e scrive `SIGNOFF <ID> FORCED`. Il worktree dell'operatore non si tocca; i suoi rami e WO aperti finiscono nell'elenco 2c.
- `DEFER <WO|review>: motivo` (azione `CLOSEOUT`, fase DRAIN): un WO o una review lunga che passa alla sessione successiva. Lo script lo considera chiuso per DRAIN e lo elenca in 2c.

### 2e. Pulizia di branch e worktree (fase C5)
SPV produce l'elenco di ogni branch (locale e remoto) e worktree rimasto, con stato: `mergiato`, `non mergiato`, `abbandonato` (nessun commit non presente altrove, nessun WO aperto).
- Da solo: rimuove i worktree puliti (suoi, di revisione, degli operatori SIGNED-OFF) e cancella i branch locali gia' mergiati in `origin/bm-core`.
- Con il via dell'utente: cancellazione di branch remoti e di branch non mergiati (`agy/*`, `claude/*` non mergiati). L'utente riceve l'elenco e SPV cancella solo cio' che approva.
- Mai rimosso: worktree con modifiche non committate, branch con commit non raggiungibili da altri rami senza via esplicito. Vengono segnalati in 2c.
- Verifica da script: `closeout_phase_checks` stampa l'elenco 2e e un avviso `! C5` per ogni worktree sporco.

## 3. Dove stanno i file
- Template (ramo `sync-handshake`): `SYNC.md` (§1 Stati, §3b Chiusura, §5 azioni Log), `sync/_template.md` (sezione `## Chiusura`), `tools/sync_check.py`, `tools/sync_hdr.py`, `docs/progress/sync.md`. Che spv_brief e sync_log siano su questo ramo e' un'ipotesi da verificare: CO-8.
- Live (bm-core): `docs/progress/sync.md` (regola Sign-off, riga 71), `CLAUDE.md` §6 (paragrafo Sign-off), `tools/sync_check.py`, `tools/sync_hdr.py` (gia' presenti su bm-core; spv_brief.py e sync_log.py stanno solo sul ramo di sync).
- Ramo di sync del progetto (`sync/protocollo-ottobre`, solo SPV): copia live di `SYNC.md`, `sync/_template.md`, `sync/NEXT.md`, `tools/spv_brief.py`.

## 4. Work Order

### CO-1 sync_check --closeout (bm-coder)
- Obiettivo: lo script calcola lo stato di chiusura di ogni operatore e stampa CLOSEOUT-READY.
- File: `tools/sync_check.py`: `main()`; nuove `closeout_state(oid, h, text, log, now) -> str` e `closeout_rows(log)`.
- Modifica: stati della tabella 2b e timeout di 2d. Righe del Log: azione (`line.split("|")[4]`, senza backtick) `SIGNOFF` o `CLOSEOUT`. FORCED: messaggio che inizia con `SIGNOFF <ID> FORCED`. Riga CLOSED: regex `^[-*]?\s*CLOSED\s+@\s+\S+@\S+\s+·\s+tree clean\s+·\s+aperti:\s+.+·\s+\d{4}-\d\d-\d\dT\d\d:\d\d` nella sezione `## Chiusura`. Con `--closeout`: colonna `closeout`; ultima riga `CLOSEOUT-READY` se tutti gli attivi diversi da SPV sono SIGNED-OFF o FORCED, `CLOSEOUT-END` se esiste anche una riga `CLOSEOUT` che inizia per `END` e SPV e' DONE, altrimenti `CLOSEOUT-NOT-READY: <ID>=<stato>,...`. `--strict --closeout` esce con 1 se NOT-READY.
- Verifica: `python -m unittest tests.tools.test_sync_check_closeout` → returncode 0.

### CO-2 test closeout (bm-coder)
- File: nuovo `tests/tools/test_sync_check_closeout.py`, schema di `tests/tools/test_spv_brief.py` (tempdir con SYNC.md, sync/operators.toml, sync/<ID>.md; `sc.ROOT` sul tempdir).
- Modifica: un test per ciascuno stato di 2b, per READY, END, NOT-READY con exit 1 sotto `--strict`, e per un handshake successivo al SIGNOFF che riporta ACTIVE.
- Verifica: `python -m unittest tests.tools.test_sync_check_closeout -v` → tutti OK.

### CO-3 elenco "da non perdere" (bm-coder)
- File: `tools/sync_check.py`: nuova `leftovers(log, ops) -> list[str]`; importa `pending` da `tools/spv_brief.py` solo se il file esiste (su bm-core manca).
- Modifica: le voci di 2c e l'elenco 2e, una per riga con prefisso `  > `; comandi git (`git branch -r --no-merged origin/bm-core`, `git worktree list --porcelain`) via subprocess; se git fallisce stampa `git: n/d`.
- Verifica: `python -m unittest tests.tools.test_sync_check_closeout -k leftovers` → OK (git mockato).

### CO-4 controlli DOCS/HANDOFF/CLEAN (bm-coder)
- File: `tools/sync_check.py`: nuova `closeout_phase_checks(now) -> list[str]`, chiamata da `--closeout` dopo READY.
- Modifica: NEXT.md ha il titolo `## <n>. CLOSEOUT <YYYY-MM-DD di oggi>` e sotto i 7 titoli `1 RUOLO` ... `7 LEZIONI`; `git status --porcelain` vuoto; `sync/inbox/*.md` (esclusi README e .gitkeep) senza righe non vuote; worktree sporchi (2e). Ogni controllo fallito stampa `! C4: ...` o `! C5: ...` ed e' contato tra i problemi.
- Verifica: `python -m unittest tests.tools.test_sync_check_closeout -k phase` → OK.

### CO-5 sync_hdr --closed (bm-coder)
- File: `tools/sync_hdr.py` (argparse righe 163-183): opzione `--closed OPEN` (`nessuno` o ID dei WO aperti).
- Modifica: richiede `--branch`, legge HEAD con `--commit` o `git rev-parse --short HEAD`, aggiunge sotto `## Chiusura` (creandola prima di `## Progressi` se manca) la riga CLOSED nel formato di CO-1, poi applica `--state DONE --ack auto`.
- Verifica: `python -m unittest tests.tools.test_sync_hdr_close -v` → OK, con nuovo caso `test_closed_line`.

### CO-6 template operatore (bm-mechanic)
- File: `sync/_template.md`: tra `## Consegne` e `## Progressi` la sezione `## Chiusura` con placeholder `(python tools/sync_hdr.py {ID} --closed nessuno --branch {branch})`.
- Verifica: `python tools/sync_check.py` → nessun nuovo problema su file creati dal template.

### CO-7 testo della regola (solo dopo il via dell'utente; bm-light + SPV)
- File: `SYNC.md` §3b "Chiusura" (righe 129-134, da sostituire), §5 riga 191 (aggiungere `CLOSEOUT`); `docs/progress/sync.md` riga 71 e Rules; `CLAUDE.md` §6 paragrafo "Sign-off".
- Modifica: testo di §2 accorciato: fasi e tabella 2b in SYNC.md; negli altri due un solo rimando "chiusura sessione = CLOSEOUT, SYNC.md §3b". Poi riga di Log `RULE CLOSEOUT` con ack richiesto a tutti (la scrive SPV).
- Verifica: `python scripts/docs_audit.py score` senza link rotti; `python tools/sync_check.py` → 0 righe di Log oltre 600 caratteri.

### CO-8 propagazione template e live (bm-ops, sotto SPV)
- File: ramo `sync-handshake`: SYNC.md, sync/_template.md, tools/sync_check.py, tools/sync_hdr.py, tests/tools/*, docs/progress/sync.md. bm-core: tools/sync_check.py, tools/sync_hdr.py, tests/tools/test_sync_check_closeout.py, docs/progress/sync.md, CLAUDE.md.
- Modifica: prima `git ls-tree -r --name-only origin/sync-handshake tools/ sync/` per sapere se spv_brief.py esiste sul template; poi un cherry-pick per ramo con commit separati. Merge su bm-core solo con il via dell'utente.
- Verifica: su ciascun ramo `python -m unittest discover -s tests/tools -p "test_sync*"` → OK.

Ordine: dopo il via sulle decisioni del §5, CO-1, CO-2, CO-5; poi CO-3, CO-4, CO-6; poi CO-7 e CO-8. CO-1..CO-4 toccano tutti `tools/sync_check.py`: in sequenza, mai in parallelo.

## 5. Decisioni per l'utente
1. Nome CLOSEOUT: si'/no.
2. Timeout: 30 min + 30 min prima di FORCED (proposta).
3. Fine sessione: `active` resta true (proposta) oppure `active = false` a ogni chiusura.
4. Traduzione inglese dentro C3 oppure come WO separato fuori dal CLOSEOUT.
5. Ordine: la RULE SIGNOFF in attesa confluisce in CO-7 (proposta), oppure si pubblica prima da sola.
