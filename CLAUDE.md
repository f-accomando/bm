# bm (BareMetal) — note per chi lavora su questo repository

Kernel bare metal per Raspberry Pi Zero W (BCM2835, ARM1176JZF-S): C + assembly +
Lua 5.4 embedded. Documentazione: `README.md`, `docs/ROADMAP.md`, `docs/HARDWARE.md`.

## Build e test

- `make` → `build/kernel.img` e `build/chainloader.img` (toolchain `arm-none-eabi-gcc`).
- `make test` → test sul PC (grafica, FAT, USB, audio, rete, giochi) + test end-to-end in
  QEMU (`-M raspi0`).
- L'utente prova sul Pi reale copiando `dist/kernel.img` sulla SD (WSL, `/mnt/d`),
  senza cavo seriale: tutto ciò che deve verificare va mostrato sullo schermo.

## bm Studio (sdk/studio)

- Applicazione per il PC (pagina web, niente build né dipendenze) per i modelli 3D a
  tessere (stile Crocotile 3D) e la pixel art dello sheet; legge e scrive il `.bm`
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
- **Studio 3D della console** (`carts/studio3d/main.lua`, cartuccia incorporata come
  l'editor, scheda Dev, monitor `3`): player + versione semplificata di Studio/Animator.
  Codifica MESH/ANIM in Lua (`string.pack`) e le passa al kernel con `cart_data()`;
  `tile_face`/`place_faces` sono il port di `edit.js` (le facce devono restare identiche,
  `check_studio3d.js`). Al livello principale del file ci sono meno di 200 locali: ogni
  pagina sta in un blocco `do ... end`. Prova sul PC: `tests/studio/studio3d_host.lua`
  (in `make test-studio`).

## Nome

- Il progetto si chiama **bm** (BareMetal); cartucce `.bm`, cartella `bm/` sulla SD.
  Il vecchio nome sopravvive solo dove serve alla compatibilità (la cartella della SD e
  l'intestazione delle cartucce di prima, lette ancora; il tag di rete per i kernel
  vecchi in `tools/bm_net.py`). Il repository GitHub è `f-accomando/bm`.
- Il branch principale è `claude/bare-metal-mvp`: quando l'utente dice "main" intende
  quello (un branch `main` non esiste).

## Cartucce `.cart`: rimosse

- Decisione dell'utente (2026-09-30): bm esegue solo i `.bm`. Il vecchio formato `.cart`
  e il suo interprete non entrano nelle build, nel kernel o nell'immagine SD; non
  reintrodurli senza una richiesta esplicita.

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
  `src/ai/kb/README.md`) e ricette di sprite; pannello Lua `require "assist"`.
- Dopo aver cambiato la base di conoscenza: `make ai-model` (numpy) e commit di
  `src/ai/assist.weights`; `make test-ai` controlla C contro Python, domande di prova,
  esempi di codice e pannello.
- bm Code (`carts/code/main.lua`, scheda Dev): l'editor del codice; usa `cart_read` /
  `cart_write` (solo il codice), `font("6x12")` e `assist.act` per le righe `#entry:`.
  Test: `test_code_editor` in QEMU (lo schermo si legge anche col font 6x12).

## Market (M25, branch `bm-store`)

- Prima scheda del menu: **Market | Games | Dev | Settings** (tasti 1 2 3 4); il menu si
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
  una descrizione breve e lo stato di ciascuna; le milestone completate barrate
  (`~~M12 — ...~~`).
