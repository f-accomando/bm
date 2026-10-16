# AGY-28 - IT-LINKS-DOCS: nove righe `Italian version: [X-IT.md]` (file inesistenti) diventano righe vuote
Assegnato ad AGY (16 ott). Nove documenti inglesi hanno alla riga 3 un link alla versione italiana `*-IT.md` che non esiste (link rotto). Si svuota la riga (non si cancella, così le righe seguenti non scorrono).
Base: `origin/claude/integrazione-ottobre` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/docs-it-links-docs C:\Users\Utente\Documents\GitHub\bm33-agy28 origin/claude/integrazione-ottobre`); branch `claude/docs-it-links-docs`. Mai push su bm-core; push solo di questo branch.
Perimetro: la sola riga 3 di ciascuno di: docs/BENCH3D.md, docs/DRIVERS.md, docs/HARDWARE.md, docs/RIFF.md, docs/RGB30.md, docs/RISORSE.md, docs/RISOLUZIONI.md, docs/giochi/mecha-fighter-concept.md, docs/giochi/mecha-fighter-design.md (9 file).
NON toccare: ogni altra riga di questi file (la riga 3 si svuota, NON si cancella); `docs/PADTYPE.md` (AGY-29); qualunque altro file.
Disgiunzione: i file di questo WO non sono nei perimetri di AGY-10..25 (perimetri letti in `sync/wo/`), AGX-3..12, APP-7/8 e R-NET6..9; nessuno di essi tocca questi 9 file.
Vincoli: solo documentazione, nessun WSL. `not verified on hardware`. I numeri di riga qui sotto sono stati riaperti dal planner con `git show origin/claude/integrazione-ottobre:<file>` ma possono essere spostati: PRIMA di ogni modifica ri-trova ogni punto con `git grep -n -F` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi esattamente come descritto: `BLOCKED`, non indovinare. Fine riga: `git ls-files --eol <file>` mostra `w/lf` o `w/crlf`: tieni quello del file; `git diff --stat` deve restare piccolo (vedi Criterio): se dice centinaia di righe hai cambiato i fine riga, `git checkout -- <file>` e rifai.
Obiettivo: nessun link a `*-IT.md` nei 9 file; `git diff --stat`: 9 file, +9 -9.

## Stato reale (verificato su `origin/claude/integrazione-ottobre` @ f5cd71b7)
- `git grep -n -F -e "-IT.md" -- <i 9 file>` dà 9 righe, tutte la riga 3, tutte nella forma `Italian version: [NOME-IT.md](NOME-IT.md).`

## AGY-28a - misura PRIMA (bm-mechanic, 1 min)
`python scripts/docs_audit.py score`: segna la prima riga e le righe `  !`.

## AGY-28b - la modifica (bm-mechanic, 4 min)
In ciascuno dei 9 file la riga 3 (`Italian version: [...-IT.md](...-IT.md).`) diventa una riga VUOTA (lunghezza 0). Ritrova la riga con `git grep -n -F "Italian version:" -- <file>` (deve dire `:3:`).

## Verifica
1. `git grep -c -F -e "-IT.md" -- docs/BENCH3D.md, docs/DRIVERS.md, docs/HARDWARE.md, docs/RIFF.md, docs/RGB30.md, docs/RISORSE.md, docs/RISOLUZIONI.md, docs/giochi/mecha-fighter-concept.md, docs/giochi/mecha-fighter-design.md`: nessuna uscita.
2. `python scripts/docs_audit.py score`: nessuna penalità nuova.
3. `git diff --stat`: 9 file, 9 inserimenti, 9 cancellazioni.

Criterio di fine: 9 file, +9 -9.
Commit: un solo commit sul branch `claude/docs-it-links-docs`, messaggio `docs: drop the links to the missing -IT.md versions`. Push solo di questo branch (`git push -u origin claude/docs-it-links-docs`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-28 --repo C:\Users\Utente\Documents\GitHub\bm33-agy28 --branch claude/docs-it-links-docs --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGY-28 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-28 @ claude/docs-it-links-docs@<sha> · verifica: git grep -c -IT.md sui 9 file -> nessuna uscita; diff --stat 9 file +9 -9 · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-28 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-mechanic (testo esatto).
Stima: 5 minuti di lavoro.

## Decisioni (proposte del planner, da adottare da SPV)
Nessuna.

## Prove per l'utente
Nessuna.
