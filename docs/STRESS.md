# Stress test di rendering — bm su Raspberry Pi Zero W

Obiettivo: capire **quanti oggetti per frame** può disegnare bm prima di scendere sotto
**60 fps** (16,7 ms per frame) e sotto **30 fps** (33,3 ms per frame), per sprite e
per poligoni 2D/3D, sia dal C sia attraverso l'API delle cartucce Lua (`.bm`).

> **Stato:** misurato sul Pi Zero W reale (kernel `792787f`, 2026-09-26). I valori
> QEMU sono riportati solo come controllo: QEMU non emula cache, bus di memoria né
> tempi reali.

## Metodo

- Modo video delle cartucce native: **640×360, RGB565, doppio buffer**.
- Per ogni test il carico parte da un valore iniziale e cresce del 50% a ogni passo
  (1, 2, 3, 4, 6, 9, 13, … oppure 16, 24, 36, …).
- A ogni passo si disegnano **20 frame** e si misura il **solo tempo di disegno**
  (per la parte Lua: `_update` + `_draw`), senza l'attesa del ritmo a 60 Hz.
- Ci si ferma quando un passo supera 40 ms. Le soglie a 16,7 ms e 33,3 ms si ricavano
  per **interpolazione lineare** tra i due passi a cavallo (il costo cresce in modo
  circa lineare con il numero di oggetti).
- "us/item" è la pendenza: il costo di un oggetto in più, in microsecondi.
- Ogni frame parte da `cls` (schermo intero), quindi il costo fisso di pulizia è incluso.
- I dettagli di ogni passo escono sulla seriale; lo schermo mostra la tabella finale.

## Test

| Test | Cosa disegna | Note |
|---|---|---|
| sprites 16×16 (C) | N sprite 16×16 con trasparenza, metà specchiati, in movimento | blit C con maschera |
| sprites 32×32 (C) | come sopra, 32×32 | 4× i pixel per sprite |
| triangles 2D ~170px | N triangoli pieni di circa 20 px di lato (≈170 pixel l'uno) | rasterizzatore a scanline |
| 3D spheres 96 (C) | N sfere di 96 triangoli (6×8), ruotanti, prospettiva, z-buffer, luce per faccia | circa il 40% dei triangoli è visibile (gli altri vengono scartati come facce posteriori) |
| 3D smooth (Gouraud) | le stesse sfere con `R3D_SMOOTH`: luce per vertice, colore sfumato e dithering | dal kernel con Gouraud (M14), da misurare sul Pi |
| 3D textured | le stesse sfere con una texture a scacchi 16×32 dello sheet (prospettiva corretta) | dal kernel con Gouraud (M14), da misurare sul Pi |
| sprites 16×16 (Lua) | come il test C, ma ogni sprite è una chiamata `spr()` da Lua con il calcolo della posizione in Lua | costo reale per una cartuccia |
| 3D spheres 96 (Lua) | come il test C, con `draw3d()` chiamato da Lua | trasformazioni e raster in C |
| quad 320×180 flat / no z / Gouraud / texture | N quad di 320×180 pixel (un quarto dello schermo, cioè uno schermo 320×180 intero), uno per quadrante a turno, ciascuno più vicino del precedente: ogni pixel passa lo z-buffer e viene scritto | la pendenza è il **costo di un pixel** (colonna `ns/px`), senza il lavoro per triangolo delle sfere; texture 256×256, più grande della cache dati (M33) |
| GPU spheres 96 / smooth / textured, GPU quad flat / Gouraud / texture | le stesse scene con il 3D disegnato dalla GPU (backend `gpu3d`, M33): l'ARM trasforma, illumina e taglia, la V3D riempie i pixel; il tempo comprende il lavoro della GPU | solo sul Pi (in QEMU la riga `GPU rows: none (...)` dice perché mancano); i quad GPU arrivano a 2000, ognuno più vicino del precedente di un passo dello z a 24 bit |
| GPU spheres AA 4x, GPU quad AA 4x | sfere e quad piatti della GPU con l'anti-aliasing MSAA 4× (M34): tile di 32×32 pixel con 4 campioni l'uno | il costo dell'MSAA rispetto alle righe GPU senza; `no MSAA on this GPU` se la prova all'avvio non lo trova |
| GPU+VS spheres 96, GPU+VS smooth | le sfere delle righe GPU con i vertici messi dal vertex shader della GPU (M36): l'ARM manda solo matrici e luci | quante sfere in più rispetto alle righe GPU (obiettivo di M36: almeno il doppio a 60 fps); `no vertex shader for lit models on this GPU` se le prove all'avvio non lo trovano |

Per il 3D la tabella riporta il numero di sfere e, tra parentesi, i **triangoli
effettivamente disegnati** per frame.

## Pipeline 3D (software)

Il 3D lo disegna la GPU (V3D, M33: `src/gpu/gpu3d.c`) dopo che l'ARM ha trasformato,
illuminato e tagliato; il rasterizzatore software sull'ARM (`src/bm/r3d.c`, descritto
qui sotto) resta con `gpu3d=0`, in QEMU, se la GPU non risponde e per quello che la GPU
non sa ancora fare (ombre, effetti 3D, retino, texture con la luce precalcolata).
- trasformazione per vertice in virgola mobile (VFP), camera con yaw/pitch e FOV;
- eliminazione delle facce posteriori e degli oggetti dietro la camera (senza
  clipping sul piano vicino: un triangolo che lo attraversa viene scartato);
