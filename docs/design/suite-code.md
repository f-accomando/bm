# Proposte di Sviluppo: bm Code (L'Editor di Codice della Console)

Questo documento analizza cosa manca a **bm Code** (`carts/code`) per supportare progetti di programmazione complessi su console, superare il vincolo del file monolitico e migliorare l'integrazione con gli asset grafici e sonori.

---

## 1. Stato Attuale di bm Code

Attualmente `bm Code` è un editor di codice agile e ben ottimizzato per schermi a bassa risoluzione:
- **Visualizzazione**: Tab per cartucce multiple, vista affiancata a due pagine (Split), font 6×12 (106 colonne, 28 righe di codice), 8×14 e 8×16.
- **Scrittura assistita**: Completamento automatico predittivo (`predict`), scrittura assistita da gamepad (`padtype`), assistente AI su F6 e righe `#entry: ... #`.
- **Debug & Audio**: Breakpoint su riga con F8, riproduzione delle righe di codice come note musicali con Ctrl+Invio (`riff`).

---

## 2. Cosa Manca per Progetti e Giochi Complessi

Nonostante l'eccellente usabilità su console, per progetti di grandi dimensioni emergono forti limitazioni:

### A. Assenza di Struttura Multi-File all'Interno dello Stesso Progetto
- Attualmente le tab di bm Code aprono **cartucce `.bm` o `.bme` distinte**, non file sorgente multipli dello stesso progetto.
- Di conseguenza:
  - In *Titan Clash*, gli sviluppatori hanno dovuto creare una cartella `carts/titan/src/` con 11 file Lua (`00_core.lua`, `20_fighter.lua`, `30_fx.lua`, `70_screens.lua`...) e creare uno script Python `build.py` da eseguire su PC per concatenarli in un unico `main.lua`.
  - In *Yharnam*, dove lo script di build non è stato impostato allo stesso modo, l'intero gioco risiede in un unico gigantesco file `main.lua` da **5.573 righe**.
- Modificare, navigare e manutenere un file di 5.000 righe su una console retro con risoluzione 640×360 o 360×360 è un'esperienza sfiancante.

### B. Mancanza di Asset Picker e Completamento Visivo
- Quando un programmatore scrive in bm Code:
  ```lua
  zspr("eroe_colpo_pesante", x, y)
  sfx("slash_crit")
  local m = model("gargoyle")
  ```
  deve ricordare a memoria la grafia esatta delle stringhe definite in altri editor (bm Pixel, bm Sound, bm Studio).
- Se sbaglia una lettera (`"eroe_colpo_pesabte"`), il gioco fallisce silenziosamente o va in crash a runtime.
- Non esiste alcun completamento automatico o anteprima contestuale degli asset del progetto.

### C. Assenza di un Ispettore delle Variabili a Runtime (Live Watcher)
- Per il debug, gli sviluppatori sono costretti a inserire decine di `print("stato=" .. p.state, 10, 10)` temporanei nel codice, che sporcano lo schermo e vanno poi cancellati a mano.
- Sebbene esista F8 per i breakpoint, manca un pannello di **Watch** per monitorare le variabili dell'entità selezionata (es. `p1.state`, `p1.vx`, `p1.vy`, `p1.swing`, `p1.invuln`) durante l'esecuzione a 60 fps.

### D. Scrittura Ripetitiva delle Macchine a Stati da Zero
- Ogni nuovo gioco richiede di riscrivere da zero le stesse 100–200 righe per gestire:
  - Buffer degli input (memorizzare la pressione di un tasto per 4–8 frame in modo che l'attacco esca appena termina l'animazione precedente).
  - Macchina a stati finiti (FSM) con controllo dei frame e transizioni.
  - Timer di invulnerabilità post-colpo (i-frames) e hit-stop (blocco temporaneo del fotogramma all'impatto).

---

## 3. Complessità "Fatta a Mano" vs Soluzione Integrata

| Operazione | Approccio Attuale ("A mano") | Con l'Evoluzione di bm Code |
|---|---|---|
| **Organizzazione Progetto** | File unico da 5000+ righe, oppure build script Python esterno su PC | Tab per moduli (`player.lua`, `boss.lua`, `hud.lua`) nello stesso progetto `.bme`, caricati nativamente da `require` |
| **Richiamo Asset** | Ricordare a memoria i nomi di sprite, mesh e suoni o cercarli su foglietti | Scrivi `zspr("` e compare il menu contestuale con le miniature degli sprite e i nomi delle zone |
| **Debug di Movimento e Stati** | `print()` sparsi nel codice di disegno | Overlay F7: tabella variabili d'interesse visibile in sovrimpressione trasparente durante la prova con F5 |
| **Buffer Input & Combattimento** | Decine di righe di logica custom con contatori di frame | Snippet assistito: *Insert Combat State Machine* dal menu dell'assistente |

---

## 4. Proposte di Sviluppo & Roadmap per bm Code

### Proposta 1: Supporto Multi-Modulo Nativo nei Progetti `.bme`
- Permettere a un progetto `.bme` di contenere più sezioni di codice Lua (es. `main.lua`, `player.lua`, `enemies.lua`, `ui.lua`).
- In bm Code, una barra secondaria o una scorciatoia (es. `Ctrl+Tab` o `Ctrl+P`) permette di saltare istantaneamente da un modulo all'altro.
- Il runtime Lua integra nativamente `require "player"` cercando prima tra i moduli interni della cartuccia. In questo modo il progetto rimane pulito, modulare e sviluppabile al 100% su console senza script Python esterni.

### Proposta 2: Asset IntelliSense & Popup Visivo
- Quando il cursore è dentro le funzioni di richiamo asset:
  - **`zspr("`**: si apre un pop-up con la lista di tutte le zone nominali salvate in `SPRITES`, con anteprima grafica in miniatura (8×8, 16×16) e numero di frame.
  - **`sfx("`** o **`note(`**: pop-up con i nomi degli effetti sonori e possibilità di ascoltare l'anteprima premendo Spazio.
  - **`model("`**: elenco dei modelli 3D disponibili nella sezione `MESH` con anteprima wireframe.
  - **`flag(`**: visualizzazione dei flag 0–7 e del loro significato configurato nel progetto (es. `0=Muro, 1=Piattaforma, 4=Spuntoni`).

### Proposta 3: Live Variable Watcher (Pannello F7 durante F5)
- In bm Code è possibile contrassegnare una riga o una variabile con un marcatore di "Watch" (es. `#watch player.state, player.vx, player.vy`).
- Quando si avvia la prova del gioco con **F5**, premendo **F7** compare un riquadro semitrasparente in alto a destra con i valori aggiornati a 60 fps, senza dover toccare il codice di `_draw()`.

### Proposta 4: Snippet Generatori per Archetipi di Gioco
Integrare nel menu dell'assistente (F6) generatori di pattern architetturali collaudati:
1. **FSM Controller**: Genera la struttura standard per stati `IDLE`, `WALK`, `ATTACK`, `HURT`, `FALL`.
2. **Input Buffer Queue**: Genera la logica standard a 6 frame per accettare input di attacco in anticipo.
3. **Screen Shake & Hit-stop**: Funzioni standard pronte all'uso per dare impatto visivo ai colpi.

### Proposta 5: Template di Gioco per "New Game" (R16)
- Attualmente "New Game" in bm Code genera un unico scheletro vuoto elementare (`TEMPLATE`).
- Integrare la selezione guidata dei template già disponibili nell'SDK (Platform 2D, Top-Down 2D, Versus 2D, Shooter 2D, 3D Scene), generando file pronti e strutturati a moduli.

