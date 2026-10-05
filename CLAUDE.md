# bm (BareMetal) — note per chi lavora su questo repository

Kernel bare metal per Raspberry Pi Zero W (BCM2835, ARM1176JZF-S) e, con una seconda
build (`kernel7.img`), per il Pi Zero 2 W (BCM2710A1, Cortex-A53 a 32 bit): C + assembly +
Lua 5.4 embedded. Documentazione: `README.md` (presentazione in inglese, con showreel e
screenshot in `docs/img/`), `README_OLD.md` (il README completo, in italiano),
`docs/ROADMAP.md`, `docs/HARDWARE.md`; per chi fa giochi `docs/API-IT.md` e
`docs/GUIDA-GIOCHI.md` (in inglese `docs/API.md`, `docs/GAME-GUIDE.md`).

## Build e test

- `make` → `build/kernel.img` e `build/chainloader.img` (toolchain `arm-none-eabi-gcc`);
  con `ZERO2=1` anche `build/kernel7.img` (vedi la sezione del Pi Zero 2 W).
- `make test` → test sul PC (grafica, FAT, USB, audio, rete, giochi) + test end-to-end in
  QEMU (`-M raspi0`); `make test-zero2` gli stessi con `kernel7.img` (Pi Zero 2 W) in
  `-M raspi2b`.
- **Test durante lo sviluppo** (decisione dell'utente, 2026-10-04): non la suite intera.
  Solo i test di ciò che la modifica tocca nel kernel (QEMU con `tests/qemu_test.py -k
  <nome>`, i test sul PC del pezzo cambiato); per la GPU solo Overbit (`make
  test-overbit`, `test_overbit`) e il 3D Bench (`make test-b3d`). I test dei giochi e gli
  altri per ora si ignorano: se un aggiornamento tocca uno di questi, lo si attiva solo in
  quella sessione. kernel7 (Pi Zero 2 W) si ignora del tutto per ora: né build né test.
- L'utente prova sul Pi reale copiando `dist/kernel.img` (e `dist/kernel7.img`) sulla SD
  (WSL, `/mnt/d`), senza cavo seriale: tutto ciò che deve verificare va mostrato sullo
  schermo. Da WSL usa `./easy_install.sh` (nella root): al primo avvio salva cartella del
  repository e lettera della SD in `.easy_install.conf` (in `.gitignore`), installa i
  pacchetti e fa `make firmware`; poi mostra il branch e un menu: solo il kernel, `make
  install`, o immagine (`make image`, SD cancellata e formattata FAT32 da PowerShell come
  amministratore, i file dell'immagine sopra, impostazioni, salvataggi e giochi suoi
  rimessi), o kernel dalla rete (`bm_net.py --kernel`) a una console dei profili salvati
  (nome|IP|codice di 6 cifre|scheda|ultima volta, `PROFILES` nello stesso file), o un file a
  una console (5, `bm_net.py --send`: un `.bm` in `/carts`, che il suo menu mostra subito, una
  risorsa in `/bm/lib`; nome 8.3 proposto), o una release (r, `scripts/release.sh vX.Y.Z
  --no-sd`, la versione dopo l'ultimo tag proposta; i comandi `send FILE [profilo]` e `release`), o il monitor
  di una console (6, `bm_net.py`; `monitor [profilo]`), o il suo `bm/config.txt` (7 dalla rete, `bm_net.py
  --config`: le chiavi coi segreti nascosti, `chiave=valore` cambia, `chiave=` toglie; il comando `C` di
  `netxfer.c`, `config_merge` in `config.c`, valori fino a 127 caratteri; 8 sulla SD; `config [profilo]`,
  `config-sd`; prove in `make test-net`). Alla fine scrive "kernel: vecchio -> nuovo": la versione sta in `kernel.img`
  dopo `bmVER=` (`bm_version_tag` in `src/kernel/version.c`).
- Dev kit (richiesta dell'utente): l'overlay delle prestazioni sopra ogni `.bm` (`perf_frame`
  in `runtime.c`): fps, ms di `_update` + `_draw`, istruzioni Lua del fotogramma
  (`stat(10)`; prima del merge con `3d-performance` era `stat(6)`, ora il tempo del 3D),
  i massimi dell'ultimo secondo, la RAM (Lua + dati, e il massimo) e i token del codice
  (2026-10-04) e il grafico degli ultimi 64 fotogrammi. Settings > Screen and sound >
  "Performance overlay" (config `perf`), F11 (tasto di sistema, anche nelle app; era F3), `p`
  dalla seriale. `stat(11)` token (`src/bm/tokens.c`, `code_tokens()`: un'informazione, mai un
  limite, come vuole `docs/B16.md` §2.4), `stat(12)` KiB di Lua al massimo, `stat(13)` KiB dei
  dati (`assets_kb`), `stat(14)` il fotogramma più pesante; dopo una prova da uno strumento
  `cart_arg().run` ha i numeri della partita (`bm_set_arg_run` in `carts_tool_session`).
- Arrivati dal gioco Yharnam (branch `claude/yharnam`; il gioco va nel market): la risoluzione
  quadrata 256×256 del formato (il firmware dà 480×270, il gioco disegna nel riquadro al centro,
  `bm_video_enter` in `runtime.c`; `--res 256x256` in `mkbm.py`), la luce a livelli come in Dank
  Tomb (`fades`, `dark_begin`, `glow`, `dark_end`; `g16_fade_*` in `gfx16.c`), lo sheet con
  palette nel Makefile (`sheet8_<gioco> := 1`), L1/R1 di `pad()` dalla seriale (`u`, `o`) e dalla
  tastiera (Q, E). Prova in QEMU: `test_square_lights`. `tools/bmplay` (`make
  build/host/bmplay`) gioca una cartuccia sul PC col disegno e il suono della console e un bot
  in Lua; `video.sh` ne fa un video.

## bm SDK (carts/editor, branch `sdk-update`, 2026-10-04)

- L'hub del progetto nella bm Suite (richiesta dell'utente), con l'estetica di bm Studio e bm
  Animator e la loro libreria (`require "bm3d"`: colori, `draw_list`, `look`, `gizmo`,
  `split_mesh`/`encode_mesh`). Pagine: F1 progetto (1-6 aprono bm Code, Pixel, Studio,
  Animator, Mesh, Sound con `cart_tool`; titolo, autore, schermo, target `.bm`/`.b16`), F1 di
  nuovo il dev kit (token, memoria dei dati, file contro gli 8 MiB del `.b16`, numeri
  dell'ultima prova), F2 codice, F3 sprite e di nuovo mappa, F4 3D (modelli e animazioni,
  `i` scrive il codice), Esc menu; Ctrl+N i modelli di gioco (`TEMPLATES`: codice, sprite e
  mappa); F6 l'assistente nel modo della pagina (`guide` sul progetto, il modello dell'
  assistente entra in MESH/ANIM). Salva con `cart_save` (nome 8.3).
- `cart_arg().from` è lo strumento che ha aperto quello corrente: bm Code, Pixel, Studio,
  Animator, Mesh e Sound aperti dall'SDK hanno *Back to bm SDK* nel menu (solo allora: i
  menu dei test restano come prima).
- Ogni pagina è un blocco `do ... end` che esporta in `P` (meno di 200 locali); le scritte
  sulle righe di 16 pixel; la cornice di un dialogo non passa sulla riga del titolo (i test
  in QEMU lo leggono).
- Prove: `tests/studio/sdk_host.lua` (in `make test-studio`; ogni modello di gioco gira 400
  fotogrammi), QEMU `test_editor`, `test_sdk_suite`, `test_home_ui`. Guida: `sdk/README.md`.

## bm Studio (sdk/studio)

- Applicazione per il PC (pagina web, niente build né dipendenze) per i modelli 3D a
  tessere e la pixel art dello sheet; legge e scrive il `.bm`
  (sezione MESH, tipo 8, `src/bm/bm.h`). Guida: `sdk/README.md`.
- Numeri delle sezioni: 6 è AUDIO (banco di suoni), 8 MESH, 9 ANIM. I primi file di bm
  Studio avevano MESH 6 e ANIM 7: kernel (`format.c`), `core.js` e `bmmesh.py` li leggono
  ancora (un 6 senza la firma `BMAU` è MESH); si scrivono sempre 8 e 9.
- `sdk/studio/js/core.js`, `tiles.js`, `edit.js` girano anche in Node (`make test-studio`);
  l'interfaccia si prova con Playwright (`make test-studio-ui`, screenshot in
  `build/studio/`). In questo ambiente: `/opt/node22/lib/node_modules/playwright`.
- Convenzione dei vertici: una faccia si vede dal lato da cui appare in senso orario
  (`r3d.c`); verso glTF la z cambia segno e l'ordine dei vertici si inverte.
- **bm Animator** (`sdk/animator`, usa i file di `sdk/studio/js`): scheletri e animazioni
  nella sezione ANIM (tipo 9); `rig.js` e `animate()` in `runtime.c` fanno gli stessi
  conti (cambiarli insieme). `sprites.js` (3D→sprite) è un rasterizzatore software.
