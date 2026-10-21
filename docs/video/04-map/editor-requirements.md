# Editor mappa 3D — requisiti (per il video 4 di Tropic Trail)

Specifica dei requisiti dell'editor che il video 4 deve mostrare. Non è un work order (quelli sono AGY-01..03 in `SYNC.md` §3, di Antigravity):
serve a dire cosa deve fare l'editor perché il video si possa registrare, con criteri di accettazione verificabili. Contratto condiviso:
`SYNC.md` §2 e `docs/video/PIANO.md` §2. Decisione di progetto: `docs/design/suite-sdk.md`, "Proposta 6" (riga 108).

Stato al momento della stesura: **nessuna riga di codice dell'editor 3D esiste** (nessun `src/bm/map3d.*`; la ricerca di `map3d` trova solo documenti).

## 1. Cosa esiste già

Verificato con Grep/lettura nei file citati.

| Cosa | Dove | Note |
|---|---|---|
| Pagina Map nell'SDK (`F3` due volte), fino a 8 layer, `l` layer successivo, `Shift+L` nuovo layer (`layer2`…), `o` solo questo layer | `manuals/content/en/editor-sdk.md` righe 183-197; `carts/editor/main.lua` righe 1732-1763 (`next_layer`), 2722 (tasti) | i layer nuovi si chiamano `layerN`; **nessuna rinomina** |
| Layer `objects` con segnaposto ≥ 1024: pennello 16×16 e primo segnaposto scelti in automatico | `manuals/content/en/editor-sdk.md` riga 203; `carts/editor/main.lua` righe 1747-1761; `docs/progress/tools.md` righe 14-19 | i segnaposto non sono disegnati dal gioco, li legge il codice |
| Colori dei segnaposto nella panoramica: solo id pari 1024, 1026, 1028, 1030 | `carts/editor/main.lua` riga 1578 e 1617-1618 | non coincide con la tabella 1024-1029 di `SYNC.md` |
| Pennello 16×16 = 2 × 2 celle (`t`, `t+1`, `t+c`, `t+c+1`), posato su celle pari | `carts/editor/main.lua` righe 1717-1729 | un "1.0 unità" del contratto è questo blocco |
| Posa/cancella/preleva/riempi/annulla: `Space`, `Backspace`, `x`, `f`, `u` e `Ctrl+Z` (`"^z"`) | `carts/editor/main.lua` righe 2705-2710 (`SPRITE_KEYS`), 1858-1862 (`next`/`prev`/`undo`) | `[` `]` `,` `.` sono già "tessera prima/dopo" |
| Zoom `+`/`-`, panoramica `m`, flag `c`, selettore `Tab` | `manuals/content/en/editor-sdk.md` righe 191-197; `docs/progress/tools.md` righe 16-19 | |
| Mappa 256 × 256 celle, fino a 8 layer, sezioni MAP + LAYERS (layer 2..8) + FLAGS | `carts/editor/main.lua` riga 677; `docs/BM-FORMAT.md` riga 61; `src/bm/bm.h` (sezioni 12 LAYERS, 13 FLAGS) | MAP conta come layer 1 (`nlayers` 2..8) |
| Salvataggio nel progetto `.bme`; `Ctrl+S`, `Ctrl+B` genera il `.bm`; primo salvataggio di un gioco crea la copia modificabile | `manuals/content/en/editor-sdk.md` righe 294-306; `docs/progress/tools.md` riga 12 | |
| Pagina 3D dell'SDK (`F4`): anteprima dei modelli e animazioni del progetto; `i` scrive il codice di caricamento | `manuals/content/en/editor-sdk.md` riga 77 e tabella "Keys" | è già un contesto di rendering 3D nell'editor |
| API 3D per l'anteprima: `light3d`, `sky3d`, `lamp3d` (1-4), `fog3d`, `shadow3d`, `visible3d`, `draw3d`, `model` | `docs/api/light3d.md` righe 71-96 | |
| Modelli `PICO`, `APPLE`, `CRATE` nella MESH (video 1-3) | `docs/video/PIANO.md` §2 e §4 | |
| Edizione RGB30 dell'SDK come cartuccia separata | `carts/sdk_b16`; `SYNC.md` AGY-04 | non si modifica prima della registrazione del video 4 |

