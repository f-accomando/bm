# AGY-35 - TEST-SYLLABLES: nuovo test `tests/tools/test_syllables.py` per `syllables()` e `plain()`
Assegnato ad AGY (16 ott). `scripts/syllables.py` ha una funzione di sillabazione senza test. I risultati attesi sono stati calcolati A MANO dal planner.
Base: `origin/claude/integrazione-ottobre` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/test-syllables C:\Users\Utente\Documents\GitHub\bm33-agy35 origin/claude/integrazione-ottobre`); branch `claude/test-syllables`. Mai push su bm-core; push solo di questo branch.
Perimetro: un file NUOVO: `tests/tools/test_syllables.py` (unittest, 9 test).
NON toccare: `scripts/syllables.py` (importato così com'è, mai modificato; AGY-34 cambia solo stringhe di `main()`); ogni altro file.
Disgiunzione: i file di questo WO non sono nei perimetri di AGY-10..25 (perimetri letti in `sync/wo/`), AGX-3..12, APP-7/8 e R-NET6..9; nome file non usato da AGX-4/8/9/10/11/12 né da AGY-11/12/13/17/32.
Vincoli: solo host (`python` di Windows), stdlib; nessun WSL. `not verified on hardware`. I numeri di riga qui sotto sono stati riaperti dal planner con `git show origin/claude/integrazione-ottobre:<file>` ma possono essere spostati: PRIMA di ogni modifica ri-trova ogni punto con `git grep -n -F` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi esattamente come descritto: `BLOCKED`, non indovinare. Fine riga: `git ls-files --eol <file>` mostra `w/lf` o `w/crlf`: tieni quello del file; `git diff --stat` deve restare piccolo (vedi Criterio): se dice centinaia di righe hai cambiato i fine riga, `git checkout -- <file>` e rifai.
Obiettivo: `python -m unittest tests/tools/test_syllables.py` -> 9 test OK.

## Stato reale (verificato su `origin/claude/integrazione-ottobre`)
`scripts/syllables.py`: `def plain(s)` (riga 31), `def syllables(w)` (riga 35, ritorna una lista di tuple `(attacco, nucleo, coda)`), `main()` (89) parte solo sotto `if __name__ == '__main__'` (163): l'import non stampa nulla. Ritrova con `git grep -n -E "^def " -- scripts/syllables.py`.
Import nel test: `sys.path.insert(0, os.path.join(ROOT, "scripts")); import syllables`, con `ROOT` come in `tests/tools/test_agent_audit.py` riga 3.

## AGY-35a - il test (bm-coder, 10 min)
Una classe `unittest.TestCase`, nove metodi, `assertEqual`:
- `syllables('casa')` == `[('c','a',''),('s','a','')]`
- `syllables('pizza')` == `[('p','i','z'),('z','a','')]`
- `syllables('canto')` == `[('c','a','n'),('t','o','')]`
- `syllables('padre')` == `[('p','a',''),('dr','e','')]`
- `syllables('paese')` == `[('p','a',''),('','e',''),('s','e','')]`
- `syllables('piano')` == `[('p','ia',''),('n','o','')]`
- `syllables('per')` == `[('p','e','r')]`
- `syllables('')` == `[('','','')]`
- `plain('città') == 'citta'`
Calcolati a mano: ESEGUILI. Se uno è sbagliato correggi l'ASPETTATIVA (mai lo script) e scrivilo nella consegna (`aspettativa corretta: <parola>`) e in chat.

## Verifica
1. `python -m unittest tests/tools/test_syllables.py -v`: 9 test OK.
2. `git status --short`: solo il file nuovo; cancella i `__pycache__` che hai creato.

Criterio di fine: 1 file nuovo, 9 test OK.
Commit: un solo commit sul branch `claude/test-syllables`, messaggio `tests: unit tests for syllables.py`. Push solo di questo branch (`git push -u origin claude/test-syllables`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-35 --repo C:\Users\Utente\Documents\GitHub\bm33-agy35 --branch claude/test-syllables --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGY-35 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-35 @ claude/test-syllables@<sha> · verifica: python -m unittest tests/tools/test_syllables.py -> 9 OK (eventuali aspettative corrette: <elenco>) · 1 file nuovo · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-35 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (casi forniti).
Stima: 10 minuti di lavoro.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Aspettativa sbagliata = si corregge l'aspettativa, non lo script. Raccomandata: sì.

## Prove per l'utente
Nessuna.
