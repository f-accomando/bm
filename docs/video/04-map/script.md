STATO: BLOCCATO finché AGY-01..03 non sono DONE in bm-core

# Video 4 — il sentiero: editor mappa 3D di bm SDK (PROVVISORIO)

Serie: Tropic Trail (episodio 4 di 8). Durata prevista: **3:30**. Ritmo calmo: ~130 parole/minuto, ogni azione resta a schermo il tempo di leggerla.
Narrazione e didascalie in **inglese**; la prosa di servizio è in italiano.

**Questo copione è PROVVISORIO.** L'editor mappa 3D non esiste ancora (nessun `src/bm/map3d.*`, nessun tasto `v` nella pagina Map): i tasti proposti da
`SYNC.md` (AGY-02/03) possono cambiare. Ogni riga della [`shot-list.md`](shot-list.md) che usa un tasto proposto è marcata `PROVVISORIO`;
i tasti della mappa 2D esistente (F3 F3, `Shift+L`, `l`, `Tab`, `Space`, `Backspace`, `z`, `f`, `u`, `+`, `-`, `m`, `o`) sono quelli veri del manuale
`manuals/content/en/editor-sdk.md` ("The map editor"). Requisiti dell'editor: [`editor-requirements.md`](editor-requirements.md).

- **Strumento**: bm SDK ▸ pagina Map (`F3`, poi `F3` di nuovo). Edizione Pi (`carts/editor`); l'RGB30 viene dopo (AGY-04).
- **Parte da**: `chain/03-animator.bm` (modelli `PICO`, `APPLE`, `CRATE` con scheletro e clip; sheet con le tessere del terreno dal video 2).
  **Produce**: `chain/04-map.bm` (layer `floor`, `height`, `walls`, `objects`). Vedi [`input.md`](input.md).
- Contratto dei layer e dei segnaposto: `SYNC.md` §2 e `docs/video/PIANO.md` §2. Livello: 9 × 96 tessere (z crescente), 2 buche, 1 gradino,
  1 muro basso, ~60 mele, 14 casse, partenza e arrivo.

## Copione

| Tempo | Sullo schermo | Narrazione (English) |
|---|---|---|
| 0:00 | PROVVISORIO. Gancio: anteprima 3D del sentiero finito, la camera gira sopra mele, casse e lanterne; titolo "Tropic Trail · Ep 4 · the 3D map" | This is the whole trail Pico will run, drawn with numbers on a grid. |
| 0:08 | Tab Dev, bm SDK; `F3` due volte: la pagina Map; la griglia vuota | bm SDK has a map editor. Press F3 twice. A map is a grid of numbers, in layers, and our trail uses four of them. |
| 0:22 | `Shift+L` crea i layer; si nominano `floor`, `height`, `walls`, `objects` (PROVVISORIO: il nome del layer) | Floor says where the ground is. Height says how high. Walls are solid blocks. Objects holds apples, crates, lanterns, and where Pico starts. |
| 0:40 | Layer `floor`: `Tab` sceglie la tessera di erba, `z` pennello 16 per 16; PROVVISORIO: `Shift+P` traccia il corridoio, `]` lo allarga; `Backspace` apre due buche | Pick a grass tile with Tab, and press Z for the big brush: one tile is one unit of world. Shift P draws the path between two cells. A cell left empty is a hole. |
| 1:10 | Layer `height`: PROVVISORIO, un gradino alto 0,5; layer `walls`: un muro basso | In the height layer a number is a quarter of a unit. Two quarters make a step. In walls, a number is a solid block that Pico cannot cross. |
| 1:35 | Layer `objects`: PROVVISORIO, `Shift+A` una fila di mele, `Shift+C` le casse, `Shift+O` le lanterne; partenza e arrivo con `Tab` e `Space` | Shift A lays a row of apples. Shift C puts a crate on a cell. Shift O adds a lantern. Start and goal are tiles from the chooser: put them at the two ends. |
| 2:05 | PROVVISORIO. La barra di stato: mele, casse, lanterne, triangoli stimati; `Ctrl+Z` annulla una fila di mele | The status line counts apples, crates and lanterns, and estimates the triangles. Control Z takes back a whole row in one step. |
| 2:20 | PROVVISORIO. `v` passa alla vista 3D; frecce girano e ingrandiscono, `,` `.` inclinano; `v` torna al 2D | Press V. The same code that builds the world in the game builds this preview, so what you see is what you play. Left and right orbit, up and down zoom, comma and period tilt. |
| 2:50 | `Ctrl+S` salva nel progetto; `Ctrl+B` genera il `.bm` | Control S saves the project. Control B builds the cartridge, with the four layers inside the file. |
| 3:10 | Cartello finale: elenco dei file che passano al video seguente | The trail is done. The next video gives Pico a voice: six soft sound effects and a jungle song, all made in bm Sound. |

