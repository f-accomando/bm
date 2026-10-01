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

Globali: `SCREEN_W` e `SCREEN_H` (640 e 360; 320 e 180 con `--res 320x180`).
Lo schermo **non** viene cancellato da solo: di solito `_draw` comincia con `cls()`.

Limiti: un errore o un ciclo infinito (oltre **20 milioni di istruzioni** Lua in un
fotogramma) ferma la cartuccia senza bloccare la console. Sandbox: niente `io`, `os`,
`load`, `dofile`; `require` carica solo le librerie incluse nel kernel (per ora
`"assist"`, il pannello dell'assistente); ci sono `string`, `table`, `math`, `utf8`,
`coroutine`.

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
| `font([nome])` | il font di `print` da qui in poi: `"8x16"` (quello normale), `"8x14"` o `"6x12"` (106 colonne per 30 righe a 640×360: per gli strumenti con tanto testo); restituisce larghezza e altezza di un carattere del font corrente |
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

**Mouse e puntatore (M31).** Una cartuccia ha il puntatore solo se lo chiede: senza
`mouse(true)` non c'è (nel menu di bm invece c'è sempre). Lo muovono un mouse USB o
Bluetooth, oppure la levetta destra di un pad (R2 o R3 tasto sinistro, L2 destro); la
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

**Tasti a scelta del giocatore.** Una cartuccia che vuole far scegliere i tasti (come
nano8) legge la tastiera tasto per tasto e i controller pulsante per pulsante:

| Funzione | Descrizione |
|---|---|
| `rawkeys(on)` | con `true` le tastiere smettono di fare da controller per `btn()` e `pad()`: si leggono con `keydown()`. Esc chiude comunque la cartuccia |
| `keydown(u)` | `true` finché è premuto il tasto con l'usage USB HID `u` (USB o Bluetooth): `0x04`…`0x1D` le lettere A–Z, `0x1E`…`0x27` le cifre, `0x28` Invio, `0x2C` spazio, `0x4F`…`0x52` le frecce (destra, sinistra, giù, su), `0xE0`…`0xE7` Ctrl, Shift, Alt, GUI di sinistra e poi di destra |
| `keys()` | gli usage dei tasti premuti adesso (`{0x1D, 0xE1}`): per "premi un tasto" |
| `pad([p])` | i pulsanti che il giocatore `p` (1–4) tiene premuti, in bit: 1 sinistra, 2 destra, 4 su, 8 giù, 16 A, 32 B, 64 Start, 128 Select, 256 X, 512 Y, 1024 L1, 2048 R1, 4096 L2, 8192 R2, 16384 L3, 32768 R3 (i grilletti e le levette premute: DS4 e Xbox 360); senza `p` quelli di tutti. I tasti della seriale contano come il controller del primo giocatore |

### Tempo e sistema

| Funzione | Descrizione |
|---|---|
| `time()` | secondi dall'avvio della cartuccia (con decimali) |
| `stat(n)` | 0 KiB usati da Lua, 1 ms di CPU dell'ultimo fotogramma, 2 fps, 3 numero del fotogramma, 4 triangoli 3D, 5 pixel 3D |
| `log(...)` | scrive nel log del kernel (seriale e console), non sullo schermo del gioco |
| `quit()` | chiude la cartuccia alla fine del fotogramma |
| `timeslice(co, [k])` | la coroutine `co` si ferma da sola dopo circa `k` mila istruzioni Lua in un fotogramma (400 se manca) e `coroutine.resume` torna `true` senza valori: un calcolo lungo prosegue nei fotogrammi successivi invece di fermare la cartuccia per il limite di istruzioni. `timeslice(nil)` lo toglie (nano8 lo usa per le sue cartucce) |

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

L'audio esce dall'HDMI a 48 kHz (dagli altoparlanti del monitor) ed è generato in un
interrupt: non costa nulla al tuo `_update`. Ci sono due modi di usarlo, anche insieme:

- **il banco di suoni** della cartuccia (effetti sonori e musica fatti con il Sound
  editor della scheda Dev): `sfx(n)` e `music(n)`;
