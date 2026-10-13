# Tropic Trail · Video 2: bm Pixel — textures, tiles and painting the models

Durata target ~3:30. Ritmo calmo (~130 parole/min), ogni azione resta a schermo il tempo di leggerla. Le texture si disegnano a tempo accelerato (timelapse) dopo il primo esempio.
Fonti dei tasti: `manuals/content/en/editor-pixel.md` (pagine F1-F3, "The keys of the drawing page", "Copy, move and transform", "Palettes", "Make an animated sprite") e `manuals/content/en/editor-studio.md` ("Painting a model", "Selecting and moving", tutorial "Paint a model"). Nomi e budget: `docs/video/PIANO.md`.
Narrazione e didascalie in inglese; il resto in italiano. Tempi = secondi dall'inizio.

Riceve: `TROPIC.BME` del video 1 (modelli `PICO`, `APPLE`, `CRATE` a colori piatti). Produce: lo stesso progetto con la sheet disegnata e i modelli dipinti, poi `chain/02-pixel.bm`.

Disposizione della sheet (16x16 pixel, "sprite" numero n al formato 16): 0 grass, 1 path, 2 rock, 3 wood, 4 skin (pelle di Pico, menta), 5 face (copia di skin, per gli occhi), 6 apple, 7 apple_icon (icona HUD, sfondo trasparente). Le due tessere `face` e `apple_icon` sono aggiunte al piano, vedi "Problemi trovati". `[DA VERIFICARE: dove stanno le tessere iniziali di Studio sulla sheet: scegliere una zona libera]`.

| Tempo | Sullo schermo | Narrazione (English) |
|---|---|---|
| 0:00 | Gancio: F2 di Studio, PICO con occhi, APPLE rossa, CRATE di legno che girano; poi la sheet di Pixel con le tessere | Grass, wood, skin and an apple: the textures of Tropic Trail, drawn and painted right on the console. |
| 0:08 | Dev ▸ bm Pixel; Ctrl+O; TROPIC.BME nella lista | bm Pixel is the pixel editor inside the console. Open our project with Control O. Textures are pieces of the same sprite sheet that bm Studio uses. |
| 0:20 | F1; Z fino a 16x16; F3 e tasto 2 (palette di Studio); torna F1 | Draw at sixteen by sixteen: Z changes the size. On F3, key two loads the palette of bm Studio, so the colours match our models. |
| 0:32 | Sprite 0: riempi di verde, punte d'erba scure e chiare con la matita | First tile: grass. G fills the whole tile with green, then the pencil adds darker and lighter blades. |
| 0:52 | PgDn; sprite 1: sentiero | PgDn moves to the next sprite. A path: sandy brown, with a few darker pebbles. |
| 1:07 | PgDn; sprite 2: roccia | Rock: grey, a diagonal crack with L, and a lighter patch with the filled oval. |
| 1:22 | PgDn; sprite 3: legno | Wood: brown planks, dark lines between them, and a few dots of grain. |
| 1:37 | PgDn; sprite 4: pelle menta; Ctrl+C, PgDn, Ctrl+V, Enter: copia nella 5 | Pico's skin is mint green with darker scales. Copy it with Control C, move on, paste with Control V and press Enter: this copy will get the eyes. |
| 1:52 | PgDn; sprite 6: mela | The apple: red, with a lighter shine drawn with the oval. |
| 2:04 | PgDn; sprite 7: icona mela, sfondo trasparente | And a small apple icon for the score, on a transparent background. We will draw it in the HUD later. |
| 2:16 | F2: tutta la sheet con 8 tessere; Ctrl+S | F2 shows the whole sheet. Control S saves it in the project. |
| 2:24 | Studio: Ctrl+O, TROPIC.BME, F2 | Back in bm Studio, we open the same project. The models are still flat colours. |
| 2:36 | CRATE: strumento 3, `a`, Tab, tessera wood, Enter, Enter | Select the crate, tool three, A for all faces. Tab opens the sheet: pick the wood and press Enter, then Enter again to put it on the faces. |
| 2:52 | PICO: strumento 3, `a`, Tab, skin, Enter, Enter; occhi: due facce della testa con `face` | Same for Pico: the skin on every face. Then the two outer front faces of the head get the face tile. |
| 3:04 | Strumento 5, Enter su una faccia della testa; `c`, colori; Space su 4 pixel; Esc | Tool five paints the pixels of a face's tile. Pick white and a dark colour and paint an eye. The model changes while you paint, and every face with that tile follows. |
| 3:16 | APPLE: tessera apple su tutte le facce; Ctrl+S; i tre modelli dipinti | The apple gets its tile. Control S saves. |
| 3:22 | Chiusura: la lista con i tre modelli dipinti | Next time: bm Animator. We give Pico a skeleton and make him run. |

