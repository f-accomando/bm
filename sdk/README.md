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

Sulla console c'è la loro versione semplificata, lo **studio 3D** (scheda **Dev**): un
player dei modelli e delle animazioni, e gli strumenti essenziali per costruire, fare lo
scheletro e animare, sugli stessi file. [Vedi sotto](#sulla-console-lo-studio-3d).

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
- `bone3d(m, "arm.L")`: dove si trova la testa di un osso nella posa (coordinate del
  modello): per attaccarci una spada, una lanterna...
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

## Sulla console: lo studio 3D

Lo **studio 3D** è una cartuccia incorporata nel kernel (`carts/studio3d/main.lua`),
nella scheda **Dev** accanto all'SDK. Si apre anche da un gioco: **X** sulla copertina,
**Open in the 3D studio** (o, dal monitor, il tasto `3`). Legge e scrive le stesse sezioni
MESH e ANIM di bm Studio e bm Animator: un `.bm` fatto sul PC si apre sulla console e
viceversa, e un gioco senza modelli può riceverne. Si usa con la tastiera o con il
gamepad (Bluetooth o USB); è pronto per un puntatore quando arriverà il mouse Bluetooth.

Quattro pagine (F1–F4, oppure Y + sinistra/destra sul gamepad), il menu con Esc
(Y + B); tenendo premuto **F12**, o con **?**, compaiono i tasti della pagina.

- **F1 play, il player**: i modelli del `.bm` in una lista, con vertici, triangoli e ossa;
  la camera gira da sola intorno al modello (a d w s per girarla a mano, + e − per lo
  zoom; sul gamepad X + croce). Le animazioni si scelgono con sinistra/destra e partono
  con spazio; `,` e `.` vanno avanti e indietro di un fotogramma, `<` e `>` cambiano la
  velocità, **k** mostra lo scheletro, **b** mescola l'animazione con la successiva
  (25, 50, 75 %: la stessa `animate()` dei giochi).
- **F2 build, i blocchi e le tessere** (Crocotile all'essenziale): un cursore a forma di
  cella si muove con le frecce sul piano e con PgUp/PgDn in altezza, sempre rispetto alla
  vista (q e girano la vista di 45°, w s la inclinano). Tre attrezzi:
  1 **blocco** (spazio lo mette, Backspace lo toglie: due blocchi vicini non hanno parete
  in mezzo, e togliendone uno ricompare la parete del vicino), 2 **tessera** (su un lato
  della cella: pavimento, parete in fondo, a destra, davanti, a sinistra, soffitto; **f**
  cambia lato), 3 **pittura** (ridipinge le facce di quel lato, o di tutti i lati; **x**
  prende la tessera da una faccia). **Tab** (o Y) apre lo sheet del progetto: si sceglie
  una tessera da 8, 16 o 32 pixel (**z**) o un colore (**c**); **r** e **h** girano e
  specchiano la tessera. Le facce sono le stesse che fa bm Studio con gli stessi attrezzi.
- **F3 rig, le ossa**: **n** fa lo scheletro (un osso, root, dal fondo del modello) e poi
  aggiunge ossa figlie di quella scelta; su/giù sceglie l'osso, w a s d r f spostano la sua
  coda (o la testa, con Tab) di 1/8 (maiuscole: 1/32); x lo cancella, **k** dà ogni faccia
  all'osso più vicino (la pelle a parti rigide), **v** colora le facce con il loro osso.
- **F4 animate, i keyframe**: **n** fa un'animazione; su/giù sceglie l'osso,
  sinistra/destra il fotogramma (12 al secondo, come bm Animator). w/s, a/d, q/e girano
  l'osso di 15° intorno a x, y, z (maiuscole: 5°), con **g** lo spostano: ogni giro è un
  keyframe in quel punto. k aggiunge un keyframe, x lo toglie, c e v copiano e incollano la
  posa, r rimette l'osso a riposo; l ciclo sì/no, m linear / smooth / step, `<` `>` la
  durata; PgUp/PgDn cambiano animazione, Backspace due volte la cancella.

Il **menu** apre i `.bm` della SD, fa un progetto nuovo (con lo sheet di tessere iniziale
di bm Studio e il codice del visualizzatore dei modelli), salva (Ctrl+S, o *Save as* con
un nome 8.3 in `/carts`), **prova il gioco** (F5: si gioca il file salvato, poi si torna
allo studio nella stessa pagina) e crea, rinomina, duplica e cancella i modelli; `[` e `]`
(o Y + su/giù) cambiano modello. Ctrl+Z e Ctrl+Y (Y + A sul gamepad) annullano e rifanno.

Come lavora: i modelli e gli scheletri sono tabelle Lua; a ogni modifica la cartuccia
riscrive la parte di quel modello delle sezioni MESH e ANIM (`string.pack`, il formato di
`src/bm/bm.h`) e le passa al kernel con `cart_data()`, che le controlla: `model()`,
`animate()` e `bone3d()` disegnano e muovono sempre quello che verrà salvato, e
`cart_save()` lo scrive. Un `.bm` aperto e salvato senza modifiche tiene le sue sezioni
byte per byte.

Più semplice dei programmi per il PC: niente angoli spostati a mano, selezioni, pittura
dello sheet (si fa nell'SDK, pagina sprite), import/export `.glb` e sprite
pre-renderizzati; un osso per faccia (la pelle a parti rigide); keyframe di posa intera.

## Test

```sh
make test-studio      # core in Node (.bm, ANIM, PNG, glTF anche animato, sprite, geometria), letto da Python e dal kernel
make test-studio-ui   # nel browser con Playwright: bm Studio e bm Animator col mouse, screenshot in build/studio/
```

`make test` comprende `test-studio` (saltato senza Node) e, in QEMU, `test_models`,
`test_sdk_keeps_models`, `test_village`, `test_animation` (un braccio che si alza sulla
console emulata) e `test_studio_cart` (i `.bm` scritti dallo Studio, con il loro
visualizzatore).

Lo studio 3D della console ha un banco di prova sul PC (`tests/studio/studio3d_host.lua`,
dentro `make test-studio`, con il Lua 5.4 della console): le API di bm sostituite (una
cartella fa da SD), i tasti e il gamepad simulati, e il percorso intero (il player sul
villaggio, blocchi, tessere, pittura, annulla, ossa, keyframe, salvataggio e riapertura).
I file che scrive sono riletti da bm Studio (`check_studio3d.js`: le facce sono
**identiche** a quelle dello Studio con gli stessi attrezzi), da `bmmesh.py` e dal parser
del kernel (`test_bm`). In QEMU, `test_studio3d` apre il villaggio dalle opzioni del gioco,
costruisce, anima, salva, prova il gioco e torna.

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
carts/studio3d/main.lua      lo studio 3D della console (incorporato nel kernel, scheda Dev)
carts/studio3d/mkassets.js   il suo sheet (le tessere iniziali) e la copertina
tests/studio/studio3d_host.lua    lo studio 3D sul PC, con le API di bm sostituite
tests/studio/check_studio3d.js    i suoi file riletti da bm Studio
```