- triangoli pieni a scanline con **z-buffer a 16 bit** (1/z), ombreggiatura piatta
  (Lambert per faccia + luce ambiente);
- dal 2026-09-27 texture (prospettiva corretta) e clipping sul piano vicino; dal
  2026-09-29 Gouraud (flag `R3D_SMOOTH`, luce per vertice, dithering 4×4). Le misure
  qui sotto sono del rasterizzatore piatto.

API per le cartucce: `mesh`, `mesh_sphere`, `mesh_cube`, `draw3d`, `camera3d`,
`light3d`, `zclear`, `tri` (vedi README).

![scena 3D del test](stress-3d.png)

## Risultati

**Versione del kernel:** la sigla in alto a sinistra sullo schermo (barra azzurra,
es. `bm 1b31924`) è il commit git con cui è stato compilato il kernel. Lo stress
test esiste dalla versione `1b31924`: con una sigla diversa (es. `7044581`, che è M5)
sulla SD c'è un kernel più vecchio. Se compare `-dirty`, il kernel è stato compilato
con modifiche locali non committate.

| Misura | Versione | Data |
|---|---|---|
| Pi Zero W v1.1, 1 GHz, cache attive | `792787f` | 2026-09-26 |
| QEMU (controllo) | `1b31924` | 2026-09-26 |
| Pi Zero W, ripetuto (disegno diretto) | `89452b3` | 2026-09-27 |

> Lo stress test conta anche l'eventuale copia sullo schermo quando le cartucce
> disegnano via RAM (comando `V`, non il default). I numeri qui sotto sono stati
> misurati disegnando direttamente nella memoria video, come fa il default; ripetuti
> con `89452b3` danno gli stessi valori (4481 sprite 16×16, 1207 triangoli 3D a 60 fps).
> Il benchmark `p` sullo stesso kernel: mappa piena + 256 sprite 8,50 ms diretti,
> 12,65 ms via RAM.

Numero massimo di oggetti per frame (640×360, RGB565):

| Test | **Pi 60 fps** | **Pi 30 fps** | **Pi µs/oggetto** | QEMU 60 fps | QEMU 30 fps |
|---|---:|---:|---:|---:|---:|
| sprites 16×16 (C) | **4482** | **9255** | 3,49 | 2133 | 4088 |
| sprites 32×32 (C) | **1513** | **3130** | 10,30 | 635 | 1363 |
| triangles 2D ~170px | **2594** | **5368** | 6,00 | 1340 | 2680 |
| 3D spheres 96 (C) | **31** (1195 tri) | **129** (5011 tri) | 162,88 | 16 (602 tri) | 53 (2065 tri) |
| sprites 16×16 (Lua) | **1829** | **3774** | 8,52 | 994 | 1822 |
| 3D spheres 96 (Lua) | **29** (1115 tri) | **120** (4692 tri) | 169,97 | 16 (603 tri) | 48 (1868 tri) |