- **le note** suonate dal codice, una voce alla volta: `note`, `slide`, `arp`...

Otto voci (0–7). Forme d'onda: `SQUARE` (quadra, con `duty`), `TRIANGLE`, `SAW` (dente
di sega), `NOISE` (rumore), `SINE` (seno), `METAL` (rumore corto e metallico: piatti,
campanelli). Ogni voce ha un inviluppo ADSR. Le altezze sono in **Hz** (anche con la
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
| `apu(v, reg, [valore])` | legge o scrive un registro grezzo della voce (16 byte per voce: `src/audio/synth.h`; il registro 10 sono i 1/256 di Hz) |

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
per le melodie); il progetto dimostrativo del Sound editor (`carts/sound/demo.json`) ha
effetti sonori e due brani da ascoltare e copiare.

#### Il banco: formato e strumenti

Il banco è la sezione **AUDIO** del `.bm` (formato in `src/audio/player.h`): fino a 32
suoni (strumenti), 64 effetti sonori, 64 pattern e 8 brani. Si crea con il **Sound
editor** (scheda Dev), che apre un gioco e ne salva i suoni direttamente dentro.
Sul PC: `scripts/bmaudio.py unpack gioco.bm -o suoni.json` lo estrae in JSON leggibile,
`mkbm.py --audio suoni.json` lo rimette in una cartuccia, `make wav BANK=suoni.json
SONG=0` lo ascolta in un WAV.

### Tastiera e file (per strumenti come gli editor)

