title: Un platform costruito da un riferimento: il primo livello di SMW
accent: platform
lang: it
kicker: guida di esempio bm
subtitle: Come un gioco vero è costruito come progetto bm: il .bme, le API che usa, la struttura del gioco e la pipeline dei suoi asset.
footer: manuali bm - smw-guide

::: readme
**Cos'è.** Un giro guidato di `carts/smw/`, un port personale del primo livello di un classico platform a 16 bit per l'RGB30 (360x360, `.bm`). È un esempio di gioco completo scritto come progetto bm: layer della mappa con flag, zone sprite con box, `bmlib`, un ciclo di gioco a frequenza fissa e una fisica misurata.

**Come usarlo.** Leggi i primi tre capitoli per vedere la forma del progetto. Poi leggi il capitolo della parte che vuoi copiare: movimento, nemici, blocchi, asset.

**Cosa non dà.** La grafica del gioco originale. Il progetto la costruisce dalla ROM di chi possiede la cartuccia; non è mai nel repository. Le immagini di questa guida mostrano lo stesso livello con una grafica ridisegnata.
:::

# Il progetto

## A cosa serve

Il port ha due scopi. È un **benchmark**: lo stesso livello, la stessa fisica e gli stessi comandi sulla console mostrano quanto veloce la piattaforma esegue un gioco vero. Ed è un **esempio svolto** di come un gioco bm si divide in dati (le risorse del `.bme`) e codice (Lua).

