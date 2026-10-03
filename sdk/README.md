# bm Studio e bm Animator — le risorse delle cartucce `.bm`

Due applicazioni per il PC fanno le risorse delle cartucce `.bm`, con le stesse regole:
lavorano direttamente sul `.bm` (lo aprono, lo cambiano, lo salvano al suo posto, anche
sulla SD), non toccano il codice del gioco, la mappa e le sezioni che non conoscono, e
scambiano formati standard con gli altri programmi.

- **bm Studio** (`sdk/studio/`): **modelli 3D a tessere** in stile
  [Crocotile 3D](https://crocotile3d.com/) e la **pixel art dello sprite sheet**
  (anche come editor di PNG).
- **bm Animator** (`sdk/animator/`): **scheletro** dei modelli (rigging), **animazione a
  keyframe**, **animazione scheletrica** che la console riproduce, e le animazioni
  **pre-renderizzate in sprite** per i giochi in 2D. [Vedi sotto](#bm-animator).

Sono pagine web senza dipendenze (HTML + JavaScript, niente da installare né compilare).
Un pulsante in alto passa dall'una all'altra portando il progetto aperto.

Sulla console ci sono bm Studio e bm Animator con gli stessi nomi (scheda **Dev**), per
tastiera e gamepad, sugli stessi file. [Vedi sotto](#sulla-console-bm-studio-e-bm-animator).

## Aprirlo

- **Doppio clic su `sdk/studio/index.html`** (o `sdk/animator/index.html`) con Chrome o
  Edge (da Windows con WSL: `\\wsl$\<distro>\...\bm\sdk\studio\index.html`, oppure copia la
  cartella `sdk` dove vuoi). Firefox e Safari funzionano, ma senza il salvataggio "al suo
  posto": il `.bm` si scarica.
- Oppure `make studio` e poi <http://localhost:8765/studio/> e
  <http://localhost:8765/animator/> (da WSL si aprono anche dal browser di Windows).

## Come si usa (l'essenziale)

Un progetto è una cartuccia: **sprite sheet**, **modelli 3D**, copertina, titolo e codice.
Un progetto nuovo parte con uno sheet di tessere 16×16 già disegnate (erba, pietra,
mattoni, legno, tegole, finestre, una porta, piante...) e con un codice Lua che mostra i
modelli sulla console, così si prova subito sul Pi.

**Pagina 3D** (a sinistra la vista, a destra lo sheet):

| Strumento | Tasto | Cosa fa |
|---|---|---|
| **Tile** | `1` | clic o trascina: posa le tessere scelte nello sheet sulla griglia; su una faccia le cambia la texture. `R` gira la tessera, `F` la specchia, Alt+clic prende la tessera di una faccia |
| **Block** | `2` | cubi di una unità con la tessera su ogni lato; su una faccia, un cubo accanto. Tra due cubi vicini la parete sparisce da sola |
| **Select** | `3` | clic, Shift+clic, rettangolo; trascina per spostare; frecce e PagSu/PagGiù di un quadretto (Shift: di un pixel); `R` gira, `T` ribalta, `M` specchia, `N` mostra l'altro lato, Invio mette la tessera scelta, `U` gira la texture, `+`/`−` ingrandisce e rimpicciolisce, Ctrl+D duplica, Canc cancella; "⇪" sposta le facce in un modello nuovo |
| **Vertex** | `4` | sposta gli angoli (tetti, rampe, forme libere); `M` unisce più angoli in uno |
| **Paint** | `5` | dipinge i pixel dello sheet direttamente sul modello; clic destro prende il colore |

- **Camera**: trascina col tasto destro per girare, Shift+destro (o il tasto centrale)
  per spostarla, rotellina per lo zoom, `Z` inquadra il modello, `Home` la rimette a posto.
- **Griglia**: le tessere vanno sul piano della griglia (in automatico il pavimento o la
  parete più di fronte alla camera; `P` la fissa). `[` `]`, PagSu/PagGiù o Ctrl+rotellina
  la spostano di un quadretto. Una faccia sotto il mouse ha la precedenza: si costruisce
  "attaccandosi" a quello che c'è.
- **Clic destro** (senza trascinare) cancella la faccia sotto il mouse.
- Il **pannello Tiles** sceglie le tessere (trascina per sceglierne più di una: una porta
  alta due tessere si posa in un colpo solo) e ha matita, gomma, riempimento e contagocce
  per ritoccare lo sheet senza cambiare pagina; "plain colour" posa facce di un colore pieno.
- **Vista** (`L` luce, `K` facce posteriori, `W` fil di ferro): con la luce e le facce
  posteriori nascoste si vede come sul Pi (la stessa convenzione della console: una
  faccia si vede da un lato solo). "Colours of the console" mostra i colori RGB565.
- La barra in basso conta facce, triangoli e angoli del modello e avvisa quando è
  pesante per i 60 fps (circa 1200 triangoli per scena) o supera i 4096 angoli.

**Pagina Pixel** (`Tab`): lo sprite sheet a tutto schermo, con matita (`B`), gomma (`E`),
riempimento (`G`), contagocce (`I`, o clic destro), linea (`L`), rettangolo (`U`),
selezione (`M`: si trascina per spostarla, con Alt una copia; Ctrl+C / Ctrl+X / Ctrl+V;
`H`, `V`, `R` specchiano e girano), zoom con la rotellina. Le modifiche si vedono subito
sui modelli. "Size…" cambia le dimensioni dello sheet (fino a 4096×4096).
Si può usare anche da solo, come editor di PNG: aprendo un `.png` (File → Open), Ctrl+S
risalva quel `.png` (al suo posto con Chrome/Edge); *Save as…* ne fa invece un `.bm`.

**Pannello Cartridge**: titolo, autore, risoluzione, **copertina** (dalla vista 3D così
com'è, o da un'immagine), il margine delle texture e il codice `main.lua`.

**Pannello Models**: più modelli nello stesso `.bm` (una casa, un albero, un nemico...),
con nome (fino a 15 caratteri), duplicati, ordine.

Annulla e rifai: Ctrl+Z, Ctrl+Y. Salva: Ctrl+S. Tutti i tasti: F1.

## I file

| File | Cosa |
|---|---|
| **`.bm`** | il formato di lavoro: i modelli stanno nella sezione **MESH** della cartuccia (`src/bm/bm.h`), con lo sprite sheet come texture. Si apre, si modifica e si salva al suo posto |
| **`.glb`** (glTF) | per scambiare modelli con altri programmi (Blender, il visualizzatore 3D di Windows). *Export* scrive i modelli con lo sheet come texture; *Add models from a .glb* importa anche modelli altrui: le loro texture finiscono nello sheet (rimpicciolite, se si vuole), i colori delle facce restano colori |
| **`.png`** | lo sheet (o una parte) in entrata e in uscita; un'immagine può diventare lo sheet intero, un pezzo dello sheet o la copertina |
| **`.lua`** | *Export this model as Lua code*: il modello come tabelle per `mesh()`, per chi costruisce le mesh nel codice |

Si può anche trascinare un file sulla finestra.

## I modelli in un gioco

```lua
local casa
function _init()
  casa = model("house")            -- una mesh, come quelle di mesh()
end
function _draw()
  cls(0x1c2030)
  zclear()
  camera3d(0, 4, -8, 0, -0.4)
  light3d(-0.4, 0.8, -0.5, 0.4)
  draw3d(casa, 0, 0, 0, 0, time() * 0.5)
end
```

- `model(nome)` (o `model(n)`, dall'1) dà la mesh, `nil` se non c'è; `models()` la
  lista dei nomi; `bounds3d(m)` il box `x0, y0, z0, x1, y1, z1` (per centrare o per le
  collisioni). Riferimento: [docs/API.md](../docs/API.md).
- L'origine della griglia (le tre linee colorate) va nel punto `x, y, z` di `draw3d`; un
  quadretto è una unità.
- Le facce con texture usano lo sprite sheet della cartuccia: se il gioco cambia lo sheet
  con `sset`, cambiano anche i modelli.

**Sul Pi**: si copia il `.bm` in `carts/` sulla SD (o lo si manda con `tools/bm_net.py`).
Un progetto nuovo ha come codice un visualizzatore: sinistra/destra cambia modello,
su/giù zoom, A la luce, B la rotazione.

**Per un gioco del repository** (costruito da `make`): si mette `models.glb` (File →
*Export all models*) in `carts/<gioco>/`; `make` lo mette nella cartuccia con
`mkbm.py --models`. Se il gioco non ha un suo `sheet.png`, lo sheet è quello del `.glb`.
Esempio completo: `carts/village` (Studio Village, i cui modelli sono fatti con gli
strumenti di bm Studio da `mkmodels.js`; `models.glb` si apre nello Studio).

## Limiti

- Un modello ha al massimo **4096 angoli** e 16384 triangoli (oltre, lo Studio non salva);
  la console disegna circa **1200 triangoli a 60 fps** per scena.
- Pixel pieni o trasparenti (alfa < 128 = trasparente), come per gli sprite.
- Le texture che si ripetono (uv fuori da 0..1) non ci sono: bm allunga il bordo.
- Il **margine delle texture** (0,25 px, pannello Cartridge) sposta gli angoli della
  texture di ogni faccia un po' verso l'interno, perché sul Pi la tessera accanto nello
  sheet non si veda lungo i bordi.

## bm Animator

Apre un `.bm` con dei modelli (fatti con bm Studio) e ne fa lo scheletro e le animazioni;
parte con un esempio già pronto, un paesano con tre animazioni (idle, walk, wave). Tre
pagine (`1` `2` `3`, o Tab):

**Rig** — lo scheletro, sul modello a riposo:
- **＋ Bone** (`N`) aggiunge un osso, figlio di quello scelto (il primo va dal fondo al
  centro del modello). Un osso ha una **testa**, il punto intorno a cui gira, e una
  **coda**. Si trascinano le giunture (testa e coda) col mouse, sulla griglia di 1/32;
  le giunture nello stesso punto si muovono insieme (una catena resta unita), con Shift
  solo quella dell'osso scelto. Nome e padre si cambiano a destra.
- **⇋ Mirror** copia l'osso scelto e i suoi figli dall'altra parte (sinistra ↔ destra,
  con i nomi `.L` / `.R`).
- **Skin** (pelle): ogni faccia segue un osso. Si scelgono delle facce (clic, Shift+clic,
  rettangolo) e **Assign** (`A`) le dà all'osso scelto; **Auto** dà ogni faccia all'osso
  più vicino (parti rigide, come i giochi PS1), **Auto smooth** ogni angolo (il modello si
  stira alle giunture). I colori mostrano chi segue chi (`C`).

**Animate** — le pose sulla linea del tempo:
- clic su un osso per sceglierlo; i tre **anelli** (x rosso, y verde, z blu) lo girano
  (Shift: a scatti di 15°); trascinare la **coda** lo punta dove si vuole, trascinare la
  **testa** lo sposta. A destra gli stessi valori in numeri.
- **auto key**: ogni cambio della posa diventa un **keyframe** al tempo corrente (un
  keyframe è la posa di tutto lo scheletro). `K` ne mette uno, Canc lo toglie; sulla linea
  del tempo un clic sposta il tempo, i rombi (i keyframe) si trascinano, clic destro li
  cancella.
- Spazio riproduce, ←/→ un fotogramma, Shift+←/→ il keyframe prima/dopo; lunghezza,
  fotogrammi al secondo, ciclo (loop) e il passaggio tra i keyframe: **linear**, **smooth**
  (accelera e rallenta) o **step** (a scatti, niente in mezzo).
- `M` specchia la posa (sinistra ↔ destra), Ctrl+C / Ctrl+V copia e incolla la posa, `R`
  rimette l'osso a riposo; `O` mostra in trasparenza i keyframe prima e dopo (onion skin).
- Più animazioni per modello (pannello a destra: New, Duplicate, Rename, Delete).

**Sprites** — l'animazione **pre-renderizzata in sprite**, per i giochi in 2D:
- si sceglie animazione, numero di fotogrammi, dimensione (per esempio 48×48), **direzioni**
  (1, 2, 4 o 8: il modello girato intorno a sé), quanto si guarda dall'alto, camera piatta
  (ortogonale) o in prospettiva;
- l'aspetto: luce (anche a **bande**, cel shading), **contorno**, **colori** ridotti (una
  tavolozza per tutti i fotogrammi), bordi lisci (disegno a 2× e riduzione);
- l'anteprima gira e una griglia mostra tutti i fotogrammi (a destra i fotogrammi, in
  basso le direzioni: davanti, poi girando in senso orario);
- **Put in the sheet** mette la griglia nello sprite sheet della cartuccia (dove c'è posto,
  o lo allarga) e dà il codice Lua per disegnarla con `sspr()`; **Export .png** la salva a
  parte. Il disegno è un piccolo rasterizzatore in JavaScript: gli stessi pixel ogni volta.

Nel gioco:

```lua
local man, t = nil, 0
function _init() man = model("villager") end          -- con il suo scheletro
function _update() t = t + 1 / 60 end
function _draw()
  cls(0) zclear()
  camera3d(0, 2, -6, 0, -0.25)
  animate(man, "walk", t)                              -- la posa di "walk" al tempo t
  draw3d(man, 0, 0, 0)
end
```

- `animate(m, "walk", t, "wave", t, k)` mescola due animazioni (`k` da 0 a 1: per passare
  dall'una all'altra); `animate(m)` è la posa di riposo; restituisce la durata.
- `clips(m)`: le animazioni, `{ {name=, length=, loop=}, ... }`.
- `bone3d(m, "arm.L")`: dove si trovano la testa e la coda di un osso nella posa
  (coordinate del modello; prima i tre numeri della testa, poi quelli della coda): per
  attaccarci una spada, una lanterna...
- Esempio: *Studio Village* (`carts/village`): il paesano cammina sul sentiero, saluta
  alle estremità (due animazioni mescolate), di notte porta una luce (`bone3d`), e nell'angolo
  c'è la sua versione a sprite pre-renderizzata.

File: lo scheletro e le animazioni stanno nel `.bm` (sezione **ANIM**, `src/bm/bm.h`),
accanto ai modelli; bm Studio li conserva quando modifica il modello (le facce nuove seguono
il primo osso). **Export .glb** scrive il modello con giunture, pelle e animazioni (per
Blender o il visualizzatore 3D di Windows; `smooth` diventa una curva campionata a 30 al
secondo). Per un gioco del repository: `carts/<gioco>/models.bm` (un `.bm` con i modelli e
gli scheletri) entra nella cartuccia da solo, con il suo sheet se il gioco non ha
`sheet.png`.

Limiti: ogni angolo segue **un** osso (niente pesi misti); 64 ossa per modello, 255
animazioni, 1024 keyframe per animazione; un keyframe è sempre la posa intera. I `.glb`
con scheletro di altri programmi non si importano (i modelli fermi sì, in bm Studio).

## Sulla console: bm Studio e bm Animator

Sulla console ci sono gli stessi due programmi, con gli stessi nomi: **bm Studio**
(`carts/studio/main.lua`, i modelli) e **bm Animator** (`carts/animator/main.lua`,
scheletri, animazioni e sprite). Sono cartucce incorporate nel kernel, nella scheda **Dev**;
da un gioco si aprono con **X** sulla copertina, **Open in bm Studio** o **Open in bm
Animator** (dal monitor i tasti `3` e `6`), e dal menu dell'uno si passa all'altro sullo
stesso file (*Open in bm Animator*, *Open in bm Studio*). Leggono e scrivono le stesse
sezioni MESH e ANIM dei programmi per il PC: un `.bm` fatto sul PC si apre sulla console e
viceversa, e un gioco senza modelli può riceverne. Si usano con la tastiera o con il
gamepad (Bluetooth o USB); il mouse verrà in un secondo momento.

Le pagine si scelgono con i tasti F (o Y + sinistra/destra sul gamepad), il menu con Esc
(Y + B); tenendo premuto **F12**, o con **?**, compaiono i tasti della pagina. In tutte le
viste 3D + e − fanno lo zoom e **Alt + frecce** (sul gamepad X + croce) girano la camera.

Hanno l'aspetto delle altre app della console (bm Mesh, bm Pixel): a sinistra un pannello
con le liste, sopra la vista due righe che dicono cosa c'è e cosa si sta facendo, in basso
i tasti; le cose scelte in giallo, quella sotto il puntatore in azzurro.

**bm Studio**, F1 **build** — nel pannello a sinistra gli attrezzi, la tessera o il colore
del pennello (con lo sheet intorno) e il modello (facce, triangoli, vertici: avvisa oltre
i 1200 triangoli dei 60 fps):

- **1 blocco** e **2 tessera** (Crocotile all'essenziale): un cursore a forma di cella si
  muove con le frecce sul piano e con PgUp/PgDn in altezza, sempre rispetto alla vista
  (**q e** girano la vista di 45°, **w s** la inclinano). Spazio mette, Backspace toglie:
  due blocchi vicini non hanno parete in mezzo, e togliendone uno ricompare la parete del
  vicino; la tessera va su un lato della cella (**f**: pavimento, pareti, soffitto). **Tab**
  (o Y) apre lo sheet: tessere da 8, 16 o 32 pixel (**z**), anche **più tessere insieme**
  (**a d w s**: una porta alta due tessere si posa in un colpo), o un colore (**c**); **r**
  e **h** girano e specchiano la tessera, **x** la prende da una faccia.
- **3 select**: le frecce portano il puntatore sulla faccia più vicina in quella direzione
  sullo schermo; spazio la sceglie, **a** tutte, **c** quelle unite (un oggetto intero).
  Le facce scelte: **g** le sposta (frecce, PgUp/PgDn; Tab cambia il passo da 1 a 1/16;
  Invio le lascia lì, Esc le rimette dov'erano), **r** le gira, **t** le capovolge, **m** le
  specchia, **n** mostra l'altro lato, Invio ci mette la tessera del pennello, **u** gira la
  texture, **,** **.** dimezzano e raddoppiano, **d** (Ctrl+D) le copia e sposta la copia,
  Canc le cancella, **o** le porta in un modello nuovo.
- **4 vertex**: il puntatore va sugli angoli; spazio li sceglie, **g** li sposta (tetti,
  rampe, forme libere), **m** ne unisce più d'uno in uno.
- **5 paint**: la faccia sotto il puntatore, Invio: la sua tessera grande nel pannello, e
  si dipinge pixel per pixel (spazio; **e** trasparente, **i** prende il colore, **c** la
  tavolozza); il modello cambia mentre si dipinge. Lo sheet dipinto si salva con il modello.
- **v** cambia la vista (luce, colori piatti, fil di ferro), **b** mostra anche le facce
  viste da dietro (più scure), **z** torna al modello.

F2 **models**: i modelli del file, con il loro aspetto; **n** nuovo, **r** rinomina, **d**
duplica, Canc cancella (due volte), PgUp/PgDn cambiano l'ordine, **i** il margine delle
texture, **-** riduce i triangoli (chiede quanti; la metà per default): il riduttore del
kernel (`src/bm/decimate.c`, collasso degli spigoli con le quadriche) tiene bordi, linee di
colore e cuciture della texture, lo scheletro segue i vertici, Ctrl+Z annulla. Per
adattare un modello pesante (un `.glb` importato, un modello di meshy2mesh) ai 1200
triangoli del Pi. Sul PC fa lo stesso `tools/bmreduce.py CART.bm --faces 1200`. Il menu
ha anche titolo e autore della cartuccia.

**bm Animator**:

- F1 **play**: il player. I modelli in una lista, con vertici, triangoli e ossa; la camera
  gira da sola (a d w s per girarla a mano). Le animazioni si scelgono con
  sinistra/destra e partono con spazio; `,` e `.` vanno avanti e indietro di un
  fotogramma, `<` e `>` cambiano la velocità, **k** mostra lo scheletro, **b** mescola
  l'animazione con la successiva (25, 50, 75 %: la stessa `animate()` dei giochi).
- F2 **rig**: **n** fa lo scheletro (un osso, root, dal fondo del modello) e poi aggiunge
  ossa figlie di quella scelta; su/giù sceglie l'osso, **w a s d r f** spostano la sua coda
  (o la testa, con Tab) di 1/8 (maiuscole: 1/32): le giunture nello stesso punto si
  muovono insieme. **m** specchia l'osso e i suoi figli dall'altra parte (nomi `.L` / `.R`,
  anche nelle animazioni), Invio lo rinomina, **p** sceglie il padre, x lo cancella. La
  pelle: **k** dà ogni faccia all'osso più vicino (parti rigide), **K** ogni angolo
  (il modello si stira alle giunture); **v** mostra le facce coi colori delle ossa e le fa
  scegliere col puntatore (spazio, **c** le unite), **a** le dà all'osso (PgUp/PgDn).
- F3 **animate**: **n** fa un'animazione; su/giù sceglie l'osso, sinistra/destra il
  fotogramma (12 al secondo, come bm Animator). w/s, a/d, q/e girano l'osso di 15° intorno
  a x, y, z (maiuscole: 5°), con **g** lo spostano: ogni giro è un keyframe in quel punto.
  **k** aggiunge un keyframe, x lo toglie, **,** **.** vanno al keyframe prima e dopo,
  **(** **)** lo spostano di un fotogramma; **c v** copiano e incollano la posa, **r** rimette
  l'osso a riposo, **m** specchia la posa; **o** mostra le ossa dei keyframe prima e dopo
  (onion skin); **l** ciclo sì/no, **i** linear / smooth / step, `<` `>` la durata. A
  sinistra le animazioni: PgUp/PgDn le cambiano, **n** nuova, Ctrl+D duplica, Invio
  rinomina, Backspace due volte cancella.
- F4 **sprites**: l'animazione disegnata in sprite dal motore 3D della console, per i
  giochi in 2D: fotogrammi, misura (16…128), direzioni (1, 2, 4, 8), quanto dall'alto,
  camera piatta o in prospettiva, luce, contorno, colori ridotti (32, 16, 8), riempimento;
  l'anteprima gira tra le direzioni. Invio mette la griglia nello sprite sheet (sotto
  quello che c'è, allargandolo se serve) e mostra il codice `sspr()` per disegnarla.

Il **menu** apre i `.bm` della SD, salva (Ctrl+S, o *Save as* con un nome 8.3 in
`/carts`), **prova il gioco** (F5: si gioca il file salvato, poi si torna nella stessa
pagina); bm Studio fa anche un progetto nuovo (con lo sheet di tessere iniziale di bm Studio
e il codice del visualizzatore dei modelli). `[` e `]` (o Y + su/giù) cambiano modello.
Ctrl+Z e Ctrl+Y (Y + A sul gamepad) annullano e rifanno, anche i pixel dipinti e gli
sprite messi nello sheet.

Come lavorano: i modelli e gli scheletri sono tabelle Lua; a ogni modifica la cartuccia
riscrive la parte di quel modello delle sezioni MESH e ANIM (`string.pack`, il formato di
`src/bm/bm.h`) e le passa al kernel con `cart_data()`, che le controlla: `model()`,
`animate()` e `bone3d()` disegnano e muovono sempre quello che verrà salvato. Il codice
comune ai due programmi (formato, progetto, annulla, menu, schede, il puntatore della
tastiera) è la libreria del kernel `src/script/bm3d.lua` (`require "bm3d"`). Salvano con
`cart_write`: nel file cambiano solo MESH e ANIM (e lo sheet, se è stato dipinto o ha
ricevuto sprite); un `.bm` aperto e salvato senza modifiche resta uguale byte per byte.

Restano solo sul PC: import ed export `.glb` e `.png`, la copertina da un'immagine; ogni
angolo segue un osso (niente pesi misti) e un keyframe è la posa intera, come sul PC.

## Sulla console: bm Mesh

**bm Mesh** è l'editor delle mesh, incorporato nel kernel (`carts/mesh/main.lua`): scheda
**Dev**, oppure **X** sulla copertina di un gioco → **Open in bm Mesh** (dal monitor, il
tasto `4`). Legge tre tipi di mesh di un `.bm`, segnati nella lista con una lettera:

- **M**, i **modelli** della sezione MESH (quelli di bm Studio, sul PC e sulla console, con lo
  scheletro di bm Animator se ce l'hanno): nel gioco `model("nome")`;
- **C**, le mesh **nel codice** scritte da bm Mesh: funzioni `mesh_nome()` alla fine di
  `main.lua`, tra le righe `-- [bm Mesh begin]` e `-- [bm Mesh end]`; nel gioco
  `local m = mesh_nome()` (in `_init` o dopo) e poi `draw3d(m, ...)`;
- **G**, le mesh che il **codice del gioco** costruisce con `mesh()`, `mesh_sphere()` e
  `mesh_cube()`, come le navi e gli anelli di Astro Wing. Il kernel le trova eseguendo il
  codice a parte (`cart_meshes()`: niente file, schermo o suono, un limite di istruzioni)
  e dà a ciascuna il nome della variabile che la tiene (`M.ship` → `ship`).

Due pagine (F1 lista, F2 modifica; sul gamepad Y + sinistra/destra) e il menu con Esc;
tenendo premuto **F12**, o con **?**, compaiono i tasti.

- **F1, la lista**: su/giù sceglie la mesh, che gira in anteprima con vertici, triangoli e
  scheletro. **m** la copia come **modello** (da mesh a modello), **c** come **codice** (da
  modello a mesh: `mesh_nome()`), **r** rinomina, **d** duplica (un modello con lo
  scheletro), **Del** due volte cancella, **n** fa un modello nuovo (un cubo; con F3 anche
  piano e sfera). Le mesh del gioco non si rinominano né si cancellano: è il suo codice
  che le fa (bm Code lo modifica).
- **F2, la modifica**: un **puntatore** (frecce, più veloce tenendo premuto; sul gamepad la
  croce) indica il vertice o la faccia sotto di sé (**Tab** cambia tra vertici e facce;
  **n** e **b** lo portano sul successivo o sul precedente, anche dietro). **Spazio**
  aggiunge o toglie dalla scelta, **Invio** sceglie solo quello, **a** tutto o niente,
  **l** tutto quello che è collegato. Poi:
  - **g** sposta, **r** ruota, **t** scala: le frecce e PgUp/PgDn cambiano il valore
    (sinistra/destra e PgUp/PgDn sui due assi della vista, su/giù in altezza; **x y z**
    solo su quell'asse, **n** lungo la normale), `,` e `.` il passo (0,01–1; 1–90°;
    ×1,01–2), **Invio** conferma, **Esc** annulla;
  - **x** estrude le facce scelte (poi si spostano lungo la normale), **d** le duplica,
    **m** specchia la scelta (sinistra-destra sullo schermo), **M** la copia dall'altra
    parte dello 0 (per i modelli simmetrici: i vertici sullo 0 restano in comune);
  - **j** fa una faccia sui 3 o 4 vertici scelti (nell'ordine, verso la camera), **k**
    unisce i vertici scelti in uno, **K** salda quelli nello stesso punto (con il flag 4
    di `draw3d` le facce intorno sembrano lisce), **u** divide ogni faccia in 4, **i** le
    gira, **Del** cancella;
  - **p** colora le facce, **o** prende il colore dalla faccia, **c** la tavolozza;
  - la vista: q e girano, w s inclinano, + − zoom, **f** inquadra la scelta, 1 3 7 0 le
    viste dritte; sul gamepad X + croce. Ctrl+Z e Ctrl+Y annullano e rifanno.

  Modificare una mesh **G** ne fa prima una copia come modello (il codice del gioco non si
  riscrive): il gioco la usa con `model("nome")`.

Il **menu** apre un altro `.bm`, salva (Ctrl+S) o salva come (nome 8.3 in `/carts`) e
**prova il gioco** (F5: si torna nella stessa pagina). Il salvataggio usa
`cart_write(path, {sections = {[8] = MESH, [9] = ANIM}, lua = ...})`: cambiano solo i
modelli e il blocco di bm Mesh nel codice; sprite sheet, mappa, copertina, banco di suoni
e il resto del codice restano byte per byte. Un modello con lo scheletro lo tiene: l'osso
di ogni vertice segue i vertici aggiunti (quello del vertice da cui vengono) e tolti, e
ossa e animazioni restano quelle di bm Animator.

Compatibile con le altre app: i modelli sono quelli che leggono e scrivono bm Studio, bm
Animator (sul PC e sulla console), `mkbm.py --models` e il kernel; le mesh nel codice hanno il formato
di "Copy as Lua" di bm Studio (vertici, poi `a, b, c, colore` con `-1` per la texture, poi
le coordinate dello sheet) e si aprono in bm Code come il resto del codice.

## Sulla console: bm Pixel

**bm Pixel** è l'editor della pixel art, incorporato nel kernel (`carts/pixel/main.lua`):
scheda **Dev**, oppure **X** sulla copertina di un gioco → **Open in bm Pixel** (dal monitor,
il tasto `5`). Lavora sullo **sprite sheet** del `.bm`, lo stesso che usano i giochi
(`spr`, `sspr`, `map`), l'SDK e bm Studio (le texture dei modelli). Tre pagine
(F1–F3, sul gamepad Y + sinistra/destra), il menu con Esc; tenendo premuto **F12**, o con
**?**, compaiono i tasti; **Tab** (sul gamepad X) apre l'elenco dei comandi della pagina.

- **F1, il disegno**: lo sprite scelto, ingrandito (8×8, 16×16, 32×32, 64×64 o 128×128
  pixel: **z** cambia la misura, PgUp e PgDn passano allo sprite prima e dopo), con la
  griglia (**t**). Un puntatore si muove con le frecce (o la croce) e **spazio** (o A)
  usa l'attrezzo; tenendo premuto spazio la matita traccia una linea.
  - **b** matita, **e** gomma, **g** riempimento, **i** il colore di un pixel (un colore
    nuovo entra nella tavolozza), **l** linea, **u**/**U** rettangolo vuoto/pieno, **o**/**O**
    ovale vuoto/pieno, **m** selezione: per linee, rettangoli, ovali e selezioni lo spazio
    fissa un angolo, le frecce portano all'altro, lo spazio di nuovo disegna.
  - Con una selezione (o tutto lo sprite): Ctrl+C copia, Ctrl+X taglia, Ctrl+V incolla
    (il blocco galleggia: si sposta con le frecce e si posa con spazio o Invio; i suoi
    pixel trasparenti lasciano quello che c'è sotto), **Invio** solleva la selezione per
    spostarla, **h**/**v** specchiano, **r** gira di un quarto, Canc cancella;
    Shift+**w a s d** fanno scorrere lo sprite di un pixel (quello che esce rientra
    dall'altra parte).
  - **y** disegna a specchio (sinistra-destra), **,** e **.** cambiano colore, **x** torna
    al colore di prima, **1**–**9**, **0** i primi dieci; il colore 0 è il trasparente.
  - **Animazione**: lo sprite e quelli che lo seguono nello sheet sono i fotogrammi
    (**+**/**−** quanti, **<**/**>** la velocità, **p** ferma o riparte): il riquadro a destra
    li fa girare, accanto allo sprite alla misura vera. **k** è l'onion skin: il fotogramma
    prima si vede a puntini sotto i pixel trasparenti.
  - **F6**: l'assistente (M30) disegna la base di uno sprite da una parola ("slime",
    "moneta", "astronave"…) con i colori della tavolozza; galleggia come un incollato.
- **F2, lo sheet**: lo sheet intero (zoom con + e −), le frecce scelgono lo sprite (nella
  misura scelta), Invio lo disegna; Ctrl+C e Ctrl+V copiano uno sprite in un altro posto,
  Canc lo svuota, **R** cambia la misura dello sheet (multipli di 8 fino a 4096: quello che
  ci sta resta). In alto il numero dello sprite e la chiamata `spr()` che lo disegna.
- **F3, la tavolozza**: fino a 256 colori. Frecce e Invio scelgono il colore con cui
  disegnare, **e** lo modifica (R, G e B con le frecce, < e > di uno; accanto il colore come
  lo mostra la console, in RGB565), **a** ne aggiunge uno, Canc lo toglie, **[** e **]** lo
  spostano, **s** ordina per tinta, **f** prende i colori usati nello sheet, **1** e **2**
  mettono le tavolozze dell'SDK e di bm Studio; **x** (**X**) cambia il colore con cui si
  disegna in quello scelto, in tutto lo sprite (in tutto lo sheet).

Il **menu** apre un altro `.bm`, fa uno **sheet nuovo** (256×256; salvandolo con *Save as*
diventa una cartuccia con un codice che mostra lo sheet), salva (Ctrl+S), salva come,
**prova il gioco** (F5: si torna nella stessa pagina), cambia la misura dello sheet.
Ctrl+Z e Ctrl+Y (Y + A sul gamepad) annullano e rifanno. Per ogni file si ricorda lo
sprite, la misura, l'animazione e la pagina.

Il salvataggio usa `cart_write(path, {sheet = true, palette = ...})`: nel file cambia solo
lo sheet (codice, mappa, copertina, suoni, modelli e scheletri restano byte per byte, e un
file con il nome lungo lo tiene). Lo sheet diventa una sezione **SHEET8** quando ha al più
256 colori, con la tavolozza di bm Pixel per prima: riaprendo il file torna la stessa
tavolozza, e la leggono anche l'SDK, bm Studio, bm Animator, `mkbm.py` e i giochi. La
console tiene 16 bit per pixel (RGB565): un pixel che non è stato ridisegnato tiene i 24 bit
che aveva nel file (quelli di bm Studio sul PC), uno ridisegnato prende quelli del colore
della tavolozza. Anche gli sheet grandi si aprono (quello di Titan Clash, 2048×3448, parte
rimpicciolito a 1/4); scriverli richiede qualche secondo, e intanto lo schermo dice
"saving ...".

## Test

```sh
make test-studio      # core in Node (.bm, ANIM, PNG, glTF anche animato, sprite, geometria), letto da Python e dal kernel
make test-studio-ui   # nel browser con Playwright: bm Studio e bm Animator col mouse, screenshot in build/studio/
```

`make test` comprende `test-studio` (saltato senza Node) e, in QEMU, `test_models`,
`test_sdk_keeps_models`, `test_village`, `test_animation` (un braccio che si alza sulla
console emulata) e `test_studio_cart` (i `.bm` scritti dallo Studio, con il loro
visualizzatore).

bm Studio e bm Animator della console hanno un banco di prova sul PC
(`tests/studio/tools3d_host.lua`, dentro `make test-studio`, con il Lua 5.4 della console):
le API di bm sostituite (una cartella fa da SD; i disegni controllano i loro argomenti, i
tasti chip i loro nomi), i tasti e il gamepad simulati, e il percorso intero: bm Studio
sul villaggio, blocchi, tessere (anche più insieme), selezione e spostamenti, vertici,
pittura, viste, modelli, annulla, salvataggio e riapertura; poi bm Animator sullo stesso
file (`cart_tool`): ossa, specchio, nomi, padre, pelle, keyframe, onion, animazioni,
sprite nello sheet, salvataggio. I file sono riletti da bm Studio (`check_studio3d.js`: le
facce sono **identiche** a quelle dello Studio con gli stessi attrezzi), da `bmmesh.py` e
dal parser del kernel (`test_bm`). In QEMU, `test_studio_animator` apre il villaggio in bm
Studio dalle opzioni del gioco, prova gli attrezzi, costruisce, salva, prova il gioco,
passa a bm Animator, anima, salva e mette gli sprite del paesano nello sheet.

bm Mesh ha il suo banco di prova sul PC (`tests/studio/mesh_host.lua`, in `make
test-studio`): `cart_meshes()` sostituito da un `load` del codice del gioco con le stesse
regole del kernel, e il percorso intero (le 13 mesh di Astro Wing, mesh → modello uguale a
quello che il codice dà a `mesh()`, mesh → codice che rieseguito dà le stesse mesh,
spostamenti, annulla, suddivisione, specchio, facce nuove, unione, colore, estrusione,
duplicazione, il paesano con lo scheletro dopo vertici spostati e cancellati, salva come).
I file che scrive sono riletti da bm Studio (`check_mesh.js`), da `bmmesh.py` e dal kernel
(`test_meshcap`: ogni scheletro corrisponde al suo modello). `test_meshcap` prova anche la
cattura vera (`src/bm/meshcap.c`) su Astro Wing, Texture Room e Chaos Kitchen. In QEMU,
`test_mesh` apre Astro Wing dalle opzioni, copia la nave come modello, ne sposta i vertici,
la copia come codice e salva.

bm Pixel ha il suo banco di prova sul PC (`tests/studio/pixel_host.lua`, in `make
test-studio`): lo sheet in una tabella con i colori come li tiene il kernel, `cart_write`
che lo scrive come lui, e tutti gli attrezzi (matita e tratto, linea, rettangolo, ovale,
riempimento, contagocce, specchio, selezione, copia, incolla, sollevare e spostare,
specchiare, girare, far scorrere, annulla e rifai, l'assistente, la tavolozza, lo sheet più
alto, salva, riapri, sheet nuovo, prova il gioco). I file sono riletti da bm Studio
(`check_pixel.js`: i pixel non ridisegnati con i loro 24 bit, la tavolozza, modelli e
scheletri intatti) e dal kernel (`test_meshcap`); `test_bm` prova il packer SHEET8 del kernel
(le stesse sequenze del codificatore di bm Studio), lo sheet al posto del vecchio in
`bm_rewrite_with` e lo `sspr` ingrandito. In QEMU, `test_pixel` apre Studio Village dalle
opzioni, disegna e salva: nel file cambiano solo i pixel disegnati, gli altri restano
identici byte per byte; `test_pixel_big` fa lo stesso con lo sheet di Titan Clash
(2048×3448): cambia un pixel solo e la tavolozza resta quella.

## Struttura

```
sdk/studio/index.html        bm Studio
sdk/studio/studio.css        l'aspetto (i colori dell'interfaccia della console)
sdk/studio/js/core.js        formato .bm e sezioni, MESH, ANIM, SHEET8, PNG, glTF, codice Lua (anche in Node)
sdk/studio/js/tiles.js       lo sheet iniziale, disegnato nel codice
sdk/studio/js/edit.js        geometria degli strumenti: tessere, blocchi, raggi, spostamenti
sdk/studio/js/rig.js         scheletri: quaternioni, pose, keyframe, pelle (gli stessi conti del kernel)
sdk/studio/js/sprites.js     da 3D a sprite: un rasterizzatore software (anche in Node)
sdk/studio/js/gltfskin.js    .glb con scheletro e animazioni
sdk/studio/js/examples.js    il paesano d'esempio (scheletro e tre animazioni)
sdk/studio/js/handoff.js     il progetto da un'applicazione all'altra
sdk/studio/js/gl.js          rendering WebGL con la convenzione di bm (r3d.c)
sdk/studio/js/view3d.js      vista 3D e strumenti di bm Studio
sdk/studio/js/sheetview.js   lo sheet: scelta delle tessere ed editor dei pixel
sdk/studio/js/app.js         bm Studio: file, annulla, pannelli, tasti
sdk/animator/index.html      bm Animator
sdk/animator/js/animator.js  bm Animator: vista con ossa e anelli, linea del tempo, sprite, file
scripts/bmmesh.py            MESH e .glb per mkbm.py
carts/studio/main.lua        bm Studio della console (incorporato nel kernel, scheda Dev)
carts/animator/main.lua      bm Animator della console (incorporato nel kernel, scheda Dev)
carts/studio/mkassets.js     lo sheet di bm Studio (le tessere iniziali) e le due copertine
src/script/bm3d.lua          il codice comune ai due (require "bm3d")
tests/studio/tools3d_host.lua     bm Studio e bm Animator sul PC, con le API di bm sostituite
tests/studio/check_studio3d.js    i loro file riletti da bm Studio
carts/mesh/main.lua          bm Mesh, l'editor delle mesh (incorporato nel kernel, scheda Dev)
carts/mesh/mkcover.js        la sua copertina
src/bm/meshcap.c             cart_meshes(): le mesh che il codice di un .bm costruisce
tests/bm/test_meshcap.c      la cattura sulle cartucce vere
tests/studio/mesh_host.lua   bm Mesh sul PC, con le API di bm sostituite
tests/studio/check_mesh.js   i suoi file riletti da bm Studio
carts/pixel/main.lua         bm Pixel, l'editor della pixel art (incorporato nel kernel, scheda Dev)
carts/pixel/mkcover.js       la sua copertina
tests/studio/pixel_host.lua  bm Pixel sul PC, con le API di bm sostituite
tests/studio/check_pixel.js  i suoi file riletti da bm Studio
```
