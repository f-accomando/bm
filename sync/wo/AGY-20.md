# AGY-20 - IT2EN-OPEN3: otto voci italiane di "Open work" in `audio-output.md`, `runtime.md`, `platforms.md` passano all'inglese
Assegnato ad AGY (16 ott), indipendente dagli altri WO. I file di `docs/progress/` sono in inglese (regola del repo); queste voci (R21, R7, R27, M41, M42, R22) sono rimaste in italiano. È solo una traduzione: il significato, i numeri e i nomi non cambiano.
Base: `origin/bm-core` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/docs-en-open-items C:\Users\Utente\Documents\GitHub\bm33-agy20 origin/bm-core`); branch `claude/docs-en-open-items`. Mai push su bm-core; push solo di questo branch.
Perimetro: tre file, SOLO le righe indicate: `docs/progress/audio-output.md` (voce R21: 3 righe, oggi 37-39), `docs/progress/runtime.md` (R7 e R27: 2 righe, oggi 55-56), `docs/progress/platforms.md` (M41, M42, R22: 3 righe, oggi 69-71). Totale 8 righe.
NON toccare: nessun altro file; in questi tre file nessuna riga fuori dalle 8 (compresa la riga di `platforms.md` che inizia con `- SDK b16 (` e contiene "layout a due colonne": è italiana ma NON è in questo WO, la segnala al planner nella consegna; e le righe già inglesi vicine, per esempio la riga `- RGB30 Start+Select freezes the game…` di platforms.md). Non cambiare numeri, `code span`, nomi, link markdown `[graphics-mali](graphics-mali.md)` e `[b16](b16.md)`, né il numero di righe (una riga italiana diventa UNA riga inglese). Nella voce R7 le parole di esempio `"città"` e `"perché"` restano ITALIANE (sono l'esempio di accenti da rendere).
Vincoli: solo documentazione, nessun WSL, nessun hardware (`not verified on hardware`). I numeri di riga sono stati aperti dal planner su `origin/bm-core` @ `e6d7bfd7` ma possono essere spostati: PRIMA di modificare ritrova ogni riga con `git grep -n` (comandi sotto) e NON fidarti dei numeri; una riga che non ritrovi esattamente: `BLOCKED`.
Obiettivo: nessuna parola italiana nelle 8 righe (tranne gli esempi di R7); `docs_audit.py score` senza penalità nuove.

## Stato reale (verificato dal planner sul testo di `e6d7bfd7`) e come ritrovare ogni riga
- audio-output.md: `git grep -n "R21" -- docs/progress/audio-output.md` (la prima riga è `- **R21 — Audio senza HDMI …`, oggi 37); le due righe dopo (rientro 2 spazi, `  - `) sono la 38 e la 39.
- runtime.md: `git grep -n "R7 —\|R27 —" -- docs/progress/runtime.md` (due righe consecutive, oggi 55 e 56).
- platforms.md: `git grep -n "^- \*\*M41\*\*\|^- \*\*M42\*\*\|R22 —" -- docs/progress/platforms.md` (tre righe consecutive, oggi 69, 70, 71; la 69 contiene già "at 360×360 e 720×720" a metà inglese).
- Il carattere `—` (lineetta lunga) e `Ω` (ohm) nelle righe restano com'è: copia dal testo sotto, non ritrascrivere con altri caratteri.
- Fine riga: `git ls-files --eol docs/progress/audio-output.md docs/progress/runtime.md docs/progress/platforms.md` mostra `w/lf` o `w/crlf` per ciascuno: tieni quello del file. Se `git diff --stat` dice più di 8 righe cambiate in tutto, hai cambiato i fine riga: `git checkout -- <file>` e rifai.

## AGY-20a - misura PRIMA (bm-mechanic, 2 min)
Dalla radice del worktree: `python scripts/docs_audit.py score`; segna la prima riga (`score N  (always loaded … + penalties P)`) e le eventuali righe `  !`. Sulla base il planner ha visto `penalties 0`.

## AGY-20b - le traduzioni (bm-light, 10 min)
Sostituisci ciascun gruppo di righe con ESATTAMENTE il testo seguente.

File `docs/progress/audio-output.md`, 3 righe (R21):
```
- **R21 — Audio without HDMI (Analog / I2S on the Pi)**:
  - PWM output on GPIO pins with a passive RC filter (270 Ω + 33 nF) and a stereo jack, to allow listening on monitors without built-in speakers.
  - Alternative support for external I2S audio DACs (e.g. MAX98357A / PCM5102).
```
File `docs/progress/runtime.md`, 2 righe (R7, R27):
```
- **R7 — Accented letters in `print()`**: text is rendered in CP437 (`g16_text` in `gfx16.c`); implement transparent decoding of multibyte UTF-8 sequences into CP437 glyphs to avoid wrong glyphs with accents ("città", "perché").
- **R27 — Battery handling in games**: integrate the `battery()` and `battery_low()` states with the runtime library to provide `lib.autosave` at the first critical-battery warning in long games.
```
File `docs/progress/platforms.md`, 3 righe (M41, M42, R22):
```
- **M41** (RGB30): Overbit in `.b16` at 360×360 and 720×720, ARM benchmark and Mali driver (details in [graphics-mali](graphics-mali.md)).
- **M42**: unified `.b16` profile (details in [b16](b16.md)).
- **R22 — Handheld shell for the Pi Zero**: support for handheld setups based on the Pi Zero (reading keys/pad directly on GPIO pins, compact display on a DPI or SPI bus).
```
Se una riga attuale ha un contenuto che il testo sopra non rende, NON inventare: segui l'originale e scrivilo nella consegna.

## AGY-20c - verifica
1. Parole italiane rimaste nelle 8 righe: `git grep -nE "\b(il|lo|gli|dei|dal|nel|nei|alla|sul|sui|sulle|dalla|anche|quando|dopo|ogni|tutti|oppure|quindi|invece|dove|sono|che|della|delle|degli|nella|nelle|tramite|senza|Fase|Passo|Criterio|Esecuzione|Misura|Profilazione|Adattamento|dettagli|Guscio|Gestione|Lettere|Uscita|Supporto)\b" -- docs/progress/audio-output.md docs/progress/runtime.md docs/progress/platforms.md`. Atteso: NESSUNA riga nelle 8 righe. Oggi (prima) il controllo dà: audio-output 37, 38, 39; runtime 55, 56; platforms 56 (la riga `- SDK b16 …`, FUORI da questo WO: deve restare così dopo la tua modifica) e 69, 70, 71. Quindi DOPO: l'unico risultato ammesso è `docs/progress/platforms.md:56`. (Il controllo non vede tutte le parole italiane: fa fede anche che le 8 righe siano IDENTICHE al testo del passo AGY-20b.)
2. `python scripts/docs_audit.py score`: stesso `penalties P` di prima e nessuna riga `  !` nuova (`score N` cambia di poco: normale).
3. `git diff --stat`: 3 file, 8 inserimenti e 8 cancellazioni in tutto (audio-output.md 3+3, runtime.md 2+2, platforms.md 3+3). `git status --short`: solo i tre file.

Criterio di fine: punti 1-3 verificati; ogni gruppo ha lo stesso numero di righe di prima; nessun'altra riga dei tre file cambiata.
Commit: un solo commit sul branch `claude/docs-en-open-items`, messaggio `docs: translate eight open-work items (audio-output, runtime, platforms) to English`. Push solo di questo branch (`git push -u origin claude/docs-en-open-items`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-20 --repo C:\Users\Utente\Documents\GitHub\bm33-agy20 --branch claude/docs-en-open-items --commit <sha del commit> --tokens <N> --min <M> --state FREE` (stampa la riga chat `done WO AGY-20 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-20 @ claude/docs-en-open-items@<sha> · verifica: git grep parole italiane sui 3 file -> solo platforms.md:56 (fuori WO); docs_audit.py score -> penalties <P> come prima · 3 file, +8 -8 · non verificato su hardware · <ISO con fuso>`. Nella chat segnala a SPV che `platforms.md:56` (SDK b16, "layout a due colonne") è ancora italiana.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-20 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-light (traduzione; testo già fornito).
Stima: 15 minuti di lavoro; nessuna compilazione.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 La riga `platforms.md:56` (SDK b16, due colonne, italiana) non è in questo WO per rispettare il perimetro del catalogo: va in un WO separato o si aggiunge qui con una riga di SPV. Raccomandata: WO separato (è una riga sola, 5 minuti).
- D2 "Guscio Portatile" tradotto `Handheld shell`; "Lettere accentate" `Accented letters`. Raccomandata: sì.

## Prove per l'utente
Nessuna.
