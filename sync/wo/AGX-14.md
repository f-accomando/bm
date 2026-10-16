# AGX-14 - DELETE-TOOLS: un test QEMU per cancellare un gioco congelato e per i tool tenuti fuori da Games (D08, D13)
Assegnato ad AGX (16 ott), indipendente da AGX-13/AGX-15 (funzione di test diversa; non tocca `ui.c`). Nasce da BETA-6: D08 (cancellare un gioco chiude la sua copia congelata) e D13 (un tool installato resta fuori da Games) sono `untested`.
Base: `origin/claude/integrazione-ottobre` @ `f5cd71b7` (`git fetch origin`, poi `git worktree add -b claude/rgb30-qemu-delete-tools C:\Users\Utente\Documents\GitHub\bm33-agx14 origin/claude/integrazione-ottobre`); branch `claude/rgb30-qemu-delete-tools`. Mai push su bm-core; push solo di questo branch.
Perimetro: `tests/rgb30/qemu_test.py` (UNA funzione nuova `test_delete_frozen_and_tools`, subito dopo `test_bm_cartridge`) e `docs/spec/divergences.toml` (solo la riga `note =` dei blocchi D08 e D13).
NON toccare: `src/**`, le altre funzioni di `tests/rgb30/qemu_test.py` (APP-6, APP-8, AGY-15, API-1, CI, AGY, AGX-13, AGY-38, AGX-15), `docs/spec/divergences.toml` fuori da D08/D13 (AGY-21: D19; AGY-31: D17/D24/D27/D39; APP-7: D43; AGX-13: D04; AGY-38: D06/D07; AGX-15: D12), `docs/progress/platforms.md:56`, `docs/progress/tools.md:85-86` e la nota di D28, `SYNC.md`, `sync/*`.
Disgiunzione dalle code (letti i perimetri di `sync/wo/*.md`): nessun WO in coda tocca D08/D13 ne' `test_delete_frozen_and_tools`; i file condivisi lo sono per funzioni/blocchi diversi. Nota: D13 e' vicino a D28 (WO AGY, gia' integrato): non modificare la `note` di D28.
Vincoli: solo QEMU virt, `not verified on hardware`. Il worktree NON e' `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120`; `/tmp` di WSL condiviso (3,9 GB): `df -h /tmp | tail -1; ls -d /tmp/bm64* 2>/dev/null | wc -l` prima e dopo, il secondo numero torna a 0 (cancella solo cio' che hai creato), `TMPDIR` resta `/tmp` di WSL. Ritrova ogni riga con `git grep -n`; cio' che non ritrovi come descritto: `non verificato` e `BLOCKED`.
Obiettivo: `python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k delete_frozen_and_tools` stampa `PASS test_delete_frozen_and_tools` (exit 0).

