# AGX-13 - D04-START: nel menu dell'RGB30 Start conferma e Tab / 1-2-3-5 di una tastiera Bluetooth cambiano scheda
Assegnato ad AGX (16 ott). Sequenza: AGX-13 prima di AGX-15 (stesso file `src/rgb30/ui.c`). Nasce dalla divergenza D04 (BETA-6): sul Pi Start conferma e Tab / 1-5 saltano di scheda, sull'RGB30 il menu legge solo `pad_pressed()`.
Base: `origin/claude/integrazione-ottobre` @ `f5cd71b7` (`git fetch origin`, poi `git worktree add -b claude/rgb30-menu-start-tab C:\Users\Utente\Documents\GitHub\bm33-agx13 origin/claude/integrazione-ottobre`); branch `claude/rgb30-menu-start-tab`. Mai push su bm-core; push solo di questo branch.
Perimetro: `src/rgb30/ui.c` (solo il ciclo del menu, `ui_home()` (ui.c:894), dove c'e' `uint32_t p = pad_pressed();`), `tests/rgb30/qemu_test.py` (UNA funzione nuova `test_menu_start_confirms`, subito dopo `test_bm_cartridge`), `docs/spec/divergences.toml` (solo il blocco `id = "D04"`).
NON toccare: `tests/rgb30/qemu_test.py` oltre alla funzione nuova (APP-6, APP-8, AGY-15, API-1, CI, AGY toccano altre funzioni dello stesso file), `docs/spec/divergences.toml` fuori da D04 (AGY-21: D19; AGY-31: D17/D24/D27/D39; APP-7: D43; R-NET7: un blocco nuovo in fondo), `docs/progress/platforms.md:56` (riga SDK b16), la nota di D28 e la riga 86 di `docs/progress/tools.md` (D28), `src/usb/*`, `src/rgb30/pad.c`, `src/kernel/*`, `SYNC.md`, `sync/*`.
Disgiunzione dalle code (letti i perimetri di `sync/wo/*.md`): nessun WO in coda tocca `src/rgb30/ui.c` se non AGX-15 (dopo questo, sullo stesso branch di base); `qemu_test.py` e `divergences.toml` sono condivisi ma per funzioni/blocchi diversi (vedi sopra).
Vincoli: solo QEMU virt; `not verified on hardware` (la tastiera Bluetooth non e' provata: Tab e 1/2/3/5 passano da `hid_getc()`, che QEMU non alimenta; `non verificato`). Il worktree NON e' `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120` dal tuo worktree; il `/tmp` di WSL (3,9 GB) e' condiviso: `df -h /tmp | tail -1; ls -d /tmp/bm64* 2>/dev/null | wc -l` prima e dopo, il secondo numero torna a 0 (cancella solo cio' che hai creato tu), `TMPDIR` resta il `/tmp` di WSL. I numeri di riga sono stati aperti dal planner su `f5cd71b7` ma possono spostarsi: PRIMA di ogni modifica ri-trova ogni punto con `git grep -n` (comandi sotto) e non fidarti del numero. Cio' che non ritrovi come descritto: `non verificato`, `BLOCKED`, non indovinare.
Obiettivo: `python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k menu_start_confirms` stampa `PASS test_menu_start_confirms` (exit 0), rosso prima del passo 2; D04 diventa `untested` con la sua nota.

