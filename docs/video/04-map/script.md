# Video 4 — il sentiero: editor mappa 3D di bm SDK

Serie: Tropic Trail (episodio 4 di 8). Durata: **3:15** (195 s, 11700 frame a 60 fps). Ritmo calmo: ogni azione resta a schermo il tempo di leggerla.
Narrazione e didascalie in **inglese**; la prosa di servizio è in italiano.

Lo script di registrazione è [`video/3d/04-map/record.py`](../../../video/3d/04-map/record.py); il controllo del progetto salvato è `video/3d/04-map/verify.py`.
Tutti i tasti sono stati provati dal vivo con `bmhost` e screenshot (`video/3d/04-map/proto.py`). Le frecce con Shift (inclinazione 3D) non si possono
digitare via seriale: il video **non** mostra l'inclinazione. Requisiti dell'editor: [`editor-requirements.md`](editor-requirements.md).

- **Strumento**: bm SDK ▸ pagina Map (`F3`, poi `F3` di nuovo). Edizione Pi (`carts/editor`); l'RGB30 viene dopo (AGY-04).
- **Parte da**: `TROPIC.BME` (progetto del video 3: modelli `PICO`, `APPLE`, `CRATE` con scheletro e clip; sheet con le tessere del terreno dal video 2),
  aperto con `Ctrl+O` e `Invio`. **Produce**: `TROPIC.BME` salvato con i layer `floor`, `height`, `walls`, `objects` e `TROPIC.BM` costruito con `Ctrl+B`. Vedi [`input.md`](input.md).
- Il tasto `T` applica il **modello 3D**: crea i layer `floor`, `height`, `walls`, `objects` e imposta il pennello 16×16. Sostituisce i vecchi `Shift+L` ×3 più
  la rinomina; la rinomina dei layer non esiste. Contratto dei layer e dei segnaposto: `SYNC.md` §2 e `docs/video/PIANO.md` §2 (id pari: 1024 start, 1026 APPLE, 1028 CRATE, 1030 GOAL, 1032 LAMP).

## Copione

| Tempo | Sullo schermo | Narrazione (English) |
|---|---|---|
| 0:00 | Gancio: titolo "Tropic Trail · Episode 4 · the 3D map"; sullo schermo l'SDK attende (il sentiero 3D finito è mostrato dal montaggio finale, nessuna anteprima 3D all'inizio) | This is the whole trail Pico will run, drawn with numbers on a grid. |
| 0:08 | `Ctrl+O` poi `Invio` (apre `TROPIC.BME`), `F3` due volte: la pagina Map | bm SDK has a map editor. Open the project with Control O. Press F3 twice. A map is a grid of numbers in layers. |
| 0:20 | `T` applica il modello 3D (layer `floor`, `height`, `walls`, `objects`, pennello 16×16); `l` scorre i layer e torna a `floor` | Floor says where the ground is. Height says how high. Walls are solid blocks. Objects holds apples, crates, lanterns, and where Pico starts. |
| 0:38 | Layer `floor`: cursore a destra, `Shift+W` due volte (larghezza 3), `Shift+P`, `Invio`, giù ×20, `Invio` (corridoio); su ×8 e `Backspace` (buca); `m` panoramica, `m` ritorno | Pick the grass tile with Tab. The big brush is on: one tile is one unit of world. Shift W sets the width, Shift P draws the path between two cells. A cell left empty is a hole. |
| 1:05 | Layer `height`: `l`, `.` due volte, `Space` (gradino), giù ×2, `Space`; layer `walls`: `l`, `.`, `Space` | In the height layer a number is a quarter of a unit. Two quarters make a step. In walls, a number is a solid block that Pico cannot cross. |
| 1:28 | Layer `objects`: `l`, `Shift+A` `Invio`, giù ×6, `Invio` (fila di mele); `Shift+C` (cassa); giù ×2, `Shift+O` (lanterna) | Shift A lays a row of apples. Shift C puts a crate on a cell. Shift O adds a lantern. |
| 1:55 | La barra di stato: mele, casse, lanterne, triangoli stimati; `Ctrl+Z` annulla l'ultimo tratto (la lanterna), `Shift+O` di nuovo, destra ×4, di nuovo la fila di mele con `Shift+A` | The status line counts apples, crates and lanterns, and estimates the triangles. Control Z takes back the last stroke in one step. |
| 2:10 | `v` passa all'anteprima 3D; `←` `→` orbitano, `↑` `↓` ingrandiscono; `v` torna al 2D | Press V. The same code that builds the world in the game builds this preview, so what you see is what you play. Left and right orbit, up and down zoom. |
| 2:40 | `Ctrl+S` salva nel progetto; `Ctrl+B` genera il `.bm` | Control S saves the project. Control B builds the cartridge, with the four layers inside the file. |
| 2:55 | Cartello finale: il video seguente | The trail is done. The next video gives Pico a voice: six soft sound effects and a jungle song, all made in bm Sound. |

Il tasto `z` (alterna il pennello) non viene usato: il pennello 16×16 lo imposta `T`.

## Capitoli (identici a youtube.md)

0:00 The trail · 0:08 The map page · 0:20 Four layers · 0:38 Floor and holes · 1:05 Height and walls · 1:28 Apples, crates, lanterns, start and goal ·
1:55 Counts and undo · 2:10 The 3D preview · 2:40 Save and build · 2:55 What is next

## Note verificate sul codice

1. **Nessuna rinomina dei layer**: `Shift+L` crea `layer2`, `layer3`…; i nomi `floor`, `height`, `walls`, `objects` li dà `T` (modello 3D).
2. **Segnaposto e pennello 16×16**: ogni "tessera" scrive 2×2 celle; `map3d_objects` legge la cella in alto a sinistra del blocco. Gli id sono pari (vedi `SYNC.md` §2).
3. **`verify.py`** controlla: mappa 256×256, 4 layer, corridoio largo almeno 3 celle e lungo 10 righe con una buca, almeno un gradino e un muro, mele 1026, cassa 1028, lanterna 1032 (rimessa dopo `Ctrl+Z`), sezioni MESH/ANIM/SHEET mantenute, `TROPIC.BM` con gli stessi layer, `bm_forge check`.
