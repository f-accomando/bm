# AGY-39 - BETA-HWCARD: una scheda con le 13 prove su hardware che restano per la beta
Assegnato ad AGY (16 ott), indipendente da AGX-13/14/15 e AGY-38 (file nuovo). Nasce da BETA-1, BETA-4 e BETA-6: restano voci che solo l'utente puo' provare sulla console o sul Pi (D01, D02, D04, D06, D07, D08, D12, D13, SYS-5, APP-1, M35, M36, M37). Oggi ognuna e' sparsa in file diversi; la scheda le mette in una tabella con i passi, l'atteso e cosa scrivere nel toml se va bene.
Base: `origin/claude/integrazione-ottobre` @ `f5cd71b7` (`git fetch origin`, poi `git worktree add -b claude/beta-hardware-card C:\Users\Utente\Documents\GitHub\bm33-agy39 origin/claude/integrazione-ottobre`); branch `claude/beta-hardware-card`. Mai push su bm-core; push solo di questo branch.
Perimetro: `docs/spec/beta-hardware-tests.md` (FILE NUOVO, in inglese) e `docs/spec/CONSISTENCY.md` (UNA riga nuova in fondo al file, con il collegamento; le righe 30-37 della tabella "The files" e la riga 47 sono di AGY-11 e non si toccano).
NON toccare: `docs/spec/divergences.toml` (nessuna `status` cambia ora: la scheda dice cosa scrivere DOPO la prova dell'utente), `docs/progress/*` (AGY-40 cita la scheda in `v3d.md`), `docs/ROADMAP.md`, `docs/progress/platforms.md:56` e la nota di D28, `SYNC.md`, `sync/*`, `src/**`.
Disgiunzione dalle code (letti i perimetri di `sync/wo/*.md`): `docs/spec/CONSISTENCY.md` e' toccato anche da AGY-11 (righe 30-37 e 47) e AGY-3 (sola lettura di `beta6-triage.md`): questo WO aggiunge una riga dopo l'ultima riga del file, quindi non si sovrappone. AGY-40 parte DOPO questo WO (cita il file nuovo).
Vincoli: solo documenti, nessuna prova su hardware (`non verificato`: la scheda dice cosa provare, non lo prova). Il worktree NON e' `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. Nessuna build. Ritrova ogni riga con `git grep -n` prima di citarla; un punto che non ritrovi come descritto: `non verificato` e `BLOCKED`.
Obiettivo: il file esiste, ha la tabella `| id | device | prerequisites | steps | expected | if OK |` con 13 righe, `docs_audit.py score` non ha nuovi `!`.

## Prove (aperte dal planner su `f5cd71b7`)
- Stato attuale delle voci nel toml (`docs/spec/divergences.toml`): D01 `untested` (PS/Ctrl+Esc dal pad/tastiera Bluetooth, `bm_input.c`), D02 `untested` (Bluetooth all'avvio con un pad accoppiato), D04 `open` (diventa `untested` con AGX-13), D06/D07/D08 `untested`, D12 `open` (diventa `untested` con AGX-15), D13 `untested`.
- SYS-5: `docs/ROADMAP.md:54` (warm reboot senza schermo nero; Start+Select congela e riprende come sul Pi), passi in `docs/progress/platforms.md:72-74` (congelare, riprendere, congelare e avviare un altro gioco, memoria liberata, Overbit/online non congelati) e `:76-84` (pannello dopo un riavvio a caldo: schermo nero, righe sfasate; leggere le righe `display:` di `bm/bootlog.txt`).
- APP-1: la lista di prove della console e' in `docs/progress/tools.md:85-86` (Dev > bm SDK e Open in bm SDK, pad, scrittura, salvataggio, copia di un gioco, Ctrl+B, F5 e Start+Select di ritorno); `docs/ROADMAP.md:82`.
- M35 (coda dei frame, "async queues"), M36 (vertici sulla GPU) e M37 (2D e qualita' sulla GPU): `docs/ROADMAP.md:65-67`; chiave `gpu3d_queue` (`docs/spec/config-keys.toml:240`, Settings > Screen and sound > 3D frame queue; 0 = spenta); `menu_scale` (`config-keys.toml:339`, "3" = menu 3x, solo Pi); passo 16 del test `g` = lo stesso HUD dall'ARM e dalla GPU, pixel uguali (`docs/DRIVERS.md:111`); il test `queue` del 3D Bench (`docs/BENCH3D.md:20,78-79`); tabella delle righe GPU+VS dello Stress test (`docs/STRESS.md:40`).
- Il ponte: dopo R-NET6 la riga che lancia il test GPU e invia i report e' `python3 tools/pi.py line "gpu; send"` (WO R-NET6; fino all'integrazione di R-NET6 `non verificato`).
- Ritrovare: `git grep -n 'untested' -- docs/spec/divergences.toml | head -30`; `git grep -n 'SYS-5\|APP-1' -- docs/ROADMAP.md`; `wc -l docs/spec/CONSISTENCY.md` (oggi 158 righe).

## AGY-39a - la scheda (bm-light, 35 min)
Passo 1: crea `docs/spec/beta-hardware-tests.md`, in inglese, nell'ordine: titolo `# Beta hardware tests (only the user can run them)`; due frasi (cosa e' la scheda, e che le prove vanno fatte da chi ha la console/il Pi in mano; QEMU non le sostituisce); una frase "How to use": a prova riuscita si apre la voce in `docs/spec/divergences.toml` o nel `Open work` dell'area indicata, e si scrive quanto in "if OK"; se fallisce si apre un'issue con il testo di `bm/bootlog.txt`; poi la tabella con la riga d'intestazione ESATTA
`| id | device | prerequisites | steps | expected | if OK |` e la riga `|---|---|---|---|---|---|`, poi le 13 righe in QUESTO ordine, una per riga di markdown, ognuna che comincia con `| <id> |`:
1. RGB30, solo il pad: `D06`, `D07`, `D08`, `D12`, `SYS-5` (passi di SYS-5 da `platforms.md:72-84`: riavvio a caldo, Start+Select, avviare un altro gioco).
2. RGB30 con Bluetooth: `D01`, `D02`, `D04`.
3. RGB30 con WiFi: `D13` (installare un'app da Market > Apps e controllare che resti fuori da Games), `APP-1` (la lista di `tools.md:85-86`).
4. Pi: `M35` (3D Bench: il test `queue`; e Overbit con la coda spenta e accesa), `M36` (Stress test, righe GPU+VS; l'anello degli eroi di Overbit a 60 fps), `M37` (Dev > test GPU, passo 16 senza pixel diversi; `menu_scale=3` a 1080p; dopo R-NET6 la riga del ponte e' `pi.py line "gpu; send"`).
Colonna `device`: `RGB30 (pad)`, `RGB30 + Bluetooth pad/keyboard`, `RGB30 + WiFi`, `Pi Zero W`. Colonna `prerequisites`: cosa serve in mano (kernel della beta, una SD con due giochi `.bm`, un pad o una tastiera Bluetooth accoppiati, rete, un monitor 1080p per `menu_scale=3`). Colonna `steps`: passi numerati in una cella, separati da `<br>` oppure `;` (la riga resta UNA riga di markdown). Colonna `expected`: l'atteso (da ROADMAP/toml, parole originali dove ci sono). Colonna `if OK`: per le divergenze `status = "resolved"`, note "tried by the user on <device> on <date>"; per SYS-5, APP-1, M35, M36, M37 la voce si segna fatta in `Open work`/`To do` dell'area (`platforms.md`, `tools.md`, `v3d.md`) con la stessa frase "tried by the user on <device> on <date>". Niente numeri inventati: dove la fonte non dice una soglia, scrivi `see <file>`. In coda una sezione `## Not covered` con le voci beta che la scheda NON include (nessuna se non ne trovi: scrivi `none found` solo dopo aver controllato `grep -n untested docs/spec/divergences.toml`).
Passo 2: in `docs/spec/CONSISTENCY.md`, DOPO l'ultima riga del file, una riga vuota e la riga `Hardware checks that remain for the beta: [beta-hardware-tests](beta-hardware-tests.md).` (non spostare niente altro; stesso fine riga del file).
Passo 3 (verifica): `grep -c '^| \(D[0-9]\|SYS\|APP\|M3\)' docs/spec/beta-hardware-tests.md` -> `13` (il WO d'origine citava il modello `\(D0\|SYS\|APP\|M3\)`, che con D12 e D13 darebbe 11: il modello giusto e' questo); `grep -c '^| id |' docs/spec/beta-hardware-tests.md` -> 1; `python scripts/docs_audit.py score` prima e dopo: nessun nuovo `!` (collegamento rotto o file orfano; il file nuovo e' collegato da CONSISTENCY.md). `git diff --stat`: 1 file nuovo e CONSISTENCY.md +2.

## Criterio di fine
Passo 3 con 13 righe e docs_audit senza nuovi `!`. Commit unico sul branch `claude/beta-hardware-card`, messaggio `docs: beta hardware tests card (13 checks only the user can run)`. Push solo di questo branch.

## Consegna
Dal worktree di sync di AGY (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-39 --repo C:\Users\Utente\Documents\GitHub\bm33-agy39 --branch claude/beta-hardware-card --commit <sha del commit> --tokens <N> --min <M> --state FREE` (token non noti: scrivi quelli che sai e dillo in chat; riporta in chat la riga `done WO AGY-39 ...`).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-39 @ claude/beta-hardware-card@<sha> · verifica: grep -c righe -> 13; docs_audit.py score senza nuovi ! · 2 file · non verificato su hardware (nessuna prova e' stata eseguita) · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-39 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-light. Stima: 40 minuti.

## Decisioni
- La scheda e' in inglese (documenti di `docs/spec/`); i WO restano in italiano.
- Non si cambia nessuna `status` del toml qui: la scheda dice cosa scrivere dopo la prova dell'utente.

## Prove per l'utente
Questa e' la scheda stessa: ogni riga e' una prova da fare sulla console o sul Pi; nessuna e' stata eseguita.
