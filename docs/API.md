# Cartucce native `.bm`: API e prima cartuccia

> Guida pratica passo per passo (sprite, mappe, modelli 3D, suono, luci, salvataggi):
> [GUIDA-GIOCHI.md](GUIDA-GIOCHI.md).

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

   **Copertina** (facoltativa): `--cover copertina.png`, un PNG di qualsiasi misura
   (viene ritagliato a 16:10 e ridotto a 128×80) che il menu stampa sulla scheda. Nella
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

Esc (tastiera) o Start+Select (gamepad) chiudono la cartuccia e tornano al menu.
Se c'è un errore Lua, la cartuccia si ferma e l'errore, con la riga, appare sulla console.

## Struttura

| Funzione | Quando |
|---|---|
| `_init()` | una volta, dopo il caricamento |
| `_update()` | ogni fotogramma (60 Hz), prima di `_draw` |
| `_draw()` | ogni fotogramma, dopo `_update` |

Globali: `SCREEN_W` e `SCREEN_H` (640 e 360; 480 e 270 con `--res 480x270`; 320 e 180 con
`--res 320x180`).
Lo schermo **non** viene cancellato da solo: di solito `_draw` comincia con `cls()`.

Limiti: un errore o un ciclo infinito (oltre **20 milioni di istruzioni** Lua in un
fotogramma) ferma la cartuccia senza bloccare la console. Sandbox: niente `io`, `os`,
`load`, `dofile`, `require`; ci sono `string`, `table`, `math`, `utf8`, `coroutine`.

## Colori

I colori sono interi `0xRRGGBB` (es. `0xFF8000` arancione) o `rgb(r, g, b)` con valori
0–255. Lo schermo li converte in RGB565 (5 bit rosso, 6 verde, 5 blu).

## Riferimento

Le coordinate sono in pixel, (0,0) in alto a sinistra; `w` e `h` sono larghezza e altezza.

### Schermo e forme

| Funzione | Descrizione |
|---|---|
| `cls([c])` | riempie lo schermo (nero se `c` manca) |
| `pset(x, y, c)` / `pget(x, y)` | scrive / legge un pixel (`pget` dà `0xRRGGBB` o `nil` fuori schermo) |
| `line(x0, y0, x1, y1, c)` | linea |
| `rect(x, y, w, h, c)` / `rectfill(x, y, w, h, c)` | rettangolo vuoto / pieno |
| `circ(x, y, r, c)` / `circfill(x, y, r, c)` | cerchio vuoto / pieno |
| `tri(x0, y0, x1, y1, x2, y2, c, [c1, c2])` | triangolo pieno; con tre colori (uno per vertice) il colore sfuma da un angolo all'altro (Gouraud, con dithering) |
| `print(testo, x, y, [c, scala])` | testo con il font 8×16 (bianco se `c` manca), ingrandito `scala` volte (1–8: 2 = caratteri 16×32); restituisce la x dopo l'ultimo carattere |
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
| `sspr(sx, sy, sw, sh, dx, dy, [flip_x, flip_y])` | copia un rettangolo qualsiasi dello sheet |
| `sget(x, y)` / `sset(x, y, [c])` | legge / scrive un pixel dello sheet (`nil` = trasparente) |
| `map(mx, my, [x, y, mw, mh])` | disegna la mappa dalla cella (mx, my), mw×mh celle, a (x, y) |
| `mget(mx, my)` / `mset(mx, my, n)` | legge / scrive una cella della mappa (0 = vuota) |

La mappa viene da `--map mappa.csv` (una riga di numeri separati da virgole per riga
della mappa; ogni numero è una cella dello sheet).

### Input

| Funzione | Descrizione |
|---|---|
| `btn(i, [p])` | `true` finché il tasto è premuto; senza `p` da **qualsiasi** controller, con `p` = 1–4 solo da quello del giocatore `p` |
| `btnp(i, [p])` | `true` solo nel fotogramma in cui viene premuto (stesso `p`) |
| `players()` | quanti giocatori hanno un controller (almeno 1) e, come secondo valore, quali: bit `n` = giocatore `n+1` (es. `3, 7` = giocatori 1, 2 e 3) |
| `stick([p])` | la levetta sinistra del giocatore `p`: `x, y` tra −1 e 1 (x verso destra, y verso il basso), con zona morta; con la tastiera o un pad senza levetta vale la croce (8 direzioni). Senza `p`: quella spinta di più |

