# Cartucce native `.bm`: API e prima cartuccia

> Guida pratica passo per passo (sprite, mappe, modelli 3D, suono, luci, salvataggi):
> [GUIDA-GIOCHI.md](GUIDA-GIOCHI.md). In inglese: [API.md](API.md) e
> [GAME-GUIDE.md](GAME-GUIDE.md).

Una cartuccia `.bm` è un gioco per bm scritto in **Lua 5.4**. Il kernel disegna in C
(640×360, colore a 16 bit RGB565, 60 fotogrammi al secondo); Lua si occupa solo della
logica. Esempi completi in `carts/`: `pong/`, `snake/`, `shooter/` (solo codice) e
`demo/` (con sprite sheet PNG e mappa CSV).

## La prima cartuccia in 5 minuti

1. Crea `carts/ciao/main.lua`:

   ```lua
   local x, y = 300, 160

   function _init()                 -- una volta, all'avvio
   end

   function _update()               -- 60 volte al secondo: logica
     if btn(0) then x = x - 3 end   -- sinistra
     if btn(1) then x = x + 3 end   -- destra
     if btn(2) then y = y - 3 end   -- su
     if btn(3) then y = y + 3 end   -- giù
   end

   function _draw()                 -- 60 volte al secondo: disegno
     cls(0x102040)                  -- sfondo blu scuro
     circfill(x, y, 20, 0xFFD050)   -- una palla gialla
     print("ciao da bm!", 8, 8, 0xFFFFFF)
   end
   ```

2. Impacchettala:

   ```sh
   python3 scripts/mkbm.py -o ciao.bm --lua carts/ciao/main.lua --title "Ciao" --author "io"
   ```

   Per aggiungerla alla build, metti il nome in `GAMES` nel `Makefile` e il titolo in
   una riga `title_ciao := Ciao`: `make` la crea in `build/carts/ciao.bm`.

   **Copertina** (facoltativa): `--cover copertina.png`, un PNG di qualsiasi misura che
   il menu stampa sulla scheda: un quadrato di 88×88 (2026-10-04; prima 128×80). Un'immagine
   quadrata viene ridotta; le altre restano intere, larghe quanto il quadrato, sopra una
   copia sfocata e più scura di sé che riempie il resto. Nella
   build basta un file `carts/ciao/cover.png`; quelle dei giochi demo sono disegnate da
   `scripts/mkcovers.py`. Senza copertina il menu stampa il titolo.

3. Provala:
   - **sul Pi**: copia `ciao.bm` nella cartella `carts/` della SD, riaccendi (o premi
     `R` nel menu): compare nel menu con titolo e autore;
   - **in QEMU** (senza Pi):

     ```sh
     qemu-system-arm -M raspi0 -bios build/kernel.img -serial tcp:127.0.0.1:4444,server=on,wait=off -serial null &
     python3 tools/bm_load.py tcp:127.0.0.1:4444 --cart ciao.bm
     ```

     (dal menu o dal monitor: il loader manda `U`, poi la cartuccia);
   - **dalla seriale** (Pi con adattatore USB-seriale):
     `python3 tools/bm_load.py /dev/ttyUSB0 --cart ciao.bm`.

Ctrl+Esc (tastiera), PS o Start+Select (gamepad) tornano al menu di bm lasciando il gioco
**sospeso** (fermo in memoria: A sulla sua copertina lo riprende da dove era; se la cartuccia ha
`_exit()`, prima chiedono a lei); Esc da solo, in un gioco, è Start (il
suo menu). In una **partita in rete** (`online(true)`) il gioco non si sospende: al giocatore
che esce, solo sulla sua console, il sistema chiede "Leave the match?" (uscirà dal gioco e si
disconnetterà dal server); sì chiama `_leave()` e chiude il gioco, indietro resta.
Dalla seriale Ctrl+\ fa Ctrl+Esc e `q` chiude senza chiedere. I tasti di sistema
(una sola tabella, `src/kernel/syskeys.c`) e quelli della cartuccia si vedono tenendo F12.
Se c'è un errore Lua, la cartuccia si ferma e l'errore, con la riga, appare sulla console.

## Struttura

| Funzione | Quando |
|---|---|
| `_init()` | una volta, dopo il caricamento |
| `_update()` | ogni fotogramma (60 Hz), prima di `_draw` |
| `_draw()` | ogni fotogramma, dopo `_update` |
| `_exit()` | (facoltativa) Ctrl+Esc, PS o Start+Select: `true` chiude adesso; `false` resta (la cartuccia chiede, per esempio "modifiche non salvate: Ctrl+Esc di nuovo esce", e chiude dopo con `quit()` o al secondo Ctrl+Esc) |
| `_leave()` | (facoltativa) in una partita in rete (`online(true)`), quando il giocatore conferma l'uscita: il gioco lo dice al server (o all'host), poi la cartuccia si chiude senza sospendersi |

Globali: `SCREEN_W` e `SCREEN_H` (640 e 360; 480 e 270 con `--res 480x270`; 320 e 180 con
`--res 320x180`; 256 e 256 con `--res 256x256`; 360 e 360 con `--res 360x360`, lo schermo
del `.b16`). La cartuccia può cambiare risoluzione
mentre gira con `screen(w, h)` (sotto): dal fotogramma dopo valgono le misure nuove.
Lo schermo **non** viene cancellato da solo: di solito `_draw` comincia con `cls()`.