Parole di narrazione: ~420 (a 130 parole/minuto, ~3:30 con le pause delle azioni).

## Capitoli (identici a youtube.md)

0:00 The trail · 0:08 The map page · 0:22 Four layers · 0:40 Floor and holes · 1:10 Height and walls · 1:35 Apples, crates, lanterns, start and goal ·
2:05 Counts and undo · 2:20 The 3D preview · 2:50 Save and build · 3:10 What is next

## Problemi trovati

Tutti verificati sul codice reale (`carts/editor/main.lua`, righe citate), non sul progetto di SYNC.md:

1. **Nessuna rinomina dei layer**: `Shift+L` crea layer chiamati `layer2`, `layer3`… (`carts/editor/main.lua` righe 1739-1740) e non c'è un tasto o
   una voce di menu per cambiare il nome. Il contratto richiede nomi fissi `floor`, `height`, `walls`, `objects`. Solo `objects` ha un trattamento
   speciale (riga 1747). Serve un'azione "rinomina layer" (non prevista in AGY-02/03).
2. **Segnaposto 1024-1029 contro il pennello 16×16**: con il pennello grande ogni "tessera" scrive 4 celle (`t`, `t+1`, `t+c`, `t+c+1`,
   righe 1717-1721) su celle pari; i colori del layer `objects` sono definiti solo per gli id **pari** 1024, 1026, 1028, 1030 (riga 1578).
   Il contratto di SYNC.md §2 usa id consecutivi (1025 = APPLE) come celle singole: 1025 sarebbe la seconda cella del pennello 1024.
   **RISOLTO (2026-10-13)**: `SYNC.md` §2 ora usa id pari (1024 start, 1026 APPLE, 1028 CRATE, 1030 GOAL, 1032 LAMP) e `map3d_objects` legge la cella in alto a sinistra del blocco 2×2.
3. **`[` `]` e `,` `.` esistono già** nella pagina Map come "tessera precedente/seguente" (`SPRITE_KEYS`, righe 2706 e 2710; azione `next`/`prev`,
   riga 1858). SYNC.md li propone per larghezza del percorso (`[` `]`) e inclinazione (`,` `.`): è accettabile solo se valgono in modo diverso in
   modalità "percorso" e in vista 3D. `Ctrl+Z` esiste già come annulla (`"^z"` riga 2707, azione `undo` riga 1859): in AGY-03 va esteso ai tratti.
4. **`v` è libero nella pagina Map** (nella tabella di `SPRITE_KEYS` è `flipv` e la mappa non lo gestisce: `act` righe 1840-1870 non ha `flipv`), ma è
   usato dalla pagina sprite per il capovolgimento: va controllato che il commutatore non interferisca. `Shift+P/A/C/O` sono liberi (solo `L` è mappato).
5. **Dimensione fissa 256×256 celle** (`carts/editor/main.lua` riga 677) e valori di `height`/`walls` che sono numeri 0-255, non tessere del foglio:
   la scelta del valore passa oggi dal selettore di tessere (`Tab`). Un livello di 9 × 96 tessere = 18 × 192 celle con il pennello 16×16.
6. **Il video dipende da AGY-01..03** (tutti `PENDING` in `SYNC.md`): finché non sono fusi in `bm-core` questo copione non è registrabile.
