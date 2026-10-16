# R-NET7 - FB-CRC: comando `crc` della riga del monitor (R-NET7a) e, più avanti, `bm_net --crc` (R-NET7b, NON da iniziare)
Sostituisce la bozza `sync/wo/drafts/R-NET7.md` (che resta dove sta, non si cancella: è superata da questo file). Assegnato a NET (16 ott), dopo R-NET6. Ordine di NET: R-NET5, API-1, APP-5b, R-NET6, poi questo. Solo la parte 7a è assegnata ora; la 7b è descritta più sotto e parte solo quando R-NET6 è integrato e SPV scioglie il punto aperto P1.
Base: `claude/integrazione-ottobre` @ `f5cd71b7` (worktree esistente `C:\Users\Utente\Documents\GitHub\bm33-int2`; il tuo è nuovo: `git worktree add -b claude/monitor-fb-crc C:\Users\Utente\Documents\GitHub\bm33-netcrc claude/integrazione-ottobre`). Branch: `claude/monitor-fb-crc`. Mai push su bm-core; push solo di questo branch.
Perimetro (7a): `src/kernel/monitor.c` (un `#include`, un ramo nuovo in `line_command`, il messaggio di errore, `help_text`), `tests/qemu_test.py` (UNA funzione nuova `test_monitor_crc`, subito dopo `test_monitor_line`), `docs/spec/divergences.toml` (UN blocco nuovo in fondo).
NON toccare: `src/drivers/fb.*`, `src/lib/crc32.*`, `src/gfx/console.*`, `src/net/*`, `src/rgb30/**`, `rgb30.mk`, `tools/*` (`bm_net.py`, `pi_bridge.py`, `pi.py`: sono della 7b), `tests/net/*`, `docs/design/pi-test.md` (7b), le altre funzioni di `tests/qemu_test.py` (CI, AGX-3, API-1, APP-5b e APP-7 vi lavorano: nessuna riga in comune), gli altri rami di `line_command`, `SYNC.md`, `sync/*`, `.github/workflows/*`, `docs/spec/CONSISTENCY.md`.
Vincoli: solo host e QEMU (`not verified on hardware`); NESSUN kernel sul Pi in questo WO: il caricamento di un kernel sul Pi richiede l'ok dell'utente in chat e va a fine giornata (vedi "Prova per l'utente"). Il tuo worktree NON è `bm33`, `bm33-sync`, `bm33-int`, `bm33-int2`. WSL solo con `scripts/wsl-run.ps1 '<comando>' -Tail 120`. Il `/tmp` di WSL è condiviso e piccolo (3,9 GB): prima e dopo `df -h /tmp | tail -1; ls -d /tmp/bm-* 2>/dev/null | wc -l`; `Qemu(...)` lascia una cartella `bm-XXXX` per corsa finché AGX-3 non è integrato: cancella con il percorso esatto solo quelle che la tua corsa ha creato. Ogni `file:riga` qui sotto è stato riaperto dal planner su `claude/integrazione-ottobre` @ `f5cd71b7`; PRIMA di modificare ri-trovalo con `git grep -n` (comandi indicati) e non fidarti del numero; ciò che non ritrovi come descritto: `non verificato` e `BLOCKED`. Un punto non verificabile si scrive `non verificato`.
Obiettivo: la riga del monitor `crc` stampa il CRC32 della pagina del framebuffer in quel momento mostrata, così un operatore può confrontarlo con un valore preso dallo stesso Pi, senza scaricare pixel.

