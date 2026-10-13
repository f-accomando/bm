# Yharnam

The game: `carts/yharnam/`. Its graphics come from `build/mkassets.py`; after a change, run it
again and commit `assets/sheet.png`. Never commit a sheet made with `YH_DRAFT=1`.

---

## 1. Architettura e Struttura

- **Risoluzione & Schermo**: 360×360 (visuale 3/4 isometrica gotica, perfetta per il pannello 1:1 della RGB30 e il formato `.b16`).
- **Mondo & Streaming**: Mappa continua generata a chunk di 16×16 celle durante il movimento (`visible_chunks`, `stream()`).
- **Illuminazione**: Livelli di luce per pixel con tabelle di dissolvenza (`fades`, `dark_begin`, `glow`, `dark_end`) ispirate a Dank Tomb, lanterne a gas, bracieri e pire.
- **Pipeline Grafica**: Generazione procedurale di oltre 5.000 frame tramite Signed Distance Fields (`build/mkassets.py`, `build/art/sdf.py`, `art/anims.py`, `art/hunter.py`), animazioni del Cacciatore su curve cubiche Hermite a 8 direzioni, con risoluzione automatica delle collisioni dell'arma (`unclip`).
- **Mappa a Layer**: `map_ground.csv`, `map_overlay.csv` e `map_objects.csv` (256×256 celle), disegnata nell'SDK e convertita con `mkmap.py`.

---

## 2. Verifiche e Controlli Aperti

### A. Prestazioni su Pi Zero W e RGB30
- Misurare il costo del fotogramma a 60 fps (generazione chunk in background, `stream()`, illuminazione a livelli, particelle).
- Raccogliere i numeri con il report di sessione del dev kit e le righe `devinfo()` a fine partita.
- Confrontare i parametri con il budget a 60 fps del profilo `.b16` (M42).

### B. Mappa Disegnata sul Pi
- Procedura di test su hardware:
  1. Aprire `yharnam.bm` nell'SDK sulla console.
  2. Pagina mappa: `F3` due volte, pennello 16×16 (`z`).
  3. Piazzare tessere di terreno e, sul livello `objects` (`l`), posizionare segnaposto (es. albero, casa con angolo H7 e tessere tetto).
  4. Salvare con `Ctrl+S` (genera la copia `YHARNAM.BME`, ~6 MB: lo sheet resta in formato compatto `SHEET8`).
  5. Provare con `F5`: verificare che terreno e oggetti modificati compaiano nel gioco.
  6. Eseguire `python3 carts/yharnam/build/mkmap.py --from` sulla cartuccia estratta dalla SD e committare i CSV aggiornati.

### C. Glitch Grafico all'Avvio
- Indagare il piccolo difetto visivo presente nei primi fotogrammi:
  - Documentare sul Pi quando compare, la durata esatta e la schermata (titolo o transizione iniziale dopo il caricamento dei primi chunk).
  - Punti di indagine:
    - `_init`: verificare se le tabelle di fade e dissolvenza sono completamente inizializzate prima del primo frame di `_draw`.
    - `visible_chunks`: verificare che i chunk non ancora completati vengano esclusi dal rendering finché non sono pronti.

---

## 3. Oggetti, Luoghi e Level Design (R36)

- **Segnaposto nel Layer `objects`** (`map_objects.csv`):
  - Lanterne del cacciatore (`shrine` = 1186)
  - Barricate (`barricade` = 1188)
  - Cancelli (`gate` = 1190)
  - Arene dei boss (`boss` = 1192)
  - Si possono posizionare e spostare direttamente nel `bm SDK`.
- **Spawning Creature**: il tipo di terreno (`ch.kind`: strada, marciapiede, erba) viene derivato dinamicamente dalle tessere disegnate in `map_ground.csv`, permettendo allo spawn dei mostri di seguire le modifiche fatte nell'SDK.
- **Da completare**:
  - Vincolare ogni casa dentro il proprio blocco 16×16.
  - I confini delle aree con nome risiedono ancora in `MAP.areas` nel codice: valutare il passaggio a metadati del file mappa.