| Funzione | Descrizione |
|---|---|
| `keyp()` | il prossimo tasto scritto: un carattere (`"a"`, `"\n"` Invio, `"\b"` Backspace, `"\t"`), un nome (`"up"`, `"down"`, `"left"`, `"right"`, `"home"`, `"end"`, `"pgup"`, `"pgdn"`, `"del"`, `"esc"`, `"f1"`…`"f12"`) o `"^s"` per Ctrl+S; `nil` se nessuno. Dalla prima chiamata la tastiera scrive e non fa più da gamepad per `btn()`, ed Esc non chiude la cartuccia (Start+Select e PS sì) |
| `ls([cartella])` | i file della SD: `{ {name=, size=, dir=}, … }` |
| `cart_load(percorso)` | apre un `.bm`: il suo sprite sheet, la sua mappa e i suoi modelli 3D (con gli scheletri) sostituiscono quelli della cartuccia che chiama; restituisce `{title, author, res, lua, sheet_w, sheet_h, map_w, map_h}` |
| `cart_new()` | sprite sheet e mappa vuoti (256×256), niente modelli |
| `cart_save(percorso, {title, author, res, lua})` | scrive un `.bm` con il codice dato e lo sprite sheet, la mappa, la copertina, il banco di suoni, i modelli e gli scheletri correnti (le altre sezioni del file aperto restano come erano); nome 8.3, es. `"/carts/GIOCO.BM"` |
| `cart_read(percorso)` | il codice e l'intestazione di un `.bm`: `{title, author, res, lua, size}`, **senza** toccare lo sheet e la mappa di chi chiama (al contrario di `cart_load`): per editor con più file aperti |
| `cart_write(percorso, {[lua, title, author, res, from, sections]})` | cambia **solo** il codice (e i campi dati) di un `.bm`: sprite sheet, mappa, copertina, banco di suoni e le sezioni che il kernel non conosce restano com'erano; un file con il nome lungo lo tiene. Senza `lua` il codice resta quello. Un file che non c'è diventa una cartuccia con solo il codice (nome 8.3). `from`: le altre sezioni vengono da un altro file ("salva come"). `sections`: `{[8] = byte MESH, [9] = byte ANIM}` (`false` le toglie), controllate prima (`false, "broken MESH section"`): così bm Mesh scrive i modelli |
| `cart_meshes(percorso)` | le mesh che il **codice** di un `.bm` costruisce con `mesh()`, `mesh_sphere()` e `mesh_cube()`: `{ {name=, kind=, verts={x,y,z,…}, faces={a,b,c,colore,…}, [uv={…}]}, … }` (gli argomenti di `mesh()`, indici da 1, colore `-1` = texture) e `nil` oppure il primo errore del codice; `nil` e un messaggio se il file non si legge. Il codice gira **a parte** (uno stato Lua suo, `src/bm/meshcap.c`): il corpo del file, poi `_init`, `_update` e `_draw` una volta, con un limite di istruzioni; le altre funzioni di bm non fanno niente (niente file, schermo o suono). Il nome è quello della variabile che tiene la mesh (`M.ship` → `"ship"`; in un array `chef[2].body` → `"chef2_body"`). Per bm Mesh |
| `cart_audio([percorso])` | il banco di suoni di un `.bm` come stringa (`false` se non ne ha) e il suo titolo; senza percorso, il banco della cartuccia che gira |
| `cart_put_audio(percorso, banco, [titolo, lua])` | mette il banco (stringa; `nil` lo toglie) in un `.bm`, il resto del file come prima; se il file non c'è lo crea con quel titolo e quel codice. `true`, o `false` e un messaggio |
| `audio_bank(banco)` | da ora suona questo banco (per gli editor: musica ed effetti che suonano vanno avanti); `nil`: nessuno |
| `audio_pattern(p, bpm, swing)` / `audio_play(v, suono, nota, [vol], [fx], [ms])` | un pattern in loop, un suono del banco su una voce (anteprime degli editor) |
| `cart_run(percorso)` | esce, gioca quel file e poi riapre la cartuccia che l'ha chiesto, con `cart_arg()` = `{path=, error=, back=true}` (dal menu, "Open in the SDK", "... Sound editor", "... 3D studio" o "... bm Mesh": `back=false`) |
| `cart_data(tipo, [byte])` | le sezioni **MESH** (`tipo` 8) e **ANIM** (9) del progetto, come stringhe nel formato di `src/bm/bm.h`: senza `byte` le restituisce (`nil` se non ci sono), con `byte` le sostituisce (`nil` o `""` le toglie) → `true`, oppure `false` e il motivo. Il kernel le controlla prima; `model()`, `animate()` e `bone3d()` usano subito quelle nuove e `cart_save` le scrive. Così lo studio 3D modifica modelli e scheletri (con `string.pack` / `string.unpack`) |

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
| `ai.ask(domanda, [{n=5, ctx=parola, kinds="api,howto"}])` | le voci migliori, la prima è la più probabile: `{ {id=, title=, kind=, score=}, … }`, e come secondo valore i microsecondi impiegati. `ctx`: la parola sotto il cursore (se è una funzione delle API, la sua voce va in cima). `kinds`: `api`, `howto`, `error`, `tip`, `sprite` |
| `ai.entry(id)` | una voce: `{id, kind, title, name, text, code, gen, see = {id, …}}` |
| `ai.list([kinds])` | tutte le voci `{id, title, kind}` (per sfogliarle col pad) |
| `ai.near(parola)` | il nome delle API più vicino a una parola scritta male (`"sprr"` → `"spr"`, 1), o `nil` |
| `ai.sprite(richiesta, [{gen=, size=16, seed=1, outline=true, palette={…}}])` | la base di uno sprite: `{w, h, gen, name, seed, px = {0xRRGGBB o -1 (trasparente), …}}` riga per riga. La ricetta viene dalle parole (`"slime"`, `"astronave"`, `"moneta"`, `"erba"`…) o da `gen`; i colori (`"rosso"`, `"blue"`…) e la misura (`"8x8"`, `"32x32"`, `"piccolo"`, `"grande"`) dalle parole; un altro `seed` è una variante; con `palette` ogni pixel diventa il colore più vicino della tavolozza |
| `ai.recipes()` | le ricette degli sprite `{id, name}` |
| `ai.checksum(domanda)` | il CRC-32 delle uscite della rete per una domanda: per i test (uguale a quello del riferimento in Python) |

