# bm (BareMetal) — note per chi lavora su questo repository

Kernel bare metal per Raspberry Pi Zero W (BCM2835, ARM1176JZF-S): C + assembly +
Lua 5.4 embedded. Documentazione: `README.md` (presentazione in inglese, con showreel e
screenshot in `docs/img/`), `README_OLD.md` (il README completo, in italiano),
`docs/ROADMAP.md`, `docs/HARDWARE.md`.

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
  blocco `do ... end` (meno di 200 locali). Il mouse (branch `claude/mouse`) verrà dopo:
  per ora tastiera e pad. Scritte sulle righe di 16 pixel (i test in QEMU leggono lo
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
- Le animazioni del cacciatore (8 direzioni) sono pose chiave in `art/anims.py` (lo
  scheletro e le sue articolazioni in `art/hunter.py`; `aim` gira il polso perché la saw
  cleaver punti dove serve). Renderle tutte richiede circa un'ora: `mkassets.py` tiene i
  fotogrammi in `build/yharnam-frames/` e ridisegna solo quelli cambiati. Nel gioco:
  `HUNT[nome].d[direzione][fotogramma]`; sul titolo X mostra tutte le animazioni.
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
- La caccia (richiesta dell'utente: non si va all'infinito in una direzione): zone di `AREA` ×
  `AREA` pezzi verso est, chiuse da un muro di nebbia (`inside`, `draw_edge`); in fondo a ogni
  zona l'arena del boss (`boss_chunk`: pira, giardini, cimitero, cappella a turno), ucciso il
  quale si apre la zona dopo (`G.open`). Due lampade del cacciatore per zona (`lamp_chunk`, prop
  `shrine`): una a metà, una prima del boss; si torna all'ultima accesa. Echi (`G.echoes`) dai
  nemici uccisi, da spendere con parsimonia: Select (Tab) cura poco per `HEAL_COST`, la morte
  costa `DEATH_COST` e senza abbastanza echi la caccia è perduta (stato `lost`).
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
- Lo sheet è largo 4096 (skyline, fotogrammi uguali tenuti una volta). La cache delle creature
  dipende dal codice (non dai commenti né dagli import) di `rig.py`, `foeparts.py` e del loro
  modulo; quella del cacciatore da `hunter.py`; tutte da `sdf.SDF_VERSION` (aumentarlo se cambia
  il modo di disegnare). `sdf.render` valuta ogni primitiva solo dove la sua sfera può arrivare
  (stessi pixel, da 2 a 5 volte più veloce); il render completo delle creature richiede circa
  un'ora. Con `YH_DRAFT=1` i fotogrammi mancanti diventano segnaposto, per provare il gioco
  intanto (non fare commit di quello sheet).

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
