# M30 — GPU e 3D più veloce: prima e dopo

Confronto tra **prima** (`9cda3c7`, 2026-09-30, l'ultimo commit prima di M30) e **dopo**
(il branch `claude/gallant-newton-z2cugw`, 2026-10-01). Il codice di M30 è tutto
scritto e provato sul PC e in QEMU. Sul Pi il 3D dell'ARM è misurato e la GPU passa
tutto il suo test; manca la prova di Texture Room HD con la GPU, e fino ad allora il
backend GPU resta spento di default (sezione 4).

## 1. In breve

- **3D sull'ARM (il default):** stessi pixel di prima e meno istruzioni. Texture Room
  scende del 30% (5,52 → 3,86 milioni di istruzioni a fotogramma), i pixel con texture
  da 70 a 46 istruzioni l'uno.
- **3D sulla GPU (da accendere):** l'ARM non disegna più pixel, trasforma e illumina
  soltanto. Per Texture Room fa 0,62 milioni di istruzioni a fotogramma, l'89% in meno
  di prima. Da qui il costo dell'ARM dipende dai triangoli e non più dai pixel: la
  stessa stanza a 640×360 (Texture Room HD) costa all'ARM quanto quella a 320×180.
- **Correttezza della GPU:** su un emulatore della V3D le scene disegnate dalla GPU
  differiscono da quelle dell'ARM per lo 0,01–0,75% dei pixel, nei quattro ordini di
  byte possibili. I pixel diversi stanno sui bordi e sui confini dei texel.
- **Sul Pi (2026-10-01):** il 3D sull'ARM è 2,1–2,3× più veloce sui triangoli
  piccoli e i triangoli 2D quasi 2×. Texture Room HD sull'ARM fa 37–41 fps. La GPU,
  dopo una correzione al driver, passa tutto il test `g` (0,1% di pixel diversi
  dall'ARM): 182 sfere a 60 fps contro 69 dell'ARM (6× settembre), 811 Mpixel/s di
  riempimento. Manca solo Texture Room HD con la GPU (sezione 4).

## 2. Cosa c'era e cosa c'è

| | Prima (`9cda3c7`) | Dopo |
|---|---|---|
| Chi disegna il 3D | solo l'ARM | l'ARM (default) o la GPU V3D (*Impostazioni > 3D of the games*) |
| GPU 3D (V3D, 12 QPU) | mai usata | driver nostro a funzioni fisse: liste di controllo, 3 shader QPU, texture dalla TMU, z a 24 bit |
| Rasterizzatore ARM | bordi in virgola mobile, un ciclo texture unico | bordi in virgola fissa 32.32, cicli texture specializzati, scarto delle mesh fuori vista; **pixel identici** |
| Pulizia dello z-buffer | CPU | DMA a fine fotogramma (`dma_zclear=0` per spegnerlo) |
| Risoluzioni delle cartucce | 640×360, 320×180 | in più **480×270** (4× esatto su 1080p) |
| Z tra 2D e 3D (GPU) | — | conservato quando serve (salvato e ricaricato dalla V3D in formato T) |
| Se la GPU non risponde | — | torna all'ARM da sola e lo scrive nel log |
| Stress test | sprite, triangoli, sfere | in più: attesa di fine avvio, clock/temperatura/throttling/interrupt, quad a tutto schermo (ns per pixel), **righe GPU** |
| Prova della GPU sul Pi | — | `g` nel monitor o *Dev > GPU test*: 11 passi sullo schermo, ARM e GPU affiancati |
| Cartucce | Texture Room 320×180 | in più **Texture Room HD** 640×360 (il criterio di chiusura) |
| Per le cartucce | — | `stat(6)` = 1 se il 3D lo fa la GPU; `stat(1)` comprende il lavoro della GPU |
| Strumenti | — | `make bench3d`, `make count-insns` (anche con la GPU), `tools/qpuasm.py`, emulatore V3D, `make test-gpu3d` |
| Test QEMU | 47 | 51 (nuovi: 480×270, GPU assente, ritorno all'ARM, Texture Room HD; stress e menu estesi) |

## 3. Numeri misurati sul PC

Sono istruzioni ARM per fotogramma, contate con `qemu-arm` sulle scene di
`tests/bm/bench3d.c` (`make count-insns`, ARM1176 con `-O2`). Cache e bus non sono
compresi: misurano il lavoro della CPU, non il tempo sul Pi. Con la GPU si conta solo
la parte dell'ARM (r3d e backend, a regime); il tempo della V3D non c'è.

| Scena | Prima | Dopo, ARM | Dopo, GPU (parte ARM) |
|---|---:|---:|---:|
| Texture Room, 8 casse (320×180, 446 triangoli) | 5,52 M | 3,86 M (−30%) | 0,62 M (−89%) |
| Texture Room, 32 casse (561 triangoli) | 8,60 M | 6,27 M (−27%) | 0,80 M (−91%) |
| 30 sfere piatte (1137 triangoli) | 4,15 M | 3,77 M (−9%) | 1,06 M (−74%) |
| 30 sfere Gouraud | 8,95 M | 8,66 M (−3%) | 1,17 M (−87%) |
| 30 sfere con texture | 14,46 M | 12,34 M (−15%) | 1,28 M (−91%) |
| 4 quad con texture (230 400 pixel) | 16,13 M | 10,55 M (−35%) | 0,02 M (−99,9%) |
| 4 quad piatti | 2,87 M | 2,39 M (−17%) | — |
| 4 quad Gouraud | 8,25 M | 7,55 M (−8%) | — |

Per pixel (solo l'ARM):

| Scena | Prima | Dopo |
|---|---:|---:|
| Quad piatti con z | 12,5 | 10,4 |
| Quad Gouraud | 35,8 | 32,7 |
| Quad con texture e luce | 70,0 | 45,8 |
| Sfere piatte (triangoli piccoli) | 24,5 | 21,6 |
| Sfere con texture | 94,8 | 79,9 |
| Texture Room, 8 casse | 84,7 | 57,7 |
| Texture Room, 32 casse | 87,8 | 62,9 |

Per triangolo, la parte dell'ARM fuori dai pixel resta quasi uguale in software
(≈ 530–710 istruzioni). Con la GPU sale a ≈ 930–1400 istruzioni, perché ogni vertice va
scritto nel formato della V3D. È il costo da tenere d'occhio: con la GPU il limite
diventa il numero di triangoli, non i pixel.

**Correttezza della GPU** (emulatore della V3D, `make test-gpu3d`). Sono le
percentuali di pixel diversi tra ARM e GPU (oltre 48 livelli su un canale), uguali nei
quattro ordini di byte:

| Scena | Pixel diversi |
|---|---:|
| Sfere piatte, Gouraud, cubo, nebbia | 0,04% |
| Texture, trasparenze, pavimento sotto la camera | 0,01% |
| Pavimento senza z, `zclear` a metà, 480×270 | 0,03% |
| 330 sfere (più gruppi di triangoli) | 0,65% |
| 700 sfere (due lavori, z conservato tra i due) | 0,75% (1,64% prima di conservare lo z) |
| Tre sprite sheet nello stesso lavoro | 0,06% |
| 3D, 2D, 3D (z conservato) | 0,02% |

## 4. Sul Pi

**Prima (misurato):**

- Texture Room: 8 casse 11,6 ms di CPU, **60 fps** (456 triangoli, 60 516 pixel con
  texture); 32 casse 18,0 ms, **54 fps**.
- Stress test (settembre): ~1200 triangoli 3D a 60 fps, ~5000 a 30 fps, circa 14 µs
  per triangolo disegnato. Il 29 settembre la parte C era più lenta senza una causa
  chiara.
- Astro Wing 6,34 ms, 59,9 fps; Chaos Kitchen 14,1 ms, 54 fps.

**Dopo, 3D sull'ARM (misurato il 2026-10-01, kernel `6c2fdaa`).** Stress test a sistema
fermo: ARM 1000 MHz, core 250 (massimo 400), V3D 250, SDRAM 400 MHz, 49,7 °C, nessun
throttling, interrupt allo 0,4%. Massimo per fotogramma a 60 fps (30 fps tra parentesi):

| Test | Settembre (`792787f`) | 29 set. (`8298b15`) | Ora (`6c2fdaa`) | Ora / settembre |
|---|---:|---:|---:|---:|
| sprite 16×16 (C) | 4482 | 3500 | 4456 | = |
| sprite 32×32 (C) | 1513 | 1183 | 1504 | = |
| triangoli 2D ~170 px | 2594 | 2053 | 5038 | **1,9×** |
| sfere 3D (C) | 31 (1195 tri) | 15 (556 tri) | 70 (2700 tri) | **2,3×** |
| sfere 3D Gouraud | — | <1 (45 a 30 fps) | 18 (84 a 30 fps) | — |
| sfere 3D con texture | — | <1 (<1 a 30 fps) | <1 (51 a 30 fps) | — |
| sprite 16×16 (Lua) | 1829 | 1854 | 1848 | = |
| sfere 3D (Lua) | 29 (1115 tri) | 26 (1015 tri) | 61 (2323 tri) | **2,1×** |

Costo di un pixel (righe nuove `quad`): piatto con z 22 ns, senza z 5 ns, Gouraud 49 ns,
texture 97 ns. Con il codice di prima Texture Room dava 130–180 ns per pixel con texture.

- Il calo del 29 settembre era un disturbo all'avvio: con l'attesa di 20 s gli sprite
  tornano ai valori di settembre.
- Sui triangoli piccoli il guadagno vero (2,3×) è molto più grande di quello previsto
  dalle istruzioni (−9%). Il rasterizzatore di prima fermava la pipeline a ogni riga
  con i confronti in virgola mobile (`vmrs`), e il conteggio delle istruzioni non lo
  vede. I triangoli 2D usano lo stesso rasterizzatore e raddoppiano anche loro.
- **Texture Room HD sull'ARM** (640×360): 8 casse 25,3 ms, 41 fps (450 triangoli,
  249 288 pixel con texture); 16 casse 26,3 ms, 37 fps (485 triangoli, 287 057 pixel).
  Sono 4 volte i pixel di Texture Room nel doppio del tempo.

**Dopo, 3D sulla GPU.** Prima prova (`6c2fdaa`): la V3D si accendeva e rispondeva
(250 MHz, 3 slice × 4 QPU, 2 TMU per slice, VPM 12 KiB), ma la prima pulizia si fermava
con "no end of frame". Dai registri (`CT1CA = CT1EA`, `PCS = 0x104`, `RFC = 0`) si vedeva
che la colpa era dell'attesa nel driver: trattava come errore il bit "binner senza
memoria" di `PCS`, acceso fin dall'avvio anche quando il binner non si usa. Corretto, con
un test sul PC che simula i registri come li ha mostrati il Pi (`make test-v3d`).

Seconda prova (`d0c7fe8`, 2026-10-01): **test `g` superato in tutti gli 11 passi**.

| Passo | Risultato |
|---|---|
| 3 pulizia | 211 µs, byte a = rosso |
| 4 un triangolo | binning 3 µs, rendering 296 µs |
| 6 20 000 triangoli piccoli | preparati dall'ARM in 7,7 ms; binning 2,7 ms + rendering 4,1 ms: **3,0 milioni di triangoli/s** |
| 7 20 schermi interi | rendering 5,7 ms: **811 Mpixel/s** (l'ARM: ~45 Mpixel/s in tinta unita) |
| 8 texture dalla TMU | ok, texel nello stesso ordine del tile buffer |
| 10 stessa scena ARM e GPU (3309 triangoli, 640×360) | ARM 28,8 ms, GPU **9,1 ms** (di cui binning 0,6 e rendering 1,5); **0,1%** di pixel diversi |
| 11 3D, 2D, 3D | ARM 9,5 ms, GPU 3,9 ms (2 lavori con lo z salvato e ricaricato); **0,0%** di pixel diversi |

Righe GPU dello stress test (stesse scene delle righe ARM; massimo per fotogramma):

| Test | ARM 60 fps | GPU 60 fps | GPU 30 fps | GPU / ARM |
|---|---:|---:|---:|---:|
| sfere 96 piatte | 69 (2679 tri) | **182 (7142 tri)** | 389 (15 346 tri) | 2,6× |
| sfere Gouraud | 18 (703 tri) | **170 (6673 tri)** | 364 (14 360 tri) | 9,4× |
| sfere con texture | <1 (52 a 30 fps) | **156 (6094 tri)** | 333 (13 135 tri) | — |
| quad 320×180 piatti | 11 (22 ns/px) | **200 (1 ns/px)** | 430 | 18× |
| quad 320×180 Gouraud | 5 (49 ns/px) | **199 (1 ns/px)** | 427 | 40× |
| quad 320×180 texture | 2 (98 ns/px) | **28 (9 ns/px)** | 60 | 14× |

Rispetto a settembre (31 sfere, 1195 triangoli a 60 fps) sono sei volte i triangoli.
Cosa dicono i numeri:

- Con la GPU il limite è l'ARM, non la V3D. Nel passo 10 la GPU lavora 2,1 ms su 9,1:
  il resto (≈ 2 µs per triangolo) è r3d che trasforma, illumina e scrive i vertici. Lo
  stesso si vede sulle sfere (80 µs per sfera di ~39 triangoli).
- Le texture sono il punto debole della GPU: 9 ns per pixel contro 1. Una texture
  256×256 in ordine di riga (RGBA32R) sfrutta male la cache della TMU; il formato a tile
  (T-format) dovrebbe avvicinarle al piatto.
- Salvare e ricaricare lo z costa circa 1 ms per lavoro a 640×360 (passo 11), come
  previsto: per questo si fa solo quando serve.
- La parte Lua dello stress è girata sull'ARM (in *Impostazioni* il 3D era su `ARM`).

Da provare ancora: **Texture Room HD con la GPU** (il criterio di chiusura di M30). Dai
numeri sopra ci si aspetta 60 fps: ~450 triangoli costano all'ARM circa 1 ms, e a
640×360 la V3D disegna lo schermo con le texture in 2–3 ms.

## 5. Differenze rimaste tra ARM e GPU

- Gouraud senza dithering sulla GPU: le sfumature sono più lisce.
- Z a 24 bit sulla GPU contro 16 sull'ARM: meno errori di profondità sugli oggetti
  vicini tra loro.
- Sulla GPU lo z-buffer riparte da zero a ogni fotogramma, anche senza `zclear()`.
- `stat(5)` (pixel 3D) vale 0 con la GPU.
- Il 3D dopo il 2D nello stesso fotogramma: lo z si conserva dal secondo fotogramma
  in poi. Nessuno dei giochi lo fa: disegnano tutti prima il 3D e poi l'HUD.

## 6. Cosa resta

- Texture Room HD con la GPU sul Pi. Se fa 60 fps, M30 si chiude e il 3D della GPU può
  diventare il default.
- Il costo per triangolo dell'ARM (≈ 2 µs) ora è il limite: da ridurre in r3d per il
  percorso GPU, o spostando le trasformazioni sulle QPU.
- Texture in T-format per la TMU (oggi 9 ns per pixel con texture contro 1 senza).
- Rinviati: MSAA 4× (caricare la pagina in un tile multicampione non è un percorso di
  Mesa né di Linux, quindi va provato sul Pi), filtro bilineare (cambia l'aspetto delle
  texture), sprite 2D sulla GPU.
