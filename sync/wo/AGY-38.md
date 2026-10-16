# AGY-38 - FREEZE-FLOW: un test QEMU per congelare, cambiare gioco e chiudere sull'RGB30 (D06, D07)
Assegnato ad AGY (16 ott), indipendente da AGX-13 e AGX-14 (funzioni di test diverse). Nasce da BETA-6: D06 (domanda "Close <gioco>?") e D07 (badge, riga "Close the game") sono `untested` e nessun test li percorre.
Base: `origin/claude/integrazione-ottobre` @ `f5cd71b7` (`git fetch origin`, poi `git worktree add -b claude/rgb30-qemu-freeze-flow C:\Users\Utente\Documents\GitHub\bm33-agy38 origin/claude/integrazione-ottobre`); branch `claude/rgb30-qemu-freeze-flow`. Mai push su bm-core; push solo di questo branch.
Perimetro: `tests/rgb30/qemu_test.py` (UNA funzione nuova `test_freeze_switch_close`, subito dopo `test_bm_cartridge`) e `docs/spec/divergences.toml` (solo la riga `note =` dei blocchi D06 e D07).
NON toccare: `src/**` (il kernel non cambia), le altre funzioni di `tests/rgb30/qemu_test.py` (APP-6, APP-8, AGY-15, API-1, CI, AGY e AGX-13/14/15 ne toccano altre), `docs/spec/divergences.toml` fuori da D06/D07 (AGY-21: D19; AGY-31: D17/D24/D27/D39; APP-7: D43; AGX-13: D04; AGX-14: D08/D13; AGX-15: D12), `docs/progress/platforms.md:56`, la nota di D28, `SYNC.md`, `sync/*`.
Disgiunzione dalle code (letti i perimetri di `sync/wo/*.md`): nessun WO in coda tocca `D06`/`D07` ne' la funzione `test_freeze_switch_close`; i file condivisi lo sono per funzioni/blocchi diversi.
Vincoli: solo QEMU virt, `not verified on hardware`. Il worktree NON e' `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120`; `/tmp` di WSL condiviso (3,9 GB): `df -h /tmp | tail -1; ls -d /tmp/bm64* 2>/dev/null | wc -l` prima e dopo, il secondo numero torna a 0 (cancella solo cio' che hai creato), `TMPDIR` resta `/tmp` di WSL. Ritrova ogni riga con `git grep -n` e non fidarti dei numeri; cio' che non ritrovi come descritto: `non verificato` e `BLOCKED`.
Obiettivo: `python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k freeze_switch_close` stampa `PASS test_freeze_switch_close` (exit 0).

## Prove (aperte dal planner su `f5cd71b7`)
- Start+Select congela il gioco: `src/rgb30/bm_input.c:96-98` (`(held & leave) == leave` con `leave = HID_START | HID_SELECT`); la seriale manda Select come `'\t'` e Start come `' '` (`src/rgb30/pad.c:65-66`), tenuti 120 ms ciascuno: un unico `q.send("\t ")` li tiene insieme. Il tasto `q` del test esistente congela anch'esso (commento di `test_bm_cartridge`): qui si usa `"\t "`, il percorso vero.
- Domanda "Close %s?" in `src/rgb30/ui.c:361` (dettaglio `:362`, bottone "Close it" `:363`); il dettaglio della copertina dice `frozen` (`ui.c:1028`, testo `"%s/%s: frozen, %s resumes it"`); la riga "Close the game" `ui.c:766`; `resuming %s` `ui.c:379`; il titolo nella domanda e' quello della cartuccia (`bm_suspended(t, ...)`, `:360`).
- Il tasto `\x7f` e' `pad_back` (A) e `\r` e' `pad_ok` (`src/rgb30/pad.c:57-58`); `x` apre le opzioni del gioco (`PAD_X`, `ui.c:1289`); `d` e' Destra (cambia copertina).
- Ritrovare: `git grep -n 'Close %s?' -- src/rgb30/ui.c`; `git grep -n 'Close the game' -- src/rgb30/ui.c`; `git grep -n 'resuming %s' -- src/rgb30/ui.c`; `git grep -n 'def test_bm_cartridge\|^def test_bench3d' -- tests/rgb30/qemu_test.py`; `git grep -n 'def screen_all\|def in_menu\|^def keys' -- tests/rgb30/qemu_test.py`.