## Stato reale (riaperto dal planner)
- `src/kernel/monitor.c`: `help_text[]` inizia alla 45; l'elenco della riga `:` è alle 88-95 (`"       render  room  log  err  send  reboot\n"` alla 92, l'esempio alla 93); `static int line_command(char *c)` alla 206 (stampa l'eco `: <cmd> <resto>` con `kprintf` alla 219 e poi la catena di `strcmp`); gli altri comandi: `gpu` 221, `b3d` 225, `play` 233, `set` 239, `save` 257, `render`/`room` 260, `log` 271, `err` 273, `send` 284, `reboot` 287; il ramo finale con il messaggio `'%s': not a command of the line (gpu, b3d, play, set, save, render, room, log, err, send, reboot)` alla 291; `line_commands()` 297-313 (`;` a 305, si ferma al primo errore). Il tasto `c` del monitor è `console_clear()` (338-342 `case 'c'`). Il file NON include `lib/crc32.h` (gli `#include` sono alle righe 1-30: `drivers/fb.h` alla 15).
- `src/drivers/fb.h:13-15`: `uint8_t *mem` (inizio del framebuffer virtuale), `buffers`, `shown` (indice della pagina in onda); `width`, `height`, `pitch` (byte per riga), `depth` (32 o 16). `src/drivers/fb.c:130`: `fb->base = fb->mem + index * fb->height * fb->pitch`: una pagina occupa `height * pitch` byte e la pagina `shown` comincia a `mem + shown * height * pitch`. `src/gfx/console.c:307` `console_framebuffer()` dà il `framebuffer_t *` del console (`main.c:65` `static framebuffer_t fb`, `main.c:223` `fb_init(&fb, SCREEN_W, SCREEN_H, 2)`: due pagine).
- `src/lib/crc32.h:7`: `uint32_t crc32(const void *data, uint32_t len);` (IEEE 802.3, uguale a `zlib.crc32`); `src/lib/crc32.c` è già nel kernel del Pi (`Makefile:70` `KERNEL_SRCS` prende tutto `src/` tranne `src/rgb30`). La tabella a 256 voci fa circa 6 MB in 40 ms sull'ARM1176 (commento di `crc32.c`): una pagina da 640x480x4 non pesa. `kprintf` accetta `%lu` e `%08lx` (zero-fill e `l` in `src/lib/printf.c:52-57,78`).
- RGB30: `rgb30.mk` non compila `monitor.c` (le sue liste `SHARED_SRCS` 58-62 e `BM_SRCS` 91-95 non lo nominano; `grep -n monitor rgb30.mk` non dà il file) e `docs/spec/divergences.toml` D22 (253-262, `area = "monitor"`) dice "none by design (no_monitor)". Quindi `crc` esiste solo sul Pi.
- `docs/spec/divergences.toml` sull'integrazione finisce con `D44` (Dev tab `.bme`); il blocco nuovo si chiama `D45`. NOTA: i numeri collidono con altri branch (B16: D43/D44 diversi): SPV rinumera al merge, tu non rinumerare.
- `tests/qemu_test.py`: `test_monitor_line` 5131-5174 (poi `def test_reports` alla 5177); `MENU = b"cartridge menu"` alla 39; `import shutil` 15, `import re` 13, `import tempfile` 21. Il modello apre l'SD con `mksd.build(img, [(cfg, "bm/config.txt")])`, `Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])`, `q.expect(MENU, timeout=30)`, `q.send(":err\r")`, `q.expect("the line is done", timeout=10)`, `q.expect("> ")`.

## R-NET7a - il comando `crc` e il suo test (bm-light, 40 min)
Passo 0: crea il worktree (riga "Base"); build: `scripts/wsl-run.ps1 'make -j4 > build/make.log 2>&1; echo EXIT=$?' -Tail 120` (la prima volta 10-20 minuti: `run_in_background`; leggi `build/make.log`). Atteso `EXIT=0` e `build/kernel.img`.
Passo 1: `src/kernel/monitor.c`.
 (a) `#include "lib/crc32.h"` dopo `#include "lib/printf.h"` (riga 20).
 (b) `line_command`: un ramo nuovo subito DOPO quello di `err` e PRIMA di `send` (ri-trova con `git grep -n 'strcmp(c, "send")' -- src/kernel/monitor.c`), stesso stile degli altri:
```
    } else if (!strcmp(c, "crc")) {
        /* the CRC32 of the page on screen (zlib.crc32 of its bytes): a golden to keep
         * from the same Pi, compared by the PC (R-NET7) */
        const framebuffer_t *f = console_framebuffer();
        const uint8_t *page = f->mem + (size_t)f->shown * f->height * f->pitch;
        kprintf("crc: %lux%lu bpp=%lu pitch=%lu crc32=0x%08lx\n", (unsigned long)f->width,
                (unsigned long)f->height, (unsigned long)f->depth, (unsigned long)f->pitch,
                (unsigned long)crc32(page, f->height * f->pitch));
```
 (c) il messaggio di errore (291): `crc` entra nell'elenco, nello stesso ordine del resto: `(gpu, b3d, play, set, save, render, room, log, err, crc, send, reboot)`; NON cambiare altro nella riga (il test di `test_monitor_line` cerca solo `'nope': not a command of the line`).
 (d) `help_text` (92): `"       render  room  log  err  send  reboot\n"` → `"       render  room  log  err  crc  send  reboot\n"` e, subito dopo la riga dell'esempio, una riga: `"     crc: the CRC32 of the page on screen (crc: WxH bpp=D pitch=P crc32=0x........)\n"` (non cambiare le altre righe dell'help).
 Il formato della riga stampata è fisso: `crc: WxH bpp=D pitch=P crc32=0x%08x` (es. `crc: 640x480 bpp=32 pitch=2560 crc32=0x1a2b3c4d`).