- **bm Studio e bm Animator della console** (decisione dell'utente: gli stessi nomi dei
  programmi per il PC). `carts/studio/main.lua` (monitor `3`: build con block, tile,
  select, vertex, paint; pagina models) e `carts/animator/main.lua` (monitor `6`: play,
  rig, animate, sprites), incorporate, scheda Dev, opzioni "Open in bm Studio" / "Open in
  bm Animator"; si passano lo stesso file con `cart_tool(nome, path)`. Il codice comune
  (MESH/ANIM con `string.pack`, progetto, annulla, menu, dialoghi, schede, `nav()` per il
  puntatore della tastiera) è la libreria del kernel `src/script/bm3d.lua`
  (`require "bm3d"`); salvano con `cart_write` (`sections`, `sheet` solo se dipinto,
  `from = false` per un progetto nuovo). `tile_face`/`place_faces` sono il port di
  `edit.js` (le facce devono restare identiche, `check_studio3d.js`). Una pagina è un
  blocco `do ... end` (meno di 200 locali). Il puntatore di sistema (M32) c'è, ma non
  lo chiedono ancora (`mouse(true)`): per ora tastiera e pad. Scritte sulle righe di 16 pixel (i test in QEMU leggono lo
  schermo). Stessa estetica delle altre app (richiesta dell'utente): pannello a sinistra
  di 168 px con liste a intestazione grigia (niente barre di icone), due righe sopra la
  vista (nome in arancio), colori di bm Mesh (`C.PT` giallo per le cose scelte, `C.HOT`
  azzurro per il puntatore, `C.WIRE`), assi in basso a sinistra (`T.gizmo`), menu,
  dialoghi e scacchiera della trasparenza come bm Mesh e bm Pixel. Prova sul PC: `tests/studio/tools3d_host.lua` (in `make test-studio`), QEMU
  `test_studio_animator`.
- **bm Mesh** (`carts/mesh/main.lua`, incorporata, scheda Dev, monitor `4`, opzioni "Open in
  bm Mesh"): vertici e facce dei modelli (MESH), delle mesh scritte da lui nel codice
  (funzioni `mesh_<nome>()` tra `-- [bm Mesh begin]` e `-- [bm Mesh end]` in fondo a
  `main.lua`, le sole righe che riscrive) e di quelle che costruisce il codice del gioco
  (`cart_meshes()`, `src/bm/meshcap.c`: il codice gira in uno stato Lua a parte con le API
  sostituite; i nomi dalle variabili). Salva con `cart_write(path, {sections=, lua=})`;
  lo scheletro di un modello segue i vertici (`vb`). Prove: `tests/studio/mesh_host.lua`,
  `check_mesh.js`, `test_meshcap` (in `make test-bm`/`test-studio`), QEMU `test_mesh`.
- **bm Pixel** (`carts/pixel/main.lua`, incorporata, scheda Dev, monitor `5`, opzioni "Open
  in bm Pixel"): la pixel art dello sprite sheet del progetto (`sget`/`sset`, `sspr` con
  zoom, `cart_sheet(w, h)` per la misura). Salva con `cart_write(path, {sheet = true,
  palette = ...})`: solo lo sheet cambia nel file, come SHEET8 con la tavolozza per prima
  (`sheet_section` in `runtime.c`, `bm_sheet8_pack` in `format.c`); i pixel non ridisegnati
  tengono i loro 24 bit. Attenzione in Lua: `cond and nil or x` dà sempre `x`. Prove:
  `tests/studio/pixel_host.lua`, `check_pixel.js` (in `make test-studio`), QEMU `test_pixel`.

## Risorse e scheda Lib (`docs/RISORSE.md`)

- File di risorsa: lo stesso contenitore del `.bm` con la firma `BMRES`, il tipo all'offset 12
  e mai codice: `.bmm` modelli, `.bmi` immagini, `.bms` suoni, `.bmt` mappe, `.bmc` palette,
  `.bmk` kit; sezioni INFO (10, testo `chiave: valore` e blocchi `[tipo nome]`) e SPRITES (11,
  zone con nome, fotogrammi a destra del primo), anche nei `.bm`. Specifica in `src/bm/bm.h`.
- `scripts/bmres.py`: list, extract, add (isole nelle celle libere, lo sheet cresce solo in
  altezza, nomi resi unici, INFO con `origin`), convert, info. Kernel: `bm_parse_any`,
  `bm_zone`, `bm_info_get` (`format.c`); in un `.bm` INFO o SPRITES rotti si ignorano.
- Scheda **Lib** dopo Dev (decisione dell'utente): Market · Games · Dev · Lib · Settings
  (dalla seriale `4` è Lib, `5` Settings); gruppi con sinistra/destra, lista a sinistra, anteprima a destra; Y
  (tastiera V) suona. `src/kernel/lib.c` (elenco), `libview.c` (anteprime), `menu_ui.c`,
  `carts.c`. Test: `make test-res`, QEMU `test_lib_tab`.

## API dei giochi: bmlib, mappa, flag, hitbox, più giocatori, bmnet (R10, R11; branch `game-api`)

- Documentazione in due lingue (richiesta dell'utente, 2026-10-04): `docs/API.md` (inglese;
  era `docs/API-EN.md`) e `docs/GAME-GUIDE.md`, in italiano `docs/API-IT.md` e
  `docs/GUIDA-GIOCHI.md`.
  Un'API nuova o cambiata va in tutti e quattro (le due versioni hanno le stesse tabelle e gli
  stessi esempi) e nella base dell'assistente (`src/ai/kb/`, poi `make ai-model` e commit di
  `assist.weights`).
- **bmlib** (`require "bmlib"`, `src/script/bmlib.lua`, nel kernel come `bm3d`: `embed.S`,
  `require.c`, `tests/host/libs.S`, `src/rgb30/bm_embed.S`): le utilità comuni dei giochi
  (numeri, caso, collisioni con rettangoli e mappa, tween, timer, script, particelle,
  camera, stati, testo, menu e pausa, jingle, salvataggi, costruttore 3D). Tempi in
  secondi (`lib.update()` in `_update`), velocità in pixel per fotogramma; non tocca le
  globali e non copre funzioni della console (`sfx`, `slide`...). bm Mesh (`meshcap.c`)
  carica la vera bmlib.
- **Mappa**: fino a 8 livelli (sezione LAYERS 12; la MAP è il primo, con nome `"main"`),
  `rt.layer[]` in `runtime.c`; **flag** delle tile (FLAGS 13, letti per posto della cella),
  `fget`/`fset`, `mflags` (pixel), `msize`, `mlayers`, `map(..., livello, maschera)`;
  zone con nome (SPRITES) dai giochi: `zspr`, `zone`, `zones`. Convenzione dei flag (bmlib):
  0 solido, 1 piattaforma, 2 scala, 3 acqua, 4 fa male. `mkbm.py --map nome=file.csv`,
  `--flags`, `--sprites`; nella build `map_<nome>.csv` (`layers_<gioco>`), `flags.csv`,
  `sprites.txt`. L'SDK li modifica (mappa: `l`, Shift+L, `o`, `c`, *New map layer*;
  sprite: tasti `0`–`7`), `bmres.py` li porta nelle risorse.
- **Hitbox e hurtbox** (richiesta dell'utente): sezione BOXES (14, i riquadri dei
  fotogrammi delle zone: `hurt`, `hit`, `body`, tipi del gioco), `zboxes()` nel runtime,
  righe sotto la zona in `sprites.txt`; in bmlib `lib.hits()` (`H:clear`, `H:hurt`,
  `H:hit` con `id`/`team`, `H:zone`, `H:check`), `lib.box`, `lib.separate`.
- **Più giocatori** sulla stessa console: `controller(p).color`, `lib.PLAYER_COLORS`,
  `lib.pads`, `lib.party` (la schermata dove si entra), `lib.split` e `camera:apply(vista)`.
  In bmhost `device P kind` nello script collega il giocatore P (`players()`).
- **bmnet** (`require "bmnet"`, `src/script/bmnet.lua`, incorporata come bmlib): il
  protocollo di rete di Overbit per tutti i giochi (LAN o relay `tools/overbit_relay.py`,
  lobby, `net.send`/`net.post`, lockstep `net.input`/`net.frames`, `net.check`); gli eventi
  arrivano da `net.update()` (anche quello di `net.start` di chi ospita, al giro dopo). Le
  regole del lockstep sono quelle di Overbit (sezione *Overbit*, "Rete").
- Modelli dell'SDK (`TEMPLATES` in `carts/editor/main.lua`): Platform, Top-down e Shooter
  con bmlib e `fset` nel `paint`; Versus 2D e Online 2D. `tests/studio/sdk_host.lua` li
  compila e li fa girare sugli stand-in (bmnet senza rete).
- Prove: `make test-gameapi` (bmhost, `tests/gameapi/`), `make test-bmnet` (due bmhost),
  QEMU `test_game_api`, `test_sdk_layers`, `test_editor`, `make test-res`, `make test-bm`
  (meshcap con bmlib), `make test-studio` (sdk_host).

## Nome

- Il progetto si chiama **bm** (BareMetal); cartucce `.bm`, cartella `bm/` sulla SD.
  Il vecchio nome sopravvive solo dove serve alla compatibilità (la cartella della SD e
  l'intestazione delle cartucce di prima, lette ancora; il tag di rete per i kernel
  vecchi in `tools/bm_net.py`). Il repository GitHub è `f-accomando/bm`.
- Il branch principale è `bm-core` (decisione dell'utente del 2026-10-04; prima
  `claude/bare-metal-mvp`): quando l'utente dice "main" intende quello (un branch `main`
  non esiste).

## Branch delle sessioni

- Quando una nuova sessione comincia uno sviluppo specifico (una funzione, un gioco, un
  passo di una milestone), prima di modificare i file chiedere all'utente il nome del
  branch, proponendone uno breve legato allo sviluppo (es. `claude/m19-aggiornamenti`).
  Il branch parte da `bm-core` aggiornato e commit e push vanno lì, anche se
  la sessione ne ha assegnato un altro.
- Non serve chiederlo per domande, letture della roadmap o lavoro che l'utente ha già
  indicato su un branch esistente (es. "fai commit su bm-core").
- Gli spunti **R1, R2, …** (in fondo a `docs/ROADMAP.md`) sono idee non ancora decise:
  quando l'utente ne nomina uno ("facciamo R7") il significato è lì.

## Pi Zero 2 W (M31): `kernel7.img`