## AGY-38a - il test (bm-coder, 30 min)
Passo 0: ambiente. Worktree (riga "Base"), build `scripts/wsl-run.ps1 'make -f rgb30.mk PLAT=virt all > build/rgb30.log 2>&1; echo EXIT=$?' -Tail 120` (prima volta 10-20 minuti: in background). Atteso `EXIT=0`.
Passo 1: in `tests/rgb30/qemu_test.py`, subito DOPO `test_bm_cartridge` (due righe vuote prima e dopo) scrivi `def test_freeze_switch_close(b, opts):` con docstring di una riga ("Start+Select freezes; the options of a frozen game; starting another asks 'Close <game>?'; resume; close and switch"). SD: `make_sd(tmp, {"bm/a.bm": mkbm.pack(PI_CART.encode(), title="A cart", author="tests", res=(360, 360)), "bm/b.bm": mkbm.pack(PI_CART.encode(), title="B cart", author="tests", res=(360, 360))})`; `q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)`; tutto in `try/finally: q.close()`. Per leggere lo schermo: `def look(): time.sleep(0.6); return screen_all(q.screendump())` (funzione interna). Giochi in ordine alfabetico: all'inizio la copertina scelta e' `a.bm`. Passi, nell'ordine:
1. `boot(q)`; `time.sleep(0.4)`; `q.send("\r")`; `q.expect("play: /bm/a.bm", timeout=10)`; `q.expect("bm: loaded", timeout=30)`; `time.sleep(2)`.
2. `q.send("\t ")` (Select+Start); `q.expect("suspended", timeout=10)`; `time.sleep(1)`; `assert in_menu(look())` e dopo `assert "frozen" in look()` (il dettaglio della copertina di A).
3. `q.send("x")`: `text = look()`; `assert "Close the game" in text and "Resume" in text, text`; poi `q.send("\x7f")` (torna alla copertina); `assert "Close the game" not in look()`.
4. `q.send("d")` (copertina B); `q.send("\r")`: `text = look()`; `assert "Close A cart?" in text, text`; `q.send("\x7f")`: `assert "Close A cart?" not in look()`.
5. `q.send("a")` (copertina A); `q.send("\r")`; `q.expect("resuming a.bm", timeout=10)`; `time.sleep(2)`; `q.send("\t ")`; `q.expect("suspended", timeout=10)`; `time.sleep(1)`.
6. `q.send("d")`; `q.send("\r")` (la domanda); `q.send("\r")` (risposta "Close it"); `q.expect("play: /bm/b.bm", timeout=10)`.
Ogni `assert` porta come messaggio il testo dello schermo. Se una `expect`/`assert` fallisce non allentare il test: termina `FAILED` allegando il testo dello schermo (puo' essere un difetto vero, torna a SPV). Un `time.sleep` piu' lungo e' ammesso, un'asserzione tolta no.
Passo 2: `scripts/wsl-run.ps1 'python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k freeze_switch_close; echo EXIT=$?' -Tail 120`. Atteso `PASS test_freeze_switch_close`, EXIT=0. Poi controlla `/tmp` (df e `ls -d /tmp/bm64*`).
Nota: prova rossa sul codice vecchio non applicabile (il test non cambia il kernel); `non verificato` che il test fallisca se D06/D07 si rompessero: per una prova di mutazione facoltativa commenta (in una copia, mai committata) la riga `ASK_SWITCH` e rilancia: deve fallire al punto 4.

## AGY-38b - note del toml (bm-mechanic, 5 min)
`docs/spec/divergences.toml`, blocchi D06 (`git grep -n 'id = "D06"'`, oggi riga 69) e D07 (oggi 81): alla fine della stringa `note =` aggiungi ` QEMU: test_freeze_switch_close.` (uno spazio, prima del `"` finale). `status` resta `untested`. Verifica: `python scripts/consistency.py check` stesso output di prima.

## Criterio di fine
Passo 2 verde; `git diff --stat`: 2 file (qemu_test.py ~ +40, divergences.toml 2 righe). Commit unico sul branch `claude/rgb30-qemu-freeze-flow`, messaggio `rgb30 qemu: test the freeze, switch and close flow (D06, D07)`. Push solo di questo branch.

## Consegna
Dal worktree di sync di AGY (`bm33-sync-agy`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGY --close AGY-38 --repo C:\Users\Utente\Documents\GitHub\bm33-agy38 --branch claude/rgb30-qemu-freeze-flow --commit <sha del commit> --tokens <N> --min <M> --state FREE` (token non noti: scrivi quelli che sai e dillo in chat; riporta in chat la riga `done WO AGY-38 ...` che il comando stampa).
2. In `## Consegne` di `sync/AGY.md` la riga standard `DELIVERED AGY-38 @ claude/rgb30-qemu-freeze-flow@<sha> · verifica: python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k freeze_switch_close -> PASS test_freeze_switch_close · 2 file · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGY --state FREE --task "AGY-38 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (testo esatto). Stima: 40 minuti.

## Decisioni
Nessuna. D06 e D07 restano `untested` (la prova su console vera e' dell'utente: vedi AGY-39).

## Prove per l'utente
Sull'RGB30, con il solo pad: congelare un gioco (Start+Select), aprire le sue opzioni (X: "Resume" e "Close the game"), avviarne un altro (domanda "Close <titolo>?"). Nessun test automatico lo prova su hardware.
