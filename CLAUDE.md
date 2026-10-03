# bm (BareMetal) — note per chi lavora su questo repository

Kernel bare metal per Raspberry Pi Zero W (BCM2835, ARM1176JZF-S) e, con una seconda
build (`kernel7.img`), per il Pi Zero 2 W (BCM2710A1, Cortex-A53 a 32 bit): C + assembly +
Lua 5.4 embedded. Documentazione: `README.md` (presentazione in inglese, con showreel e
screenshot in `docs/img/`), `README_OLD.md` (il README completo, in italiano),
`docs/ROADMAP.md`, `docs/HARDWARE.md`.

## Build e test

- `make` → `build/kernel.img`, `build/kernel7.img` e `build/chainloader.img` (toolchain
  `arm-none-eabi-gcc`).
- `make test` → test sul PC (grafica, FAT, USB, audio, rete, giochi) + test end-to-end in
  QEMU (`-M raspi0`); `make test-zero2` gli stessi con `kernel7.img` (Pi Zero 2 W) in
  `-M raspi2b`.
- L'utente prova sul Pi reale copiando `dist/kernel.img` (e `dist/kernel7.img`) sulla SD
  (WSL, `/mnt/d`), senza cavo seriale: tutto ciò che deve verificare va mostrato sullo
  schermo.
- Dev kit (richiesta dell'utente): l'overlay delle prestazioni sopra ogni `.bm` (`perf_frame`
  in `runtime.c`): fps, ms di `_update` + `_draw`, istruzioni Lua del fotogramma
  (`stat(10)`; prima del merge con `3d-performance` era `stat(6)`, ora il tempo del 3D),
  i massimi dell'ultimo secondo e il grafico degli ultimi 64 fotogrammi. Settings > System >
  "Performance overlay" (config `perf`), F3 (non in modalità testo), `p` dalla seriale.
- Arrivati dal gioco Yharnam (branch `claude/yharnam`; il gioco va nel market): la risoluzione
  quadrata 256×256 del formato (il firmware dà 480×270, il gioco disegna nel riquadro al centro,
  `bm_video_enter` in `runtime.c`; `--res 256x256` in `mkbm.py`), la luce a livelli come in Dank
  Tomb (`fades`, `dark_begin`, `glow`, `dark_end`; `g16_fade_*` in `gfx16.c`), lo sheet con
  palette nel Makefile (`sheet8_<gioco> := 1`), L1/R1 di `pad()` dalla seriale (`u`, `o`) e dalla
  tastiera (Q, E). Prova in QEMU: `test_square_lights`. `tools/bmplay` (`make
  build/host/bmplay`) gioca una cartuccia sul PC col disegno e il suono della console e un bot
  in Lua; `video.sh` ne fa un video.

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

## Nome

- Il progetto si chiama **bm** (BareMetal); cartucce `.bm`, cartella `bm/` sulla SD.
  Il vecchio nome sopravvive solo dove serve alla compatibilità (la cartella della SD e
  l'intestazione delle cartucce di prima, lette ancora; il tag di rete per i kernel
  vecchi in `tools/bm_net.py`). Il repository GitHub è `f-accomando/bm`.
- Il branch principale è `claude/bare-metal-mvp`: quando l'utente dice "main" intende
  quello (un branch `main` non esiste).

## Branch delle sessioni

- Quando una nuova sessione comincia uno sviluppo specifico (una funzione, un gioco, un
  passo di una milestone), prima di modificare i file chiedere all'utente il nome del
  branch, proponendone uno breve legato allo sviluppo (es. `claude/m19-aggiornamenti`).
  Il branch parte da `claude/bare-metal-mvp` aggiornato e commit e push vanno lì, anche se
  la sessione ne ha assegnato un altro.
- Non serve chiederlo per domande, letture della roadmap o lavoro che l'utente ha già
  indicato su un branch esistente (es. "fai commit su bare-metal-mvp").
- Gli spunti **R1, R2, …** (in fondo a `docs/ROADMAP.md`) sono idee non ancora decise:
  quando l'utente ne nomina uno ("facciamo R7") il significato è lì.

## Pi Zero 2 W (M31): `kernel7.img`

- Gli stessi sorgenti compilati una seconda volta per ARMv7 a 32 bit (`ARCH7` nel
  `Makefile`, oggetti in `build/k7/`, `-DBM_ZERO2`): `build/kernel7.img`. Sulla SD stanno
  `kernel.img` e `kernel7.img`; `config.txt` sceglie (`[pi02]`). Il codice specifico va
  sotto `#ifdef BM_ZERO2` (indirizzi del SoC, `src/drivers/mmio.h`) o `#if __ARM_ARCH >= 7`
  (istruzioni ARMv7: barriere, cache, HYP); ogni modifica deve compilare in tutti e due.
- Sul Zero 2 W: periferiche a 0x3F000000, avvio in HYP, un solo core (gli altri nello stub
  del firmware a 0x0: mai scrivere lì), LED sul GPIO 29 (il 47 è l'I2C dell'alimentatore),
  BT_ON GPIO 42, firmware del CYW43436 (`board.c`, `wifi.c`, `bt.c`).
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
- Menu 512×512 (`ui.c`), giochi `.s16` (formato da definire), `.bm` nascosti salvo `show_bm=1`.
  Nei test lo schermo si legge dai pixel: il testo del menu sta sulla griglia del font (1x a
  multipli di 8×16, 2x a multipli di 16).
- L'utente prova senza seriale: LED (rosso = avvio, verde a 1 Hz = vivo) e `bm/bootlog.txt`
  scritto sulla SD a ogni avvio.
- WiFi: port di rtw88 (`src/rgb30/rtw*.c`, BSD-3-Clause), WPA2 in software (`wpa.c`), lwIP di
  M18. `make TARGET=rgb30 test-wifi`: frame, WPA2 contro `tests/rgb30/wpa_vectors.h` (scritto da
  `wpa_vectors.py`, Python + `cryptography`) e tutta la stazione su un chip e access point simulati
  (`wifi_sim_test.c`, `-v` mostra la console).

## Cartucce `.cart`: rimosse

- Decisione dell'utente (2026-09-30): bm esegue solo i `.bm`. Il vecchio formato `.cart`
  e il suo interprete non entrano nelle build, nel kernel o nell'immagine SD; non
  reintrodurli senza una richiesta esplicita.

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
  push di `art/meshy/request.txt` su 3d-performance e mette i risultati su `meshy-out`),
  poi impacchettate (`art/meshy/pack.py`: `<nome>.mesh` con 1200 e 450 triangoli,
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
  all'utente le foto del benchmark e dell'overlay.
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
  esempi di codice e pannello.
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
  `test_code_completion`. La scrittura col pad (accordi, modi facile/sillabe/steno) non è
  su main: è archiviata nel branch `archive/pad-typing` (l'ultimo stato del vecchio
  `ai-assistant`, chiuso il 2026-10-03); per riprenderla si parte da lì.

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

## Comunicazione con l'utente

- Riportare la **lista delle milestone** solo quando una milestone è completata per
  intero (non per i singoli passi): una lista puntata (niente tabelle, niente icone),
  una descrizione breve e lo stato di ciascuna; le milestone completate barrate
  (`~~M12 — ...~~`).