![L'inizio del livello](shots/smw-guide-01-start.png)

## I file

| Percorso | Cos'è |
|---|---|
| `carts/smw/src/game.lua` | il gioco: stati, nemici, blocchi, camera, disegno |
| `carts/smw/src/physics.lua` | il movimento di Mario, con le costanti misurate sull'originale |
| `carts/smw/build/build.py` | trasforma i dati estratti nel `.bme` e nel `.bm` |
| `carts/smw/build/sprites.py` | taglia le zone sprite e i loro box |
| `carts/smw/build/bg.py` | taglia il layer dello sfondo |
| `carts/smw/bm.info` | i metadati: nome, `360x360`, target `rgb30`, input `pad` |
| `carts/smw/guide/build.py` | l'edizione per questa guida: lo stesso livello, grafica ridisegnata |
| `scripts/smw_*.py` | gli script di cattura: eseguono la ROM in un emulatore e scrivono i dati |
| `tests/smw/` | il test di fisica e il confronto con l'emulatore |

::: note
La ROM, i file che produce e la grafica vera restano fuori da git (`.gitignore`). Gli script di build li leggono da `carts/smw/extracted/` sulla tua macchina.
:::

## Come si fa una cartuccia

Un `.bme` è il progetto di un gioco: il codice Lua, il foglio degli sprite, i layer della mappa e le zone in un solo file. Un `.bm` è lo stesso contenuto come gioco; la sua intestazione dice che è per l'RGB30. `scripts/mkbm.py` crea entrambi dagli stessi input; la build lo chiama due volte:

```
python3 scripts/mkbm.py --lua main.lua --sheet sheet.png --sprites sprites.txt \
  --map map_main.csv --map col=map_col.csv --map bg=map_bg.csv --flags flags.csv \
  --info carts/smw/bm.info --targets rgb30 --input pad -o smw.bm
```

`python3 scripts/bmres.py info smw.bm` elenca cosa c'è dentro: la dimensione del Lua, il foglio, la mappa (640x54 celle), i tre layer `main`, `col` e `bg`, i flag e le 40 zone sprite.

# Il livello come mappa

## Tre layer

Il livello è largo 20 schermate. La mappa ha 640x54 celle da 8x8 pixel e tre layer:

| Layer | Cosa contiene |
|---|---|
| `main` | ciò che il giocatore vede davanti: terreno, tubi, blocchi, monete |
| `col` | ciò che è solido: una cella marcatore per ogni blocco 16x16 |
| `bg` | lo sfondo (colline e alberi); scorre a metà della velocità della camera |

Il layer `col` usa i flag della mappa. Due celle marcatore in fondo al foglio dei tile li portano: il flag 1 vuol dire *solido*, il flag 2 vuol dire *piattaforma* (un piano su cui si atterra solo dall'alto, come una sporgenza). Il gioco dice a `bmlib` quale layer e quali flag usare:

```lua
lib.tiles({ layer = "col", solid = 1, platform = 2 })
```

Dopo questo, `lib.solid(x, y)` risponde per qualsiasi punto del livello, in pixel.

## Blocchi da 16x16

L'originale costruisce i livelli con blocchi da 16x16 pixel, ognuno fatto di quattro tile 8x8. La build tiene quella griglia in una tabella Lua (`L16`, un numero per blocco) e scrive i quattro tile di ogni blocco nel layer `main`. Quando il gioco cambia un blocco (un colpo, una rottura, una moneta presa) cambia insieme la tabella, i quattro tile e le quattro celle di `col`:

```lua
local function put16(cx, cy, id)
  L16[cy + 1][cx + 1] = id
  set16(cx, cy, M16[id] or { 0, 0, 0, 0 })     -- l'immagine: quattro chiamate mset su "main"
  set_col(cx, cy, id)                           -- i muri: quattro chiamate mset su "col"
end
```

La lista `changed` ricorda ogni blocco toccato dal giocatore, così un riavvio li rimette a posto.

## La camera

L'immagine dell'originale è 256x224. Si disegna intera al centro dello schermo 360x360, dentro un `clip`. `map()` disegna una finestra dei layer dalla posizione della camera:

```lua
map(bu // 8, camy // 8, VX - (bu % 8), VY - (camy % 8), 33, 29, "bg")      -- prima lo sfondo
map(mx, my, VX - (camx % 8), VY - (camy % 8), 33, 29, "main")             -- poi il livello
```

La camera segue Mario su x (`x - 108`) e su y, e si ferma ai bordi del livello.

![La camera segue Mario](shots/smw-guide-06-camera.png)

# Gli sprite

## Zone e box

Ogni cosa che si muove è una **zona sprite**: un rettangolo del foglio, con un numero di frame e dei box dentro. `sprites.txt` è l'elenco. Questa è la voce del Mario che cammina:

```
mario_walk 0 448 32 48 2 8
  hurt * 10 24 12 16
  box 3 * 10 36 12 4
  box 4 * 10 24 12 16
```

La riga dà il nome, il posto nel foglio (`0 448`), la dimensione di un frame (`32 48`), il numero di frame (`2`) e la loro frequenza. Le tre righe sotto sono i box, in pixel dall'angolo del frame:

- `hurt` è dove Mario può essere colpito (12x16, a 10,24).
- `box 3` sono i **piedi**: un box sottile in basso. Atterrare su un nemico con questo è un salto sulla testa.
- `box 4` è il **corpo**: toccare un guscio con questo lo calcia.

Il gioco disegna una zona con `zspr(zona, x, y, frame, specchio)` e legge i suoi box con `zboxes(zona, frame, tipo)`. Una tabella `ZA` dà l'angolo di ogni zona dalla posizione dello sprite, perché le immagini non hanno tutte la dimensione della cosa che mostrano.

## I contatti con bmlib

`lib.hits` fa i test di contatto. A ogni frame il gioco lo svuota, inserisce ogni corpo come zona e chiede i contatti:

```lua
H:clear()
H:zone(mario, mz, mf, mcx, mcy, mflip, { team = "p" })
for _, b in ipairs(zboxes(mz, mf, 3)) do          -- i piedi
  local bx, by, bw, bh = lib.box(b, mcx, mcy, mflip, 32)
  H:hit(mario, bx, by, bw, bh, { team = "p", kind = "stomp" })
end
-- ogni nemico: H:zone(k, zona, frame, x, y, false, { team = "e" })
local contacts = H:check()
```

Ogni contatto dice chi ha colpito chi e con che tipo: il gioco trasforma uno `stomp` in un nemico schiacciato e un rimbalzo, e un contatto di corpo con un nemico che cammina in una morte.

## Specchio e animazione

Le zone contengono una sola direzione. L'immagine specchiata è un flag della chiamata di disegno (`zspr(..., specchio)`), quindi un Mario che cammina a sinistra non costa grafica in più. L'animazione di Mario è una regola sola: un contatore di distanza (`anim`) cresce con la sua velocità, e un frame dura 122 unità, quindi passeggia più in fretta quando corre.

# Il movimento di Mario

## Il tick fisso

L'originale gira a 50 Hz. La console disegna a 60 frame al secondo. Il gioco tiene la logica a un **tick fisso di 50 Hz** e disegna tra un tick e l'altro:

```lua
acc = acc + 50 / 60
while acc >= 1 do
  acc = acc - 1
  tick(inp)
end
```

`_draw` interpola la posizione tra gli ultimi due tick (`prev` e `mario`) con il resto `acc`, così il moto è fluido a 60 frame al secondo e la logica resta veloce esattamente come l'originale.

## Costanti misurate, non indovinate

`physics.lua` tiene il movimento in una tabella di costanti: accelerazione, decelerazione, i tre limiti di velocità (cammino, corsa, corsa con il contatore P pieno), la frenata, la velocità di salto, due gravità, il limite di caduta. Le posizioni sono in pixel e le velocità in 1/16 di pixel per tick.

Due regole dell'originale si vedono nei numeri:

- **Il pulsante di salto cambia la gravità.** Finché è premuto la gravità è più leggera (3,4); dopo il rilascio è più pesante (circa 6,8). È tutto il salto a altezza variabile.
- **Il contatore P.** Correre a piena velocità a terra lo riempie; quando è pieno il limite di corsa sale. Cala lentamente, anche in aria, a meno che non sia pieno.

![Corsa con il contatore P pieno](shots/smw-guide-03-run-pmeter.png)

## Un tick di movimento

`Phys.step` si muove prima, con la velocità dell'ultimo tick, e solo dopo cambia la velocità. Il test mostra che l'originale fa lo stesso: la posizione è un tick indietro rispetto alla velocità. Il movimento è prima x poi y, quindi un muro ferma Mario sul suo bordo e un pavimento lo ferma sulla sua cima. Una sporgenza è una *piattaforma*: ci atterra solo quando scende nella sua riga.

```lua
function Phys.step(m, inp, solid, plat)
  move(m, solid, plat)                  -- la posizione, con l'ultima velocità
  -- ... poi la velocità: accelera, frena, decelera, il contatore P
  if inp.jump and not m.held and m.ground then
    m.vy = -(Phys.JUMP + Phys.JUMP_SPEED * math.abs(m.vx))     -- un Mario più veloce salta più in alto
  end
end
```

La funzione riceve il test di solidità come parametro, così lo stesso codice gira nel gioco e nel test.

## Come si sono trovate le costanti

Lo script di cattura esegue l'originale in un emulatore con tasti scriptati (cammina, corri, salta, gira, frena, abbassati...) e scrive una riga per tick: posizione e velocità. Il test costruisce una piccola cartuccia con `physics.lua`, esegue gli stessi tasti in `bmhost` e dà un punteggio alla differenza. Poi una ricerca regola una costante alla volta per abbassare il punteggio. Su dodici scenari l'errore medio di posizione è sotto 0,8 pixel; il peggiore è circa 3 pixel.

::: note
Una corsa più lunga, tutta la prima parte del livello con gli stessi tasti, resta entro 5 pixel per i primi 700 tick. Dopo, il contatto con lo spigolo di un gradino è diverso in una corsa e non nell'altra, e le due corse si separano: basta una piccola differenza in una collisione. Le costanti non possono correggerlo.
:::

![A metà salto](shots/smw-guide-04-mid-jump.png)

# Il gioco

## Gli stati

`game.lua` è una piccola macchina a stati: `play`, `dying`, `clear`, `over` e `msg` (un riquadro di messaggio). `tick` chiama il passo dello stato corrente. L'HUD mostra vite, monete, monete drago, tempo e punteggio. Il tempo scende di uno ogni 35 tick.

![L'HUD](shots/smw-guide-08-hud.png)

## I nemici come dati

Ogni nemico è una tabella con un tipo e uno stato. Una tabella `FOE` dice, per ogni tipo, come si muove, quale zona e quale frame mostra, cosa fa un salto sulla testa e cosa ferisce Mario:

```lua
FOE.koopa = { step = koopa_step, zone = koopa_zone, stomp = koopa_stomp,
              hurts = function(k, mx) return k.state == "walk" or ... end }
```

Così il ciclo che muove, disegna e verifica i contatti non conosce nessun nemico. Per aggiungerne uno, aggiungi una voce a `FOE`. Il livello ha il Koopa Troopa (rosso e verde, con un guscio che Mario può calciare), una pianta saltellante che segue il percorso dato dall'originale e due Chargin' Chuck che guardano, caricano e aspettano, e cadono dopo tre salti sulla testa. I Koopa si muovono con `lib.move` e girano contro un muro e alla fine di una sporgenza.

## I blocchi

Una tabella `KIND` segna i blocchi che fanno qualcosa quando sono colpiti dal basso: un blocco punto interrogativo dà una moneta, un altro dà un uovo da cui nasce un dinosauro, un altro fa crescere una liana. Il colpo viene dal codice di movimento: quando la testa di Mario si ferma contro un blocco, `move` lascia il punto in `m.bump`, e `hit_block` legge il blocco lì.

![Colpire un blocco](shots/smw-guide-07-bump.png)

Altri blocchi:

- **I blocchi rotanti** si rompono quando Mario ci atterra con un salto a vite; quattro pezzi volano e cadono.
- **L'interruttore P blu** scambia blocchi rotanti e monete per 704 tick, poi li ripristina. `swap_pswitch` percorre il livello una volta e usa `put16`.
- **I blocchi messaggio** sono sprite solidi; un colpo apre un riquadro di testo e il gioco aspetta un pulsante.

## La liana

Il blocco della liana crea una testa sprite che sale di un pixel per tick, con una cella di foglie disegnata sotto. Premere su vicino ad essa fa afferrare la liana a Mario: la fisica normale si ferma, su e giù lo muovono a velocità fissa, sinistra e destra lo fanno scivolare e un salto lo stacca. È un buon esempio di *modalità*: un flag (`mario.climb`) e una funzione (`climb_tick`) che gira prima di `Phys.step` e dice al gioco se ha preso il tick.

## Il traguardo e la fine

Il traguardo è una barra che sale e scende lungo un ciclo fisso. Attraversarla avvia lo stato `clear`: un banner, il tempo trasformato in punteggio e un riavvio.

# La pipeline degli asset

## Perché una pipeline

La grafica non è nel repository. Uno script legge la ROM e scrive tutto ciò che serve alla build in `carts/smw/extracted/`. Chiunque abbia la propria cartuccia può ricostruire lo stesso gioco. Gli script lavorano in quest'ordine:

| Script | Cosa estrae |
|---|---|
| `scripts/smw_extract.py` | controlla la ROM e decomprime la sua grafica |
| `scripts/smw_map16.py` | i blocchi del livello e i tile di ciascuno |
| `scripts/smw_sprites.py` | le pose di Mario, con i loro colori |
| `scripts/smw_spawns.py` | dove parte ogni nemico |
| `scripts/smw_diff.py` | l'immagine esatta di uno sprite, ottenuta nascondendolo tra due frame |
| `scripts/smw_bg2.py` | il layer dello sfondo |
| `scripts/smw_solid.py` | quali blocchi sono solidi e quali sono piattaforme |
| `scripts/smw_vine.py` | la liana, la sua crescita e le pose di arrampicata |

Due tecniche vale la pena conoscerle. La **cattura per differenza** esegue due frame dallo stesso stato, uno con uno sprite e uno senza, e tiene i pixel che differiscono: l'immagine è esatta anche quando il gioco la costruisce da più parti. Il **test di solidità** mette Mario contro ogni tipo di blocco nell'emulatore e registra cosa succede, invece di leggere una tabella.

## Dalle immagini a un foglio

`sprites.py` taglia ogni immagine in una zona: mette ogni frame in una cella di dimensione fissa, verifica che ci stia, aggiunge i box, scrive la riga di `sprites.txt` e restituisce la tabella Lua `ZA` delle ancore. Il foglio dei tile e quello delle zone sono un'unica immagine, quindi la cartuccia ha un solo `SHEET`.

Lo sfondo ha 637 celle distinte. `bg.py` ripiega l'immagine sul suo periodo, riempie i buchi, la taglia in celle 8x8 e le scrive come terzo layer della mappa.

## L'edizione di questa guida

`carts/smw/guide/build.py` costruisce lo stesso gioco con la grafica **ridisegnata** per ruolo: le forme sono le stesse, i colori sono scelti per ruolo e l'ombreggiatura è procedurale, quindi nessun pixel dell'originale è nell'output. Le immagini di questa guida vengono da lì.

# Provalo

1. Costruisci l'edizione della guida: `python3 carts/smw/guide/build.py`. Scrive `build/smw-guide/guide.bm`.
2. Eseguila in `bmhost` e fai le immagini: `python3 tests/smw/guide_shots.py`. Le immagini vanno in `manuals/shots/`.
3. Apri `carts/smw/src/physics.lua` e cambia `GRAV_HOLD` da 3.4 a 6.8. Ricostruisci e salta: il pulsante non cambia più l'altezza.
4. Apri `game.lua`, trova `FOE.koopa` e cambia cosa restituisce `hurts` per un Koopa che cammina. Ricostruisci e cammina contro uno.
5. Premi `Select` nel gioco per vedere il costo del frame e leggi le righe `bench` nel log.

::: try
Cambia la fisica e guarda il risultato.
:::

# Punti aperti

- Il tempo di frame reale sull'RGB30 non è ancora misurato: su un PC il livello costa circa 0,05 ms a frame in media. [DA COMPLETARE: il numero dalla console, dalle righe `bench` del log]
- Mario grande, i potenziamenti, cavalcare il dinosauro, le altre mosse del Chargin' Chuck e l'audio non sono nel port.
