# AGY-26 - IT-LINKS-API: cinque link a `API-IT.md` (file che non esiste) vengono tolti da quattro file
Assegnato ad AGY (16 ott). Il riferimento in italiano `docs/API-IT.md` non esiste; cinque righe lo linkano ancora (link rotti). Decisione di SPV: `API-IT.md` NON si ricrea, i link si tolgono.
Base: `origin/claude/integrazione-ottobre` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/docs-it-links-api C:\Users\Utente\Documents\GitHub\bm33-agy26 origin/claude/integrazione-ottobre`); branch `claude/docs-it-links-api`. Mai push su bm-core; push solo di questo branch.
Perimetro: `docs/API.md` (righe 3, 6, 7), `AI.md` (riga 18), `sdk/README.md` (riga 41), `docs/progress/lua.md` (riga 5). Totale 6 righe.
NON toccare: ogni altra riga di questi 4 file; `docs/GUIDA-GIOCHI.md` e `docs/GAME-GUIDE.md` (AGY-27 e BETA-7a); `docs/API-IT.md` non si crea.
Disgiunzione: i file di questo WO non sono nei perimetri di AGY-10..25 (perimetri letti in `sync/wo/`), AGX-3..12, APP-7/8 e R-NET6..9; nessuno di essi tocca `docs/API.md`, `AI.md`, `sdk/README.md` o `lua.md`.
Vincoli: solo documentazione, nessun WSL. `not verified on hardware`. I numeri di riga qui sotto sono stati riaperti dal planner con `git show origin/claude/integrazione-ottobre:<file>` ma possono essere spostati: PRIMA di ogni modifica ri-trova ogni punto con `git grep -n -F` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi esattamente come descritto: `BLOCKED`, non indovinare. Fine riga: `git ls-files --eol <file>` mostra `w/lf` o `w/crlf`: tieni quello del file; `git diff --stat` deve restare piccolo (vedi Criterio): se dice centinaia di righe hai cambiato i fine riga, `git checkout -- <file>` e rifai.
Obiettivo: `git grep -c "API-IT" -- docs/API.md AI.md sdk/README.md docs/progress/lua.md` non stampa nulla.

## Stato reale (verificato dal planner su `origin/claude/integrazione-ottobre` @ f5cd71b7) e come ritrovare ogni riga
- `git grep -n -F "API-IT" -- docs/API.md AI.md sdk/README.md docs/progress/lua.md` deve dare esattamente 5 righe: `docs/API.md:3`, `docs/API.md:6`, `AI.md:18`, `docs/progress/lua.md:5`, `sdk/README.md:41`.
- `docs/API.md:7` è `> [GUIDA-GIOCHI.md](GUIDA-GIOCHI.md).` (non contiene `API-IT`, resta com'è).

## AGY-26a - misura PRIMA (bm-mechanic, 1 min)
`python scripts/docs_audit.py score`: segna la prima riga e le righe `  !`.

## AGY-26b - le modifiche (bm-mechanic, 4 min)
Sostituisci ESATTAMENTE:
- `docs/API.md` riga 3 `Italian version: [API-IT.md](API-IT.md).` -> riga VUOTA (la riga resta, senza testo).
- `docs/API.md` righe 6-7: oggi `> [GAME-GUIDE.md](GAME-GUIDE.md). In Italian: [API-IT.md](API-IT.md) and` / `> [GUIDA-GIOCHI.md](GUIDA-GIOCHI.md).` diventano
```
> [GAME-GUIDE.md](GAME-GUIDE.md). In Italian:
> [GUIDA-GIOCHI.md](GUIDA-GIOCHI.md).
```
- `AI.md` riga 18: `API in [docs/API-IT.md](docs/API-IT.md) ([docs/API.md](docs/API.md) in English). The same` -> `API in [docs/API.md](docs/API.md). The same`.
- `sdk/README.md` riga 41: togli SOLO la parte ` ([API-IT.md](../docs/API-IT.md) in Italian)` (spazio iniziale compreso); il resto della riga resta.
- `docs/progress/lua.md` riga 5: togli SOLO `` `docs/API-IT.md`, `` (con lo spazio che segue); resta `its copies (`docs/GAME-GUIDE.md`, `docs/GUIDA-GIOCHI.md`): not repeated`.

## Verifica
1. `git grep -c "API-IT" -- docs/API.md AI.md sdk/README.md docs/progress/lua.md`: nessuna uscita.
2. `python scripts/docs_audit.py score`: stesso `penalties P` di prima, nessuna riga `  !` nuova.
3. `git diff --stat`: 4 file; `git status --short`: solo i 4 file.

Criterio di fine: 4 file, 6 righe cambiate.
Commit: un solo commit sul branch `claude/docs-it-links-api`, messaggio `docs: drop the links to the nonexistent API-IT.md`. Push solo di questo branch (`git push -u origin claude/docs-it-links-api`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-26 --repo C:\Users\Utente\Documents\GitHub\bm33-agy26 --branch claude/docs-it-links-api --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGY-26 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-26 @ claude/docs-it-links-api@<sha> · verifica: git grep API-IT sui 4 file -> nessuna uscita; docs_audit.py score -> penalties come prima · 4 file · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-26 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-mechanic (testo esatto).
Stima: 5 minuti di lavoro.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 `API-IT.md` non si ricrea (decisione di SPV). Raccomandata: sì.

## Prove per l'utente
Nessuna.
