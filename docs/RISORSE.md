# Risorse: un file per risorsa e la scheda Lib

Proposta del 2026-10-03, in lavorazione sul branch `claude/lib-risorse`. Descrive i file
che portano **una risorsa fuori da un `.bm`** (modelli, immagini, suoni, mappe, palette),
gli strumenti per estrarle, integrarle e vederle, e la **scheda Lib** del menu che le
elenca. Il codice Lua come risorsa non è compreso: per ora resta fuori.

**Stato (2026-10-03)**
- Fatti i passi 1–4 del capitolo 10: la specifica in `src/bm/bm.h`; `scripts/bmres.py`
  (`make test-res`: 112 controlli in Python, 28 in C col parser del kernel); il kernel
  legge i file `BMRES` (`bm_parse_any`, `bm_zone`, `bm_info_get` in `format.c`); la
  scheda Lib (`src/kernel/lib.c` l'elenco, `libview.c` le anteprime, `menu_ui.c` il
  disegno, `carts.c` i comandi), provata in QEMU da `test_lib_tab`.
- Da fare: la lettura a pezzi nella FAT e l'indice `/bm/INDEX.DAT` (oggi entrando in Lib
  si leggono tutti i file, come fa già il menu per le copertine); *Options* (X); Y che
  anima i modelli; le app che aprono i file di risorsa (oggi A apre solo le risorse dei
  `.bm`); SPRITES e i tag in bm Pixel e nelle altre app.

---

## 1. Da dove si parte

- Le risorse di un gioco stanno solo nelle sezioni del suo `.bm` (`src/bm/bm.h`): SHEET o
  SHEET8 (sprite, texture, palette), MAP, COVER, AUDIO (suoni, effetti, pattern, canzoni),
  MESH (modelli), ANIM (scheletri e animazioni).
- Le sorgenti stanno sul PC: `.png`, `.glb`, `.json` e `.csv` che `mkbm.py` trasforma in
  sezioni quando si compila. Sulla SD arrivano solo i `.bm`.
- Sulla SD, oltre ai giochi: i pacchetti di suoni di bm Sound in `/bm/sounds` (`.bm` con
  solo AUDIO e un piccolo programma che li suona), le immagini `.png`/`.jpg` in `/pics`
  da cui bm Studio fa un modello ("Model from picture"), le cartucce di nano8 in
  `carts/nano8`.
- Il kernel legge PNG, JPEG e glTF binario (`src/bm/png.c`, `jpeg.c`, `glb.c`), oggi per
  le immagini di `/pics` e per i modelli dei servizi image-to-3D.
- Oggi si scambia solo la cartuccia intera. L'unica eccezione è bm Sound, che con
  "Import from…" prende un suono, un effetto, un pattern o una canzone da un altro `.bm`.
- Tre ostacoli:
  - tutto dipende dallo sheet: gli angoli di texture dei modelli sono pixel dello sheet,
    la mappa usa numeri di sprite, il codice usa coordinate;
  - sprite, pattern, mappa e palette non hanno un nome;
  - autore, licenza e versione esistono solo per la cartuccia intera.

## 2. Regole comuni

1. **Lo stesso contenitore del `.bm`**: intestazione da 128 byte, tabella delle sezioni,
   CRC-32, stessi tipi di sezione. Una sezione passa da un `.bm` a un file di risorsa
   copiandola, e i lettori che esistono (C `format.c`, Python `mkbm.py`, `bmmesh.py`,
   `bmaudio.py`, JS `core.js`, Lua in `bm3d.lua` e bm Sound) servono quasi come sono.
2. **Una firma diversa, `BMRES`**: un file di risorsa non è un gioco. Il menu non lo
   elenca tra i giochi (guarda solo i `.bm`) e `bm_parse` lo rifiuta; gli strumenti usano
   una variante che accetta le due firme.
3. **Niente codice**: un file di risorsa non ha mai una sezione LUA, quindi si può
   ricevere da chiunque. L'anteprima la fa il sistema.
4. **Autosufficiente**: il file porta quello da cui la risorsa dipende (un modello il suo
   pezzo di texture e il suo scheletro, una canzone i suoi pattern e suoni, una mappa le
   sue tessere).
5. **Una sezione INFO in ogni file**: nome lungo, autore, licenza, versione, tag.
6. **Nomi 8.3**: la console scrive solo nomi di 8 caratteri con estensione di 3. Le
   estensioni sono `bm` più una lettera (mai `.bmp`, che è delle bitmap); il nome vero è
   dentro il file.
