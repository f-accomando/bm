# 3D Bench

Il benchmark unico delle capacità 3D di bm: *Dev > 3D Bench*, o il tasto `j` del
monitor. Codice: `src/bm/b3d.c` (portabile), `src/kernel/b3dpi.c` (il Pi),
`tests/bm/b3d_host.c` (il PC, `make test-b3d`).

## Cosa fa

Ogni test è una scena il cui carico `n` cresce (sfere, eroi, quad di pixel, chiamate…).
Ogni test gira con ogni **profilo**, cioè con ogni driver che il codice di oggi sa
riprodurre (`docs/DRIVERS.md`):

- **ARM**: bm3d 0.2, il rasterizzatore dell'ARM, come prima della GPU;
- **GPU**: bm3d 2.1, la GPU disegna i triangoli che l'ARM prepara;
- **GPU+AA**: la stessa con l'MSAA 4×;
- **GPU+VS1**: bm3d 3.0, il vertex shader per lo scenario;
- **GPU+VS**: bm3d 3.4, il vertex shader per tutto;
- **GPU+VS+Q**: bm3d 4.1, lo stesso con il fotogramma in coda (M35; solo nel test `queue`);
- **GPU+FS2**: gli shader dei pixel con texture a due thread (bm3d 5.3, tutti quelli del 3D dalla
  6.3), nei test con texture; dalla 6.4 sono il default, e la riga GPU li riproduce a un thread.
  Quelli a colore restano a un thread (a due il Pi li ha misurati più lenti del 4–5%).