- Gli stessi sorgenti compilati una seconda volta per ARMv7 a 32 bit (`ARCH7` nel
  `Makefile`, oggetti in `build/k7/`, `-DBM_ZERO2`): `build/kernel7.img`. Sulla SD stanno
  `kernel.img` e `kernel7.img`; `config.txt` sceglie (`[pi02]`). Il codice specifico va
  sotto `#ifdef BM_ZERO2` (indirizzi del SoC, `src/drivers/mmio.h`) o `#if __ARM_ARCH >= 7`
  (istruzioni ARMv7: barriere, cache, HYP); ogni modifica deve compilare in tutti e due
  (per ora no: kernel7 si ignora, decisione dell'utente del 2026-10-04).
- Sul Zero 2 W: periferiche a 0x3F000000, avvio in HYP, un solo core (gli altri nello stub
  del firmware a 0x0: mai scrivere lì), LED sul GPIO 29 (il 47 è l'I2C dell'alimentatore),
  BT_ON GPIO 42, firmware del CYW43436 (`board.c`, `wifi.c`, `bt.c`).
- **Per ora spenta** (decisione dell'utente, 2026-10-03): `make`, `make test`, `sdcard`,
  `install`, `image`, `release` e la CI lasciano fuori `kernel7.img` e i suoi test.
  `make ZERO2=1 ...` (o `ZERO2: "1"` in `.github/workflows/ci.yml`) li riaccende; `make
  test-zero2` lo compila comunque. Il codice `BM_ZERO2` resta e deve continuare a compilare.
- Test: `make test-zero2` (QEMU `raspi2b`), `make test-hyp` (avvio in HYP nella macchina
  `virt`). Un kernel mandato dalla rete deve essere per la scheda giusta (`bmK6`/`bmK7`
  all'offset 4, `netxfer.c`).

## RGB30 (branch `rgb30-powkiddy`, M40)

- bm per PowKiddy RGB30 (RK3566, AArch64): `make TARGET=rgb30` (incluso da `rgb30.mk`; la build
  del Pi non cambia), `make TARGET=rgb30 test` (QEMU `-M virt`), `make TARGET=rgb30 firmware
  image` (SD). Guida: `docs/RGB30.md`.
- Compilatore `aarch64-linux-gnu-gcc` + **picolibc** (non newlib); codice specifico in
  `src/rgb30/` (`plat_virt.c`/`sd_virt.c` per QEMU, `rk_*.c` per la console). Gli header in comune
  con l'ARMv6 (`kernel/irq.h`, `arch/cache.h`) hanno un ramo `__aarch64__`.
- Menu: **lo stesso del Pi** (`menu_ui.c`, decisione dell'utente del 2026-10-04) a 360×360
  ingrandito ×2 sul pannello, due copertine per riga; `ui.c` gli dà la vista e i tasti: schede
  Games / Dev e Settings con i pannelli del Pi (`settings.c`; L1/R1 senza giro; dalla seriale
  `l`/`r`, `w a s d`, Invio, Backspace), giochi `.b16` (la cartuccia a risorse limitate per
  le portatili, formato da definire: `docs/B16.md`), `.bm` visibili per le prove
  (`show_bm=0` li nasconde); le pagine dietro le voci sono ancora sulla console. In Dev il 3D Bench (`b3d_rgb30.c`: `src/bm/b3d.c` con i contatori del
  Cortex-A55). Nei test lo schermo si legge dai pixel: il testo del menu sta sulla griglia del
  font 8×16 (x multipli di 8, y di 16).
- L'utente prova senza seriale: LED (rosso = avvio; verde fisso = tutto bene, verde lento =
  altro, `ledstate.c`) e `bm/bootlog.txt` scritto sulla SD a ogni avvio. Il WiFi salvato si
  collega all'avvio come sul Pi (`wifi_boot=0` lo spegne; prima serviva `wifi_boot=1`).
- Cartucce del Pi (`.bm`): per ora nel menu e avviabili, per le prove (decisione dell'utente).
  Il runtime `src/bm` è lo stesso del Pi (`#ifdef BM_RGB30` solo in `bm_video_enter`: lo schermo è `fb_init_game`); i
  driver del Pi che chiama sono sostituiti in `src/rgb30/bm_port.c` (suono muto, niente V3D e
  DMA) e `bm_input.c` (comandi per lettera, `game_buttons=position`). Yharnam (dal branch
  `claude/yharnam`) è nell'immagine SD; test `test_bm_cartridge`.
- Tasti: **B conferma, A torna indietro** (decisione dell'utente; `confirm=a` li scambia):
  nell'interfaccia si usano `pad_ok` / `pad_back` e `pad_ok_name()` / `pad_back_name()` (`pad.h`),
  mai `PAD_A` / `PAD_B` per conferma e indietro.
- Schermo: modalità pronte per la GPU Mali (`src/rgb30/display.h`, `fb_init_mode`): righe a 64
  byte, tessere da 16, pagine su 64 KiB nella memoria video e GPU (0x3c000000, 64 MiB), il
  controller video ingrandisce sul pannello 720×720. Pagina *Display* nel menu.
- WiFi: port di rtw88 (`src/rgb30/rtw*.c`, BSD-3-Clause), WPA2 in software (`wpa.c`), lwIP di
  M18. `make TARGET=rgb30 test-wifi`: frame, WPA2 contro `tests/rgb30/wpa_vectors.h` (scritto da
  `wpa_vectors.py`, Python + `cryptography`) e tutta la stazione su un chip e access point simulati
  (`wifi_sim_test.c`, `-v` mostra la console).
- Aggiornamenti come sul Pi: *Settings > Updates* (`page_update` in `ui.c`, `src/kernel/update.c`
  con `BM_RGB30`: `manifest-rgb30`, `kernel8.img` riconosciuto dall'intestazione arm64 `ARM\x64` a
  +56), HTTPS e `release.c` nella build; le fibre del Market non ci sono (`stubs.c`: la rete
  aspetta sul posto). `netxfer.c` scrive `kernel8.img`. Test `test_update_from_sd`.

## Cartucce `.cart`: rimosse

- Decisione dell'utente (2026-09-30): bm esegue solo i `.bm`. Il vecchio formato `.cart`
  e il suo interprete non entrano nelle build, nel kernel o nell'immagine SD; non
  reintrodurli senza una richiesta esplicita.

## Menu del Pi e della RGB30 (decisioni dell'utente, 2026-10-04)

- **Un solo menu**: `src/kernel/menu_ui.c` disegna il menu dei due sistemi (stessa barra,
  stesse pillole, copertine, pannelli e colori); la larghezza decide le colonne
  (`menu_ui_cols()`: 4 copertine per riga a 640, 2 a 360), l'altezza è 360 per tutti. Il Pi lo
  guida da `carts.c`, la RGB30 da `src/rgb30/ui.c` (stessa `menu_view_t`).
- **Risoluzione**: Pi 640×360 (l'ARM disegna); `menu_scale=3` in `bm/config.txt` lo porta a
  1920×1080 con lo stesso layout ×3, ingrandito dall'ARM (lento: una prova; il 1080p vero lo
  disegnerà la GPU, M37). RGB30 360×360 ×2, ingrandito dal controller video sul 720×720.
- **Comandi**: sul Pi prima tastiera e mouse (i suggerimenti mostrano i tasti della tastiera
  finché non si preme un controller); sulla RGB30 il controller (`confirm_b`: B conferma, A
  indietro, `no_monitor`: niente monitor).
- **Copertine quadrate** (2026-10-04, l'identità della bm Suite): 88×88 (`MENU_CARD`),
  angoli di 16, 12 px tra l'una e l'altra, 6 per riga sul Pi e 3 sulla RGB30. Le icone degli
  editor (bm Studio, Code, Animator, SDK, Sound, Mesh, Pixel) vengono dal foglio dell'utente
  `art/brand/bm-suite.png`: `scripts/mkicons.py` le ritaglia in `carts/<app>/icon.png`, il
  Makefile le dà a `mkbm.py --cover`. `mkbm.py` scrive la COVER a 88×88; una copertina non
  quadrata (i giochi, 16:10) resta intera sopra una copia sfocata di sé, e il kernel fa lo
  stesso con le copertine vecchie e quelle del Market (`menu_load_cover`). Gli strumenti
  senza icona propria hanno copertine disegnate nello stesso stile (gradiente diagonale,
  `menu_make_tool_cover`).
- **Market a sinistra**: fuori dallo schermo, se ne vede la fine del nome, finché non è la
  scheda; scelto, le pillole scorrono a destra (`peek_first` di `menu_view_t`, solo il Pi).
- **Settings uguali** sui due sistemi (2026-10-04): `src/kernel/settings.c` costruisce i
  pannelli per tutti e due (`home_panel`, `home_act`; le righe di un sistema solo sotto
  `#ifdef BM_RGB30`): Controllers, WiFi and network, Screen and sound, Updates, Reports, System,
  Restart, Shut down, con dentro i comandi utili del monitor (scansione USB, prova della connessione, test pattern,
  prova del suono, il log). Un pannello nuovo o una riga nuova va lì, per tutti e due.
  Settings è una **pagina a sé** (richiesta dell'utente), non un pannello sopra le copertine
  (`menu_page_t`, `draw_page` in `menu_ui.c`): sul Pi le sezioni a sinistra e a destra le
  righe della sezione (un'anteprima finché non ci si entra: destra o A entra, sinistra o B
  torna); sulla RGB30, stretta, una lista alla volta. `home_sub_panel()` dà il pannello di
  una sezione.
- **Il lavoro del menu in background** (richiesta dell'utente, 2026-10-04: menu fluido con
  tantissime app): copertine di Games e Dev e scheda Lib in fibre (`fiber_job_t`, `fiber_run`,
  `fiber_slice` in `src/kernel/fiber.c`) nel tempo libero del fotogramma (`menu_idle` in `carts.c`,
  prima il lavoro della scheda mostrata). Il menu si apre subito con i segnaposto (titolo e barre,
  come il Market); di un `.bm` si legge solo l'inizio (`fat_load_part`, `bm_cover_peek`: la COVER è
  la prima sezione), quelle a schermo per prime (`next_cover`). La Lib legge l'elenco
  (`scan_main`, "reading the SD card: 3/11") e poi il file e l'anteprima della risorsa scelta
  (`lib_view_work`) a fette; `lib_open` fuori dalla fibra la ferma prima. Le pause: `fat_load_tick`
  dopo ogni cluster e `bm_parse_tick` (CRC a pezzi da 64 KiB, mesh, animazioni, SHEET8) chiamano
  `sd_tick` (lo splash, `fiber_slice`); se restituiscono non zero la lettura si ferma ("stopped").
  `background_stop()` ferma tutto prima di un'app, di `rescan()` e di una cancellazione. Il log della
  Lib dice quanto il menu ha aspettato al massimo ("the menu waited at most N ms more": in QEMU 3 ms
  con Overbit e Yharnam sulla SD). Il CRC è a tabella (`src/lib/crc32.c`, `crc32_update`). Sulla
  RGB30 (senza fibre) le copertine leggono anch'esse solo l'inizio del file.
- Test: Pi `test_menu_tabs`, `test_menu_scale`, `test_home_ui` (il giro delle Settings) e quelli
  del menu; RGB30 tutto `tests/rgb30/qemu_test.py` (la scheda e il titolo scelto si leggono
  sulla pillola).

## Avvio, LED e avvisi di sistema (decisioni dell'utente, 2026-10-04)

- **Splash**: all'avvio il logo di bm (`src/kernel/splash.c`, il "bm" del foglio
  `art/brand/bm-suite.png` in `logo_data.c`, scritto da `scripts/mklogo.py`: rieseguirlo se il
  foglio cambia) con la versione; la console è sospesa ma tiene le righe dell'avvio (le mostra il
  monitor, le pagine di testo e *Log since boot*), e la seriale e `klog` le hanno tutte. Il menu
  prende lo schermo quando si apre (`menu_ui_open`).
- **Caricamento delle applicazioni** (richiesta dell'utente; `src/bm/loading.c`): uno splash di
  sistema al posto del log, lo stesso per ogni gioco e strumento (niente titolo, decisione
  dell'utente): un'animazione retro su una tela di 160×90 ingrandita (il "bm" in pixel art di
  `loading_logo.c`, da `mklogo.py`, con la gamba sinistra della m lunga come le altre, che cade e
  atterra, un jingle nostro sulle voci 5-7, poi un circolino di punti che gira) finché file, asset
  e `_init` non sono pronti, e almeno l'intro (1,8 s). La comincia il menu per ogni applicazione
  (giochi, SDK, Sound, bm Code, bm Studio, bm Animator, bm Mesh, bm Pixel; dalla scheda Games, Dev
  o Lib; non per un gioco sospeso che riprende): `loading_begin` nel passaggio `GO_*` di
  `carts.c` con `menu_ui_close_quiet` per non far vedere la console, `play_bm` della RGB30; un
  `loading_stop()` dopo, per chi non arriva al primo fotogramma. Il runtime la fa andare avanti
  (`fat_load_tick`, gli asset, il Lua compilato a pezzi da `read_chunk`, il hook durante
  `_init`), la porta sulla pagina del gioco dopo `enter_mode` (`loading_page`) e la chiude prima
  del primo fotogramma (`loading_end`, che scrive `bm: loaded in ... ms, the splash over at ...`:
  i test QEMU lo aspettano prima di premere i tasti del gioco). Senza `loading_begin` (bmhost, i
  giochi provati dagli strumenti) niente; `game_intro=0` in `bm/config.txt` la spegne. Prova:
  `make test-loading` (fotogrammi in `build/loading/`).
- **LED** (Pi e RGB30, `src/kernel/ledstate.c`): fisso se non c'è niente che non va, lampeggio
  lento (1 s / 1 s) per tutto il resto: avvio non finito (`LED_BOOT`), SD mancante o illeggibile,
  alimentazione bassa (Pi: `GET_THROTTLED` bit 0, ogni 5 s dal menu; RGB30: batteria sotto 3,45 V
  senza caricatore), schermo (RGB30), un kernel che arriva o un aggiornamento che si installa. Le
  eccezioni fatali tengono i loro codici (`led_blink_code`). Una causa nuova è un bit in
  `ledstate.h`.
- **Avvisi sopra tutto** (`src/kernel/notice.c`): un kernel dalla rete (`netxfer.c`) mostra un
  riquadro con l'avanzamento nel menu (`menu_view_t.notice`) e nei giochi (`bm_set_notice`,
  `sys_box` in `runtime.c`), righe ogni 10% sulla console; poi **3 s contati** prima del riavvio
  (`netxfer_kernel_state`). Anche l'aggiornamento conta 3 s ("Restarting in 3...") prima di
  riavviare. Un riavvio che parte da solo conta sempre i 3 s.
- **Restart e Shut down** sono le ultime voci di Settings su tutti i sistemi (`settings.c`); sul
  Pi Shut down è l'arresto del firmware (`watchdog_halt`, la partizione 63 come Linux): resta
  spento finché non torna la corrente.
- L'icona del mouse USB c'è solo quando il mouse ha mosso, cliccato o girato la rotella
  (`hid_mouse_seen`): il ricevitore di una tastiera wireless dichiara un mouse anche senza mouse
  (Pi 1 B).

## Report dei test (decisione dell'utente, 2026-10-04)

- Quando servono i numeri di un test sul Pi o sulla RGB30, chiedere all'utente di farlo: la
  console ne fa un **report** e lo manda via rete nel branch `reports` di `f-accomando/bm`
  (`report_repo`, `report_branch` in `bm/config.txt`), in
  `reports/<branch>/<data>_<tipo>_<scheda>_<kernel>.txt`; si legge con gli strumenti GitHub
  (ref `reports`). L'intestazione dice tipo, kernel (git describe), branch (`bm_branch`,
  `BRANCH` nel Makefile), scheda e data.
- Serve `github_token` (un token che può scrivere i contenuti del repository) e la rete;
  senza, i report aspettano sulla SD (`bm/reports/RPTnnnnn.TXT`) e partono **da soli** appena
  la console è in rete (richiesta dell'utente, 2026-10-04: `reports_auto_due` in `reports.c`;
  sul Pi in una fibra di `menu_idle`, solo in Games, Dev e Lib senza pannelli aperti, fermata
  prima del Market e delle Settings perché il DNS di `stream.c` è uno solo; sulla RGB30 dal
  menu, sul posto); se GitHub rifiuta riprovano dopo 15 minuti. A mano: *Settings > Reports >
  Send the reports* (anche sulla RGB30) o `z` nel monitor; `Z` (o *Report the log*) manda il
  log; `report_upload=0` solo a mano. Spediti, spariscono dalla SD. Prova sul PC: `make
  test-github` (`tests/kernel/test_reports.c`: SD, GitHub, rete e orologio finti).
- Fanno un report: gli strumenti di Dev che stampano (`tool_t.report` in `home.c`: System,
  Audio, CPU bench, Render bench, Stress test, DMA test, GPU test, Texture Room, Demo,
  Diagnostics), i comandi `g k p D s R` del monitor, il 3D Bench (il suo, anche sulla
  RGB30), le cartucce con `report(tipo, testo)` (Overbit: il benchmark). Codice:
  `src/kernel/reports.c` (cattura con `klog_capture`, SD, invio), `src/net/report.c` (nome e
  intestazione, portabile), `github_put` in `src/net/github.c`. Prove: `make test-github`
  (anche `test_report`), QEMU `test_reports`, RGB30 `test_bench3d`.

## Icone dei tasti (bm-ui)

- `src/kernel/prompts.c`: tasti di DS4, pad generici e tastiera come icone, due set scelti
  dall'utente. Menu: in rilievo (faccia bianca su bordino grigio, simbolo ritagliato; solo
  i 4 tasti frontali del DS4 anche a colori), nei suggerimenti secondo `hid_last_source()`.
  App di sviluppo (SDK, bm Studio, bm Animator, bm Mesh, bm Pixel, Sound, bm Code,
  assistente; non nano8): chip colorati da 16 o 12 px, dal Lua con `prompt()` /
  `lastinput()`; le scritte accanto restano sulle colonne del font (`hint()` e
  `chip_hint()` in bm Mesh, bm Pixel e bm3d). In `prompt()` le maiuscole sono pulsanti
  del pad: un tasto con Shift si scrive `"shift"` + la lettera.
  Nei test sul PC `prompt` scrive `"[nome]"`. bm Code ha un suo `prompt()` locale (il
  dialogo): lì si chiama `key_chip`. `make test-prompts` disegna i due set in
  `build/prompts/`: guardarli dopo ogni modifica.

## Tasti di sistema (2026-10-04, tastiera; il pad dopo)

- Una sola tabella, `src/kernel/syskeys.c` (decisione dell'utente: tasti di sistema
  universali, quelli delle app non li usano per altro): **F12 tenuto** mostra i tasti
  (sistema, poi quelli del menu o dell'app, con le icone; niente tasto `?`), **Esc** menu
  dell'app o un livello indietro (nei giochi è Start), **Ctrl+Esc** torna al menu di bm (=
  PS del DS4; Ctrl+\ dalla seriale), Ctrl+Shift+Esc il monitor, F1–F4 le pagine, F5 /
  Ctrl+R prova il gioco, F6 l'assistente, **F11** l'overlay delle prestazioni, Ctrl+S /
  Ctrl+Shift+S / Ctrl+O / Ctrl+N, Ctrl+Z / Ctrl+Y, Ctrl+X / C / V, Ctrl+F.
- Le app danno i loro con `keyhelp(lista, titolo)` (minuscolo la tastiera, maiuscolo il
  pad; in rosso e nel log le voci con un tasto che il kernel tiene per sé: F11, F12, Ctrl+Esc) e chiedono prima di
  uscire con `_exit()` (modifiche non salvate: Ctrl+Esc di nuovo esce). bm3d lo fa per
  bm Studio e bm Animator (`A.help`, `help`/`help_pad` delle pagine, `T.keyhelp`).
- Prove: QEMU `test_keys_help` (menu e bm Studio), le prove sul PC delle app
  (`tools3d_host.lua`, `mesh_host.lua`, `pixel_host.lua`, `tests/sound/sim.lua`).
- **I tasti dei giochi** (richiesta dell'utente, `docs/API-IT.md`): `btn`/`btnp` anche per nome,
  `"ok"` e `"back"` come i menu del sistema (DS4: croce sì, cerchio indietro; RGB30:
  `input_ok_bit()` in `bm_input.c`), `keymap()` per le azioni del gioco, `controller(p)` per il
  dispositivo e il layout di ogni giocatore (`input_device()` con `INPUT_DEV_DS4`, `_XBOX`,
  `_BUILTIN`), `prompt(..., giocatore)` con le icone del suo controller. PS (Ctrl+Esc) esce al
  menu di bm con il gioco sospeso. Prova: `make test-keymap` (bmhost, `tests/keymap/`; il
  comando `device` dello script).
- **PS in una partita in rete** (decisione dell'utente, 2026-10-04): niente sospensione. Il
  gioco dice `online(true[, nota])`; PS chiede solo al giocatore che esce, sulla sua console,
  "Leave the match?" (uscirà dal gioco e si disconnetterà dal server), sopra il gioco che va
  avanti senza vedere i suoi tasti (`leave_step`/`leave_draw` in `runtime.c`); sì chiama
  `_leave()` e chiude la cartuccia, indietro resta. Overbit: `Net.begin`/`Net.close`, il Q
  porta il posto (l'host lo dà subito a un bot) e se viene dall'host. Prove: `make
  test-online` (bmhost, comando `ps` dello script, screenshot in `build/online/`), le partite
  in rete di `make test-overbit` (l'ospite esce con PS).

## Overbit (M38)

- Sparatutto a eroi in 3D (`carts/overbit`; motore GPU e gioco cresciuti nel branch
  `3d-performance`, unito al principale il 2026-10-03; `claude/overclone` è fermo e già
  unito, decisione dell'utente): kit di Overwatch
  con **nomi e design nostri** (decisione dell'utente), mai marchi di Blizzard o altri.
  Lua in `src/*.lua` uniti da `build.py` (00_core resta al livello più alto, gli altri in
  `do ... end`); modelli e animazioni generati da `art/models.py` (`geo.py` primitive,
  `rig.py` scheletri e clip, `heroes/<eroe>.py`), suoni da `art/sounds.py`.
- Convenzioni: metri, y in alto, un personaggio guarda +z e la sua destra è +x (ossa "L" a
  −x); yaw a → avanti (sin a, 0, cos a). Facce in senso orario viste da fuori.
- Schermo **480×270** all'avvio (`OVERBIT_RES` nel `Makefile`; era 320×180), poi la voce
  RESOLUTION del menu (`screen()`, 320×180–1920×1080, salvata con `save()`; l'ARM fino a
  640×360). Mai numeri fissi nel 2D: `00_core.lua` dà `SW`, `SH` (pixel), `ZOOM` (le cose
  del mondo disegnate in 2D: sole, lampi) e l'HUD su uno schermo logico `LW`×`LH`
  ingrandito `UI` volte con `urectfill`, `uprint`, `uprompt`... (le coordinate proiettate
  si dividono per `UI`); `screen_size()` le rifà quando `SCREEN_W` cambia. Con la GPU il
  cielo è 3D dopo `zclear()` (nessun 2D prima del 3D: la GPU pulisce la pagina).
- Eroi: il kit in `src/5x_<eroe>.lua` (tabella in `H`, forme, `update`, `draw_fp`, `hud`,
  ganci `draw_extra`, `draw_hud`, `camera`, `on_lethal`...), il modello in
  `art/heroes/<eroe>.py`. Le persone usano il corpo comune (`humanoid.py`: scheletro,
  `body()`, armi tenute con IK, clip standard) e `fp.py` per mani e braccia in prima
  persona. Livelli di dettaglio: i pezzi piccoli solo al 3, le versioni povere fino al 2.
  Sistemi comuni: barriere e props (`45_proj`, `47_props`), stati (`frozen_t`,
  `rooted_t`, `slow_t`, `haste_t`, `rush_t`, `invuln_t`), `Actors.cone`, numeri delle cure.
- **Modelli Meshy** (richiesta dell'utente): i corpi in terza persona degli eroi, dei mech e
  dei piloti sono figure Meshy fatte dal testo con le **nostre** descrizioni
  (`art/meshy/<nome>.txt`, `tools/meshy_text.py`; il workflow `meshy-overbit` parte da un
  push di `art/meshy/request.txt` su `bm-core` e mette i risultati su
  `meshy-out`), poi impacchettate (`art/meshy/pack.py`: `<nome>.mesh` con 1200 e 450 triangoli,
  `<nome>.png`, `<nome>.rig` dal rigging di Meshy, `tools/meshy_rig.py`). `art/meshyrig.py`
  le mette sugli scheletri degli eroi (da A-pose alla posa di riposo, un osso a vertice dal
  rig; per i mech uno scheletro con le stesse ossa misurato sulla figura e le clip rifatte);
  le armi e gli altri pezzi su ossa non del corpo restano procedurali (le mitragliatrici
  rotanti di Rally, `rotor.*`; la spada e lo scudo di Kaiju, `blade` e `shield`; i pod),
  ingranditi con il mech; l'altezza della figura è quella del `.txt` (mech 3 e 3,4 m).
  Texture 256×256 nello sheet (1024×1024 a 24 bit; l'atlante della mappa in alto a
  sinistra; `WHITEN` schiarisce quella del mech di Rally). Le pilote dei mech sono due
  ragazze (richiesta dell'utente); niente modelli di Overwatch, nemmeno rinominati. I colpi provano la
  mesh (`hit3d`): la testa dei mech va sull'osso critico (`canopy`, `dome`).
  `OVERBIT_CLASSIC=1 make` (o `models.py --classic`): i corpi fatti di primitive.
- Test e reel per eroe: `build.py --hero <id>` (anche una lista per il reel), le cartucce
  `range-<eroe>.bm` di `make test-overbit`; `make overbit-reel-heroes` per il video.
- **bmhost** (`make bmhost`, `tests/host/`): il runtime vero delle cartucce sul PC; per
  vedere i frame (`--shots`), registrare (`--video`, `--wav`), input da script,
  `--clock-scale 21` per stimare i ms del Pi. `make test-overbit`, `make overbit-reel`.
- Prestazioni senza Pi: `tools/armprof.py` (istruzioni ARM per funzione con QEMU, serve
  `gcc-arm-linux-gnueabihf`) su `tests/bm/r3dbench.c` e `tests/bm/mapbench.c` (la mappa da
  un punto di vista); anche bmhost compilato per ARM Linux (un frame intero con il Lua).
  `BMHOST_SLOW=ms` fa scrivere a bmhost i frame più lenti. Il Pi resta il giudice: chiedere
  all'utente di fare il benchmark, i cui numeri arrivano come report nel branch `reports`
  (sezione *Report dei test*); le foto solo per ciò che il report non ha (l'overlay).
- La mappa (Partenope): `art/partenope.py` la costruisce con `mapgeo.py` (scatole, rampe,
  scale, pezzi di `geo.py`, quadri dell'atlante `maptex.py` con `decal`/`tex_quad`, nodi
  dei bot `nav_node`), `mapbake.py` la cuoce (facce nascoste, luce agli angoli, pezzi di
  8 m come modelli "lit", visibilità con `tools/mappvs.c`, grafo dei bot) e scrive
  `build/overbit/21_map.lua` (`World.MAP`); l'atlante è lo sheet della cartuccia (SHEET8).
  Conta i **triangoli**, non i pixel: dettagli nelle texture, `Mat(thin=True)` per le
  cose lunghe e sottili (non divise), `detail=True` per quelle piccole (solo vicino).
- La partita (`src/81_match.lua`, Controllo): regole in `RULES` (`--define
  'OVERBIT_RULES={...}'` di `build.py` per i test); i posti hanno un numero (`a.seat`:
  1-5 blu, 6-10 rossi), `step()` è un frame della partita.
- Bot (`src/75_bots.lua`): tattica (rete INT8 `Bots.BRAIN` in `src/76_brain.lua`, scritta
  da `art/brain.py` facendo giocare i bot in bmhost; le regole di `teacher` come riserva),
  strada sul grafo della mappa, mira e abilità per eroe. Dopo aver cambiato i bot o le
  regole: `python3 carts/overbit/art/brain.py build` (~20 min) e commit di `76_brain.lua`.
- Rete (`src/83_net.lua`): **lockstep**, ogni console simula tutto e viaggiano solo i
  comandi. Quindi nella partita: numeri a caso del gioco con `grandom()` (00_core; per gli
  effetti `random`), niente `G.frame` né cose della console (`G.local_actor`, la camera,
  la qualità) che cambino la simulazione, niente stato del gioco cambiato durante il
  disegno. `OVERBIT_NET_DEBUG` scrive lo stato ogni 10 frame per trovare dove due console
  divergono. Relay: `tools/overbit_relay.py`.

## Audio

- Sintetizzatore `src/audio/synth.c`; player dei banchi di suoni `src/audio/player.c`
  (sezione AUDIO del `.bm`, formato descritto in `player.h`); Sound editor
  `carts/sound/main.lua`, incorporato nel kernel come l'SDK.
- Il formato del banco esiste in tre posti: C (`au_parse`), Lua (l'editor) e Python
  (`scripts/bmaudio.py`). Se cambia, cambiarlo in tutti e tre: `make test-sound`
  controlla che il banco demo torni identico byte per byte.

## nano8 (M23)

- Emulatore delle cartucce `.p8` / `.p8.png`: la cartuccia `carts/nano8` (Lua: traduttore del
  dialetto, ambiente, input, interfaccia) e la macchina in C nel kernel (`src/bm/n8*.c`, la
  tabella `n8`; `src/audio/n8snd.c` per il suono). Il C è portabile: `make test-nano8` lo
  prova sul PC, `build/host/n8host` gioca nano8 sul PC con screenshot e WAV.
- Non è PICO-8: nome, logo e font sono nostri (`src/bm/n8font.c`), mai quelli di Lexaloffle.
- In `carts/nano8/roms` solo cartucce con una licenza che ne permette la ridistribuzione,
  elencate in `CREDITS.md` con il testo della licenza in `licenses/`.

## Yharnam (carts/yharnam)

- Gioco a 256×256: la risoluzione quadrata del formato (il firmware dà 480×270, il gioco
  disegna nel riquadro al centro, `bm_video_enter` in `runtime.c`). Città gotica infinita
  fatta a pezzi grandi uno schermo (16×16 tessere) fuori dallo schermo mentre si cammina
  (`timeslice`), le strade decise solo dalle coordinate. Luce a livelli come in Dank Tomb:
  `fades`, `dark_begin`, `glow`, `dark_end` (`g16_fade_*` in `gfx16.c`).
- La grafica è tutta in codice: `mkassets.py` (numpy, Pillow) con `art/` (`sdf.py` rende
  modelli 3D in pixel art: il cacciatore, gli oggetti; tessere e case in 2D) scrive
  `sheet.png` e il blocco `-- [atlas begin]` di `main.lua`. Dopo averla cambiata:
  rieseguirlo e fare commit di `sheet.png`. Prove: `make test-yharnam`, QEMU `test_yharnam`.
- Ordine e caso secondo la zona (richiesta dell'utente): in città (isolati di case, piazze, pire,
  cappelle: `urban`) gli incroci stanno sulla griglia e le strade corrono dritte, larghe 4, con un
  viale largo 6 ogni tre (sui confini dei quartieri, mai mancante); i lampioni stanno in fila sul
  marciapiede ogni `lsp` tessere (10; sui viali 12, su entrambi i lati sfalsati), lontani dagli
  incroci; i vicoli sono dritti; le case sono a schiera (una fila: stessa altezza e tetto, due
  facciate alternate, larghezze uguali, porta al centro, finestre specchiate, comignoli in coppia o
  al centro); nelle piazze bracieri e panche in coppie specchiate, attorno alla pira un anello a
  passo regolare. Vicino a parchi e cimiteri le strade ondeggiano e i lampioni sono sparsi; alberi
  e cespugli a boschetti con radure (`grove`). Nell'arena di un boss un quinto degli ostacoli in
  meno (fontane, pire, lampioni, ringhiere e bracieri restano).
- La città (richiesta dell'utente: zone ampie, strade non affollate): il pavimento delle strade è
  lo stesso in un quartiere di 3×3 isolati (`quarter_pave`); le varianti di una tessera vanno a
  chiazze (`patch` in `gen`), i cortili verso erba o terra a zone grandi, poche decorazioni;
  lampioni radi, pochi oggetti, slarghi agli incroci. La tessera 0 dello sheet è vuota: la
  cella 0 per `map()` non si disegna (prima lasciava quadrati neri nelle strade).
- Oggetti distruttibili (richiesta dell'utente: strade più vive e meno ingombre): barili, casse,
  pile di casse, panche e bare (`BRK` in `main.lua`; i resti in `art/props.py`, `BROKEN`). Si
  rompono con i colpi del cacciatore (`FOE.strike`), correndo, rotolando o con un passo rapido
  addosso (`walk_by`), con lo sparo (`BRK.first`: il primo sulla traiettoria, se prima non c'è una
  creatura), con i colpi e gli schianti delle creature, le palle dei fucili e le bombe, e i boss ci
  passano attraverso. Rotti smettono di bloccare (`col.off`), restano i resti a terra, schegge
  (particelle tipo 8 che cadono e restano un po') e polvere; restano rotti anche quando il pezzo
  di città si rifà (`BRK.gone`) e tornano interi quando la città si ripopola (`FOE.reset`: lampada,
  morte). Prove in `tests/yharnam/sim.lua`.
- Il fuoco (bracieri, pire) è un ciclo di fotogrammi in pixel art (`art/fire.py`, `FIRE_ANIM`),
  disegnato dopo la luce, più le scintille: pieno alla base, lingue che salgono. Le fiamme
  lasciano libere 40 delle `MAXP` particelle, e solo i fuochi visti ne emettono.
- La lampada del cacciatore (richiesta dell'utente): non una luce forte ma un'aura fredda,
  mistica: vetro e fiamma azzurro pallido (`SPIRIT` in `art/props.py`), un bagliore tenue che
  respira, particelle lucenti che salgono e luccicano (tipo 6; 7 fioche quando è spenta) e un
  alone di punti che gira. Sullo schermo niente nomi dei quartieri né "lamps k/2" (richiesta
  dell'utente): solo gli echi, e LAMP LIT quando se ne accende una.
- Le animazioni del cacciatore (8 direzioni) sono pose chiave in `art/anims.py` (lo
  scheletro e le sue articolazioni in `art/hunter.py`; `aim` gira il polso perché la saw
  cleaver punti dove serve). Renderle tutte richiede circa un'ora: `mkassets.py` tiene i
  fotogrammi in `build/yharnam-frames/` e ridisegna solo quelli cambiati. Nel gioco:
  `HUNT[nome].d[direzione][fotogramma]`; sul titolo X mostra tutte le animazioni.
- La saw cleaver (dalle foto e dalle immagini dell'utente, `art/hunter.py`): manico lungo e
  sottile di cuoio ad arco ampio (`HANDLE`, `ARCH`, corda chiara alle estremità), impugnato più
  vicino allo snodo che alla coda (`GRIP_AT`); snodo a disco con il gancio (`HOOK`); lama larga
  (`BLADE_L`, `BLADE_W`, un po' curva: `BEND`) fasciata di bende incrociate sul ferro scuro, denti
  su un lato, sangue secco. Proporzioni grandi come nelle immagini (lama circa un terzo
  dell'altezza del cacciatore). Chiusa (come la miniatura dell'utente): l'arma è girata nella mano,
  lo snodo dietro il pugno, lama e coda davanti, l'arco sopra la lama, così nei colpi la lama non
  rientra nel corpo; aperta (come l'illustrazione): la lama oltre lo snodo, la coda del manico
  dietro la mano; aprendola l'arma ruota nella mano (`cleaver` sotto `wield`: `wield` è dove la
  mano punta i colpi, lo usa `aim`). Quello che resta dietro la mano è tenuto fuori dal corpo
  ruotando l'arma sul suo asse fotogramma per fotogramma (`croll`, `anims.unclip`, misura in
  `hunter.intrusion`: zero alle pose di guardia). La scia dei colpi parte dalla fine della lama
  (`TIP`). Sparando, il braccio sinistro è teso e un po' alzato (`AIM` in `anims.py`).
- Le creature (12 nemici e 4 boss, uno per tipo: villici, bestie, cacciatori, orrori) sono in
  `art/foe_*.py`; scheletri (umanoide, quadrupede, ragno), pose chiave, `reach` per la seconda
  mano su un'asta, `aim` e il registro `Creature` in `art/rig.py`; colori e pezzi comuni in
  `art/foeparts.py` (lo sheet tiene al più 255 colori: riusare le rampe). Sono disegnate in 5
  direzioni (S SE E NE N) e specchiate nel gioco per SW W NW, perciò la luce viene dall'alto
  (`sdf.LIGHT_TOP`). Nemici: idle, walk, attack, hurt, death; boss: in più 2 attacchi speciali
  e 2 combo di due colpi (eventi `hit`, `fire`, `throw`, `slam`, `howl`, `beam`). Nel gioco
  `FOES` (l'atlante) e la tabella `FOE` (comparsa per quartiere, IA, colpi, boss).
- Il combattimento come in Bloodborne (`update_play`, `FOE.strike` / `stagger` / `visceral`):
  stamina (colpi, schivate, corsa), A colpo rapido in combo, R1 pesante (tenuto: caricato; alle
  spalle fa barcollare), Y dopo un colpo trasforma la saw cleaver in un colpo (`trick`) e la
  combo continua nell'altra forma, B toccato schiva (col lock-on L1 passo rapido, senza
  capriola, fermo backstep; tenuto corre), X spara: durante la carica di un nemico è il parry
  (barcolla), poi A è il visceral. Rally, i boss barcollano quando i colpi si sommano (poise).
  Le due forme della saw cleaver (`BLADE`, decisione dell'utente): chiusa un po' più rapida,
  colpi leggeri ed economici, la stamina torna prima (più DPS: kiting, tanti colpi in poco
  tempo); aperta più lenta e pesante (più danno per colpo, soprattutto caricato: colpire al
  momento giusto e ritirarsi).
  L1/R1 si leggono con `pad()`; dalla seriale sono `u` e `o` (kernel), dalla tastiera Q ed E.
- Come combattono le creature (richiesta dell'utente: non solo numero di colpi; `AI` nel blocco
  `FOE`): ogni nemico ha uno stile (`AI.STYLE`: distanza che tiene, giri attorno al cacciatore,
  zigzag, carica, pausa tra i colpi, caricamento lento, colpo trattenuto in alto con l'arma che
  luccica, colpo rapido, catene, ritirata dopo il colpo, schivata dei colpi del cacciatore,
  contrattacco quando lui è scoperto). I boss hanno tre fasi secondo la vita (`AI.BOSS`: sopra
  due terzi, sopra un terzo, l'ultimo): semplici all'inizio, poi più rapidi, con più mosse e
  sequenze `"a+b"`; si entra in una fase con un ruggito (il Hound ulula), un momento per colpire.
  I valori delle creature più in là nella caccia crescono un po' (`o.agg`). `tests/yharnam/foes.lua`
  (in `make test-yharnam`) misura ritmo, pause, caricamenti, distanza, movimento, ritirate,
  schivate e contrattacchi di ogni creatura e di ogni fase dei boss, e controlla che tutto sia
  leggibile (almeno 8 tick di caricamento), che non ci siano due creature uguali e che i boss
  crescano di fase in fase.
- La caccia (richiesta dell'utente: non si va all'infinito in una direzione): zone di `AREA` ×
  `AREA` pezzi verso est, chiuse da un muro di nebbia (`inside`, `draw_edge`); in fondo a ogni
  zona l'arena del boss (`boss_chunk`), ucciso il quale si apre la zona dopo (`G.open`). Due
  lampade del cacciatore per zona (`lamp_chunk`, prop `shrine`): una a metà, una prima del boss; si
  torna all'ultima accesa. Echi (`G.echoes`) dai nemici uccisi, da spendere con parsimonia: Select
  (Tab) cura poco per `HEAL_COST`, la morte costa `DEATH_COST` e senza abbastanza echi la caccia è
  perduta (stato `lost`). Per ora (decisione dell'utente) le zone sono 4: ucciso il quarto boss,
  sparito il suo annuncio, la caccia finisce (`G.done`, stato `end`: tempo, uccisi, morti, echi;
  A torna al titolo).
- Una regione per zona (richiesta dell'utente; `REGION` in `main.lua`, `REGION.of(cx)`): i quartieri
  che la fanno (pesi), il pavimento dei suoi quartieri, il tipo dell'arena del boss. 1 Yharnam
  centrale (strade, piazze, pire; arena pira, Butcher); 2 il bosco (`wild`: `woods` e `clearing`,
  qualche cimitero; sentieri di terra `PV_EARTH` senza cordoli, niente marciapiedi ma erba, alberi
  e cespugli a boschetti, nelle radure un fuoco con due panche, un po' di luce di luna:
  `AMBIENT` + 1; arena radura, Hound); 3 Cathedral Ward (cimiteri, cappelle; arena cimitero,
  Father); 4 il quartiere proibito (cappelle; arena cappella, Watcher). Niente nemici nuovi nel
  bosco (decisione dell'utente): `POOL.woods`/`clearing` e i `POOL[tipo .. regione]` riusano i 12.
  Una strada tra due regioni è di quella a ovest. Gli alberi sono sprite grandi: un bosco disegna al
  più quanto la città (più cespugli che alberi; misura: sprite pixel nel test).
- Le vie della lampada (`PATHS`, decisione dell'utente): al massimo 4 (`SLOT_COST`), anche la
  stessa più volte. Bilanciamento (richiesta dell'utente: build puntate su un aspetto, con
  varianti): la prima via presa è quella del cacciatore e pesa di più; ripresa conta ogni volta
  meno ma ancora in modo visibile (`curve`, diversa per via); le altre vie pesano circa un terzo,
  meno quante più sono (`DISCORD`): un mix vale meno di una via seguita (`path_weights`).
  Alla lampada un rombo per ogni volta che una via è stata presa: dorati quelli della prima.
  Nomi evocativi e descrizioni senza numeri; gli effetti non si vedono sulle barre (stessa
  misura): agiscono su `P.mods` (`apply_paths`). Feral Affinity: meno danno subito; Moonlit
  Breath: stamina spesa meno e recuperata prima; Quicksilver Rite: danno della pistola, finestra
  del parry (in tick dopo il colpo), barcollare più lungo; Serrated Oath: danno della saw
  cleaver aperta; Hunter's Path: lama chiusa più rapida, combo prima e meno stamina per i suoi
  colpi. `tests/yharnam/balance.lua` (in `make test-yharnam`) misura col codice del gioco colpi
  per uccidere un cittadino e due boss, colpi subiti, colpi in 4 s, DPS, stamina e pistola per
  ogni via e per dei mix, e controlla queste regole: rieseguirlo dopo ogni ritocco.
- I comandi non sono sullo schermo: Start apre la pausa, con la pagina Controls (icone di
  `prompt()` secondo `lastinput()`).
- Video di una caccia: `make yharnam-video` (`build/yharnam-run.mp4`, serve ffmpeg). `tools/bmplay/bmplay.c`
  fa girare la cartuccia sul PC con il disegno (`gfx16.c`, luci comprese) e il suono (`synth.c`,
  `player.c`) della console, i tasti premuti da un bot in Lua; `yharnam_bot.lua` gioca dal titolo
  al Butcher ucciso (strade, lampade, lotta, parry, visceral); la run è sempre la stessa (seme e
  bot). `BOT_TRACE=1` stampa come va.
- Lo sheet è largo 4096 (skyline, fotogrammi uguali tenuti una volta). La cache delle creature
  dipende dal codice (non dai commenti né dagli import) di `rig.py`, `foeparts.py` e del loro
  modulo; quella del cacciatore da `hunter.py`; tutte da `sdf.SDF_VERSION` (aumentarlo se cambia
  il modo di disegnare). `sdf.render` valuta ogni primitiva solo dove la sua sfera può arrivare
  (stessi pixel, da 2 a 5 volte più veloce); il render completo delle creature richiede circa
  un'ora. Con `YH_DRAFT=1` i fotogrammi mancanti diventano segnaposto, per provare il gioco
  intanto (non fare commit di quello sheet).

## Assistente AI (M30)

- `src/ai/`: rete INT8 che sceglie tra le voci di `src/ai/kb/*.txt` (formato in
  `src/ai/kb/README.md`), ricette di sprite (`sprite.c`) e ricette 3D (`mesh.c`,
  `mesh_chars.c`: forme, oggetti, persone, animali e macchine con scheletro e animazioni,
  `kind: mesh` in `kb/meshes.txt`, API `ai.mesh`); pannello Lua `require "assist"`
  (modo `mesh`: il modello gira nel pannello). bm Studio e bm Animator lo aprono con F6
  (pad: Y + X) da `bm3d.lua` (`T.assistant`, `T.take_model`): il modello entra nel
  progetto con scheletro e animazioni. Per guardare le ricette sul PC: `make
  build/host/meshview && build/host/meshview sheet out.ppm` (tutte) o `meshview one
  mech out.ppm` (una, da quattro lati e nelle pose); guardarle dopo ogni modifica.
  Una faccia si vede dal lato in senso orario: i primitivi passano per `face_out()`
  con un punto interno al solido. Un'unità = un blocco; il modello guarda verso −z.
- **Linguaggio delle parti** (`src/ai/mesh_script.c`, una riga per primitivo, la grammatica
  in testa al file; `ai.script(testo)` in Lua, `meshview script FILE OUT.ppm` e `meshview
  json FILE OUT.json` sul PC). **img2mesh** (`tools/img2mesh.py`): un'immagine diventa un
  modello: Claude (API Anthropic, `claude-opus-5-5`, SDK `anthropic`) guarda l'immagine e
  scrive lo script, `meshview` lo costruisce e lo disegna, i render tornano al modello per
  due giri di correzione, il `.bm` esce con MESH e ANIM (`bmmesh.encode_faces`,
  `encode_anim`, `decode_anim`; `mkbm.pack` o le sezioni di una cartuccia esistente).
  Esempio e test: `tests/ai/img2mesh/mech.txt` (il mech della ricetta nel linguaggio),
  `tests/ai/img2mesh/replay/` (risposte registrate: `make test-img2mesh` non chiama l'API),
  `test_img2mesh` in QEMU. Serve `ANTHROPIC_API_KEY` o un profilo `ant auth login` solo per
  usarlo davvero.
- **meshy2mesh** (`tools/meshy2mesh.py`, chiave `MESHY_API_KEY` nell'ambiente, mai nei
  file): image-to-3D di meshy.ai → `.glb` → `.bm` (texture nello sheet della cartuccia
  nuova, colori piatti in una esistente o con `--flat`, il riduttore sopra `--max-tris`).
  `--glb` converte un `.glb` qualunque. Test: `tests/ai/check_meshy.py` (in `make
  test-img2mesh`), `test_meshy2mesh` in QEMU. La rete di questo ambiente nega
  `api.meshy.ai`: la chiamata vera si prova dal PC dell'utente.
- **Riduttore di poligoni** (`src/bm/decimate.c`, C portabile, niente AI): collasso degli
  spigoli con le quadriche (Garland-Heckbert), mezzo spigolo (i vertici restano quelli del
  modello, le ossa seguono), bordi, linee di colore e cuciture della texture tenuti con un
  piano attraverso lo spigolo, nessuna faccia rovesciata. Kernel: `mesh_reduce(record,
  triangoli, ossa)` in `runtime.c`, `T.reduce_model` in `bm3d.lua`, tasto `-` nella pagina
  models di bm Studio. PC: `build/host/libbmdecimate.so` via ctypes (`scripts/bmdecimate.py`),
  `tools/bmreduce.py`, e meshy2mesh sopra `--max-tris` (la griglia resta solo oltre i limiti
  del formato). Test: `tests/bm/test_decimate.c` (in `make test-bm`), lo stand-in Lua in
  `tools3d_host.lua`, `test_mesh_reduce` in QEMU.
- **Modello da un'immagine sulla console** (`picture3d` in `runtime.c`): `src/net/img3d.c`
  parla col servizio (tabella dei fornitori: nome, indirizzo, nome della chiave in
  `bm/config.txt`; Meshy per primo: POST `/image-to-3d` con l'immagine in base64, GET dello
  stato, download del `.glb`) sopra `http.c`/`tls.c`; `src/bm/glb.c` legge il `.glb`
  (`json.c` parser JSON minimo, `jpeg.c` decodificatore JPEG baseline, `png.c` il PNG di
  nano8 spostato lì) e dà il record MESH con la texture 256×256, il gemello a colori piatti
  e il modello ridotto con `decimate.c`. In Lua `T.picture_chooser`/`T.picture_update` in
  `bm3d.lua` (tasto `m` della pagina models, voce "Model from picture..." del menu; le
  chiamate bloccano: il messaggio si mostra il fotogramma prima). Test: `make test-img3d`
  (servizio finto in Python, `tests/net/run_img3d_test.py`), `tests/bm/run_glb_test.py` in
  `make test-bm` (il `.glb` di `tests/ai/glbfix.py`, PNG e JPEG), lo stand-in in
  `tools3d_host.lua`, `test_picture_model` in QEMU (senza rete: il messaggio). Dalla
  seriale di QEMU `p` è l'overlay delle prestazioni: non usarlo come tasto delle app.
- **Contorno → modello, senza AI** (`src/bm/cutout.c`, C portabile): maschera (alpha, o il
  colore degli angoli), griglia di 96 celle, via i frammenti sotto 1/50, contorni esterni
  seguiti sugli spigoli delle celle (i buchi si riempiono), Douglas-Peucker (almeno una
  cella), ear clipping; estrusione (`depth` frazione dell'altezza) o tornio (`segments`);
  `face()` gira ogni triangolo perché la normale destrorsa guardi *via* dall'esterno (così
  la console lo mostra). La texture: il riquadro dell'immagine sullo sheet, i pixel di
  sfondo accanto alla figura prendono il colore vicino (il contorno corre sugli angoli
  delle celle). `glb_pack()` in `glb.c` impacchetta record, gemello piatto e texture per
  tutti e due. Kernel: `cutout3d` in `runtime.c`; bm Studio `m` → scelta del modo (cutout,
  lathe, meshy.ai) → immagine; il calcolo va al fotogramma dopo il messaggio. PC:
  `build/host/libbmcutout.so` (ctypes `scripts/bmcutout.py`), `tools/cutout2mesh.py`.
  Test: `tests/bm/run_cutout_test.py` (lecca-lecca su sfondo trasparente e bianco: chiuso,
  alto 2, l'immagine davanti nel render; il tornio chiuso e tondo) in `make test-bm`,
  `test_picture_model` in QEMU (il ritaglio fatto sul kernel ARM e salvato).
- **local2mesh** (`tools/local2mesh.py`): modelli aperti image-to-3D sul PC dell'utente
  (TripoSR, Hunyuan3D 2: `--install` clona e fa il venv in `~/.bm/local3d`; `--backend
  command` per qualunque strumento che scriva un `.glb`), poi `meshy2mesh.convert` e
  `write_cart`. Qui non si provano i backend veri (niente GPU, huggingface negato):
  `tests/ai/check_local2mesh.py` (in `make test-img2mesh`) usa un comando finto che scrive
  il `.glb` di `glbfix.py`.
- Dopo aver cambiato la base di conoscenza: `make ai-model` (numpy) e commit di
  `src/ai/assist.weights`; `make test-ai` controlla C contro Python, domande di prova,
  esempi di codice e pannello. Il tipo `guide` (2026-10-04, `kb/guide_sdk.txt`): come fare un
  gioco 2D o 3D con l'SDK, a passi; il modo `guide` del pannello le mette prima (bit 128 di
  `AI_KIND_*`: i tipi sono 8, il prossimo vuole un `kmask` più largo). I commenti `#` solo in
  cima a un file della base: dopo un `text:` o un `code:` diventano parte della voce.
- bm Code (`carts/code/main.lua`, scheda Dev): l'editor del codice; usa `cart_read` /
  `cart_write` (solo il codice), `font("6x12")` e `assist.act` per le righe `#entry:`.
  Test: `test_code_editor` in QEMU (lo schermo si legge anche col font 6x12).

## Completamento delle parole (M30)

- `src/ai/predict.lua` (`require "predict"`): n-gramma che finisce la parola scritta; in
  bm Code il resto in blu-grigio, Tab lo scrive (verde fino al tasto dopo), anche in
  Trova/Sostituisci e nella domanda del pannello (`assist.lua`). Guida `docs/PREDICT.md`.
- Il dizionario segue il cursore (`place_at` in bm Code): codice → `"lua"` + nomi della
  scheda; dopo `--` e nelle stringhe `"it"` / `"en"` (menu); `#entry:` e pannello
  `{it = 1, ask = 2}`.
- Dizionari: `scripts/mkwords.py` → `build/words.lua` (`require "words"`, nel kernel) dai
  testi di `src/ai/words` (scritti per bm, niente testi con licenze altrui), dal Lua dei
  giochi e dalle API della base di conoscenza.
- `make test-predict` (in `make test`), `make predict-bench`, `make syllables`; in QEMU
  `test_code_completion`. La vecchia scrittura ad accordi (modi facile/sillabe/steno) è
  archiviata nel branch `archive/pad-typing`; quella di adesso è la sezione dopo.

## Scrittura col pad (branch `assistive-typing`, 2026-10-05)

- `src/ai/padtype.lua` (`require "padtype"`, nel kernel, in bmhost e nella RGB30), guida
  `docs/PADTYPE.md`. Regole dell'utente (le tabelle `CROSS`, `FACE`, `KB` in cima al file
  si cambiano lì): **composizione rapida** con solo su/giù/destra/sinistra (le diagonali
  non scrivono); una freccia aspetta X ms (`delay`, 250) la sua doppia, che dà la
  consonante gemella (↑ t, ↑↑ d; mai la stessa due volte); la predizione completa la
  sillaba (consonante + doppia + h + vocali dal dizionario di `predict`); □ / △ la girano
  (poi la consonante sola, a e i o u, le accentate per ultime); L2, R2, L2+R2 altri
  livelli; L1 / R1 indietro / avanti, L1 + R1 tenuti a capo; ✕ spazio, ✕✕ punto, ○
  cancella l'ultima cosa scritta, R2 + ✕ □ △ le tre parole; L3 maiuscola, R3 la tastiera
  per un carattere. **Share** passa alla **tastiera su schermo** (un tasto alla volta con
  la croce) e torna (`fallback = "off"`: spegne, come in bm Code). L'**overlay** del
  controller (`pt.draw`, 340×132) mostra le sillabe delle frecce, i tasti del livello
  tenuto e le parole.
- Consonanti per frequenza come inizio di sillaba (it ½, en ¼, Lua ¼: n t r s l c d p il
  69%), la doppia gemella (t/d, n/m, r/l, s/c), una famiglia per direzione nei livelli;
  L2+R2 i numeri (0 è ○, il 9 è l'8 girato con □: anche le cifre girano).
- `pt.coach(host, testo)`: il prossimo tasto per scrivere un testo (l'esercizio e i test);
  `pt.text_host(lang)`: un host su una stringa (in Lua rientra dopo `then`/`do`/`{`, e
  `end`/`else`/`until` tornano indietro). Nel codice `pt.align` confronta come se `end`
  fosse già rientrato.
- **bm Code**: Share accende e spegne (anche il menu, *Pad typing*); `pad_host` modifica la
  scheda con `edit_key` (la cancellazione è esatta, anche tra le righe), la lingua segue
  `place_at`; overlay sotto il codice, `PAD compose lua` nella riga di stato, al cursore la
  pressione che aspetta (gialla), la sillaba che gira (azzurra, niente sottolineature: i
  test leggono lo schermo), il resto della parola (blu-grigio). `pt.idle` quando un menu ha
  il pad.
- **Pad Typing** (`carts/typing`, scheda Games, copertina in `scripts/mkcovers.py`):
  esercizio in italiano, inglese, Lua (otto testi a lingua in `pt.TEXTS`, o scrittura
  libera), il prossimo tasto sotto il testo, caratteri al minuto e pressioni a carattere
  contro i record (`save()`). I testi sulla griglia del font 8×16.
- Test: `make test-padtype` (in `make test`: le regole, tutti i testi scritti fino in fondo
  dal dattilografo simulato, it 1,09 / en 1,06 / Lua 1,33 pressioni a carattere contro
  3,7–4,0 della tastiera su schermo; poi Pad Typing in bmhost con `tests/padtype/script.lua`,
  fotogrammi in `build/padtype/`), QEMU `test_pad_typing` (DS4 simulato: Pad Typing e Share
  in bm Code). Dopo aver cambiato `padtype.lua` va ricompilato bmhost (lo incorpora).

## Mouse e puntatore (M32)

- `src/kernel/pointer.c`: il puntatore di sistema (mouse USB/Bluetooth, levetta destra dei
  pad). Decisioni dell'utente: si spegne per tutto il sistema solo con `mouse=off` in
  `bm/config.txt` (nessuna voce nel menu); c'è nel menu di bm e nelle app solo se lo
  chiedono (`mouse(true)`); nascosto se niente lo muove; icona bianca senza numero, pallino
  blu per il Bluetooth.
- `ble.c` tiene tastiera e mouse LE insieme: `le` punta al dispositivo in lavorazione, le
  funzioni chiamate da `bt.c` lo scelgono (per handle) e lo rimettono com'era.
- Test: `make test-usb`, QEMU `test_usb_mouse`, `test_mouse_cart`, `test_bt_mouse`,
  `test_bt_mouse_classic`, `test_stick_pointer` (il tablet di QEMU si muove via QMP:
  `Qemu.pointer()`, `Qemu.click()`).

## GPU (M33)

- Il 3D dei giochi lo disegna la **GPU** (backend V3D `src/gpu/gpu3d.c`, sotto
  `src/bm/r3d.c`), verificata sul Pi il 2026-10-01. Il rasterizzatore software di r3d
  resta: con `gpu3d=0` in `bm/config.txt`, in QEMU (che non ha la V3D) e da solo se la
  GPU non risponde. Sul PC la GPU si prova con l'emulatore `tests/gpu/v3d_emu.c`
  (`make test-gpu3d`) e il driver con `make test-v3d`; sul Pi con il test `g` del
  monitor e le righe GPU dello stress test.
- Gli shader QPU si scrivono in `tools/qpuasm.py`, che genera `src/gpu/shaders.h`
  (`make test-qpu` controlla che sia aggiornato).
- M34 (in corso): cose della V3D non documentate o non usate da Mesa (layout T-format
  delle texture, load della pagina in un tile MSAA) le **impara la prova all'avvio** di
  `gpu3d.c` e, se non tornano, si spengono da sole; l'emulatore ne ha le varianti
  (`make test-gpu3d` le prova tutte). L'MSAA è spento di default (`gpu3d_aa=1`).
- Numeri: nel branch `3d-performance` erano M30 e M31; sul branch principale M30 è
  l'assistente, M31 il Pi Zero 2 W e M32 il mouse, quindi la GPU è M33–M37, Overbit M38
  (era M31 su `3d-performance`) e il prossimo driver M39 (bm3d 5.x, `docs/ROADMAP.md`).
- Il backend disegna anche ombre (nere a retino, con la prova dello z), effetti 3D, facce a
  retino (shader con discard) e texture dei modelli "lit" (luce e nebbia sugli angoli,
  shader `TEX_RGB`). Solo le facce con texture e a retino insieme chiamano
  `r3d_t.arm_hook`: la GPU disegna ciò che ha in coda e la cartuccia passa all'ARM
  (`gpu3d_to_arm` in `runtime.c`, una riga nel log, il fotogramma misto non si mostra).
  `stat(9)` vale 1 se il 3D lo fa la GPU; `gpu3d([on, aa, vs])` lo cambia dalla cartuccia.
- **Risoluzione della cartuccia** (`screen(w, h)`, bm3d 4.2): cambia tra due fotogrammi
  (`screen_apply` in `runtime.c`), modi 16:9 da 320×180 a 1920×1080; `gpu3d.c` ha i buffer
  per 1080p e la guard band che si stringe (`set_guard`); con la GPU `cls()` lo fa il
  lavoro della GPU (`cls_settle`: l'ARM riempie solo se prima del 3D arriva del 2D, una
  lettura o la pagina va mostrata). Prova: `test_screen_modes` in QEMU.
- M36 (in corso): le mesh le mette il **vertex shader** (`vs_baked`, `vs_tex_rgb`, `vs_lit`,
  `cs_colour` in `tools/qpuasm.py`; copie degli angoli in `gpu3d.c`, `mesh_get`, gruppi
  per osso con un blocco di uniform a osso). Livello 1: lo scenario (spente o con la luce
  agli angoli, `clight`); livello 2: anche i modelli illuminati dal sole, con le ossa
  (gli eroi: `light_fast` di r3d in `vs_lit`, che va cambiato insieme). La GPU taglia
  quelle che passano il piano vicino (flag 4 del record, `CLIPPER_*`, `VIEWPORT_OFFSET`
  al centro). Spento di default: chiave `gpu3d_vs` (0/1/2), *Graphics > 3D vertices*,
  renderer "GPU+VS1"/"GPU+VS" di Overbit; le prove all'avvio (`probe_gl`, `probe_clip`,
  `probe_lit`) lo spengono se il Pi non disegna come l'emulatore, che esegue gli shader
  (interprete QPU) e taglia come GL; passo 14 del test `g`.
- **Versioni dei driver 3D**: `bm3d X.Y` (X il blocco/milestone, Y il passo) in
  `src/gpu/version3d.h` e `docs/DRIVERS.md`; ogni passo che cambia quello che r3d o
  gpu3d sanno fare alza la versione e aggiunge una riga alla tabella. Le impostazioni
  riproducono le versioni vecchie (ARM 0.2, GPU 2.1, GPU+VS1 3.0, GPU+VS 3.4, coda 4.1): così i
  benchmark le confrontano.
- **Fotogramma in coda (M35, `gpu3d_queue`, spento di default)**: il lavoro della GPU parte e
  l'ARM va avanti (il `_update` dopo). Il 2D disegnato mentre la GPU ha un lavoro sulla
  pagina si registra (`draw2d()`/`d2` in `runtime.c`) e va sulla pagina dopo, nello stesso
  ordine: una funzione Lua nuova che disegna sulla pagina passa da `draw2d()` (o da
  `sync3d()` se legge la pagina), una che cambia ciò che il 2D registrato legge (sheet,
  mappa) chiama prima `flush3d(1)`/`sync3d()`. `zclear()` resta nel lavoro (`fs_zclear`).
  `make test-queue2d` confronta i fotogrammi con la coda accesa e spenta.
- **3D Bench** (`src/bm/b3d.c`, *Dev > 3D Bench*, monitor `j`, `docs/BENCH3D.md`): ogni
  test 3D con ogni profilo (ARM 0.2, GPU 2.1, GPU+AA, GPU+VS1 3.0, GPU+VS 3.4), carico
  fino a 40 ms, 60/30 fps, statistiche (istruzioni e cache miss dai contatori
  dell'ARM1176, `src/kernel/pmu.c`, solo sul Pi), grafico a barre con le misure di prima,
  report in `bm/bench` sulla SD confrontato col giro dopo. Un test nuovo per ogni
  capacità nuova dei driver; quelle future stanno nella lista come "non ancora".
  `make test-b3d` lo prova sul PC.
- Overbit va sulla GPU (menu "3D": GPU, GPU+AA, ARM; benchmark dei bot con `--start
  bench`, `84_bench.lua`). bmhost ha gli stub della GPU; `make bmhost-gpu` usa `gpu3d.c`
  sull'emulatore della V3D (`BMHOST_EMU_SKIP=1`: i lavori non si eseguono). Quanto costa
  all'ARM un fotogramma della partita: `tests/overbit/frames.py` (qemu-arm, per funzione).

## Aggiornamenti dal Pi (M19)

- Release firmate (CI sui tag `v*`, `scripts/mkrelease.py`, chiave `keys/release-pub.pem` /
  `scripts/release-key.sh`, secret `BM_RELEASE_KEY`; la RGB30 ha un manifesto suo,
  `manifest-rgb30` con `kernel8.img`, nella stessa release, e *Settings > Updates*;
  `scripts/release.sh vX.Y.Z` fa tutta la
  procedura da WSL, anche il kernel con la chiave sulla SD prima del tag); sul Pi `src/kernel/update.c`:
  Settings > Updates > *Check for updates* / *Install the update*, monitor `u`. Niente si
  scrive finché tutti i file non sono scaricati e controllati; i kernel di prima vanno in
  `/bm/backup`; quello della scheda si scrive per ultimo. Prova: `test_update` in QEMU
  (`update_url=sd:/release/`, `bm/release.pem`).

## Market (M25; nato nel branch `bm-store`, unito al principale il 2026-10-04)

- Prima scheda del menu: **Market | Games | Dev | Lib | Settings** (tasti 1–5); il menu si
  apre su Games. Catalogo dal repository pubblico `f-accomando/bm-market` (GitHub Pages),
  modello in `market/`, `make market-seed MARKET=../bm-market` ci mette i giochi del progetto.
- Decisioni dell'utente (2026-10-01): repository dedicato; tutti i giochi scaricabili (per
  ora restano anche nell'immagine della SD); il market **non blocca il menu** e **non carica
  niente quando la scheda non è attiva** (segnaposto finché le risorse non arrivano);
  pubblicazione con pull request e, più avanti, token dal Pi; P2P solo in rete locale (M24).
- `src/net/catalog.c` (portabile, `make test-catalog`): firma ECDSA P-256 con la chiave
  del market (`keys/market-pub.pem`, `scripts/market-key.sh`; non quella delle release),
  record, SHA-256 dei file. `scripts/mkmarket.py` fa il catalogo (stesso formato).
- `src/kernel/market.c`: i lavori (cache, catalogo, copertine, download) girano in una
  fibra (`src/kernel/fiber.c`, `src/arch/fiber.S`) nel tempo libero del frame
  (`menu_view_t.idle`); `net_wait_step` cede il controllo dentro una fibra e restituisce -1
  se è stata annullata: ogni attesa di rete nuova deve controllarlo.
- Le cartucce non incorporate scrivono solo `.bm` in `/carts` (`write_refused` in
  `runtime.c`); gli strumenti incorporati passano da `carts_tool_session` (`bm_set_tool`).
- Test: `test_market` in QEMU con `market_url=sd:/market/` e `market_delay` (in QEMU non c'è
  rete); `bm/market.pem` sulla SD aggiunge una chiave.
- Rete locale (M24): `src/net/lan.c` (annuncio UDP 3335, TCP 3336, domanda al giocatore,
  SHA-256; portabile, `make test-lan`), acceso da `market_lan()` solo con la scheda Market
  o il pannello di invio aperti.
- Pubblicazione dal Pi: `src/kernel/publish.c` (pannello nelle opzioni dei giochi) e
  `src/net/github.c` (API REST di GitHub, portabile: `make test-github` con un finto
  server); token `github_token`, market `market_repo`, API `github_api` in `bm/config.txt`.

## Comunicazione con l'utente

- Riportare la **lista delle milestone** solo quando una milestone è completata per
  intero (non per i singoli passi): una lista puntata (niente tabelle, niente icone),
  una descrizione breve e lo stato di ciascuna. Le milestone chiuse non vanno nella lista
  (decisione dell'utente, 2026-10-04), tranne quelle chiuse nell'ultima sessione, barrate
  (`~~M12 — ...~~`). Dopo la lista, a parte, una riga **In corso** con le sole milestone su
  cui si sta lavorando (quelle "in corso" in `docs/ROADMAP.md` o toccate nelle ultime
  sessioni), per distinguerle da quelle aperte ma ferme o ancora da fare.