Limiti: un errore o un ciclo infinito (oltre **20 milioni di istruzioni** Lua in un
fotogramma) ferma la cartuccia senza bloccare la console. Sandbox: niente `io`, `os`,
`load`, `dofile`; `require` carica solo le librerie incluse nel kernel (`"bmlib"`, la
libreria comune dei giochi: [sotto](#bmlib-la-libreria-comune-dei-giochi); `"assist"`, il
pannello dell'assistente; `"bm3d"`, quello che bm Studio e bm Animator condividono;
`"predict"` e `"words"`, il completamento delle parole); ci sono `string`, `table`,
`math`, `utf8`, `coroutine`.

## Colori

I colori sono interi `0xRRGGBB` (es. `0xFF8000` arancione) o `rgb(r, g, b)` con valori
0–255. Lo schermo li converte in RGB565 (5 bit rosso, 6 verde, 5 blu).

## Riferimento

Le coordinate sono in pixel, (0,0) in alto a sinistra; `w` e `h` sono larghezza e altezza.

### Schermo e forme

| Funzione | Descrizione |
|---|---|
| `cls([c])` | riempie lo schermo (nero se `c` manca). Con il 3D sulla GPU non lo riempie l'ARM: il lavoro della GPU pulisce la pagina a quel colore (gratis anche a 1080p); se prima del 3D arriva del 2D lo riempie l'ARM come sempre |
| `screen(w, h)` | la risoluzione della cartuccia da qui al prossimo fotogramma: `true`, oppure `false` se non è una di queste (16:9): 320×180, 384×216, 480×270, 640×360, 960×540, 1280×720, 1920×1080 (su un TV 1080p tutte a pixel interi tranne 1280×720). `SCREEN_W` e `SCREEN_H` cambiano quando è fatto; lo z-buffer, la camera 3D e i buffer della luce seguono, il font e `camera()` restano, `clip()` torna a tutto lo schermo. Se la console non riesce a dargliela resta quella di prima. `screen()` → larghezza e altezza di adesso; `screen(i)` → la i-esima della lista (da 1), o `nil`. Sulla **RGB30** (pannello quadrato) la lista sono invece le due misure del `.b16`, 360×360 (mostrato grande il doppio) e 720×720, entrambe a pixel interi. Una cartuccia quadrata (256×256, 360×360) cambia come le altre (2026-10-10): sul Pi allora riempie il TV, e lì la GPU può disegnarne il 3D (nel riquadro al centro lo disegna l'ARM). Il rasterizzatore dell'ARM paga ogni pixel: sul Pi sopra 640×360 serve la GPU |
| `pset(x, y, c)` / `pget(x, y)` | scrive / legge un pixel (`pget` dà `0xRRGGBB` o `nil` fuori schermo) |
| `line(x0, y0, x1, y1, c)` | linea |
| `rect(x, y, w, h, c)` / `rectfill(x, y, w, h, c)` | rettangolo vuoto / pieno |
| `circ(x, y, r, c)` / `circfill(x, y, r, c)` | cerchio vuoto / pieno |
| `tri(x0, y0, x1, y1, x2, y2, c, [c1, c2])` | triangolo pieno; con tre colori (uno per vertice) il colore sfuma da un angolo all'altro (Gouraud, con dithering) |
| `print(testo, x, y, [c, scala])` | testo con il font 8×16 (bianco se `c` manca), ingrandito `scala` volte (1–8: 2 = caratteri 16×32); restituisce la x dopo l'ultimo carattere |
| `font([nome])` | il font di `print` da qui in poi: `"8x16"` (quello normale), `"8x14"` o `"6x12"` (106 colonne per 30 righe a 640×360: per gli strumenti con tanto testo); restituisce larghezza e altezza di un carattere del font corrente |
| `prompt(nome, x, y, [piccolo, scala])` | un tasto disegnato come un chip colorato (il set delle app, `src/kernel/prompts.c`) con l'angolo in alto a sinistra in (x, y): alto 16 pixel accanto al testo 8×16, 12 con `piccolo` (da solo quando il font è `"6x12"`), ingrandito `scala` volte (1–8, come `print`); restituisce la x dopo il chip. In **maiuscolo** i pulsanti del pad (`"A"`, `"B"`, `"X"`, `"Y"`, `"START"`, `"SELECT"`, `"L1"`…`"R3"`, `"UP"`, `"UPDOWN"`, `"LEFTRIGHT"`, `"DPAD"`, `"LSTICK"`, `"PS"`, `"TOUCHPAD"`), disegnati come sul pad usato per ultimo: un DS4 (croce, cerchio, quadrato, triangolo, OPTIONS, SHARE) finché non se ne usa un altro, che li ha con le lettere. Sulla **RGB30** `"A"`, `"B"`, `"X"`, `"Y"` sono i tasti della console (disposizione Nintendo: X in alto, A a destra, B in basso, Y a sinistra), un tasto scuro con la lettera nel suo colore: A verde, B blu, X rosso, Y giallo (finché non si usa un pad Bluetooth); `"RGB30_A"`, `"RGB30_B"`, `"RGB30_X"`, `"RGB30_Y"` li disegnano su ogni console. In **minuscolo** i tasti della tastiera, coi nomi di `keyp()` (`"enter"`, `"esc"`, `"tab"`, `"space"`, `"up"`, `"f1"`…) o un carattere (`"s"`, `"1"`, `"+"`). `"ok"`, `"back"` e le azioni di `keymap()` sono il loro pulsante (sulla RGB30 `"ok"` è B e `"back"` A, scambiati da `confirm=a`: il chip è il tasto premuto davvero). Con `giocatore` (1–4, dopo `scala`: `prompt(nome, x, y, piccolo, scala, giocatore)`) il pulsante come lo mostra il controller di quel giocatore: il simbolo del DS4, la lettera di un pad o il tasto della tastiera che lo preme (`"space"` per A). `prompt(nome, [piccolo, scala, giocatore])` senza coordinate non disegna: restituisce larghezza e altezza |
| `camera([x, y])` | sposta tutto il disegno di (−x, −y); senza argomenti la azzera |
| `clip([x, y, w, h])` | limita il disegno al rettangolo; senza argomenti tutto lo schermo |

### Sprite e mappa

Lo **sprite sheet** è un'immagine divisa in celle 8×8: la cella `n` è alla colonna
`n % (larghezza/8)` e riga `n // (larghezza/8)`. Viene da `--sheet foglio.png`
(PNG RGB o RGBA; alfa < 128 = trasparente) oppure, senza PNG, è un foglio 256×256
trasparente da disegnare con `sset` (32 celle per riga: vedi `carts/shooter`).
Lo sheet può arrivare a 4096×4096 pixel. Uno sheet grande con al massimo 256 colori
va messo nella cartuccia con `--sheet foglio.png --sheet8`: palette e sequenze
ripetute (RLE) invece di 4 byte per pixel, decodificati al caricamento (Titan Clash:
2048×3376 pixel in 1,7 MB).

| Funzione | Descrizione |
|---|---|
| `spr(n, x, y, [w, h, flip_x, flip_y])` | disegna la cella `n` (w×h celle, default 1×1), anche specchiata |
| `sspr(sx, sy, sw, sh, dx, dy, [flip_x, flip_y, zoom])` | copia un rettangolo qualsiasi dello sheet; con `zoom` (predefinito 1) lo disegna ingrandito (`2`, `3`…) o rimpicciolito (`0.5`), pixel per pixel: copre `sw * zoom` × `sh * zoom` pixel |
| `sget(x, y)` / `sset(x, y, [c])` | legge / scrive un pixel dello sheet (`nil` = trasparente) |
| `zspr(nome, x, y, [fotogramma, flip_x, flip_y, zoom])` | disegna una **zona con nome** dello sheet (sotto): `fotogramma` da 1 al numero dei fotogrammi (oltre si ricomincia), oppure `nil`: quello che i suoi fps danno a `time()`, cioè l'animazione va da sola. Restituisce il fotogramma disegnato. Una zona che non c'è è un errore |
| `zone(nome)` | `x, y, w, h, fotogrammi, fps` della zona (il primo fotogramma, in pixel dello sheet), o `nil` |
| `zones()` | i nomi delle zone, in ordine (`{}` se non ce ne sono) |
| `zboxes(nome, [fotogramma, tipo])` | le **hitbox e hurtbox** dei fotogrammi di una zona (sezione BOXES, 2026-10-04): una lista di `{x, y, w, h, kind, frame}`, `x` e `y` dall'angolo in alto a sinistra del fotogramma (possono uscirne), `kind` `"hurt"` (dove si viene colpiti), `"hit"` (dove si colpisce), `"body"` (l'ingombro del corpo) o un numero del gioco (3–255), `frame` 0 per un riquadro di tutti i fotogrammi. Con `fotogramma` (da 1; oltre si ricomincia) i suoi e quelli di tutti; senza, tutti quelli della zona. `tipo` (un nome o un numero) ne tiene uno solo. `{}` se non ce ne sono; una zona che non c'è è un errore |
| `map(mx, my, [x, y, mw, mh, livello, maschera])` | disegna la mappa dalla cella (mx, my), mw×mh celle, a (x, y). `livello`: il numero (da 1) o il nome del livello (predefinito 1). `maschera`: solo le celle la cui tile ha almeno uno di quei **flag** (`fget`; 0 o niente: tutte) |
| `mget(mx, my, [livello])` / `mset(mx, my, n, [livello])` | legge / scrive una cella della mappa (0 = vuota; fuori dalla mappa `mget` dà 0) |
| `fget(n, [f])` | i **flag** della tile (cella dello sheet) `n`: un byte, 8 flag; con `f` (0–7) quel flag, `true` o `false` |
| `fset(n, f, acceso)` / `fset(n, byte)` | accende o spegne il flag `f` della tile `n`; con due argomenti scrive tutti gli 8 flag insieme |
| `mflags(x, y, [w, h, livello])` | i flag delle tile che il rettangolo tocca, **in pixel** (la mappa disegnata a 0, 0; senza `w` e `h` un punto), messi insieme (or); 0 = niente. La cella 0 e il fuori mappa non ne hanno. Per le collisioni: `mflags(x, y + h, w, 1) & 1 ~= 0` (il flag 0 sotto i piedi) |
| `msize([w, h])` | `w, h, livelli`: la misura della mappa in celle e quanti livelli ha; con `w` e `h` tutti i livelli prendono quella misura, le celle che ci stanno restano dov'erano |
| `mlayers([lista])` | i nomi dei livelli, in ordine (il primo si disegna dietro). Con una lista (da 1 a 8 voci) la mappa prende quei livelli: una voce è un nome (il livello con quel nome, o uno nuovo vuoto) oppure `{nome, da}` (una copia del livello `da`, numero o nome): così si aggiungono, spostano, rinominano e tolgono |

**La mappa a livelli (R11, 2026-10-04).** La mappa ha da 1 a 8 **livelli** della stessa
misura, con un nome (il primo si chiama `"main"` se non gliene si dà un altro). Si
disegnano uno alla volta, nell'ordine che si vuole: per esempio lo sfondo, poi gli sprite,
poi il livello che passa **davanti** al personaggio (chiome degli alberi, archi, tetti).

```lua
cls(0)
map(0, 0, 0, 0, 80, 45, "back")     -- il pavimento
spr(eroe, x, y)
map(0, 0, 0, 0, 80, 45, "front")    -- le chiome sopra l'eroe
```

**I flag delle tile.** Ogni tile dello sheet ha 8 flag (0–7) che dicono cosa è per il
gioco: muro, acqua, scala, pericolo... Le collisioni guardano i flag invece dei numeri delle
tile. La convenzione di [bmlib](#bmlib-la-libreria-comune-dei-giochi) (ogni gioco può
sceglierne un'altra): flag 0 **solido** (1), 1 **piattaforma** che si attraversa da sotto
(2), 2 **scala** (4), 3 **acqua** (8), 4 **fa male** (16); 5–7 liberi.

```lua
if fget(mget(cx, cy), 0) then ... end            -- la cella è solida
if mflags(px, py + 8, 8, 1) & 1 ~= 0 then        -- il personaggio (8x8) poggia su qualcosa
  a_terra = true
end
map(0, 0, 0, 0, 80, 45, 1, 4)                    -- solo le scale
```

**Le zone con nome.** La sezione SPRITES dà un nome a dei rettangoli dello sheet, con i
loro fotogrammi (le caselle della stessa misura a destra della prima) e la velocità
dell'animazione. Il codice non ha più bisogno delle coordinate:

```lua
zspr("moneta", x, y)                   -- l'animazione della moneta, va da sola
zspr("eroe_corre", x, y, f, a_sinistra)
local _, _, w, h = zone("eroe_corre")  -- la misura, per le collisioni
```

**Hitbox e hurtbox dei fotogrammi.** Sotto una zona, nel file delle zone, i suoi riquadri:
`hurt` dove il personaggio può essere colpito, `hit` dove il suo colpo fa male, `body`
l'ingombro (per non attraversarsi), con il fotogramma (da 1, o `*` per tutti) e il
rettangolo dall'angolo del fotogramma. Il gioco li legge con `zboxes` e li mette nel mondo
(anche specchiati) con [`lib.hits`](#hitbox-e-hurtbox) di bmlib:

```
# nome  x  y  w  h  fotogrammi fps
pugno   0  64 32 32 4 12
  hurt  *  8  2 16 30        # il corpo, in tutti i fotogrammi
  hit   3  24 10 10  6       # il pugno, solo nel fotogramma 3
```

```lua
for _, b in ipairs(zboxes("pugno", f, "hit")) do
  rect(x + b.x, y + b.y, b.w, b.h, 0xFF4040)     -- per vederli mentre si fa il gioco
end
```

**Da dove vengono.** Con `mkbm.py`: `--map mappa.csv` è il primo livello; ogni altro
`--map nome=file.csv` è un livello in più (fino a 8, nell'ordine dato; un CSV più piccolo
del primo si riempie di celle vuote); `--flags flag.csv` i flag (numeri 0–255 per le celle
0, 1, 2... in ordine, oppure `n=flag` per la cella `n`); `--sprites zone.txt` le zone (una
per riga: `nome x y w h [fotogrammi [fps]]`, sotto una zona i suoi riquadri: `hurt`, `hit`
o `body fotogramma x y w h`, oppure `box tipo fotogramma x y w h` per un tipo del gioco; i
nomi `hurt`, `hit`, `body` e `box` non sono zone). Nella build del repository bastano i file
`map_<nome>.csv` (in ordine con `layers_<gioco> := nome ...` nel `Makefile`), `flags.csv` e
`sprites.txt` nella cartella del gioco. Sulla console nella pagina della mappa dell'SDK `l`
passa al livello dopo, Shift+L ne aggiunge uno (anche dal menu, *New map layer*), `o`
mostra solo quello, `c` i flag delle tile; nella pagina degli sprite i tasti `0`–`7`
accendono i flag della cella; bm Pixel e `scripts/bmres.py` fanno le zone (`bmres.py`
porta i riquadri con le loro zone). `cart_save` e `cart_load` portano livelli, flag, zone
e riquadri con la cartuccia. Formato: sezioni LAYERS (12), FLAGS (13), SPRITES (11) e
BOXES (14) in `src/bm/bm.h`; un kernel di prima disegna il primo livello e ignora il resto.

La mappa viene da `--map mappa.csv` (una riga di numeri separati da virgole per riga
della mappa; ogni numero è una cella dello sheet). Senza mappa è di 256×256 celle vuote,
un livello solo.

### Input

| Funzione | Descrizione |
|---|---|
| `btn(i, [p])` | `true` finché il tasto è premuto; senza `p` da **qualsiasi** controller, con `p` = 1–4 solo da quello del giocatore `p`. Al posto di `i` anche un **nome** (2026-10-04): un pulsante (`"a"`, `"b"`, `"x"`, `"y"`, `"left"`…`"down"`, `"start"`, `"select"`, `"l1"`, `"r1"`, `"l2"`, `"r2"`, `"l3"`, `"r3"`), `"ok"` e `"back"` (il sì e l'indietro del sistema: croce e cerchio sul DS4, A e B su un pad Xbox, Spazio e X sulla tastiera; sulla RGB30 come dicono `confirm=` e `game_buttons=`) o un'azione di `keymap()` |
| `btnp(i, [p])` | `true` solo nel fotogramma in cui viene premuto (stesso `p`, anche con un nome) |
| `keymap(t)` | le **azioni** del gioco sui pulsanti: `keymap({ salta = "a", spara = {"x", "r1"}, pausa = "start", conferma = "ok" })` (fino a 32 azioni, 4 pulsanti ciascuna); poi `btn("salta", p)`, `btnp("spara")` e `prompt("salta", x, y)`. Il gioco cambia i tasti chiamandola di nuovo (il suo menu delle opzioni; li tiene con `save()`); `keymap()` restituisce la tabella, `keymap(nil)` la toglie. Un pulsante sconosciuto o un'azione col nome di un pulsante è un errore |
| `controller([p])` | con che cosa gioca il giocatore `p` (1–4, il primo se manca): `{kind = "keyboard" / "ds4" / "xbox" / "pad" / "builtin" / "none", layout = "keyboard" / "ds4" / "xbox" / "nintendo" / "none", bluetooth = bool, ok = "a" / "b", back = "b" / "a"}`: `layout` dice come si chiamano i tasti (simboli del DS4; lettere con A in basso come Xbox; A a destra come la RGB30), `ok` e `back` quali pulsanti del gioco sono il sì e l'indietro, `color` il colore del giocatore (quello della luce del suo pad: 1 blu, 2 rosso, 3 verde, 4 rosa) |
| `online([on, nota])` | la partita si gioca in **rete** (2026-10-04): con `online(true)` PS, Ctrl+Esc e Start+Select non sospendono il gioco (gli altri continuano a giocare) ma chiedono al giocatore che esce, solo sulla sua console, "Leave the match?" (uscirà dal gioco e si disconnetterà dal server), sopra il gioco che va avanti. Finché la domanda è aperta il gioco non vede né pulsanti né levette né tasti; ok (croce sul DS4, Invio o Spazio sulla tastiera) o PS di nuovo escono: `_leave()` e la cartuccia si chiude; indietro (cerchio, Esc) resta, e i pulsanti ancora premuti tornano al gioco solo dopo essere stati lasciati. `nota`: una riga sotto la domanda (es. `"You are the host: the match ends for all."`). `online(false)` alla fine della partita; `online()` restituisce se era in rete e se la domanda è aperta (il giocatore è via) |
| `players()` | quanti giocatori hanno un controller (almeno 1) e, come secondo valore, quali: bit `n` = giocatore `n+1` (es. `3, 7` = giocatori 1, 2 e 3) |
| `stick([p, n])` | la levetta sinistra del giocatore `p`: `x, y` tra −1 e 1 (x verso destra, y verso il basso), con zona morta; con la tastiera o un pad senza levetta vale la croce (8 direzioni). Con `n = 1` la levetta **destra** (per mirare negli sparatutto; `0, 0` senza levetta). Senza `p`: quella spinta di più |

**Mouse e puntatore (M32).** Una cartuccia ha il puntatore solo se lo chiede: senza
`mouse(true)` non c'è (nel menu di bm invece c'è sempre). Lo muove solo un mouse USB o
Bluetooth: un controller non lo fa mai comparire (la sua levetta destra è `stick(p, 1)`); la
console può averlo spento per tutto il sistema (`mouse=off` in `bm/config.txt`).

| Funzione | Descrizione |
|---|---|
| `mouse(on, [freccia])` | `mouse(true)`: la cartuccia vuole il puntatore, e la console disegna la sua freccia sopra il fotogramma (`mouse(true, false)`: niente freccia, la cartuccia disegna il suo cursore); `mouse(false)` lo toglie. Restituisce `false` se la console ha il mouse spento |
| `mouse()` | `x, y, tasti, rotellina, visibile`: la posizione in pixel dello schermo della cartuccia (senza `camera`), i tasti tenuti in bit (1 sinistro, 2 destro, 4 centrale), gli scatti della rotellina in questo fotogramma (in su positivi) e `true` se il puntatore si vede (appena qualcosa lo muove). `nil` se la cartuccia non l'ha chiesto o se non c'è niente che lo muova (né mouse né levetta destra) |
| `mousep([i])` | `true` nel fotogramma in cui il tasto `i` viene premuto (0 sinistro, il default; 1 destro; 2 centrale) |

```lua
function _init() mouse(true) end
function _update()
  local x, y = mouse()
  if x and mousep() then sfx(0) end   -- un clic
end
```

**bmui, il mouse degli strumenti di bm** (`require "bmui"`, `src/script/bmui.lua`, 2026-10-06):
lo usano gli editor (SDK, bm Code, bm Studio, bm Animator, bm Mesh, bm Pixel, Sound, il pannello
dell'assistente) e può usarlo anche un gioco o uno strumento tuo. La regola: il mouse fa quello
che fanno i tasti. Le cose diventano cliccabili mentre si disegnano, e un clic preme il tasto che
c'è dietro o sceglie quello che mostrano; il tasto destro apre un menu contestuale le cui voci
sono tasti.

| Funzione | Descrizione |
|---|---|
| `U.update()` / `U.begin()` | in `_update` per prima (legge `mouse()` una volta a fotogramma), in `_draw` per prima (dimentica le zone del fotogramma prima) |
| `U.zone(x, y, l, a, tipo, a, b)` | qualcosa di cliccabile, mentre si disegna (vince l'ultima disegnata: un dialogo sopra la pagina); `U.at()` la zona sotto il puntatore, `U.click([b])` la zona premuta in questo fotogramma (0 sinistro, 1 destro, 2 centrale), `U.clicked(b)` un tasto lasciato senza trascinare (la zona dove era sceso, o `true`) |
| `U.key_zone(x, y, l, a, tasti)` / `U.keys()` | un chip (`"ctrl s"`, `{"shift", "l"}`, `"f5"`) che premuto preme il suo tasto; `U.keys()` lo fa per il clic del fotogramma |
| `U.press(tasto)` | un tasto come se fosse scritto: il prossimo `keyp()` lo dà (bmui mette il suo `keyp` per primo) |
| `U.menu(voci, [x, y])` / `U.menu_update()` / `U.menu_draw()` | il menu contestuale al puntatore: `{ {"Copy", "^c"}, "-", {"Run", function() ... end} }` (il tasto scritto a destra, "Ctrl+C"); `U.menu_update()` in `_update` (true: ha preso l'input), `U.menu_draw()` per ultimo in `_draw`; righe sulla griglia 8x16 |
| `U.x`, `U.y`, `U.on`, `U.moved`, `U.wheel`, `U.double`, `U.drag` | il puntatore (`U.on`: un mouse l'ha mosso e si vede), se si è mosso, gli scatti della rotella del fotogramma, un doppio clic, il trascinamento `{b, x0, y0, dx, dy}` (oltre 3 pixel; `dx, dy` lo spostamento del fotogramma); `U.pressed(b)`, `U.released(b)`, `U.down(b)`, `U.shift()`, `U.drag_arrows(passo)` (un trascinamento come frecce) |

Negli editor: un clic sceglie una riga, un doppio clic (o nei dialoghi un clic su quella scelta)
la prende, la rotella si muove o cambia un valore; nelle viste 3D il tasto destro trascinato
gira, il centrale sposta, la rotella avvicina; sulle tele il sinistro disegna e il centrale
preleva il colore; in bm Code un clic mette il cursore, un trascinamento seleziona, un doppio
clic prende la parola, un clic sui numeri è un breakpoint.

**Più giocatori (M16).** Il controller Bluetooth *n* è il giocatore *n* (abbinati dal monitor
con `T`, uno alla volta: ognuno prende il primo posto libero e la sua luce il colore del
giocatore: 1 blu, 2 rosso, 3 verde, 4 rosa). La tastiera o il gamepad USB e la seriale sono
il primo giocatore senza pad (senza pad Bluetooth: il giocatore 1); la tastiera Bluetooth
(M28) è un giocatore a sé, il successivo senza pad (il primo, se non c'è niente di USB).
I giochi a un giocatore
usano `btn(i)` senza `p` e funzionano con qualsiasi controller; un gioco a più giocatori
chiede `btn(i, p)` per ciascuno (esempio: `carts/pong`, modalità 2 giocatori). bmlib ha la
schermata dove i giocatori entrano (`lib.party`), lo schermo diviso (`lib.split`) e i
colori (`lib.PLAYER_COLORS`): [Più giocatori sulla stessa console](#più-giocatori-sulla-stessa-console);
il modello *Versus 2D* dell'SDK li usa.

| `i` | Tastiera | Gamepad | Seriale |
|---|---|---|---|
| 0 sinistra | ← o A | croce / levetta | `a` o ← |
| 1 destra | → o D | croce / levetta | `d` o → |
| 2 su | ↑ o W | croce / levetta | `w` o ↑ |
| 3 giù | ↓ o S | croce / levetta | `s` o ↓ |
| 4 **A** | spazio, Z, J | A / croce (DS4) | spazio, `j` |
| 5 **B** | X, K | B / cerchio | `x`, `k` |
| 6 **X** | C, L | X / quadrato | `c`, `l` |
| 7 **Y** | V, I | Y / triangolo | `v`, `i` |
| 8 **Start** | Invio | Start / Options | Invio |
| 9 **Select** | Tab | Select / Share | — |

Start+Select insieme (o il tasto PS) chiudono sempre la cartuccia: Start da solo è libero
per la pausa del gioco.

Una cartuccia che non chiede mai `btn(6)`/`btn(7)` (o `btnp`) riceve X come A e Y come B:
i giochi con due tasti funzionano con tutti e quattro.

**Tasti a scelta del giocatore.** Una cartuccia che vuole far scegliere i tasti (come
nano8) legge la tastiera tasto per tasto e i controller pulsante per pulsante:

| Funzione | Descrizione |
|---|---|
| `rawkeys(on)` | con `true` le tastiere smettono di fare da controller per `btn()` e `pad()`: si leggono con `keydown()`. Ctrl+Esc chiude comunque la cartuccia |
| `keydown(u)` | `true` finché è premuto il tasto con l'usage USB HID `u` (USB o Bluetooth): `0x04`…`0x1D` le lettere A–Z, `0x1E`…`0x27` le cifre, `0x28` Invio, `0x2C` spazio, `0x4F`…`0x52` le frecce (destra, sinistra, giù, su), `0xE0`…`0xE7` Ctrl, Shift, Alt, GUI di sinistra e poi di destra |
| `keys()` | gli usage dei tasti premuti adesso (`{0x1D, 0xE1}`): per "premi un tasto" |
| `pad([p])` | i pulsanti che il giocatore `p` (1–4) tiene premuti, in bit: 1 sinistra, 2 destra, 4 su, 8 giù, 16 A, 32 B, 64 Start, 128 Select, 256 X, 512 Y, 1024 L1, 2048 R1, 4096 L2, 8192 R2, 16384 L3, 32768 R3 (i grilletti e le levette premute: DS4 e Xbox 360; sui pad generici i pulsanti 7–8 e 11–12); senza `p` quelli di tutti. I tasti della seriale contano come il controller del primo giocatore (L1 e R1: `u` e `o` dalla seriale, Q ed E dalla tastiera USB; Select: Tab da entrambe) |
| `lastinput()` | che cosa è stato premuto per ultimo: `"keyboard"`, `"ds4"` o `"pad"` (un altro controller); `nil` prima di ogni tasto. Serve a mostrare i tasti giusti con `prompt()` (per esempio `"enter"` o `"A"`) |

### Tempo e sistema

| Funzione | Descrizione |
|---|---|
| `time()` | secondi dall'avvio della cartuccia (con decimali) |
| `stat(n)` | 0 KiB usati da Lua, 1 ms dell'ultimo fotogramma (`_update` + `_draw`, con il 3D della GPU), 2 fps, 3 numero del fotogramma, 4 triangoli 3D, 5 pixel 3D (0 con la GPU), 6 ms passati nel disegno 3D (da `zclear`; con la GPU la parte dell'ARM), 7 vertici 3D trasformati, 8 ms dall'inizio di questo fotogramma (per misurare le fasi), 9 `1` se il 3D lo disegna la GPU, 10 istruzioni Lua dell'ultimo fotogramma (`_update` + `_draw`, alle migliaia); il **dev kit** (2026-10-04): 11 token del codice della cartuccia (`code_tokens`), 12 i KiB di Lua più alti di questa partita, 13 KiB dei dati della cartuccia in memoria (sprite sheet, mappa, modelli e scheletri, banco di suoni, z-buffer del 3D), 14 le istruzioni Lua del fotogramma più pesante di questa partita; 15 quanti `_update` sono girati prima di questo `_draw` (1; di più con `frameskip`) |
| `frameskip([n])` | il tempo del gioco a 60 `_update` al secondo qualunque sia il costo di `_draw` (2026-10-05): quando un fotogramma dura più di 1/60 s, prima del `_draw` dopo girano fino a `n` `_update` (i fotogrammi in mezzo non si disegnano), così un gioco che avanza di 1/60 s a ogni `_update` non rallenta; oltre `n` il tempo si lascia andare (il gioco rallenta piuttosto che non disegnare mai). `1` (il default) è un `_update` per fotogramma, come prima; al massimo 8. Restituisce il valore di prima. Un tasto premuto conta una volta in `btnp()` (e `mousep()`, la rotella) per quanti `_update` lo vedano; `btn()` resta tenuto. Overbit usa `frameskip(4)` (il suo benchmark `1`) |
| `devkit([modo])` | l'overlay del dev kit: `0` spento, `1` semplice, `2` dettagliato, `3` funzioni; con un modo mostra quella pagina (un tasto del gioco per lui, per esempio Select sul pad: F11 è della tastiera), solo in questa partita: ogni gioco parte come dice Settings. Restituisce il modo di prima |
| `profile([acceso])` | il profiler delle funzioni (R14): le funzioni che costano di più nell'ultimo secondo intero (60 fotogrammi), la più cara per prima, fino a 32: `{name, where, c, self, total, calls}` (`where` `"main.lua:120"` o `"[C]"`; `c` vero per le funzioni della console e di Lua scritte in C; `self` e `total` ms per fotogramma, il suo tempo e con quelle che chiama; `calls` le chiamate per fotogramma, solo per quelle in C), e i fotogrammi misurati (0 durante il primo secondo). `profile(true)` lo accende per la partita anche senza la pagina del dev kit, `profile(false)` lo spegne (e dimentica). Es. `for _, f in ipairs(profile()) do log(f.name, f.self) end` |
| `devinfo(riga, ...)` | fino a 4 righe del gioco nella pagina dettagliata del dev kit (la sua qualità, i suoi attori...), di 18 caratteri; `devinfo()` nessuna. Va chiamata di nuovo quando cambiano (Overbit a ogni fotogramma mentre la pagina dettagliata è aperta) |
| `battery()` | la batteria della console (2026-10-10): la carica `0`–`100` e `true` sul caricatore (`local pct, plugged = battery()`); `nil` senza batteria (il Pi). Sulla RGB30 la carica viene dalla tensione (un'indicazione: sotto sforzo scende, sul caricatore sale); il caricatore si vede entro un quarto di secondo |
| `battery_low()` | `true` finché la batteria è scarica: 20% o meno fuori dal caricatore (fino al 23%). Allora il sistema disegna da sé una piccola batteria rossa in alto a destra sopra il fotogramma (a sinistra dell'overlay del dev kit), in ogni gioco e strumento; *Settings > Screen and sound > Low battery icon* (`battery_icon=0` in `bm/config.txt`) spegne l'icona, non `battery_low()`. Solo informazione: ogni console ha la sua batteria, quindi in lockstep (Overbit, `bmnet`) non cambia mai ciò che il gioco simula; serve a mostrare qualcosa o a salvare |
| `code_tokens(testo)` | i **token** di un pezzo di codice Lua, contati come `stat(11)`, l'overlay e il dev kit dell'SDK (`src/bm/tokens.c`): ogni nome, parola chiave, numero, stringa e operatore vale uno; commenti, spazi, `,` `.` `:` `;` `::`, le parentesi che si chiudono (`)` `]` `}`), `end` e `local` non contano, e nemmeno il segno meno davanti a un numero (`-1` è un token). Un'informazione, non un limite: bm non mette un tetto ai token (e nemmeno il `.b16`, [B16.md](B16.md) §2.4) |
| `log(...)` | scrive nel log del kernel (seriale e console), non sullo schermo del gioco |
| `report(tipo, testo)` | un report per chi sviluppa bm (2026-10-04): salvato in `bm/reports` sulla SD con kernel, branch, scheda e data, poi inviato al repository dei report se c'è `github_token` e la rete (`src/kernel/reports.h`); al più 8 per partita, 256 KiB l'uno; `true` se salvato. La prima volta che un gioco la chiama la console lo chiede al giocatore (la risposta resta in `bm/config.txt`, `allow_...`): dopo un no, `false` |
| `quit()` | chiude la cartuccia alla fine del fotogramma |
| `timeslice(co, [k])` | la coroutine `co` si ferma da sola dopo circa `k` mila istruzioni Lua in un fotogramma (400 se manca) e `coroutine.resume` torna `true` senza valori: un calcolo lungo prosegue nei fotogrammi successivi invece di fermare la cartuccia per il limite di istruzioni. `timeslice(nil)` lo toglie (nano8 lo usa per le sue cartucce) |

Numeri casuali: `math.random`. Per partite diverse a ogni avvio, inizializza il
generatore quando il giocatore preme un tasto: `math.randomseed(stat(3))`.

### Rete (UDP)

Per i giochi in rete (M38.5: Overbit). Per un gioco conviene la libreria
[bmnet](#bmnet-i-giochi-in-rete) (lobby, messaggi sicuri, lockstep) che usa queste
funzioni. Pacchetti UDP fino a 1024 byte; ogni cartuccia
ha 2 socket, chiusi quando finisce. Gli indirizzi sono testo (`"192.168.1.23"`); `"*"` è
il broadcast della LAN. Serve la rete della console (WiFi o cavo): senza, `udp_open`
restituisce `nil`. In bmhost gli stessi pacchetti passano dai socket del PC
(`BMHOST_NET_ID=k` per più console sullo stesso PC, `--realtime` per giocare a 60 frame
al secondo).

| Funzione | Cosa fa |
|---|---|
| `s, porta = udp_open([porta])` | un socket sulla porta (0 o niente: una qualsiasi); `nil` e il motivo se non c'è rete o socket libero. La prima volta che un gioco ne apre uno, la console chiede al giocatore se può usare la rete (la risposta resta in `bm/config.txt`); dopo un no, `nil` e il motivo |
| `udp_send(s, indirizzo, porta, dati)` | manda una stringa (al massimo 1024 byte); `true` se è partita (UDP: può perdersi) |
| `dati, indirizzo, porta = udp_recv(s)` | il prossimo pacchetto arrivato, o `nil`; ne restano in coda fino a 48 |
| `udp_close(s)` | chiude il socket |
| `net_ip()` | l'indirizzo della console, o `nil` senza rete |
| `net_resolve(nome)` | l'indirizzo di un nome: `nil` mentre lo cerca (richiamarla al frame dopo), `false` se non esiste |

### Salvataggi

| Funzione | Descrizione |
|---|---|
| `save(t, [slot])` | salva la tabella `t` sulla SD, nello slot 1–8 (predefinito 1); `true`, oppure `false` e il motivo (niente SD, scheda piena...) |
| `saved([slot])` | la tabella salvata l'ultima volta in quello slot (predefinito 1), oppure `nil` |
| `saves()` | gli slot usati, `{[slot] = byte}` (es. `{[1] = 40, [3] = 212}`), e quanti ce ne sono (8): per la pagina "carica partita" |
| `delsave([slot])` | svuota lo slot (predefinito 1); `true`, oppure `false` e il motivo (`"nothing saved in slot 3"`) |

Ogni cartuccia ha **8 slot** di salvataggio, ognuno una tabella fino a 32 KiB, in
`/bm/save/` sulla SD: lo slot 1 è `XXXXXXXX.SAV` (quello di sempre: `save(t)` e `saved()`
senza slot), gli altri `XXXXXXXX.S02` ... `.S08`. Il nome dipende da titolo e autore:
cambiandoli si riparte da zero. Un uso comune: lo slot 1 per impostazioni e record (lo usa
anche `lib.store`), gli altri per le partite. Una tabella può contenere numeri, stringhe,
booleani e altre tabelle (niente funzioni). Le opzioni del gioco nel menu (X sulla
copertina) mostrano i byte di tutti gli slot e *Delete the save data* li cancella tutti.
Scrivere sulla SD richiede qualche millisecondo: chiama `save()` in momenti come la fine
della partita, non a ogni fotogramma (`lib.store` e `lib.best` di [bmlib](#bmlib-la-libreria-comune-dei-giochi)
scrivono solo quando un valore cambia). Esempio (record di Snake):

```lua
function _init()
  local data = saved()
  if data then best = data.best end
end
-- a fine partita
if score > best then best = score; save({ best = best }) end
```

Tre partite, con la pagina per scegliere (gli slot 2, 3 e 4):

```lua
local function slots()                  -- le righe della pagina "carica partita"
  local used, rows = saves(), {}
  for i = 1, 3 do
    local p = used[i + 1] and saved(i + 1)
    rows[i] = p and ("Partita " .. i .. ": livello " .. p.level) or ("Partita " .. i .. ": vuota")
  end
  return rows
end
function save_game(i) save({ level = level, hp = hp }, i + 1) end
function load_game(i) local p = saved(i + 1) if p then level, hp = p.level, p.hp end end
```

### Documenti

I file del giocatore, comuni a tutte le app: `/docs` sulla SD (bm Write ci tiene i suoi
`.BMD`, con le esportazioni `.TXT`, `.MD`, `.HTM` e `.PDF`). Nomi 8.3 senza cartelle
(`"LETTER.BMD"`; vanno bene le minuscole, il file si scrive in maiuscolo); la cartella la
crea la prima scrittura. La prima volta che un'app li usa la console chiede al giocatore,
come per la rete; la risposta resta in `bm/config.txt` (`allow_...=docs=yes`), e dopo un no
ogni chiamata restituisce `nil` e il motivo.

| Funzione | Descrizione |
|---|---|
| `doc_list()` | i file di `/docs`: `{ {name = "LETTER.BMD", size = 1234}, ... }` (vuota se non ce ne sono); `nil` e il motivo se il giocatore non l'ha permesso |
| `doc_read(nome)` | i byte di un documento (una stringa), oppure `nil` e il motivo |
| `doc_write(nome, byte)` | scrive un documento, nuovo o sostituito (al massimo 4 MiB); `true`, oppure `nil` e il motivo |
| `doc_delete(nome)` | cancella un documento; `true`, oppure `nil` e il motivo |

```lua
local ok, why = doc_write("NOTES.TXT", "spesa:\npane\nlatte\n")
if not ok then msg = why end
for _, d in ipairs(doc_list() or {}) do print(d.name .. " " .. d.size) end
```

### Suono

L'audio esce in **stereo** a 48 kHz (sul Pi dall'HDMI, gli altoparlanti del monitor; sulla
RGB30 dalle cuffie o dall'altoparlante) ed è generato in un interrupt: non costa nulla al
tuo `_update`. Il sintetizzatore è **hi-fi** (2026-10-06): onde senza aliasing, inviluppi
esponenziali, filtro risonante, ambiente (riverbero) ed eco, un compressore leggero
sull'uscita; la voce **8-bit** di prima resta (`retro(true)`, l'opzione `raw` di `tone()`,
o *Settings > Screen and sound > Sound style*). Ci sono due modi di usarlo, anche insieme:

- **il banco di suoni** della cartuccia (effetti sonori e musica fatti con il Sound
  editor della scheda Dev): `sfx(n)` e `music(n)`;
- **le note** suonate dal codice, una voce alla volta: `note`, `slide`, `arp`...

Otto voci (0–7). Forme d'onda: `SQUARE` (quadra, con `duty`), `TRIANGLE`, `SAW` (dente
di sega), `NOISE` (rumore), `SINE` (seno), `METAL` (rumore corto e metallico: piatti,
campanelli), `FM` (due seni, uno piega l'altro: piano elettrico, campane, bassi, ottoni),
`PLUCK` (una corda pizzicata: chitarre, arpe), `SUPERSAW` (tre denti di sega un po'
stonati: pad, lead larghi), `ORGAN` (quattro armoniche: organi, flauti), `SAMPLE` (un
campione registrato: del banco o uno degli otto tamburi della console, sotto), `PINK`,
`BROWN` (rumori più morbidi: vento, mare, fruscio) e `CRACKLE` (scatti a caso: un vecchio
disco, un fuoco). Ogni voce ha un inviluppo ADSR, un filtro, un posto tra sinistra e destra,
quanto va nell'ambiente, nell'eco e nel chorus, e gli effetti del superdough di Strudel (bit
crush, una frequenza più bassa, vocali, curve di saturazione, tremolo, ducking: `tone()`).
Le altezze sono in **Hz** (anche con la
virgola: `261.63`) oppure un **nome di nota**: `"C4"` (do centrale), `"A4"` (440 Hz),
`"F#3"`, `"Bb2"`.

#### Effetti sonori e musica (il banco della cartuccia)

| Funzione | Descrizione |
|---|---|
| `sfx(n, [v], [semitoni], [vol])` | suona l'effetto sonoro `n` del banco; senza `v` sceglie una voce libera, preferendo quelle che la musica lascia vuote. `semitoni` lo traspone (`sfx(0, nil, 12)`: un'ottava sopra), `vol` 0–1. Restituisce la voce, o `nil` (nessun banco, numero che non c'è) |
| `sfx(-1, [v])` | ferma l'effetto della voce `v`, o tutti |
| `sfxpos(v)` | l'effetto che suona sulla voce `v` e il suo passo, o `nil` |
| `music(n, [fade_ms], [pos])` | suona il brano `n` dall'inizio (o dalla posizione `pos` della sua sequenza), con una dissolvenza in entrata di `fade_ms` |
| `music(-1, [fade_ms])` | ferma la musica, sfumandola |
| `music()` | mentre suona: brano, posizione, passo, pattern (per andare a tempo); altrimenti `nil` |
| `tempo(x)` | la musica va `x` volte più veloce (1 = come scritta): accelera quando il gioco si fa difficile |
| `mute(traccia, [on])` | spegne (`on`, il predefinito) o riaccende una traccia della musica: strati che entrano e escono |
| `volume([livello])` | il volume generale 0–10 (con `livello` lo cambia). È della console: vale per tutti i giochi e resta in `bm/config.txt` |

La traccia `t` di un pattern suona sulla voce `t`. Mentre un effetto sonoro usa una voce,
la traccia della musica su quella voce tace. Un banco con un brano che usa le tracce 0–5
lascia libere per gli effetti le voci 6 e 7.

```lua
function _init()
  music(0)                         -- il brano 0, in loop come deciso nell'editor
end
function _update()
  if btnp(4) then sfx(1) end       -- salto
  if preso_moneta then sfx(0) end
  if boss then tempo(1.2) end      -- più veloce
  if btnp(8) then music(-1, 500) end
end
```

#### Note dal codice

| Funzione | Descrizione |
|---|---|
| `note(v, hz, [ms], [forma], [vol])` | suona una nota sulla voce `v` (riparte l'inviluppo); con `ms` si spegne da sola, senza resta accesa fino a `noteoff(v)`. `hz` in Hz o un nome (`"C4"`). `vol` 0–255 (predefinito 128) |
| `noteoff(v)` | rilascia la nota (parte la fase di release) |
| `freq(v, hz)` | cambia l'altezza senza ripartire |
| `slide(v, hz, [ms])` | la nota scivola fino a `hz` in `ms` (predefinito 100): glissandi, sirene, laser |
| `vibrato(v, [semitoni], [hz])` | vibrato largo `semitoni` (per esempio 0.3) a `hz` oscillazioni al secondo (predefinito 6); `vibrato(v)` lo toglie |
| `arp(v, accordo, [ms])` | la nota percorre un accordo, `ms` per nota (predefinito 50): `"major"`, `"minor"`, `"maj7"`, `"min7"`, `"7"`, `"sus2"`, `"sus4"`, `"dim"`, `"aug"`, `"power"`, `"octave"`, oppure una tabella di semitoni (`{0, 4, 7, 12}`); `arp(v)` lo toglie |
| `hz(nota)` | la frequenza di una nota: un numero MIDI (60 = do centrale, 69 = la 440) o un nome (`hz("A4")` = 440) |
| `envelope(v, a, d, s, r)` | inviluppo della voce: attack, decay e release sono tempi 0–255 (0 = istantaneo, 255 = 2 s), sustain è un livello 0–255. Predefinito `1, 0, 255, 10` |
| `duty(v, d)` | larghezza dell'onda quadra, 0–255 (128 = 50%; 32–64 suona più "nasale") |
| `playing(v)` | `true` finché la voce suona (release compreso) o un effetto / la musica la tiene |
| `apu(v, reg, [valore])` | legge o scrive un registro grezzo della voce (32 byte per voce, 0–31: `src/audio/synth.h`; il registro 10 sono i 1/256 di Hz, dall'11 il timbro: filtro, posto, ambiente, eco, LFO) |

Forma e volume restano quelli dell'ultima nota della voce, quindi basta darli una volta.
All'avvio e all'uscita della cartuccia le voci si spengono e tornano ai valori
predefiniti. Più voci forti insieme si sommano: un limitatore le tiene sotto il massimo,
ma tieni i volumi intorno a 100–130. Esempi:

```lua
note(0, 880, 60, SQUARE, 100)          -- "blip" di 60 ms
note(0, "C5", 60, SQUARE, 100)         -- lo stesso con il nome della nota
envelope(2, 0, 60, 0, 30)              -- esplosione che si smorza...
note(2, 2500, 300, NOISE, 130)         -- ...con il rumore
note(1, 1300, 300, SQUARE, 70)         -- laser: parte alto...
slide(1, 300, 250)                     -- ...e scende
note(3, "C4", 600, SQUARE, 90)         -- un accordo maggiore arpeggiato
arp(3, "major", 40)
```

Pong, Snake e Star Shooter in `carts/` usano le note (una funzione `jingle` di 10 righe
per le melodie; ora c'è `lib.jingle` in bmlib); il progetto dimostrativo del Sound editor
(`carts/sound/demo.json`) ha effetti sonori e tre brani da ascoltare e copiare (HIFI usa le
voci nuove).

#### Strumenti, timbro, ambiente

| Funzione | Descrizione |
|---|---|
| `tone(v, suono)` | il timbro della voce per le `note()` che seguono: il nome di uno **strumento pronto** (`"epiano"`, `"pluck"`, `"pad"`, `"kick"`… `instruments()` li elenca) o una tabella in unità semplici (sotto). `tone(v)` torna alla quadra di una voce nuova |
| `play([v], suono, [nota], [ms], [vol])` | suona uno strumento come lo suona la musica, con il suo inviluppo dell'altezza e il vibrato (la cassa che scende, il laser): un nome, una tabella come quella di `tone()` (più `pitch` semitoni da cui partire, `ptime` ms per arrivare, `vib` cent, `vibhz`, `detune` cent) o un suono del banco (un numero; un nome è prima un suono del banco con quel nome, poi uno strumento pronto, poi un tamburo del kit: `"bd"`, `"sd"`, `"hh"`, `"oh"`, `"cp"`, `"cb"`). `"NOME:N"` è un suono di campioni sul suo N-esimo campione dopo il proprio (`"kit:2"` il terzo tamburo del kit, il charleston; per un suono del banco i campioni del banco; ricomincia dal primo). `nota`: numero MIDI o nome (`"C4"`, predefinito 60), `ms` 0 = tenuta fino a `noteoff`, `vol` 0–1; senza `v` una voce libera. Restituisce la voce |
| `instruments([tipo])` | gli strumenti pronti: `{ {name=, kind=, about=}, … }`; `tipo`: `"drum"`, `"bass"`, `"keys"`, `"pad"`, `"pluck"`, `"lead"`, `"fx"` |
| `instrument(nome)` | uno strumento con tutti i suoi valori, come bm Sound tiene un suono (`wave`, `a`, `d`, `s`, `r`, `pitch`, `tone` = i 21 byte del timbro…), o `nil`; anche un tamburo del kit (`"sd"`) o `"kit:3"` |
| `reverb([grandezza], [smorzo], [livello])` | l'ambiente dove suonano le voci (0–1: da una stanza piccola a una sala, da brillante a sordo, quanto si sente); restituisce i tre valori. Ogni voce ci manda quanto dice il suo `reverb` (predefinito poco: 0.16) |
| `echo([ms], [ritorno], [livello])` | l'eco a ping-pong (sinistra, destra): il tempo tra le ripetizioni (al più 680 ms), quanto torna (0–0.95), quanto si sente; restituisce i tre valori. Ci va quanto dice l'`echo` di ogni voce |
| `chorus([velocità_hz], [profondità_ms], [livello])` | il chorus (due ritardi che oscillano, sinistra e destra: larghezza per pad e tastiere): quanto veloce oscilla (0.05–5 Hz, predefinito 0.8), quanto (0–8 ms, predefinito 2.5), quanto si sente (0–1, predefinito 1); restituisce i tre valori. Ci va quanto dice il `chorus` di ogni voce |
| `audio_depth()` | `bit, uscita`: i bit di ogni campione che la console manda (16, 24 o 32: `sound_depth` in `bm/config.txt`, *Settings > Screen and sound > Bit depth*, 24 se non detto) e quelli che l'uscita chiede ora (0: nessuna). Un gioco la legge, non la cambia |
| `audio_samples(["kit"])` | i campioni del banco della cartuccia (la voce `n + 1` è quella che suona `sample = n`) o, con `"kit"`, gli otto tamburi del kit: `{ {name=, frames=, rate=, channels=, root=, fine=, loop=, loop_start=, loop_end=, peak=, meter=}, … }`. `rate` Hz, `root` nota MIDI, `fine` cent, `loop` 0 nessuno, 1 in avanti, 2 ping-pong (`loop_start`, `loop_end` in frame), `peak` 0–1 del massimo, `meter` 32 picchi 0–1 dall'inizio alla fine |
| `retro([on])` | tutte le voci **8-bit** come nelle prime versioni (onde ingenue, inviluppi dritti, niente ambiente né eco) finché il gioco gira; restituisce se lo sono (anche le Settings lo possono chiedere) |
| `audio_time()` | l'orologio del suono in secondi (va avanti con i campioni suonati) |
| `play_at(t, suono, [nota], [ms], [vol], [tag])` | una nota al tempo `t` di `audio_time()`: la fa partire l'interrupt del suono, entro 1,3 ms, qualunque sia il frame rate. `suono` come quello di `play()`, `ms` quanto è tenuta (predefinito 250), `vol` 0–1, `tag` 1–255 (un gruppo per `play_cancel`, predefinito 1). La voce si sceglie quando parte (una libera, una coda, la nota più vecchia di `play_at`; mai quelle di un brano o di un effetto). `true`, o `false` se la coda (160 note) è piena. È quello che usa riff |
| `play_cancel([tag])` | dimentica le note di `play_at` che aspettano con quel `tag` (senza: tutte) e rilascia quelle che suonano |
| `play_voices([v, …])` | le voci che `play_at` può prendere (senza: tutte e 8); restituisce quante note aspettano |

Le chiavi di `tone()` (tutte facoltative; quelle che mancano restano come sono):

| Chiave | Valore |
|---|---|
| `preset` | uno strumento pronto da cui partire |
| `wave` | `"square"`, `"triangle"`, `"saw"`, `"noise"`, `"sine"`, `"metal"`, `"fm"`, `"pluck"`, `"supersaw"`, `"organ"`, `"sample"`, `"pink"`, `"brown"`, `"crackle"` |
| `vol`, `duty`, `sustain` | 0–1 |
| `attack`, `decay`, `release` | ms (fino a 2000) |
| `cutoff` | il **filtro**: Hz (0 = nessun filtro) |
| `res`, `filter` | risonanza 0–1; `"lp"` passa-basso, `"bp"` passa-banda, `"hp"` passa-alto, `"notch"` |
| `keytrack` | `true`: il taglio segue la nota (al do centrale è quello dato) |
| `fenv`, `fdecay` | l'inviluppo del filtro: ottave che lo aprono (anche negative), ms per richiudersi |
| `lfo`, `wah`, `pwm` | un'oscillazione lenta (Hz) che muove il filtro (`wah` ottave) e la larghezza della quadra (`pwm` 0–1) |
| `drive`, `noise` | saturazione morbida prima del filtro, rumore bianco aggiunto (0–1) |
| `pan` | −1 sinistra, 0 centro, 1 destra |
| `reverb`, `echo` | quanto la voce manda nell'ambiente e nell'eco (0–1) |
| `ratio`, `depth`, `mdecay`, `feedback` | FM: rapporto del modulatore (1, 2, 3.5…), profondità (radianti, 0–8), in quanti ms si spegne, retroazione 0–1 |
| `bright`, `ring` | PLUCK: brillantezza della corda e quanto suona (0–1) |
| `spread` | SUPERSAW: quanto sono stonati i tre denti di sega (0–1) |
| `bars` | ORGAN: i quattro registri `{8, 6, 3, 2}` (0–15) |
| `sample`, `begin`, `reverse` | SAMPLE: quale campione (un numero del banco, un nome del banco, un tamburo del kit `"bd"` `"sd"` `"hh"` `"oh"` `"cp"` `"rim"` `"tom"` `"cb"`, o `"kit:N"`), da dove parte (0–1 della sua lunghezza), `true` al contrario. La nota rispetto a quella del campione ne decide la velocità |
| `density` | CRACKLE: quanti scatti (0–1) |
| `color` | il colore del `noise` aggiunto: `"white"`, `"pink"`, `"brown"`, `"crackle"` |
| `curve` | la curva del `drive`: `"soft"` (come prima), `"hard"`, `"fold"`, `"sine"`, `"asym"` (armoniche pari, una valvola), `"cubic"` |
| `vowel` | le formanti di una vocale dopo il filtro: `"a"`, `"e"`, `"i"`, `"o"`, `"u"`, `""` nessuna |
| `crush`, `coarse` | bit crush: i bit tenuti (1–15; 0 nessuno); la frequenza divisa (1–16: ogni valore tenuto per tanti campioni) |
| `trem` | tremolo: l'`lfo` sul volume, quanto profondo (0–1) |
| `duck` | ducking: questa voce abbassa le altre mentre suona (0–1): una cassa che fa pompare un pad, un effetto che abbassa la musica |
| `chorus` | quanto la voce manda nel chorus (0–1; lo regola `chorus()`) |
| `raw` | `true`: questa voce è 8-bit (gli effetti sopra sono ignorati, come in `retro`) |

Ogni chiave nuova a 0 (o assente) è il suono com'era. `play()` e `play_at()` prendono le
stesse chiavi nella loro tabella.

```lua
tone(0, "epiano")                                   -- un piano elettrico per note()
note(0, "E4", 400)
tone(1, { preset = "bass", cutoff = 300, res = 0.7 })   -- un basso più cupo e risonante
play(nil, "kick", "C2")                             -- la cassa, su una voce libera
play(nil, { wave = "noise", cutoff = 900, fenv = -2, fdecay = 300, decay = 400, sustain = 0 }, "C3")
play(nil, "sd")                                     -- il rullante del kit (un campione)
play(nil, { preset = "kit", sample = "hh", reverse = true })  -- il charleston al contrario
tone(2, { preset = "lead", crush = 4, coarse = 3 }) -- un lead rotto, 4 bit
tone(3, { preset = "pad", vowel = "o", chorus = 0.6 })  -- un coro che canta "oh"
play(nil, { preset = "kick", duck = 0.8 }, "C2")    -- la cassa abbassa tutto il resto
reverb(0.8, 0.5, 1)                                 -- una cattedrale (Yharnam)
echo(375, 0.4, 1)                                   -- un'eco a tempo a 80 BPM
chorus(0.5, 4, 1)                                   -- un chorus più lento e largo
retro(true)                                         -- il suono 8-bit di una volta
```

**Strumenti pronti** (`src/audio/presets.c`, 50): batteria `kick`, `punch`, `snare`, `clap`,
`hat`, `openhat`, `tom`, `rim`, `crash`, `cowbell`, `shaker` (e `chipkick`, `chipsnr`,
`chiphat` a 8 bit), `kit` (i tamburi del kit come campioni: `"kit:0"`…`"kit:7"`), `pump` (la
cassa del kit che abbassa il resto: il pompare del sidechain); bassi `bass`, `acid`, `sub`,
`fmbass`, `pickbass`, `bitbass` (ridotto a 4 bit); tastiere `epiano`, `organ`, `bell`,
`marimba`, `glock`, `rhodes` (con tremolo e chorus); pad `pad`, `strings`, `warm`, `glass`,
`lush` (supersaw nel chorus), `choir` (un coro che canta "aah"); corde `pluck`, `guitar`,
`harp`; lead `lead`, `sawlead`, `flute`, `brass`, `triangle`, `chip`, `chiptri`, `solo`
(saturazione calda, vibrato, eco); effetti `laser`, `blip`, `boom`, `wind`, `vinyl` (il
crepitio di un disco). Gli stessi nel menu *Instrument...* di bm Sound.

**Il kit della console**: otto tamburi fatti dal sintetizzatore all'avvio della console
(niente di registrato), suonati come campioni: `bd` cassa, `sd` rullante, `hh` charleston
chiuso e `oh` aperto, `cp` battito di mani, `rim`, `tom`, `cb` campanaccio (`"kit:0"`…`"kit:7"`
in quest'ordine). `play(nil, "cp")`, `tone(v, { wave = "sample", sample = "oh" })`; `rim` e
`tom` per nome sono gli strumenti del sintetizzatore (quelli del kit: `"kit:5"`, `"kit:6"`).
Nel modo 8-bit suonano ancora come tamburi (8 bit, il frame più vicino).

#### Il banco: formato e strumenti

Il banco è la sezione **AUDIO** del `.bm` (formato in `src/audio/player.h`, versione 2, o 3
con i campioni; la 1 si legge ancora): fino a 32 suoni (strumenti, ciascuno con il suo
timbro: filtro, posto, ambiente, eco, LFO, effetti), 64 effetti sonori, 64 pattern, 8 brani
(ciascuno con l'eco a tempo, in passi, e la grandezza dell'ambiente) e fino a 64 **campioni**
(8, 16, 24, 32 bit o float, mono o stereo, a qualunque frequenza, con una nota di base e un
loop: in avanti o ping-pong; 2 MiB a banco, circa 22 s di mono a 48 kHz). Si crea con il
**Sound editor** (scheda Dev), che apre un gioco e ne salva i suoni direttamente dentro; il
suo *Samples...* nel menu elenca i campioni del banco e del kit con lunghezza e picco e ne
mette uno nel suono (l'onda SAMPLE, con SAMPLE e START tra le regolazioni), e il piede del
menu dice la profondità dell'uscita (`output 24-bit`). Sul PC: `scripts/bmaudio.py unpack
gioco.bm -o suoni.json` lo estrae in JSON leggibile (i campioni come file WAV accanto),
`mkbm.py --audio suoni.json` lo rimette in una cartuccia, `make wav BANK=suoni.json SONG=0`
lo ascolta in un WAV (`BITS=24`, `32` o `f32` per un file più profondo; 16 se non detto). Un
file WAV entra in un banco dal JSON: `"samples": [{"name": "voce", "wav": "voce.wav",
"root": "C4", "loop": "fwd"}]`, e un suono lo suona con `"wave": "sample", "sample":
"voce"`. `bmhost --wav-bits 24` registra il suono di un gioco a 24 bit.

**Profondità di bit.** Il mix è in float ed è arrotondato una volta sola, con il dither,
alla profondità che porta l'uscita: `sound_depth=16|24|32` in `bm/config.txt` (*Settings >
Screen and sound > Bit depth*, 24 se non detto). L'HDMI del Pi e il codec della RGB30
convertono al più 24 bit; `16` è esattamente la strada di prima. `audio_depth()` dice a un
gioco qual è.

### Tastiera e file (per strumenti come gli editor)

**Giochi e progetti** (2026-10-06): un `.bm` (e un `.b16`) è un **gioco**: gli strumenti di bm
lo leggono e ne prendono codice e asset, ma non lo cambiano mai. Quello che cambiano è un
**progetto**, un `.bme`: lo stesso contenitore, con il bit 0 dell'u16 all'offset 18
dell'intestazione acceso (`src/bm/project.h`). Uno strumento che salva un gioco chiede prima
(*Save an editable copy: NOME.BME?*, come i permessi) e scrive la copia, accanto al gioco
(`NOME.BME`, o `NOME1.BME`… se è preso); un `.bm` nuovo diventa un progetto (`"/carts/NUOVO.BM"`
→ `/carts/NUOVO.BME`). Perciò `cart_save`, `cart_write` e `cart_put_audio` restituiscono `true`
**e il file scritto** quando non è quello chiesto: lo strumento continua con quello. Un
progetto fa il suo gioco con `cart_build`. Nel menu: *Make an editable copy* nelle opzioni di un
gioco (il progetto va in Dev, con l'etichetta "Project"), *Build the game (.bm)* in quelle di un
progetto; A su un progetto lo apre nell'SDK. Queste regole sono degli strumenti di bm; le altre
cartucce scrivono solo file `.bm` e `.bme` in `/carts` fatti nella stessa partita.

| Funzione | Descrizione |
|---|---|
| `keyheld(nome)` | `true` finché è premuto il tasto `nome` di una tastiera: `"f1"`…`"f12"`, `"tab"`, `"space"`, `"enter"`, `"esc"` (bm Pixel: spazio tenuto per disegnare) |
| `keyp()` | il prossimo tasto scritto: un carattere (`"a"`, `"\n"` Invio, `"\b"` Backspace, `"\t"`), un nome (`"up"`, `"down"`, `"left"`, `"right"`, `"home"`, `"end"`, `"pgup"`, `"pgdn"`, `"del"`, `"esc"`, `"f1"`…`"f10"`), `"^s"` per Ctrl+S o `"^S"` per Ctrl+Shift+S (anche Ctrl+I e Ctrl+M: `"^i"`, `"^m"`, non Tab e Invio); `nil` se nessuno. F11 e F12 sono del sistema e non arrivano. Dalla prima chiamata la tastiera scrive e non fa più da gamepad per `btn()`, ed Esc è un tasto come gli altri (Ctrl+Esc, Start+Select e PS chiudono) |
| `keyhelp(lista, [titolo])` | i tasti della cartuccia, mostrati sotto quelli del sistema mentre si tiene **F12** (2026-10-04): `lista` è `{ {"tasti", "cosa fanno"}, "titoletto", … }`; i tasti come in `prompt()` (minuscolo la tastiera, maiuscolo il pad), separati da spazi: `"ctrl s"`, `"shift w a s d"`, `"a / d"` (alternative), `"1 - 5"` (intervallo), `"Y LEFTRIGHT"`. Restituisce quante voci nominano un tasto che il sistema tiene per sé e la cartuccia non riceve mai (F11, F12, Ctrl+Esc, Ctrl+Shift+Esc: in rosso e nel log, vanno tolte); gli altri tasti di sistema (Esc, Ctrl+S…) si elencano quando si dice che cosa fanno lì; `keyhelp(nil)` la toglie. Chiamarla di nuovo quando la pagina cambia |
| `ls([cartella])` | i file della SD: `{ {name=, size=, dir=}, … }` |
| `cart_load(percorso)` | apre un `.bm`: il suo sprite sheet (con i flag delle tile, le zone e i loro riquadri), la sua mappa (con i livelli) e i suoi modelli 3D (con gli scheletri) sostituiscono quelli della cartuccia che chiama; restituisce `{title, author, res, lua, sheet_w, sheet_h, map_w, map_h, layers, [palette]}`; `layers` i nomi dei livelli della mappa; `palette` sono i colori (0xRRGGBB) della tavolozza della sezione SHEET8, nel loro ordine, se lo sheet è salvato così |
| `cart_sheet([w, h])` | larghezza e altezza dello sprite sheet del progetto; con `w` e `h` (multipli di 8, da 8 a 4096) lo porta a quella misura: i pixel che ci stanno restano dove sono, i nuovi sono trasparenti (bm Pixel); i flag restano alla loro tile |
| `cart_new()` | sprite sheet e mappa vuoti (256×256, un livello), niente flag, zone, riquadri o modelli |
| `cart_save(percorso, {title, author, res, lua})` | scrive un `.bm` con il codice dato e lo sprite sheet, i flag delle tile, le zone con i loro riquadri, la mappa con i suoi livelli, la copertina, il banco di suoni, i modelli e gli scheletri correnti (le altre sezioni del file aperto restano come erano); nome 8.3, es. `"/carts/GIOCO.BME"`. `true` (e il file scritto quando non è `percorso`: la copia modificabile di un gioco, vedi sopra), o `false` e un messaggio |
| `cart_build(progetto)` | strumenti di bm: fa il **gioco** di un progetto (`.bme`): gli stessi byte, segnati come un gioco che tiene il nome del progetto (offset 104), in `.BM` accanto; il gioco fatto prima è sostituito, un gioco con lo stesso nome che non ha fatto lui resta e il nuovo prende una cifra (`PONG1.BM`). `true` e il percorso del gioco, o `false` e un messaggio. L'SDK: Ctrl+B, *Build the game .bm* |
| `cart_read(percorso)` | il codice e l'intestazione di un `.bm`: `{title, author, res, lua, size}`, **senza** toccare lo sheet e la mappa di chi chiama (al contrario di `cart_load`): per editor con più file aperti |
| `cart_write(percorso, {[lua, title, author, res, from, sections]})` | cambia **solo** il codice (e i campi dati) di un `.bm`: sprite sheet, mappa, copertina, banco di suoni e le sezioni che il kernel non conosce restano com'erano; un file con il nome lungo lo tiene. Senza `lua` il codice resta quello. Un file che non c'è diventa una cartuccia con solo il codice (nome 8.3). `from`: le altre sezioni vengono da un altro file ("salva come"); `from = false`: una cartuccia nuova, qualunque cosa ci sia nel file (il progetto nuovo di bm Studio). `sections`: `{[8] = byte MESH, [9] = byte ANIM}` (`false` le toglie), controllate prima (`false, "broken MESH section"`): così bm Mesh scrive i modelli. `sheet = true`: lo sprite sheet del progetto (`cart_load`, `sset`, `cart_sheet`) prende il posto di quello del file, come **SHEET8** quando ha al più 256 colori (altrimenti SHEET), con i colori di `palette` (`{0xRRGGBB, …}`) per primi nella sua tavolozza, così come sono e in quell'ordine (la voce trasparente della tavolozza di prima resta al suo posto); un pixel che ha ancora l'RGB565 che aveva nel file tiene i suoi 24 bit di prima (la console tiene 16 bit per pixel): cambiano solo i pixel ridisegnati. Così bm Pixel salva lo sheet. Se nel frattempo `fset` ha cambiato dei flag, vanno nel file anche quelli. Restituisce come `cart_save` |
| `cart_meshes(percorso)` | le mesh che il **codice** di un `.bm` costruisce con `mesh()`, `mesh_sphere()` e `mesh_cube()` (anche con il costruttore di bmlib): `{ {name=, kind=, verts={x,y,z,…}, faces={a,b,c,colore,…}, [uv={…}]}, … }` (gli argomenti di `mesh()`, indici da 1, colore `-1` = texture) e `nil` oppure il primo errore del codice; `nil` e un messaggio se il file non si legge. Il codice gira **a parte** (uno stato Lua suo, `src/bm/meshcap.c`): il corpo del file, poi `_init`, `_update` e `_draw` una volta, con un limite di istruzioni; le altre funzioni di bm non fanno niente (niente file, schermo o suono). Il nome è quello della variabile che tiene la mesh (`M.ship` → `"ship"`; in un array `chef[2].body` → `"chef2_body"`). Per bm Mesh |
| `mesh_reduce(record, triangoli, [ossa, [max_err]])` | **meno triangoli** per un modello della sezione MESH (`src/bm/decimate.c`: collasso degli spigoli con le quadriche di Garland-Heckbert, senza AI): `record` è la parte del modello nella sezione (come la scrive `encode_mesh` di `bm3d.lua`), `ossa` l'osso di ogni vertice (un byte ciascuno, dalla sezione ANIM), `max_err` ferma prima di un collasso più costoso (0: nessun limite). Restituisce il record con al più `triangoli` triangoli (di più solo se non si può andare oltre senza rovesciare facce), le ossa dei suoi vertici (`nil` senza `ossa`) e il numero di triangoli; `nil` e un messaggio se il record è rotto. Bordi, linee di colore e cuciture della texture restano al loro posto; i vertici restano quelli del modello. La pagina models di bm Studio (**-**); sul PC `tools/bmreduce.py` |
| `picture3d(azione, ...)` | un'**immagine diventa un modello 3D** attraverso un servizio image-to-3D (`src/net/img3d.c`; il primo è Meshy, chiave `meshy_key=...` in `bm/config.txt` sulla SD). `picture3d("providers")` i servizi; `picture3d("ready", servizio)` `true`, o `false` e il perché (manca la chiave); `picture3d("start", immagine, {provider=, polycount=})` avvia il lavoro su un `.png`/`.jpg` della SD o su un URL https e dà l'id del lavoro (o `nil` e un messaggio); `picture3d("status", id, servizio)` → `"running", avanzamento` oppure `"done", url` del `.glb` (o `nil` e un messaggio); `picture3d("take", url, {name=, faces=, height=})` scarica il `.glb` e lo converte (`src/bm/glb.c`: posizioni unite, facce in senso orario, texture PNG o JPEG ridotta a 256×256, modello incorniciato alto `height`, ridotto a `faces` triangoli) → `{record=, flat=, texture=, nv=, nf=, textured=}`: `record` è il modello per la sezione MESH (con la texture, se c'è), `flat` lo stesso con i colori presi dalla texture, `texture` 256×256 RGBA. Le chiamate bloccano mentre la rete lavora. La pagina models di bm Studio (**m**) |
| `cutout3d(immagine, {name=, lathe=, height=, depth=, segments=, faces=})` | un modello dal **contorno dell'immagine**, fatto sulla console senza rete né AI (`src/bm/cutout.c`): lo sfondo trasparente (o il colore degli angoli) va via, il contorno diventa un poligono semplificato e il poligono un solido: un **ritaglio** con spessore `depth` (frazione dell'altezza, 0.2: l'immagine davanti, specchiata dietro, i colori del bordo sui lati) o, con `lathe = true`, il mezzo contorno **tornito** intorno all'asse verticale in `segments` passi (vasi, torri, razzi; l'immagine proiettata davanti). `immagine`: un `.png` o `.jpg` della SD; `height` in blocchi (2); `faces` triangoli al più (1200). Dà la stessa tabella di `picture3d("take")`, o `nil` e un messaggio. La pagina models di bm Studio (**m**, le prime due voci) |
| `cart_audio([percorso])` | il banco di suoni di un `.bm` come stringa (`false` se non ne ha) e il suo titolo; senza percorso, il banco della cartuccia che gira |
| `cart_put_audio(percorso, banco, [titolo, lua])` | mette il banco (stringa; `nil` lo toglie) in un `.bm`, il resto del file come prima; se il file non c'è lo crea con quel titolo e quel codice. `true` (e il file scritto, come `cart_save`), o `false` e un messaggio |
| `audio_bank(banco)` | da ora suona questo banco (per gli editor: musica ed effetti che suonano vanno avanti); `nil`: nessuno |
| `audio_pattern(p, bpm, swing)` / `audio_play(v, suono, nota, [vol], [fx], [ms])` | un pattern in loop, un suono del banco su una voce (anteprime degli editor) |
| `cart_run(percorso, [opzioni])` | esce, gioca quel file e poi riapre la cartuccia che l'ha chiesto, con `cart_arg()` = `{path=, error=, back=true, run=}` (dal menu, "Open in the SDK", "... Sound editor", "... bm Studio", "... bm Animator", "... bm Mesh" o "... bm Pixel": `back=false`). `run` (2026-10-04, il dev kit) sono i numeri della partita provata: `{frames, secs, fps, ms, ms_max, slow, lua_kb, lua_peak_kb, data_kb, instr_max, tokens, tris, gpu}` (ms medi e massimi di `_update` + `_draw`, `slow` i fotogrammi oltre 16,7 ms, la memoria di Lua alla fine e al massimo, quella dei dati, le istruzioni del fotogramma più pesante, i token, i triangoli dell'ultimo fotogramma, se il 3D lo faceva la GPU); l'SDK li mostra nel suo dev kit |
| `cart_run(percorso, {breaks = {righe}, stop = true})` | il gioco provato è una sessione del **debugger** (R13, bm Code: F8 e F5): si ferma alle righe `breaks` del suo `main.lua`, a `breakpoint()` e, con `stop`, alla sua prima riga. Fermo, il debugger copre lo schermo: il codice attorno alla riga, le variabili della funzione (le locali, poi le upvalue in azzurro), la pila; F10 o A la riga dopo (sopra le chiamate), F8 o X dentro la chiamata, Shift+F8 o Y fuori dalla funzione, F5 o Start continua, Esc o Select ferma il gioco (che finisce con `main.lua:N: stopped in the debugger`: lo strumento torna sulla riga); le frecce scorrono le variabili e scelgono la funzione della pila. Ogni gioco provato da uno strumento con `cart_run` è una sessione: senza righe non costa niente finché non arriva a `breakpoint()` |
| `breakpoint([perché])` | in un gioco provato da uno strumento (`cart_run`) il debugger si ferma qui, con il motivo nel titolo; negli altri giochi non fa niente |
| `cart_tool(nome, [percorso])` | esce e apre un altro strumento della console sullo stesso file: `"studio"`, `"animator"`, `"mesh"`, `"pixel"`, `"code"`, `"sdk"`, `"sound"` (bm Studio → *Open in bm Animator*, e ritorno); lo strumento lo trova in `cart_arg()` come dal menu, con `from` = il nome dello strumento che l'ha aperto (`"sdk"`: i menu della suite offrono *Back to bm SDK*) |
| `cart_config(chiave, [valore])` | il testo di una chiave di `bm/config.txt`, o `nil`: i **flag** che un gioco prende dalla riga del monitor (`set overbit_bench=auto ; play overbit`) o da `easy_install.sh` (`bench`, `config`). Solo le chiavi che cominciano con la prima parola del titolo del gioco in minuscolo e `_` (Overbit: `overbit_`); le altre sono un errore. Con un valore la chiave cambia e si salva (`""` la toglie: un flag che si legge una volta). Valori fino a 127 caratteri, senza `;` né `,` (dividono la riga del monitor e la configurazione del PC): i flag di Overbit sono `chiave:valore` separati da `/`, le liste da `+` |
| `cart_data(tipo, [byte])` | le sezioni **MESH** (`tipo` 8) e **ANIM** (9) del progetto, come stringhe nel formato di `src/bm/bm.h`: senza `byte` le restituisce (`nil` se non ci sono), con `byte` le sostituisce (`nil` o `""` le toglie) → `true`, oppure `false` e il motivo. Il kernel le controlla prima; `model()`, `animate()` e `bone3d()` usano subito quelle nuove e `cart_save` le scrive. Così bm Studio e bm Animator della console modificano modelli e scheletri (con `string.pack` / `string.unpack`, nella libreria `require "bm3d"`) |

### Assistente (M30, per gli strumenti di sviluppo)

Una piccola AI che gira sulla console: capisce una domanda (italiano o inglese,
anche con errori di battitura) e risponde con le voci della sua base di conoscenza
(ogni funzione delle API, esempi di codice per i giochi, errori di Lua, consigli) o
disegna la base di uno sprite. Non è un chatbot: una rete INT8 minuscola sceglie tra
le voci che conosce, in meno di un millisecondo. Non fa niente finché non la chiami
(nessun processo in background; base di conoscenza e rete stanno nel kernel).
Si prova da **Dev > Assistant** (o `I` dal monitor).

| Funzione | Descrizione |
|---|---|
| `ai.ask(domanda, [{n=5, ctx=parola, kinds="api,howto"}])` | le voci migliori, la prima è la più probabile: `{ {id=, title=, kind=, score=}, … }`, e come secondo valore i microsecondi impiegati. Titoli e testi sono nella lingua delle risposte (`ai.lang()`), che segue quella della domanda. `ctx`: la parola sotto il cursore (se è una funzione delle API, la sua voce va in cima). `kinds`: `api`, `howto`, `error`, `tip`, `sprite` |
| `ai.entry(id)` | una voce: `{id, kind, title, name, text, code, gen, see = {id, …}}` |
| `ai.list([kinds])` | tutte le voci `{id, title, kind}` (per sfogliarle col pad) |
| `ai.lang([lingua])` | la lingua delle risposte (R18): `"it"` o `"en"`, e se segue le domande (`true`); `"it"`/`"en"` la fissano, `"auto"` la fa seguire di nuovo la lingua di ogni domanda. Si parte da `assist_lang` di `bm/config.txt` (`it`, `en`, `auto`: il predefinito, dalla lingua usata per ultima). Nel pannello Ctrl+E la cambia; la lingua è nella barra del titolo |
| `ai.near(parola)` | il nome delle API più vicino a una parola scritta male (`"sprr"` → `"spr"`, 1), o `nil` |
| `ai.sprite(richiesta, [{gen=, size=16, seed=1, outline=true, palette={…}}])` | la base di uno sprite: `{w, h, gen, name, seed, px = {0xRRGGBB o -1 (trasparente), …}}` riga per riga. La ricetta viene dalle parole (`"slime"`, `"astronave"`, `"moneta"`, `"erba"`…) o da `gen`; i colori (`"rosso"`, `"blue"`…) e la misura (`"8x8"`, `"32x32"`, `"piccolo"`, `"grande"`) dalle parole; un altro `seed` è una variante; con `palette` ogni pixel diventa il colore più vicino della tavolozza |
| `ai.recipes()` | le ricette degli sprite `{id, name}`; `ai.recipes("mesh")` quelle 3D `{id, name, rigged}` |
| `ai.script(testo)` | un modello scritto nel **linguaggio delle parti** (`src/ai/mesh_script.c`: `mat`, `box`, `bx`, `tube`, `cyl`, `ell`, `prism`, `wedge`, `tf`, `bone`, `use`, `side`, `mirror`, `clip`, `key`, `turn`, `shift`, una per riga): la stessa tabella di `ai.mesh`, o `nil` e l'errore (`"line 3: ..."`). È il formato che `tools/img2mesh.py` ottiene dal modello con la visione per un'immagine |
| `ai.music(richiesta, [{gen=, seed=1, key=, minor=, bpm=, bars=, inst=, context=}])` | **musica** per un banco di suoni (`src/ai/music.c`): ritmi, basi (batteria, basso e accordi), linee di basso, arpeggi, melodie ed effetti sonori classici, 87 ricette. Le parole scelgono la ricetta (`"ritmo rock"`, `"base lofi"`, `"melodia triste"`, `"effetto moneta"`), la tonalità (`"in la minore"`, `"C major"`), il tempo (`"120 bpm"`, `"veloce"`), la lunghezza (`"8 battute"`) e lo strumento (`"con il piano"`, `"8 bit"`); un altro `seed` è una variante. Le melodie le scrive una piccola rete addestrata su melodie tradizionali e scritte per bm. `context = {notes = {…}, bars = {…}}`: le note che ci sono già (MIDI) e la battuta di ognuna: melodie, arpeggi e bassi ne prendono la tonalità e gli accordi. Restituisce `{gen, name, kind, bpm, swing, key, minor, meter, bars, echo, room, seed, chords = {"Am", …}, instruments = {"kick", …}, patterns = { {len, tracks = {[0..7] = {passo, …}}} }, sfx = {ms, loop = {a, b}, steps = {passo, …}}}`; un passo è `nota \| strumento << 8 \| vol << 16 \| fx << 24` (strumento: indice da 0 in `instruments`). Anche sulla RGB30, dove la ricetta la scelgono le parole senza la rete. `ai.music_recipes()` le elenca |
| `ai.mesh(richiesta, [{gen=, seed=1, scale=1, rig=true}])` | la base di un **modello 3D** per bm Studio e bm Animator: `{gen, name, seed, faces = { {p = {{x,y,z}, …}, c = 0xRRGGBB, b = {osso, …}}, … }, bones = { {name, parent, head, tail}, … } o nil, clips = { {name, loop, length, mode, keys = { {t, pose = { {q, t}, … }}, … }}, … }}`, le stesse tabelle di `bm3d.lua` (un'unità = un blocco di bm Studio, il modello guarda verso −z e poggia su y = 0). La ricetta (53: forme, oggetti, persone, animali, macchine) viene dalle parole (`"casa"`, `"albero"`, `"mech"`…) o da `gen`; i colori (`"rossa"`, `"blue"`), la misura (`"piccolo"`, `"grande"`, `"enorme"`), le proporzioni (`"alto"`, `"basso"`, `"largo"`, `"sottile"`) e `"senza scheletro"` dalle parole; un altro `seed` è una variante. Persone, animali e macchine hanno lo scheletro (ogni spigolo su un osso) e le animazioni (`idle`, `walk`, `fly`, `attack`…) |
| `ai.checksum(domanda)` | il CRC-32 delle uscite della rete per una domanda: per i test (uguale a quello del riferimento in Python) |

**Reti piccole per i giochi** (M38.4: i bot di Overbit). Una rete di strati densi con pesi
e attivazioni INT8 (gli stessi conti dell'assistente, con le istruzioni SIMD dell'ARMv6),
da una stringa che scrive uno script di addestramento (`scripts/nnetlib.py`: quantizza una
rete di numpy, la impacchetta e dà i numeri esatti della console per i test):

| Funzione | Cosa fa |
|---|---|
| `nnet(blob)` | la rete di una stringa "BMNN" (formato in `src/ai/net.h`; errore se è rovinata), fino a 8 strati di 256 |
| `net:run(ingressi, [uscite])` | le uscite (numeri) per una tabella di ingressi; `uscite`: una tabella da riempire invece di una nuova |
| `k, v = net:pick(ingressi, [maschera])` | l'indice (da 1) dell'uscita più grande e il suo valore; `maschera`: tabella di booleani, `false` = quell'uscita non si sceglie |
| `n_in, n_out = net:size()` | quanti ingressi e uscite |

**Il pannello** (`require "assist"`): quello che gli strumenti aprono con un tasto
(F6 nell'Assistant). Risponde mentre scrivi; Invio (A) passa il codice o lo sprite allo
strumento, Esc (B) chiude, Tab (X) cambia modo; senza domanda si sfoglia tutto col pad.
Mentre si scrive una parola della domanda il resto della più probabile appare in
blu-grigio e Tab la scrive (il completamento, sotto).

```lua
local assist = require "assist"

function _update()
  if assist.update() then return end          -- aperto: i tasti sono suoi
  local k = keyp()
  if k == "f6" then
    assist.open{ mode = "code", ctx = word_under_cursor,
                 on_insert = function(code) insert_lines(code) end }
  end
end

function _draw()
  draw_tool()
  assist.draw()                                -- sopra, se aperto
end
```

`assist.open{...}`: `mode` = `"code"` (API, esempi, errori), `"sprite"`, `"mesh"` (le ricette 3D: il modello gira nel pannello, `on_mesh(m)` lo riceve; è il modo di bm Studio e bm Animator con F6), `"error"`
o `"any"`; `query` (domanda già scritta), `ctx`, `error` (un messaggio d'errore: il
pannello mostra la riga, il nome scritto male e cosa vuol dire), `size` e `palette`
per gli sprite, `on_insert(codice)`, `on_sprite(sprite)`, `on_close()`, `x, y, w, h`
(predefinito: quasi tutto lo schermo). Poi `assist.update()` e `assist.draw()` a ogni
fotogramma, `assist.is_open()`, `assist.close()`.

**Azioni sul codice** (`assist.act(richiesta, righe, n)`): quello che chiede una riga
`#entry: richiesta #` alla riga `n` di `righe` (una tabella di stringhe). La rete sceglie
tra le azioni (operatore ternario, commento, log, togli i log, controllo dei nil, rendi
locale, indentazione, rinomina, commenta/scommenta, ottimizza, spiega), un esempio da
inserire o uno sprite scritto come codice; lavora sulla funzione intorno alla riga o
subito sotto. Restituisce `{lines, ok, message, cursor, explain}` (le righe nuove, senza
la riga `#entry:`), e non cambia niente se non è abbastanza sicura. bm Code la usa con
Invio su quelle righe.

La base di conoscenza è in `src/ai/kb/` (formato e come riaddestrare:
`src/ai/kb/README.md`).

**Completamento delle parole** (`require "predict"`, guida in [PREDICT.md](PREDICT.md)):
`predict.complete(testo_prima_del_cursore, {lang = "lua"})` → `nil` o
`{prefix, word, rest, ending, list}`: la parola più probabile che inizia come quella
scritta (`rest` è quello che manca, da mostrare in `predict.C_GHOST`; Tab sostituisce
`prefix` con `word`). `lang`: `"it"`, `"en"`, `"lua"`, `"ask"` (le domande
all'assistente), una miscela con i pesi (`{it = 1, ask = 2}`) o `"none"`; `words` i nomi
del codice (`predict.count_words(righe)`), con peso `words_weight`. I dizionari si leggono
alla prima parola, o un pezzo per fotogramma con `predict.preload({"lua", "it"})`.

**Scrittura col pad** (`require "padtype"`, guida in [PADTYPE.md](PADTYPE.md)): la
composizione rapida (la croce scrive consonanti, la predizione finisce la sillaba, □ e △
la girano, una pressione aspetta la sua doppia, L2 / R2 / L2+R2 altri livelli, R2 + ✕ □ △
le parole) e la tastiera su schermo; Share passa dall'una all'altra. `pt.update(host)`
ogni fotogramma legge `pad()` e modifica il testo attraverso l'host (`before`, `insert`,
`erase`, `newline`, `move`, `lang`; `pt.text_host(lang)` ne fa uno su una stringa),
`pt.draw(x, y)` disegna l'overlay del controller (`pt.size()`), `pt.coach(host, testo)`
dà il prossimo tasto per scrivere un testo. La usano bm Code e la cartuccia Pad Typing.

### nano8 (la libreria `n8`)

Ogni cartuccia vede anche la tabella `n8`: la macchina di **nano8** (`src/bm/n8*.c`), con le
funzioni delle cartucce `.p8` (`n8.spr`, `n8.map`, `n8.print`, `n8.peek`…, con i loro
argomenti e i numeri come li vogliono loro) e quelle per caricarle, avviarle e mostrarle
(`n8.load`, `n8.power`, `n8.buttons`, `n8.blit`, `n8.preview`, `n8.compile`). È fatta per
`carts/nano8` (vedi il commento in `src/bm/n8lua.c`); un gioco `.bm` non ne ha bisogno.

### Luce

Scene al buio illuminate solo da lampade, candele, torce: il disegno del fotogramma viene
moltiplicato per una "mappa di luce" calcolata in C (una griglia ogni 4 pixel,
interpolata e con un leggero dithering). Dal primo `light_begin()` la cartuccia disegna
in RAM invece che direttamente sullo schermo.

| Funzione | Descrizione |
|---|---|
| `light_begin([ambiente])` | inizia le luci del fotogramma: `ambiente` è il colore della luce di fondo (`0x000000` buio pesto, `0xFFFFFF` nessun effetto) |
| `light(x, y, raggio, colore, [intensità])` | una luce morbida in coordinate del mondo (vale `camera`); più luci si sommano, fino a 2× la luminosità |
| `light_end()` | applica la luce a tutto ciò che è stato disegnato; quello che disegni dopo (HUD, testi) resta alla luce piena |

```lua
cls(0); map(...); spr(...)                  -- la scena
light_begin(0x0A0A16)                        -- notte blu scura
light(lx, ly, 50 * (0.95 + math.random() * 0.1), 0xFFB060)   -- un lampione che tremola
light(px, py, 40, 0xFFC888, 0.9)             -- la lanterna del giocatore
light_end()
print("vita", 4, 4, 0xFFFFFF)                -- l'HUD non viene oscurato
```

Esempio completo: `carts/hunt` (Hunter's Night).

### Luce a livelli (come in Dank Tomb)

L'altra luce, quella del gioco PICO-8 *Dank Tomb*: ogni pixel ha un **livello di luce**
(0 il più buio) e il suo colore diventa quello che una **tabella di dissolvenza** dà a
quel livello. Le lampade fanno anelli concentrici di livelli, dal loro livello al centro
fino a 0 al bordo; i bordi degli anelli sono mescolati con un dithering ordinato 4×4; dove
due lampade si toccano vince la più forte. Le tabelle sono scelte dalla cartuccia: si può
far diventare blu notte le ombre e arancioni i colori vicino alle lampade, restando sui
colori della propria palette. Tutto in C (`g16_fade_*` in `src/bm/gfx16.c`).

| Funzione | Descrizione |
|---|---|
| `fades(tabelle)` | le tabelle: `{ {colore, l0, l1, ...}, ... }`, per ogni colore della palette quello che diventa al livello 0 (il più buio), 1, ...; tutte le righe hanno lo stesso numero di livelli (2–16, fino a 255 colori). I colori senza tabella vengono scalati come la media delle tabelle. Restituisce il numero di livelli |
| `dark_begin([ambiente])` | inizia il fotogramma: tutti i pixel al livello `ambiente` (predefinito 0); da qui la cartuccia disegna in RAM |
| `glow(x, y, raggio, livello, [dither])` | una lampada in coordinate del mondo (vale `camera`): `livello` al centro, 0 a `raggio`; `dither` 0–1 (predefinito 0,5) è quanto si mescolano i bordi degli anelli (0 anelli netti, 1 sfumatura continua a retino) |
| `dark_end()` | applica i livelli a tutto ciò che è stato disegnato; quello che disegni dopo (fiamme, scintille, HUD) resta com'è e "brilla" |

```lua
fades(TABELLE)                               -- una volta, in _init
cls(0); map(...); spr(...)                   -- la scena alla luce piena
dark_begin(1)                                -- notte: livello 1 dappertutto
glow(lx, ly, 72 + math.random(2), 6)         -- un lampione
glow(px, py, 28, 3)                          -- la poca luce attorno al giocatore
dark_end()
spr(FIAMMA, fx, fy)                          -- le fiamme non vengono oscurate
```

Esempio completo: `carts/yharnam`; uno piccolo: `SQUARE_CART` in `tests/qemu_test.py` (una lampada su una cartuccia 256×256).

### 3D (software)

| Funzione | Descrizione |
|---|---|
| `mesh(v, f, [uv])` | mesh da tabelle (fino a 65535 vertici e 65535 facce: 4096 e 16384 prima di bm3d 5.0): `v` = {x,y,z, x,y,z, …}, `f` = {a,b,c,colore, …} (indici da 1; una faccia si vede dal lato da cui i suoi vertici appaiono in senso **orario**). Con `uv` (6 numeri per faccia: u,v dei tre vertici in pixel dello sprite sheet) le facce con colore `-1` hanno la **texture** dello sprite sheet (prospettiva corretta, i pixel trasparenti restano vuoti). Il colore può avere i **bit di materiale** (tabella sotto) |
| `mesh_sphere([r, segmenti, c1, c2])`, `mesh_cube([c])` | mesh pronte |
| `model(nome)` / `model(n)` | un **modello 3D della cartuccia** (fatto con [bm Studio](../sdk/README.md), sezione MESH) come mesh, con la texture dello sprite sheet; `n` conta dall'1; `nil` se non c'è. Ogni chiamata costruisce una mesh nuova: va fatta in `_init`. Un modello con la **luce precalcolata** (il bit "lit" di MESH, `src/bm/bm.h`: la luce di ogni angolo delle facce, fatta da uno script, come la mappa di Overbit) si disegna liscio con quella luce, senza sole né cielo ma con le lampade (`lamp3d`) e la nebbia: costa meno della luce calcolata; le sue facce con la texture (finestre, insegne) hanno una luce sola per faccia, colorata, e la nebbia |
| `models()` | i nomi dei modelli della cartuccia, in ordine (`{}` se non ne ha) |
| `bounds3d(m)` | `x0, y0, z0, x1, y1, z1`: il box intorno ai vertici di una mesh, nelle sue coordinate (prima di spostarla, girarla e scalarla con `draw3d`): per centrarla, per le collisioni |
| `animate(m, [anim, t, anim2, t2, k, osso])` | **animazione scheletrica**: un modello con lo scheletro di [bm Animator](../sdk/README.md#bm-animator) prende la posa dell'animazione `anim` (nome o numero) al tempo `t` in secondi (in ciclo, se l'animazione è in ciclo); con `anim2, t2` mescola due animazioni (`k` da 0, solo la prima, a 1, solo la seconda: per passare dall'una all'altra); con `osso` la seconda vale solo per quell'osso e quelli sotto (un busto che spara su gambe che corrono); senza animazione la posa di riposo. Restituisce la durata dell'animazione. Errore se la mesh non ha scheletro o l'animazione non c'è. Le ossa muovono i vertici mentre la mesh si disegna (skinning rigido): `animate` costa solo le ossa |
| `bone_turn(m, osso, [rx, ry, rz])` | da ora ogni `animate` gira anche l'osso di questi angoli (radianti, x poi y poi z, nel sistema del genitore) sopra l'animazione: mirare in alto e in basso, gambe che seguono la direzione di marcia. `bone_turn(m, osso)` lo toglie |
| `bones3d(m)` | i nomi delle ossa dello scheletro, in ordine |
| `hit3d(m, x, y, z, ry, scala, ox, oy, oz, dx, dy, dz, [maxd])` | `t, osso`: il raggio da `o` lungo `d` contro le ossa della mesh nell'ultima posa, disegnata in (x, y, z) girata di `ry` e scalata; ogni osso è una capsula dalla testa alla coda larga quanto i suoi vertici. Il colpo più vicino entro `maxd` (`t` in unità di `d`), o `nil`: hitbox che seguono l'animazione (colpo alla testa: osso `"head"`) |
| `clips(m)` | le animazioni di un modello: `{ {name=, length=, loop=}, ... }` (`{}` senza scheletro) |
| `bone3d(m, osso)` | `x, y, z, cx, cy, cz`: dove si trovano la testa e la coda di un osso (nome o numero) nell'ultima posa, nelle coordinate del modello (come `bounds3d`); `nil` se l'osso non c'è. Per attaccare oggetti alle mani (la testa), la punta di una spada (la coda), luci, effetti |
| `draw3d(m, x, y, z, [rx, ry, rz, scala, flag])` | disegna una mesh con z-buffer e luce per faccia. `flag`: 1 = senza z-buffer (né prova né scrittura: pavimenti e sfondi disegnati per primi, più veloci), 2 = senza luce (colori pieni), 4 = **liscia** (Gouraud: luce calcolata sui vertici e sfumata sulla faccia, con dithering; le facce che condividono gli stessi indici di vertice sembrano una superficie curva, per gli spigoli vivi usare vertici separati o il bit "piatta"), 8 = l'**ombra** della mesh sul piano `y` del punto (lungo il sole: scurisce quello che c'è già, la mesh non si disegna), 16, 32, 48 = **livello di dettaglio** 2, 1, 0 (solo le facce di quel livello, vedi i bit di materiale; senza: 3, tutte), 64 = **in primo piano** (armi e braccia in prima persona: lo z-buffer sotto viene pulito, profondità precise da 0,1 unità); si sommano |
| `sky3d(sole, cielo, terreno)` | colori (0xRRGGBB) della luce del sole e della luce ambiente che viene dall'alto e dal basso (le facce rivolte in su prendono il cielo, quelle in giù il terreno); `sky3d()` torna al bianco |
| `shine3d(spec, esponente, bordo)` | riflessi del sole sulle facce lucide (`spec` 0–2, `esponente` 4–64: più alto, più piccolo) e luce sul bordo delle forme (`bordo` 0–1) |
| `shadow3d(stile)` | ombre di `draw3d` con il flag 8: 0 scuriscono (predefinito), 1 nero a retino (senza leggere lo schermo) |
| `point3d(x, y, z, raggio, colore, [flag])` | un punto rotondo di raggio nel mondo, dietro le cose più vicine (non scrive lo z-buffer): particelle, scintille, proiettili. `flag` 1 = un pixel sì e uno no. Restituisce i pixel |
| `line3d(x0, y0, z0, x1, y1, z1, colore, [spessore, flag])` | una linea 3D, tagliata dal piano vicino e nascosta dalle cose più vicine: traccianti, raggi |
| `sprite3d(sx, sy, sw, sh, x, y, z, larghezza, [flag])` | un rettangolo dello sprite sheet rivolto alla camera, largo `larghezza` unità nel mondo, nascosto dalle cose più vicine: esplosioni, fumo, icone sopra i personaggi |
| `camera3d(x, y, z, [yaw, pitch, fov, roll])` | camera (default a z = −5, fov 60°); `roll` inclina l'inquadratura (radianti) |
| `light3d(x, y, z, [ambiente])` | direzione della luce e luce ambiente (0–1) |
| `zclear()` | pulisce lo z-buffer (a ogni fotogramma, prima di `draw3d`) |
| `gpu3d([on, aa, vs, queue])` | `on, aa, vs, version, queue`: se il 3D lo disegna la GPU, se con l'MSAA 4× e se i vertici dei modelli li mette il vertex shader della GPU (M36): `vs` è `false`, `1` (lo scenario: modelli senza luce o con la luce agli angoli) o `2` (tutti, anche quelli illuminati dal sole e con le ossa; `true` vale 2); `queue` (M35): se il lavoro della GPU parte senza aspettarlo e il `_update` del fotogramma dopo gira intanto (dove la prova all'avvio l'ha visto funzionare); il 2D disegnato dopo il 3D (l'HUD) e quello del `_update` si registrano e vanno sulla pagina quando la GPU ha finito, nello stesso ordine (il risultato è lo stesso; un `_update` che disegna 3D o legge lo schermo con `pget` torna a girare dopo il fotogramma). Con gli argomenti lo cambia per questa cartuccia (un menu "3D: GPU / GPU + VS / GPU + AA / ARM" nel gioco); `on` resta `false` se la GPU non c'è (QEMU, `gpu3d=0`) e nei giochi a 256×256 (la GPU scrive pagine intere), `aa` se l'MSAA non si può usare e `vs` se la prova all'avvio non ha visto il vertex shader (e il suo clipping) funzionare. `version` è la versione dei driver 3D che questa scelta riproduce (`"0.2"` l'ARM, `"2.1"`, `"3.0"`, `"3.4"`, `"4.1"`: `docs/DRIVERS.md`). All'uscita torna quello delle impostazioni |
| `fog3d(colore, vicino, lontano)` | nebbia: le facce sfumano nel colore tra le due distanze; `fog3d()` la toglie |
| `lamp3d(i, x, y, z, raggio, [k, colore])` | luce puntiforme `i` (1–4): le facce con il centro entro `raggio` diventano più chiare, fino a `k` in più (predefinito 1) al centro, del `colore` dato (bianco se manca); `lamp3d(i)` la spegne, `lamp3d()` le spegne tutte. Con `light3d` ad ambiente basso fa scene al buio con lanterne |
| `project3d(x, y, z)` | punto del mondo → `sx, sy, profondità` sullo schermo (`nil` se è dietro la camera): per disegnare in 2D cose allineate al 3D (orizzonte, mirini, etichette) |
| `visible3d(x, y, z, r)` | `false` se una sfera (centro, raggio) non si può vedere: dietro la camera, oltre un bordo dello schermo o (dopo `pvs3d`) su pezzi della mappa che la cella della camera non vede. Per saltare i personaggi e gli effetti nascosti dai muri prima di disegnarli (bm3d 5.5) |
| `pvs3d(t)` | la visibilità precalcolata della mappa per `visible3d`: `t = { x0, z0, cell, nx, nz, max_y, sets, boxes }`, una griglia di `nx`×`nz` celle di lato `cell` sul terreno da (`x0`, `z0`); `sets` una stringa per cella (riga per riga lungo x), ogni byte il numero (da 1) di un pezzo visto da lì; `boxes` sei numeri per pezzo (`x0 y0 z0 x1 y1 z1`). Con la camera sopra `max_y` o fuori dalla griglia conta solo l'inquadratura; una sfera fuori da tutti i pezzi si vede. `pvs3d()` la dimentica |

**Bit di materiale** nel colore di una faccia (di `mesh()` e dei modelli; 0 = la faccia di
sempre):

| Bit | Valore | Effetto |
|---|---|---|
| 30 | `0x40000000` | **emissiva**: colore pieno, senza luce (luci, schermi, energia) |
| 29 | `0x20000000` | **lucida**: il riflesso del sole (`shine3d`) |
| 28 | `0x10000000` | **a retino**: un pixel sì e uno no, si vede quello che c'è dietro (scudi, vetri) |
| 27 | `0x08000000` | **piatta**: anche in un disegno liscio (flag 4) prende la luce del suo piano; gli spigoli vivi possono condividere i vertici |
| 24–26 | | **livello di dettaglio**: bit 24–25 un livello `k` (0–3); bit 26 a 0 la faccia si vede da `k` in su (un dettaglio), a 1 sotto `k` (una versione semplice) |

```lua
local SHIELD = 0x40C8FF | 0x40000000 | 0x10000000   -- azzurro, emissivo, a retino
local DETAIL = 0xFFFFFF | 0x02000000                 -- bianco, solo dal dettaglio 2 in su
```

### Mondi di collisione

Scatole solide, raggi e corpi che si muovono scivolando sui muri (in C: molto più veloci
che in Lua). Esempio completo: `carts/overbit`. Per la mappa 2D ci sono i flag delle tile
(`mflags`) e `lib.move` / `lib.step` di bmlib.

| Funzione | Descrizione |
|---|---|
| `world3d()` | un mondo di collisione vuoto |
| `world_box(w, x0, y0, z0, x1, y1, z1, [tag])` | una scatola solida; restituisce il suo numero |
| `world_ray(w, ox, oy, oz, dx, dy, dz, [maxd, terreno])` | `t, nx, ny, nz, scatola`: il primo punto colpito lungo il raggio (la normale della faccia; scatola 0 = il terreno `y = 0`, che conta a meno che `terreno` sia `false`), o `nil` |
| `world_move(w, x, y, z, r, h, dx, dy, dz, [gradino, a_terra])` | `x, y, z, flag`: un corpo (piedi in `x, y, z`, raggio `r`, altezza `h`) spostato di `d`, che scivola sui muri e sale i gradini fino a `gradino` (0,45) se era a terra. `flag`: 1 a terra, 2 un muro (4 lungo x, 8 lungo z), 16 un soffitto |
| `world_floor(w, x, z, y, r, [gradino])` | l'altezza del pavimento sotto `(x, z)` |

I triangoli che attraversano il piano vicino alla camera vengono tagliati, non scartati:
pavimenti e oggetti grandi restano interi anche quando passano accanto alla camera.

**3D sulla GPU (M33).** I triangoli li disegna la GPU del Pi (V3D); con
*Impostazioni > Graphics > 3D of the games* su `ARM` (`gpu3d=0` in `bm/config.txt`), e in
QEMU, li disegna l'ARM. Le stesse funzioni, nessun cambiamento nelle cartucce. L'ARM continua a
trasformare, illuminare e tagliare; la GPU riempie i pixel con uno z-buffer a 24 bit,
sfumature senza dithering e texture con il texel più vicino. Il 3D in attesa viene
disegnato prima di ogni disegno 2D che lo segue, di `pget`, di `sset` e a fine
fotogramma. Se una cartuccia disegna altro 3D dopo il 2D nello stesso fotogramma,
dal fotogramma successivo la GPU conserva lo z-buffer tra le due parti (circa 1 MB
di memoria scritta e riletta per fotogramma; il primo fotogramma no): conviene
comunque disegnare prima tutto il 3D e poi l'HUD. Lo z-buffer della GPU riparte da
zero a ogni fotogramma, anche senza `zclear()`. `stat(9)` vale 1 quando il 3D lo fa
la GPU. Se la GPU non risponde, il kernel torna all'ARM da solo e lo scrive nel log.
La GPU disegna anche le ombre (`draw3d` con il flag 8: sempre nere a retino, lo stile 1
di `shadow3d`, sopra le cose già disegnate grazie allo z-buffer), gli effetti 3D
(`point3d`, `line3d`, `sprite3d`), le facce a retino e le texture dei modelli con la
luce precalcolata (la luce e la nebbia sfumate sugli angoli, anche con le `lamp3d`).
Solo le facce **con texture e a retino** insieme non le sa fare: alla prima la
cartuccia passa all'ARM per il resto della partita (una riga nel log; il fotogramma
misto non si mostra).
Un fotogramma che comincia con `cls()` costa meno alla GPU: le tile partono dal colore
di `cls` invece di rileggere la pagina. Le texture con i lati multipli di 32 (sprite
sheet 128×128, 256×256, …) vanno alla GPU in T-format, il formato a tile della sua
cache, più veloce da leggere.

**Anti-aliasing (M34).** *Impostazioni > Graphics > 3D anti-aliasing: 4x*
(`gpu3d_aa=1` in `bm/config.txt`) fa disegnare il 3D della GPU con l'MSAA 4×: quattro
campioni per pixel, la media a fine tile, bordi dei triangoli senza scalini. Non
cambia nulla nelle cartucce. Vale solo dove la GPU lo regge (la prova all'avvio lo
dice: *4x: not on this GPU* altrimenti) e solo nei lavori senza z-buffer conservato:
le cartucce che disegnano 3D, poi 2D, poi altro 3D nello stesso fotogramma restano
senza anti-aliasing (lo z-buffer a 4 campioni non si salva). Se la prova trova che la
GPU non sa ricaricare la pagina nei 4 campioni, l'MSAA si usa solo nei fotogrammi che
cominciano con `cls()`. Sull'ARM non c'è anti-aliasing.
Esempio completo: `carts/astrowing` (volo in stile Star Fox: modelli costruiti in
codice, orizzonte con `project3d`, nebbia, esplosioni, boss). Con i modelli di bm Studio:
`carts/village` (`model()` per ogni modello, terreno disegnato senza z-buffer, notte con
`lamp3d` e `fog3d`; il paesano di bm Animator con `animate()`, due animazioni mescolate,
una luce in mano con `bone3d()`, e la sua versione a sprite pre-renderizzati).

## bmlib: la libreria comune dei giochi

`local lib = require "bmlib"` (R10, 2026-10-04): quello che ogni gioco riscriveva da sé
(trovato in 5–10 cartucce del repository) in una libreria sola, in Lua, nel kernel
(`src/script/bmlib.lua`). Non tocca le variabili globali e non copre nessuna funzione della
console. Unità: **tempi in secondi**, posizioni in pixel, velocità in **pixel per
fotogramma**. Gli oggetti (particelle, camera, stati, menu, pausa, costruttore 3D) si usano
con i due punti: `cam:follow(x, y)`.

Tween, timer, script e jingle vanno avanti con **`lib.update()`**, da chiamare una volta
in `_update` (aggiunge 1/60 s; `lib.update(dt)` un altro passo). `lib.time` è il tempo
contato così.

```lua
local lib = require "bmlib"

function _update()
  lib.update()          -- tween, timer, script e jingle
  -- ... il gioco
end
```

### Numeri

| Funzione | Descrizione |
|---|---|
| `lib.clamp(v, lo, hi)` | `v` tra `lo` e `hi` |
| `lib.lerp(a, b, t)` / `lib.unlerp(a, b, v)` | da `a` (t = 0) a `b` (t = 1); il contrario: dove sta `v` tra `a` e `b` |
| `lib.remap(v, a0, a1, b0, b1)` | `v` dall'intervallo `a0..a1` all'intervallo `b0..b1` |
| `lib.approach(v, meta, passo)` | `v` verso `meta` di al più `passo` (barre che scendono piano, velocità che frenano) |
| `lib.sign(v)` | −1, 0 o 1 (0 per 0) |
| `lib.round(v, [passo])` | all'intero più vicino, o al multiplo di `passo` più vicino |
| `lib.wrap(v, lo, hi)` | `v` riportato in `lo..hi` (`hi` escluso) girando: mondi che si richiudono |
| `lib.cycle(i, d, n)` | l'indice `i` (1..n) spostato di `d` girando: le righe di un menu |
| `lib.dist(ax, ay, bx, by)` / `lib.dist2(...)` | la distanza / il suo quadrato (per confrontarla con `r * r` senza radice) |
| `lib.len(x, y)` / `lib.norm(x, y)` | la lunghezza di un vettore / il vettore lungo 1 (e la lunghezza che aveva); `0, 0, 0` per `0, 0` |
| `lib.angle(ax, ay, bx, by)` | l'angolo da `a` a `b` in radianti (0 a destra, π/2 in giù sullo schermo) |
| `lib.angdiff(a, b)` / `lib.turn(a, meta, passo)` | da `a` a `b` per la via più corta (−π..π) / l'angolo `a` girato verso `meta` di al più `passo` |
| `lib.dir8(x, y)` | una delle 8 direzioni: 0 destra, 1 giù a destra, 2 giù… 7 su a destra (`nil` per `0, 0`); `lib.DIR8[d + 1]` è il vettore lungo 1 |
| `lib.TAU` | 2π |

### Numeri a caso

| Funzione | Descrizione |
|---|---|
| `lib.rnd([a, b])` | un numero tra `a` e `b` (`b` escluso); `lib.rnd(n)`: tra 0 e `n`; `lib.rnd()`: tra 0 e 1 |
| `lib.chance(p)` | `true` con probabilità `p` |
| `lib.choose(t)` / `lib.shuffle(t)` | un elemento a caso della lista (`nil` se è vuota) / la lista mescolata (sul posto) |
| `lib.rng(seme)` | un generatore suo, che dallo stesso seme dà sempre gli stessi numeri (mondi fatti da un seme, partite in rete in lockstep): `r:next()` tra 0 e 1, `r:range(a, b)`, `r:int(a, b)` (interi, compresi), `r:pick(t)`, `r:chance(p)`, `r:seed(s)` |

### Collisioni

| Funzione | Descrizione |
|---|---|
| `lib.overlap(ax, ay, aw, ah, bx, by, bw, bh)` | due rettangoli (x, y, larghezza, altezza) si toccano |
| `lib.hit(a, b)` | lo stesso per due tabelle con `x, y, w, h` (`w` e `h` 8 se mancano) |
| `lib.inside(px, py, x, y, w, h)` | il punto è nel rettangolo |
| `lib.circles(ax, ay, ar, bx, by, br)` / `lib.circrect(cx, cy, r, x, y, w, h)` | due cerchi / un cerchio e un rettangolo si toccano |

### La mappa: muri, piattaforme, gravità

Le funzioni guardano i **flag delle tile** ([sopra](#sprite-e-mappa)): `lib.SOLID` (flag 0,
1), `lib.PLATFORM` (flag 1, 2), `lib.LADDER` (4), `lib.WATER` (8), `lib.HURT` (16).

| Funzione | Descrizione |
|---|---|
| `lib.tiles(opz)` | come la mappa ferma i corpi: `layer` (il livello: numero o nome, 1), `solid` e `platform` (le maschere dei flag: 1 e 2; 0 = nessuna), `edge` (`true`: fuori dalla mappa è solido). Restituisce la configurazione |
| `lib.solid(x, y, [w, h])` | il rettangolo (o il punto) tocca qualcosa di solido |
| `lib.move(c, dx, dy)` | sposta il corpo `c = {x, y, w, h}` (`w`, `h` 8 se mancano) di `dx, dy` fermandolo contro le tile solide e facendolo scivolare lungo i muri; su una piattaforma cade solo da sopra (e non con `c.drop`: giù attraverso). A passi più corti di una tile: non ne salta nessuna. Restituisce `hx, hy`: −1 / 1 dove ha urtato (sinistra / destra, su / giù), altrimenti 0 |
| `lib.step(c)` | un fotogramma di un corpo con la gravità (un platform): `c.vx, c.vy` in pixel per fotogramma, `c.gravity` (`lib.GRAVITY`, 0,25) e `c.maxfall` (`lib.MAXFALL`, 6); poi `c.ground` è `true` a terra e la velocità che ha urtato è 0. Restituisce quello che dà `lib.move` |
| `lib.ray(x0, y0, x1, y1, [maschera])` | la prima tile con un flag di `maschera` (quella solida di `lib.tiles` se manca) sul segmento: `x, y` dove il segmento ci entra e la sua cella `mx, my`; `nil` se la strada è libera (linea di vista, proiettili) |

```lua
local eroe = { x = 40, y = 40, w = 8, h = 8, vx = 0, vy = 0 }
lib.tiles({ edge = true })                         -- i bordi della mappa sono muri
function _update()
  eroe.vx = (btn("right") and 2 or 0) - (btn("left") and 2 or 0)
  if eroe.ground and btnp("a") then eroe.vy = -5 end
  eroe.drop = btn("down")                          -- giù da una piattaforma
  lib.step(eroe)
end
```

### Hitbox e hurtbox

Le **hurtbox** sono dove un corpo può essere colpito, le **hitbox** dove un attacco fa male
(un pugno, una spada, un proiettile). Un mondo di colpi le raccoglie a ogni fotogramma e
dice chi colpisce chi: le squadre, un colpo solo per attacco, le parti del corpo, le corsie
dei picchiaduro a scorrimento, le lame che si scontrano. I riquadri si danno nel codice o
vengono dallo sheet (`zboxes`, [sopra](#sprite-e-mappa)).

| Funzione | Descrizione |
|---|---|
| `lib.hits()` | un mondo di colpi `H` (tabelle `H.hurts`, `H.hitl`: i riquadri del fotogramma) |
| `H:clear()` | un fotogramma nuovo, da chiamare in `_update` prima dei riquadri: quelli di prima vanno; gli attacchi (`id`) che non sono tornati nel fotogramma passato sono finiti |
| `H:hurt(chi, x, y, w, h, [opz])` | una hurtbox di `chi` (un valore qualsiasi: la tabella del corpo). `team` (la stessa squadra non si colpisce), `part` (un nome: `"head"`; prima le parti che contano di più), `z` e `depth` (una terza dimensione: la corsia, l'altezza) |
| `H:hit(chi, x, y, w, h, [opz])` | una hitbox dell'attaccante `chi`: `team`, `id` (un attacco: colpisce ogni corpo **una volta** finché lo stesso `id` torna fotogramma dopo fotogramma; i riquadri di un attacco lo condividono; senza `id` fa male a ogni fotogramma che tocca), `clash` (due hitbox con `clash` che si toccano danno un contatto `"clash"`), `z`, `depth` e tutto quello che serve al gioco (`damage`, `knock`...) |
| `H:zone(chi, nome, fotogramma, x, y, [flip, opz])` | i riquadri del fotogramma di una zona (`zboxes`: `hurt` e `hit`) per lo sprite disegnato con `zspr(nome, x, y, fotogramma, flip)`: le hurtbox con `opz` (`team`, `part`), le hitbox con `opz.attack` (le opzioni di `H:hit`; la squadra di `opz` se manca) |
| `H:check()` | i contatti del fotogramma, nell'ordine delle hitbox: `{kind = "hit" o "clash", by (l'attaccante), to (il corpo colpito, o l'altro attaccante), hit (le opzioni di H:hit), part, x, y (il centro di dove si toccano)}`. Un attacco colpisce un corpo una volta per fotogramma (il primo riquadro che tocca) |
| `H:draw([c_hurt, c_hit])` | i riquadri, per vederli mentre si fa il gioco (blu e rosso) |
| `lib.box(b, x, y, [flip, w])` | un riquadro di un fotogramma (`b.x`, `b.y` dall'angolo, `b.w`, `b.h`) nel mondo, per il fotogramma disegnato a `x, y`; con `flip` specchiato sulla larghezza `w` del fotogramma: `x, y, w, h` |
| `lib.separate(a, b)` | due corpi `{x, y, w, h}` che si sovrappongono si spostano lungo l'asse dove si toccano meno, metà ciascuno (`a.fixed` o `b.fixed`: solo l'altro); `true` se si toccavano. Per i lottatori che non si attraversano, le folle |

```lua
local H = lib.hits()

function _update()
  H:clear()
  for _, f in ipairs(fighters) do
    H:zone(f, f.anim, f.frame, f.x, f.y, f.face < 0,
           { team = f.team, attack = { id = f.swing, damage = 5 } })
  end
  for _, c in ipairs(H:check()) do
    c.to.life = c.to.life - c.hit.damage    -- c.by ha colpito c.to
  end
end
```

### Easing, tween, timer e script

`lib.ease` ha le curve da 0 a 1: `linear`, `inquad`, `outquad`, `inoutquad`, `incubic`,
`outcubic`, `inoutcubic`, `insine`, `outsine`, `inoutsine`, `inback`, `outback`,
`inoutback`, `outelastic`, `outbounce`, `smooth` (smoothstep).

| Funzione | Descrizione |
|---|---|
| `lib.tween(ogg, a, secondi, [ease, fatto])` | i campi di `ogg` vanno ai valori della tabella `a` in `secondi` (`ease`: una funzione o il nome di una di `lib.ease`, lineare se manca), poi `fatto(ogg)`. Restituisce un riferimento: `h:cancel()` |
| `lib.after(secondi, f)` | `f()` tra `secondi` |
| `lib.every(secondi, f, [volte])` | `f()` ogni `secondi` (`volte` volte, sempre se manca); `f` che restituisce `false` si ferma |
| `lib.script(f, ...)` | `f(...)` come uno **script che può aspettare**: `lib.wait(secondi)` (un fotogramma se manca) e `lib.waitfor(cond)` (finché `cond()` è vera). Gira subito fino alla prima attesa, poi lo porta avanti `lib.update()`: dialoghi, scene, ondate di nemici scritte in fila |
| `lib.cancel(h)` | ferma un tween, un timer o uno script (lo stesso di `h:cancel()`) |
| `lib.countdown(t, chiavi, [d])` | i campi `chiavi` di `t` sopra 0 scendono di `d` (1/60 se manca), senza andare sotto 0: i tempi di ricarica di un eroe (in fotogrammi con `d = 1`) |
| `lib.clear()` | ferma tutti i tween, timer, script e jingle (un livello nuovo) |

```lua
lib.tween(scritta, { y = 80 }, 0.6, "outback")
lib.after(2, function() porta.aperta = true end)
lib.script(function()
  dialogo("Chi va là?")
  lib.wait(1.5)
  lib.waitfor(function() return btnp("a") end)
  dialogo("Passa pure.")
end)
```

### Liste e particelle

| Funzione | Descrizione |
|---|---|
| `lib.each(lista, f)` | `f(elemento, i)` per ogni elemento; quelli per cui restituisce `false` escono dalla lista (gli altri restano in ordine) |
| `lib.sweep(lista)` | toglie gli elementi con `.dead` (in ordine) |
| `lib.particles([max])` | un insieme di al più `max` particelle (200); pieno, una nuova prende il posto della più vecchia |
| `P:add(x, y, vx, vy, vita, colore, [opz])` | una particella (velocità in pixel per fotogramma, vita in secondi); `opz`: `size` (raggio; 0 o 1 un pixel), `gravity`, `drag` (la parte della velocità che resta a ogni fotogramma), `colors` (una lista: il colore lungo la vita), `shrink`, `floor` (la y dove rimbalza), `bounce` (0,3) |
| `P:burst(x, y, n, [opz])` | `n` particelle da `x, y`: `speed` (2), `angle` e `spread` (in radianti; tutto intorno se mancano), `life` (0,5 s), `color` o `colors`, e le opzioni di `:add` |
| `P:update()` / `P:draw()` | in `_update` / in `_draw` |
| `P:count()` / `P:clear()` | quante sono / tutte via |

```lua
local fx = lib.particles(300)
fx:burst(x, y, 20, { speed = 3, colors = { 0xFFFFFF, 0xFFD050, 0xFF6020 }, gravity = 0.1 })
```

### Camera

| Funzione | Descrizione |
|---|---|
| `lib.camera([opz])` | una camera 2D: `smooth` (0,15: la parte di strada che fa a ogni fotogramma; 1 = segue esatta), `dead` `{w, h}` (un riquadro in mezzo dove il bersaglio si muove senza la camera), `bounds` `{x0, y0, x1, y1}` in pixel o `true` (la mappa, `msize()`), `offset` `{x, y}`, `w`, `h` (lo schermo se mancano). Campi `x`, `y` |
| `C:follow(x, y, [subito])` | un passo verso il punto `x, y` (al centro dello schermo); `subito`: ci va in una volta |
| `C:shake(quanto, [secondi])` | lo schermo trema fino a `quanto` pixel, sempre meno, per `secondi` (0,3) |
| `C:apply([vista])` | `camera()` al posto della camera (con il tremolio): in `_draw` prima del mondo, poi `camera()` per l'HUD. Con una vista `{x, y, w, h}` (`lib.split`) il disegno resta nella vista (`clip`) e l'angolo della camera è il suo: dopo le viste, `clip()` e `camera()` |
| `C:map([livello, maschera])` | le celle della mappa che si vedono |
| `C:sees(x, y, [w, h])` / `C:screen(x, y, [vista])` | il rettangolo si vede / dove sta un punto del mondo sullo schermo (nella vista) |

### Stati del gioco

`lib.states(def, [primo, ...])`: titolo, partita, pausa, game over come tabelle
`{enter, update, draw, exit}`; ogni funzione riceve la sua tabella (`function play:update()`),
che ha `t` (i secondi passati nello stato) e `name`.

| Funzione | Descrizione |
|---|---|
| `S:go(nome, ...)` | allo stato `nome`: quelli aperti escono (`exit`), `nome` entra (`enter(...)`) |
| `S:push(nome, ...)` / `S:pop()` | `nome` sopra quello di adesso (una pausa, un dialogo: si aggiorna solo lui, si disegnano tutti e due) / torna a quello sotto |
| `S:update()` / `S:draw()` | in `_update` / in `_draw` (dal basso: il gioco sotto la sua pausa) |
| `S:is(nome)`, `S.name`, `S:top()` | lo stato in cima |

```lua
local S = lib.states({
  title = { update = function(s) if btnp("ok") then S:go("play") end end,
            draw = function() cls(0); lib.printc("PREMI A", 160, 0xFFFFFF) end },
  play = { enter = function(s) s.punti = 0 end,
           update = function(s) ... end, draw = function(s) ... end },
}, "title")
function _update() lib.update(); S:update() end
function _draw() S:draw() end
```

### Testo, barre e menu

| Funzione | Descrizione |
|---|---|
| `lib.textw(testo, [scala])` | larghezza e altezza in pixel con il font di `print` di adesso (la riga più lunga) |
| `lib.printc(testo, y, [c, scala, x, w, griglia])` | al centro dello schermo (o di `x..x + w`); `griglia`: sulle colonne del font (scritte ferme, come i menu). Restituisce la x |
| `lib.printr(testo, x, y, [c, scala])` | che finisce in `x` (numeri allineati a destra) |
| `lib.prints(testo, x, y, c, [ombra, scala])` / `lib.printo(...)` | con l'ombra sotto a destra / con il contorno tutto intorno (nero se manca il colore) |
| `lib.bar(x, y, w, h, v, max, [c, sfondo, bordo])` | una barra piena per `v` su `max` (vita, stamina, caricamento) |
| `lib.blink([periodo, t])` | `true` metà del tempo, cambia ogni `periodo` secondi (0,5): "premi A" |
| `lib.timestr(secondi, [decimi])` | `"m:ss"` (`"h:mm:ss"` da un'ora), con i decimi `"m:ss.d"` |
| `lib.btnr(i, [p, attesa, ritmo])` | `btnp` che si **ripete** finché il tasto è tenuto: il primo fotogramma, poi dopo `attesa` fotogrammi (15) ogni `ritmo` (4). `i` come per `btn()`. Da chiamare a ogni fotogramma |
| `lib.menu(voci, [opz])` | una lista da scegliere: le voci sono testi o tabelle `{label=, value=, change=function(voce, d), ok=function(voce), off=true}`; `opz.p` il giocatore, `opz.wrap` (`true`) |
| `M:update()` | su e giù (ripetuti) si spostano saltando le voci `off`, sinistra e destra chiamano `change`, ok sceglie (`ok(voce)`; restituisce la voce e `"ok"`), indietro restituisce `nil, "back"` |
| `M:draw(x, y, [opz])` | le righe da `x, y`: `w` (larghezza: i valori a destra), `c`, `sel_c`, `bar`, `dim`, `scala`, `gap` |
| `lib.pause([opz])` | il **menu di pausa** che ogni gioco aveva: Start (Esc) lo apre quando `opz.when()` è vera (sempre se manca); RESUME, VOLUME (quello della console: sinistra e destra), le righe di `opz.rows`, QUIT (`opz.quit()`, se c'è). `opz.color`, `opz.title`, `opz.scale` |
| `P:update()` / `P:draw()` | in `_update`: `if pausa:update() then return end` (mentre è aperta il gioco è fermo); in `_draw`, dopo il gioco |

```lua
local pausa = lib.pause({ when = function() return S:is("play") end,
                          quit = function() S:go("title") end })
function _update()
  if pausa:update() then return end
  lib.update(); S:update()
end
function _draw() S:draw(); pausa:draw() end
```

### Più giocatori sulla stessa console

`btn(i, p)`, `stick(p)` e `controller(p)` leggono il giocatore `p` ([Input](#input)); bmlib
aggiunge la schermata dove si entra, lo schermo diviso e i colori. Il modello *Versus 2D*
dell'SDK è un esempio completo.

| Funzione | Descrizione |
|---|---|
| `lib.PLAYER_COLORS` | i colori dei giocatori, quelli delle luci dei pad: 1 blu, 2 rosso, 3 verde, 4 rosa (come `controller(p).color`) |
| `lib.pads()` | i numeri dei giocatori che hanno un controller ora, in ordine (`{1, 3}`) |
| `lib.party([opz])` | la schermata dove i giocatori di una console entrano: ognuno preme ok sul suo controller per entrare, indietro per uscire; Start di uno che è dentro comincia, con almeno `min` giocatori (1); `max` (4); `join(p)`, `leave(p)`: funzioni chiamate quando uno entra o esce. `P:update()` in `_update` restituisce i giocatori entrati (i loro numeri, nell'ordine in cui sono arrivati) quando si comincia, se no `nil`; `P:draw([x, y, w, h])` una carta per posto, nel colore del giocatore, con il suo controller e il tasto per entrare; `P.list` chi è dentro ora |
| `lib.split(n, [opz])` | lo schermo diviso in viste `{x, y, w, h}` per `n` giocatori (1–4): due affiancate (`vertical`: una sopra l'altra), tre o quattro negli angoli (con tre quello in basso a destra resta libero: una mappa, i punti); `gap` pixel tra l'una e l'altra (2); `x, y, w, h` la parte dello schermo (tutto se mancano) |

```lua
local party = lib.party({ min = 2 })
local views, cams

function _update()
  if not views then
    local who = party:update()               -- es. {1, 2}
    if who then
      views, cams = lib.split(#who), {}
      for i, v in ipairs(views) do cams[i] = lib.camera({ w = v.w, h = v.h }) end
    end
    return
  end
  -- ... cams[i]:follow(eroe[i].x, eroe[i].y)
end

function _draw()
  cls(0)
  if not views then party:draw(16, 80, SCREEN_W - 32, 200) return end
  for i, v in ipairs(views) do
    cams[i]:apply(v)
    cams[i]:map()
    disegna_mondo()
  end
  clip()
  camera()
end
```

### Suono, salvataggi, animazioni, colori

| Funzione | Descrizione |
|---|---|
| `lib.jingle(note, [voce, forma, vol])` | una melodia breve sulla voce (3): `note = { {nota, secondi, [forma, vol]}, ... }`, la nota in Hz o per nome (`"C5"`), 0 o `"-"` una pausa; `lib.jingle(nil, voce)` la ferma, `lib.jingling([voce])` dice se suona ancora |
| `lib.store([k, [v]])` | un campo del salvataggio della cartuccia (`save`/`saved`): `lib.store(k)` lo legge, `lib.store(k, v)` lo scrive (sulla SD solo se è cambiato: non a ogni fotogramma), `lib.store()` tutta la tabella |
| `lib.best(k, v)` | il record `k`: `v` se lo batte (e lo salva), e `true` se è nuovo |
| `lib.frame(fotogrammi, fps, [t])` | l'elemento di `fotogrammi` che il tempo `t` (`time()` se manca) mostra a `fps` al secondo, in ciclo |
| `lib.anim(fotogrammi, fps, [ciclo])` | un'animazione sua: `a:update()` va avanti di un fotogramma e restituisce l'elemento; `a.done` quando una senza ciclo (`false`) è finita; `a:reset()` |
| `lib.mix(c1, c2, t)` / `lib.shade(c, k)` | tra due colori `0xRRGGBB` / un colore `k` volte più luminoso (0 nero) |

### 3D: il costruttore di Astro Wing

`lib.builder()`: una mesh fatta di pezzi convessi; ogni faccia di un pezzo è girata verso
l'esterno da sola, quindi l'ordine degli angoli non conta mai.

| Funzione | Descrizione |
|---|---|
| `B:piece(punti, triangoli, [colore])` | un pezzo convesso: `punti = { {x, y, z}, ... }`, `triangoli = { {i, j, k, [colore]}, ... }` |
| `B:box(x0, y0, z0, x1, y1, z1, colore, [sopra])` | una scatola tra due angoli; `sopra`: il colore della faccia di sopra |
| `B:tetra(p1, p2, p3, p4, colore)` / `B:quad(p1, p2, p3, p4, colore, nx, ny, nz)` | un tetraedro / un quadrilatero piano visto dal lato `nx, ny, nz` |
| `B:build()` | la mesh (`mesh()`); i metodi restituiscono `B`, quindi si concatenano |

```lua
local casa = lib.builder()
  :box(-1, 0, -1, 1, 1.5, 1, 0xC0A080)
  :piece({ { -1.1, 1.5, -1.1 }, { 1.1, 1.5, -1.1 }, { 1.1, 1.5, 1.1 }, { -1.1, 1.5, 1.1 }, { 0, 2.4, 0 } },
         { { 1, 2, 5 }, { 2, 3, 5 }, { 3, 4, 5 }, { 4, 1, 5 }, { 1, 2, 3 }, { 1, 3, 4 } }, 0xA03020)
  :build()
```

Prova di tutto: `make test-gameapi` (bmhost, `tests/gameapi/cart.lua`: ogni funzione con
i suoi casi, anche i tasti con uno script) e `test_game_api` in QEMU.

## riff: la musica in pattern

`local R = require "riff"` (2026-10-06, guida completa in [RIFF.md](RIFF.md)): ritmi e
melodie scritti in una riga, come in TidalCycles e Strudel, suonati in tempo
dall'interrupt del suono (`play_at`) con gli strumenti pronti e i suoni del banco per
nome. Il tempo si conta in **cicli** (una battuta di quattro tempi; 2 s all'inizio).

```lua
local R = require "riff"

function _init()
  R.setcpm(30)
  R.play("drums", R.s "kick*4, ~ snare, hat*8")
  R.play("bass", R.note "<c2 a1 f1 g1>" :s "acid" :lpf(R.sine:range(300, 1800):slow(4)))
end

function _update()
  R.update()                      -- ogni fotogramma: le note dei prossimi istanti in coda
end
```

| Mini-notazione | Cosa fa |
|---|---|
| `a b c d` | una sequenza nel ciclo |
| `~` `-` | pausa |
| `[a b]` | un gruppo in una parte |
| `a, b` | insieme |
| `<a b>` | una per ciclo |
| `a*2`, `a/2` | più veloce, più lenta (anche `a*<2 4>`) |
| `a!3`, `a@3`, `a _ _` | ripetuta, più lunga |
| `a?`, `a?0.3` | a caso |
| `a(3,8,2)` | ritmo euclideo |
| `{a b c}%4` | polimetro |
| `a \| b` | una delle due, ogni ciclo |
| `kick:2` | variante (due semitoni sopra) |
| `kit:2`, `bd sd hh` | il tamburo 2 del kit (campioni, alla loro velocità); i tamburi del kit per nome, come in Strudel |

| Funzione | Descrizione |
|---|---|
| `R.s(p)`, `R.note(p)`, `R.n(p)`, `R.chord(p)` | strumenti, note (`c4` = 60, l'ottava 3 se manca), gradi di una scala, accordi (`"<Am F C G7>"`) |
| `R.seq`, `R.cat`, `R.stack`, `R.timecat`, `R.arrange`, `R.run(n)` | in sequenza, un ciclo ciascuno, insieme, con i pesi, sezioni di più cicli, `0..n-1` |
| `R.sine`, `R.cosine`, `R.saw`, `R.tri`, `R.square`, `R.rand`, `R.perlin`, `R.irand(n)`, `R.choose(…)` | segnali 0..1 |
| `:fast`, `:slow`, `:early`, `:late`, `:rev`, `:ply`, `:iter`, `:palindrome` | il tempo |
| `:every(n, f)`, `:lastOf`, `:sometimes(f)`, `:often`, `:rarely`, `:degradeBy(x)`, `:someCycles` | cambi ogni tanto, a caso |
| `:euclid(k, n, r)`, `:struct(p)`, `:mask(p)`, `:segment(n)`, `:chunk(n, f)`, `:linger(x)`, `:swing(n)` | struttura |
| `:off(t, f)`, `:superimpose(f)`, `:layer(…)`, `:jux(f)` | strati, sinistra e destra |
| `:add`, `:sub`, `:mul`, `+`, `:transpose`, `:scale("C:minor")`, `:arp("updown")`, `:range(a, b)` | note e numeri |
| `:s`, `:gain`, `:legato`, `:lpf`, `:hpf`, `:bpf`, `:res`, `:pan`, `:room`, `:delay`, `:attack`, `:decay`, `:sustain`, `:release`, `:shape`, `:vib`, `:fm`, `:raw`, `:crush`, `:coarse`, `:vowel`, `:chorus`, `:trem`, `:duck`, `:begin`, `:tone{…}` | il suono di ogni nota (unità di `tone()`; `:tone{curve = "fold", color = "pink"}` per il resto) |
| `R.play(nome, p)`, `R.stop(nome)`, `R.hush()`, `R.update()` | suonare (con un nome), fermare, da chiamare in `_update` |
| `R.setcps(x)`, `R.setcpm(x)`, `R.bpm(x)` | la velocità |
| `R.code(testo)` | codice dal vivo: le funzioni di riff sono globali, ogni globale con un pattern suona con quel nome, quelli di prima non più nominati si fermano; `true` o `nil` e l'errore |
| `R.bake(p, {cycles=, steps=16})`, `R.piece(t)` | il pattern come pezzo per il banco (la forma di `ai.music`), un pezzo di `ai.music` come pattern |
| `R.playing()`, `R.get([nome])`, `R.errors()`, `R.active()`, `R.voices(v, …)` | i nomi che suonano, i loro pattern, gli errori, dove sono scritte le note che suonano, le voci che può usare |

```lua
R.code [[
setcpm(28)
local verse = chord "<Am F C G>"
pad   = verse :s "pad" :room(.6) :gain(.7)
arp   = verse :s "pluck" :arp("updown") :fast(4)
drums = s "kick ~ ~ kick, ~ snare, hat*8?" :every(4, fast(2))
]]
```

```lua
R.code [[
beat  = s "bd ~ [~ bd] ~, ~ cp, hh*8" :crush(8)        -- i campioni del kit, 8 bit
vox   = note "<a3 f3>" :s "choir" :vowel("o") :chorus(.5)
]]
```

In **bm Code** Ctrl+Invio suona il codice (la scheda, o il blocco `riff.code [[ ]]` sotto
il cursore) e accende le parole delle note che suonano, Ctrl+. ferma; in **bm Sound** F7
(*Riff...*) suona una riga e Ctrl+Invio la mette nel banco come brano. Prove: `make
test-riff`, QEMU `test_riff_code`, `test_sound_riff`.

## bmnet: i giochi in rete

`local net = require "bmnet"` (2026-10-04): quello che fa il codice di rete di Overbit, per
ogni gioco. Le console si trovano sulla **LAN** (broadcast UDP) o su **internet** con un
relay (`tools/overbit_relay.py`: una stanza passa ogni pacchetto alle altre console, come un
broadcast); una **ospita** la partita e le altre **entrano**; si mandano **messaggi** (che
possono perdersi, o sicuri e in ordine) e, per i giochi d'azione, la partita va in
**lockstep**: ogni console fa girare tutto il gioco con lo stesso seme e viaggiano solo gli
input dei giocatori; un fotogramma gira quando gli input di tutti per quel fotogramma sono
arrivati (chi ospita li raccoglie e li manda a tutti). Il modello *Online 2D* dell'SDK è un
esempio completo; i pacchetti sono descritti in testa a `src/script/bmnet.lua`.

| Funzione | Descrizione |
|---|---|
| `net.open([opz])` | apre la rete: `game` (4 caratteri, il gioco e la sua versione: i pacchetti degli altri giochi non si vedono), `port` (47320, la stessa su tutte le console), `relay` (`"nome"` o `"nome:porta"`, 47310: su internet; senza, la LAN), `room` (4 caratteri, `"PLAY"`: le console di una stanza si vedono sul relay), `name` (il nome del giocatore). `true`, o `false` e il motivo (niente rete) |
| `net.close()` | esce: gli altri lo sanno (nella partita chi ospita dà il posto a nessuno, l'input del posto diventa `false`; se esce chi ospita la partita finisce per tutti), il socket si chiude. Da chiamare anche da `_leave()` |
| `net.host([opz])` | questa console ospita una partita: `max` giocatori (2–8, 4), `info` (una riga che gli altri vedono nell'elenco). È il posto 1 |
| `net.hosts()` | le partite trovate negli ultimi 3 secondi: `{id, name, players, max, info}`, per id |
| `net.join(id)` | chiede un posto alla console `id`: l'evento `"joined"` quando dice sì, `"refused"` se è piena o è già cominciata |
| `net.peers()` | le console della stanza o della partita: `{id, name, seat, me}`, per posto |
| `net.start([opz])` | (chi ospita) comincia la partita con chi c'è: `seed` (lo stesso caso su tutte le console; uno a caso se manca), `delay` (fotogrammi tra un tasto e il suo fotogramma: 4 sulla LAN, 7 col relay), `data` (una stringa per tutti: le opzioni scelte). Tutti ricevono l'evento `"start"` (anche chi ospita, al `net.update()` dopo) |
| `net.update()` | una volta in ogni `_update`: i pacchetti arrivati, gli annunci della lobby, i messaggi sicuri da ripetere. Restituisce gli **eventi** del fotogramma, una lista di `{type = ...}`: `"join"` (`id, name, seat`), `"leave"` (`seat, id`), `"joined"` (`seat`), `"refused"`, `"start"` (`seed, seat, seats, host, data`), `"msg"` (`from, data, sure`), `"lost"` (`why`: chi ospita non c'è più), `"desync"` (`frame`), `"error"` (`text`) |
| `net.send(dati, [a])` | un messaggio (una stringa fino a 900 byte) a tutti o alla console `a` (un id): può perdersi o arrivare dopo uno più nuovo (le posizioni) |
| `net.post(dati, [a])` | un messaggio **sicuro**: ripetuto finché non arriva, e ogni console riceve quelli di un'altra nell'ordine in cui sono partiti (la chat, un turno, una scelta) |
| `net.input(v)` | (lockstep) il mio input per il fotogramma `net.frame + delay`: un intero di 32 bit (`net.pad(1)`). Una volta in ogni `_update` della partita, prima di `net.frames()` |
| `net.frames()` | i fotogrammi della partita che possono girare ora: `for f, inputs in net.frames() do ... end`, `inputs[posto]` l'input di quel posto (`false` quando il suo giocatore è uscito). Uno, due quando la console è indietro; nessuno se manca un input (`net.stall`: secondi dall'ultimo) |
| `net.check(hash)` | un numero che riassume il gioco dopo il fotogramma appena girato (posizioni, punti): le console lo confrontano e una differenza è l'evento `"desync"`. Ogni secondo basta |
| `net.pad([p])` / `net.unpad(v)` | i 16 pulsanti di `pad(p)` e la levetta sinistra (8 bit per asse) in 32 bit / di nuovo pulsanti, `x`, `y` (−1..1) |
| `net.held(v, pulsante)` | un pulsante (`"a"`, `"left"`... come `pad()`) premuto in un input di `net.pad` |

Campi: `net.state` (`"off"`, `"lobby"`, `"hosting"`, `"joining"`, `"joined"`, `"playing"`,
`"lost"`), `net.me` (il mio id), `net.seat`, `net.seats` (i posti della partita),
`net.is_host`, `net.frame` (il prossimo fotogramma), `net.seed`, `net.delay`, `net.data`,
`net.error`.

**Le regole del lockstep.** Il gioco della partita dipende **solo** dagli input dei
fotogrammi e dal seme: numeri a caso con `lib.rng(seme)` (mai `math.random`, `time()` o
`stat()`), niente che venga dalla console (la camera, la qualità) dentro la simulazione,
niente stato del gioco cambiato in `_draw`. Il disegno può essere diverso su ogni console.
Due giocatori sulla stessa console in una partita in rete mettono i loro input nello stesso
numero (16 bit ciascuno). `online(true)` lo accende `bmnet` all'inizio della partita: PS
chiede prima di uscire e chiama `_leave()`.

```lua
local net = require "bmnet"
local lib = require "bmlib"
local rng

function _init() net.open({ game = "MYG1" }) end
function _leave() net.close() end

function _update()
  for _, e in ipairs(net.update()) do
    if e.type == "start" then rng = lib.rng(e.seed) end
  end
  if net.state == "lobby" and btnp("a") then net.host({ max = 2 }) end
  if net.state == "hosting" and btnp("start") then net.start() end
  if net.state == "playing" then
    net.input(net.pad(1))
    for _, inputs in net.frames() do passo(inputs) end   -- il gioco, uguale ovunque
  end
end
```

**Provarlo sul PC.** Due `bmhost` sullo stesso PC sono due console: `BMHOST_NET_ID=0` e
`BMHOST_NET_ID=1`, con `--realtime`; il relay gira con `tools/overbit_relay.py --port N`.
`make test-bmnet` lo fa: lobby, messaggi sicuri con un quinto dei pacchetti persi
(`loss = 0.2` in `net.open`, solo per le prove), una partita in lockstep uguale sulle due
console, un giocatore che esce; sulla LAN e attraverso il relay.

## Budget e consigli

- 60 fps = **16,7 ms** per fotogramma per `_update` + `_draw` + la copia sullo schermo.
  In alto a sinistra nella demo, `stat(1)` mostra quanto ne usa la cartuccia.
- Il **dev kit**: l'overlay delle prestazioni sopra qualsiasi gioco, in alto a destra.
  Si accende da Settings > Screen and sound > "Performance overlay" (Off, Simple, Detailed,
  Functions: resta salvato), con F11 sulla tastiera (tasto di sistema, anche negli
  strumenti; era F3), con `p` dalla seriale o con `devkit(modo)` dal gioco: una volta la
  pagina semplice, di nuovo quella dettagliata, di nuovo quella delle funzioni, di nuovo
  spento. F11, `p` e `devkit()` valgono per la partita:
  ogni gioco parte (e riprende) come dice Settings. La pagina semplice:

      60fps 6.1ms ^7.5      fotogrammi al secondo; ms di _update + _draw: media e,
                            dopo ^, il massimo dell'ultimo secondo
      lua 9k ^10k           istruzioni Lua di un fotogramma (migliaia): media, massimo
      ram 612k ^700k        memoria: il Lua di adesso più i dati (sheet, mappa,
                            modelli, suoni, z-buffer) e, dopo ^, il massimo
      1234 tokens           i token del codice (code_tokens)

  sotto, il tempo degli ultimi 64 fotogrammi: la cima è 16,7 ms; verde sotto metà,
  giallo fino a 16,7, rosso oltre (il fotogramma salta). La pagina dettagliata aggiunge
  l'ultimo fotogramma nelle sue fasi e il 3D:

      update 5.8ms x2       tutti i suoi _update (x2: ne sono girati due, frameskip)
      draw   17.4ms         il suo _draw
      3D     13.3ms         le chiamate 3D (stat(6))
      gpu 9.1ms 2 jobs      il lavoro della GPU (con l'ARM: px, i pixel che ha disegnato)
      tri 3620 vtx 5699     triangoli disegnati, vertici messi
      bm3d 2.1 GPU          il driver 3D (ARM, GPU, GPU+AA)
      quality HIGH          le righe del gioco (devinfo)

  La pagina delle **funzioni** (R14) mostra sotto quella semplice le dieci funzioni che
  costano di più, in ms per fotogramma, la media dell'ultimo secondo (60 fotogrammi):

      function        self   all
      draw_scene      2.28  6.43    una funzione Lua: il suo tempo e con quelle che chiama
      map             0.94  0.94    in azzurro le funzioni della console (spr, map, il 3D...)
      _draw           0.03  7.70    le callback hanno il loro nome

  `self` è il tempo della funzione stessa, `all` quello con le funzioni che chiama: una
  funzione con `all` grande e `self` piccolo passa il tempo nelle altre. Le funzioni della
  console sono misurate esattamente, quelle Lua a pezzi di 1000 istruzioni (abbastanza
  per vedere dove va il tempo). Il profiler gira solo con questa pagina o `profile(true)`;
  spento non costa niente. Dal codice: `profile()` (sotto).

- Il **report dell'ultima sessione** (2026-10-10): a fine partita il dev kit scrive nei
  reports un file solo per gioco, `reports/<branch>/session_<gioco>_<scheda>.txt`, che la
  partita dopo dello stesso gioco **sostituisce**. Dentro: schermo, fotogrammi e fps, tempo
  medio e massimo di `_update` + `_draw`, memoria, e il tempo **da un'immagine alla
  successiva** in sei classi (fino a 18 ms, 25, 34, 50, 100, oltre) più i 12 fotogrammi più
  lunghi con le loro parti (`cpu`, `upd`, `draw`, 3D, copia e `outside`: il tempo che non è di
  nessuno di loro, la scheda SD, la GPU, la macchina) e le righe `devinfo()` del gioco com'erano a fine partita ("the game says:"; Yharnam: i chunk che la vista ha dovuto aspettare). Così un blocco di mezzo secondo non
  si perde più dopo i 64 fotogrammi dell'overlay. Parte dal menu quando c'è rete. In
  `bm/config.txt`: `session_report=0` mai, `=1` sempre; senza la riga, solo se c'è
  `github_token`.

  Sugli schermi grandi è più grande (×2 da 1280 di larghezza, ×3 a 1920). Dal codice:
  `stat(1)`, `stat(2)`, `stat(6)`, `stat(10)`, `stat(11)`–`stat(15)`, `devkit()`,
  `profile()`. Il
  limite è di 20 milioni di istruzioni per chiamata. Il **dev kit dell'SDK** (F1 due volte) ha gli stessi
  numeri per il progetto: token, le funzioni più grandi, la memoria dei dati, il file
  contro gli 8 MiB di un `.b16` e i numeri dell'ultima prova (F5). A fine partita la
  seriale scrive anche la riga `dev kit: Lua peak ... KiB, data ... KiB, busiest frame
  ...k instructions, ... frames over 16.7 ms, ... tokens`.
- Il disegno è in C: una chiamata `spr` o `rectfill` costa pochi microsecondi, ma
  ogni chiamata da Lua ha un costo fisso. Ordini di grandezza sul Pi (docs/STRESS.md):
  ~1800 sprite 16×16 chiamati da Lua a 60 fps, ~4500 dal C; ~1200 triangoli 3D.
- Evita di creare tabelle nuove in ogni fotogramma se non serve (meno lavoro al GC).
- Il testo è a celle 8×16: per scritte ben allineate usa x multipli di 8 e y di 16.
