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
- **bm Mesh** (`carts/mesh/main.lua`, incorporata, scheda Dev, monitor `4`, opzioni "Open in
  bm Mesh"): vertici e facce dei modelli (MESH), delle mesh scritte da lui nel codice
  (funzioni `mesh_<nome>()` tra `-- [bm Mesh begin]` e `-- [bm Mesh end]` in fondo a
  `main.lua`, le sole righe che riscrive) e di quelle che costruisce il codice del gioco
  (`cart_meshes()`, `src/bm/meshcap.c`: il codice gira in uno stato Lua a parte con le API
  sostituite; i nomi dalle variabili). Salva con `cart_write(path, {sections=, lua=})`;
  lo scheletro di un modello segue i vertici (`vb`). Prove: `tests/studio/mesh_host.lua`,
  `check_mesh.js`, `test_meshcap` (in `make test-bm`/`test-studio`), QEMU `test_mesh`.

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

## Assistente AI (M30)

- `src/ai/`: rete INT8 che sceglie tra le voci di `src/ai/kb/*.txt` (formato in
  `src/ai/kb/README.md`) e ricette di sprite; pannello Lua `require "assist"`.
- Dopo aver cambiato la base di conoscenza: `make ai-model` (numpy) e commit di
  `src/ai/assist.weights`; `make test-ai` controlla C contro Python, domande di prova,
  esempi di codice e pannello.
- bm Code (`carts/code/main.lua`, scheda Dev): l'editor del codice; usa `cart_read` /
  `cart_write` (solo il codice), `font("6x12")` e `assist.act` per le righe `#entry:`.
  Test: `test_code_editor` in QEMU (lo schermo si legge anche col font 6x12).

## Comunicazione con l'utente

- Riportare la **lista delle milestone** solo quando una milestone è completata per
  intero (non per i singoli passi): una lista puntata (niente tabelle, niente icone),
  una descrizione breve e lo stato di ciascuna; le milestone completate barrate
  (`~~M12 — ...~~`).
