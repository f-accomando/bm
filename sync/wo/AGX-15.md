# AGX-15 - D12-DETAILS: la copertina di un gioco sull'RGB30 mostra autore, tipo, dimensione, percorso e la nota di prestazioni dell'ultimo giro
Assegnato ad AGX (16 ott). Inizia dopo: DELIVERED AGX-13 (stesso file `src/rgb30/ui.c`: perimetri non disgiunti, quindi AGX-13 va prima e questo WO parte dal SUO branch). Nasce dalla divergenza D12 (BETA-6): sul Pi la copertina scrive `autore   tipo   N KiB   percorso` e, dopo un gioco, la nota `last: <file>, X fps, update+draw ...`; sull'RGB30 c'e' solo il suggerimento.
Base: il branch di AGX-13, `origin/claude/rgb30-menu-start-tab` (dopo il suo push; `git fetch origin`, poi `git worktree add -b claude/rgb30-cover-details C:\Users\Utente\Documents\GitHub\bm33-agx15 origin/claude/rgb30-menu-start-tab`). Se AGX-13 non e' ancora consegnato: `BLOCKED: aspetta AGX-13`. Branch `claude/rgb30-cover-details`. Mai push su bm-core; push solo di questo branch.
Perimetro: `src/rgb30/ui.c` (solo `play_bm` e il ramo `shown == TAB_GAMES` del calcolo di `details`), `tests/rgb30/qemu_test.py` (UNA funzione nuova `test_cover_details_perf`, subito dopo `test_bm_cartridge`; se AGX-13/14 o AGY-38 hanno gia' messo la loro dopo `test_bm_cartridge`, mettila dopo l'ultima di esse, mai in mezzo a una), `docs/spec/divergences.toml` (solo il blocco `id = "D12"`).
NON toccare: il resto di `ui.c` (in particolare il ciclo del menu con `tabto`, di AGX-13), le altre funzioni di `tests/rgb30/qemu_test.py` (APP-6, APP-8, AGY-15, API-1, CI, AGY, AGX-13/14, AGY-38), `docs/spec/divergences.toml` fuori da D12 (AGY-21: D19; AGY-31: D17/D24/D27/D39; APP-7: D43; AGX-13: D04; AGY-38: D06/D07; AGX-14: D08/D13), `docs/progress/platforms.md:56`, la nota di D28, `src/kernel/carts.c` (sola lettura), `SYNC.md`, `sync/*`.
Disgiunzione dalle code (letti i perimetri di `sync/wo/*.md`): l'unico WO che tocca `src/rgb30/ui.c` e' AGX-13 (a monte di questo); il resto dei file condivisi lo e' per funzioni/blocchi diversi.
Vincoli: solo QEMU virt, `not verified on hardware`. Il worktree NON e' `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120`; `/tmp` di WSL condiviso (3,9 GB): `df -h /tmp | tail -1; ls -d /tmp/bm64* 2>/dev/null | wc -l` prima e dopo, il secondo torna a 0 (cancella solo cio' che hai creato), `TMPDIR` resta `/tmp` di WSL. I numeri di riga sono stati aperti dal planner su `f5cd71b7` (prima di AGX-13: dopo, le righe di `ui.c` dopo ~1100 si spostano di ~21): ritrova ogni punto con `git grep -n` e non fidarti dei numeri. Cio' che non ritrovi come descritto: `non verificato`, `BLOCKED`, non indovinare.
Obiettivo: `python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k cover_details_perf` stampa `PASS test_cover_details_perf` (exit 0); D12 `untested` con una nota; `python scripts/consistency.py check` con lo stesso output di prima.

