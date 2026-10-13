# Proposte di Sviluppo: bm Pixel (Suite 2D & Sprite Editor)

Questo documento analizza cosa manca a **bm Pixel** (`carts/pixel`) per supportare la creazione completa di kit di animazione per personaggi complessi (stile *Titan Clash*, *Yharnam* o action/platformer avanzati), identificando le criticità dell'approccio manuale e delineando proposte concrete per l'interfaccia e il motore.

---

## 1. Stato Attuale di bm Pixel

Attualmente `bm Pixel` è strutturato in tre modalità principali:
- **F1 (Draw)**: Disegno dello sprite ingrandito (matita, gomma, riempimento, linee, rettangoli, ovali, selezioni, specchio, onion skin singolo frame).
- **F2 (Sheet)**: Vista d'insieme dello sprite sheet (scelta sprite, copia/incolla, ridimensionamento dello sheet da 8 fino a 4096 px).
- **F3 (Palette)**: Gestione dei colori (modifica tavolozza, estrazione da sheet, preset).

Recentemente è stata introdotta la pressione di `j` per visualizzare le hitbox della sezione `BOXES` sullo sheet.

---

## 2. Cosa Manca per Creare un Kit di Animazione Completo

Se uno sviluppatore vuole creare un personaggio d'azione completo con le sue animazioni su bm Pixel, si scontra con ostacoli insormontabili a mano:

### A. Nessuna Gestione Naming delle "Zone" (`SPRITES`)
- Per usare la funzione ad alto livello `zspr("eroe_corre", x, y)`, il file `.bm` deve contenere la sezione `SPRITES`.
- In bm Pixel **non c'è un'interfaccia per definire e nominare le zone**: lo sviluppatore deve ricordare a memoria le coordinate (es. `x=64, y=32, w=16, h=16, 6 frame, 10 fps`), uscire da bm Pixel, aprire un editor di testo o un PC, compilare `sprites.txt` e repackare la cartuccia con `mkbm.py --sprites`.

### B. Hitbox & Hurtbox Non Editabili Visivamente
- La pressione del tasto `j` mostra le scatole se già presenti nel file, ma **è impossibile disegnarle, ridimensionarle o spostarle interattivamente col puntatore**.
- In un gioco come *Titan Clash*, ogni mossa (jab, pugno pesante, calcio, parata, spazzata) richiede la calibrazione al pixel di:
  1. `body`: collisione fisica / pushbox.
  2. `hurt`: dove il personaggio può ricevere colpi.
  3. `hit`: dove il colpo causa danno, attiva solo in determinati frame.
- Scrivere queste coordinate numeriche alla cieca è frustrante e richiede centinaia di riavvii di prova.

### C. Assenza di un Anchor / Pivot Point (Origine Piedi)
- Quando un personaggio passa da `idle` (16×24) a `slash` (32×32 con la scia della spada), il punto di riferimento `x, y` standard in alto a sinistra fa "saltare" la figura visivamente su schermo.
- Manca la definizione dell'**Anchor Point** (il punto tra i piedi a terra, es. `ax=8, ay=24`). Senza di esso, ogni cambio di dimensione costringe a calcolare manualmente gli offset nel codice Lua (`x - 8, y - 16`).

### D. Mancanza di una Timeline di Animazione Multi-frame
- bm Pixel mostra una piccola anteprima animata che scorre automaticamente le celle contigue orizzontali.
- Non permette di:
  - Cambiare la sequenza dei fotogrammi (es. frame 1 -> 2 -> 3 -> 2).
  - Impostare durate differenziate per frame (es. frame di anticipazione lungo, frame di impatto breve).
  - Riordinare o duplicare un fotogramma senza dover copiare/incollare manualmente pixel sulla griglia dello sheet.

---

## 3. Complessità "Fatta a Mano" vs Soluzione Integrata

| Operazione | Attualmente ("A mano") | Con gli strumenti proposti in bm Pixel |
|---|---|---|
| **Definizione Animazione** | Calcolo manuale di coordinate `x, y, w, h`, scrittura manuale di `sprites.txt` | Selezione dell'area, tasto `Nome`, selezione frame e FPS, salvataggio automatico in `SPRITES` |
| **Piazzamento Hitbox/Hurtbox** | Misurazione pixel su foglio a quadretti, inserimento stringhe tipo `hit 3 24 10 10 6` | Strumento rettangolo dedicato (`H` per Hurtbox in blu, `K` per Hitbox in rosso) disegnato direttamente sul canvas |
| **Allineamento a Terra** | Offset calcolati in Lua (`draw_x = x - ox`) | Clic per piazzare l'ancora (croce gialla ai piedi), applicata automaticamente da `zspr()` |
| **Controllo Fluidità** | Onion skin limitato al frame adiacente | Onion skin multi-frame (frame precedente in rosso, successivo in verde) con regolazione opacità |

---

