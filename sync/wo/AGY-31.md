# AGY-31 - DIV-EVIDENCE: quattro `evidence` di `divergences.toml` puntano a righe sbagliate
Assegnato ad AGY (16 ott). Quattro campi `evidence` di `docs/spec/divergences.toml` citano righe di `docs/progress/*.md` che non contengono la cosa citata.
Base: `origin/claude/integrazione-ottobre` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/divergence-evidence C:\Users\Utente\Documents\GitHub\bm33-agy31 origin/claude/integrazione-ottobre`); branch `claude/divergence-evidence`. Mai push su bm-core; push solo di questo branch.
Perimetro: `docs/spec/divergences.toml`, SOLO la riga `evidence =` di D17 (oggi 204), D24 (282), D27 (315), D39 (437). 4 righe.
NON toccare: ogni altra riga; i blocchi D19 (AGY-21), D43 (APP-7), D45 (R-NET7); D26 e D40/D41: li decide SPV (vedi sotto), NON si toccano; i file `docs/progress/*.md` si LEGGONO soltanto.
Disgiunzione: i file di questo WO non sono nei perimetri di AGY-10..25 (perimetri letti in `sync/wo/`), AGX-3..12, APP-7/8 e R-NET6..9; stesso file di AGY-21 (D19 `note`, riga 226), APP-7 (D43) e R-NET7 (D45 in fondo): blocchi diversi.
Vincoli: solo documentazione/dati, nessun WSL. `not verified on hardware`. I numeri di riga qui sotto sono stati riaperti dal planner con `git show origin/claude/integrazione-ottobre:<file>` ma possono essere spostati: PRIMA di ogni modifica ri-trova ogni punto con `git grep -n -F` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi esattamente come descritto: `BLOCKED`, non indovinare. Fine riga: `git ls-files --eol <file>` mostra `w/lf` o `w/crlf`: tieni quello del file; `git diff --stat` deve restare piccolo (vedi Criterio): se dice centinaia di righe hai cambiato i fine riga, `git checkout -- <file>` e rifai.
Obiettivo: le quattro `evidence` puntano alle righe che contengono il testo indicato e `python scripts/consistency.py check` stampa lo stesso di prima.

## Stato reale (verificato su `origin/claude/integrazione-ottobre` @ f5cd71b7)
- D17 `evidence = "src/rgb30/rtw_sta.c:643; docs/progress/system.md:107"` (riga 204): `system.md:107` è un'altra cosa. Il testo giusto è nelle righe `- **R1 — ` (WiFi dal menu col pad, oggi 82) e `- **R2 — ` (tastiera a schermo, oggi 83): nuova evidence `src/rgb30/rtw_sta.c:643; docs/progress/system.md:82-83`.
- D24 `evidence = "docs/progress/platforms.md:68"` (riga 282): la 68 è vuota; la voce è `- **M41** (RGB30)` (oggi 69): `docs/progress/platforms.md:69`.
- D27 `evidence = "docs/progress/system.md:110"` (riga 315): la voce è `- **R4 — Screenshot**` (oggi 85): `docs/progress/system.md:85`.
- D39 `evidence = "docs/progress/platforms.md:41-45"` (riga 437): il punto elenco `- Battery (` inizia alla riga 40: `docs/progress/platforms.md:40-45`.
- RITROVA ogni numero con `git grep -n -F "R1 — " -- docs/progress/system.md`, idem `"R2 — "` e `"R4 — Screenshot"`, `git grep -n -F "**M41**" -- docs/progress/platforms.md`, `git grep -n -F "Battery (" -- docs/progress/platforms.md`, e le quattro `evidence` con `git grep -n -F "evidence = " -- docs/spec/divergences.toml` (guarda il blocco `id =` sopra). Se un numero è diverso da quello scritto qui, usa quello trovato e scrivilo nella consegna.
- A SPV (non in questo WO): D26 punta a `platforms.md:71` ma la riga giusta è la 56 (riga `- SDK b16`, esclusa dal perimetro di AGY-20); D40/D41: da rivedere.

## AGY-31a - misura PRIMA (bm-mechanic, 1 min)
`python scripts/consistency.py check > /tmp/c_before.txt; echo $?` (salva uscita e codice).

## AGY-31b - le modifiche (bm-mechanic, 9 min)
Cambia SOLO il testo tra virgolette delle 4 righe `evidence =` come sopra, nulla d'altro.

## Verifica
1. `python scripts/consistency.py check` ha stessa uscita e stesso codice di prima (`diff` vuoto).
2. `git diff -U0 -- docs/spec/divergences.toml` mostra solo 4 righe `-` e 4 `+`, ciascuna `evidence =`; le nuove contengono rispettivamente `system.md:82-83`, `platforms.md:69`, `system.md:85`, `platforms.md:40-45`.
3. `git diff --stat`: 1 file, +4 -4.

Criterio di fine: 1 file, +4 -4.
Commit: un solo commit sul branch `claude/divergence-evidence`, messaggio `spec: fix four divergence evidence line numbers`. Push solo di questo branch (`git push -u origin claude/divergence-evidence`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-31 --repo C:\Users\Utente\Documents\GitHub\bm33-agy31 --branch claude/divergence-evidence --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGY-31 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-31 @ claude/divergence-evidence@<sha> · verifica: consistency.py check -> uscita identica a prima; diff -> 4 evidence · 1 file, +4 -4 · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-31 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-mechanic (testo esatto).
Stima: 10 minuti di lavoro.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 D26 (evidence `platforms.md:71`, giusta la riga 56) e D40/D41 li decide SPV. Raccomandata: sì.

## Prove per l'utente
Nessuna.