7. **I giochi non li leggono mentre girano**: una risorsa entra in un `.bm` copiandola
   (integrazione). I file di risorsa li leggono il menu, le app di sviluppo e gli
   strumenti del PC. Una cartuccia resta un file solo, che funziona da sola.

## 3. Le estensioni

| Estensione | Risorsa | Sezioni | Sulla console | Sul PC |
|---|---|---|---|---|
| `.bmm` | modelli 3D | MESH, ANIM (se animati), SHEET8 o SHEET (le texture) | bm Studio, bm Mesh, bm Animator | bm Studio, bm Animator, `bmmesh.py`; da e verso `.glb` |
| `.bmi` | immagini e sprite | SHEET8 o SHEET, SPRITES | bm Pixel, bm editor, bm Studio (tessere) | bm Studio; da e verso `.png` |
| `.bms` | suoni | AUDIO | bm Sound | `bmaudio.py`; da e verso `.json`, verso `.wav` |
| `.bmt` | mappa a tessere | MAP, SHEET8 o SHEET (le tessere) | bm editor | da e verso `.csv` + `.png` |
| `.bmc` | palette | SHEET8 di N×1 pixel | bm Pixel, bm Studio (pittura) | da e verso `.hex`, `.gpl` |
| `.bmk` | kit (più tipi insieme) | qualsiasi combinazione delle sezioni sopra | tutte, ognuna prende la sua parte | tutti |

In ogni file c'è anche INFO.

### `.bmm`: modelli
- MESH con uno o più modelli (fino a 256), con il margine delle texture come nel `.bm`.
- ANIM con gli scheletri dei modelli animati (legati al modello per nome).
- Uno sheet con **solo le celle usate dalle texture**, con gli angoli di texture riferiti
  a questo sheet. Un modello a soli colori non ha sheet.

### `.bmi`: immagini e sprite
- Lo sheet (o un suo pezzo) e la sezione SPRITES con le zone che hanno un nome.
- Senza SPRITES l'immagine intera è una sola voce.
- Un'immagine può diventare anche la copertina di un `.bm` o le tessere di bm Studio.

### `.bms`: suoni
- La sezione AUDIO così com'è (il banco `BMAU`, `src/audio/player.h`).
- Le voci sono le canzoni, gli effetti e i suoni (strumenti); i pattern viaggiano con le
  canzoni che li usano.
- I pacchetti di `/bm/sounds` (`.bm`) si continuano a leggere.

### `.bmt`: mappe
- MAP e uno sheet con **solo le tessere usate dalla mappa**, con i numeri della mappa
  riferiti a questo sheet.

### `.bmc`: palette
- Uno SHEET8 di N×1 pixel (N fino a 256): la sua palette è la palette, il pixel *i* è il
  colore *i*. Nessun formato nuovo da leggere.
- Sul PC: `.hex` (un colore per riga, il formato di Lospec) e `.gpl` (GIMP).

### `.bmk`: kit
- Un insieme di risorse di tipi diversi con **un solo sheet** in comune, per esempio un
  personaggio con modello, animazioni, sprite e suoni.
- Nella scheda Lib il kit compare nel suo gruppo, e le sue parti negli altri gruppi.

## 4. Il contenitore `BMRES`

Little endian, come il `.bm`. Cambiano solo i byte 0–7 e 12–16; il resto è nello stesso
posto, così i lettori condividono il codice.

| Offset | Tipo | Contenuto |
|---|---|---|
| 0 | char[8] | firma `"BMRES"` e tre byte a zero |
| 8 | u16 | versione (1) |
| 10 | u16 | dimensione dell'intestazione (128) |
| 12 | u16 | tipo: 1 `.bmm`, 2 `.bmi`, 3 `.bms`, 4 `.bmt`, 5 `.bmc`, 6 `.bmk` |
| 14 | u16 | riservato (0) |
| 16 | u8 | riservato (0) |
| 17 | u8 | numero di sezioni |
| 18 | u16 | riservato (0) |
| 20 | u32 | CRC-32 di tutto quello che segue l'intestazione |
| 24 | char[48] | nome mostrato (UTF-8) |
| 72 | char[32] | autore |
| 104 | … | riservato (0) fino a 128 |
| 128 | | tabella delle sezioni: per ognuna `u32 tipo, u32 offset, u32 dimensione, u32 riservato`; i corpi allineati a 4 byte |

