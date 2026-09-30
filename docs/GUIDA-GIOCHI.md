# Creare un gioco `.bm`

Guida pratica: dal primo file Lua alla cartuccia sulla SD, con sprite, mappe, modelli 3D,
suono e salvataggi. Il riferimento completo di ogni funzione è in [API.md](API.md).

## 0. Sulla console: l'editor

Senza PC: nel menu delle cartucce l'ultimo elemento è **bm33 editor** (freccia su dal primo; anche `e` dal
monitor). Con una tastiera USB (e se vuoi un gamepad Bluetooth per disegnare):

| Tasto | Pagina |
|---|---|
| **F1** | codice (Ctrl+Z annulla, Ctrl+K taglia riga, Ctrl+D duplica) |
| **F2** | sprite: frecce, spazio disegna, `x` preleva il colore, `f` riempie, `,` `.` (o `è` `+`, o `[` `]`) colore, Tab passa al foglio, `z` 8×8/16×16, `h`/`v` specchia, `u` annulla |
| **F3** | mappa: frecce, spazio piazza la tile, `x` la preleva, `f` riempie, `,` `.` (o `è` `+`) tile, Tab sceglie la tile |
| **F12** (tenuto premuto) | l'elenco dei tasti della pagina |
| **Esc** | menu: nuovo, apri, salva, salva come, titolo, autore, risoluzione, esci |
| **Ctrl+S** / **Ctrl+R** (F5) | salva / prova il gioco (poi si torna all'editor) |

Se il gioco si ferma con un errore, l'editor torna sulla riga in rosso (Ctrl+G la
ritrova). I giochi si salvano in `/carts` con un nome 8.3 (es. `MIOGIOCO.BM`) e
compaiono nel menu. Tutto il resto di questa guida vale anche per l'editor.

## 1. Com'è fatta una cartuccia

Una cartuccia `.bm` è un unico file che contiene:

| Parte | Da dove arriva | Obbligatoria |
|---|---|---|
| codice Lua (`main.lua`) | `--lua` | sì |
| sprite sheet (PNG) | `--sheet` | no |
| mappa a tile (CSV) | `--map` | no |
| copertina per il menu (PNG) | `--cover` | no |

La crea `scripts/mkb33.py` (solo libreria standard di Python, nessuna dipendenza).
Risoluzione: **640×360** (predefinita) oppure **320×180** con `--res 320x180` (pixel più
grossi, stile 16 bit, e più tempo per fotogramma). Colori: `0xRRGGBB`, lo schermo è a
16 bit (RGB565).

## 2. Il gioco più piccolo

`carts/palla/main.lua`:

```lua
local x, y, c = 320, 180, 0xFFD050

function _init()                -- una volta, all'avvio
end

function _update()              -- 60 volte al secondo: logica e input
  if btn(0) then x = x - 3 end  -- 0 sinistra, 1 destra, 2 su, 3 giù
  if btn(1) then x = x + 3 end
  if btn(2) then y = y - 3 end
  if btn(3) then y = y + 3 end
  if btnp(4) then               -- A, solo nel fotogramma in cui viene premuto
    c = rgb(math.random(255), math.random(255), math.random(255))
    note(0, 660, 80, SQUARE, 100)
  end
end

function _draw()                -- 60 volte al secondo: disegno
  cls(0x101828)
  circfill(x, y, 20, c)
  print("frecce muovono, A cambia colore", 16, 16, 0xFFFFFF)
end
```

Pulsanti: `btn(i)` finché è premuto, `btnp(i)` solo al momento della pressione.

| `i` | Tasto | DS4 | Tastiera |
|---|---|---|---|
| 0–3 | sinistra, destra, su, giù | croce / levetta | frecce o WASD |
| 4 | A | croce | spazio, Z, J |
| 5 | B | cerchio | X, K |
| 6 | X | quadrato | C, L |
| 7 | Y | triangolo | V, I |

Esc, Start+Select o il tasto PS tornano al menu, e il gioco resta **sospeso** in memoria
(A sulla sua copertina lo riprende). `quit()` invece chiude davvero la cartuccia.

**Più giocatori.** Con due o più controller Bluetooth (abbinati dal monitor con `T`: il
primo è il giocatore 1, il secondo il giocatore 2...) ogni giocatore ha i suoi tasti:

```lua
local n = players()                       -- quanti controller ci sono
if btn(2, 1) then p1.y = p1.y - 3 end     -- su, giocatore 1
if btn(2, 2) then p2.y = p2.y - 3 end     -- su, giocatore 2
local x, y = stick(1)                     -- levetta del giocatore 1, da -1 a 1
```

`btn(i)` senza giocatore risponde a tutti i controller: va bene per i menu e i giochi a
un giocatore. La tastiera USB è il primo giocatore che non ha un pad.

## 3. Impacchettare e provare

```sh
cd ~/bm33
python3 scripts/mkb33.py -o palla.bm --lua carts/palla/main.lua --title "Palla" --author "io"
```

Opzioni: `--sheet sprite.png`, `--map mappa.csv`, `--cover copertina.png`,
`--res 320x180`.

**Sul Pi:** copia il file nella cartella `carts/` della SD.