Ripetibilità: una seconda esecuzione sullo stesso Pi ha dato gli stessi valori entro
±1 oggetto (es. 4481 contro 4482 sprite, 5010 contro 5011 triangoli), quindi la misura
è stabile.

### Cosa dicono i numeri del Pi

- **Sprite in C:** ~4500 sprite 16×16 a 60 fps. Il costo è quasi tutto nei pixel:
  3,5 µs per 256 pixel (~14 ns/pixel); il 32×32 ha 4× i pixel e costa 3× (10,3 µs),
  quindi la gestione del singolo sprite pesa poco e il limite è la banda verso la
  memoria video.
- **Sprite da Lua:** ~1800 a 60 fps. La differenza con il C (8,5 − 3,5 ≈ **5 µs per
  sprite**) è la logica Lua del test (calcolo della posizione) più la chiamata `spr()`.
  Per un gioco: centinaia di sprite con logica vera restano abbondantemente a 60 fps.
- **Triangoli 2D** (~170 px): ~2600 a 60 fps, 6 µs l'uno (~35 ns/pixel compresa la
  preparazione della scanline).
- **3D:** ~**1200 triangoli disegnati** a 60 fps, ~5000 a 30 fps (circa 14 µs per
  triangolo disegnato, comprese trasformazioni, facce scartate, pulizia di schermo e
  z-buffer). Da Lua il costo è quasi identico (+4%), perché tutto il lavoro 3D è in C.
  È un budget da console di fine anni '90 in software: scene low-poly a 60 fps,
  scene più ricche a 30 fps.
- **Confronto con il budget dichiarato per `.bm`** (mappa piena + 256 sprite sotto il
  25% del frame): 256 sprite 16×16 costano ~0,9 ms, ampiamente dentro.

Dati di riferimento già misurati sul Pi reale (M3–M7): riempimento 640×360 a 32 bit
2,2 ms; demo C con 64 sprite 2,7 ms per frame; Lua circa 100 ns per operazione semplice.

### Misura del 2026-09-29 (`8298b15`)

Tabella e analisi in `docs/ROADMAP.md` (M14): parte C più lenta che a settembre, parte
Lua uguale; da allora lo stress test stampa clock, temperatura, throttling e un ciclo di
sola CPU prima e dopo la parte C, per capire la differenza alla prossima misura.

### Misura del 2026-10-01 (`6c2fdaa`, M33)

A sistema fermo (la parte C aspetta 20 s dall'avvio): ARM 1000 MHz, core 250 (massimo
400), V3D 250, SDRAM 400 MHz, 49,7 °C, nessun throttling, interrupt 0,4% (1188/s).

| Test | Pi 60 fps | Pi 30 fps | µs/oggetto |
|---|---:|---:|---:|
| sprites 16×16 (C) | 4456 | 9204 | 3,50 |
| sprites 32×32 (C) | 1504 | 3113 | 10,36 |
| triangles 2D ~170px | **5038** | 10424 | 3,09 |
| 3D spheres 96 (C) | **70** (2700 tri) | 226 (8914 tri) | 114,11 |
| 3D smooth (Gouraud) | 18 (705 tri) | 84 (3276 tri) | 221,84 |
| 3D textured | <1 | 51 (1987 tri) | 238,64 |
| quad 320×180 flat | 11 | 24 | 22 ns/px |
| quad 320×180 no z | 53 | 114 | 5 ns/px |
| quad 320×180 Gouraud | 5 | 11 | 49 ns/px |
| quad 320×180 texture | 2 | 5 | 97 ns/px |
| sprites 16×16 (Lua) | 1848 | 3817 | 8,42 |
| 3D spheres 96 (Lua) | 61 (2323 tri) | 204 (7994 tri) | 126,70 |

