# AGY-36 - TEST-ASSISTLIB: nuovo test `tests/tools/test_assistlib.py` per `fnv1a`, `tokens`, `content_words`, `to_cp437`, `features`
Assegnato ad AGY (16 ott). `scripts/assistlib.py` (l'hash e i token che il kernel deve riprodurre in C) non ha test. Gli attesi sono stati calcolati dal planner leggendo il codice.
Base: `origin/claude/integrazione-ottobre` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/test-assistlib C:\Users\Utente\Documents\GitHub\bm33-agy36 origin/claude/integrazione-ottobre`); branch `claude/test-assistlib`. Mai push su bm-core; push solo di questo branch.
Perimetro: un file NUOVO: `tests/tools/test_assistlib.py` (unittest).
NON toccare: `scripts/assistlib.py` (importato così com'è); ogni altro file.
Disgiunzione: i file di questo WO non sono nei perimetri di AGY-10..25 (perimetri letti in `sync/wo/`), AGX-3..12, APP-7/8 e R-NET6..9; nome file non usato da altri WO.
Vincoli: solo host, stdlib; nessun WSL. `not verified on hardware`. I numeri di riga qui sotto sono stati riaperti dal planner con `git show origin/claude/integrazione-ottobre:<file>` ma possono essere spostati: PRIMA di ogni modifica ri-trova ogni punto con `git grep -n -F` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi esattamente come descritto: `BLOCKED`, non indovinare. Fine riga: `git ls-files --eol <file>` mostra `w/lf` o `w/crlf`: tieni quello del file; `git diff --stat` deve restare piccolo (vedi Criterio): se dice centinaia di righe hai cambiato i fine riga, `git checkout -- <file>` e rifai.
Obiettivo: `python -m unittest tests/tools/test_assistlib.py` OK.

## Stato reale (verificato su `origin/claude/integrazione-ottobre`)
`scripts/assistlib.py`: `to_cp437` (riga 60, alza `ValueError` se manca il glifo), `fnv1a(data, h=2166136261)` (71), `tokens(raw)` (77; `MAX_TOKEN_LEN = 24`, `MAX_TOKENS = 64`), `content_words` (109), `features(raw, nbuckets=4096)` (117; `MAX_FEATS = 255`, ritorna lista ordinata). Ritrova con `git grep -n -E "^def |^MAX_|^NBUCKETS" -- scripts/assistlib.py`. Import: `sys.path.insert(0, <ROOT>/scripts); import assistlib`.

## AGY-36a - il test (bm-coder, 10 min)
Una classe `unittest.TestCase`, un metodo per controllo:
- `fnv1a(b'') == 2166136261`; `fnv1a(b'a') == 0xE40C292C`
- `tokens('Ciao, Mondo! x_1') == ['ciao','mondo','x_1']`
- `tokens('città') == ['citta']`
- `tokens('a'*30) == ['a'*24]`
- `len(tokens(' '.join(['ab']*100))) == 64`
- `content_words('il gatto e la volpe') == ['gatto','volpe']`
- `to_cp437('\u2603')` alza `ValueError` (`assertRaises`)
- `f = features('gatto volpe')`: `f == sorted(f)`, `0 < len(f) <= 255`, tutti gli elementi in `range(4096)`.
Calcolati leggendo il codice: ESEGUILI; se un'aspettativa è sbagliata correggi l'ASPETTATIVA (mai lo script) e scrivilo nella consegna.

## Verifica
1. `python -m unittest tests/tools/test_assistlib.py -v`: tutti OK.
2. `git status --short`: solo il file nuovo; cancella i `__pycache__` che hai creato.

Criterio di fine: 1 file nuovo, test OK.
Commit: un solo commit sul branch `claude/test-assistlib`, messaggio `tests: unit tests for assistlib.py`. Push solo di questo branch (`git push -u origin claude/test-assistlib`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-36 --repo C:\Users\Utente\Documents\GitHub\bm33-agy36 --branch claude/test-assistlib --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGY-36 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-36 @ claude/test-assistlib@<sha> · verifica: python -m unittest tests/tools/test_assistlib.py -> OK (eventuali aspettative corrette: <elenco>) · 1 file nuovo · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-36 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (casi forniti).
Stima: 10 minuti di lavoro.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Aspettativa sbagliata = si corregge l'aspettativa, non lo script. Raccomandata: sì.

## Prove per l'utente
Nessuna.
