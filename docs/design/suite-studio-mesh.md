# Proposte di Sviluppo: bm Studio & bm Mesh (Suite di Modellazione 3D)

Questo documento analizza cosa manca a **bm Studio** (`carts/studio`) e **bm Mesh** (`carts/mesh`) per supportare la creazione rapida di asset 3D, scenari complessi e geometrie di collisione per giochi sia tridimensionali (come *Studio Village* o *Astro Wing*) sia ibridi 2D/3D.

---

## 1. Stato Attuale di bm Studio e bm Mesh

La suite 3D di bm consente di lavorare completamente su console:
- **bm Studio**: Modellazione a blocchi e tessere (strumenti *block*, *tile*, *select*, *vertex*, *paint*, *reduce*). Utilizza la tavolozza dello sprite sheet per applicare texture e colori alle facce.
- **bm Mesh**: Editor analitico di vertici e triangoli:
  - Ispezione e modifica dei modelli della sezione `MESH`.
  - Visualizzazione e modifica delle mesh generate da codice Lua (`cart_meshes()`).
  - Esportazione da modello a codice Lua (`mesh_nome()`) e viceversa.

---

## 2. Cosa Manca per Level Design 3D e Giochi Completi

Nonostante la facilità d'uso per piccoli oggetti singoli, quando si affrontano scenari ampi o gameplay articolati emergono chiare limitazioni:

### A. Mancanza di Geometrie di Collisione Semplificate (Collision Hulls)
- Un modello 3D scenografico (es. un albero con chioma dettagliata o una statua gotica) può contenere 300–800 triangoli.
- In un gioco, testare le collisioni del giocatore contro tutti i triangoli a ogni frame con `hit3d()` o calcoli matematici è proibitivo per la CPU del Raspberry Pi o della console retro.
- Manca la possibilità di definire un **Collision Hull semplificato** (un cilindro, una capsula o una scatola AABB di ingombro) salvato insieme al modello.
- Attualmente gli sviluppatori devono calcolare a mano i raggi di collisione in Lua (`dist(p.x, p.z, obj.x, obj.z) < r`).

### B. Assenza di un Assemblatore di Scenari 3D (Scene / Kitbash Layout)
- In *Studio Village* ([`carts/village/main.lua`](file:///C:/Users/ROCKET/Documents/GitHub/bm/carts/village/main.lua)), la disposizione del villaggio è definita da una tabella Lua scritta a mano:
  ```lua
  local things = {
    { "house", -3.5, 5.5, 0, 1 },
    { "well", 3, -3, 0.3, 1 },
    { "crates", 4.4, 0.2, 0, 0.8 },
    { "tree", -4.5, -3.5, 0, 1.6 },
    ...
  }
  ```
- bm Studio e bm Mesh consentono di lavorare su **un singolo modello alla volta**, ma non offrono una modalità per **comporre una scena 3D** posizionando più modelli nello spazio con rotazioni, scale e quote Y.

### C. Modellazione di Forme Complesse vs Scripting Matematico
- In *Astro Wing* ([`carts/astrowing/main.lua`](file:///C:/Users/ROCKET/Documents/GitHub/bm/carts/astrowing/main.lua)), le astronavi e i dettagli sono stati scritti interamente come generatori matematici procedurali (`builder()`, tetraedri e parallelepipedi con calcolo dei prodotti vettoriali delle normali).
- Questo è avvenuto perché modellare forme affusolate, ali rastremate e sezioni smussate a mano in bm Studio partendo da semplici cubi è lungo e manca di strumenti di estrusione lungo curve (loft/lathe) o tagli di spigoli (chamfer).

### D. Mappatura UV e Assegnazione Texture dallo Sheet
- Le facce con texture in bm Studio prendono le coordinate dallo sprite sheet.
- Spesso regolare l'orientamento, la scala e il padding di 0.25 pixel per evitare sbavature di contorno (*texture bleeding*) richiede regolazioni minuziose vertice per vertice, mancando un selettore visivo rapido di tessere 8×8 / 16×16 applicabili con un clic.

---

## 3. Complessità "Fatta a Mano" vs Soluzione Integrata

| Operazione | Approccio Attuale ("A mano") | Con l'Evoluzione di Studio & Mesh |
|---|---|---|
| **Composizione Livello 3D** | Scrivere a mano tabelle di coordinate e angoli in Lua | Modalità "Scene Layout": posiziona istanze dei modelli con griglia e snapping |
| **Volumi di Collisione** | Calcoli geometrici approssimativi hardcoded nel codice del gioco | Strumento "Collision Capsule/Box" integrato nel modello e letto da `bounds3d()` |
| **Applicazione Texture** | Spostamento manuale delle coordinate UV per ogni vertice | Tasto "Tile Stamp": clicchi la tessera sullo sheet e clicchi la faccia del modello 3D |
| **Generazione LOD (Livelli di Dettaglio)** | Duplicazione manuale della mesh e rimozione facce una per una | Tasto "Bake LOD": genera automaticamente le versioni a 50% e 25% di poligoni per i flag di `draw3d()` |

---

## 4. Proposte di Sviluppo & Roadmap per bm Studio & bm Mesh

### Proposta 1: Strumento "Collision Volumes" (Hitbox 3D)
- In bm Studio o bm Mesh, aggiungere la definizione di volumi di ingombro semplificati:
  - **Capsule**: ideale per personaggi bipedi (altezza, raggio).
  - **Box**: ideale per case, casse e muri.
  - **Cylinder**: per tronchi d'albero, pozzi e colonne.
- Questi volumi vengono memorizzati nella sezione `MESH` e restituiti automaticamente da una nuova funzione runtime `col3d(modello, x, y, z)` o integrati in `hit3d()`.

### Proposta 2: Modalità "Scene / Diorama Assembler"
Aggiungere una pagina dedicata all'assemblaggio di ambienti:
- Permette di importare i modelli del file e posizionarli su una griglia tridimensionale con snapping a 0.5 unità.
- Controlli per rotazione sui 3 assi (`rx, ry, rz`) e scala.
- Salvataggio del layout in una sezione dedicata `SCENE` della cartuccia, caricabile con una sola riga in Lua:
  ```lua
  local villaggio = load_scene("village")
  villaggio:draw() -- disegna tutti i modelli istanziati ottimizzando il culling
  ```

### Proposta 3: UV Tile Brush Interattivo
- Un pannello laterale che mostra lo sprite sheet del progetto diviso in tessere 8×8 o 16×16.
- Cliccando su una tessera e poi su una faccia del modello 3D, la mappatura UV viene applicata istantaneamente con orientamento corretto e margine anti-bleeding automatico di 0.25 px.

### Proposta 4: Primitive Avanzate e Strumenti di Smussatura
- Aggiungere al selettore di primitive (attualmente Cubo, Sfera, Piano):
  - **Cylinder / Cone**: cilindri a 6, 8 o 12 lati per arti robotici, tubi, alberi.
  - **Prism / Wedge**: cunei e rampe per tetti ed edifici.
  - **Chamfer Tool**: smussamento rapido degli spigoli vivi, fondamentale per ottenere il look retro-mecha di *Titan Clash*.