- **Il tipo conta più dell'estensione**: l'estensione serve agli elenchi, ma un file
  rinominato si riconosce dal tipo.
- Le sezioni ammesse sono quelle della tabella del capitolo 3. Un lettore ignora le
  sezioni che non conosce e le conserva quando riscrive il file; un file con una sezione
  LUA si rifiuta.
- Uno sheet per file (SHEET o SHEET8, mai tutti e due), come nel `.bm`.
- I limiti sono quelli del `.bm`: sheet fino a 4096×4096, 256 modelli, 64 ossa, il banco
  di suoni com'è.

## 5. Sezioni nuove

### INFO (10)
Testo UTF-8, righe `chiave: valore`. Prima le righe del file; poi, per le singole voci,
blocchi che iniziano con `[tipo nome]`.

```
name: Cavalieri del villaggio
author: Mario
license: CC-BY-4.0
version: 2
tags: personaggio, medievale
origin: 9f86d081884c7d65...

[model knight]
desc: cavaliere con spada, 3 animazioni
tags: personaggio, armatura

[sfx clang]
tags: metallo, colpo
```

- Chiavi: `name`, `desc`, `author`, `license` (codice SPDX: `CC0-1.0`, `CC-BY-4.0`…),
  `version`, `tags` (separati da virgole), `origin` (SHA-256 della risorsa da cui è stata
  copiata), `date` (AAAA-MM-GG).
- Tipi dei blocchi: `model`, `sprite`, `sound`, `sfx`, `song`, `map`, `palette`.
- Al massimo 16 KiB. Le chiavi sconosciute si conservano.
- I tag calcolati (animato, con texture, numero di triangoli, dimensioni) non si
  scrivono: li ricava chi legge.
- INFO può stare anche nel `.bm` di un gioco: i kernel vecchi la ignorano e `bm_rewrite`
  la conserva.

### SPRITES (11)
Zone dello sheet con un nome.

```
u16 zone (1..1024), u16 riservato (0)
per zona, 28 byte:
  char[16] nome (UTF-8, zeri in fondo, unico nella sezione)
  u16 x, y, w, h     pixel dello sheet (w, h ≥ 1)
  u8  fotogrammi     1..16
  u8  fps            0 = ferma
  u16 riservato (0)
```

- I fotogrammi dopo il primo sono i riquadri w×h a destra del primo, sulla stessa riga
  (come in bm Pixel finché la fila non va a capo): una zona si sposta nello sheet
  intera, senza che i fotogrammi cambino ordine.
- Può stare anche nel `.bm` di un gioco: gli sprite prendono un nome, e i fotogrammi e
  la velocità che oggi bm Pixel tiene nel suo salvataggio passano lì. Un'eventuale
  `spr("nome")` nei giochi è un'altra decisione, fuori da questa proposta.

## 6. Estrazione e integrazione

**Estrazione** (da un `.bm` a un file di risorsa):
- un modello diventa `.bmm` con il suo scheletro e solo le celle di texture che usa;
- una zona dello sheet diventa `.bmi` (con SPRITES se ha un nome o dei fotogrammi);
- canzoni, effetti o suoni scelti diventano `.bms`, con i pattern e i suoni da cui
  dipendono;
- la mappa diventa `.bmt` con le tessere che usa;
- la palette di uno SHEET8 diventa `.bmc`;
- tutte le risorse di un gioco diventano `.bmk`.

**Integrazione** (da un file di risorsa a un `.bm`):
- texture, sprite e tessere: celle 8×8 libere nello sheet di destinazione, a isole (i
  riquadri che le facce, le zone o le tessere usano insieme). Una cella è libera se è
  trasparente e nessuna faccia, zona o tessera la usa; la cella 0 non si usa mai (nella
  mappa è il vuoto). Se non c'è posto lo sheet cresce **in altezza**, fino a 4096: la
  larghezza resta, così i numeri degli sprite e della mappa non cambiano. Un'isola più
  larga dello sheet non entra (un messaggio lo dice). Poi si correggono gli angoli di
  texture, le zone di SPRITES e i numeri della mappa;
- palette: i colori si uniscono; oltre 256 lo sheet diventa SHEET da solo, come fa già
  `sheet_section` in `runtime.c`;
- nomi: se esistono già, si aggiunge un numero (16 caratteri per modelli e zone, 8 per
  i suoni);
