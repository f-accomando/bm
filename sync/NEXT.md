# Next session (written by SPV, 2026-10-15)

Single writer: SPV. Read this first, then `SYNC.md` Log (last rows) and `python tools/sync_check.py`.

## Indice

Alla ripartenza leggi: sezione 9 (e 8). Le sezioni 1-7 sono nell'archivio `sync/archive/NEXT-2026-10-16.md` (verbatim, incluse le note "supersedes" iniziali).
- 1 Stato a fine giornata 15 Oct: archivio. `grep -n -A 20 '^## 1\.' sync/archive/NEXT-2026-10-16.md`
- 2 Lavoro non finito: archivio. `grep -n -A 20 '^## 2\.' sync/archive/NEXT-2026-10-16.md`
- 3 Lavoro delle 8-10 ore verso la Beta: archivio. `grep -n -A 20 '^## 3\.' sync/archive/NEXT-2026-10-16.md`
- 4 Housekeeping: archivio. `grep -n -A 20 '^## 4\.' sync/archive/NEXT-2026-10-16.md`
- 5 Aggiornamento 18:15: archivio. `grep -n -A 20 '^## 5\.' sync/archive/NEXT-2026-10-16.md`
- 6 Aggiornamento 21:50: archivio. `grep -n -A 20 '^## 6\.' sync/archive/NEXT-2026-10-16.md`
- 7 Aggiornamento 16 Oct mattina: archivio. `grep -n -A 20 '^## 7\.' sync/archive/NEXT-2026-10-16.md`
- 8 Aggiornamento 16 Oct sera: in questo file (sotto)
- 9 Decisioni utente 17 Oct (divergenze, BETA-6): in questo file (sotto)

## 8. Update 16 Oct (evening, supersedes sections 6-7 where they differ)
Session opens when the user says "buongiorno". The user runs the compact prompt, then logs off; tomorrow restarts with "buongiorno".

**Operators at the close (all FREE, SIGNOFF sent):** NET, B16, AGY, AGX. CI idle (stale 550 min).

**Reviews to do first thing (Haiku for docs and host Python, Sonnet for C/Lua/QEMU):** AGY-29 fix (313d1b41), AGY-38 (726e4718, QEMU), AGY-39 (4123405b), AGY-40 (f15ff570); AGX-3, 5, 6 (not reviewed today, user decision; AGX-4 done). Corrections owed: APP-8 (text only), R-NET7a (test_monitor_crc flaky: find why two `c` presses give different screens; screendump or short sleep).

**WO drafts for the day (`sync/wo/drafts/`, not assigned):** R-NET10 (bridge reads the password from a file outside the repo, mode 0600, user writes it; the user already created `~/.pi_bridge.pw`), QEMU-FLAKE (ConnectionRefused in `Qemu.__init__` + stages_build banner timeout; after AGX-3), R-NET8b, AGX-14 test with `keys(pause=0.3)` (`tests/rgb30/qemu_test.py:592-594`), AGY-26 note: `AI.md:3` points to untracked `AI-IT.md`, D12 note cites `menu_ui.c:1465` (the condition is at :1463), `ZOOM_STEPS` undefined in `carts/editor/main.lua:1799` (`make test-studio` EXIT=2, pre-existing). Renumber D43/D44 -> D45 at integration.

**User proofs (all moved to tomorrow):** `:crc` (R-NET7), `pi_soak` (R-NET8), long watchdog, budget API-1, Stages 2D template, APP-6 pad only, APP-5b on screen, BETA-HWCARD (AGY-39 card). Done today with the user's OK: R-NET9 on the Pi (watchdog on 3000 ms, voluntary freeze reboots after ~10 s, normal kernel restored).

**Standing user decisions (16 Oct):** permanent OK for upload/crash/reboot/pi_soak on Pi 1 (`set`/`save` stay refused by the bridge); password rule "never on disk" relaxed for the bridge file only (R-NET10); Haiku medium for reviews of docs and host Python (`bm-reviewer` on `claude/bm-reviewer-agent@7e7427f8`, not merged).

**Merges still waiting for the user's go:** PR 25 (CI red: test_chainloader fixed by R-NET9 but not in the PR yet; flaky meshy2mesh/editor_mouse), approved branches (see Log), `claude/bm-reviewer-agent`.

**Warning:** reviewers deleted `bm*` dirs in WSL `/tmp` (not only theirs). Tell reviewers to remove only dirs they created.

## 9. Decisioni utente 17 Oct (divergenze, BETA-6)
- D03: accepted. RGB30 un giocatore locale; eccezione futura: schermo condiviso/split screen se l'RGB30 e' collegato via HDMI a una TV.
- D05: RGB30 senza mouse, ma lo stick fa da puntatore in app/feature specifiche. Tenere il codice mouse; SPV verifica se coincide con la funzione dello stick (ricerca lanciata, esito da riportare).
- D09: non chiusa. Da discutere con l'utente: cosa e' gioco/app/utility, cosa sta nel kernel e cosa nel market; distinzione .b16 / .bm / .bme e cartelle.
- D10: non chiusa. Definizione di sistema: fiber su ARM6 (Pi Zero), multitask vero su ARM7/8 multicore; quando entra in gioco l'uno e quando l'altro (vedi progress/multitasking.md).
- D11: Send si' (condividere giochi e app con amici), Publish spento per ora (da considerare in futuro); l'RGB30 non e' per sviluppo, il .b16 dell'SDK e' solo per test.
- Seguito: WO per divergences.toml (D03 accepted, D05, D11) dopo la verifica D05; D09 e D10 = sessioni di discussione/progetto.
- D05 verifica (17 Oct, git grep su integrazione-ottobre): il puntatore del kernel (`src/kernel/pointer.h`, M32) e' solo per un mouse vero (USB, BT classic, BLE), mai da un controller (decisione utente 2026-10-05: stick e R2 lo facevano apparire come un mouse). Lo stick-cursore esiste solo nelle cartucce (nano8 `40_vm.lua:232`: il cursore segue lo stick sinistro; overbit). Sull'RGB30 il mouse BT funziona (`bm_input.c:90`, `main.c:253`), la porta USB no (`bm_port.c:185`). Quindi sono due codici distinti: tenere il puntatore del kernel; se l'utente vuole lo stick come puntatore nel menu o negli strumenti e' una funzione nuova che ribalta la decisione del 5 ott.
- R2 (17 Oct): approvati tutti gli sviluppi 3D programmati e consigliati per BETA-5 (piano W01..W17, H1..H8 nel Log del 17 ott). M44 (QPU) rientra; audio QPU e hit3d fuori. Mali fasi 2-4: si scrive senza aspettare H1. Assegnazione dei WO: decide SPV.
- D1 (17 Oct): budget 3D. RGB30 360x360 (opzionale 720x720); Pi 640x360 (test opzionali a 720p e 1080p). Frame rate: Pi 60 fps; RGB30 30 fps finche' H6 non da numeri.
- D2 (17 Oct): risoluzioni diverse (Pi 640x360, RGB30 360x360) -> una sola cartuccia non basta: SPV decide logica di gioco condivisa (moduli Lua comuni) + front per edizione (vista/HUD/risoluzione), .bm per il Pi e .b16 per l'RGB30, in due tempi (prima il Pi).