```sh
sudo mount -t drvfs D: /mnt/d
sudo cp palla.bm /mnt/d/carts/
sync && sudo umount /mnt/d
```

La cartuccia compare nel menu (se il Pi è già acceso, **R** nel menu rilegge la SD).
Uscendo, la riga in fondo al menu mostra fps e millisecondi di `_update` + `_draw`: il
budget è **16,7 ms** per fotogramma.

**Nella build del repository:** metti il gioco in `carts/<nome>/main.lua`, aggiungi
`<nome>` a `GAMES` nel `Makefile` e una riga `title_<nome> := Titolo` (più
`res_<nome> := 320x180` se serve). `make` la crea in `build/carts/<nome>.bm` e
`make sdcard` la copia in `dist/carts/`. Nella stessa cartella vengono presi da soli
`sheet.png`, `map.csv` e `cover.png`, se ci sono.

**Errori:** se il codice Lua sbaglia, il gioco si ferma e il messaggio con il numero di
riga appare sulla console.

## 4. Sprite 2D

Gli sprite stanno in un **PNG** (RGB o RGBA a 8 bit, lati multipli di 8, fino a
2048×2048), disegnato con qualsiasi editor (Aseprite, LibreSprite, Piskel, GIMP). I pixel
con alpha sotto 128 sono **trasparenti**.

Il foglio è diviso in **celle 8×8**, numerate da sinistra a destra e dall'alto in basso:
con un foglio largo 128 px ci sono 16 celle per riga, e la cella `n` sta in colonna
`n % 16`, riga `n // 16`.

```lua
spr(0, x, y)                   -- cella 0 (8x8)
spr(2, x, y, 2, 2)             -- 2x2 celle a partire dalla 2: uno sprite 16x16
spr(2, x, y, 2, 2, true)       -- specchiato in orizzontale (e poi in verticale)
sspr(sx, sy, w, h, x, y)       -- un rettangolo qualsiasi del foglio, in pixel
```

Con `sset(x, y, colore)` / `sget(x, y)` si legge e scrive il foglio dal codice: si
possono disegnare gli sprite senza PNG (Star Shooter li scrive come stringhe in
`carts/shooter/main.lua`) o modificarli durante il gioco.

Animazione: si cambia cella ogni tot fotogrammi, per esempio
`spr(base + (frame // 8) % 2 * 2, x, y, 2, 2)`.

## 5. Mappe a tile

La mappa è una griglia di numeri di cella dello sprite sheet (0 = vuoto), in un **CSV**
(una riga di testo per riga della mappa, le righe che iniziano con `#` sono commenti):

```
# mappa 4x3
1,1,1,1
1,0,0,1
1,1,1,1
```

```lua
camera(cam_x, cam_y)                                  -- scorrimento
map(cam_x // 8, cam_y // 8, cam_x // 8 * 8, cam_y // 8 * 8, 81, 46)   -- solo la parte visibile
local t = mget(px // 8, py // 8)                      -- la cella sotto un punto (collisioni)
mset(cx, cy, 5)                                       -- cambiare una cella (porte, oggetti presi)
```