- **GPU+VS+FS2**: il driver come lo hanno i giochi dalla 6.6 (vertex shader per tutti i modelli e
  shader con texture a due thread); gira in ogni test della GPU (non in `bilinear`, che prova
  un'opzione) e lo score usa lui.

Un profilo che la GPU non sa fare (le prove all'avvio lo hanno spento) non ha la riga.
A ogni passo `n` cresce di un terzo finché un fotogramma supera i **40 ms**; i carichi a
**60 fps** (16,7 ms) e a **30 fps** (33,3 ms) sono interpolati tra due passi. Ogni passo
misura 6 fotogrammi dopo uno di riscaldamento (texture, copie degli angoli, cache) e vale
il fotogramma **di mezzo** (la mediana; i contatori sono la media): un fotogramma rallentato
da altro, come la rete che stampa durante una riga del monitor, non sposta il passo. Il carico
parte dall'**ultimo** passo sotto il limite: un passo lento prima di uno più pesante che ci
sta era stato rallentato da altro (sul Pi, il 2026-10-05, `quad_flat` con la GPU: 25 quad sì,
33 no, 44 sì, e il report diceva 28,7). Il tempo di un fotogramma comprende la copia sullo
schermo, come nello stress test.

Le sfere e i quad sono le scene dello stress test, alla stessa risoluzione (640×360): così
i numeri del Pi con i driver di prima sono le barre storiche.

## I test

- `spheres`, `spheres_smooth`, `spheres_tex`: sfere di 96 facce piatte, Gouraud, con
  texture (quelle dello stress test);
- `spheres_unlit`, `spheres_baked`: spente, con la luce agli angoli (lo scenario: anche
  GPU+VS1);
- `spheres_shine`: cielo e terra, bordo, riflessi, 4 lampade, nebbia;
- `heroes`: eroi di 16 ossa e 1536 facce, Gouraud, ossa in movimento; `heroes_tex`: gli
  stessi con la texture; `heroes_skin`: con la texture e le giunture su due ossa (come i
  modelli Meshy di Overbit); `heroes_shadow`:
  con le ombre su un pavimento;
- `clip`: pezzi di mappa attorno e sotto la camera, i più vicini attraverso il piano
  vicino (li taglia la GPU);
- `tiny`: facce di circa 4 pixel (il costo di preparare un triangolo);
- `draws`: un cubo per chiamata (il costo di una chiamata);
- `quad_flat`, `quad_smooth`, `quad_tex`, `quad_alpha`, `quad_screen`: riempimento, quad
  di 320×180 (piatti, Gouraud, texture, texel con buchi, retino);
- `texswap`: tre texture a turno (la GPU ne tiene due);
- `split`: 3D e 2D sopra, più volte nello stesso fotogramma (lavori della GPU spezzati,
  z conservato);
- `match`: una partita sintetica, mappa di 100 pezzi, eroi con le ombre, un modello in
  prima persona e un HUD;
- `mix`: tutto insieme, come un gioco (M41, per lo score): n fette di una strada, ognuna un pezzo
  di mappa con la luce cotta sotto un eroe (a turno Gouraud con i riflessi, con la texture, con la
  pelle su due ossa) con la sua ombra, un recinto di texel con buchi, un vetro a retino e una
  cassa; il suolo sotto la camera attraverso il piano vicino, cielo, bordo, 4 lampade e nebbia, un
  modello in prima persona in basso a destra e un HUD di sprite e testo sopra il 3D;
- `queue`: sfere Gouraud più un lavoro fisso dell'ARM dopo il 3D (4 milioni di istruzioni,
  la logica di un gioco), con GPU+VS e GPU+VS+Q: in coda l'ARM lavora mentre la GPU disegna;
- `gpu2d`: sprite e testo sopra il 3D (M37; con GPU+2D nel lavoro della GPU, altrimenti l'ARM);
  `bilinear`: quad con le texture filtrate (M37, un'opzione spenta di default);
- `big`, `big_logic`: modelli di 10 080 triangoli su una mappa di 14 112 (M39), il secondo con la
  logica di `queue`.

I carichi massimi sono molto oltre quello che i driver fanno oggi (fino a 8000 sfere, 512
eroi, 40 000 chiamate): restano margine per i driver che verranno.

## Le statistiche

Per ogni test e profilo, al passo che sta ancora nei 60 fps: carico, ms (media e peggiore),
fps, triangoli disegnati, vertici messi dall'ARM, pixel (dell'ARM), ms della GPU
(binning + rendering), lavori della GPU, **istruzioni dell'ARM** a fotogramma senza quelle
spese ad aspettare la GPU (e quelle a parte), istruzioni per triangolo e per elemento,
**cache miss** dei dati. Le istruzioni e i miss vengono dai contatori dell'ARM1176
(`src/kernel/pmu.c`), solo sul Pi vero (QEMU non li ha).

## Lo score

La prima pagina alla fine (richiesta dell'utente, 2026-10-06). Due numeri:

- **Score**: per ogni test che è una tecnologia (24: sfere piatte, Gouraud, texture, spente, luce
  cotta, luci; eroi, texture, pelli, ombre; clip, facce piccole, chiamate; i riempimenti; tre
  texture; 3D e 2D; partita; mesh grandi; 2D sopra il 3D) il carico a 60 fps diviso per quello
  che **bm3d 2.1** (il profilo GPU) ha dato al suo meglio sul Pi Zero W (report del 5 e 6 ottobre,
  `score_ref` in `b3d.c`); lo score è la media geometrica dei rapporti per 1000. Quindi 1000 è
  bm3d 2.1 sul Pi, 2000 un driver due volte più veloce in ogni test (o 4 volte in metà e uguale
  nel resto). Un carico sotto il primo passo vale la parte di quel passo che sta in 16,7 ms (un
  eroe in 20 ms: 0,83). Fuori dallo score: i test con la logica (`queue`, `big_logic`), le
  opzioni spente di default (`bilinear`) e `mix` (ha l'altro numero). Lo score grande è del driver
  **come lo hanno i giochi** in questa versione: dalla 6.6 la riga GPU+VS+FS2 (in bm3d 6.4 e 6.5
  era la GPU con gli shader con texture a due thread: la riga GPU+FS2 dove c'è, la GPU negli altri
  test); senza quella riga (la prova del vertex shader non è passata, e i giochi non lo hanno)
  GPU+FS2, poi la GPU, e l'ARM se la GPU non c'è, come la RGB30. Accanto, gli score di bm3d 2.1 (GPU) e 0.2 (ARM) negli
  stessi test e quello del report di prima (verde se non è sceso più del 3%). Un giro con
  `tests=` o `profiles=` dice "a part of the bench": non si confronta.
- **Triangoli a fotogramma a 60 fps** (640×360): quelli della scena `mix` con il driver dei
  giochi, interpolati come il carico, e quanti la GPU ne ha disegnati davvero (gli altri sono
  facce girate o fuori dallo schermo); sotto, il test con più triangoli a 60 fps e quanti al
  secondo.

Sotto, una barra per test: il suo rapporto contro bm3d 2.1 (la tacca è 1×) e il profilo che
è il driver lì. Il report ha le righe `score ...`, `S,driver,versione,score,test,intero` (il
giro dopo legge `S,games`) e `triangles at 60 fps ...`; ogni riga `R` ha in fondo `tris60` e
`drawn60`. Le ombre contano come triangoli (una mesh disegnata di nuovo, schiacciata: r3d le conta
dal kernel `v0.2.3-51`; il primo report con lo score, il `-50`, le lasciava fuori).

## I clock

La riga `machine` dice i clock dell'ARM, del core e della V3D (quello chiesto, il massimo che il
firmware permette e quello misurato) e della SDRAM; a ogni passo il bench rilegge il clock
**misurato** del core e della V3D (`GET_CLOCK_RATE_MEASURED`, fuori dal tempo del passo) e le righe
`R` li hanno in fondo (`core_mhz`, `gpu_mhz`, del passo a 60 fps), anche le righe `b3d` del log.
Servono a capire perché i quad del profilo GPU a volte vanno a metà velocità o meno nello stesso
giro (`quad_flat` 175 in un giro, 91 o 70 in un altro, con gli stessi driver). Sul Pi Zero W
`enable_uart=1` di `boot/config.txt` tiene il core a 250 MHz (la seriale), e anche la V3D si è
vista a 250; `v3d_clock=max` in `bm/config.txt` le chiede all'avvio il massimo del firmware (una
prova, spenta di default).

## Le pagine alla fine

- **Score** (sopra);
- **Riepilogo**: per ogni test il carico a 60 fps di ogni profilo, il migliore, quante
  volte l'ARM, quante volte il report di prima, se sta nei 60 fps; la media (geometrica)
  di ogni profilo contro l'ARM; i test non ancora sviluppati.
- **Driver**: le versioni bm3d, i profili, la macchina (clock, temperatura, throttling),
  lo stato della GPU, il report salvato.
- **Un test per pagina**: le barre (60 fps acceso, 30 fps spento) di ogni profilo con una
  tacca bianca al valore del report di prima; in grigio i numeri misurati sul Pi con i
  driver di prima (0.1, 0.2, 1.0: `docs/M33-PRIMA-DOPO.md`); in rosso il limite
  dell'hardware dove c'è (la V3D: 1 Gpixel/s e 1,5 Gtexel/s secondo Raspberry Pi; 3,0
  milioni di triangoli al secondo misurati dal test `g`); sotto, la tabella delle
  statistiche.

Sinistra/destra cambiano pagina, B (o Esc) esce.

## I report

Ogni giro salva `bm/bench/3D0001.TXT`, `3D0002.TXT`… sulla SD: un'intestazione (kernel,
driver, data se c'è la rete, macchina, stato della GPU) e una riga CSV per test e profilo
(`R,test,profilo,versione,n60,n30,…`; le colonne sono nella riga `columns`). Il giro
dopo legge l'ultimo report e lo confronta (tacche bianche, colonna "x last"). Per
tenerne la storia nel repository, copiarli in `docs/bench/`.

## Una parte, dal monitor

La riga di comandi del monitor (`:`) fa girare solo alcuni test e profili, senza aspettare
sulle pagine, e può mettere in fila altri comandi:

```
:gpu; b3d tests=match,quad_tex profiles=GPU,FS2; send
```

`tests=` prende gli id dei test (`spheres`, `match`, `quad_tex`…), `profiles=` i nomi dei
profili (`GPU+FS2`) o quelli brevi del riassunto (`FS2`); senza, tutto. Il report dice la parte
(`only tests …, profiles …`). `set chiave=valore` cambia una chiave di `bm/config.txt` fino al
riavvio (per provare un gioco), `save` la tiene.

La riga si scrive anche nel menu: il `:` la porta al monitor. Dal PC: `./easy_install.sh line
"gpu; b3d; send"` (o `tools/bm_net.py IP --line "..."`) la manda e mostra quello che la console
scrive finché non dice `the line is done`; nel monitor di easy_install (6) si incolla `:gpu;
b3d; send` e Invio. La console dice al PC dove vanno i tasti (il menu, che non li ripete, o il
monitor).

## Sul PC

`make test-b3d` fa due giri brevi sull'emulatore della V3D (pochi passi, un fotogramma
l'uno: i ms del PC non contano) e controlla report, profili, score, confronto e pagine
(`build/b3d/page-NN.ppm`). `build/host/b3d_host DIR --full -v` fa il giro intero;
`--tests=mix --profiles=GPU,ARM` una parte, `--frames` salva ogni fotogramma
(`DIR/frame-NNNN.ppm`) per guardare le scene.
