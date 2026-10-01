# M30 — GPU e 3D più veloce: prima e dopo

Confronto tra **prima** (`9cda3c7`, 2026-09-30, l'ultimo commit prima di M30) e **dopo**
(il branch `claude/gallant-newton-z2cugw`, 2026-10-01). Il codice di M30 è tutto
scritto e provato sul PC e in QEMU; la GPU però non è ancora mai stata eseguita sul Pi.
Per questo il backend GPU è spento di default, e la colonna "dopo, sul Pi" resta da
riempire con le misure dell'utente (sezione 4).

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
- **Da misurare sul Pi:** il tempo della GPU, gli fps di Texture Room HD (il criterio
  di chiusura di M30) e le righe GPU dello stress test.

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

**Dopo (da misurare):**

| Misura | Dove si legge | Atteso |
|---|---|---|
| Texture Room (ARM), ms | HUD in alto a sinistra | sotto gli 11,6 ms di prima (stima: −20/30% sulla parte 3D) |
| Texture Room HD con la GPU, fps | HUD (`GPU` accanto ai triangoli) | 60 fps: il criterio di chiusura di M30 |
| Texture Room HD sull'ARM | HUD | lento (4× i pixel): serve per il confronto |
| GPU che risponde, ordine dei byte | `g`, passi 1–3 | `ok` |
| Velocità della V3D | `g`, passi 6–7 (triangoli/s, Mpixel/s) | — |
| Stessa scena ARM e GPU | `g`, passo 10 (tempi e % di pixel diversi) | pochi % di pixel diversi, GPU più veloce |
| Z conservato tra 2D e 3D | `g`, passo 11 | come il passo 10 |
| Righe GPU dello stress test | *Dev > Stress* | molti più quad a 60 fps che con l'ARM |

## 5. Differenze rimaste tra ARM e GPU

- Gouraud senza dithering sulla GPU: le sfumature sono più lisce.
- Z a 24 bit sulla GPU contro 16 sull'ARM: meno errori di profondità sugli oggetti
  vicini tra loro.
- Sulla GPU lo z-buffer riparte da zero a ogni fotogramma, anche senza `zclear()`.
- `stat(5)` (pixel 3D) vale 0 con la GPU.
- Il 3D dopo il 2D nello stesso fotogramma: lo z si conserva dal secondo fotogramma
  in poi. Nessuno dei giochi lo fa: disegnano tutti prima il 3D e poi l'HUD.

## 6. Cosa resta

- La prova sul Pi (sezione 4). Se va bene, M30 si chiude e il 3D della GPU può
  diventare il default.
- Rinviati: MSAA 4× (caricare la pagina in un tile multicampione non è un percorso di
  Mesa né di Linux, quindi va provato sul Pi), filtro bilineare (cambia l'aspetto delle
  texture), trasformazioni sulle QPU (il costo per triangolo dell'ARM, se sul Pi risulta
  il limite), sprite 2D sulla GPU.