## Prove (aperte dal planner su `f5cd71b7`)
- Pi: `ksnprintf(details, ..., "%s   %s   %lu KiB   %s", c->author[0] ? c->author : "-", kind_of(c), (c->size + 1023) / 1024, c->path)` (`src/kernel/carts.c:1260-1262`); la nota di prestazioni `perf_line()` (`carts.c:527-536`: se `st->frames`, `ms = elapsed_us / 1000`, `fps10 = ms ? frames * 10000 / ms : 0`, `avg = cpu_us_total / frames`, formato `", %lu.%lu fps, update+draw %lu.%02lu ms (max %lu.%02lu)"`) e `"last: %s%s"` con il nome del file (`carts.c:690`).
- RGB30: `char details[64] = "", note[96] = "";` (`ui.c:929`); il ramo `shown == TAB_GAMES` (`ui.c:1025-1029`: `frozen ? "%s/%s: frozen, %s resumes it" : "%s/%s: Start+Select freezes it"`); `v.note = on_market ? market_status() : note[0] ? note : NULL;` (`ui.c:1034`); `static char susp_name[48];` (`ui.c:343`); `play_bm(int i)` (`ui.c:373`) con `bm_stats_t st` (`:376`) e `bm_print_stats(&st);` (`:406`, dopo l'etichetta `back:`, quindi vale anche per il ramo "resuming"); `struct game` ha `author[33]`, `dir[8]` ("/bm" o "/carts"), `size`, `is_bm` (`ui.c:182-188`).
- Ritrovare: `git grep -n 'char details\[64\]' -- src/rgb30/ui.c`; `git grep -n 'static char susp_name' -- src/rgb30/ui.c`; `git grep -n 'bm_print_stats' -- src/rgb30/ui.c`; `git grep -n 'v.note = ' -- src/rgb30/ui.c`; `git grep -n 'Start+Select freezes it' -- src/rgb30/ui.c`; `git grep -n 'def test_bm_cartridge' -- tests/rgb30/qemu_test.py`.

## AGX-15a - il codice (bm-light, 25 min)
Passo 0: ambiente. Worktree (riga "Base"), build `scripts/wsl-run.ps1 'make -f rgb30.mk PLAT=virt all > build/rgb30.log 2>&1; echo EXIT=$?' -Tail 120` (prima volta 10-20 minuti: in background). Atteso `EXIT=0`.
Passo 1: `details[64]` diventa `details[128]` (stessa riga di `note[96]`). Nel ramo `shown == TAB_GAMES` il giocatore non congelato scrive
`ksnprintf(details, sizeof details, "%s   %s   %lu KiB   %s/%s", games[*s].author[0] ? games[*s].author : "-", games[*s].is_bm ? "bm" : "b16", (unsigned long)(games[*s].size + 1023) / 1024, games[*s].dir + 1, games[*s].name);`
mentre il testo del gioco congelato resta com'e' (`"%s/%s: frozen, %s resumes it"`); scrivi i due casi con un `if (frozen) ... else ...` al posto dell'operatore `?:` sul formato (il formato non puo' piu' scegliere fra due liste di argomenti diverse).
Passo 2: accanto a `static char susp_name[48];` aggiungi `static char last_perf[96];                         /* "last: <file>, X fps, update+draw ..." after a game, as on the Pi (carts.c:527-536, :690) */`. In `play_bm`, subito dopo `bm_print_stats(&st);`:
```
    last_perf[0] = 0;
    if (st.frames) {
        uint32_t ms = st.elapsed_us / 1000, fps10 = ms ? st.frames * 10000u / ms : 0;
        uint32_t avg = st.cpu_us_total / st.frames;
        ksnprintf(last_perf, sizeof last_perf, "last: %s, %lu.%lu fps, update+draw %lu.%02lu ms (max %lu.%02lu)",
                  games[i].name, fps10 / 10, fps10 % 10, avg / 1000, avg % 1000 / 10,
                  st.cpu_us_max / 1000, st.cpu_us_max % 1000 / 10);
    }
```
(stessa formula di `carts.c:531-536`; i tipi `uint32_t`/`%lu` come nel Pi.) Poi `v.note = on_market ? market_status() : note[0] ? note : last_perf[0] ? last_perf : NULL;`. Note: con un nome file di 55 caratteri la riga supera 96 e `ksnprintf` la taglia senza scrivere fuori (non verificato che `ksnprintf` tagli: leggi `src/lib/printf.c`); lo stesso taglio vale per lo schermo stretto: non e' un difetto di questo WO.
Passo 3: il test, in `tests/rgb30/qemu_test.py`, `def test_cover_details_perf(b, opts):` con docstring di una riga. SD: `make_sd(tmp, {"bm/picart.bm": mkbm.pack(PI_CART.encode(), title="Pi cart", author="tests", res=(360, 360))})`; `q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)`; `try/finally: q.close()`. Passi: `boot(q)`; `time.sleep(0.6)`; `text = screen_all(q.screendump())`; `assert "tests" in text and "KiB" in text, text`; `q.send("\r")`; `q.expect("play: /bm/picart.bm", timeout=10)`; `q.expect("bm: loaded", timeout=30)`; `time.sleep(2)`; `q.send("\t ")`; `q.expect("suspended", timeout=10)`; `time.sleep(1)`; `text = screen_all(q.screendump())`; `assert "last: picart.bm" in text, text`; `assert "fps" in text, text`. Se compare `last: picart.bm` ma non `fps` (lo schermo a 360 px potrebbe tagliare la nota nel carattere doppio): `BLOCKED` con il testo dello schermo, non cambiare la riga in modo da nascondere il taglio.
Passo 4: `docs/spec/divergences.toml`, blocco D12 (`git grep -n 'id = "D12"'`, oggi riga 139): `status = "open"` diventa `status = "untested"` e sotto la riga `status` una riga `note = "src/rgb30/ui.c: the cover of a game shows author, kind, size and path, and the note 'last: <file>, X fps, update+draw ...' after a game (QEMU test_cover_details_perf). Not tried on an RGB30."`. Le altre righe del blocco restano.
Passo 5: ricompila; `scripts/wsl-run.ps1 'python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k cover_details_perf; echo EXIT=$?' -Tail 120`. Atteso `PASS test_cover_details_perf`, EXIT=0. Prova rossa: sul codice di AGX-13 (prima del passo 1) lo stesso test deve fallire sul primo `assert` (`KiB` assente); annotalo (basta `git stash` dei soli passi 1-2 o la build prima di modificare). Poi `python scripts/consistency.py check` prima/dopo: stesso output. Poi `/tmp` (df e `ls -d /tmp/bm64*`).

## Criterio di fine
Passo 5 verde (e rosso prima del codice); `git diff --stat` rispetto al branch di AGX-13: 3 file (ui.c ~ +20/-5, qemu_test.py ~ +25, divergences.toml +2/-1). Commit unico sul branch `claude/rgb30-cover-details`, messaggio `rgb30 menu: cover details and the last run's performance note (D12)`. Push solo di questo branch.

## Consegna
Dal worktree di sync di AGX (`bm33-agx-sync`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGX --close AGX-15 --repo C:\Users\Utente\Documents\GitHub\bm33-agx15 --branch claude/rgb30-cover-details --commit <sha del commit> --tokens <N> --min <M> --state FREE` (token non noti: scrivi quelli che sai e dillo in chat; riporta in chat la riga `done WO AGX-15 ...`).
2. In `## Consegne` di `sync/AGX.md` la riga standard `DELIVERED AGX-15 @ claude/rgb30-cover-details@<sha> · verifica: python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k cover_details_perf -> PASS test_cover_details_perf (prima del fix: assert KiB) · 3 file · base = claude/rgb30-menu-start-tab · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGX --state FREE --task "AGX-15 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-light (il codice) con il test scritto dallo stesso operatore. Stima: 45 minuti.

## Decisioni
- D-2 (adottata): il suggerimento "Start+Select freezes it" sparisce dalla copertina di un gioco non congelato (l'RGB30 si allinea al Pi); il testo del gioco congelato non cambia. Il suggerimento resta nelle opzioni/nella documentazione.

## Prove per l'utente
Sull'RGB30: la copertina di un gioco scrive autore, tipo, KiB e percorso; dopo aver giocato (o congelato) un gioco la barra dice `last: <file>, X fps, ...`. Non verificato su hardware.
