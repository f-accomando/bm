# Tropic Trail · Video 1: bm Studio — Pico, Apple and Crate

Durata target ~3:30. Ritmo calmo (~130 parole/min): ogni azione resta a schermo il tempo di leggerla, con tasto e tab in sovrimpressione.
Fonti dei tasti: `manuals/content/en/editor-studio.md` (capitoli "The screen and the 3D view", "Building", "Selecting and moving", "The models page and mesh reduction", "Saving and exporting"). Budget: `docs/video/PIANO.md` §2.
Narrazione e didascalie in inglese; il resto in italiano. Tempi = secondi dall'inizio del video finale.

Risultato del video: cartuccia di lavoro `tropic` (progetto `TROPIC.BME`) con tre modelli solo a colori: `PICO`, `APPLE`, `CRATE`.
Conteggi attesi (calcolati a mano dal progetto di costruzione, da confermare con la riga dei conteggi di F2): CRATE 12 triangoli (budget 40), APPLE 108 prima della riduzione e 80 dopo (budget 80), PICO 116 (budget 300). Totale ~208 contro i 1200 di scena del manuale.

| Tempo | Sullo schermo | Narrazione (English) |
|---|---|---|
| 0:00 | Gancio: pagina F2, i tre modelli finiti PICO, APPLE, CRATE che girano uno dopo l'altro; titolo "Tropic Trail · 1 · bm Studio" | Pico, an apple and a crate: three 3D models, built from nothing but blocks, inside the console. Let's make them. |
| 0:08 | Tab Dev, bm Studio; poi Esc, New project; Ctrl+S, nome TROPIC.BME | bm Studio is the 3D modelling editor of bm. Open it from the Dev tab, press Escape, and choose New project. We save it right away as TROPIC. |
| 0:20 | Pagina F1 build, poi F2 models, poi `?` tenuto: elenco dei tasti | Two pages: F1 to build, F2 for the list of models. Press the question mark at any time to see the keys of the page you are on. |
| 0:32 | Strumento 1 blocco: Space, PgUp + Space, `q` x3, Backspace | Tool one is the block. Arrows move the cursor, Space puts a cube. Page Up lifts the cursor one level, and a second cube sits on top. Q turns the view by forty-five degrees, so you can check every side. Backspace takes a block away. One cube is all a crate needs. |
| 1:05 | Tab: foglio, `c` palette dei colori, scelta del marrone, Enter; Space ricolora il blocco | Tab opens the sheet. C switches from tiles to colours. Pick a brown, press Enter, and the faces take that flat colour. Textures come in the next video. |
| 1:20 | F2: lista modelli, `r`, nome CRATE; riga dei conteggi: 12 tri | F2 lists the models. R renames: this one is CRATE. Twelve triangles, far below our budget of forty. |
| 1:35 | `n` nuovo modello, `r` APPLE; tre strati di blocchi (croce, quadrato, croce) in timelapse | N makes a new model: APPLE. Three layers: a plus, a full square, a plus again. Fifty-four faces, one hundred and eight triangles. |
| 2:00 | Strumento 4 vertici: quattro angoli al centro in alto, `g`, PgDn | Tool four moves corners. Choose the four corners at the top centre, press G, and push them down a little: the dimple of an apple. |
| 2:15 | F2, `-`, scatola "triangles wanted", scrivere 80, Enter; conteggio 80 | One hundred and eight is over our budget of eighty. Press minus, type eighty, and Enter. The reducer keeps the borders. Control Z would undo it. |
| 2:30 | `n`, `r` PICO; gambe arancioni, corpo/testa/braccia menta, coda con punta gialla (timelapse, 16 blocchi) | Now Pico. N, then the name. Orange for the legs, mint green for the body, head and arms, and a tail that ends in yellow. Sixteen blocks, in time-lapse. |
| 3:04 | Strumento 3 selezione: 6 facce frontali del corpo, colore crema, Enter | Tool three selects faces. Space on the six front faces of the body, then Enter puts the brush on them: a cream belly. |
| 3:10 | Selezione della faccia di fondo della coda, `g`, PgUp, Enter | G moves what you selected. Page Up lifts the tip of the tail; Enter leaves it there, Escape would put it back. |
| 3:16 | F2: conteggi di PICO sotto 300; Ctrl+S: "saved" | F2 shows the counts: Pico is well under three hundred triangles. Control S saves the project. |
| 3:20 | I tre modelli sulla lista; scritta "Next: textures" | Next time: bm Pixel. We draw grass, path, rock, wood and skin, and paint these models. |

Chiusura (3:20-3:30): il video successivo riceve `TROPIC.BME` (e `chain/01-studio.bm`) con i tre modelli a colori; nel video 2 diventano texture e pittura.

## Capitoli (identici a youtube.md)

0:00 Pico, Apple and Crate
0:08 Open bm Studio and a new project
0:20 The two pages and the key help
0:32 Blocks and the 3D view
1:05 The colour brush
1:20 The models page: CRATE
1:35 APPLE from blocks
2:00 Moving corners
2:15 Reducing triangles
2:30 PICO from blocks
3:04 Selecting and moving faces
3:16 Check and save

## Problemi trovati

1. **Studio salva un progetto `.bme`, non `.bm`.** Il piano (PIANO.md §4) chiama l'output `chain/01-studio.bm`. Il manuale non dice come un `.bme` diventi `.bm` dall'interno di Studio (Ctrl+B è dell'SDK, non di Studio). Nel video si salva solo `TROPIC.BME`; la conversione per la catena avviene fuori scena con `bm_forge.py export models` (vedi input.md). `[DA VERIFICARE: se bm_forge export models conserva la sheet e il formato accettato da bm Pixel]`.
2. **Studio non ha un contatore per-budget**: mostra facce/tri/vertici e l'avviso `heavy for 60 fps (1200 triangles a scene)` solo sopra 1200. I limiti 300/80/40 del piano si leggono a mano sulla riga dei conteggi di F2; nessun avviso li impone.
3. **La riduzione può fermarsi sopra il numero chiesto** (messaggio `(no further without turning faces over)`). Se APPLE non scende a 80, il video resta valido solo se si prova prima: `[DA VERIFICARE: la riduzione 108 -> 80 arriva a 80]`. Piano B: togliere i blocchi d'angolo dello strato centrale prima di ridurre.
4. **Solo colori in questo video**: il manuale dice che con un colore come pennello le facce sono a tinta unita senza texture. Va bene per il piano ("solo colori"), ma le facce colorate dovranno essere riassegnate a una tessera nel video 2 (sezione "Painting a model": una faccia a colore non si dipinge pixel per pixel finché non ha una tessera).
5. **Sul bmhost non esiste la tab Dev**: la riga 0:08 mostra la tab Dev solo in una breve ripresa QEMU (PIANO.md §6, D1); nel resto il runtime `--tool` parte direttamente in Studio. `[DA VERIFICARE: decisione D1]`.
6. **Gamepad**: il manuale ha `[TO FILL]` per i tasti pad di Studio su RGB30; il video usa solo la tastiera.
