# AGY-22 - IT2EN-SYSTEM: il blocco "Console Standalone & Hardware Spunti" di `docs/progress/system.md` passa all'inglese
Assegnato ad AGY (16 ott), indipendente dagli altri WO ma nello stesso file di AGY-12 (vedi NON toccare). I file di `docs/progress/` sono in inglese (regola del repo); questo blocco (R1, R2, R3, R6, R8, R19, R20 e la voce di rete) è rimasto in italiano. È solo una traduzione: il significato, i numeri e i nomi non cambiano.
Base: `origin/bm-core` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/docs-en-system-console C:\Users\Utente\Documents\GitHub\bm33-agy22 origin/bm-core`); branch `claude/docs-en-system-console`. Mai push su bm-core; push solo di questo branch.
Perimetro: `docs/progress/system.md`, SOLO il blocco che inizia con il titolo `### Console Standalone & Hardware Spunti (R1, R2, R3, R4, R6, R8, R19, R20)` e finisce con il bullet `- **Rete & bmnet Multiplayer** …` compreso (oggi righe 86-95: 10 righe).
NON toccare: nessun altro file; in questo file nessuna riga fuori dal blocco. In particolare la riga `- **Freeze reports (secondary, later):** …` e le sue righe di continuazione (oggi 78-83: è l'oggetto di AGY-12, già inglese), la riga `- **R4 — Screenshot**: …` (già inglese: resta IDENTICA, byte per byte), la riga vuota dopo il blocco e la sezione `## Rules (do not break)` (oggi riga 97). Non cambiare numeri, `code span`, nomi di funzioni e di file, il link `[`SCREENSHOTS.md`](../SCREENSHOTS.md)`, né il numero di righe (una riga italiana diventa UNA riga inglese).
Vincoli: solo documentazione, nessun WSL, nessun hardware (`not verified on hardware`). I numeri di riga sono stati aperti dal planner su `origin/bm-core` @ `e6d7bfd7` (su `claude/integrazione-ottobre` il blocco è 5 righe più in alto: 81-90; su altri rami può essere altrove): PRIMA di modificare ritrova gli estremi con `git grep -n "Hardware Spunti" -- docs/progress/system.md` (UNA riga: il titolo) e `git grep -n "Rete & bmnet" -- docs/progress/system.md` (UNA riga: l'ultimo bullet). NON fidarti dei numeri; se non sono una riga ciascuno, o se tra le due non ci sono 8 righe, `BLOCKED`.
Obiettivo: nessuna parola italiana nel blocco; `docs_audit.py score` senza penalità nuove.

## Stato reale (verificato dal planner sul testo di `e6d7bfd7`)
Il blocco ha 10 righe: titolo (86), R1 (87), R2 (88), R3 (89), R4 (90, GIÀ INGLESE), R6 (91), R8 (92), R19 (93), R20 (94), rete (95). Le righe da cambiare sono quindi 9 (tutte tranne R4). Dopo il blocco c'è una riga vuota e `## Rules (do not break)`.
Il titolo contiene "Spunti" ("ideas"): nessun altro file ne cita il testo o l'ancora (`git grep -n "Spunti\|console-standalone"` dà solo questa riga), quindi si traduce.
Fine riga: `git ls-files --eol docs/progress/system.md` mostra `w/lf` o `w/crlf`: tieni quello del file. Se `git diff --stat` dice più di 9 righe cambiate, hai cambiato i fine riga: `git checkout -- docs/progress/system.md` e rifai.

## AGY-22a - misura PRIMA (bm-mechanic, 2 min)
Dalla radice del worktree: `python scripts/docs_audit.py score`; segna la prima riga (`score N  (always loaded … + penalties P)`) e le eventuali righe `  !`. Sulla base il planner ha visto `penalties 0`.

## AGY-22b - la traduzione (bm-light, 15 min)
Sostituisci le 9 righe italiane (titolo, R1, R2, R3, R6, R8, R19, R20, rete), lasciando la riga R4 al suo posto (tra R3 e R6) senza toccarla. Il blocco finale, con R4 invariata, è:
```
### Standalone Console & Hardware Ideas (R1, R2, R3, R4, R6, R8, R19, R20)
- **R1 — WiFi from the menu with the pad**: Settings > WiFi and network with network selection via gamepad and credential entry through the on-screen keyboard (R2).
- **R2 — Kernel on-screen keyboard**: Turn `padtype` into a native C kernel service (usable for WiFi setup, file renaming, the Market) and expose it to games through the Lua API (`textinput(title, default)`).
- **R3 — SD log with crash traceback**: Write `bm/log.txt` to the Raspberry Pi SD card with the boot log and the traceback of the last Lua error of a cartridge.
- **R4 — Screenshot**: done in the games' runtime (`shot()`, Print Screen, Select+L1+R1, `bm/shots/`: [`SCREENSHOTS.md`](../SCREENSHOTS.md)). To do: the menu's own screens, a try on the Pi and the RGB30 (3D `later` mode), the key's picture in the F12 list.
- **R6 — Local web interface (`bm.local`)**: Built-in HTTP server and mDNS responder (`bm.local`) to allow uploading `.bm` files, downloading screenshots/saves and editing `bm/config.txt` directly from the browser of a local phone or PC.
- **R8 — Rumble and LED on the DualShock 4**: Expose the APIs `rumble(p, strong, weak, ms)` and `padlight(p, color)` using the output report 0x11 already implemented in `bt.c`.
- **R19 — Extended Bluetooth controller support**: Integrate profiles for DualSense, Switch Pro Controller, 8BitDo and Xbox gamepads (Bluetooth LE through `ble.c`).
- **R20 — HDMI-CEC remote control**: Support for the directional arrows and the Select/OK key of the TV remote to navigate the console menus without a controller attached.
- **Network & bmnet multiplayer**: Test two Pi consoles connected on the *Online 2D* model (also via relay) and the final port of Overbit's network protocol on top of the unified `bmnet` library.
```
Nota: nelle firme di API ancora da scrivere (`textinput(title, default)`, `rumble(p, strong, weak, ms)`, `padlight(p, color)`) i nomi dei parametri sono tradotti (erano `titolo`, `forte`, `debole`, `colore`): non esistono ancora nel codice, quindi sono segnaposto di documentazione (decisione D1). Se ritrovi una di queste API nel codice con i nomi italiani (`git grep -n "textinput\|padlight" -- src`), `BLOCKED`.
Se una riga attuale ha un contenuto che il testo sopra non rende, NON inventare: segui l'originale e scrivilo nella consegna.

## AGY-22c - verifica
1. Parole italiane rimaste: `git grep -nE "\b(il|lo|gli|dei|dal|nel|nei|alla|sul|sui|sulle|dalla|anche|quando|dopo|ogni|tutti|oppure|quindi|invece|dove|sono|che|della|delle|degli|nella|nelle|tramite|senza|Fase|Passo|Criterio|Esecuzione|Misura|Profilazione|Adattamento|Rete|Spunti|Tastiera|Trasformazione|Scrittura|Esposizione|Supporto|Integrazione|Server)\b" -- docs/progress/system.md`. Atteso: NESSUNA riga. (Oggi dà 9 righe, tutte nel blocco: 86, 87, 88, 89, 91, 92, 93, 94, 95; il controllo non vede tutte le parole italiane: fa fede anche che il blocco sia IDENTICO al testo del passo AGY-22b.)
2. La riga R4 non è cambiata: `git diff -U0 docs/progress/system.md` non deve mostrare la riga `- **R4 — Screenshot**` né la riga `- **Freeze reports` tra le righe `-`/`+`.
3. `python scripts/docs_audit.py score`: stesso `penalties P` di prima e nessuna riga `  !` nuova (`score N` cambia di poco: normale).
4. `git diff --stat`: 1 file, 9 inserimenti e 9 cancellazioni (`docs/progress/system.md | 18 +++++++++---------`). `git status --short`: solo quel file.

Criterio di fine: punti 1-4 verificati; il blocco ha ancora 10 righe; nessun'altra riga del file cambiata.
Commit: un solo commit sul branch `claude/docs-en-system-console`, messaggio `docs: translate the standalone console block of system.md to English`. Push solo di questo branch (`git push -u origin claude/docs-en-system-console`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-22 --repo C:\Users\Utente\Documents\GitHub\bm33-agy22 --branch claude/docs-en-system-console --commit <sha del commit> --tokens <N> --min <M> --state FREE` (stampa la riga chat `done WO AGY-22 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-22 @ claude/docs-en-system-console@<sha> · verifica: git grep parole italiane su system.md -> 0 righe; docs_audit.py score -> penalties <P> come prima; riga R4 invariata · 1 file, +9 -9 · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-22 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-light (traduzione; testo già fornito).
Stima: 20 minuti di lavoro; nessuna compilazione.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 I nomi dei parametri delle API ancora da scrivere (`titolo`, `forte`, `debole`, `colore`) si traducono, perché non esistono nel codice. Alternativa: lasciarli italiani dentro i `code span`. Raccomandata: tradurli.
- D2 Titolo: `Spunti` → `Ideas`, e `Console Standalone` → `Standalone Console` (ordine naturale in inglese). Raccomandata: sì.
- D3 Conflitti con AGY-12: se AGY-12 è già integrato quando consegni, `git pull --rebase` non deve dare conflitti (le righe sono diverse); se li dà, `BLOCKED` con l'uscita.

## Prove per l'utente
Nessuna.
