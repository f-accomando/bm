# AGY-29 - IT2EN-PADTYPE: `PADTYPE.md`: riga 3 svuotata e commenti Lua dell'esempio tradotti in inglese
Assegnato ad AGY (16 ott). `docs/PADTYPE.md` è inglese ma la riga 3 linka `PADTYPE-IT.md` (non esiste) e l'esempio Lua ha commenti italiani.
Base: `origin/claude/integrazione-ottobre` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/docs-en-padtype C:\Users\Utente\Documents\GitHub\bm33-agy29 origin/claude/integrazione-ottobre`); branch `claude/docs-en-padtype`. Mai push su bm-core; push solo di questo branch.
Perimetro: `docs/PADTYPE.md`: riga 3 (diventa vuota) e i soli testi dei commenti dopo `--` alle righe 210-217 e 220.
NON toccare: tutto il codice Lua (prima di `--`); le parole italiane d'esempio nel resto della pagina ("ciao", "che", "domani", `pt.text_host("it")`): sono dati e restano; ogni altro file.
Disgiunzione: i file di questo WO non sono nei perimetri di AGY-10..25 (perimetri letti in `sync/wo/`), AGX-3..12, APP-7/8 e R-NET6..9; `docs/PADTYPE.md` non è nei loro perimetri.
Vincoli: solo documentazione, nessun WSL. `not verified on hardware`. I numeri di riga qui sotto sono stati riaperti dal planner con `git show origin/claude/integrazione-ottobre:<file>` ma possono essere spostati: PRIMA di ogni modifica ri-trova ogni punto con `git grep -n -F` (comandi indicati) e NON fidarti del numero. Ciò che non ritrovi esattamente come descritto: `BLOCKED`, non indovinare. Fine riga: `git ls-files --eol <file>` mostra `w/lf` o `w/crlf`: tieni quello del file; `git diff --stat` deve restare piccolo (vedi Criterio): se dice centinaia di righe hai cambiato i fine riga, `git checkout -- <file>` e rifai.
Obiettivo: i commenti dell'esempio sono in inglese e il resto del file è invariato.

## Stato reale (verificato su `origin/claude/integrazione-ottobre` @ f5cd71b7)
Riga 3 = `Italian version: [PADTYPE-IT.md](PADTYPE-IT.md).`; il blocco lua alle righe 208-225 ha i commenti italiani alle righe 210, 211, 212, 213, 215, 216, 217, 220 (la 214 non ne ha). Ritrovali con `git grep -n -E "spegne|spenta|proprio|maiuscole|cursore| il pad |i nomi del codice" -- docs/PADTYPE.md`.

## AGY-29a - misura PRIMA (bm-mechanic, 1 min)
`python scripts/docs_audit.py score`: prima riga e righe `  !`.

## AGY-29b - le modifiche (bm-light, 4 min)
Riga 3 -> riga vuota. Poi, SOLO nel testo del commento dopo `--` (il codice prima resta identico, spazi compresi):
- `o "off": Share spegne` -> `or "off": Share turns it off`
- `"keyboard", nil: spenta` -> `"keyboard", nil: off`
- `un testo; o un host proprio:` -> `a text; or a host of your own:`
- `il testo prima del cursore, sulla sua riga` -> `the text before the cursor, on its line`
- `o "none"` -> `or "none"` (il resto della riga, `{it = 1, ask = 2}`, invariato)
- `maiuscole a inizio frase (di default: non per "lua")` -> `capitals at the start of a sentence (default: not for "lua")`
- `i nomi del codice` -> `the names in the code`
- `il pad è dell'applicazione (spenta)` -> `the pad belongs to the application (off)`

## Verifica
1. `git grep -nE "spegne|spenta|proprio|maiuscole|cursore| il pad " -- docs/PADTYPE.md`: nessuna uscita.
2. `git grep -n -F "PADTYPE-IT" -- docs/PADTYPE.md`: nessuna uscita; `python scripts/docs_audit.py score`: nessuna penalità nuova.
3. `git diff --stat`: 1 file, +9 -9.

Criterio di fine: 1 file, +9 -9 (la riga 3 e 8 commenti).
Commit: un solo commit sul branch `claude/docs-en-padtype`, messaggio `docs: PADTYPE.md example comments in English, drop the missing -IT link`. Push solo di questo branch (`git push -u origin claude/docs-en-padtype`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-29 --repo C:\Users\Utente\Documents\GitHub\bm33-agy29 --branch claude/docs-en-padtype --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGY-29 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-29 @ claude/docs-en-padtype@<sha> · verifica: git grep parole italiane in PADTYPE.md -> nessuna uscita; docs_audit.py score senza penalità nuove · 1 file, +9 -9 · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-29 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-light (traduzione; testo fornito).
Stima: 5 minuti di lavoro.

## Decisioni (proposte del planner, da adottare da SPV)
Nessuna.

## Prove per l'utente
Nessuna.
