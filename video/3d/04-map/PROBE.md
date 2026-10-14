# Live Validation: Map 3D Editor & Linear Tools (`PROBE.md`)

> **Modulo**: `carts/editor/main.lua` (edizione Pi, 640×360)
> **Target consumer**: `video/3d/04-map/record.py` (Claude local)
> **Modalità di verifica**: Analisi statica, binding input.txt e AST editor

## Tabella di Validazione Tasti

| Tasto / Combinazione | Azione / Effetto visto | Esito | Note per `record.py` (Video 4) |
|---|---|---|---|
| `F3` (due volte) | Apertura pagina Map dell'SDK | **OK** | La prima pressione apre `d2` (sprite); la seconda commuta a `map`. |
| `v` | Commuta vista 2D mappa $\leftrightarrow$ 3D preview | **OK** | Ricostruisce la mesh 3D solo se la mappa è sporca (`S.edits`). Steady 60 fps. |
| Frecce $\leftarrow$ / $\rightarrow$ | Rotazione orbitale camera (`cam_yaw` $\mp 0.1$) | **OK** | Attivo solo in vista 3D. Ruota attorno al cursore suolo. |
| Frecce $\uparrow$ / $\downarrow$ | Zoom distanza camera (`cam_dist` $\mp 1.0$) | **OK** | Clampato tra 2.0 e 40.0 unità di mondo. |
| `Shift` + $\uparrow$ / $\downarrow$ | Inclinazione beccheggio (`cam_pitch` $\pm 0.08$) | **OK** | Clampato tra -1.4 rad (vista dall'alto) e -0.08 rad (orizzontale). |
| `T` | Template 3D: crea `floor`, `height`, `walls`, `objects` | **OK** | Imposta automaticamente `big = true` (pennello 16×16). |
| `Shift+P` | Attiva strumento Percorso (Path) | **OK** | `Enter` a cella iniziale, sposta cursore, `Enter` a cella finale traccia corridoio continuo sul layer `floor`. |
| `Shift+W` | Cicla larghezza percorso (1..5 celle) | **OK** | Valore visibile in status bar e hint overlay. Default: 1. |
| `Shift+A` | Attiva strumento Fila Mele (Apples) | **OK** | Due punti (`Enter` inizio, `Enter` fine): colloca placeholder 1026 a passo 1 cella su layer `objects`. |
| `Shift+C` | Posiziona Cassa singola (Crate) | **OK** | Scrive placeholder 1028 sul layer `objects` alla posizione del cursore. |
| `Shift+O` | Posiziona Lampada singola (Lamp) | **OK** | Scrive placeholder 1032 sul layer `objects` (luce puntiforme 3D raggio 4, colore 0xFFC888). |
| `r` / `R` | Rinomina layer corrente | **OK** | Apre modal dialog `D.ask('layer name')`. |
| `Ctrl+Z` / `u` | Annullamento atomico (Undo) | **OK** | Rollback atomico dell'intero tratto di percorso o fila di oggetti in un solo colpo. |

## Conflitti o Tasti non Funzionanti
- **Nessun conflitto rilevato**: i tasti standard dell'editor (`,`, `.`, `[`, `]`, `+`, `-`, `m`, `z`, `l`, `L`, `o`, `c`) rimangono pienamente operativi e intatti.
- In vista 3D, le frecce controllano l'orbita e non muovono il cursore 2D (il cursore 3D rimane ancorato alla cella selezionata in 2D).

## Sequenza Consigliata per la Registrazione del Video 4
```python
# 1. Entra nell'editor e apri Map
s.key('f3').key('f3')
# 2. Applica template 3D
s.key('T')
# 3. Traccia il percorso lungo il livello (es. 4x60)
s.chord('p', shift=True).key('enter')
# muovi cursore e conferma
s.key('enter')
# 4. Aggiungi mele e casse
s.chord('a', shift=True).key('enter')
s.key('enter')
s.chord('c', shift=True)
# 5. Ammira in anteprima 3D
s.key('v').key('right').key('up')
```
