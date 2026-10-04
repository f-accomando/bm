# Creare un gioco `.bm`

Guida pratica: dal primo file Lua alla cartuccia sulla SD, con sprite, mappe, modelli 3D,
suono e salvataggi. Il riferimento completo di ogni funzione è in [API-IT.md](API-IT.md).
In inglese: [GAME-GUIDE.md](GAME-GUIDE.md) e [API-EN.md](API-EN.md).

## 0. Sulla console: l'editor

Senza PC: nel menu delle cartucce l'ultimo elemento è **bm editor** (freccia su dal primo; anche `e` dal
monitor). Con una tastiera USB (e se vuoi un gamepad Bluetooth per disegnare):

| Tasto | Pagina |
|---|---|
| **F1** | codice (Ctrl+Z annulla, Ctrl+K taglia riga, Ctrl+D duplica) |
| **F2** | sprite: frecce, spazio disegna, `x` preleva il colore, `f` riempie, `,` `.` (o `è` `+`, o `[` `]`) colore, Tab passa al foglio, `z` 8×8/16×16, `h`/`v` specchia, `u` annulla, `0`–`7` accendono e spengono i **flag** della tile (sotto: muro, piattaforma...) |
| **F3** | mappa: frecce, spazio piazza la tile, `x` la preleva, `f` riempie, `,` `.` (o `è` `+`) tile, Tab sceglie la tile, `l` il **livello** dopo, `o` solo quel livello; *New map layer* nel menu ne aggiunge uno |
| **F12** (tenuto premuto) | l'elenco dei tasti della pagina |
| **Esc** | menu: nuovo, apri, salva, salva come, titolo, autore, risoluzione, esci |
| **Ctrl+S** / **Ctrl+R** (F5) | salva / prova il gioco (poi si torna all'editor) |

**bm Code**: nella scheda Dev c'è anche **Code**, l'editor solo del codice: più cartucce
in tab, due pagine affiancate (F4), un font piccolo e nitido (6x12: tante righe), F5 prova
il gioco e torna sulla riga dell'errore, F1 mostra tutti i tasti. Salva solo il codice:
sprite e mappa della cartuccia restano come sono. Una riga `#entry: commenta questa
funzione #` seguita da Invio chiede all'assistente di farlo.

**Assistente** (M30): nella scheda Dev, **Assistant** risponde a domande come "come
faccio saltare il personaggio" o "attempt to call a nil value" con la spiegazione e il
codice pronto, e disegna la base di uno sprite ("slime rosso", "moneta", "tile di
erba"). Per ora si usa da solo; poi si aprirà con F6 dentro l'editor.

Se il gioco si ferma con un errore, l'editor torna sulla riga in rosso (Ctrl+G la
ritrova). I giochi si salvano in `/carts` con un nome 8.3 (es. `MIOGIOCO.BM`) e
compaiono nel menu. Tutto il resto di questa guida vale anche per l'editor.

**Sul PC**, per i modelli 3D e la pixel art: **bm Studio**; per scheletri, animazioni e
sprite pre-renderizzati: **bm Animator** ([sdk/README.md](../sdk/README.md)). Sono pagine
web che aprono e salvano i `.bm` (anche direttamente sulla SD). L'editor della console,
quando salva, tiene i modelli e le animazioni. **Sulla console**, nella scheda Dev, ci
sono **bm Studio** e **bm Animator** con gli stessi nomi (X sulla copertina di un gioco,
*Open in bm Studio* / *Open in bm Animator*): costruiscono a blocchi e tessere, scelgono e
spostano facce e angoli, dipingono sul modello, fanno scheletri, animazioni e sprite,
salvano e provano il gioco, con la tastiera o il gamepad.

## 1. Com'è fatta una cartuccia

Una cartuccia `.bm` è un unico file che contiene:

| Parte | Da dove arriva | Obbligatoria |
|---|---|---|
| codice Lua (`main.lua`) | `--lua` | sì |
| sprite sheet (PNG) | `--sheet` | no |
| mappa a tile (CSV), uno per livello | `--map` (`--map nome=file.csv` per ogni livello in più) | no |
| flag delle tile (muro, piattaforma, scala...) | `--flags` | no |
| zone con nome dello sheet (sprite e animazioni) | `--sprites` | no |
| copertina per il menu (PNG) | `--cover` | no |

La crea `scripts/mkbm.py` (solo libreria standard di Python, nessuna dipendenza).
Risoluzione: **640×360** (predefinita), **480×270** con `--res 480x270` (il compromesso
per il 3D con texture), **320×180** con `--res 320x180` (pixel più grossi, stile 16
bit, e più tempo per fotogramma) oppure **256×256** quadrata con `--res 256x256` (al centro
dello schermo, ingrandita 4× su 1080p, bordi neri). Colori: `0xRRGGBB`, lo schermo è a
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
(A sulla sua copertina lo riprende). `quit()` invece chiude davvero la cartuccia. Un gioco in
rete chiama `online(true)`: lì PS chiede al giocatore se vuole uscire dalla partita e
disconnettersi, e se sì chiama `_leave()` (vedi `docs/API-IT.md`).

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

**Mouse.** Un gioco ha il puntatore solo se lo chiede (lo muovono un mouse o la levetta
destra di un pad):

```lua
function _init() mouse(true) end           -- la console disegna la freccia
function _update()
  local mx, my = mouse()                   -- nil se non c'è niente che lo muova
  if mx and mousep() then                  -- clic sinistro
    tx, ty = mx, my
  end
end
```

## 3. Impacchettare e provare

```sh
cd ~/bm
python3 scripts/mkbm.py -o palla.bm --lua carts/palla/main.lua --title "Palla" --author "io"
```

Opzioni: `--sheet sprite.png`, `--map mappa.csv` (di nuovo `--map davanti=sopra.csv` per
un altro livello), `--flags flag.csv`, `--sprites zone.txt`, `--cover copertina.png`,
`--res 480x270` o `--res 320x180`.

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
`sheet.png`, `map.csv`, `flags.csv`, `sprites.txt` e `cover.png`, se ci sono; gli altri
livelli della mappa sono `map_<nome>.csv`, nell'ordine di `layers_<gioco> := nome ...`.

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

**Sprite con un nome.** Invece delle coordinate, un nome: le **zone** dello sheet, ognuna con
i suoi fotogrammi (caselle della stessa misura una accanto all'altra) e la sua velocità. Si
scrivono in un file di testo, una per riga (`nome x y larghezza altezza [fotogrammi [fps]]`),
e si passano a `mkbm.py --sprites` (nella build basta `sprites.txt` nella cartella del
gioco):

```
# zone.txt
eroe_fermo   0  32 16 16
eroe_corre   16 32 16 16 4 10
moneta       0  48 8  8  6 12
```

```lua
zspr("moneta", x, y)                       -- si anima da sola, a 12 fotogrammi al secondo
zspr("eroe_corre", x, y, nil, a_sinistra)  -- specchiata quando va a sinistra
zspr("eroe_fermo", x, y, 1)                -- un fotogramma preciso
```

`zone(nome)` dà dove sta e quanto è grande (per le collisioni), `zones()` tutti i nomi.
Le zone si fanno anche con bm Pixel sulla console e con `scripts/bmres.py`.

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
local t = mget(px // 8, py // 8)                      -- la cella sotto un punto
mset(cx, cy, 5)                                       -- cambiare una cella (porte, oggetti presi)
```

Senza `--map` la mappa è 256×256 vuota e si riempie con `mset` (`msize(w, h)` le dà
un'altra misura). Una mappa grande (Hunter's Night: 256×256 celle, 2048×2048 pixel)
conviene generarla con uno script: vedi `carts/hunt/mkassets.py`, che produce `sheet.png` e
`map.csv`.

**I flag: che cosa è ogni tile.** Ogni tile dello sheet ha 8 flag (0–7), accesi o spenti.
Le collisioni guardano i flag, non i numeri delle tile: si possono aggiungere tile nuove
(un muro di pietra, uno di legno) senza toccare il codice. La convenzione di bmlib:

| Flag | Valore | Significato |
|---|---|---|
| 0 | 1 | solido: muri, pavimenti |
| 1 | 2 | piattaforma: si attraversa da sotto, ci si poggia da sopra |
| 2 | 4 | scala |
| 3 | 8 | acqua |
| 4 | 16 | fa male (spine, lava) |
| 5–7 | | liberi per il gioco |

Si scrivono in un file per `mkbm.py --flags` (nella build: `flags.csv` nella cartella del
gioco), si accendono dall'editor (tasti `0`–`7` sulla pagina degli sprite) o dal codice:

```
# flag.csv: la tile 1 è un muro (1), le tile 2 e 3 piattaforme (2), la 9 una scala (4)
1=1 2 2
9=4
```

```lua
fset(12, 0, true)                       -- anche la tile 12 è solida
if fget(mget(cx, cy), 0) then ... end   -- la cella è solida?
local f = mflags(x, y + 8, 8, 1)        -- i flag sotto i piedi di uno sprite 8x8 (in pixel)
if f & 1 ~= 0 then a_terra = true end
if f & 16 ~= 0 then danno() end
```

**I livelli.** Una mappa può avere fino a 8 livelli della stessa misura, ognuno col suo
nome: il pavimento, le decorazioni, quello che passa **davanti** al personaggio. Si
disegnano uno alla volta:

```lua
map(mx, my, x, y, w, h, "main")     -- dietro
spr(eroe, ex, ey)
map(mx, my, x, y, w, h, "front")    -- davanti: chiome, archi, tetti
mset(cx, cy, 0, "front")            -- ogni livello si legge e si scrive da sé
```

Nel `.bm` vanno con `--map mappa.csv --map front=davanti.csv`; dal codice `mlayers()` li
elenca, li aggiunge e li rinomina. Per i movimenti con i muri, le piattaforme e la gravità
c'è `bmlib` (capitolo 11): `lib.move` e `lib.step` fanno tutto il lavoro.

## 6. Modelli 3D

Il 3D è software (in C sull'ARM; lo disegna la GPU del Pi quando può): triangoli con
z-buffer, luce per faccia, nebbia, texture. Budget indicativo: circa 1200 triangoli disegnati a 60 fps.

**Forme pronte:** `mesh_cube(colore)`, `mesh_sphere(anelli, segmenti, c1, c2)`.

**Modelli propri** con `mesh(vertici, facce, [uv])`, di solito creati in `_init`:

```lua
local piramide = mesh(
  { 0,1,0,  -1,-1,-1,  1,-1,-1,  1,-1,1,  -1,-1,1 },           -- x,y,z di ogni vertice
  { 1,3,2,0xE04040,  1,4,3,0xC03030,  1,5,4,0xE04040,           -- a,b,c,colore (indici da 1)
    1,2,5,0xC03030,  2,3,4,0x802020,  2,4,5,0x802020 })

function _draw()
  cls(0)
  zclear()                                   -- a ogni fotogramma, prima di draw3d
  camera3d(0, 1, -5, 0, 0, 60)               -- posizione, yaw, pitch, fov (e roll)
  light3d(-0.4, 0.8, -0.5, 0.3)              -- direzione della luce, luce ambiente
  draw3d(piramide, 0, 0, 0, 0, time(), 0, 1) -- posizione, rotazione x/y/z, scala
end
```

- Le facce vanno date in senso **orario viste da fuori**, cioè come appaiono sullo
  schermo dal lato che si vede (quelle girate dall'altra parte non vengono disegnate).
  bm Studio le fa già così. Il costruttore di bmlib (`lib.builder()`, quello di Astro
  Wing) fa i modelli da pezzi convessi e gira le facce da solo:
  `lib.builder():box(-1, 0, -1, 1, 2, 1, 0xC08040):build()`.
- **Texture:** con la terza tabella `uv` (6 numeri per faccia: u,v dei tre vertici, in
  pixel dello sprite sheet) le facce con colore `-1` prendono l'immagine dal foglio, con
  la prospettiva corretta.
- **Nebbia:** `fog3d(colore, vicino, lontano)` sfuma gli oggetti lontani.
- **Proiezione:** `project3d(x, y, z)` dà la posizione sullo schermo di un punto 3D, per
  disegnarci sopra in 2D (orizzonte, mirini, nomi).
- 2D e 3D si mescolano: sfondo con `rectfill`/`map`, modelli con `draw3d`, HUD con
  `print` alla fine.

**Con bm Studio** ([sdk/README.md](../sdk/README.md)): i modelli si fanno sul PC posando
le tessere dello sprite sheet su una griglia e stanno nel `.bm`
stesso; nel gioco `model("nome")` li dà come mesh:

```lua
local casa
function _init() casa = model("house") end            -- in _init: costruisce la mesh
function _draw()
  cls(0) zclear()
  camera3d(0, 4, -8, 0, -0.4)
  draw3d(casa, 0, 0, 0, 0, time() * 0.5)
end
```

`models()` dà i nomi, `bounds3d(m)` il box intorno al modello.

**Animati con bm Animator**: lo scheletro e le animazioni (fatti sul PC, nel `.bm`) vengono
con il modello; `animate(m, "walk", t)` mette il modello nella posa di "walk" al tempo `t`
(in secondi), prima di `draw3d`:

```lua
local man, t = nil, 0
function _init() man = model("villager") end
function _update() t = t + 1 / 60 end
function _draw()
  cls(0) zclear()
  camera3d(0, 2, -6, 0, -0.25)
  animate(man, "walk", t)                     -- oppure animate(man, "walk", t, "idle", t, k): un misto
  draw3d(man, 0, 0, 0)
end
```

`clips(m)` dice quali animazioni ci sono, `bone3d(m, "arm.L")` dove si trova un osso (la
testa, poi la coda: per attaccargli una spada o una luce). bm Animator fa anche gli **sprite pre-renderizzati**:
l'animazione disegnata da 1 a 8 direzioni nello sprite sheet, da usare con `sspr()` in un
gioco 2D (il codice Lua per disegnarli lo prepara lui).

**Da Blender** (o da altri
programmi): un `.glb` si importa in bm Studio (le texture finiscono nello sprite sheet) e
da lì nel `.bm`; per un gioco del repository basta mettere `models.glb` nella sua cartella
(vedi `carts/village`).

## 7. Suono

L'audio esce dall'HDMI: otto voci di sintesi (0–7), sei forme d'onda (`SQUARE`,
`TRIANGLE`, `SAW`, `NOISE`, `SINE`, `METAL`) con inviluppo ADSR. Due strade, anche
insieme.

**Effetti e musica fatti con il Sound editor** (scheda **Dev** del menu). È il modo più
comodo: si compongono col pad o con la tastiera e si salvano dentro il gioco.

1. Nel menu, sul gioco: **X** → *Open in the Sound editor* (oppure Dev → Sound, poi
   *Open...* dal menu dell'editor, SELECT).
2. Pagina **SOUNDS**: gli strumenti (prova *KICK*, *BASS*, *LEAD* del progetto demo).
3. Pagina **SFX**: gli effetti per il gioco, una nota per passo (A aggiunge, A + su/giù
   cambia la nota, START ascolta).
4. Pagina **PATTERN**: 8 tracce × 16 passi, come una drum machine; **SONG**: l'ordine
   dei pattern.
5. **Save** (SELECT → Save, o Ctrl+S): i suoni finiscono nel `.bm`. *Try it in the
   game* lo avvia e torna all'editor.

Nel codice bastano due funzioni:

```lua
function _init() music(0) end          -- il brano 0, in loop
-- ...
if salto then sfx(1) end               -- l'effetto 1, su una voce libera
if moneta then sfx(0, nil, combo) end  -- trasposto di `combo` semitoni
if fine then music(-1, 800) end        -- la musica sfuma in 0,8 s
```

**Note dal codice**, per suoni che dipendono dal gioco:

```lua
note(0, 880, 60, SQUARE, 100)        -- voce, Hz (o "A5"), durata in ms, forma, volume 0-255
envelope(2, 0, 60, 0, 30)            -- attack, decay, sustain, release della voce 2
note(2, 2500, 300, NOISE, 130)       -- un'esplosione che si smorza
note(1, 1300, 300, SQUARE, 70)       -- un laser che parte alto...
slide(1, 300, 250)                   -- ...e scende
note(3, "C4", 500, SQUARE, 90)
arp(3, "minor", 40)                  -- un accordo arpeggiato, stile chip
```

Buona abitudine: una voce per tipo di suono (arma, colpi, musica), così un effetto non
interrompe l'altro; con `sfx(n)` la voce la sceglie la console, lasciando stare la
musica. Il **volume** è della console: si cambia in Settings o nel menu di pausa del
gioco (`volume()` lo legge e lo cambia; mettilo anche nella pausa del tuo gioco: `lib.pause()`
di bmlib lo ha). Per le melodie brevi c'è `lib.jingle`.

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

Numeri, stringhe, booleani e tabelle, fino a 32 KiB, in `/bm/save/` sulla SD. Con bmlib:
`lib.best("record", punti)` tiene il record e scrive solo quando viene battuto.

## 10. Copertina

`--cover copertina.png`: un PNG di qualsiasi misura, stampato sulla "scheda" del gioco nel
menu, un quadrato di 88×88: un'immagine quadrata viene ridotta, le altre (16:10) restano
intere sopra una copia sfocata di sé. Le copertine dei giochi demo sono
disegnate da `scripts/mkcovers.py`. Senza copertina il menu stampa il titolo.

## 11. La libreria comune: bmlib

Molte cose tornano in quasi tutti i giochi: limitare un numero, scegliere a caso, vedere se
due rettangoli si toccano, far camminare un personaggio fra i muri, far seguire la camera,
le particelle di un'esplosione, il titolo e la partita, il menu di pausa, un jingle, il
record. Sono in **bmlib**, una libreria inclusa nella console:

```lua
local lib = require "bmlib"
```

Tempi in secondi, posizioni in pixel, velocità in pixel per fotogramma; tween, timer e
jingle vanno avanti con `lib.update()`, una volta in `_update`. L'elenco completo è in
[API-IT.md](API-IT.md#bmlib-la-libreria-comune-dei-giochi). Ecco un platform intero, senza
file a parte (le tile si disegnano nel codice e la mappa si costruisce con `mset`): titolo,
partita, salto, piattaforme, monete con le scintille, camera, pausa, record.

```lua
-- Salti: un platform con bmlib
local lib = require "bmlib"

local MURO, PIATTA, MONETA = 1, 2, 3            -- tile = celle dello sheet
local eroe, cam, fx, S, pausa

local function tile(n, c)                       -- una tile 8x8 con il bordo più scuro
  local x0, y0 = n % 32 * 8, n // 32 * 8
  for y = 0, 7 do
    for x = 0, 7 do sset(x0 + x, y0 + y, (x == 0 or y == 0) and lib.shade(c, 0.6) or c) end
  end
end

function _init()
  tile(MURO, 0x806040); tile(PIATTA, 0x40A040); tile(MONETA, 0xFFD050)
  fset(MURO, 0, true)                           -- flag 0: solido
  fset(PIATTA, 1, true)                         -- flag 1: piattaforma (si sale da sotto)
  msize(160, 45)                                -- 1280x360 pixel
  for x = 0, 159 do mset(x, 44, MURO) end       -- il pavimento
  for x = 10, 16 do mset(x, 38, PIATTA) end
  for x = 22, 28 do mset(x, 32, PIATTA) end
  mset(25, 31, MONETA); mset(60, 43, MONETA)
  lib.tiles({ edge = true })                    -- i bordi della mappa sono muri
  cam = lib.camera({ bounds = true, dead = { 64, 48 } })
  fx = lib.particles(200)
  pausa = lib.pause({ when = function() return S:is("gioco") end, quit = function() S:go("titolo") end })
  S = lib.states({
    titolo = {
      update = function() if btnp("ok") then S:go("gioco") end end,
      draw = function()
        cls(0x102030)
        lib.printc("SALTI", 120, 0xFFD050, 3)
        if lib.blink() then lib.printc("premi A", 200, 0xFFFFFF) end
        lib.printc("record " .. (lib.store("record") or 0), 240, 0x8090A0)
      end,
    },
    gioco = {
      enter = function(s)
        eroe = { x = 40, y = 300, w = 8, h = 8, vx = 0, vy = 0 }
        s.monete = 0
        cam:follow(eroe.x, eroe.y, true)
      end,
      update = function(s)
        eroe.vx = (btn("right") and 2 or 0) - (btn("left") and 2 or 0)
        if eroe.ground and btnp("a") then eroe.vy = -5; note(0, 440, 60, SQUARE, 90) end
        eroe.drop = btn("down")                 -- giù da una piattaforma
        lib.step(eroe)                          -- gravità, muri, piattaforme
        local cx, cy = (eroe.x + 4) // 8, (eroe.y + 4) // 8
        if mget(cx, cy) == MONETA then          -- presa
          mset(cx, cy, 0)
          s.monete = s.monete + 1
          fx:burst(cx * 8 + 4, cy * 8 + 4, 16, { colors = { 0xFFFFFF, 0xFFD050 }, gravity = 0.05 })
          lib.jingle({ { "E5", 0.08 }, { "B5", 0.15 } })
          lib.best("record", s.monete)          -- salvato solo se è un record
          cam:shake(2, 0.2)
        end
        fx:update()
        cam:follow(eroe.x, eroe.y)
      end,
      draw = function(s)
        cls(0x203048)
        cam:apply()                             -- la camera (e il tremolio)
        cam:map()                               -- solo le celle che si vedono
        rectfill(eroe.x, eroe.y, 8, 8, 0xFF6040)
        fx:draw()
        camera()                                -- l'HUD fermo
        lib.prints("monete " .. s.monete, 8, 8, 0xFFFFFF)
      end,
    },
  }, "titolo")
end

function _update()
  if pausa:update() then return end             -- Start: il menu di pausa
  lib.update()
  S:update()
end

function _draw()
  S:draw()
  pausa:draw()
end
```

Altre cose utili: `lib.script` per le scene scritte in fila (`lib.wait(1)`,
`lib.waitfor(...)`), `lib.tween` per scritte e menu che entrano, `lib.menu` per i menu
propri, `lib.btnr` per i tasti che si ripetono, `lib.rng(seme)` per mondi sempre uguali
dallo stesso seme, `lib.ray` per vedere se un nemico vede l'eroe, `lib.dir8` per gli sprite
a 8 direzioni.

## 12. Consigli

- **Prestazioni:** il costo sta quasi tutto nel Lua di `_update`/`_draw`; il disegno è
  in C. Evitare di creare tabelle nuove a ogni fotogramma nei cicli caldi; aggiornare
  solo i nemici vicini alla camera; disegnare solo la parte visibile della mappa.
- **Numeri:** Lua ha interi e decimali; per le posizioni sullo schermo usare
  `math.floor` o `//` se servono pixel interi.
- **Casualità:** `math.randomseed(stat(3))` quando il giocatore preme un tasto
  (il numero del fotogramma cambia a ogni partita).
- **Debug:** `log(...)` scrive sulla console seriale; `stat(1)` è il tempo dell'ultimo
  fotogramma in ms, `stat(2)` gli fps.
- **Non riscrivere quello che c'è:** prima di scrivere una funzione di utilità guarda in
  bmlib (capitolo 11); l'assistente (F6) la conosce.

## 13. Esempi da cui partire

| Gioco | Cosa mostra |
|---|---|
| `carts/pong` | il più semplice: forme, input, suoni, salvataggio |
| `carts/snake` | griglia, tempo di gioco, record |
| `carts/shooter` | sprite scritti nel codice con `sset`, ondate, particelle |
| `carts/astrowing` | 3D: modelli, nebbia, camera che si inclina, orizzonte |
| `carts/hunt` | 320×180, mappa 2048×2048 generata, luci, combattimento, boss |
| `carts/demo` | sprite sheet PNG e mappa CSV veri e propri |
| `carts/kitchen` | gioco grande: sorgenti in più file uniti da `build.py`, 3D con mesh costruite in codice, 1–4 giocatori (`btn(i, p)`, `players()`), salvataggi, e un simulatore host (`tests/kitchen/sim.lua`) che gioca da solo per trovare errori e misurare il costo di ogni frame |
| `carts/village` | modelli 3D fatti con bm Studio e un paesano animato con bm Animator (`models.bm`): `model()`, `animate()` con due animazioni mescolate, `bone3d()`, terreno senza z-buffer, notte con `lamp3d` e nebbia, sprite pre-renderizzati |
| `carts/titan` | sprite grandi pre-renderizzati (un modello 3D fatto in Python diventa pixel art a strati: un frame, tante combinazioni di equipaggiamento), sheet grande con palette (`--sheet8`), parallasse, stati di un picchiaduro con hitbox per frame, CPU avversaria |
| `tests/gameapi/cart.lua` | ogni funzione dei livelli della mappa, dei flag delle tile, delle zone con nome e di bmlib, con i suoi casi |
