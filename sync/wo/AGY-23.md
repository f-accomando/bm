# AGY-23 - IT2EN-TOOLS: tre voci italiane di "Open work" in `docs/progress/tools.md` (mappe 3D, R15, R26) passano all'inglese
Assegnato ad AGY (16 ott), indipendente dagli altri WO. I file di `docs/progress/` sono in inglese (regola del repo); queste tre voci sono rimaste in italiano. È solo una traduzione: il significato, i numeri e i nomi non cambiano.
Base: `origin/bm-core` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/docs-en-tools-open C:\Users\Utente\Documents\GitHub\bm33-agy23 origin/bm-core`); branch `claude/docs-en-tools-open`. Mai push su bm-core; push solo di questo branch.
Perimetro: `docs/progress/tools.md`, SOLO le tre righe `- **Mappe 3D in bm SDK** …`, `- **R15** …`, `- **R26** …` (oggi righe 97, 98, 99: sono consecutive, in fondo alla sezione `## Open work`).
NON toccare: nessun altro file; in questo file nessun'altra riga. In particolare le righe vicine di altri WO: la voce `- **Pi tests from the PC** …` (R-NET5, oggi riga 79) e la voce `- **PDF manuals** …` (BETA-7, oggi riga 80) sono già inglesi e restano IDENTICHE. Non cambiare numeri, `code span`, nomi di file/funzioni/tasti (`Shift+P`, `map3d_*`, `r3d_visible`, `bm_net.py --watch`), né il numero di righe (ogni riga italiana diventa UNA riga inglese: la voce delle mappe è una riga molto lunga e resta una riga).
Vincoli: solo documentazione, nessun WSL, nessun hardware (`not verified on hardware`). I numeri di riga sono stati aperti dal planner su `origin/bm-core` @ `e6d7bfd7` ma possono essere spostati (altri WO e rami aggiungono righe in quella sezione): PRIMA di modificare ritrova le tre righe con `git grep -n "Mappe 3D in bm SDK\|^- \*\*R15\*\*\|^- \*\*R26\*\*" -- docs/progress/tools.md` (devono essere ESATTAMENTE tre righe, una per voce). NON fidarti dei numeri; se non sono tre, `BLOCKED`.
Obiettivo: nessuna parola italiana in quelle tre righe; `docs_audit.py score` senza penalità nuove.

## Stato reale (verificato dal planner sul testo di `e6d7bfd7`)
Le tre righe sono tutte italiane. La riga delle mappe 3D è lunga (circa 600 caratteri) ma è una riga sola. Su `e6d7bfd7` il controllo parole italiane dà nel file solo le righe 97, 98, 99.
Fine riga: `git ls-files --eol docs/progress/tools.md` mostra `w/lf` o `w/crlf`: tieni quello del file. Se `git diff --stat` dice più di 3 righe cambiate, hai cambiato i fine riga: `git checkout -- docs/progress/tools.md` e rifai.

## AGY-23a - misura PRIMA (bm-mechanic, 2 min)
Dalla radice del worktree: `python scripts/docs_audit.py score`; segna la prima riga (`score N  (always loaded … + penalties P)`) e le eventuali righe `  !`. Sulla base il planner ha visto `penalties 0`.

## AGY-23b - la traduzione (bm-light, 10 min)
Sostituisci le tre righe con ESATTAMENTE queste tre righe (ognuna è una riga sola, senza a-capo):
```
- **3D maps in bm SDK** (Tropic Trail AGY-01..03 completed): C module `src/bm/map3d.c` with Lua API `map3d_*` (mesh in 16×16-tile pieces with frustum culling `r3d_visible`, floor height queries, wall/step collisions, object placeholders); 3D view in the editor with the `v` key and an orbit camera (AGY-02); straight path tools `Shift+P` (width 1–5 with `Shift+W`), apple rows `Shift+A`, crates `Shift+C`, lamps `Shift+O`, entity count and triangle budget in the status bar (AGY-03).
- **R15** Live reload / auto-watch from the PC: `bm_net.py --watch` to automatically send and relaunch the cartridge at every local save of `main.lua`.
- **R26** Native hardware mouse in nano8: `mouse(true)` support for `.p8` cartridges that enable the pointer with `poke(0x5f2d, 1)`, exporting coordinates and wheel on `stat(32)`–`stat(36)`.
```
(Il trattino lungo `–` in `1–5` e in `stat(32)`–`stat(36)` resta il carattere `–` dell'originale: copialo dal testo sopra.)
Se la riga attuale ha un contenuto che il testo sopra non rende (per esempio qualcuno ha già aggiunto un pezzo alla voce delle mappe), NON inventare: segui l'originale, traduci quel pezzo con lo stesso criterio e scrivilo nella consegna.

## AGY-23c - verifica
1. Parole italiane rimaste: `git grep -nE "\b(il|lo|gli|dei|dal|nel|nei|alla|sul|sui|sulle|dalla|anche|quando|dopo|ogni|tutti|oppure|quindi|invece|dove|sono|che|della|delle|degli|nella|nelle|tramite|senza|Fase|Passo|Criterio|Esecuzione|Misura|Profilazione|Adattamento|Mappe|Mouse hardware|nativo|supporto|cartucce|cartuccia|salvataggio|strumenti|conteggio|barra|vista)\b" -- docs/progress/tools.md`. Atteso: NESSUNA riga. (Oggi dà le righe delle tre voci; il controllo non vede tutte le parole italiane: fa fede anche che le tre righe siano IDENTICHE al testo del passo AGY-23b.)
2. `python scripts/docs_audit.py score`: stesso `penalties P` di prima e nessuna riga `  !` nuova (`score N` cambia di poco: normale).
3. `git diff --stat`: 1 file, 3 inserimenti e 3 cancellazioni (`docs/progress/tools.md | 6 +++---`). `git status --short`: solo quel file.

Criterio di fine: punti 1-3 verificati; nessun'altra riga del file cambiata.
Commit: un solo commit sul branch `claude/docs-en-tools-open`, messaggio `docs: translate three open-work items of tools.md to English`. Push solo di questo branch (`git push -u origin claude/docs-en-tools-open`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-23 --repo C:\Users\Utente\Documents\GitHub\bm33-agy23 --branch claude/docs-en-tools-open --commit <sha del commit> --tokens <N> --min <M> --state FREE` (stampa la riga chat `done WO AGY-23 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-23 @ claude/docs-en-tools-open@<sha> · verifica: git grep parole italiane su tools.md -> 0 righe; docs_audit.py score -> penalties <P> come prima · 1 file, +3 -3 · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-23 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-light (traduzione; testo già fornito).
Stima: 15 minuti di lavoro; nessuna compilazione.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 "file di mele" tradotto `apple rows` (sono file nel senso di righe di oggetti piazzate con `Shift+A`). Raccomandata: sì.
- D2 Conflitti: BETA-7 e R-NET5 modificano righe vicine (79-80) dello stesso file; se `git pull --rebase` dà conflitto, `BLOCKED` con l'uscita (non risolvere a mano).

## Prove per l'utente
Nessuna.