Passo 2: `tests/qemu_test.py`, `def test_monitor_crc(b, opts)`, scritta subito prima di `def test_reports` (dopo `test_monitor_line`). Docstring di 3 righe: "The monitor's ':crc' (R-NET7): the CRC32 of the page on screen, in the line's fixed format; the same screen gives the same value".
 Corpo, modellato su `test_monitor_line`: `tmp = tempfile.mkdtemp(prefix="bm-crc-")`; `cfg` con `layout=us\nwifi_boot=0\n`; `mksd.build(img, [(cfg, "bm/config.txt")])`; `q = Qemu(b("kernel.img"), ["-drive", f"if=sd,format=raw,file={img}"])`; `try:` ... `finally: q.close(); shutil.rmtree(tmp, ignore_errors=True)`.
 1. `q.expect(MENU, timeout=30)`; `time.sleep(0.5)`; `q.send(":crc\r")`; `out = q.expect("the line is done", timeout=10).decode(errors="replace")`; `m = re.search(r"crc: (\d+)x(\d+) bpp=(\d+) pitch=(\d+) crc32=0x([0-9a-f]{8})\b", out)`; `assert m, out`; `w, h, bpp, pitch = map(int, m.group(1, 2, 3, 4))`; `assert w > 0 and h > 0 and bpp == 32 and pitch >= w * 4, m.group(0)`; `first = m.group(5)`; `assert first != "00000000"`; `q.expect("> ")`.
 2. Lo schermo fermo. La riga `:crc` scrive sullo schermo (eco, risultato, "the line is done", "> ") e quindi due `:crc` di fila vedono schermi diversi e danno valori diversi: è voluto, ed è il primo controllo (il CRC segue lo schermo). Per avere due schermi uguali si usa il tasto `c` del monitor (`console_clear()`): `q.send("c")`; `q.expect("> ")`; `q.send(":crc\r")`; `A` = il CRC; `q.expect("> ")`; `q.send("c")`; `q.expect("> ")`; `q.send(":crc\r")`; `B` = il CRC; `assert A == B`. Controllo di sensibilità (stesso test, prima del `c`): il valore del passo 1 `first` e `A` devono essere DIVERSI (`assert first != A`: lo schermo dopo `:crc` e lo schermo ripulito non coincidono).
 3. PUNTO APERTO P0 (da verificare in QEMU, non indovinare): che `A == B` regga dipende dal fatto che dopo `c` la schermata sia identica (la riga di stato in alto, `status_put` in `src/gfx/console.c`, o il cursore potrebbero cambiare fra due corse: `non verificato`). Se alla prima corsa `A != B`: prova per capire la causa (stessa corsa con `q.screendump` o con un secondo `c`), scrivila nella consegna e, se non si risolve in 15 minuti, `BLOCKED: A != B (causa: <quel che hai visto>)`; NON indebolire il test in silenzio. Alternativa che SPV può adottare (decisione sua, non tua): il test controlla solo il formato e `first != A`, e la prova "stesso valore" passa a un altro WO.
