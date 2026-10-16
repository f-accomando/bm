# AGY-19 - IT2EN-V3D: le voci 5-7 di "To do, in order" di `docs/progress/v3d.md` passano dall'italiano all'inglese
Assegnato ad AGY (16 ott), indipendente dagli altri WO. I file di `docs/progress/` sono in inglese (regola del repo); queste tre voci (M44 QPU, R40, R42) sono rimaste in italiano. È solo una traduzione: il significato, i numeri e i nomi non cambiano.
Base: `origin/bm-core` (`git fetch origin`, poi, nel worktree di AGY sotto `C:\Users\Utente\Documents\GitHub\` che NON sia bm33, bm33-sync, bm33-int, bm33-int2: `git worktree add -b claude/docs-en-v3d-qpu C:\Users\Utente\Documents\GitHub\bm33-agy19 origin/bm-core`); branch `claude/docs-en-v3d-qpu`. Mai push su bm-core; push solo di questo branch.
Perimetro: `docs/progress/v3d.md`, SOLO le righe dalla voce `5. **QPU come coprocessore …` alla fine della voce 7 (oggi righe 95-111: 17 righe).
NON toccare: nessun altro file, e in questo file nessuna riga fuori dall'intervallo (le voci 1-4 sopra sono già inglesi). Non cambiare numeri, `code span` (testo tra apici inversi), nomi di registri e funzioni, né il numero di righe (una riga italiana diventa UNA riga inglese: niente a-capo nuovi, niente righe unite).
Vincoli: solo documentazione, nessun WSL, nessun hardware (`not verified on hardware`). I numeri di riga sono stati aperti dal planner su `origin/bm-core` @ `e6d7bfd7` ma possono essere spostati: PRIMA di modificare ritrova i due estremi con `git grep -n "QPU come coprocessore" -- docs/progress/v3d.md` (una riga, oggi la 95) e `git grep -n "Adattamento dinamico impostor" -- docs/progress/v3d.md` (una riga, oggi la 110; la sua voce occupa anche la riga seguente, oggi 111, che è l'ultima del blocco). NON fidarti dei numeri; se i due estremi non sono una riga ciascuno: `BLOCKED`. Dopo la riga 111 ci sono solo righe vuote o la fine del file: se trovi altro testo dopo la voce 7, il blocco finisce comunque alla fine della voce 7 (la sua riga `   - Modelli con scheletro …`).
Obiettivo: nessuna parola italiana nel blocco; `docs_audit.py score` senza penalità nuove.

## Stato reale (verificato dal planner sul testo di `e6d7bfd7`)
Le 17 righe 95-111 sono TUTTE italiane (nessuna da lasciare com'è). Contengono tre punti delicati da non rompere: la riga 96 (voce 5, testo sotto il titolo, rientro di 3 spazi, senza `- `), le righe 103-105 (rientro di 5 spazi: sottovoci del "Toolkit") e la riga 106 (la riga `Criterio` è una sottovoce della voce 5, rientro di 3 spazi, non della voce 6).
Fine riga: `git ls-files --eol docs/progress/v3d.md` mostra `w/lf` o `w/crlf`: tieni quello che il file ha già. Se `git diff --stat` dice più di 17 righe cambiate, hai cambiato i fine riga: `git checkout -- docs/progress/v3d.md` e rifai.

## AGY-19a - misura PRIMA (bm-mechanic, 2 min)
Dalla radice del worktree: `python scripts/docs_audit.py score`; segna la prima riga (`score N  (always loaded … + penalties P)`) e le eventuali righe `  !`. Sulla base il planner ha visto `penalties 0`.

## AGY-19b - la traduzione (bm-light, 20 min)
Sostituisci le 17 righe con ESATTAMENTE queste 17 righe (i rientri sono quelli mostrati: 0, 3 o 5 spazi):
```
5. **QPUs as a coprocessor: programs on the QPUs (M44)**:
   The 12 QPUs run SIMD operations on 16 floats at a time: parallel computation on large masses of data, when 3D does not saturate them (2D games, menus) or by reserving some QPUs in 3D games.
   - **Step 1 (Driver)**: Launch "user" QPU programs outside the draw loop (V3D requests: code pointer, uniform registers, QPU number, completion counter; currently `v3d.c` assigns the whole VPM to vertices); dispatch queue, wait with timeout, detection at startup that disables the feature on anomalies and falls back to ARM; execution in the emulator `tests/gpu/v3d_emu.c`, a dedicated step in test `g` and a profile in the 3D Bench.
   - **Step 2 (Particles on GPU)**: `particles()` (position, velocity, gravity, life, color) updated and drawn via GPU with no ARM load (thousands of particles instead of hundreds), integrated with the `lib.particles` module in bmlib.
   - **Step 3 (Full-screen 2D effects)**: Darkness levels and Yharnam glows (today `g16_fade_*` via ARM), fades, blurs, CRT/scanline filters.
   - **Step 4 (Block audio effects)**: FDN reverb, echo and filters processed in blocks on the QPUs (also for nano8).
   - **Step 5 (Ray tracing)**: batched `hit3d` and lines of sight for AI bots.
   - *Toolkit and hardware registers needed (from research 2026-10-12)*:
     - User launch registers: `SRQPC/SRQUA/SRQUL/SRQCS` (0x430..0x43C): write QPU UL, UA, then PC; termination at the count (bits 23:16 of `SRQCS`); requires `VPMBASE > 0` (today 0 in `v3d.c`); reset from mailbox `0x30012` for stuck QPUs; reserve `SQRSV0/1`.
     - Branches in `qpuasm.py` and in the emulator (3 delay slots): per-pixel predicates, branches only on uniform data; VPM DMA with mutex, TMU sampling via memory (`t0s` + `ldtmu0`), semaphores `sacq/srel`.
     - `qpuasm.py` checks: handling of `ra39/rb39` reads, small immediate constraints with `ldtmu`/`thrsw`. Hardware counters `PCTR*` (0x670) for fine profiling.
   - *Completion criterion*: particles and Yharnam's darkness levels handled entirely by the GPU with measurements in the 3D Bench and an identical software fallback in QEMU (`gpu3d=0`).
6. **Runtime profiling for game engines (R40)**:
   - Integration of two new `stat()` metrics in the overlay and the dev kit: audio processing time and GPU wait time.
   - Lets the dynamic governor of Overbit and Yharnam decide at runtime whether the bottleneck is the CPU/ARM bus or the GPU.
7. **Dynamic impostor adaptation for crowds and heroes (R42)**:
   - Skinned models simplified to billboard quads with `r3d_impostor` when far from the camera; Overbit/Yharnam quality governor to enable `gpu3d(nil, nil, nil, nil, 8)` at low levels.
```
Se prima di sostituire noti che una riga attuale ha un'informazione che il testo sopra non rende (planner sbagliato), NON inventare: segui l'originale, scrivilo nella consegna.

## AGY-19c - verifica
1. Parole italiane rimaste: `git grep -nE "\b(il|lo|gli|dei|dal|nel|nei|alla|sul|sui|sulle|dalla|anche|quando|dopo|ogni|tutti|oppure|quindi|invece|dove|sono|che|della|delle|degli|nella|nelle|tramite|senza|Fase|Passo|Criterio|Esecuzione|Misura|Profilazione|Adattamento)\b" -- docs/progress/v3d.md`. Atteso: NESSUNA riga (oggi ne dà 13, tutte nelle righe 95-111: 95-101, 106-111). Il controllo non vede tutte le parole italiane: fa fede anche che le 17 righe siano IDENTICHE al testo del passo AGY-19b.
2. `python scripts/docs_audit.py score`: stesso `penalties P` di prima e nessuna riga `  !` nuova (`score N` cambia di poco: normale).
3. `git diff --stat`: 1 file, 17 inserimenti e 17 cancellazioni (`docs/progress/v3d.md | 34 +++++++++++++++++-----------------`). `git status --short`: solo quel file.

Criterio di fine: punti 1-3 verificati; il blocco ha ancora 17 righe; nessun'altra riga del file cambiata.
Commit: un solo commit sul branch `claude/docs-en-v3d-qpu`, messaggio `docs: translate the QPU, profiling and impostor items of v3d.md to English`. Push solo di questo branch (`git push -u origin claude/docs-en-v3d-qpu`).

## Consegna
Dal worktree di sync (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-19 --repo C:\Users\Utente\Documents\GitHub\bm33-agy19 --branch claude/docs-en-v3d-qpu --commit <sha del commit> --tokens <N> --min <M> --state FREE` (stampa la riga chat `done WO AGY-19 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-19 @ claude/docs-en-v3d-qpu@<sha> · verifica: git grep parole italiane su v3d.md -> 0 righe; docs_audit.py score -> penalties <P> come prima · 1 file, +17 -17 · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-19 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-light (traduzione; testo già fornito).
Stima: 25 minuti di lavoro; nessuna compilazione.

## Decisioni (proposte del planner, da adottare da SPV)
- D1 "Passo N" diventa `Step N` (non `Phase`, per non confondersi con le `Phase` di graphics-mali.md). Nessun altro file cita "Passo 1-5" di M44 (verificato con `git grep` su docs/scripts/tests/Makefile; solo gli archivi in `docs/archive/` hanno "passo" di altri progetti). Raccomandata: sì.
- D2 "Buio a livelli" tradotto `Darkness levels`, "folle ed eroi" `crowds and heroes`. Raccomandata: sì.

## Prove per l'utente
Nessuna.
