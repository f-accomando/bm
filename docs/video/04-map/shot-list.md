STATO: BLOCCATO finché AGY-01..03 non sono DONE in bm-core

# Video 4 — shot list (editor mappa 3D) — PROVVISORIA

Una riga = un'azione. Tempi coerenti con [`script.md`](script.md). I tasti **senza** marcatura sono quelli veri della mappa 2D
(manuale `manuals/content/en/editor-sdk.md`, "The map editor" e tabella "Keys"; `carts/editor/main.lua`). Ogni riga che usa un tasto proposto da
`SYNC.md` (AGY-02/03: `v`, `Shift+P`, `Shift+A`, `Shift+C`, `Shift+O`, `[` `]`, `Ctrl+Z`, `,` `.`) o un comportamento ancora inesistente è marcata
**PROVVISORIO**. Ricontrollare ogni riga marcata quando AGY-01..03 saranno `DONE`.

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 1 | 0:00 | bm SDK ▸ Map ▸ vista 3D | PROVVISORIO (ripresa finale; richiede `v`) | il sentiero finito, camera che gira | Tropic Trail · Episode 4 · the 3D map |
| 2 | 0:08 | Dev ▸ bm SDK | (cartello; `bmhost` non ha la scheda Dev) | titolo dello strumento | Dev tab ▸ bm SDK |
| 3 | 0:11 | bm SDK ▸ F1 progetto | `Ctrl+O`, scelta del file `[DA VERIFICARE: nome e posizione nell'elenco]` | il progetto aperto | Ctrl+O · open the project |
| 4 | 0:15 | bm SDK ▸ F3 sprite | `F3` | foglio degli sprite con le tessere del terreno (video 2) | F3 · the sprites |
| 5 | 0:17 | bm SDK ▸ F3 mappa | `F3` | pagina Map: griglia e pennello | F3 again · the map |
| 6 | 0:22 | Map ▸ layer | `Shift+L` | nasce `layer2` (dice `map(..., "layer2")`) | Shift+L · a new layer |
| 7 | 0:25 | Map ▸ layer | `Shift+L` ×2 | `layer3`, `layer4`: 4 layer in tutto | Shift+L ×2 · four layers |
| 8 | 0:28 | Map ▸ layer | PROVVISORIO: rinominare i 4 layer `floor`, `height`, `walls`, `objects` `[DA VERIFICARE: azione e tasto; oggi non esiste]` | i nomi nella barra di stato | Names the game reads: floor · height · walls · objects |
| 9 | 0:34 | Map ▸ layer | `l` ×3 poi `o` | scorre i layer; "this layer only" | L · next layer · O · this layer only |
| 10 | 0:40 | Map ▸ `floor` | `l` fino a `floor` | barra di stato: layer `floor` | Layer: floor |
| 11 | 0:42 | Map ▸ `floor` ▸ selettore | `Tab`, frecce, `Invio` `[DA VERIFICARE: conferma del selettore]` | scelta della tessera d'erba | Tab · choose the grass tile |
| 12 | 0:46 | Map ▸ `floor` | `z` | pennello 16×16 (2 × 2 celle) | Z · big brush: 1 tile = 1 unit |
| 13 | 0:50 | Map ▸ `floor` | PROVVISORIO: `Shift+P` | modo "percorso" nella barra di stato | Shift+P · lay a path |
| 14 | 0:52 | Map ▸ `floor` | PROVVISORIO: frecce + `Invio` (prima cella) | primo estremo marcato | Arrows · Enter: first cell |
| 15 | 0:55 | Map ▸ `floor` | PROVVISORIO: frecce + `Invio` (seconda cella) | il corridoio si disegna | Arrows · Enter: last cell |
| 16 | 0:58 | Map ▸ `floor` | PROVVISORIO: `]` ×2 | larghezza 3 (da 1 a 5) | ] · wider path |
| 17 | 1:01 | Map ▸ `floor` | frecce, `Backspace` (2 volte, due celle) | due buche nel corridoio | Backspace · an empty cell is a hole |
| 18 | 1:05 | Map ▸ `floor` | `m` poi `m` | panoramica 256×256 e ritorno | M · overview |
| 19 | 1:10 | Map ▸ `height` | `l` fino a `height` | barra di stato: layer `height` | Layer: height |
| 20 | 1:13 | Map ▸ `height` | PROVVISORIO: `.` ×2 (valore 2 = 0,5 unità) `[DA VERIFICARE: come si sceglie il valore]` | valore 2 nella barra | . · value 2 = half a unit |
| 21 | 1:16 | Map ▸ `height` | `f` (riempimento) `[DA VERIFICARE: effetto con il pennello 16×16]` | il gradino occupa le celle dopo il punto | F · fill the step |
| 22 | 1:25 | Map ▸ `walls` | `l` fino a `walls` | barra di stato: layer `walls` | Layer: walls |
| 23 | 1:27 | Map ▸ `walls` | PROVVISORIO: `.` ×1 (valore 1 = 0,25), `Space` | un muro basso sul corridoio | Space · a solid block |
| 24 | 1:35 | Map ▸ `objects` | `l` fino a `objects` | il pennello diventa 16×16 e sceglie il primo segnaposto | Layer: objects |
| 25 | 1:38 | Map ▸ `objects` | `Tab`, scelta del segnaposto 1024 `PLAYER_START`, `Invio`, frecce, `Space` `[DA VERIFICARE: ids, vedi script.md Problemi 2]` | la partenza all'inizio | Tab · Space: the start |
| 26 | 1:43 | Map ▸ `objects` | `Tab`, segnaposto 1030 `GOAL`, `Invio`, frecce, `Space` | l'arrivo in fondo | Tab · Space: the goal |
| 27 | 1:48 | Map ▸ `objects` | PROVVISORIO: `Shift+A`, frecce + `Invio` (prima cella), frecce + `Invio` (seconda) | una fila di mele (segnaposto 1026) | Shift+A · a row of apples |
| 28 | 1:55 | Map ▸ `objects` | PROVVISORIO: `Shift+C`, frecce + `Invio` | una cassa (segnaposto 1028) | Shift+C · a crate |
| 29 | 2:00 | Map ▸ `objects` | PROVVISORIO: `Shift+O`, frecce + `Invio` | una lanterna (segnaposto 1032) | Shift+O · a lantern |
| 30 | 2:05 | Map ▸ `objects` | PROVVISORIO (nessun tasto) | barra di stato: mele, casse, lanterne, triangoli | Counts and triangles |
| 31 | 2:10 | Map ▸ `objects` | PROVVISORIO: `Ctrl+Z` | l'ultima fila di mele sparisce in un colpo | Ctrl+Z · undo a whole stroke |
| 32 | 2:15 | Map ▸ `objects` | PROVVISORIO: `Shift+A` (ripetuto) | la fila torna | Again |
| 33 | 2:20 | Map ▸ vista 3D | PROVVISORIO: `v` | passa all'anteprima 3D | V · 3D preview |
| 34 | 2:24 | Map ▸ vista 3D | PROVVISORIO: `←` `→` | la camera orbita attorno al cursore | ← → · orbit |
| 35 | 2:30 | Map ▸ vista 3D | PROVVISORIO: `↑` `↓` | zoom | ↑ ↓ · zoom |
| 36 | 2:36 | Map ▸ vista 3D | PROVVISORIO: `,` poi `.` | inclinazione | , . · tilt |
| 37 | 2:44 | Map ▸ vista 3D | PROVVISORIO: `v` | torna alla griglia 2D | V · back to the grid |
| 38 | 2:50 | Map | `Ctrl+S` (se è un `.bm`: domanda della copia modificabile `[DA VERIFICARE]`) | messaggio `saved` | Ctrl+S · save the project |
| 39 | 2:55 | Map | `Ctrl+B` | il `.bm` viene generato | Ctrl+B · build the cartridge |
| 40 | 3:00 | Map | (nessuno) | messaggio di fine build | Four layers inside the file |
| 41 | 3:10 | cartello finale | (nessun tasto) | "Next: sound" | Next: bm Sound |

## Tasti nuovi del video

Veri (mappa 2D, da mostrare in sovrimpressione la prima volta): `F3` poi `F3` (pagina Map), `Shift+L` nuovo layer, `l` layer successivo, `o` solo questo layer,
`Tab` selettore di tessere, `Space` posa, `Backspace` svuota, `z` pennello 8×8 / 16×16, `f` riempimento, `m` panoramica, `Ctrl+S`, `Ctrl+B`.

PROVVISORI (proposti in `SYNC.md`, non esistono ancora): `v` (2D / 3D), `Shift+P` percorso, `Shift+A` mele, `Shift+C` casse, `Shift+O` lanterne,
`[` `]` larghezza del percorso, `Ctrl+Z` annulla un tratto intero, `,` `.` inclinazione della camera in 3D, `←` `→` `↑` `↓` come camera in 3D; rinomina dei layer (da definire).
Conflitti già noti con tasti esistenti: vedi `script.md`, "Problemi trovati", punti 3 e 4.
