# Tropic Trail · Video 1 (bm Studio) — shot list

Una riga = un'azione. Tempi = secondi dall'inizio (m:ss), coerenti con `script.md`. Tasti copiati da `manuals/content/en/editor-studio.md`.
Convenzioni: "cursore" = cursore a cella della pagina F1; "su/giù/sinistra/destra" sono le frecce, relative alla vista (dopo `q` "su" resta "lontano da te"). Posizione di partenza del cursore dopo New project: cella (0,0), livello 0. Le coordinate (x destra, z in avanti, livello y) servono solo a rendere ripetibile la costruzione.

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 1 | 0:00 | F2 models (gancio) | `F2`, poi `Down`/`Up` sulla lista | I tre modelli finiti girano uno dopo l'altro | Pico, Apple, Crate: built from blocks |
| 2 | 0:08 | Dev ▸ bm Studio | (ripresa QEMU breve; con bmhost `--tool` parte sul menu) | Il menu con la tab Dev e l'icona di bm Studio | Dev tab ▸ bm Studio |
| 3 | 0:11 | bm Studio ▸ menu | `Esc` (dal menu al F1), `Esc` (menu) | Il menu di Studio | Esc: the menu |
| 4 | 0:13 | bm Studio ▸ menu | `Ctrl+N` (voce New project) | Pagina F1 vuota, strumento 1 blocco, `(not saved yet)` | Ctrl+N: new project |
| 5 | 0:16 | bm Studio ▸ F1 | `Ctrl+S` | La richiesta del nome (8.3, finisce in `.BME`) | Ctrl+S: save |
| 6 | 0:17 | bm Studio ▸ nome | `Backspace` x8 | Il campo nome vuoto (Save box tiene MY3D.BME) | Backspace: clear the name |
| 7 | 0:18 | bm Studio ▸ nome | testo `TROPIC.BME`, `Enter` | Messaggio di salvataggio; il nome compare nel menu | TROPIC.BME saved in /carts |
| 8 | 0:20 | bm Studio ▸ F1 build | `F1` | Pannello a sinistra, vista 3D, griglia | F1: build |
| 9 | 0:23 | bm Studio ▸ F2 models | `F2` | Lista dei modelli del file | F2: models |
| 10 | 0:26 | bm Studio ▸ F2 | `F12` tenuto | L'elenco dei tasti della pagina | ?: keys of the page |
| 11 | 0:32 | F1 build ▸ blocco | `F1`, `1` | Strumento 1 scelto nel pannello | 1: block tool |
| 12 | 0:36 | F1 build ▸ blocco | `Space` | Un cubo appare | Space: put a block |
| 13 | 0:40 | F1 build ▸ blocco | `PgUp` | Il cursore sale di un livello | PgUp: one level up |
| 14 | 0:43 | F1 build ▸ blocco | `Space` | Un secondo cubo sopra il primo | Space: second block |
| 15 | 0:47 | F1 build ▸ vista | `q` x3 (uno ogni ~40 frame) | La vista gira di 45 gradi ogni volta | q: turn the view 45 degrees |
| 16 | 0:53 | F1 build ▸ vista | `e` x3 | La vista torna al punto di partenza | e: turn back |
| 17 | 0:57 | F1 build ▸ blocco | `Backspace` | Il blocco sotto il cursore sparisce; resta un cubo | Backspace: remove the block |
| 18 | 1:01 | F1 build ▸ blocco | `Ctrl+Z` poi `Ctrl+Y` | Annulla e rifà la rimozione | Ctrl+Z undo, Ctrl+Y redo |
| 19 | 1:05 | F1 build ▸ pennello | `Tab` | Il foglio sul pannello | Tab: the sheet |
| 20 | 1:08 | F1 build ▸ pennello | `c` | Il foglio passa dalla griglia di tessere alla palette di colori | c: tiles or colours |
| 21 | 1:10 | F1 build ▸ pennello | `Down`, `Down`, `Right` (swatch 14, brown) | Il riquadro di selezione sul marrone | Pick a brown |
| 22 | 1:13 | F1 build ▸ pennello | `Enter` | Il pennello ora è il marrone | Enter: take it as the brush |
| 23 | 1:15 | F1 build ▸ blocco | `PgDn`, `Backspace`, `Space` (cursor on level 1 after undo+color) | Il cubo è marrone, colore piatto | Flat colour, no texture |
| 24 | 1:20 | F2 models | `F2` | Il modello selezionato, conteggi: 6 facce, 12 tri | F2: the model list |
| 25 | 1:24 | F2 models ▸ rinomina | `r` | Il campo nome (max 16 lettere) | r: rename |
| 26 | 1:26 | F2 models ▸ rinomina | `Backspace` x5, testo `CRATE`, `Enter` | La lista dice CRATE | CRATE |
| 27 | 1:31 | F2 models ▸ conteggi | (nessun tasto, pausa 3 s) | Riga conteggi: 6 faces, 12 tri, 8 vertices; sotto budget (40) | CRATE: 12 triangles (budget 40) |
| 28 | 1:35 | F2 models | `n` | Nuovo modello in lista (salta a build page) | n: new model |
| 29 | 1:38 | F2 models ▸ rinomina | `r`, testo `APPLE`, `Enter` | La lista dice APPLE | APPLE |
| 30 | 1:41 | F2 models | `Enter` | Pagina F1 sul modello APPLE | Enter: build it |
| 31 | 1:43 | F1 build ▸ pennello | `Tab`, `c`, frecce sul rosso (swatch 9), `Enter` | Pennello rosso | Red brush |
| 32 | 1:47 | F1 build ▸ blocco | `1`, `PgUp` (se serve) `Space` sul livello 0: croce a 5 blocchi, ordine: centro, su, giù, sinistra, destra, con `Space` dopo ogni freccia | Strato 0 = una croce (5 blocchi) | Layer 1: a plus |
| 33 | 1:52 | F1 build ▸ blocco | `PgUp`, riempi tutti i 9 blocchi (plus cells too): ciascuno `freccia freccia Space` | Strato 1 = quadrato 3x3 (9 blocchi) | Layer 2: a full square |
| 34 | 1:56 | F1 build ▸ blocco | `PgUp`, croce di 5 blocchi come nello strato 0 | Strato 2 = croce | Layer 3: a plus again |
| 35 | 1:59 | F1 build ▸ conteggi | (pausa 2 s) | 54 faces, 108 tri, 56 vertices | 54 faces · 108 triangles |
| 36 | 2:00 | F1 build ▸ vertici | `4` | Strumento 4 | 4: vertex tool |
| 37 | 2:03 | F1 build ▸ vertici | pointer già su corner; frecce se serve, `Space` su ciascuno (4 pressioni) | Quattro angoli scelti | Space: choose a corner |
| 38 | 2:08 | F1 build ▸ vertici | `g` | Modalità spostamento | g: move |
| 39 | 2:09 | F1 build ▸ vertici | `Tab` x2 (passo 1/4) | Il passo cambia nella riga di stato | Tab: the step |
| 40 | 2:11 | F1 build ▸ vertici | `PgDn` x1 (0.25) | Gli angoli scendono di 1/4: fossetta della mela | PgDn: down a little |
| 41 | 2:13 | F1 build ▸ vertici | `Enter` | Gli angoli restano lì | Enter: leave it there |
| 42 | 2:15 | F2 models | `F2` | APPLE selezionata, 108 tri | F2: the counts |
| 43 | 2:18 | F2 models ▸ riduzione | `-` | La scatola chiede i triangoli voluti, propone 54 | -: reduce the triangles |
| 44 | 2:21 | F2 models ▸ riduzione | `Backspace` x2, testo `80` | La scatola mostra 80 | Wanted: 80 |
| 45 | 2:24 | F2 models ▸ riduzione | `Enter` | Messaggio `reduced to 80 triangles  (Ctrl+Z undoes)` | Reduced to 80 triangles |
| 46 | 2:28 | F2 models ▸ riduzione | `Ctrl+Z`, poi `Ctrl+Y` | Annulla e rifà la riduzione | Ctrl+Z undoes it |
| 47 | 2:30 | F2 models | `n`, `r`, testo `PICO`, `Enter` | Nuovo modello PICO | PICO |
| 48 | 2:36 | F2 models | `Enter` | Pagina F1 su PICO | Enter: build it |
| 49 | 2:38 | F1 build ▸ pennello | `Tab`, `c`, frecce sull'arancione (swatch 11), `Enter` | Pennello arancione | Orange brush: the legs |
| 50 | 2:41 | F1 build ▸ blocco | `1`, `Space`, `destra` x2, `Space` | Gambe: blocchi (0,0,0) e (2,0,0) | Legs |
| 51 | 2:44 | F1 build ▸ pennello | `Tab`, `c`, frecce sul verde menta (swatch 26, 60D0C0), `Enter` | Pennello menta | Mint green brush |
| 52 | 2:47 | F1 build ▸ blocco | `PgUp`, `Space`, `sinistra`, `Space`, `sinistra`, `Space` (da x=2 a x=0, livello 1) | Prima fila del corpo | Body, row 1 |
| 53 | 2:50 | F1 build ▸ blocco | `PgUp`, `Space`, `destra`, `Space`, `destra`, `Space` (livello 2) | Seconda fila del corpo (6 blocchi in tutto) | Body, row 2 |
| 54 | 2:53 | F1 build ▸ blocco | `PgUp`, `Space`, `sinistra`, `Space`, `sinistra`, `Space` (livello 3) | Testa di 3 blocchi | Head |
| 55 | 2:56 | F1 build ▸ blocco | `PgDn`, `sinistra`, `Space`, `destra` x4, `Space` (livello 2, x=-1 e x=3) | Due braccia ai lati | Arms |
| 56 | 2:58 | F1 build ▸ blocco | `PgDn`, `sinistra` x2, `giù`, `Space`, `giù`, `Space` (livello 1, x=1, z=-1 e -2) | Primi due blocchi della coda | Tail |
| 57 | 3:00 | F1 build ▸ pennello | `Tab`, `c`, frecce sul giallo (swatch 12), `Enter` | Pennello giallo | Yellow brush: tail tip |
| 58 | 3:02 | F1 build ▸ blocco | `giù`, `Space` (z=-3) | Punta della coda gialla; 16 blocchi in tutto | Tail tip |
| 59 | 3:04 | F1 build ▸ pennello | `Tab`, `c`, frecce sul crema (swatch 13), `Enter` | Pennello crema | Cream brush: the belly |
| 60 | 3:06 | F1 build ▸ selezione | `3` | Strumento 3 (selezione) | 3: select tool |
| 61 | 3:07 | F1 build ▸ selezione | `3`, `q` x4 (turn 180), `z` (frame), `-` x3 (zoom); poi frecce/`Space` sulle 6 facce frontali (+z belly) | Sei facce scelte, in evidenza | Space: choose a face |
| 62 | 3:09 | F1 build ▸ selezione | `Enter` | Le facce prendono il colore crema (il pennello) | Enter: put the brush on the faces |
| 63 | 3:10 | F1 build ▸ selezione | `a` (release all), poi `e` x4, `Space` sulla faccia di fondo della coda (z=-3) | Una faccia scelta | Choose the tail end |
| 64 | 3:12 | F1 build ▸ spostamento | `g` | Modalità spostamento | g: move |
| 65 | 3:13 | F1 build ▸ spostamento | `PgUp` x1 (step 1/4 remembered) | La punta della coda sale di 0.25 | PgUp: lift |
| 66 | 3:15 | F1 build ▸ spostamento | `Enter` | La faccia resta lì | Enter: leave it there |
| 67 | 3:16 | F2 models | `F2` | PICO selezionato; riga conteggi 58 faces, 116 tri, 64 vertices, sotto 300 | PICO: 116 triangles (budget 300) |
| 68 | 3:19 | F2 models | `Ctrl+S` | Messaggio `saved` | Ctrl+S: save |
| 69 | 3:20 | F2 models | `Up`/`Down` sulla lista (CRATE, APPLE, PICO) | I tre modelli girano | Next: bm Pixel, textures |

## Tasti nuovi di questo video

- Pagine: `F1` build, `F2` models; menu `Esc`; `Ctrl+N` nuovo progetto; `Ctrl+S` salva; `F12` tenuto = aiuto tasti.
- Vista: `q` / `e` ruotano 45 gradi.
- Strumenti: `1` blocco, `3` selezione, `4` vertici.
- Blocco: frecce, `PgUp` / `PgDn` (livello), `Space` metti, `Backspace` togli.
- Pennello: `Tab` apre il foglio, `c` tessere/colori, `Enter` prende il pennello.
- Selezione: `Space` sceglie, `a` rilascia tutte, `Enter` mette il pennello sulle facce, `g` sposta (frecce, `PgUp`/`PgDn`, `Tab` passo, `Enter` lascia, `Esc` annulla).
- Vertici: `Space` sceglie, `g` sposta.
- Pagina modelli: `n` nuovo, `r` rinomina, `-` riduci, `Enter` costruisci.
- Annulla / rifai: `Ctrl+Z`, `Ctrl+Y`.