## 2. Cosa manca

1. **Costruttore di geometria** layer → mesh + collisioni (`src/bm/map3d.c`, API `map3d_build/draw/height/blocked/objects/size`): AGY-01, `PENDING`.
2. **Vista 3D nella pagina Map** (tasto `v`, camera orbitale, cursore di cella): AGY-02, `PENDING`.
3. **Strumenti di livello lineare** (`Shift+P` percorso, `Shift+A` mele, `Shift+C` casse, `Shift+O` lanterne, conteggi, annulla di tratto): AGY-03, `PENDING`.
4. **Rinomina dei layer** (oggi `layer2`, `layer3`… senza modo di cambiare nome): necessaria per `floor`, `height`, `walls`, `objects`. Non coperta da AGY-01..03.
5. **Valori numerici per `height` e `walls`** (id 0-255 che non sono tessere del foglio): oggi si sceglie dal selettore di tessere. Serve un modo di impostare il valore
   e una lettura del solo angolo in alto a sinistra del pennello 16×16 (altrimenti il pennello scrive `n`, `n+1`, `n+c`, `n+c+1`).
6. **Compatibilità dei segnaposto**: `SYNC.md` §2 usa 1024-1029 consecutivi, ma il pennello 16×16 occupa 4 celle e il colore esiste solo per gli id pari (riga 1578).
   Decidere (Antigravity/utente): oggetti con pennello 8×8, oppure id a passo 2, oppure lettura della sola cella in alto a sinistra.
7. **Mappa di partenza** 9 × 96 tessere = 18 × 192 celle: nessuna funzione "nuova mappa a dimensione scelta" (oggi 256 × 256 fissa, riga 677).
8. **Edizione RGB30** (AGY-04): fuori dal video 4, dopo la registrazione.

## 3. Requisiti e criteri di accettazione

Ogni criterio ha un comando o un'osservazione verificabile. Sigla: `bmhost --tool` = esecuzione riproducibile con orologio virtuale
(`docs/video/04-map/input.md`).