- lo scheletro segue il suo modello;
- i suoni vanno negli slot liberi, come fa già "Import from…" di bm Sound;
- INFO della risorsa (autore, licenza, tag) entra nel blocco della voce, con `origin`.

L'integrazione non toglie mai niente al `.bm` di destinazione. Estrarre una risorsa e
integrarla in un progetto vuoto deve dare le stesse sezioni, byte per byte (tranne INFO,
che nel progetto riceve i blocchi delle voci con `origin`).

## 7. La scheda Lib

### Posto e comandi
- Le schede diventano **Games · Dev · Lib · Settings**. L1/R1 (sulla tastiera `[` `]` o
  Tab) passano da una all'altra come oggi; nel menu di testo e dalla seriale i tasti
  `1` `2` `3` `4`.
- Dentro Lib:
  - **sinistra/destra**: il gruppo (**Models, Images, Sounds, Maps, Palettes, Kits**),
    su una riga sotto le schede, con quello scelto evidenziato come le schede;
  - **su/giù**: la lista a sinistra;
  - **A**: *Open*, nell'app del gruppo (bm Studio per i modelli, bm Pixel per immagini
    e palette, bm Sound per i suoni, bm editor per le mappe; un kit chiede con quale).
    Per ora solo per le risorse dentro un `.bm`;
  - **X**: *Options*, un pannello come quelli del menu: *Copy into a project…*, *Save
    to /bm/lib* (per le risorse dentro un `.bm`), *Tags…*, *Rename*, *Delete* (solo i
    file di `/bm/lib`), *Details*;
  - **Y** (tastiera `V`, come Y nei giochi; dalla seriale `v`): suona o ferma il suono
    (non mentre un gioco è sospeso: il suo banco aspetta nel player); più avanti anche
    l'animazione dei modelli;
  - **B** chiude i pannelli, come nelle altre schede.
- Col puntatore (M32) si scelgono gruppo, voce e pulsanti.

### La lista (a sinistra)
- Le voci sono raggruppate per file, con una riga d'intestazione grigia e il numero a
  destra (`VILLAGE.BM   8`): prima i file di `/bm/lib` (per nome), poi i pacchetti di
  `/bm/sounds` e i `.bm` di `/carts` e della radice (per titolo). Il percorso intero è
  nelle righe dei dettagli (`from bm/lib/HOUSE.BMM`).
- Cosa è una voce, gruppo per gruppo:
  - **Models**: ogni modello; una `A` accanto ai modelli animati;
  - **Images**: le zone di SPRITES; senza SPRITES, lo sheet intero del file
    (`sheet 256x256`);
  - **Sounds**: canzoni, effetti e suoni, con una lettera (`S`, `E`, `I`) come le mesh in
    bm Mesh;
  - **Maps**: una mappa per file (`64x32`);
  - **Palettes**: i file `.bmc` e la palette di ogni SHEET8 (`32 colours`);
  - **Kits**: i file `.bmk`.
- Un gruppo vuoto mostra una riga che spiega da dove arrivano le risorse.
- La lista scorre e ogni gruppo ricorda la sua posizione, come le schede ricordano la
  copertina scelta.
- Non si elencano le cartucce incorporate nel kernel (le app di sviluppo).

### L'anteprima (a destra)
- **Models**: il modello che gira, con lo sheet del suo file come texture (il renderer
  3D del kernel); Y anima con le sue clip.
- **Images**: l'immagine ingrandita per stare nel riquadro, sopra la scacchiera della
  trasparenza (come bm Pixel), la zona scelta contornata e animata se ha fotogrammi.
- **Sounds**: Y suona la canzone, l'effetto o una nota dello strumento (il player è nel
  kernel).
- **Maps**: la mappa intera in piccolo, con le sue tessere.
- **Palettes**: la griglia dei colori.
- **Kits**: l'elenco delle parti e l'anteprima della prima.
- Sotto l'immagine, righe di testo: nome; numeri (triangoli, dimensioni, colori,
  durata); file di provenienza; autore e licenza; tag.

### Disposizione (640×360, righe di 16 px, 80 colonne)

```
   Games    Dev   [Lib]   Settings                               (icone della barra)

 <  [Models]   Images   Sounds   Maps   Palettes   Kits  >

 HOUSE.BMM                    1   +------------------------------------------+
  house                           |                                          |
 VILLAGE.BMK                  8   |          (il modello che gira)           |
  ground                          |                                          |
  ...                             |                                          |
 VILLAGE.BM                   8   +------------------------------------------+
  ground                          villager
  ...                             112 vertices, 168 faces, 3 clips
 >villager                    A   from carts/village.bm
                                  bm   CC0-1.0
                                  tags: character, village

                   (A) Open in bm Studio   (Y) Play   (Share+Options) Monitor
```

