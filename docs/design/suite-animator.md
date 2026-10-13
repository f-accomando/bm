# Proposte di Sviluppo: bm Animator (Suite di Rigging e Animazione 3D/2D)

Questo documento analizza cosa manca a **bm Animator** (`carts/animator`) per colmare il divario con le pipeline procedurali avanzate usate per *Titan Clash* e *Yharnam*, e trasformarlo nello strumento definitivo per la produzione di kit di animazione completi sia 3D che 2D pre-renderizzati.

---

## 1. Stato Attuale di bm Animator

`bm Animator` è uno degli strumenti più avanzati della console bm:
- **F1 (Play)**: Visualizzazione dei modelli 3D con scheletro, interpolazione in tempo reale (`animate()`), test di fusione tra animazioni diverse (`b` per blend al 25%, 50%, 75%).
- **F2 (Rig)**: Creazione dello scheletro osseo (`n`), posizionamento giunti e code (`w a s d r f`), gerarchia genitori/figli (`p`), specchiatura simmetrica delle ossa `.L` e `.R` (`m`), assegnazione automatica o manuale della pelle (pesi rigidi o elastici con `k`/`K`).
- **F3 (Animate)**: Creazione di animazioni su timeline a 12 fps, rotazione e traslazione delle ossa con keyframe, onion skin delle ossa (`o`), interpolazione linear/smooth/step (`i`), specchiatura delle pose (`m`).
- **F4 (Sprites)**: Motore di bake 3D-to-2D: renderizza l'animazione direttamente nello sprite sheet a risoluzione pixel art, con opzioni per numero di direzioni (1, 2, 4, 8), inclinazione camera, luce, contorno e riduzione della tavolozza a 8, 16 o 32 colori.

---

## 2. Perché Titan Clash e Yharnam Hanno Usato Python Anziché bm Animator?

