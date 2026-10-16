# AGY-18 - IT2EN-MALI: la lista M41 di `docs/progress/graphics-mali.md` passa dall'italiano all'inglese
Assegnato ad AGY (16 ott), indipendente dagli altri WO. I file di `docs/progress/` sono in inglese (regola del repo); questa lista è rimasta in italiano. È solo una traduzione: il significato, i numeri e i nomi non cambiano.
Base: `origin/bm-core` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/docs-en-mali-m41 C:\Users\Utente\Documents\GitHub\bm33-agy18 origin/bm-core`); branch `claude/docs-en-mali-m41`. Mai push su bm-core; push solo di questo branch.
Perimetro: `docs/progress/graphics-mali.md`, SOLO le righe della lista M41 (oggi 33-44).
NON toccare: nessun altro file, e in questo file nessuna riga fuori dall'intervallo (il titolo `## M41 Roadmap: Mali GPU & Overbit in `.b16` (RGB30)` è già inglese e resta com'è). Non cambiare numeri, `code span` (testo tra apici inversi), nomi di funzioni, né il numero di righe (una riga italiana diventa UNA riga inglese: niente a-capo nuovi, niente righe unite).
Vincoli: solo documentazione, nessun WSL, nessun hardware (`not verified on hardware`). I numeri di riga sono stati aperti dal planner su `origin/bm-core` @ `e6d7bfd7` ma possono essere spostati: PRIMA di modificare ritrova il blocco con `git grep -n "su RGB30" -- docs/progress/graphics-mali.md` (una riga, oggi la 33, inizia con `1. **Overbit in`) e `git grep -n "Criterio di completamento" -- docs/progress/graphics-mali.md` (una riga, oggi la 44): il blocco da tradurre va dalla prima all'ultima compresa. NON fidarti dei numeri; se le due righe non sono quelle (o sono più di una): `BLOCKED`.
Obiettivo: nessuna parola italiana nel blocco; `docs_audit.py score` senza penalità nuove.

## Stato reale (verificato dal planner sul testo di `e6d7bfd7`)
Le 12 righe del blocco sono tutte italiane (nessuna da lasciare com'è). Subito sopra c'è una riga vuota e il titolo M41 (inglese); subito sotto il file finisce con una riga vuota.
Fine riga: `git ls-files --eol docs/progress/graphics-mali.md` mostra `w/lf` o `w/crlf`: tieni quello che il file ha già (le righe nuove con lo stesso fine riga). Se `git diff --stat` dice più di 12 righe cambiate, hai cambiato i fine riga: `git checkout -- docs/progress/graphics-mali.md` e rifai.

## AGY-18a - misura PRIMA (bm-mechanic, 2 min)
Dalla radice del worktree: `python scripts/docs_audit.py score` e segna la prima riga (`score N  (always loaded … + penalties P)`) e le eventuali righe che iniziano con `  !`. Sulla base il planner ha visto `penalties 0`.

## AGY-18b - la traduzione (bm-light, 10 min)
Sostituisci le 12 righe del blocco (dalla riga `1. **Overbit in …` alla riga `- **Criterio di completamento** …` compresa) con ESATTAMENTE queste 12 righe (rientri come sono: 3 spazi prima di `- `, nessun rientro per `1.`, `2.`, `3.` e per l'ultima):
```
1. **Overbit in `.b16` on RGB30**:
   - Runs at **360×360** (doubled ×2 by the hardware video controller) and native **720×720**, selectable in the *RESOLUTION* menu.
   - Adaptation with `screen()` for square modes (`fb_init_mode`: 64-byte rows and tiles of 16).
   - In Overbit: adapt the HUD, menus, sky and field of view 1:1 (`SW`, `SH`, `LW`×`LH` and `UI` of `00_core.lua`).
2. **Benchmark with ARM**:
   - Measure Overbit's performance (bot fight and hero ring) at both resolutions on ARM, with report generation.
3. **Mali driver in phases**:
   - *Phase 1*: triangles prepared by the ARM and rasterized by the GPU (triangle in the *GPU test*).
   - *Phase 2*: `r3d` backend with batching and one color per draw.
   - *Phase 3*: vertex shader for the geometry transforms on the GPU.
   - *Phase 4*: asynchronous job queue with multi-buffering support.
- **Completion criterion**: On the RGB30 Overbit `.b16` runs stably at 360×360 and at 720×720, the benchmark produces the performance report (first with ARM, then with Mali) and the RGB30 3D Bench table reports the GPU column data.
```
(Se la riga originale di una voce ha più o meno punti di quelli sopra, segui il significato dell'originale e scrivilo nella consegna: il testo sopra è stato preparato dal planner dalle 12 righe attuali.)

## AGY-18c - verifica
1. Parole italiane rimaste nel file: `git grep -nE "\b(il|lo|gli|dei|dal|nel|nei|alla|sul|sui|sulle|dalla|anche|quando|dopo|ogni|tutti|oppure|quindi|invece|dove|sono|che|della|delle|degli|nella|nelle|tramite|senza|Fase|Passo|Criterio|Esecuzione|Misura|Profilazione|Adattamento)\b" -- docs/progress/graphics-mali.md`. Atteso: NESSUNA riga (oggi ne dà 8, tutte nel blocco: righe 34, 35, 38, 40, 41, 42, 43, 44; dopo, zero. Il controllo non vede tutte le parole italiane: fa fede anche che le 12 righe siano IDENTICHE al testo del passo AGY-18b). Se resta una riga, riportala e correggi.
2. `python scripts/docs_audit.py score`: atteso lo stesso `penalties P` di prima e nessuna riga `  !` nuova (il numero `score N` cambia di poco: è normale).
3. `git diff --stat`: atteso `docs/progress/graphics-mali.md | 24 ++++++++++++------------` (12 inserimenti, 12 cancellazioni), 1 file. `git status --short`: solo quel file.

Criterio di fine: punti 1-3 verificati; il blocco ha ancora 12 righe; nessun'altra riga del file cambiata.
Commit: un solo commit sul branch `claude/docs-en-mali-m41`, messaggio `docs: translate the M41 list in graphics-mali.md to English`. Push solo di questo branch (`git push -u origin claude/docs-en-mali-m41`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-18 --repo C:\Users\Utente\Documents\GitHub\bm33-agy18 --branch claude/docs-en-mali-m41 --commit <sha del commit> --tokens <N> --min <M> --state FREE` (stampa la riga chat `done WO AGY-18 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-18 @ claude/docs-en-mali-m41@<sha> · verifica: git grep parole italiane su graphics-mali.md -> 0 righe; docs_audit.py score -> penalties <P> come prima · 1 file, +12 -12 · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-18 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-light (traduzione; testo già fornito).
Stima: 15 minuti di lavoro; nessuna compilazione.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 "tessere da 16" tradotto `tiles of 16` e "raddoppio ×2 dal controller video hardware" `doubled ×2 by the hardware video controller`: fedele all'originale. Raccomandata: sì.

## Prove per l'utente
Nessuna.
