# Video 6 — shot list (bm SDK)

Una riga = un'azione. Tempi del **video finale** (hook + titolo = 0:10 di scarto), coerenti con [`script.md`](script.md). Didascalie in inglese, max 70 caratteri.
Tasti dal manuale `manuals/content/en/editor-sdk.md` (capitoli "The screen", "Creating a project from a template", "Building and exporting", tabella "Keys").
Il video e' montato da **tre registrazioni** di `bmhost --tool` (R1, R2, R3: vedi [`input.md`](input.md)).

## Convenzioni

- Tasti per bmhost: `F1..F5 = \xf9..\xfd`, frecce `\xf0..\xf3`, `End \xf5`, Invio `\n`, spazio `\x20`, `Ctrl+O \x0f`, `Ctrl+N \x0e`, `Ctrl+S \x13`, `Ctrl+B \x02`, `Esc \x1b`.
- Il progetto si apre come **`TROPIC.BME`** (`start.bme`, flag progetto): un gioco `.bm` al salvataggio chiede "Save an editable copy: TROPIC.BME? Enter Copy / Esc No" e quel dialogo del kernel blocca `bmhost`.
- Nell'editor di codice (F2) si scrive senza indentazione. `↓ xN` = N pressioni di freccia giu' fino alla riga-marcatore (`-- [sky]` ecc.), con N calcolato da `record.py` (`move()`) contando le righe di `preview.lua`; ogni riga digitata sta sotto il suo marcatore.
- Ordine nel tempo: sky, fog, lamp, shadow, shadow-draw (`-- [shadow]` sta prima di `-- [sky]` nel file, quindi dopo `-- [lamp]` il cursore sale).
- Testi digitati: `type_text(..., per_key=4)`.
- La scena di partenza ha gia' il codice del template adattato (vedi `input.md`, "Il codice di partenza"): le sei righe nuove si scrivono sotto i marcatori.