Passo 3: `docs/spec/divergences.toml`, in fondo (dopo `D44`), con le stesse convenzioni dei blocchi vicini (copia i nomi dei campi da `D43`/`D44`):
```
[[divergence]]
id = "D45"
area = "monitor"
pi = "the monitor line `crc` prints the CRC32 of the page on screen (fixed format), to compare with a value taken from the same Pi"
rgb30 = "none: the RGB30 kernel does not compile src/kernel/monitor.c (see D22)"
cause = "todo"
status = "open"
evidence = "src/kernel/monitor.c; tests/qemu_test.py:test_monitor_crc"
action = "decide whether the RGB30 needs a crc of its screen (a serial command, or the pad kernel's own tool); until then the screen check by CRC is Pi-only"
```
 Conseguenza da scrivere in consegna: `cause = "todo"` con `status = "open"` conta fra le divergenze che bloccano 1.0.0 (`scripts/consistency.py:275` `BLOCKING`, `gate`); è la scelta del committente (RGB30 fuori per ora); se SPV preferisce `cause = "hardware"` o `"history"` lo cambia lui.
Passo 4: verifica (nell'ordine):
 - `scripts/wsl-run.ps1 'make -j4 > build/make.log 2>&1; echo MAKE=$?; python3 tests/qemu_test.py --build build -k monitor_crc; echo EXIT=$?' -Tail 120` → `MAKE=0`, `PASS test_monitor_crc`, `1/1 passed`, `EXIT=0`. Poi `-k monitor_line` → `1/1 passed` (stesso esito di prima: sul branch di base `monitor_line` può dare `back to the monitor` a 10 s, vedi CI.md `:5155`; se è rosso anche con `git stash`, scrivilo e usa solo `monitor_crc`).
 - `python scripts/consistency.py check` → nessun drift nuovo (annota prima e dopo); `python scripts/docs_audit.py score` → nessuna riga `! broken` / `! orphan` / `missing file` nuova.
 - `make TARGET=rgb30` (o `make -f rgb30.mk PLAT=virt all`) compila ancora: il file non vi entra, ma controlla che nessun header comune sia cambiato (nessuno lo è) → `non verificato` se non lo lanci.
 Prova di sensibilità (1 giro): con `git stash` della sola modifica di `monitor.c` `-k monitor_crc` deve cadere su "crc: ... " (comando sconosciuto); se non lo provi scrivi `non verificato che il test cada sul codice vecchio`.
Criterio di fine: tre file cambiati (`monitor.c` circa +12, `qemu_test.py` circa +45, `divergences.toml` +10), `monitor_crc` PASS, consistency e docs_audit senza nuovi errori.
Commit: `monitor: crc line command, the CRC32 of the shown page (R-NET7a)`.

## Dipendenze e perimetro disgiunto (letti i `Perimetro:` degli altri WO)
- Nessuna dipendenza da R-NET6 per la 7a (non tocca `tools/*`). I file coincidono con altri WO in modo non disgiunto solo per `tests/qemu_test.py` (CI.md, AGX-3, API-1, APP-5b, APP-7: ognuno su funzioni o righe diverse; CI cambia un `timeout` dentro `test_monitor_line`, alla 5155 circa: questo WO non lo tocca ma scrive la sua funzione subito dopo, quindi un conflitto di merge lì è di SPV) e `docs/spec/divergences.toml` (AGY-21 tocca solo la `note =` di D19, riga 226).

## R-NET7b - `bm_net --crc` e il bridge (SOLO dopo R-NET6 integrato; NON iniziare ora)
Descritto qui perché il lavoro resti pronto; parte solo con un nuovo via libera di SPV dopo `DELIVERED R-NET6` + integrazione e dopo la decisione sul punto aperto P1.
- `tools/bm_net.py`: opzioni `--crc` (apre la console, lancia la riga `crc` con lo stesso meccanismo di `--line`, stampa `crc32=0x........`) e `--crc-expect 0x........` (confronta con il valore atteso: exit 0 se uguale, exit 1 se diverso, con il messaggio `crc: expected 0x... got 0x...`).
- `tools/pi_bridge.py`: `crc` in `LINE_CMDS` (la lista ammessa di R-NET6; come comando senza argomenti) e, se SPV lo vuole, un verbo `crc` del bridge; `tools/pi.py` non cambia.
- `tests/net/check_bm_net_config.py`: casi con mock per `--crc` (output atteso, formato sbagliato → errore), `--crc-expect` (uguale/diverso → exit 0/1), `line crc` ammessa dal bridge e `line crc 1` rifiutata (`takes no arguments`).
- `docs/design/pi-test.md`: una riga nella tabella delle richieste (`crc`) e una sezione breve "Screen check by CRC" (cosa verifica, come si prende il valore di riferimento).
- Il riferimento si prende SEMPRE dallo stesso Pi (file `tests/golden/pi/*.txt` con il valore e la riga di comando che lo ha prodotto); nessun valore inventato o calcolato su un altro schermo.
- PUNTO APERTO P1 (da sciogliere prima della 7b): la riga `crc` gira nel monitor, quindi il CRC misura lo schermo del MONITOR (testo, scorrevole), non quello di un gioco o del menu: dal PC la riga `crc` arriva solo con il monitor o il menu in primo piano (`bm_net --line` rifiuta se un'applicazione ha i tasti, `bm_net.py:341-352`), e il tasto `c` per ripulire non è nella tabella `KEYS` del bridge. Un CRC del frame di un gioco richiede un'altra strada (una operazione di trasferimento come `T`/`G`, `src/net/netxfer.c`, che passa dal gioco in corso) e quindi un WO a parte. La 7b si pianifica di nuovo quando P1 è deciso.

## Decisioni adottate (SPV, raccomandazioni del planner)
- D1 CRC della sola pagina mostrata (`shown`), non di tutte le pagine.
- D2 Formato fisso `crc: WxH bpp=D pitch=P crc32=0x%08x`.
- D3 RGB30 fuori per ora; la divergenza D45 lo dice (`cause = "todo"`, `status = "open"`).
- D4 Il valore di riferimento (golden) si prende dallo stesso Pi, mai inventato.
- D5 La 7b non parte finché R-NET6 non è integrato e P1 non è deciso; la bozza `sync/wo/drafts/R-NET7.md` non si cancella.

## Prova per l'utente
Facoltativa, a fine giornata e SOLO dopo l'ok dell'utente in chat per caricare il kernel sul Pi (l'operatore NON carica un kernel da solo): dal monitor del Pi `c` e poi `:crc`, di nuovo `c` e `:crc`: i due `crc32=0x........` sono uguali (stesso schermo ripulito); senza il `c` in mezzo sono diversi. `non verificato su hardware`: la riga di stato o il cursore potrebbero cambiare lo schermo fra due prove (P0).

## Consegna
Dal worktree di sync di NET (`bm33-net-sync`, branch `sync/protocollo-ottobre`):
1. `python tools/sync_hdr.py NET --close R-NET7 --repo C:\Users\Utente\Documents\GitHub\bm33-netcrc --branch claude/monitor-fb-crc --commit <sha> --tokens <N> --min <M> --state FREE` (se non conosci i token scrivi quelli che sai e dillo in chat; riporta in chat la riga `done WO R-NET7 ...` stampata; la consegna riguarda R-NET7a: la 7b resta aperta).
2. In `## Consegne` di `sync/NET.md`: `DELIVERED R-NET7a @ claude/monitor-fb-crc@<sha> · verifica: python3 tests/qemu_test.py --build build -k monitor_crc -> PASS; consistency check <esito>; docs_audit score <esito> · 3 file · P0 <esito: A==B oppure BLOCKED> · divergenza D45 (SPV rinumera) · non verificato su hardware · <ISO con fuso>`.
3. `python tools/sync_hdr.py NET --state FREE --task "R-NET7a consegnato"`, commit, `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`.
Agente consigliato: bm-light (7a). Stima: 40 minuti (più la prima `make` se la build manca).
