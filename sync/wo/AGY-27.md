# AGY-27 - IT-LINKS-GUIDA: due riferimenti a `API-IT.md` in `GUIDA-GIOCHI.md` puntano ai file `docs/api/*.md` esistenti
Assegnato ad AGY (16 ott). Due righe di `docs/GUIDA-GIOCHI.md` citano `API-IT.md` (non esiste). Si puntano ai capitoli inglesi che esistono.
Base: `origin/claude/integrazione-ottobre` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/docs-it-links-guida C:\Users\Utente\Documents\GitHub\bm33-agy27 origin/claude/integrazione-ottobre`); branch `claude/docs-it-links-guida`. Mai push su bm-core; push solo di questo branch.
Perimetro: `docs/GUIDA-GIOCHI.md`, SOLO le righe 119 e 587 (2 righe).
NON toccare: ogni altra riga di `GUIDA-GIOCHI.md`, in particolare le righe 3-5 e circa 822 (BETA-7: `GAME-GUIDE.md:6` e `GUIDA-GIOCHI.md:4` appartengono a BETA-7a, e la riga 4 contiene ancora `API-IT.md`: deve restare) e il codice Lua di AGY-2; qualunque altro file.
Disgiunzione: i file di questo WO non sono nei perimetri di AGY-10..25 (perimetri letti in `sync/wo/`), AGX-3..12, APP-7/8 e R-NET6..9; `docs/GUIDA-GIOCHI.md` è toccato anche da BETA-7 (righe 3-5, circa 822) e AGY-2 (codice Lua): righe diverse, l'ordine di integrazione lo decide SPV.
Vincoli: solo documentazione, nessun WSL. `not verified on hardware`. I numeri di riga qui sotto sono stati riaperti dal planner con `git show origin/claude/integrazione-ottobre:<file>` ma possono essere spostati: PRIMA di ogni modifica ri-trova ogni punto con `git grep -n -F` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi esattamente come descritto: `BLOCKED`, non indovinare. Fine riga: `git ls-files --eol <file>` mostra `w/lf` o `w/crlf`: tieni quello del file; `git diff --stat` deve restare piccolo (vedi Criterio): se dice centinaia di righe hai cambiato i fine riga, `git checkout -- <file>` e rifai.
Obiettivo: in `docs/GUIDA-GIOCHI.md` `git grep -n API-IT` restituisce solo la riga 4.

## Stato reale (verificato su `origin/claude/integrazione-ottobre` @ f5cd71b7)
- `git grep -n -F "API-IT" -- docs/GUIDA-GIOCHI.md` dà 3 righe: 4 (fuori WO), 119, 587.
- Esistono `docs/api/input.md` e `docs/api/bmlib.md`; il titolo di `bmlib.md` riga 5 è `## bmlib: the games' shared library`, quindi l'ancora è `#bmlib-the-games-shared-library`.

## AGY-27a - misura PRIMA (bm-mechanic, 1 min)
`python scripts/docs_audit.py score`: segna la prima riga e le righe `  !`.

## AGY-27b - le modifiche (bm-mechanic, 4 min)
- Riga 119: sostituisci `` `docs/API-IT.md` `` con `` `docs/api/input.md` `` (solo quella parte; il resto della riga resta).
- Riga 587: sostituisci `[API-IT.md](API-IT.md#bmlib-la-libreria-comune-dei-giochi)` con `[api/bmlib.md](api/bmlib.md#bmlib-the-games-shared-library)`; il resto della riga (`. Ecco un platform intero, senza`) resta.

## Verifica
1. `git grep -n -F "API-IT" -- docs/GUIDA-GIOCHI.md`: solo la riga 4.
2. `git grep -n -F "api/bmlib.md#bmlib-the-games-shared-library" -- docs/GUIDA-GIOCHI.md`: una riga (587); `python scripts/docs_audit.py score`: nessuna penalità nuova.
3. `git diff --stat`: 1 file, +2 -2.

Criterio di fine: 1 file, +2 -2.
Commit: un solo commit sul branch `claude/docs-it-links-guida`, messaggio `docs: point GUIDA-GIOCHI.md at docs/api instead of API-IT.md`. Push solo di questo branch (`git push -u origin claude/docs-it-links-guida`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-27 --repo C:\Users\Utente\Documents\GitHub\bm33-agy27 --branch claude/docs-it-links-guida --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGY-27 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-27 @ claude/docs-it-links-guida@<sha> · verifica: git grep API-IT in GUIDA-GIOCHI.md -> solo riga 4; docs_audit.py score senza penalità nuove · 1 file, +2 -2 · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-27 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-mechanic (testo esatto).
Stima: 5 minuti di lavoro.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 La riga 4 resta a BETA-7a. Raccomandata: sì.

## Prove per l'utente
Nessuna.
