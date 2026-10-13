# Tropic Trail · Video 2 (bm Pixel + pittura in Studio) — shot list

Una riga = un'azione. Tempi in secondi dall'inizio (m:ss), coerenti con `script.md`. Tasti da `manuals/content/en/editor-pixel.md` e `editor-studio.md`.
Sprite al formato 16: 0 grass, 1 path, 2 rock, 3 wood, 4 skin, 5 face, 6 apple, 7 apple_icon. I colori si scelgono con `1`-`9`, `0` (primi dieci) o `,` `.` (precedente/successivo) nella palette di Studio caricata con `2` su F3. Gli indici dei colori `[DA VERIFICARE: indice della palette di Studio per verde erba, verde scuro, verde chiaro, sabbia, marrone scuro, grigio, grigio chiaro, marrone legno, menta, menta scuro, bianco, nero, rosso, rosso chiaro, verde foglia]`: nelle righe sotto "colore X" richiede quel dato.
Pixel e Studio sono due registrazioni (stessa SD): il taglio è fra le righe 38 e 39.

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 1 | 0:00 | Studio F2 e Pixel F2 (gancio) | `F2` | I modelli dipinti e la sheet con 8 tessere | Textures and paint, on the console |
| 2 | 0:08 | Dev ▸ bm Pixel | `[DA VERIFICARE: tasto per aprire bm Pixel dalla tab Dev]` (ripresa QEMU breve; con bmhost `--tool` parte già aperto) | La lista dei file di Pixel | Dev tab ▸ bm Pixel |
| 3 | 0:11 | bm Pixel ▸ menu | `Ctrl+O` | La lista dei `.bm` e `.bme` | Ctrl+O: open |
| 4 | 0:14 | bm Pixel ▸ lista | `Down` fino a TROPIC.BME (`[DA VERIFICARE: posizione nella lista]`), `Enter` | Il progetto si apre; nome nel menu | TROPIC.BME |
| 5 | 0:17 | bm Pixel ▸ F1 draw | `F1` | La pagina di disegno, sprite 0, grande a sinistra | F1: draw |
| 6 | 0:20 | bm Pixel ▸ F1 | `z` finché il pannello dice `16x16` (`[DA VERIFICARE: quante pressioni da 8]`) | Il pannello mostra 16x16 | z: sprite size |
| 7 | 0:24 | bm Pixel ▸ F3 palette | `F3` | Tutti i colori, valori RGB | F3: palette |
| 8 | 0:27 | bm Pixel ▸ F3 palette | `2` | Palette di bm Studio caricata | 2: bm Studio's palette |
| 9 | 0:30 | bm Pixel ▸ F1 draw | `F1` | Di nuovo F1 sul disegno | F1: draw |
| 10 | 0:32 | bm Pixel ▸ F1 grass | `g` | Strumento riempimento | g: fill |
| 11 | 0:34 | bm Pixel ▸ F1 grass | colore verde erba (tasto `[DA VERIFICARE]`), frecce al centro del tile, `Space` | Tutto il tile verde (`[DA VERIFICARE: il riempimento di un tile vuoto riempie tutto lo sprite]`) | Fill the tile |
| 12 | 0:37 | bm Pixel ▸ F1 grass | `b`, colore verde scuro, frecce + `Space` x12 (ciuffi) | Ciuffi scuri | b: pencil · darker blades |
| 13 | 0:46 | bm Pixel ▸ F1 grass | colore verde chiaro, frecce + `Space` x9 | Ciuffi chiari | Lighter blades |
| 14 | 0:52 | bm Pixel ▸ F1 path | `PgDn` | Sprite 1 vuoto | PgDn: next sprite |
| 15 | 0:54 | bm Pixel ▸ F1 path | `g`, colore sabbia, frecce al centro, `Space` | Tile sabbia | Fill: sand |
| 16 | 0:57 | bm Pixel ▸ F1 path | `b`, colore marrone scuro, frecce + `Space` x10 (sassolini) | Sassolini | Pebbles |
| 17 | 1:03 | bm Pixel ▸ F1 path | colore chiaro, frecce + `Space` x6 | Luci sui sassi | Highlights |
| 18 | 1:07 | bm Pixel ▸ F1 rock | `PgDn`, `g`, colore grigio, `Space` | Tile grigio | PgDn · fill grey |
| 19 | 1:12 | bm Pixel ▸ F1 rock | `l`, `Space` (primo punto), frecce, `Space` (secondo punto), colore grigio scuro | Una crepa diagonale | l: line · a crack |
| 20 | 1:17 | bm Pixel ▸ F1 rock | `O`, colore grigio chiaro, `Space`, frecce, `Space` | Chiazza chiara ovale piena | O: filled oval |
| 21 | 1:22 | bm Pixel ▸ F1 wood | `PgDn`, `g`, colore marrone legno, `Space` | Tile marrone | PgDn · fill brown |
| 22 | 1:26 | bm Pixel ▸ F1 wood | `l`, colore marrone scuro, `Space`, frecce, `Space`; ripetere per 3 linee orizzontali (6 `Space`) | Assi separate da linee | l: plank lines |
| 23 | 1:32 | bm Pixel ▸ F1 wood | `b`, frecce + `Space` x8 | Venature | Grain |
| 24 | 1:37 | bm Pixel ▸ F1 skin | `PgDn`, `g`, colore menta, `Space` | Tile menta | PgDn · fill mint |
| 25 | 1:41 | bm Pixel ▸ F1 skin | `b`, colore menta scuro, frecce + `Space` x14 (squame) | Squame | Scales |
| 26 | 1:46 | bm Pixel ▸ F1 skin | `Ctrl+C` | Copia dello sprite | Ctrl+C: copy |
| 27 | 1:47 | bm Pixel ▸ F1 face | `PgDn` | Sprite 5 vuoto | PgDn |
| 28 | 1:48 | bm Pixel ▸ F1 face | frecce fino a (0,0) `[DA VERIFICARE: puntatore a (0,0) prima di Ctrl+V]`, `Ctrl+V` | Il blocco fluttua | Ctrl+V: paste |
| 29 | 1:50 | bm Pixel ▸ F1 face | `Enter` | Lo sprite 5 è una copia della pelle | Enter: put it down |
| 30 | 1:52 | bm Pixel ▸ F1 apple | `PgDn`, `g`, colore rosso, `Space` | Tile rosso | PgDn · fill red |
| 31 | 1:56 | bm Pixel ▸ F1 apple | `O`, colore rosso chiaro, `Space`, frecce, `Space` | Riflesso della mela | O: shine |
| 32 | 2:00 | bm Pixel ▸ F1 apple | `b`, colore rosso scuro, frecce + `Space` x6 | Ombreggiatura | Shading |
| 33 | 2:04 | bm Pixel ▸ F1 icon | `PgDn` | Sprite 7 vuoto (trasparente) | PgDn: HUD icon |
| 34 | 2:06 | bm Pixel ▸ F1 icon | `O`, colore rosso, `Space`, frecce, `Space` | Ovale rosso | O: filled oval |
| 35 | 2:09 | bm Pixel ▸ F1 icon | `b`, colore verde foglia, frecce + `Space` x4 (picciolo e foglia) | Mela piccola con foglia | Stem and leaf |
| 36 | 2:16 | bm Pixel ▸ F2 sheet | `F2` | Tutta la sheet, 8 tessere | F2: the sheet |
| 37 | 2:20 | bm Pixel ▸ F2 sheet | `Ctrl+S` | Messaggio `saved`; fine della registrazione Pixel | Ctrl+S: save |
| 38 | 2:22 | bm Pixel ▸ F2 | (nessun tasto, pausa 2 s) | La sheet salvata | The sheet is saved |
| 39 | 2:24 | bm Studio ▸ menu | `Ctrl+O` (nuova registrazione: Studio) | La lista dei file di Studio | Ctrl+O: open |
| 40 | 2:27 | bm Studio ▸ lista | `Down` fino a TROPIC.BME, `Enter` | Il progetto si apre in Studio | TROPIC.BME |
| 41 | 2:30 | bm Studio ▸ F2 models | `F2` | Lista dei modelli, colori piatti | F2: models |
| 42 | 2:36 | bm Studio ▸ F2 models | `Down` fino a CRATE, `Enter` | Pagina F1 su CRATE | Enter: build it |
| 43 | 2:38 | F1 build ▸ selezione | `3` | Strumento selezione | 3: select tool |
| 44 | 2:39 | F1 build ▸ selezione | `a` | Tutte le facce scelte | a: all faces |
| 45 | 2:41 | F1 build ▸ pennello | `Tab` | Il foglio | Tab: the sheet |
| 46 | 2:42 | F1 build ▸ pennello | `z` finché 16 pixel `[DA VERIFICARE: z ciclo 8/16/32]`, `c` se serve per le tessere | Griglia di tessere da 16 | z: tile size 16 |
| 47 | 2:44 | F1 build ▸ pennello | frecce fino alla tessera 3 (wood) `[DA VERIFICARE: riga e colonna dello sprite 3 nel foglio di Studio]` | Riquadro sul legno | Pick the wood |
| 48 | 2:48 | F1 build ▸ pennello | `Enter` | Il legno è il pennello | Enter: take it |
| 49 | 2:49 | F1 build ▸ selezione | `Enter` | Le 6 facce della cassa hanno il legno | Enter: put it on the faces |
| 50 | 2:52 | F1 build ▸ cambio modello | `]` (modello successivo) fino a PICO | La barra di stato dice `model 3/3: PICO` | ]: next model |
| 51 | 2:54 | F1 build ▸ selezione | `3`, `a` | Tutte le facce di PICO scelte | a: all faces |
| 52 | 2:55 | F1 build ▸ pennello | `Tab`, frecce sulla tessera skin `[DA VERIFICARE: posizione dello sprite 4]`, `Enter` | Riquadro sulla pelle | Pick the skin |
| 53 | 2:58 | F1 build ▸ selezione | `Space` sulle facce arancioni (gambe) e crema (pancia) per escluderle `[DA VERIFICARE: il colore piatto resta se si esclude la faccia]`; `Enter` | Corpo, testa, braccia e coda con la pelle; gambe e pancia a colore | Enter: skin on the body |
| 54 | 3:00 | F1 build ▸ selezione | `a` (nessuna), `Space` sulla faccia frontale sinistra della testa, `Space` sulla frontale destra | Due facce scelte | Two front faces of the head |
| 55 | 3:02 | F1 build ▸ pennello | `Tab`, frecce sulla tessera face `[DA VERIFICARE: posizione dello sprite 5]`, `Enter`, `Enter` | Le due facce usano la tessera face | Enter · Enter: the face tile |
| 56 | 3:04 | F1 build ▸ pittura | `5` | Strumento 5 pittura | 5: paint tool |
| 57 | 3:06 | F1 build ▸ pittura | frecce su una faccia della testa con la tessera face, `Enter` | Pannello di pittura, tessera grande | Enter: paint the face's tile |
| 58 | 3:08 | F1 build ▸ pittura | `c`, scegli bianco `[DA VERIFICARE: voce nella lista di c]`, `Enter` | Colore bianco | c: pick white |
| 59 | 3:10 | F1 build ▸ pittura | frecce al pixel dell'occhio, `Space` x4 (quadrato bianco 2x2) | L'occhio appare sul modello | Space: paint a pixel |
| 60 | 3:12 | F1 build ▸ pittura | `c`, scegli nero, `Enter`, frecce, `Space` x1 | Pupilla | A dark pupil |
| 61 | 3:14 | F1 build ▸ pittura | `i` poi `e` su un pixel di prova | `i` prende il colore, `e` cancella (trasparente) | i: pick · e: clear |
| 62 | 3:15 | F1 build ▸ pittura | `Esc` | Pannello chiuso; Pico ha due occhi (la tessera è condivisa) | Esc: close |
| 63 | 3:16 | F1 build ▸ cambio modello | `[` fino a APPLE | `model 2/3: APPLE` | [: previous model |
| 64 | 3:17 | F1 build ▸ selezione | `3`, `a`, `Tab`, frecce sulla tessera apple `[DA VERIFICARE: posizione dello sprite 6]`, `Enter`, `Enter` | La mela ha la sua texture | Apple tile |
| 65 | 3:20 | F1 build ▸ salva | `Ctrl+S` | Messaggio `saved` | Ctrl+S: save |
| 66 | 3:22 | F2 models | `F2`, `Up`/`Down` | I tre modelli dipinti girano | Next: bm Animator |

## Tasti nuovi di questo video

- bm Pixel: `Ctrl+O`, `F1`/`F2`/`F3` (pagine), `z` (formato), `F3` + `2` (palette di Studio), `g` riempimento, `b` matita, `l` linea, `O` ovale pieno, `1`-`9`/`0` e `,` `.` (colori), `PgDn` sprite successivo, `Ctrl+C` / `Ctrl+V` / `Enter` (copia, incolla, posa), `Ctrl+S`.
- bm Studio, tessere: `3` selezione, `a` tutte le facce, `Tab` foglio, `z` formato tessera, `c` tessere/colori, `Enter` (pennello, poi facce), `[` `]` modello precedente/successivo.
- bm Studio, pittura: `5` strumento pittura, `Enter` apre il pannello, `c` colore, `Space` pixel, `e` cancella, `i` preleva, `Esc` chiude.
