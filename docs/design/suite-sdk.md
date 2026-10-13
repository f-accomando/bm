# Proposte di Sviluppo: bm SDK (L'Hub di Progetto e Level Design)

Questo documento analizza cosa manca a **bm SDK** (`carts/editor` e `carts/sdk_b16`) per supportare in modo integrato il level design avanzato, l'istanziazione di entità e la gestione di mondi complessi come quelli di *Yharnam*, *Skyvale World* o futuri giochi open-world/metroidvania.

---

## 1. Stato Attuale di bm SDK

Attualmente `bm SDK` è il centro nevralgico della produzione su console:
- **F1 (Project & Dev Kit)**: Budget del progetto (token, righe, RAM, memoria asset), report delle prestazioni dopo la prova (**F5**) e scorciatoie per aprire gli altri editor (`1`–`6`).
- **F2 (Code)**: Editor rapido del codice Lua.
- **F3 (Sprites & Flags)**: Visualizzazione dello sprite sheet con flag 0–7 su ciascuna tile (muro, piattaforma, scala, acqua, danno...).
- **F3 di nuovo (Map)**: Mappa a layer multipli (`l`/`L`), pennello 16×16 (`z`), zoom (`-`/`+`), vista globale a 256×256 (`m`, Overview), palette compatta dei tile usati (`Tab`).
- **F4 (3D Models)**: Visualizzatore di mesh e codice Lua di esempio.
- **Ctrl+N**: Creazione progetto da template (Platform, Top-down, Versus, Shooter, 3D...).

---

## 2. Cosa Manca per Level Design e Gameplay Strutturati

Nonostante le potenti funzioni grafiche della mappa, il level design attuale soffre di limitazioni strutturali quando si passa da una semplice griglia grafica a un mondo di gioco vivo:

### A. Assenza di un "Entity & Object Spawner" (Istantiazione di Nemici e Oggetti)
- La mappa di bm SDK memorizza solo **numeri interi di tessere** (0 = vuoto, 1..4095 = ID cella).
- Per posizionare un nemico, una cassa, una lampada o una porta:
  - In *Yharnam*, gli sviluppatori sono stati costretti a creare un layer `objects` con tessere fittizie (placeholder >= 1024) e scrivere centinaia di righe in Lua per scansionare la mappa e convertire il numero della tessera nell'entità corrispondente.
  - Non è possibile associare **proprietà o parametri** a un oggetto posizionato sulla mappa (es. `tipo: "lupo", hp: 80, direzione: "sinistra", percorso_pattuglia: [A, B]`).
- Tutto il posizionamento dei nemici e degli oggetti interattivi finisce per essere codificato a mano in tabelle Lua o decodificato tramite trucchi complessi.

### B. Mancanza di Trigger Rettangolari e Zone di Evento
- I flag di collisione (0–7) sono legati alla **tessera dello sheet**, non alla posizione sulla mappa: se la tessera "erba" non ha il flag solido, non può diventare un trigger d'uscita solo in un punto specifico.
- Manca uno strumento per disegnare **rettangoli di trigger** sulla mappa, ad esempio:
  - *Zone di transizione stanza / cambio scena*.
  - *Camera Lock Zone* (bloccare la telecamera durante una boss fight).
  - *Checkpoint e Salvataggio*.
  - *Trigger di dialogo o cutscene*.

### C. Gestione di Mondi a Chunk / Stanze (Metroidvania & Open World)
- La mappa supporta dimensioni 256×256 celle. In *Yharnam*, questo spazio è suddiviso logicamente in 8×8 chunk da 16×16 celle caricati a flusso (streaming).
- In bm SDK, lo sviluppatore vede solo una sterminata griglia continua: non c'è una griglia visiva dei chunk, né la possibilità di nominare "stanze", visualizzare porte di collegamento o gestire transizioni tra mappe separate.

### D. Mancanza di Prefab / Stamp Multi-Layer
- Per costruire una casa, un albero o una torre, il level designer deve posizionare manualmente decine di tessere 8×8, passando continuamente tra il layer del terreno (`ground`) e quello di sovrapposizione (`overlay`).
- Manca la possibilità di salvare e incollare **Prefab multi-layer** (es. un albero completo di chioma sopra e tronco solido sotto).

---

## 3. Complessità "Fatta a Mano" vs Soluzione Integrata

