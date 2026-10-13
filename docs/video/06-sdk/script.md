# Video 6 — bm SDK: luci, nebbia e ombra per Tropic Trail

Serie **Tropic Trail** ([`../PIANO.md`](../PIANO.md)). Editor: bm SDK (`carts/editor`). Durata: **~3:00 (180 s)**. Ritmo calmo (~130 parole/minuto).
Narrazione e didascalie in inglese, prosa di servizio in italiano. Fonti: `manuals/content/en/editor-sdk.md` (pagine, template, tasti, Build),
`docs/api/light3d.md` (firme di `light3d`, `sky3d`, `lamp3d`, `fog3d`, `shadow3d`, `draw3d`), `carts/editor/main.lua` (template `models3d`), `SYNC.md` §2 (lampada `LAMP`).
Altri file: [`shot-list.md`](shot-list.md), [`youtube.md`](youtube.md), [`input.md`](input.md), [`cover.json`](cover.json).

> **Stato: BLOCCATO per la registrazione finale.** Riceve l'output del video 4 (`chain/04-map.bm`), che dipende dall'editor mappa 3D (`SYNC.md` AGY-01..03) e dalle API `map3d_*`
> (non ancora implementate). Le prove usano una mappa fatta a script, **mai nel video finale** (vedi `input.md`).

## Struttura del video finale

| Parte | Tempo | Note |
|---|---|---|
| Gancio | 0:00-0:06 | 6 s della scena finita: Pico corre nel corridoio con nebbia, lanterna e ombra (ritaglio dalla registrazione dell'anteprima) |
| Titolo | 0:06-0:10 | scheda "Tropic Trail - Episode 6 - bm SDK", senza voce |
| Registrazione principale | 0:10-2:52 | tre registrazioni di `bmhost --tool` montate in ordine (vedi `input.md`) |
| Chiusura | 2:52-3:00 | scheda "Next: bm Code", voce fuori campo |

Riceve: `video/3d/chain/04-map.bm` (mappa) e `video/3d/chain/05-sound.bm` (suono), uniti in `start.bm` da uno script (merge deterministico, in `input.md`).
Produce: `video/3d/chain/06-sdk.bm` (progetto assemblato: luci, ombre, nebbia, codice minimo di anteprima), costruito con `Ctrl+B`.

## Gancio (5-8 s)

0:00-0:06. L'anteprima gia' finita: Pico (geco verde) corre lungo il sentiero in una giungla velata di nebbia, una lanterna accesa dietro di lui, l'ombra sotto i piedi.
Voce: *"From a flat stage to a glowing jungle. Six lines of Lua."* (le sei righe scritte nel video: `sky3d`, `fog3d`, `local l`, `if l then lamp3d`, `shadow3d`, `draw3d` con flag 8)

## Script

| Tempo | Sullo schermo | Narrazione (English) |
|---|---|---|
| 0:00 | Gancio: la scena finita | From a flat stage to a glowing jungle. Six lines of Lua. |
| 0:06 | Scheda titolo | (nessuna voce) |
| 0:10 | `Ctrl+O`, `Enter`; `F1`: CONTENTS con codice, mappa, modelli, animazione, suono; `F4`: PICO, APPLE, CRATE; `F3` `F3`: i layer della mappa | bm SDK is where everything comes together. This cartridge already holds the models, the map and the sounds from the earlier videos. |
| 0:22 | `Ctrl+N`: elenco dei template; `3D with models`; poi `F2`: il codice del template | A new project starts from a template. Control N lists them. We use 3D with models: it draws the models of the project and plays their animation. |
| 0:34 | `F1`, `r`: la schermata passa a 480x270 | Press R on the project page: 480 by 270. It scales exactly four times to 1080p, and it has almost half the pixels of 640 by 360. |
| 0:46 | `F2`: si scrive `sky3d(...)` | Light first. The template already sets the sun's direction and the ambient level with light3d. sky3d adds three colours: the sun, the sky above, the ground below. |
| 1:04 | Si scrive `fog3d(...)` | fog3d fades the faces into a colour between two distances: twelve to forty units, the colour of the horizon, so the corridor melts away. |
| 1:20 | Si scrive `lamp3d(...)` | A lantern: lamp3d number one, at the lamp the map marked. Radius four, warm orange, a little extra light. Up to four at once. |
| 1:36 | Si scrive `shadow3d(1)` e la riga `draw3d(..., 8)` | A shadow is the same model drawn again with flag eight, flattened on the ground. shadow3d one makes it dithered black. |
| 1:54 | `Ctrl+Shift+S`, nome TROPIC; `F5`: l'anteprima, Pico corre da solo | Save it with a name, then F5 saves and runs. Pico runs on his own through light, fog and shadow. |
| 2:14 | `F1`, `F1`: la pagina del dev kit | Why 480 by 270? At sixty frames a second a frame has sixteen milliseconds. Fewer pixels means less to fill, and the dev kit shows the frames that go over. |
| 2:30 | `Ctrl+B`: "built TROPIC: it is in Games"; scheda con la tabella dei valori | Control B builds the game. These are the values we used. The next video puts them to work. |
| 2:52 | Scheda di chiusura | Next: bm Code. Pico moves, jumps, spins and collects apples. |

## Valori usati

| Chiamata | Valori | Cosa fa | Perche' questo valore |
|---|---|---|---|
| `r` (pagina progetto) | 640x360 -> **480x270** | dimensione dello schermo del gioco | decisione M33: 129600 pixel, il 56% di 640x360 (230400); 4x esatto su 1080p (480x4 = 1920, 270x4 = 1080) |
| `light3d(x, y, z, ambient)` | `-0.4, 0.8, -0.5, 0.45` | direzione del sole e luce ambiente (0-1) | gli stessi valori del template `3D with models` (`carts/editor/main.lua`) |
| `sky3d(sun, sky, ground)` | `0xFFE8B0, 0x80B8E8, 0x486038` | colore del sole, dell'ambiente da sopra, da sotto | sole caldo, cielo azzurro, rimbalzo verde dell'erba sulle facce rivolte in basso |
| `fog3d(colour, near, far)` | `0xA8D0C0, 12, 40` | le facce sfumano nel colore tra le due distanze | colore uguale a `cls()` per non vedere il bordo; 40 unita' = cio' che il gioco disegna davanti a Pico; **valori di partenza da tarare in prova** |
| `lamp3d(i, x, y, z, radius, k, colour)` | `1, l.x, l.y + 1.2, l.z, 4, 0.8, 0xFFC888` | luce puntiforme 1 (di 1-4) | raggio 4 e colore `0xFFC888` sono il contratto di `LAMP` in `SYNC.md` §2; `k` 0.8 = un po' meno del massimo (1) per non bruciare il verde; 1.2 unita' sopra il suolo |
| `shadow3d(style)` | `1` | ombre di `draw3d` con flag 8 | 1 = nero a retino, senza leggere lo schermo; la GPU disegna sempre cosi' |
| `draw3d(m, x, y, z, rx, ry, rz, scale, flags)` | `pico, px, py, pz, 0, 0, 0, 1, 8` | con flag 8 disegna **solo l'ombra** sul piano `y` del punto | `py` e' la quota del suolo (`map3d_height`), si disegna due volte: prima l'ombra, poi Pico |
| `camera3d(x, y, z, yaw, pitch, fov)` | `px, py + 2.4, pz - 5, 0, -0.32, 60` | camera dietro e sopra Pico, guarda verso z crescente | fov 60 e' il valore di partenza della camera |
| `cls(colour)` | `0xA8D0C0` | sfondo | uguale alla nebbia |

## Perche' 60 fps a 480x270

1. **Meno pixel da riempire.** Il rasterizzatore 3D della Pi Zero e' software (ARM, anche in QEMU e bmhost): il costo cresce con i pixel disegnati. 480x270 ne ha 129600 contro 230400 di 640x360: il 44% in meno.
2. **Intero esatto.** 4x = 1920x1080: nessuna sfocatura e nessun pixel di bordo sul televisore.
3. **Costo per faccia, non per pixel.** La luce di `draw3d` e' per faccia (`docs/api/light3d.md`); luce, lanterna e nebbia non aggiungono un passaggio per ogni pixel. Il bilancio e' il numero di triangoli: Pico <= 300, terreno visibile <= 6000 (PIANO.md).
4. **A 60 fps un frame dura 16.7 ms.** La pagina dev kit (F1 due volte) conta i frame oltre 16.7 ms nell'ultima prova (manuale, "Budgets and the dev kit report").
5. **Attenzione:** `bmhost` ha un orologio virtuale e dice sempre 60: **non misura il costo**. I 60 fps veri vanno misurati sulla console (o in QEMU per una prova del kernel) con `LAST TRY`. `[DA VERIFICARE: 60 fps reali a 480x270 con la mappa vera e la lanterna]`.

## Limiti onesti

- La nebbia e le luci non tolgono triangoli: se la misura dice no, si scende di scena (meno oggetti con `visible3d`), non di risoluzione (resta 480x270).
- `lamp3d` illumina le facce con il centro dentro il raggio: i triangoli grandi mostrano a scatti il bordo della luce. Il terreno e' fatto di pezzi piccoli (tessere da 1 unita'), quindi va bene.
- L'ombra con flag 8 e' una proiezione sul piano `y` dato: sul gradino e sulle buche ricade sul piano sbagliato. Nel livello (una sola quota per frame sotto Pico) e' accettabile.