## Prove (aperte dal planner su `f5cd71b7`)
- Un tool installato resta fuori da Games: `if (bm_tool_id_of_file(e.name)) continue;` in `scan_games()` (`src/rgb30/ui.c:256`); `studio.bm` e' nell'elenco (`src/kernel/toolnames.h`, voce `{ "studio", "studio" }`, confronto sul nome senza estensione).
- La riga di cancellazione e' l'ULTIMA delle opzioni di un gioco: `home_row(... G_DELETE, "Delete from the SD card" ...)` `ui.c:786`, e la scelta fa il giro: `gopt_sel = (gopt_sel + dy + gpb.n) % gpb.n` `ui.c:1151` (quindi `w` dalla prima riga arriva all'ultima). La domanda e' "Delete the game?" (`ui.c:817`, dettaglio `:818`, bottone "Delete" `:819`) e senza risposta nulla viene cancellato.
- Cancellare un gioco chiude la sua copia congelata: `ui.c:830-833` (`is_suspended(...)` -> `bm_close_suspended(); susp_name[0] = 0;`), prima di `fat_delete` (`:834`).
- Il numero dei giochi e' nel log di avvio: `cartridge menu: %d games` (`ui.c:910`); la copertina scelta all'inizio e' il primo gioco in ordine alfabetico (`A cart` poi `B cart`).
- Congelare: `q.send("\t ")` = Select+Start (`src/rgb30/pad.c:65-66`, `bm_input.c:96-98`), stampa `suspended`.
- Ritrovare: `git grep -n 'bm_tool_id_of_file' -- src/rgb30/ui.c`; `git grep -n 'Delete the game' -- src/rgb30/ui.c`; `git grep -n 'cartridge menu' -- src/rgb30/ui.c`; `git grep -n 'def test_bm_cartridge\|^def test_bench3d' -- tests/rgb30/qemu_test.py`.

## AGX-14a - il test (bm-coder, 25 min)
Passo 0: ambiente. Worktree (riga "Base"), build `scripts/wsl-run.ps1 'make -f rgb30.mk PLAT=virt all > build/rgb30.log 2>&1; echo EXIT=$?' -Tail 120` (prima volta 10-20 minuti: in background). Atteso `EXIT=0`.
Passo 1: in `tests/rgb30/qemu_test.py`, subito DOPO `test_bm_cartridge` (due righe vuote prima e dopo), `def test_delete_frozen_and_tools(b, opts):` con docstring di una riga. SD: `make_sd(tmp, {"bm/a.bm": mkbm.pack(PI_CART.encode(), title="A cart", author="tests", res=(360, 360)), "bm/b.bm": mkbm.pack(PI_CART.encode(), title="B cart", author="tests", res=(360, 360)), "bm/studio.bm": mkbm.pack(PI_CART.encode(), title="Studio tool", author="tests", res=(360, 360))})`; `q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)`; `try/finally: q.close()`. Funzione interna `look()`: `time.sleep(0.6); return screen_all(q.screendump())`. Passi, nell'ordine:
1. `out = boot(q)`; `assert "cartridge menu: 2 games" in out, out` (studio.bm e' un tool: fuori); `assert "Studio tool" not in look()`.
2. `q.send("\r")`; `q.expect("play: /bm/a.bm", timeout=10)`; `q.expect("bm: loaded", timeout=30)`; `time.sleep(2)`; `q.send("\t ")`; `q.expect("suspended", timeout=10)`; `time.sleep(1)`.
3. `q.send("x")`; `q.send("w")` (l'ultima riga, "Delete from the SD card"); `q.send("\r")`: `text = look()`; `assert "Delete the game?" in text, text`; `q.send("\r")` (risposta "Delete"); `time.sleep(1)`; `text = look()`; `assert "A cart" not in text and "a.bm" not in text, text` e `assert in_menu(text)`.
4. `q.send("\r")`: deve avviare il gioco rimasto SENZA domanda: `q.expect("play: /bm/b.bm", timeout=10)` (il gioco congelato e' stato chiuso con il file; se compare "Close A cart?" il test fallisce con il testo dello schermo).
Se un passo non riesce non allentare il test: `FAILED` con il testo dello schermo (puo' essere un difetto vero, torna a SPV). Se `w` non porta all'ultima riga perche' ci sono righe in piu', `non verificato`: `BLOCKED` con lo schermo, senza contare i tasti a occhio.
Passo 2: `scripts/wsl-run.ps1 'python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k delete_frozen_and_tools; echo EXIT=$?' -Tail 120`. Atteso `PASS test_delete_frozen_and_tools`, EXIT=0; poi controlla `/tmp`.

## AGX-14b - note del toml (bm-mechanic, 5 min)
`docs/spec/divergences.toml`, blocchi D08 (`git grep -n 'id = "D08"'`, oggi riga 93) e D13 (oggi 150): alla fine della stringa `note =` aggiungi ` QEMU: test_delete_frozen_and_tools.` (uno spazio prima, davanti al `"` finale). `status` resta `untested`. Verifica: `python scripts/consistency.py check` stesso output di prima.

## Criterio di fine
Passo 2 verde; `git diff --stat`: 2 file (qemu_test.py ~ +30, divergences.toml 2 righe). Commit unico sul branch `claude/rgb30-qemu-delete-tools`, messaggio `rgb30 qemu: test delete of a frozen game and tools out of Games (D08, D13)`. Push solo di questo branch.

## Consegna
Dal worktree di sync di AGX (`bm33-agx-sync`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGX --close AGX-14 --repo C:\Users\Utente\Documents\GitHub\bm33-agx14 --branch claude/rgb30-qemu-delete-tools --commit <sha del commit> --tokens <N> --min <M> --state FREE` (token non noti: scrivi quelli che sai e dillo in chat; riporta in chat la riga `done WO AGX-14 ...`).
2. In `## Consegne` di `sync/AGX.md` la riga standard `DELIVERED AGX-14 @ claude/rgb30-qemu-delete-tools@<sha> · verifica: python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k delete_frozen_and_tools -> PASS test_delete_frozen_and_tools · 2 file · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGX --state FREE --task "AGX-14 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (testo esatto). Stima: 35 minuti.

## Decisioni
Nessuna. D08 e D13 restano `untested`: D13 prova solo che un tool presente in `bm/` resta fuori da Games; l'installazione da Market > Apps e' nella scheda della beta (AGY-39).

## Prove per l'utente
Sull'RGB30: congelare un gioco, cancellarlo dalle opzioni ("Delete from the SD card"): la domanda compare e, dopo, avviare un altro gioco non chiede nulla. Non verificato su hardware.
