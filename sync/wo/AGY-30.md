# AGY-30 - IT2EN-SYNCSTART: `docs/progress/sync.md`: la frase di avvio (titolo e riga 72) passa all'inglese
Assegnato ad AGY (16 ott). `docs/progress/` è in inglese; la sezione `Frase di avvio` è rimasta italiana. WO complementare ad AGY-25 (stesso file, altre righe).
Base: `origin/claude/docs-en-sync-rules` (branch di AGY-25; vedi "Stato reale e base"). `git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/docs-en-sync-start C:\Users\Utente\Documents\GitHub\bm33-agy30 origin/claude/docs-en-sync-rules`; branch `claude/docs-en-sync-start`. Mai push su bm-core; push solo di questo branch.
Perimetro: `docs/progress/sync.md`, SOLO la riga del titolo `## Frase di avvio (…)` (oggi 70) e la riga di citazione che la segue (oggi 72).
NON toccare: tutte le altre righe, in particolare 59-68 (AGY-25); qualunque altro file.
Disgiunzione: i file di questo WO non sono nei perimetri di AGY-10..25 (perimetri letti in `sync/wo/`), AGX-3..12, APP-7/8 e R-NET6..9; unica sovrapposizione di file: AGY-25 (`docs/progress/sync.md` righe 59-68), righe diverse; per questo la base è il suo branch.
Vincoli: solo documentazione, nessun WSL; i comandi e le parole-chiave restano LETTERALI. `not verified on hardware`. I numeri di riga qui sotto sono stati riaperti dal planner con `git show origin/claude/docs-en-sync-rules:<file>` ma possono essere spostati: PRIMA di ogni modifica ri-trova ogni punto con `git grep -n -F` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi esattamente come descritto: `BLOCKED`, non indovinare. Fine riga: `git ls-files --eol <file>` mostra `w/lf` o `w/crlf`: tieni quello del file; `git diff --stat` deve restare piccolo (vedi Criterio): se dice centinaia di righe hai cambiato i fine riga, `git checkout -- <file>` e rifai.
Obiettivo: titolo e riga 72 in inglese; comandi e parole-chiave letterali.

## Stato reale e base (verificato su `origin/claude/integrazione-ottobre` e su `origin/claude/docs-en-sync-rules` @ ac81296a)
- `docs/progress/sync.md` ESISTE su `origin/claude/integrazione-ottobre` (titolo a riga 70, citazione a riga 72, identiche a quelle del branch di AGY-25). Ma AGY-25 cambia le righe 59-68 dello stesso file: per non creare un conflitto la BASE di questo WO è il branch di AGY-25 `origin/claude/docs-en-sync-rules`. Se `git fetch origin` non lo trova: `BLOCKED`. Integrando, SPV unisce prima AGY-25.
- Ritrova: `git grep -n -F "Frase di avvio" -- docs/progress/sync.md` (una riga, oggi 70) e `git grep -n -F "Fai handshake" -- docs/progress/sync.md` (una riga, oggi 72).

## AGY-30a - misura PRIMA (bm-mechanic, 1 min)
`python scripts/docs_audit.py score`: prima riga e righe `  !`.

## AGY-30b - la traduzione (bm-light, 4 min)
- Titolo -> ESATTAMENTE `## Start phrase (to paste into an operator's session)`.
- Riga 72: traduci tutta la frase in inglese (`> You are `<ID>` in the sync of branch `<branch>`. ...`). Restano LETTERALI e identici: `Fai handshake `<branch>``, `CLAUDE.md §6`, `## Consegne`, `DELIVERED ...`, `git fetch origin`, `git diff <ultimo hash visto> -- SYNC.md sync/`, `sync/inbox/<ID>.md`, `python tools/sync_hdr.py <ID> --state ... --task ... --ack auto`. Significato invariato: 3-5 min finché non hai preso il primo work order, poi 15; se non ti svegli da solo, a ogni messaggio dell'utente; non iniziare altro lavoro che quello della tua casella (inbox); consegna con una riga `DELIVERED ...` nella sezione `## Consegne` del tuo file. Resta UNA riga che inizia con `> `.

## Verifica
1. `git grep -nE "Sei |battito|casella|Non iniziare" -- docs/progress/sync.md`: nessuna uscita.
2. `git grep -n -F "Fai handshake" -- docs/progress/sync.md` trova ancora la riga 72; `python scripts/docs_audit.py score`: nessuna penalità nuova.
3. `git diff --stat`: 1 file, +2 -2.

Criterio di fine: 1 file, +2 -2.
Commit: un solo commit sul branch `claude/docs-en-sync-start`, messaggio `docs: sync.md start phrase in English`. Push solo di questo branch (`git push -u origin claude/docs-en-sync-start`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-30 --repo C:\Users\Utente\Documents\GitHub\bm33-agy30 --branch claude/docs-en-sync-start --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGY-30 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-30 @ claude/docs-en-sync-start@<sha> · verifica: git grep parole italiane nella frase di avvio -> nessuna uscita; docs_audit.py score senza penalità nuove · 1 file, +2 -2 · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-30 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-light (traduzione).
Stima: 5 minuti di lavoro.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 Base = branch di AGY-25 per evitare il conflitto sullo stesso file. Raccomandata: sì.

## Prove per l'utente
Nessuna.
