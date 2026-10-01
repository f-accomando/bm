# bm Studio — modelli 3D e pixel art per le cartucce `.bm`

**bm Studio** (`sdk/studio/`) è l'applicazione per il PC con cui si fanno le risorse
delle cartucce `.bm`: **modelli 3D a tessere** in stile [Crocotile 3D](https://crocotile3d.com/)
e la **pixel art dello sprite sheet**. Lavora direttamente sul `.bm`: lo apre, cambia i
modelli e lo sheet, lo salva al suo posto (anche sulla SD). Non tocca il codice del gioco,
la mappa e le sezioni che non conosce: le riscrive come le ha trovate.

È una pagina web senza dipendenze (HTML + JavaScript, niente da installare né compilare).

## Aprirlo

- **Doppio clic su `sdk/studio/index.html`** con Chrome o Edge (da Windows con WSL:
  `\\wsl$\<distro>\...\bm\sdk\studio\index.html`, oppure copia la cartella `sdk/studio`
  dove vuoi). Firefox e Safari funzionano, ma senza il salvataggio "al suo posto": il
  `.bm` si scarica.
- Oppure `make studio` e poi <http://localhost:8765> (da WSL si apre anche dal browser di
  Windows).

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

## Test

```sh
make test-studio      # core in Node (file .bm, PNG, glTF, geometria), letto da Python e dal kernel
make test-studio-ui   # nel browser con Playwright: mouse, strumenti, salvataggio, screenshot in build/studio/
```

`make test` comprende `test-studio` (saltato senza Node) e, in QEMU, `test_models`,
`test_sdk_keeps_models` e `test_village`.

## Struttura

```
sdk/studio/index.html        la pagina
sdk/studio/studio.css        l'aspetto (i colori dell'interfaccia della console)
sdk/studio/js/core.js        formato .bm e sezioni, MESH, SHEET8, PNG, glTF, codice Lua (anche in Node)
sdk/studio/js/tiles.js       lo sheet iniziale, disegnato nel codice
sdk/studio/js/edit.js        geometria degli strumenti: tessere, blocchi, raggi, spostamenti
sdk/studio/js/gl.js          rendering WebGL con la convenzione di bm (r3d.c)
sdk/studio/js/view3d.js      vista 3D e strumenti
sdk/studio/js/sheetview.js   lo sheet: scelta delle tessere ed editor dei pixel
sdk/studio/js/app.js         l'applicazione: file, annulla, pannelli, tasti
scripts/bmmesh.py            MESH e .glb per mkbm.py
```