Gli sprite tornano ai valori di settembre, quindi il calo del 29 settembre era un
disturbo dell'avvio. Il rasterizzatore di M33 (bordi in virgola fissa, cicli delle
texture specializzati) porta i triangoli 2D a 1,9× e le sfere 3D a 2,3× rispetto a
settembre. Le righe GPU mancano (`GPU rows: none (probe: a clear did not finish)`):
era un errore nell'attesa del driver, corretto dopo questa misura (vedi
`docs/M33-PRIMA-DOPO.md`).

Con `d0c7fe8` (stesso giorno, driver corretto) le righe ARM ripetono gli stessi valori
entro un oggetto, e compaiono le righe GPU:

| Test | Pi 60 fps | Pi 30 fps | µs/oggetto |
|---|---:|---:|---:|
| GPU spheres 96 | **182** (7142 tri) | 389 (15346 tri) | 80,10 |
| GPU smooth (Gouraud) | **170** (6673 tri) | 364 (14360 tri) | 85,58 |
| GPU textured | **156** (6094 tri) | 333 (13135 tri) | 93,16 |
| GPU quad flat | **200** | 430 | 1 ns/px |
| GPU quad Gouraud | **199** | 427 | 1 ns/px |
| GPU quad texture | **28** | 60 | 9 ns/px |

Con la GPU le sfere sono limitate dall'ARM (trasformazioni e vertici, ~2 µs per
triangolo), i quad dalla V3D: 1 ns per pixel in tinta unita o Gouraud, 9 con la texture
256×256 in ordine di riga.

Dopo M33 (M34, da misurare sul Pi): la pagina pulita con `cls` non viene riletta dalla
GPU, le texture con i lati multipli di 32 vanno in T-format (la texture 256×256 dei quad
ne approfitta: da confrontare con i 9 ns/px qui sopra) e due righe nuove misurano
l'MSAA 4×.

### Misura del 2026-10-05 (kernel v0.2.0, bm3d 4.2)

Report `stress` nel branch `reports`. ARM 1000 MHz, core 250 (massimo 400), V3D 250, SDRAM
400 MHz, 47,6 → 52,9 °C, nessun throttling, interrupt 0,5% (1188/s). Le righe 3D: ARM come
bm3d 0.2, GPU come 2.1; le righe GPU+VS mancano perché la prova del vertex shader all'avvio
non è passata (`vertex shader no`, `docs/ROADMAP.md`, M36).

| Test | Pi 60 fps | Pi 30 fps | µs/oggetto |
|---|---:|---:|---:|
| sprites 16×16 (C) | 4454 | 9200 | 3,51 |
| sprites 32×32 (C) | 1501 | 3108 | 10,37 |
| triangles 2D ~170px | 4726 | 9777 | 3,29 |
| 3D spheres 96 (C) | 63 (2451 tri) | 201 (7910 tri) | 128,01 |
| 3D smooth (Gouraud) | 25 (990 tri) | 99 (3846 tri) | 212,54 |
| 3D textured | <1 | 50 (1910 tri) | 259,37 |
| quad 320×180 flat | 11 | 25 | 22 ns/px |
| quad 320×180 no z | 52 | 113 | 5 ns/px |
| quad 320×180 Gouraud | 6 | 13 | 41 ns/px |
| quad 320×180 texture | 3 | 6 | 81 ns/px |
| GPU spheres 96 | **207** (8131 tri) | 433 (17115 tri) | 73,02 |
| GPU smooth (Gouraud) | 159 (6224 tri) | 334 (13178 tri) | 94,38 |
| GPU textured | 165 (6465 tri) | 347 (13675 tri) | 90,64 |
| GPU quad flat | 203 | 428 | 1 ns/px |
| GPU quad Gouraud | 199 | 419 | 1 ns/px |
| GPU quad texture | **91** | 193 | **3 ns/px** |
| GPU spheres AA 4x | 204 (8043 tri) | 429 (16963 tri) | 73,53 |
| GPU quad AA 4x | 199 | 420 | 1 ns/px |
| sprites 16×16 (Lua) | 1990 | 4110 | 7,85 |
| 3D spheres 96 (Lua) | 193 (7559 tri) | 393 (15499 tri) | 82,41 |