## Capitoli (identici a youtube.md)

0:00 Textures for Tropic Trail
0:08 Open the project in bm Pixel
0:20 Size and the palette of bm Studio
0:32 Grass
0:52 Path
1:07 Rock
1:22 Wood
1:37 Pico's skin
1:52 Apple and the HUD icon
2:16 The sheet and save
2:24 Textured models in bm Studio
3:04 Painting a model
3:22 Next: animation

## Problemi trovati

1. **Una tessera usata da più facce cambia su tutte** (manuale Studio, "Painting a model"): non si può dipingere una sola faccia di una tessera condivisa. Per gli occhi di Pico serve una tessera a parte (`face`, copia di `skin`) data solo alle due facce frontali esterne della testa: quindi la sheet ha 8 tessere (non 6 + icona). Anche la pelle crema e la punta gialla della coda restano colori piatti del video 1 (le facce a colore piatto non si dipingono pixel per pixel).
2. **Limite di texel**: Studio dà a ogni faccia una tessera intera, di 8, 16 o 32 pixel (tasto `z` del foglio). A 16 pixel per faccia di 1 unità si ha 16 texel per unità; sul display 480x270 una faccia vicina occupa decine di pixel: nitido ma pixelato, come per un classico PSX. Non esiste una texture più grande di 32x32 per faccia. `[DA VERIFICARE: se un modello con tessere da 16 pixel e uno da 32 possono stare nello stesso progetto]`.
3. **Sovrapposizione con le tessere iniziali di Studio**: un nuovo progetto di Studio "viene con le tessere iniziali sulla sheet" (manuale). Pixel non dice dove stanno; disegnare le 8 tessere in una zona libera, senza coprire quelle già usate dai modelli (che sono a colore piatto, quindi non ne dipendono). `[DA VERIFICARE: posizione e dimensione delle tessere iniziali]`.
4. **Mappa 3D**: la sezione del contratto (`SYNC.md` §2) dice che il pennello 16x16 della mappa è una tessera e che il `tile id` sceglie la texture dalla sheet. Pixel numera gli sprite al formato scelto; `[DA VERIFICARE: corrispondenza fra numero di sprite 16x16 di Pixel e tile id del layer floor della mappa]` prima della registrazione del video 4.
5. **Pixel e Studio sono due cartucce diverse** (due registrazioni in sequenza sulla stessa SD, come nella serie 2D): non si passa da un editor all'altro dentro la stessa corsa di `bmhost`. Il taglio fra 2:16 e 2:24 è un cambio di registrazione.
6. **Icona mela per l'HUD**: Pixel non ha un "sprite con nome": le zone (`name x y w h`) si scrivono in un file di testo e `mkbm.py --sprites` (manuale, `[TO FILL]` nella sezione Zones). Nel video l'icona è solo la tessera 7; il nome `apple_icon` e la zona si danno fuori dalla console. `[DA VERIFICARE: come il video 7 chiamerà l'icona (spr(7) o zspr)]`.
7. **Pittura sul modello**: il manuale dice che `Enter` o `Space` con strumento 5 apre il pannello; il tutorial dice di dare prima una tessera alla faccia (strumento 3, `Tab`, tessera, `Enter`, poi `Enter` sulle facce). I tasti qui seguono il tutorial.