**Più giocatori (M16).** Il controller Bluetooth *n* è il giocatore *n* (abbinati dal monitor
con `T`, uno alla volta: ognuno prende il primo posto libero e la sua luce il colore del
giocatore: 1 blu, 2 rosso, 3 verde, 4 rosa). La tastiera o il gamepad USB e la seriale sono
il primo giocatore senza pad (senza pad Bluetooth: il giocatore 1); la tastiera Bluetooth
(M28) è un giocatore a sé, il successivo senza pad (il primo, se non c'è niente di USB).
I giochi a un giocatore
usano `btn(i)` senza `p` e funzionano con qualsiasi controller; un gioco a più giocatori
chiede `btn(i, p)` per ciascuno (esempio: `carts/pong`, modalità 2 giocatori).

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

### Tempo e sistema

| Funzione | Descrizione |
|---|---|
| `time()` | secondi dall'avvio della cartuccia (con decimali) |
| `stat(n)` | 0 KiB usati da Lua, 1 ms di CPU dell'ultimo fotogramma, 2 fps, 3 numero del fotogramma, 4 triangoli 3D, 5 pixel 3D |
| `log(...)` | scrive nel log del kernel (seriale e console), non sullo schermo del gioco |
| `quit()` | chiude la cartuccia alla fine del fotogramma |

Numeri casuali: `math.random`. Per partite diverse a ogni avvio, inizializza il
generatore quando il giocatore preme un tasto: `math.randomseed(stat(3))`.

### Salvataggi

| Funzione | Descrizione |
|---|---|
| `save(t)` | salva la tabella `t` sulla SD; `true`, oppure `false` e il motivo (niente SD, scheda piena...) |
| `saved()` | la tabella salvata l'ultima volta, oppure `nil` |

Ogni cartuccia ha **un** salvataggio, in `/bm/save/XXXXXXXX.SAV` sulla SD (il nome
dipende da titolo e autore: cambiandoli si riparte da zero). La tabella può contenere
numeri, stringhe, booleani e altre tabelle (niente funzioni, al massimo 32 KiB).
Scrivere sulla SD richiede qualche millisecondo: chiama `save()` in momenti come la fine
della partita, non a ogni fotogramma. Esempio (record di Snake):

```lua
function _init()
  local data = saved()
  if data then best = data.best end
end
-- a fine partita
if score > best then best = score; save({ best = best }) end
```

### Suono

Otto voci (0–7) di sintesi (nate dall'APU della s32 di lua32): forme d'onda `SQUARE`,
`TRIANGLE`, `SAW`, `NOISE`, inviluppo ADSR, uscita HDMI a 48 kHz (dagli altoparlanti
del monitor). Il suono è generato in un interrupt: non costa nulla al tuo `_update`.

| Funzione | Descrizione |
|---|---|
| `note(v, hz, [ms], [forma], [vol])` | suona una nota sulla voce `v` (riparte da capo l'inviluppo); con `ms` si spegne da sola, senza resta accesa fino a `noteoff(v)`. `vol` 0–255 (predefinito 128) |
| `noteoff(v)` | rilascia la nota (parte la fase di release) |
| `freq(v, hz)` | cambia l'altezza senza ripartire: glissandi, vibrato, sirene |
| `envelope(v, a, d, s, r)` | inviluppo della voce: attack, decay e release sono tempi 0–255 (0 = istantaneo, 255 = 2 s), sustain è un livello 0–255. Predefinito `1, 0, 255, 10` |
| `duty(v, d)` | larghezza dell'onda quadra, 0–255 (128 = 50%; 32–64 suona più "nasale") |
| `playing(v)` | `true` finché la voce suona (release compreso) |
| `apu(v, reg, [valore])` | legge o scrive un registro grezzo della voce (16 byte per voce: `src/audio/synth.h`) |

Forma e volume restano quelli dell'ultima nota della voce, quindi basta darli una volta.
All'avvio e all'uscita della cartuccia le voci si spengono e tornano ai valori
predefiniti. Più voci forti insieme si sommano e possono saturare: tieni i volumi
intorno a 100–130. Esempi:

```lua
note(0, 880, 60, SQUARE, 100)          -- "blip" di 60 ms
envelope(2, 0, 60, 0, 30)              -- esplosione che si smorza...
note(2, 2500, 300, NOISE, 130)         -- ...con il rumore
-- laser: la nota parte alta, poi in _update un passo di freq() per frame
note(1, 1300, 100, SQUARE, 70); laser = 6
if laser > 0 then laser = laser - 1; freq(1, 400 + laser * 150) end
```

Pong, Snake e Star Shooter in `carts/` usano effetti e piccole melodie (una funzione
`jingle` di 10 righe che suona una nota per volta sulla voce 3).

### Tastiera e file (per strumenti come l'editor)

| Funzione | Descrizione |
|---|---|
| `keyp()` | il prossimo tasto scritto: un carattere (`"a"`, `"\n"` Invio, `"\b"` Backspace, `"\t"`), un nome (`"up"`, `"down"`, `"left"`, `"right"`, `"home"`, `"end"`, `"pgup"`, `"pgdn"`, `"del"`, `"esc"`, `"f1"`…`"f5"`) o `"^s"` per Ctrl+S; `nil` se nessuno. Dalla prima chiamata la tastiera scrive e non fa più da gamepad per `btn()`, ed Esc non chiude la cartuccia (Start+Select e PS sì) |
| `ls([cartella])` | i file della SD: `{ {name=, size=, dir=}, … }` |
| `cart_load(percorso)` | apre un `.bm`: il suo sprite sheet e la sua mappa sostituiscono quelli della cartuccia che chiama; restituisce `{title, author, res, lua, sheet_w, sheet_h, map_w, map_h}` |
| `cart_new()` | sprite sheet e mappa vuoti (256×256) |
| `cart_save(percorso, {title, author, res, lua})` | scrive un `.bm` con il codice dato e lo sprite sheet, la mappa (e la copertina) correnti; nome 8.3, es. `"/carts/GIOCO.BM"` |
| `cart_run(percorso)` | esce, gioca quel file e poi riapre la cartuccia che l'ha chiesto, con `cart_arg()` = `{path=, error=, back=true}` (dal menu, "Open in the SDK": `back=false`) |

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

### 3D (software)

| Funzione | Descrizione |
|---|---|
| `mesh(v, f, [uv])` | mesh da tabelle: `v` = {x,y,z, x,y,z, …}, `f` = {a,b,c,colore, …} (indici da 1). Con `uv` (6 numeri per faccia: u,v dei tre vertici in pixel dello sprite sheet) le facce con colore `-1` hanno la **texture** dello sprite sheet (prospettiva corretta, i pixel trasparenti restano vuoti) |
| `mesh_sphere([r, segmenti, c1, c2])`, `mesh_cube([c])` | mesh pronte |
| `draw3d(m, x, y, z, [rx, ry, rz, scala, flag])` | disegna una mesh con z-buffer e luce per faccia. `flag`: 1 = senza z-buffer (né prova né scrittura: pavimenti e sfondi disegnati per primi, più veloci), 2 = senza luce (colori pieni), 4 = **liscia** (Gouraud: luce calcolata sui vertici e sfumata sulla faccia, con dithering; le facce che condividono gli stessi indici di vertice sembrano una superficie curva, per gli spigoli vivi usare vertici separati); si sommano |
| `camera3d(x, y, z, [yaw, pitch, fov, roll])` | camera (default a z = −5, fov 60°); `roll` inclina l'inquadratura (radianti) |
| `light3d(x, y, z, [ambiente])` | direzione della luce e luce ambiente (0–1) |
| `zclear()` | pulisce lo z-buffer (a ogni fotogramma, prima di `draw3d`) |
| `fog3d(colore, vicino, lontano)` | nebbia: le facce sfumano nel colore tra le due distanze; `fog3d()` la toglie |
| `lamp3d(i, x, y, z, raggio, [k])` | luce puntiforme `i` (1–4): le facce con il centro entro `raggio` diventano più chiare, fino a `k` in più (predefinito 1) al centro; `lamp3d(i)` la spegne, `lamp3d()` le spegne tutte. Con `light3d` ad ambiente basso fa scene al buio con lanterne |
| `project3d(x, y, z)` | punto del mondo → `sx, sy, profondità` sullo schermo (`nil` se è dietro la camera): per disegnare in 2D cose allineate al 3D (orizzonte, mirini, etichette) |

I triangoli che attraversano il piano vicino alla camera vengono tagliati, non scartati:
pavimenti e oggetti grandi restano interi anche quando passano accanto alla camera.
Esempio completo: `carts/astrowing` (volo in stile Star Fox: modelli costruiti in
codice, orizzonte con `project3d`, nebbia, esplosioni, boss).

## Budget e consigli

- 60 fps = **16,7 ms** per fotogramma per `_update` + `_draw` + la copia sullo schermo.
  In alto a sinistra nella demo, `stat(1)` mostra quanto ne usa la cartuccia.
- Il disegno è in C: una chiamata `spr` o `rectfill` costa pochi microsecondi, ma
  ogni chiamata da Lua ha un costo fisso. Ordini di grandezza sul Pi (docs/STRESS.md):
  ~1800 sprite 16×16 chiamati da Lua a 60 fps, ~4500 dal C; ~1200 triangoli 3D.
- Evita di creare tabelle nuove in ogni fotogramma se non serve (meno lavoro al GC).
- Il testo è a celle 8×16: per scritte ben allineate usa x multipli di 8 e y di 16.