## R1 — Editor (0:10-2:02)

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 1 | 0:10 | Dev > bm SDK | `Ctrl+O` | l'elenco di cartucce e progetti | Ctrl+O: open |
| 2 | 0:12 | Dev > bm SDK | `Enter` | si apre il progetto TROPIC.BME | Enter: open TROPIC |
| 3 | 0:14 | SDK > F1 progetto | `F1` | CONTENTS: righe di codice, mappa e layer, modelli, animazione, suono | F1: the project page |
| 4 | 0:17 | SDK > F4 3D | `F4` | i modelli del progetto in lista | F4: models and animations |
| 5 | 0:18 | SDK > F4 3D | `↓` x2 | PICO, APPLE, CRATE uno dopo l'altro | Up and down: the model |
| 6 | 0:20 | SDK > F4 3D | `→` | l'animazione cambia (idle, run, jump, spin, hit) | Left and right: the animation |
| 7 | 0:21 | SDK > F3 mappa | `F3`, `F3`, `l` x3 | la mappa; la barra dice il nome del layer (floor, height, walls, objects) | F3 twice: the map layers |
| 8 | 0:24 | SDK | `Ctrl+N` | l'elenco degli 8 template | Ctrl+N: new project from a template |
| 9 | 0:24 | SDK | `↓` x7 | evidenziato "3D with models" con la sua descrizione | 3D with models: the models, animated |
| 10 | 0:27 | SDK | `Esc` (chiude l'elenco senza cambiare il progetto) | si torna al progetto aperto | (the cartridge already uses it) |
| 11 | 0:30 | SDK > F2 codice | `F2` | il codice di `preview.lua` (`_init`, `_update`, `_draw`) | F2: the code |
| 12 | 0:35 | SDK > F1 progetto | `F1` | pagina progetto, riga SCREEN `640x360` | F1: the project page |
| 13 | 0:37 | SDK > F1 progetto | `r` | SCREEN passa a `480x270` | r: screen size, 480x270 |
| 14 | 0:46 | SDK > F2 codice | `F2` | il codice | F2: the code |
| 15 | 0:48 | SDK > F2 codice | `↓` xN (fino a `-- [sky]`) | il cursore sul marcatore | Down to the marker |
| 16 | 0:50 | SDK > F2 codice | `End`, `Enter` | nuova riga vuota | A new line |
| 17 | 0:51 | SDK > F2 codice | `sky3d(0xFFE8B0, 0x80B8E8, 0x486038)` (R1 40-44 s) | la riga scritta, colorata | sky3d: sun, sky, ground |
| 18 | 1:04 | SDK > F2 codice | `↓` xN (fino a `-- [fog]`), `End`, `Enter` | cursore sul marcatore, riga vuota | Down to the next marker |
| 19 | 1:07 | SDK > F2 codice | `fog3d(0xA8D0C0, 12, 40)` (R1 54-58 s) | la riga scritta | fog3d: colour, near 12, far 40 |
| 20 | 1:18 | SDK > F2 codice | `↓` xN (fino a `-- [lamp]`, dentro `_draw`), `End`, `Enter` | cursore nel disegno | The lantern goes in _draw |
| 21 | 1:21 | SDK > F2 codice | `local l = lamps[1]`, `Enter`, `if l then lamp3d(1, l.x, l.y + 1.2, l.z, 4, 0.8, 0xFFC888) end` (R1 68-72 s) | due righe scritte | lamp3d: light 1, radius 4 |
| 22 | 1:34 | SDK > F2 codice | `↑` xN (fino a `-- [shadow]`, in `_init`), `End`, `Enter`, `shadow3d(1)` (R1 84-88 s) | una riga | shadow3d(1): dithered black |
| 23 | 1:44 | SDK > F2 codice | `↓` xN (fino a `-- [shadow-draw]`), `End`, `Enter`, `draw3d(pico, px - 0.75, py, pz, 0, 0, 0, 0.5, 8)` (R1 94-98 s) | la riga del flag 8, subito prima del `draw3d` di Pico | flag 8: draw only the shadow |
| 24 | 1:54 | SDK | `Ctrl+S` | la barra dice che `TROPIC.BME` e' salvato (nessun "Save as", nessun nome da digitare) | Ctrl+S: save |
| 25 | 2:00 | SDK | `F5` (solo overlay: F5 avvierebbe la cartuccia e chiuderebbe la registrazione) | la sovrimpressione mostra F5 | F5: save and run |

## R2 — Cartuccia in esecuzione (2:02-2:14)

Il gioco e' `out/06-sdk.bm` (il progetto senza flag progetto, scritto da `record.py`); gira 12 s senza tasti.

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 26 | 2:02 | Gioco in anteprima | (nessun tasto: Pico corre da solo) | camera dietro a Pico; il corridoio sfuma nella nebbia | Pico runs on his own |
| 27 | 2:06 | Gioco in anteprima | (nessun tasto) | passa vicino a una lanterna: il verde si scalda in un raggio di 4 unita' | The lantern lights the path |
| 28 | 2:10 | Gioco in anteprima | (nessun tasto) | l'ombra sotto i piedi di Pico, retinata | The shadow follows Pico |

## R3 — Dev kit e Build (2:14-2:52)

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 29 | 2:14 | SDK > F1 progetto | `F1` | pagina progetto con LAST TRY (con `bmhost` l'orologio e' virtuale: valori non misurati) | F1: LAST TRY |
| 30 | 2:18 | SDK > F1 dev kit | `F1` | dev kit: codice, dati in memoria, file | F1 again: the dev kit |
| 31 | 2:30 | SDK | `Ctrl+B` (R3, 16 s: `bmhost --tool` non costruisce; si registra il tasto) | la pagina dev kit con il tasto in sovrimpressione | Ctrl+B: build the game |
| 32 | 2:34 | SDK | (nessun tasto) | scheda in post con la tabella "Valori usati" di `script.md` | The values we used |
| 33 | 2:50 | SDK | (nessun tasto) | fine | Next: bm Code |

## Tasti nuovi in questo video

Rispetto alla serie (fonte: tabella "Keys" del manuale dell'SDK): `Ctrl+N` (nuovo da template), `r` (dimensione schermo), `F3` due volte (mappa), `F4` (modelli, `↑ ↓ ← →`), `Ctrl+S` (salva il progetto),
`F5` (solo sovrimpressione), `Ctrl+B` (build, solo tasto in R3), `F1` due volte (dev kit); in F2 `End`, `Enter`, frecce per il codice. Non usati: `F6` (assistente), `F9`, `b` (target `.b16`), `Ctrl+Shift+B`, `Ctrl+Shift+S`.

## Da misurare

- `LAST TRY` (riga 29) mostra valori veri solo sulla console: `bmhost` ha un orologio virtuale.
