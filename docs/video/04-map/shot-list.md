# Video 4 — shot list (editor mappa 3D)

Una riga = un'azione. Tempi coerenti con [`script.md`](script.md) e con i capitoli di `video/3d/04-map/record.py` (totale 3:15). Tutti i tasti sono stati provati
dal vivo con `bmhost` e screenshot (`video/3d/04-map/proto.py`). Le sotto-righe di un capitolo sono scandite dai `wait` di `record.py`: i tempi esatti vengono dal file `out/input.txt`.

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 1 | 0:00 | bm SDK | (nessuno) | l'SDK attende; il sentiero 3D finito è mostrato dal montaggio finale, nessuna anteprima 3D | Tropic Trail · Episode 4 · the 3D map |
| 2 | 0:08 | bm SDK ▸ progetto | `Ctrl+O` | finestra di apertura progetto | Ctrl+O · open the project |
| 3 | 0:10 | bm SDK ▸ progetto | `Invio` | si apre `/carts/TROPIC.BME` | Enter |
| 4 | 0:14 | bm SDK ▸ F3 sprite | `F3` | foglio degli sprite con le tessere del terreno (video 2) | F3 · the sprites |
| 5 | 0:16 | bm SDK ▸ F3 mappa | `F3` | pagina Map: griglia vuota | F3 · the map |
| 6 | 0:20 | Map ▸ layer | `T` (lettera maiuscola) | modello 3D: nascono `floor`, `height`, `walls`, `objects`; pennello 16×16 | T · 3D template |
| 7 | 0:26 | Map ▸ layer | `l` ×4 | scorre `height`, `walls`, `objects` e torna a `floor` | l · next layer |
| 8 | 0:38 | Map ▸ `floor` | frecce destra ×6 | il cursore si sposta | (nessuna) |
| 9 | 0:41 | Map ▸ `floor` | `Shift+W` ×2 | larghezza del percorso 3 | Shift+W · path width 3 |
| 10 | 0:45 | Map ▸ `floor` | `Shift+P` | modo "percorso" | Shift+P · path tool |
| 11 | 0:47 | Map ▸ `floor` | `Invio` | primo estremo marcato | Enter · first cell |
| 12 | 0:49 | Map ▸ `floor` | giù ×20, `Invio` | il corridoio si disegna | Enter · last cell |
| 13 | 0:55 | Map ▸ `floor` | su ×8, `Backspace` | una buca nel corridoio | Backspace · a hole |
| 14 | 1:00 | Map ▸ `floor` | `m` poi `m` | panoramica 256×256 e ritorno a 1:1 | M · overview |
| 15 | 1:05 | Map ▸ `height` | `l` | barra di stato: layer `height` | Layer height |
| 16 | 1:08 | Map ▸ `height` | `.` ×2, `Space` | valore 2 (mezza unità): un gradino | . · Space · a step |
| 17 | 1:13 | Map ▸ `height` | giù ×2, `Space` | un secondo gradino | Space · a second step |
| 18 | 1:20 | Map ▸ `walls` | `l` | barra di stato: layer `walls` | Layer walls |
| 19 | 1:23 | Map ▸ `walls` | `.`, `Space` | un blocco solido | Space · a solid block |
| 20 | 1:28 | Map ▸ `objects` | `l` | layer `objects` | Layer objects |
| 20a | 1:30 | Map ▸ `objects` | su ×15, `Shift+S` | la partenza del livello (segnaposto 1024) | Shift+S · the start |
| 20b | 1:31 | Map ▸ `objects` | giù ×19, `Shift+G` | il traguardo all'altro capo (segnaposto 1030) | Shift+G · the goal |
| 21 | 1:34 | Map ▸ `objects` | su ×15, `Shift+A`, `Invio`, giù ×6, `Invio` | una fila di mele (segnaposto 1026) | Shift+A · a row of apples |
| 22 | 1:42 | Map ▸ `objects` | `Shift+C` | una cassa (segnaposto 1028) | Shift+C · a crate |
| 23 | 1:46 | Map ▸ `objects` | giù ×2, `Shift+O` | una lanterna (segnaposto 1032) | Shift+O · a lantern |
| 24 | 1:55 | Map ▸ `objects` | (nessuno) | barra di stato: mele, casse, lanterne, triangoli | Counts and triangles |
| 25 | 1:58 | Map ▸ `objects` | `Ctrl+Z` | l'ultimo tratto (la lanterna) sparisce in un colpo | Ctrl+Z · undo a stroke |
| 26 | 2:01 | Map ▸ `objects` | `Shift+O` | la lanterna torna | Shift+O · again |
| 27 | 2:03 | Map ▸ `objects` | destra ×4, `Shift+A`, `Invio`, giù ×6, `Invio` | una seconda fila di mele | Shift+A · another row |
| 28 | 2:10 | Map ▸ vista 3D | `v` | anteprima 3D | V · 3D preview |
| 29 | 2:15 | Map ▸ vista 3D | `←` `→` | la camera orbita | ← → · orbit |
| 30 | 2:25 | Map ▸ vista 3D | `↑` `↓` | zoom | ↑ ↓ · zoom |
| 31 | 2:35 | Map ▸ vista 3D | `v` | torna alla griglia 2D | V · back to the grid |
| 32 | 2:40 | Map | `Ctrl+S` | messaggio `saved` | Ctrl+S · save the project |
| 33 | 2:47 | Map | `Ctrl+B` | il `.bm` viene generato | Ctrl+B · build the cartridge |
| 34 | 2:55 | cartello finale | (nessun tasto) | "Next: sound" | Next: bm Sound |

## Tasti del video

`Ctrl+O` poi `Invio`, `F3` poi `F3` (pagina Map), `T` modello 3D, `l` layer successivo, `Shift+W` larghezza del percorso, `Shift+P` percorso, `Invio` estremi,
`Backspace` svuota, `m` panoramica, `.` valore successivo, `Space` posa, `Shift+S` partenza, `Shift+G` traguardo, `Shift+A` mele, `Shift+C` casse, `Shift+O` lanterne, `Ctrl+Z` annulla l'ultimo tratto,
`v` anteprima 3D (`←` `→` orbita, `↑` `↓` zoom), `Ctrl+S`, `Ctrl+B`. Non usati: `z` (alterna il pennello) e le frecce con Shift (non digitabili via seriale).