Senza `--map` la mappa è 256×256 vuota e si riempie con `mset`. Una mappa grande
(Hunter's Night: 256×256 celle, 2048×2048 pixel) conviene generarla con uno script:
vedi `carts/hunt/mkassets.py`, che produce `sheet.png` e `map.csv`.

Un'idea che semplifica le collisioni: tenere le tile solide in un intervallo di numeri
(in Hunter's Night le celle 32–63), così basta `v >= 32 and v < 64`.

## 6. Modelli 3D

Il 3D è software (in C sull'ARM): triangoli con z-buffer, luce per faccia, nebbia,
texture. Budget indicativo: circa 1200 triangoli disegnati a 60 fps.

**Forme pronte:** `mesh_cube(colore)`, `mesh_sphere(anelli, segmenti, c1, c2)`.

**Modelli propri** con `mesh(vertici, facce, [uv])`, di solito creati in `_init`:

```lua
local piramide = mesh(
  { 0,1,0,  -1,-1,-1,  1,-1,-1,  1,-1,1,  -1,-1,1 },           -- x,y,z di ogni vertice
  { 1,2,3,0xE04040,  1,3,4,0xC03030,  1,4,5,0xE04040,           -- a,b,c,colore (indici da 1)
    1,5,2,0xC03030,  2,4,3,0x802020,  2,5,4,0x802020 })

function _draw()
  cls(0)
  zclear()                                   -- a ogni fotogramma, prima di draw3d
  camera3d(0, 1, -5, 0, 0, 60)               -- posizione, yaw, pitch, fov (e roll)
  light3d(-0.4, 0.8, -0.5, 0.3)              -- direzione della luce, luce ambiente
  draw3d(piramide, 0, 0, 0, 0, time(), 0, 1) -- posizione, rotazione x/y/z, scala
end
```

- Le facce vanno date in senso **antiorario viste da fuori** (quelle girate dall'altra
  parte non vengono disegnate). Astro Wing (`carts/astrowing/main.lua`) usa una piccola
  funzione che costruisce modelli da pezzi convessi e gira le facce da sola: si può
  copiare.
- **Texture:** con la terza tabella `uv` (6 numeri per faccia: u,v dei tre vertici, in
  pixel dello sprite sheet) le facce con colore `-1` prendono l'immagine dal foglio, con
  la prospettiva corretta.
- **Nebbia:** `fog3d(colore, vicino, lontano)` sfuma gli oggetti lontani.
- **Proiezione:** `project3d(x, y, z)` dà la posizione sullo schermo di un punto 3D, per
  disegnarci sopra in 2D (orizzonte, mirini, nomi).
- 2D e 3D si mescolano: sfondo con `rectfill`/`map`, modelli con `draw3d`, HUD con
  `print` alla fine.

**Da Blender:** per ora i modelli si scrivono (o si generano) come tabelle Lua; un
convertitore da `.obj` è una delle prossime cose che si possono aggiungere.

## 7. Suono

Otto voci di sintesi (0–7) con quattro forme d'onda (`SQUARE`, `TRIANGLE`, `SAW`,
`NOISE`) e inviluppo ADSR; esce dall'HDMI.

```lua
note(0, 880, 60, SQUARE, 100)        -- voce, Hz, durata in ms, forma, volume 0-255
envelope(2, 0, 60, 0, 30)            -- attack, decay, sustain, release della voce 2
note(2, 2500, 300, NOISE, 130)       -- un'esplosione che si smorza
freq(0, 440)                         -- cambia nota senza ripartire (glissandi)
```

Buona abitudine: una voce per tipo di suono (arma, colpi, musica), così un effetto non
interrompe l'altro. Le piccole melodie dei giochi demo sono una funzione `jingle` di
dieci righe (in `carts/pong/main.lua`).

## 8. Luci (scene al buio)

```lua
cls(0); map(...); spr(...)                   -- la scena
light_begin(0x0A0A16)                         -- luce di fondo: notte
light(lx, ly, 50, 0xFFB060)                   -- un lampione (coordinate del mondo)
light(px, py, 40, 0xFFC888, 0.9)              -- la lanterna del giocatore
light_end()                                   -- applica la luce a tutto ciò che è sopra
print("vita", 4, 4, 0xFFFFFF)                 -- l'HUD dopo: resta a piena luce
```

Una fiamma che tremola: raggio moltiplicato per `0.95 + math.random() * 0.1`. Esempio
completo: `carts/hunt/main.lua`.

## 9. Salvataggi

```lua
function _init()
  local d = saved()                  -- la tabella salvata l'ultima volta, o nil
  if d then record = d.record end
end
-- a fine partita (non a ogni fotogramma: scrivere sulla SD richiede qualche ms)
save({ record = record })
```

Numeri, stringhe, booleani e tabelle, fino a 32 KiB, in `/bm33/save/` sulla SD.

## 10. Copertina

`--cover copertina.png`: un PNG di qualsiasi misura, ritagliato a 16:10 e ridotto a
128×80, stampato sulla "scheda" del gioco nel menu. Le copertine dei giochi demo sono
disegnate da `scripts/mkcovers.py`. Senza copertina il menu stampa il titolo.

## 11. Consigli

- **Prestazioni:** il costo sta quasi tutto nel Lua di `_update`/`_draw`; il disegno è
  in C. Evitare di creare tabelle nuove a ogni fotogramma nei cicli caldi; aggiornare
  solo i nemici vicini alla camera; disegnare solo la parte visibile della mappa.
- **Numeri:** Lua ha interi e decimali; per le posizioni sullo schermo usare
  `math.floor` o `//` se servono pixel interi.
- **Casualità:** `math.randomseed(stat(3))` quando il giocatore preme un tasto
  (il numero del fotogramma cambia a ogni partita).
- **Debug:** `log(...)` scrive sulla console seriale; `stat(1)` è il tempo dell'ultimo
  fotogramma in ms, `stat(2)` gli fps.

## 12. Esempi da cui partire

| Gioco | Cosa mostra |
|---|---|
| `carts/pong` | il più semplice: forme, input, suoni, salvataggio |
| `carts/snake` | griglia, tempo di gioco, record |
| `carts/shooter` | sprite scritti nel codice con `sset`, ondate, particelle |
| `carts/astrowing` | 3D: modelli, nebbia, camera che si inclina, orizzonte |
| `carts/hunt` | 320×180, mappa 2048×2048 generata, luci, combattimento, boss |
| `carts/demo` | sprite sheet PNG e mappa CSV veri e propri |
| `carts/kitchen` | gioco grande: sorgenti in più file uniti da `build.py`, 3D con mesh costruite in codice, 1–4 giocatori (`btn(i, p)`, `players()`), salvataggi, e un simulatore host (`tests/kitchen/sim.lua`) che gioca da solo per trovare errori e misurare il costo di ogni frame |
| `carts/titan` | sprite grandi pre-renderizzati (un modello 3D fatto in Python diventa pixel art a strati: un frame, tante combinazioni di equipaggiamento), sheet grande con palette (`--sheet8`), parallasse, stati di un picchiaduro con hitbox per frame, CPU avversaria |