## Capitoli (identici a youtube.md)

```
0:00 The finished look
0:10 Open the cartridge
0:22 The 3D with models template
0:34 Screen size: 480x270
0:46 Sun and sky
1:04 Fog
1:20 A lantern
1:36 Shadows
1:54 Preview with F5
2:14 Why 60 fps
2:30 Build with Ctrl+B
```

## [DA VERIFICARE] di questo video

- **Come portare modelli, mappa e suoni in un progetto da template.** In `carts/editor/main.lua` `new_project()` chiama `cart_new()` e azzera tutto: `Ctrl+N` + template **non** conserva i modelli. La Lib tab (`Copy into a project...`) e' nascosta (`lib_tab=1`) e `bmhost` non ha il menu del kernel. Soluzione provvisoria usata nei documenti: `start.bm` contiene gia' il codice del template (adattato), e il video mostra l'elenco con `Ctrl+N` senza confermare (`Esc`). Da decidere con l'utente.
- Se `Esc` chiude l'elenco dei template senza cambiare il progetto.
- Movimento del cursore nell'editor di codice F2 (quanti `↓`, effetto di `End` e `Enter`, rientro automatico).
- Il nome proposto da *Save as* (`MYGAME.BME`) si cancella digitando o va cancellato con Backspace; nome finale `TROPIC`.
- `F5` e `Ctrl+B` sotto `bmhost --tool`, e dove finisce il `.bm` costruito.
- Cosa mostra `LAST TRY` dopo una prova sotto `bmhost` (orologio virtuale).
- Direzione in cui guarda Pico (rotazione `ry`) e yaw 0 della camera verso z crescente.
- Editor mappa 3D e API `map3d_*`: non esistono ancora (AGY-01..03).