Con la GPU le sfere passano da 182 a 207 (73 µs a sfera invece di 80: meno lavoro dell'ARM
per triangolo, M34) e la texture 256×256 dei quad da 9 a 3 ns per pixel (le tile); l'MSAA
costa quasi niente. Le sfere da Lua ora vanno sulla GPU (193). Sull'ARM, rispetto al
1° ottobre, le sfere piatte calano (70 → 63) e Gouraud sale (18 → 25).

## Come eseguirlo sul Pi

```sh
cd ~/bm && git pull
make sdcard-stress               # kernel dedicato: esegue lo stress test all'avvio
sudo umount /mnt/d 2>/dev/null; sudo mount -t drvfs D: /mnt/d
cp dist/kernel.img /mnt/d/ && sync && cmp dist/kernel.img /mnt/d/kernel.img && echo "COPIA OK"
sudo umount /mnt/d
```

All'avvio, dopo il riepilogo di sistema, scorrono i sei test (circa 30–60 s in tutto)
e alla fine resta sullo schermo la tabella dei risultati: basta una foto.
Per tornare al kernel normale: `make sdcard` e ricopiare `dist/kernel.img`.
Con il cavo seriale lo stesso test si avvia dal monitor con il tasto `S`.

## Le righe della macchina (M33)

Prima e dopo la parte C lo stress test scrive due righe:

- `ARM ... MHz, core ... (max ...), V3D ..., SDRAM ... MHz, ... C`: i clock. Il **core**
  regola la cache L2 e il bus della memoria; `enable_uart=1` in `config.txt` lo fissa a
  250 MHz (con `force_turbo=1` il firmware lo fissa invece alla frequenza turbo, 400 MHz
  sul Zero: da provare, confrontando le righe `quad`).
- `throttled ..., loop ... ms, irq ...% (N/s; ... )`: i flag di throttling del firmware
  (0 = niente), il tempo di un ciclo di sola CPU e la parte del tempo passata negli
  interrupt (audio, Bluetooth, timer...), con i due più pesanti: è il tempo che il
  disegno non ha.

Il kernel `make sdcard-stress` aspetta che il kernel giri da 20 s prima di partire: subito
dopo l'avvio il WiFi si collega e il pad abbinato si riconnette, e i primi test lo
pagavano (con `8298b15` la sfera 3D da Lua andava quasi il doppio di quella in C).

## Come leggere i risultati

- **C vs Lua:** la differenza tra le due righe "sprites 16×16" è il costo della logica
  Lua per sprite (calcolo della posizione + chiamata `spr`): è ciò che paga una cartuccia.
- **32×32 vs 16×16:** se il costo per sprite cresce circa 4×, il limite è il numero
  di pixel (banda verso la memoria video), non la gestione degli sprite.
- **3D:** il limite è il numero di triangoli disegnati e la loro area. Le sfere del
  test sono piccole (poche centinaia di pixel per triangolo al massimo); triangoli
  grandi costano di più.
- **Quad:** `ns/px` è il costo di un pixel scritto per ciascun modo; per esempio con
  100 ns/px le texture riempiono in 16,7 ms circa 167 000 pixel (meno il resto del
  frame).

## Possibili ottimizzazioni (dopo le misure)

- scrittura degli sprite a 32 bit (2 pixel per volta) e percorsi dedicati per i flip;
- rasterizzatore in virgola fissa con incrementi per scanline invece di divisioni;
- ridisegno solo delle zone cambiate (dirty rectangles) per i giochi a schermo fisso;
- Gouraud e texture solo se il budget lo permette;
- a lungo termine: la GPU 3D del VideoCore IV (V3D) senza sistema operativo,
  un progetto a parte.