| Funzionalità | Approccio Attuale ("A mano") | Con l'Evoluzione di bm SDK |
|---|---|---|
| **Spawn dei Nemici** | Scrivere a mano tabelle di coordinate in Lua (`{x=140, y=320, type="slime"}`) o usare layer CSV con ID arbitrari | Modalità "Entità": clicchi sulla mappa, selezioni l'entità dal menu a tendina, imposti i parametri |
| **Zone di Trigger** | Rilevamento in `_update` con formule matematiche (`if player.x > 500 and player.x < 550 then ...`) | Strumento "Trigger Area": tracci un riquadro sulla mappa e imposti il nome dell'evento |
| **Blocco Telecamera** | Condizioni scritte in codice per ogni stanza | Proprietà "Camera Bounds" trascinabile sulla mappa |
| **Assemblaggio Scenari** | Posizionamento tessera per tessera su layer separati | Libreria "Stamps/Prefabs": timbri composti posizionabili con un solo clic |

---

## 4. Proposte di Sviluppo & Roadmap per bm SDK

### Proposta 1: Layer "Entities" & Object Inspector
Aggiungere una modalità dedicata nella pagina F3 della mappa:
- **Layer `Entities`**:
  - Si seleziona dal pannello dei layer accanto a Ground e Overlay.
  - Sulla mappa compaiono icone visive delle entità (giocatore, nemici, NPC, porte, scrigni).
- **Object Inspector (Pannello Proprietà)**:
  - Cliccando su un'entità, si apre un pannello laterale per configurare:
    - `ID / Nome` univoco (es. `boss_cathedral`).
    - `Classe / Script` (es. `foe_beast`).
    - `Campi personalizzati` (es. `hp = 150`, `speed = 2.5`, `drops = "key_bronze"`).
- **Salvataggio in Sezione `ENTITIES`**:
  - Il file della cartuccia salva una nuova sezione compatta binaria o testuale leggibile a runtime tramite l'API `cart_entities()`.

### Proposta 2: Strumento "Trigger & Volume Boxes"
- Permettere di tracciare rettangoli logici sopra la mappa con colori distintivi:
  - **Giallo**: Trigger generico (chiama `on_trigger(nome, entità)` quando il giocatore vi entra).
  - **Ciano**: Camera Confiner (definisce i limiti minimi e massimi di scorrimento della telecamera).
  - **Verde**: Checkpoint / Spawn Point.
- I trigger vengono esportati nella sezione mappa e gestiti automaticamente dalla libreria `bmlib`.

### Proposta 3: Stamp & Prefab Library
- Possibilità di selezionare un'area della mappa (es. 4×4 o 8×8 tessere) che coinvolge più layer (Ground + Overlay + Flag di solidità) e salvarla come **Stamp** col tasto `Ctrl+C` o *Save Prefab*.
- Una palette "Prefabs" permette di incollare con un clic alberi complessi, case, trappole con punte e piattaforme senza dover ricostruire la struttura ogni volta.

### Proposta 4: Visualizzatore Chunk & Stanze (Room Grid)
- Un'opzione di visualizzazione per mostrare una griglia a sezioni (es. riquadri da 320×180 o 16×16 celle per schermo).
- Tasto `n` su un riquadro per assegnare un nome alla stanza (es. `Cathedral_Entrance`, `Sewer_Level1`), facilitando il debug e consentendo il caricamento selettivo delle sole aree visibili in memoria.

### Proposta 5: Runtime API Unificata `lib.world()`
Fornire nella libreria standard di bm una classe unificata che automatizza il caricamento del mondo:
```lua
local lib = require "bmlib"
local world = lib.world() -- carica automaticamente mappa, flag, entità e trigger

function _init()
  world:load("level1")
  -- spawna automaticamente le entità definendo le tabelle Lua corrispondenti
  world:spawn_all(function(obj)
    add_entity(obj.class, obj.x, obj.y, obj.props)
  end)
end

function _update()
  world:update(player) -- gestisce trigger di cambio stanza e limiti telecamera
end
```

---

## 5. Spunti Aperti e Integrazioni Esterne (R11, R38, R39)

- **Import Mappe da Tiled `.tmj` (R11)**:
  - Supporto per importare livelli generati con Tiled Map Editor (`.tmj` JSON) preservando livelli grafici multipli e proprietà custom delle tile.
- **Supporto Assistente per Mappe Grandi (R38)**:
  - Voce dedicata nella knowledge base per la composizione di mappe estese con il pennello 16×16 (`z`), con esempi basati su `mkmap.py` e il tutorial platform.
- **Interfaccia SDK a 720×720 su RGB30 (R39)**:
  - Estensione del layout di `carts/sdk_b16` con scala logica a griglia 8×16: sul display 720×720 nativo permette di affiancare pannello laterale e vista mappa contemporaneamente come sull'edizione Raspberry Pi.