**Il pannello** (`require "assist"`): quello che gli strumenti aprono con un tasto
(F6 nell'Assistant). Risponde mentre scrivi; Invio (A) passa il codice o lo sprite allo
strumento, Esc (B) chiude, Tab (X) cambia modo; senza domanda si sfoglia tutto col pad.

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

`assist.open{...}`: `mode` = `"code"` (API, esempi, errori), `"sprite"`, `"error"`
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

### 3D (software)

| Funzione | Descrizione |
|---|---|
| `mesh(v, f, [uv])` | mesh da tabelle: `v` = {x,y,z, x,y,z, …}, `f` = {a,b,c,colore, …} (indici da 1; una faccia si vede dal lato da cui i suoi vertici appaiono in senso **orario**). Con `uv` (6 numeri per faccia: u,v dei tre vertici in pixel dello sprite sheet) le facce con colore `-1` hanno la **texture** dello sprite sheet (prospettiva corretta, i pixel trasparenti restano vuoti) |
| `mesh_sphere([r, segmenti, c1, c2])`, `mesh_cube([c])` | mesh pronte |
| `model(nome)` / `model(n)` | un **modello 3D della cartuccia** (fatto con [bm Studio](../sdk/README.md), sezione MESH) come mesh, con la texture dello sprite sheet; `n` conta dall'1; `nil` se non c'è. Ogni chiamata costruisce una mesh nuova: va fatta in `_init` |
| `models()` | i nomi dei modelli della cartuccia, in ordine (`{}` se non ne ha) |
| `bounds3d(m)` | `x0, y0, z0, x1, y1, z1`: il box intorno ai vertici di una mesh, nelle sue coordinate (prima di spostarla, girarla e scalarla con `draw3d`): per centrarla, per le collisioni |
| `animate(m, [anim, t, anim2, t2, k])` | **animazione scheletrica**: un modello con lo scheletro di [bm Animator](../sdk/README.md#bm-animator) prende la posa dell'animazione `anim` (nome o numero) al tempo `t` in secondi (in ciclo, se l'animazione è in ciclo); con `anim2, t2` mescola due animazioni (`k` da 0, solo la prima, a 1, solo la seconda: per passare dall'una all'altra); senza animazione la posa di riposo. Restituisce la durata dell'animazione. Errore se la mesh non ha scheletro o l'animazione non c'è |
| `clips(m)` | le animazioni di un modello: `{ {name=, length=, loop=}, ... }` (`{}` senza scheletro) |
| `bone3d(m, osso)` | `x, y, z, cx, cy, cz`: dove si trovano la testa e la coda di un osso (nome o numero) nell'ultima posa, nelle coordinate del modello (come `bounds3d`); `nil` se l'osso non c'è. Per attaccare oggetti alle mani (la testa), la punta di una spada (la coda), luci, effetti |
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
codice, orizzonte con `project3d`, nebbia, esplosioni, boss). Con i modelli di bm Studio:
`carts/village` (`model()` per ogni modello, terreno disegnato senza z-buffer, notte con
`lamp3d` e `fog3d`; il paesano di bm Animator con `animate()`, due animazioni mescolate,
una luce in mano con `bone3d()`, e la sua versione a sprite pre-renderizzati).

## Budget e consigli

- 60 fps = **16,7 ms** per fotogramma per `_update` + `_draw` + la copia sullo schermo.
  In alto a sinistra nella demo, `stat(1)` mostra quanto ne usa la cartuccia.
- Il disegno è in C: una chiamata `spr` o `rectfill` costa pochi microsecondi, ma
  ogni chiamata da Lua ha un costo fisso. Ordini di grandezza sul Pi (docs/STRESS.md):
  ~1800 sprite 16×16 chiamati da Lua a 60 fps, ~4500 dal C; ~1200 triangoli 3D.
- Evita di creare tabelle nuove in ogni fotogramma se non serve (meno lavoro al GC).
- Il testo è a celle 8×16: per scritte ben allineate usa x multipli di 8 e y di 16.