Nonostante la presenza della modalità F4 Sprites, i titoli di punta hanno sviluppato complessi generatori esterni in Python ([`carts/titan/mkrobot.py`](file:///C:/Users/ROCKET/Documents/GitHub/bm/carts/titan/mkrobot.py) e [`carts/yharnam/art/`](file:///C:/Users/ROCKET/Documents/GitHub/bm/carts/yharnam/art/)). I motivi tecnici risiedono in precise carenze dell'editor su console:

### A. Mancanza di Event Marker sulla Timeline (Frame Attivi, Suoni, Spari)
- In un'animazione di fendente (es. 6 frame), il danno non avviene per tutta la durata: il frame 1–2 è preparazione, il frame 3 è l'impatto con la hitbox attiva, il frame 4–6 è recupero.
- In bm Animator **non c'è modo di marcare un frame con un evento** (es. `event = "hit"`, `sound = "slash"`, `spawn = "bullet"`).
- Tutto questo deve essere sincronizzato manualmente via codice Lua memorizzando a mente i numeri dei fotogrammi.

### B. Assenza di "Sockets" (Punti di Ancoraggio 2D per Armi e Particelle)
- In *Yharnam*, ogni fotogramma del Cacciatore calcola la coordinata 2D esatta della punta della mannaia (per le scie di sangue e scintille) e della volata della pistola (per il lampo di fuoco e la traiettoria del proiettile).
- bm Animator disegna il modello composito nello sprite sheet ma **non memorizza le coordinate 2D delle ossa terminali**.
- Di conseguenza, se un gioco 2D vuole mostrare una fiammata dalla canna di un'arma, lo sviluppatore non sa dove posizionarla senza hardcodare coordinate a mano per ogni fotogramma.

### C. Nessun Supporto a Layer Modulari (Armature e Armi Componibili)
- In *Titan Clash*, il robot Vanguard ha armature intercambiabili (integra, danneggiata da colpi pesanti, armi montate sull'avambraccio).
- Invece di duplicare 100 frame per ogni combinazione di armatura, lo script Python ha renderizzato la base + layer trasparenti separati sovrapposti in ordine fisso.
- F4 in bm Animator renderizza l'intero modello come un blocco opaco unico, senza possibilità di esportare layer di accessori separati allineati alla stessa griglia.

### D. Mancanza di Naming Automatico per le Direzioni in `SPRITES`
- Quando F4 inforna un'animazione a 8 direzioni nello sprite sheet, genera una matrice di rettangoli ma non crea automaticamente le etichette strutturate per la sezione `SPRITES` (es. `cammina_s`, `cammina_se`, `cammina_e`, ecc.) pronte per l'uso immediato con `zspr()`.

---

## 3. Complessità "Fatta a Mano" vs Soluzione Integrata

| Esigenza | Approccio Attuale (Script Python o a mano) | Con l'Evoluzione di bm Animator |
|---|---|---|
| **Hitbox da Scheletro** | Scrivere parser matematico in Python che proietta i volumi delle ossa in coordinate 2D | In F4 Sprites, le ossa contrassegnate come "Attacco" o "Corpo" generano direttamente la sezione `BOXES` |
| **Sincronizzazione Suoni/Colpi** | Contatori `if frame == 3 then play_sfx() end` sparsi nel codice di gioco | Tasto `e` sul frame per inserire un marcatore evento (`hit`, `sfx_sword`, `step`) esportato nei metadati |
| **Punto di Sparo/Scia** | Trovare le coordinate X/Y del proiettile aprendo l'immagine con GIMP e segnando i pixel | Definire un osso come "Socket": F4 esporta una tabella con le coordinate schermo relative a ogni frame |
| **Animazioni 8 Direzioni** | Centinaia di righe di script per ruotare la camera e comporre l'atlas | F4 genera la nomenclatura standard a 8 direzioni pronta per `zspr()` con punto di ancoraggio a terra |

---

## 4. Proposte di Sviluppo & Roadmap per bm Animator

### Proposta 1: Event Marker Track in F3 (Animate)
Aggiungere sotto la timeline dei keyframe una traccia per gli **Eventi**:
- Selezionando un fotogramma, premere `e` (Event) per assegnare un tag:
  - **HIT (Rosso)**: attiva la hitbox del colpo.
  - **SFX (Giallo)**: attiva un suono specifico dal banco audio.
  - **SPAWN (Ciano)**: genera un'entità secondaria o particella.
  - **CANCEL (Verde)**: finestra in cui l'animazione può essere interrotta da un'altra azione (dodge/parry).
- Questi eventi vengono salvati nei metadati della cartuccia e attivati automaticamente da `bmlib`.

### Proposta 2: Socket Point Exporter
- Nella pagina F2 (Rig), permettere di impostare il flag `Socket` su determinate ossa (es. `hand.R`, `muzzle`, `head`).
- Durante il bake in F4 Sprites, per ogni fotogramma ed ogni direzione vengono calcolate e salvate le coordinate 2D del socket:
  ```lua
  -- Generato automaticamente da F4
  local SOCKETS = {
    hero_shoot = {
      [1] = { muzzle = { x = 24, y = 14 } },
      [2] = { muzzle = { x = 26, y = 12 } },
    }
  }
  ```
- Questo permette al codice di posizionare scintille, fiammate e armi con precisione al sub-pixel.

### Proposta 3: Batch Directional Bake con Generazione `SPRITES` & `BOXES`
Potenziare la pagina F4 Sprites:
- Selezionare l'elenco delle animazioni da sfornare (`idle`, `walk`, `attack`, `hurt`) e il set di direzioni (4 direzioni ortogonali o 8 isometriche).
- **Nome Base**: impostare il prefisso (es. `hunter`).
- **Pressione Invio (Bake Completo)**:
  1. Renderizza i frame nello sprite sheet con layout compatto (skyline packing).
  2. Scrive nella sezione `SPRITES` i nomi con suffissi direzionali: `hunter_walk_s`, `hunter_walk_se`, ecc.
  3. Proietta i volumi delle ossa dell'arma e del busto creando automaticamente le scatole `hit`, `hurt` e `body` per la sezione `BOXES`.
  4. Imposta l'Anchor Point esattamente alla base delle ossa dei piedi al contatto col suolo.

### Proposta 4: Multi-Layer Pass Rendering (Bake Modulare)
- Permettere di marcare le parti del modello in gruppi visivi (es. Gruppo A: `Corpo`, Gruppo B: `Armatura`, Gruppo C: `Arma`).
- Opzione in F4: *Bake as Layers*: sforna una sequenza per ciascun gruppo mantenendo la trasparenza e lo stesso identico rettangolo di delimitazione, consentendo a giochi 2D di implementare armature e armi personalizzabili a costo zero.