- Riga 1 le schede, riga 4 i gruppi (su una barra loro), righe 6–18 la lista (colonne
  2–31) e l'anteprima (colonne 35–76, righe 6–13), righe 14–18 i dettagli, riga 21 i
  suggerimenti dei tasti con le icone di `prompts.c` (A solo se la risorsa è in un `.bm`,
  Y solo per i suoni).
- Le scritte stanno sulle righe di 16 px del font, così i test in QEMU leggono lo
  schermo.
- Colori, barre e pannelli sono quelli di BareMetal UI (`menu_ui.c`).

### Velocità e memoria
- La lista si costruisce quando si entra in Lib, e di nuovo dopo un salvataggio o una
  copia. Per ogni file servono solo l'intestazione, la tabella delle sezioni e i nomi.
- Un indice in `/bm/INDEX.DAT` ricorda percorso, dimensione e CRC-32 di ogni file (il CRC
  è già nell'intestazione): un file che non è cambiato non si rilegge.
- Oggi la FAT legge i primi 512 byte (`fat_read_head`) o il file intero: serve una
  lettura di un pezzo (posizione e lunghezza), per leggere solo la tabella e i nomi.
- L'anteprima carica solo il file della voce scelta, e solo quando la selezione si
  ferma per qualche fotogramma (scorrendo veloce non si carica ogni voce); il file di
  prima si libera.

## 8. Strumenti del PC

- `scripts/bmres.py`, solo con la libreria standard di Python come `mkbm.py`:
  - `list FILE`: cosa contiene un `.bm` o un file di risorsa;
  - `extract GAME.bm --model villager -o VILLAGER.bmm` (e `--sprite`, `--sfx`, `--song`,
    `--map`, `--palette`, `--all`);
  - `add GAME.bm FILE...`: integra, con le regole del capitolo 6;
  - `convert`: da e verso `.glb`, `.png`, `.json`, `.csv`, `.hex`, `.gpl`;
  - `info FILE --tags …`: scrive INFO.
- Anteprime sul PC: `tools/bmrender.py` per i modelli (PNG), `make wav` per i suoni.
- `mkbm.py --res FILE` (anche più volte) integra le risorse quando si compila: un gioco
  del repository può tenere i suoi file in `carts/<gioco>/res/`.
- bm Studio e bm Animator sul PC aprono e salvano `.bmm`, `.bmi` e `.bmk`.

## 9. Compatibilità e test

- I kernel vecchi non vedono i file di risorsa (il menu elenca solo i `.bm`) e ignorano
  INFO e SPRITES dentro un `.bm`; gli strumenti le conservano.
- I pacchetti di `/bm/sounds` si continuano a leggere; bm Sound salva i nuovi come `.bms`
  in `/bm/lib`.
- Il formato vive in quattro linguaggi (C, Lua, Python, JS): come per il banco di suoni
  (`make test-sound`), un test estrae, reintegra e confronta le sezioni byte per byte.
- Ogni lettore controlla i dati prima di usarli (`bm_mesh_check`, `bm_anim_check`,
  `au_parse` lo fanno già), e un test prova file rovinati di proposito.
- Licenze: INFO le porta attraverso le copie; per pubblicare nello store (M25) sono
  obbligatorie.
- Scambio (M24–M26): i file di risorsa viaggiano come i `.bm`, in rete locale e nello
  store, ciascuno con il suo SHA-256.

## 10. Passi

1. Specifica in `src/bm/bm.h` (`BMRES`, INFO, SPRITES) e questo documento.
2. `scripts/bmres.py` con i test sul PC: estrazione, integrazione, conversioni.
3. Kernel: lettura di `BMRES`, lettura a pezzi nella FAT, indice; scheda Lib con gruppi e
   liste; test in QEMU `test_lib_tab` (legge lo schermo).
4. Anteprime: immagini, palette e mappe; poi modelli (renderer 3D) e suoni (player).
5. *Options*: *Copy into a project*, *Save to /bm/lib*, *Delete*; poi le app aprono i
   file di risorsa.
6. Tag e dettagli (INFO) da *Options*; SPRITES in bm Pixel.
7. bm Studio e bm Animator sul PC.