## Prove (aperte dal planner su `f5cd71b7`)
- Pi: Start conferma salvo Select premuto (`src/kernel/carts.c:1474`: `(pressed & (HID_A | HID_START)) && !(b & HID_SELECT)`); Tab avanza di una scheda (`:1490-1491`) e 1-5 vanno alle schede (`:1492-1493`); ordine schede del Pi Market, Games, Dev, Lib, Settings (`:191`).
- RGB30: il ciclo legge solo `pad_pressed()` (`src/rgb30/ui.c:1091`); schede `enum { TAB_MARKET, TAB_GAMES, TAB_DEV, TAB_SETTINGS }` (`ui.c:674`, niente Lib); `hid_getc()` esiste (`src/usb/hid.c:408`, ritorna -1 se vuota, dichiarata in `src/usb/hid.h:121`, gia' inclusa da `ui.c`); la seriale manda Start come `' '` e Select come `'\t'` (`src/rgb30/pad.c:65-66`); `pad_ok` e' un `uint32_t` globale (`pad.h:33`), `pad_state()` in `pad.h:41`; il blocco `if (pad_serial_char() == '`')` e' a `ui.c:1100-1103`; la variabile `asking` e `ASK_NONE` a `ui.c:679`, `:927`.
- Ritrovare: `git grep -n 'uint32_t p = pad_pressed();' -- src/rgb30/ui.c` (UNA riga, oggi 1091); `git grep -n "pad_serial_char() == '\`'" -- src/rgb30/ui.c` (una riga, oggi 1100); `git grep -n 'def test_bm_cartridge\|^def test_bench3d' -- tests/rgb30/qemu_test.py` (oggi 529 e 566).

## AGX-13a - il test, prima rosso (bm-coder, 15 min)
Passo 0: ambiente. Crea il worktree (riga "Base"). Build: `scripts/wsl-run.ps1 'make -f rgb30.mk PLAT=virt all > build/rgb30.log 2>&1; echo EXIT=$?' -Tail 120` (la prima volta 10-20 minuti: in background e leggi `build/rgb30.log`). Atteso `EXIT=0` e `build/rgb30-virt/kernel.elf`.
Passo 1: in `tests/rgb30/qemu_test.py`, subito DOPO la fine di `test_bm_cartridge` (prima di `def test_bench3d`), due righe vuote prima e dopo, la funzione:
```
def test_menu_start_confirms(b, opts):
    """Start confirms in the menu, as on the Pi (carts.c:1474): one cartridge,
    Start (space on the serial line) plays it."""
    tmp = tempfile.mkdtemp(prefix="bm64sd-")
    sd = make_sd(tmp, {"bm/picart.bm": mkbm.pack(PI_CART.encode(), title="Pi cart", author="tests",
                                                 res=(360, 360))})
    q = Qemu(os.path.join(b, "kernel.elf"), sd=sd)
    try:
        boot(q)
        time.sleep(0.4)
        q.send(" ")                             # Start
        q.expect("play: /bm/picart.bm", timeout=10)
    finally:
        q.close()
```
(la raccolta dei test e' automatica: `TESTS` prende ogni `test_*`, ~riga 1122; non serve altra riga).
Passo 1b (prova rossa, codice ancora vecchio): ricompila se serve, poi `scripts/wsl-run.ps1 'python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k menu_start_confirms; echo EXIT=$?' -Tail 120`. Atteso: fallisce con `timeout waiting for b'play: /bm/picart.bm'` ed EXIT diverso da 0. Se PASSA gia': `BLOCKED` con l'output.

## AGX-13b - il fix in `src/rgb30/ui.c` (bm-coder, 20 min)
Passo 2: subito DOPO `    uint32_t p = pad_pressed();` (ui.c:~1091) inserisci
```
        /* Start confirms unless Select is held (the Pi, carts.c:1474) */
        if ((p & PAD_START) && !(pad_state() & PAD_SELECT))
            p |= pad_ok;
```
(rientro come le righe vicine: 8 spazi).
Passo 3: subito dopo il blocco `if (pad_serial_char() == '`') { run_page(serial_lua); continue; }` (~1100-1103) inserisci
```
        int tabto = -1;                             /* a Bluetooth keyboard: Tab and 1 2 3 5 jump tabs (carts.c:1490-1493) */
        for (int k; (k = hid_getc()) >= 0;) {
            if (k == '\t')
                tabto = (tab + 1) % (TAB_SETTINGS + 1);
            else if (k == '1')
                tabto = TAB_MARKET;
            else if (k == '2')
                tabto = TAB_GAMES;
            else if (k == '3')
                tabto = TAB_DEV;
            else if (k == '5')
                tabto = TAB_SETTINGS;               /* '4' is the Pi's Lib: absent here, ignored */
        }
        if (tabto >= 0 && asking == ASK_NONE) {
            tab = tabto;
            mopt = -1;
            gopt = -1;
            continue;
        }
```
Se `tab` o `mopt`/`gopt` hanno nel ciclo un nome o una posizione diversi da come qui sono scritti: `non verificato` e `BLOCKED`, non indovinare. Il Start+Select che congela un gioco e' letto in `src/rgb30/bm_input.c:96-98` (`ui.c:456` e' la pagina Input test): non cambia.
Passo 4: `docs/spec/divergences.toml`, blocco `id = "D04"` (oggi righe 46-55; ritrovalo con `git grep -n 'id = "D04"'`): `status = "open"` diventa `status = "untested"` e subito sotto la riga `status` aggiungi
`note = "src/rgb30/ui.c: Start confirms in the menu unless Select is held (QEMU test_menu_start_confirms); Tab and 1/2/3/5 of a Bluetooth keyboard jump tabs (4 is the Pi's Lib, absent). U (upload) not ported. Not tried with a Bluetooth keyboard."`
Le altre righe del blocco (`evidence`, `action`) e gli altri blocchi restano.
Passo 5: ricompila e rilancia il test: atteso `PASS test_menu_start_confirms`, EXIT=0. Poi `python scripts/consistency.py check` prima/dopo il cambio: stesso output (salva il testo di prima con `git stash` o lanciandolo sul worktree pulito).

## Criterio di fine
Passo 1b rosso per timeout, passo 5 verde; `git diff --stat`: 3 file (ui.c ~ +21, qemu_test.py ~ +14, divergences.toml +2/-1); `consistency.py check` invariato. Commit unico sul branch `claude/rgb30-menu-start-tab`, messaggio `rgb30 menu: Start confirms, Tab and 1-5 of a keyboard change tab (D04)`. Push solo di questo branch.

## Consegna
Dal worktree di sync di AGX (`bm33-agx-sync`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py AGX --close AGX-13 --repo C:\Users\Utente\Documents\GitHub\bm33-agx13 --branch claude/rgb30-menu-start-tab --commit <sha del commit> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; il comando stampa la riga chat `done WO AGX-13 ...`: riportala in chat).
2. In `## Consegne` di `sync/AGX.md` aggiungi la riga standard `DELIVERED AGX-13 @ claude/rgb30-menu-start-tab@<sha> · verifica: python3 tests/rgb30/qemu_test.py --build build/rgb30-virt -k menu_start_confirms -> PASS test_menu_start_confirms (prima del fix: timeout) · 3 file · Tab/1-2-3-5 non verificati (nessun test), non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py AGX --state FREE --task "AGX-13 consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-coder (testo esatto). Stima: 40 minuti (la build dell'RGB30 e' la parte lunga).

## Decisioni
- D-1 (adottata): `U` (upload di una cartuccia) resta non portato sull'RGB30; la nota di D04 lo dice.
- Rischio noto, non verificato: i tasti di una tastiera arrivati mentre gira un gioco restano in coda di `hid_getc()` e, tornati al menu, possono cambiare scheda; sul Pi vale lo stesso. Non si risolve qui.

## Prove per l'utente
Con una tastiera Bluetooth sull'RGB30: Tab, 1, 2, 3, 5 nel menu cambiano scheda; Start conferma, Start+Select non conferma. Non verificato.