| ID | Requisito | Criterio di accettazione (verificabile) |
|---|---|---|
| R1 | **Vista a griglia + percorso lineare**: pavimento nel layer `floor` (id ≠ 0 = pavimento, 0 = buca); il tasto `Shift+P` traccia un corridoio di larghezza 1-5 tra due celle | `bmhost --tool` con un copione di tasti: percorso 4 × 60 salvato; `scripts/bmres.py` (`layers_get`) trova nel layer `floor` il numero atteso di celle ≠ 0 e nessuna cella fuori dal corridoio |
| R2 | **Piazzamento di mele, casse, lanterne** (segnaposto 1026, 1028, 1032) con `Shift+A` (fila a passo di una cella), `Shift+C`, `Shift+O` | stesso test: 20 celle `1026`, 6 celle `1028`, 1 cella `1032` nel layer `objects` (come AGY-03) |
| R3 | **Partenza e arrivo** (1024 `PLAYER_START`, 1030 `GOAL`): uno solo per tipo | `map3d_objects(w, "PLAYER_START")` restituisce 1 elemento e `GOAL` 1 elemento; la barra di stato avvisa se ne manca uno `[DA VERIFICARE: l'avviso non è nel contratto]` |
| R4 | **Luci**: le lanterne (1032) sono luci puntiformi raggio 4, colore `0xFFC888`; l'anteprima 3D le mostra con `lamp3d` (max 4 attive: le più vicine alla camera) | a camera ferma su una lanterna, il pixel vicino alla lanterna è più chiaro che a 8 unità di distanza (confronto di due `--shots` di `bmhost`) |
| R5 | **Altezze**: layer `height` (id n = quota `n × 0,25`) e layer `walls` (id n = blocco alto `n × 0,25` sopra il pavimento); un gradino da 0,5 e un muro basso | `map3d_height(w, x, z)` = 0,5 sul gradino e `nil` sulle buche; `map3d_blocked(w, x, y, z, r)` = `true` sul muro (test C/Lua di AGY-01: `make test-map3d` → riga `map3d OK`) |
| R6 | **Undo**: `Ctrl+Z` annulla un tratto intero (percorso, fila di mele) in un colpo | dopo una fila di 20 mele e `Ctrl+Z`, il layer `objects` torna alle 0 celle `1026` di prima; un secondo `Ctrl+Z` annulla l'azione precedente |
| R7 | **Salvataggio nel `.bm`**: i layer `floor`, `height`, `walls`, `objects` (nomi minuscoli esatti) stanno in LAYERS/MAP; MESH, ANIM e il resto non cambiano | `Ctrl+S` poi `Ctrl+B`; `bmres.check(f)` senza errori; `bmres.layers_get(f)` elenca i quattro nomi; le sezioni MESH e ANIM sono identiche byte per byte a `chain/03-animator.bm` |
| R8 | **Anteprima 3D con lo stesso costruttore del gioco**: `v` commuta 2D/3D e chiama `map3d_build`/`map3d_draw`, la stessa funzione che userà il gioco | lo stesso layer costruito dal codice del gioco (`map3d_build`) e dall'editor dà lo stesso numero di triangoli; il test di AGY-02 verifica che l'immagine 3D differisca dalla 2D e che non ci siano errori Lua |
| R9 | **Prestazioni**: 60 fps a 480×270 con la mappa di 9 × 96 tessere in anteprima; il mondo si ricostruisce solo quando la mappa cambia (flag sporco) | `bmhost` con la mappa vera per 10 s: nessun frame oltre 16,7 ms `[DA VERIFICARE: come si misura il frame time in bmhost; PIANO.md §6 punto 5]`; la barra di stato mostra i triangoli ≤ 6000 visibili |
| R10 | **Edizione**: prima Pi (`carts/editor`), poi RGB30 (`carts/sdk_b16`, AGY-04) | `make test-map3d` per Pi; per RGB30 `python scripts/consistency.py check` ok e `make TARGET=rgb30` compila |
| R11 | **Compatibilità 2D**: le mappe 2D esistenti non cambiano (tile e flag come negli invarianti: 0 solido, 1 piattaforma, 2 scala, 3 acqua, 4 fa male) | aprire `carts/` con mappa 2D, salvare senza modifiche: il file resta identico byte per byte (come "A file opened and saved with no changes stays identical" dei manuali) |
| R12 | **Tasti documentati**: `manuals/content/en/editor-sdk.md` ("3D maps") e `docs/progress/tools.md` aggiornati; la riga del video 4 della shot-list si sblocca | `python scripts/consistency.py check` senza nuovi errori; `shot-list.md` senza righe `PROVVISORIO` |

## 4. Conflitti di tasti da risolvere (verificati nel codice)

| Tasto proposto | Uso esistente nella pagina Map | Riferimento |
|---|---|---|
| `[` `]` (larghezza del percorso) | tessera precedente / seguente | `carts/editor/main.lua` riga 2706, 1858 |
| `,` `.` (inclinazione della camera 3D) | tessera precedente / seguente | riga 2710, 1858 |
| `Ctrl+Z` | già annulla (`"^z"`); va esteso a un tratto intero | riga 2707, 1859 |
| `v` | libero nella mappa (nella tabella condivisa è `flipv`, ignorato da `P.map.act`) | riga 2707 e 1840-1870 |
| `Shift+P`, `Shift+A`, `Shift+C`, `Shift+O` | liberi (solo `L` è mappato) | riga 2722 |

## 5. Dopo la registrazione

Quando AGY-01..03 sono `DONE` in `bm-core`: ripassare `shot-list.md` (togliere `PROVVISORIO`), riprovare ogni riga con `bmhost --tool`,
scrivere `video/3d/04-map/record.py` e `verify.py` come in `input.md`, aggiornare `docs/video/STATUS.md` e il PIANO (§6 punto 1).