## 4. Proposte di Sviluppo & Roadmap per bm Pixel

### Proposta 1: Nuova Pagina F4 — "Animation & Zones"
Aggiungere una quarta pagina a bm Pixel dedicata alla gestione delle animazioni:
- **Pannello sinistro (Lista Zone)**:
  - Elenco di tutte le zone del progetto (`idle`, `walk`, `punch`, `jump`, `hurt`).
  - Tasti: `n` per nuova zona, `d` per duplica, `Invio` per rinomina, `Del` per elimina.
- **Timeline inferiore**:
  - Mostra i fotogrammi della zona selezionata in sequenza con miniature.
  - Possibilità di aggiungere/rimuovere fotogrammi pescando da qualsiasi punto dello sheet.
  - Cursore per regolare gli FPS (da 1 a 60 fps) e modalità di riproduzione (Loop, One-shot, Ping-pong).
- **Canvas centrale di Anteprima**:
  - Riproduce l'animazione in loop a dimensione reale e ingrandita.
  - Mostra una linea di terra e l'ancora modificabile col cursore.

### Proposta 2: Strumento Visivo "Box Editor" (Hitbox / Hurtbox)
Nella pagina F4 (o integrato in F1):
- Selezione del tipo di scatola con tasti rapidi:
  - **1: Body Box (Verde)** — ingombro solido.
  - **2: Hurtbox (Blu)** — area vulnerabile del personaggio.
  - **3: Hitbox (Rossa)** — area attiva del colpo.
- Tracciare il rettangolo col mouse o le frecce della tastiera direttamente sul fotogramma corrente.
- Opzione per copiare la scatola su tutti i fotogrammi (`*`) o lasciarla attiva solo nel frame selezionato.
- Salvataggio automatico nella sezione `BOXES` della cartuccia.

### Proposta 3: Generatore di Rampe di Colore & Palette Assistita
- Nei giochi come *Yharnam* e *Titan Clash*, ogni materiale usa rampe di 5–6 tonalità coerenti (chiaroscuro, ombre fredde e luci calde).
- Proposta: nella pagina F3 (Palette), aggiungere uno strumento **Ramp Builder**:
  - Si seleziona un colore base: l'assistente (o una funzione matematica) genera automaticamente una rampa di 5 toni (ombra bluastra, mezzotono, riflesso caldo) salvandola nella tavolozza.
  - Strumento **Shading Brush**: un pennello che non cambia colore ma aumenta/diminuisce di un gradino la tonalità sulla rampa corrente.

### Proposta 4: 8-Direction Mirroring Assistant
- Per personaggi con visuale 3/4 (stile *Yharnam* o *Hunter's Night*):
  - Spesso le direzioni verso sinistra (SW, W, NW) sono speculazioni di quelle verso destra (SE, E, NE).
  - Un comando dedicato "Genera direzioni specchiate" che clona le sequenze nello sheet applicando il flip orizzontale, preservando la mano che impugna l'arma (per evitare che il personaggio diventi improvvisamente mancino).

---

## 5. Milestone M43 — Sprite Stacking (Fette Sovrapposte e Voxel)

La tecnica dello *sprite stacking* consente di generare volumi pseudo-3D sovrapponendo fette 2D scalate e ruotate, ottenendo profondità al costo del rendering 2D classico:

1. **Integrazione Runtime**:
   - `stack(sx, sy, w, h, n, x, y, [rz, scala, passo])`: disegna `n` fette consecutive dallo sheet a partire da `(sx, sy)` (in C `gfx16.c` con campionamento inverso `sspr`, colore 0 trasparente), supportato in `draw2d()` e `bmhost`.
2. **Pile Nominali in `SPRITES`**:
   - Definizione zone con tipo `stack` e conteggio fette in `sprites.txt` / `mkbm.py --sprites`.
   - API `zstack(nome, x, y, [rz, scala])`, visualizzazione con rotazione interattiva nella scheda Lib e nei file `.bmi`.
3. **Editor dedicato in bm Pixel**:
   - Nuova pagina **Stack**: visualizzazione fette sia affiancate in griglia sia sovrapposte con onion skin verticale; anteprima 3D rotante in tempo reale; duplicazione automatica della fetta sul livello superiore per facilitare l'estrusione; creazione rapida di una pila partendo da una zona esistente.
4. **Rotazione Completa x, y, z**:
   - `stackv(...)` come volume di voxel con orientamento 3D libero lungo l'asse più vicino alla camera.
5. **Esportazione verso gli altri strumenti della suite**:
   - Conversione automatica della pila in `MESH` (facce esterne dei voxel colorate) per `bm Studio` e `bm Mesh`.
   - Generazione automatica di sprite pre-renderizzati in 8 direzioni per `bm Animator`.
- **Criterio di completamento**: decine di stack rotanti renderizzati a 60 fps sul Pi, modificabili nativamente in bm Pixel ed esportabili in modelli 3D.

