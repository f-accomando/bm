# AGY-40 - V3D-CLOSEOUT: una riga in `v3d.md` che chiude M35-M37 sul codice e rimanda alla scheda delle prove sul Pi
Assegnato ad AGY (16 ott). Inizia dopo: DELIVERED AGY-39 (la riga cita `docs/spec/beta-hardware-tests.md`, creato da AGY-39; senza il file il collegamento sarebbe rotto). Nasce da BETA-4: il codice di M35-M37 e' fatto (bm3d 3.0-4.9 e 6.5-6.8 in `docs/DRIVERS.md`); resta solo la prova sul Pi.
Base: il branch di AGY-39, `origin/claude/beta-hardware-card` (dopo il suo push; `git fetch origin`, poi `git worktree add -b claude/v3d-closeout C:\Users\Utente\Documents\GitHub\bm33-agy40 origin/claude/beta-hardware-card`). Se AGY-39 non e' ancora consegnato: `BLOCKED: aspetta AGY-39`. Branch `claude/v3d-closeout`. Mai push su bm-core; push solo di questo branch.
Perimetro: `docs/progress/v3d.md`, UNA riga nuova subito sotto `## To do, in order` (oggi riga 76; la riga nuova va dopo la riga vuota 77, prima della voce `1.` di riga 78).
NON toccare: le righe 95-111 di `docs/progress/v3d.md` (voci 5-7, di AGY-19), le altre righe di quel file, ogni altro file, `docs/spec/beta-hardware-tests.md` (di AGY-39), `docs/progress/platforms.md:56`, la nota di D28, `SYNC.md`, `sync/*`.
Disgiunzione dalle code (letti i perimetri di `sync/wo/*.md`): `docs/progress/v3d.md` e' toccato solo da AGY-19 (righe 95-111, voci 5-7): la riga nuova sta nelle righe 76-78, quindi non si sovrappone; se AGY-19 e' gia' integrato nel branch di base le righe dopo la 94 si spostano, ma questo WO non le tocca.
Vincoli: solo documento, nessuna prova su hardware (`non verificato`). Il worktree NON e' `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. Nessuna build. Ritrova il punto con `git grep -n '^## To do, in order' -- docs/progress/v3d.md` (UNA riga) e non fidarti del numero; se non la ritrovi: `BLOCKED`.
Obiettivo: `v3d.md` ha, sotto `## To do, in order`, la voce `0.` col testo esatto sotto; `python scripts/docs_audit.py score` senza collegamenti rotti.

## Prove (aperte dal planner su `f5cd71b7`)
- `docs/progress/v3d.md:76` e' `## To do, in order`, la 77 e' vuota, la 78 apre la voce `1. Pixels at 1080p (R41)`.
- `docs/DRIVERS.md:233-236`: la 6.6 dice che nel 3D Bench GPU+VS fa le sfere 4×, gli eroi 11×, la scena `mix` 9×; le voci 3.0-3.4 (M36), 4.0-4.9 (M35/M36/M37) e 6.5-6.8 (M36) esistono (`git grep -n '^- \*\*[346]\.' -- docs/DRIVERS.md`).
- Le prove rimaste: `docs/ROADMAP.md:65-67` (M36: anello degli eroi di Overbit e stress test a 60 fps; M35: coda con opzione accesa/spenta; M37: passo 16 del test `g`, `menu_scale=3`).

## AGY-40a - la riga (bm-mechanic, 10 min)
Passo 1: apri `docs/progress/v3d.md`, trova `## To do, in order`. Subito dopo la riga vuota che segue il titolo, PRIMA della voce `1.`, inserisci ESATTAMENTE questa riga (una sola riga, inglese, senza andare a capo; la riga seguente e' la voce `1.` com'era):
```
0. M35-M37 close-out (BETA-4): code done (bm3d 3.0-4.9, 6.5-6.8); the Pi's 3D Bench already gives GPU+VS spheres 4x, heroes 11x, mix 9x (DRIVERS.md 6.6). Left, on the Pi only: Stress test GPU+VS rows, Overbit hero ring at 60 fps (M36); queue on/off (M35); test g step 16 and menu_scale=3 (M37): [beta-hardware-tests](../spec/beta-hardware-tests.md).
```
Passo 2 (verifica): `git diff --stat` -> `docs/progress/v3d.md | 1 +` (un file, una riga aggiunta, nessuna tolta); `python scripts/docs_audit.py score` prima e dopo: nessun collegamento rotto in piu' e nessun file orfano. Il collegamento `../spec/beta-hardware-tests.md` deve risolvere (`ls docs/spec/beta-hardware-tests.md`).
Fine riga: lascia quello che il file ha gia' (`git ls-files --eol docs/progress/v3d.md`); se `git diff --stat` mostra piu' di una riga hai cambiato i fine riga: `git checkout -- docs/progress/v3d.md` e rifai.

## Criterio di fine
Passo 2 OK. Commit unico sul branch `claude/v3d-closeout`, messaggio `v3d: M35-M37 close-out line and the Pi checks card`. Push solo di questo branch.

## Consegna
Dal worktree di sync di AGY (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-40 --repo C:\Users\Utente\Documents\GitHub\bm33-agy40 --branch claude/v3d-closeout --commit <sha del commit> --tokens <N> --min <M> --state FREE` (token non noti: scrivi quelli che sai e dillo in chat; riporta in chat la riga `done WO AGY-40 ...`).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-40 @ claude/v3d-closeout@<sha> · verifica: docs_audit.py score senza collegamenti rotti; git diff --stat 1 file +1 · base = claude/beta-hardware-card · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-40 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-mechanic (testo esatto). Stima: 10 minuti.

## Decisioni
Nessuna. La riga dice "code done" solo per cio' che `DRIVERS.md` documenta; M35-M37 restano da provare sul Pi (`non verificato`).

## Prove per l'utente
Nessuna oltre a quelle della scheda `docs/spec/beta-hardware-tests.md` (righe M35, M36, M37).
