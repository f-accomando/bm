# Prestazioni e scelte tecniche — Raspberry Pi Zero W v1.1

Rapporto a colpo d'occhio: limiti dell'hardware, scelte fatte (e scartate) per starci
dentro, numeri attesi contro numeri misurati.

**Fonti**
- *(M)* misurato sul Pi reale; *(Q)* misurato in QEMU; *(H)* benchmark sull'host x86,
  indicativo solo in proporzione; *(D)* datasheet o documentazione.
- Dettagli: stress test in `docs/STRESS.md`; risorse in `docs/HARDWARE.md`; risoluzioni
  in `docs/RISOLUZIONI.md`; storia in
  `docs/ROADMAP.md`.

---

## 1. Limiti dell'hardware

| Risorsa | Valore | Conseguenza per bm |
|---|---|---|
| CPU | ARM1176JZF-S, 1 core, 700 MHz all'avvio → **1000 MHz** chiesti al firmware *(M)* | Tutto (gioco, grafica, audio, input) gira su un solo core in un solo ciclo |
| Cache | L1 16 KiB istruzioni + 16 KiB dati *(D)* | Uno sprite sheet grande o lo z-buffer non ci stanno: conta l'ordine degli accessi |
| Banda RAM | memcpy ~100 MB/s, riempimento ~430 MB/s *(M)* | **È il vero collo di bottiglia**: ogni byte per pixel in più si paga |
| Lettura SDRAM dall'ARM | circa 4× più lenta della scrittura *(M)* | Leggere lo schermo (luci, copie) costa più che scriverlo |
| Divisione intera | non in hardware (routine di libgcc) *(D)* | Vietata nei cicli per pixel: shift o reciproci in virgola mobile |
| VFP (virgola mobile) | VFPv2; FDIVS ~19 cicli *(D)*; numeri denormali → eccezione se non in RunFast *(M)* | Divisioni per triangolo, non per riga né per pixel; RunFast obbligatorio |
| GPU 3D | VideoCore IV (V3D: 12 QPU, TMU, z-buffer nei tile) *(D)*; 811 Mpixel/s, 3 M triangoli/s *(M)* | Niente OpenGL: un driver nostro piccolo a funzioni fisse sotto `draw3d` (M30), il default; il 3D in software resta la riserva |
| Scaler (HVS) | ingrandisce il framebuffer fino a 1080p gratis *(M)* | Si disegna a 640×360 o 320×180 e la GPU scala |
| Memoria | 448 MiB per l'ARM *(M)* | Non è un vincolo (sheet da 4096 px, cache, tabelle) |
| UART Bluetooth | 921 600 baud *(M)* | 4 DS4 a 125 report/s ci stanno; oltre no |
| Frame a 60 fps | **16,7 ms** | Budget unico per `_update` + `_draw` + copia sullo schermo |

## 2. Costi di base misurati

| Operazione | Costo | Fonte |
|---|---:|---|
| Riempimento 640×360 (32 bit) | 2,2 ms | *(M)* |
| Copia di 450 KiB RAM → RAM (CPU / DMA) | 3,70 / 2,99 ms | *(M)* |
| Riempimento di 450 KiB in RAM (CPU / DMA) | 1,02 / 0,85 ms | *(M)* |
| Fascia di 48 righe, copia RAM → schermo (CPU / DMA) | 0,87 / 0,26 ms | *(M)* |
| Benchmark `p`, mappa + 256 sprite: disegno diretto / via RAM | 8,58 / 11,85 ms | *(M)* |
| Frame intero via RAM copiato col DMA | 14,06 ms (più lento della CPU) | *(M)* |
| Operazione Lua semplice | ~100 ns → ~140 000 per frame | *(M)* |
| Sprite 16×16 dal C | 3,5 µs (~14 ns/pixel) | *(M)* |
| Sprite 16×16 da Lua (`spr()` + logica) | 8,4 µs | *(M)* |
| Triangolo 2D ~170 pixel | 6,0 µs | *(M)* |
| Triangolo 3D disegnato (piatto, piccolo) | ~14 µs (sett.), ~5 µs a triangolo della sfera (2026-09-29) | *(M)* |

## 3. Scelte 2D

| Scelta | Alternativa scartata | Perché | Numeri |
|---|---|---|---|
| **RGB565** (16 bit) per le `.bm` | XRGB8888 (32 bit) | Metà banda per ogni pixel scritto o letto | 32 bit = copia di un frame da ~6,5 a ~13 ms con la CPU (stima dalle misure) |
| **Modo 32 bit rimandato** (2026-09-29) | farlo in M14 | Tutta la libreria da duplicare, beneficio solo nelle sfumature | Sfumature coperte dal dithering 4×4 |
| Disegno **diretto** nella memoria video | sempre in RAM e poi copia | La copia rilegge la SDRAM (lenta) | 8,58 ms diretto contro 11,85 ms via RAM *(M)* |
| Via RAM **solo** quando serve (luci) | mai | Le luci leggono i pixel: dallo schermo sarebbe peggio | Scatta al primo `light_begin` |
| **Triplo buffer** | doppio buffer | Sfarfallio nella metà bassa del menu sul Pi | Una pagina in più (450 KiB), nessun costo per frame |
| Copia dei frame con la **CPU** | DMA | Sul frame intero il DMA è risultato più lento | 14,06 ms DMA contro 11,85 ms CPU *(M)* |
| DMA con impostazioni "prudenti" (come Circle) | burst 8, priorità alta, `WAIT_RESP` | Le impostazioni aggressive bloccavano il Pi | Test `D` passo per passo, tutti ok *(M)* |
| Luci su **griglia 4×4 px**, bilineari, con dithering | luce per pixel | 1/16 dei calcoli | Hunter's Night a 320×180 |
| Giochi a luce intensa a **320×180** | 640×360 | 1/4 dei pixel da scrivere e illuminare | Hunter's Night |
| Sprite con blit C, riga intera con `memcpy` se la cella è opaca | pixel per pixel ovunque | Le celle opache non controllano la trasparenza | ~14 ns/pixel *(M)* |
| **SHEET8**: sheet a ≤256 colori + RLE nel file | RGBA grezzo | File molto più piccolo, decodificato una volta al caricamento | Titan Clash: 2048×3376, 166 colori |
| Sheet fino a **4096 px** di lato | 256×256 | Robot grandi come in SF2 | La memoria non è un problema |
| Testo con il font 8×16 della console, su griglia 16 px | font proporzionali | Semplice, e i test QEMU leggono lo schermo | — |

## 4. Scelte 3D

| Scelta | Alternativa scartata | Perché | Numeri |
|---|---|---|---|
| **Backend V3D nostro** sotto `draw3d` (M30, predefinito) | OpenGL ES sulla GPU | OpenGL vuole Linux o il firmware (VCHIQ); la pipeline fissa delle cartucce basta | 182 sfere (7142 triangoli) a 60 fps *(M, 1 ott.)* |
| Rasterizzatore **software** sull'ARM (riserva, QEMU) | — | Gira ovunque, stessi pixel nei test | 69 sfere (2679 triangoli) a 60 fps *(M, 1 ott.)*; ~1200 triangoli a settembre |
| **Z-buffer a 16 bit** di 1/z | z a 32 bit o float | Metà banda; 1/z è lineare sullo schermo | 460 KiB da pulire per frame |
| Flag **senza z-buffer** per pavimenti e sfondi | tutto con z | Niente lettura né scrittura dello z | Usato da Chaos Kitchen |
| Eliminazione delle facce posteriori prima di tutto | disegnarle | ~60% dei triangoli di una sfera scartati | Stress: ~40% disegnati |
| Clipping **solo** sul piano vicino | clipping su tutti i piani | Gli altri bordi li taglia già la scanline | — |
| Luce **per faccia** (piatta) di default | Gouraud ovunque | Costa meno per pixel | Gouraud: vedi §5 |
| **Gouraud** su richiesta (`draw3d` flag 4), colori limitati sui vertici | saturazione per pixel | Nessun controllo per pixel nel ciclo | Pixel identici |
| **Dithering 4×4** (Bayer) | colori quantizzati a RGB565 | Niente bande, costo di una tabella | 3 somme per pixel |
| Texture **prospettiche**, divisione ogni **16 px** (come Quake) | divisione a ogni pixel / texture affini | 1 divisione ogni 16 pixel; le affini deformano | Errore sotto il texel |
| Gradienti di z, colore e uv **una volta per triangolo** | interpolazione per riga | Toglie 2–3 divisioni per riga | Vedi §6 |
| Arrotondamento **in linea** | `ceilf()` di libreria | 4 chiamate per riga risparmiate | — |
| Camera **in cache** tra i `draw3d`; identità per mesh non ruotate | seni e coseni a ogni chiamata | 12 funzioni trigonometriche in meno per chiamata | Chaos Kitchen: 490 chiamate per frame |
| Matrici e trasformazioni in **float** (VFP) | virgola fissa | VFP disponibile | — |
| Cicli interni in **virgola fissa** (16.16, 24.8) | float per pixel | Somme intere più veloci, niente conversioni | — |
| **Nebbia** per faccia | per pixel | Stesso costo del piatto | Astro Wing |
| **Lampade** puntiformi per faccia o vertice (max 4) | luce per pixel | Costo per faccia | — |
| Bordi dei triangoli in **virgola fissa 32.32** (M30) | `ceilf` e confronti in virgola mobile a ogni riga | Ogni confronto VFP ferma la pipeline (`vmrs`): 5 per riga | ~80 → ~60 istruzioni per riga; stessi pixel |
| Cicli delle texture **specializzati** (M30): con/senza z, trasparenza, luce | un ciclo unico con i controlli dentro | Il clamp delle coordinate si controlla una volta per segmento di 16 pixel, la trasparenza per cella 8×8 dello sheet | 70 → 46 istruzioni per pixel con luce, 55 → 30 senza; stessi pixel |
| Luce sulle texture con **2 moltiplicazioni** (rosso e blu insieme) | 3 | Stesso risultato | — |
| Mesh **fuori dalla vista scartate** prima di trasformarle (M30) | trasformare tutti i vertici | Sfera d'ingombro contro i quattro bordi dello schermo e il piano vicino | Chaos Kitchen: centinaia di `draw3d` per frame |
| **z-buffer pulito dal DMA** a fine frame (M30) | `memset` in `zclear()` | Il DMA lavora mentre gira `_update`; `zclear()` aspetta solo la fine | ~1 ms a 640×360; `dma_zclear=0` lo spegne |

## 5. Costo per pixel: piatto, Gouraud, texture

| Modo | Host, prima *(H)* | Host, dopo la riscrittura *(H)* | Pi: sfera a tutto schermo, ~180 000 px *(M, 8298b15)* |
|---|---:|---:|---|
| Piatto | 0,27 ms (1×) | 0,22 ms | entra in 16,7 ms (60 fps con 15 sfere) |
| Gouraud | 0,84 ms (**3,1×**) | 0,59 ms (2,7×) | **> 16,7 ms**: oltre ~90 ns/pixel |
| Texture | 1,12 ms (**4,1×**) | 1,08 ms (4,9×) | **> 33 ms**: oltre ~180 ns/pixel |

**Regola pratica**
- Gouraud e texture vanno bene su oggetti **piccoli o medi**, non a tutto schermo.
- Pavimenti e sfondi grandi: piatti e senza z-buffer.

**Istruzioni ARM per pixel (M30, `make count-insns`)**, contate con `qemu-arm` sulle scene
di `tests/bm/bench3d.c` (cache e bus esclusi: è il lavoro della CPU, non il tempo):

| Scena | Prima | Dopo |
|---|---:|---:|
| Quad piatti con z | 12,5 | 10,4 |
| Quad piatti senza z | 3,4 | 1,2 |
| Quad Gouraud | 35,8 | 32,7 |
| Quad con texture e luce | 70,0 | 45,8 |
| Quad con texture, senza luce | 55,0 | 30,2 |
| Sfere piatte (triangoli piccoli) | 24,5 | 21,6 |
| Sfere con texture | 94,8 | 79,9 |
| Texture Room (8 casse) | 84,7 | 57,7 |

I pixel restano identici (stessi checksum di `make bench3d`). Con il 3D sulla GPU l'ARM
non disegna più pixel: Texture Room 0,62 milioni di istruzioni a fotogramma invece di 3,86
(`make count-insns` con le scene `+gpu`); il confronto completo prima/dopo è in
`docs/M30-PRIMA-DOPO.md`.

## 6. Atteso (simulazioni) e trovato (Pi)

| Misura | Atteso | Trovato sul Pi | Nota |
|---|---|---|---|
| QEMU come previsione | "QEMU ~2× più lento del Pi" (sprite, 3D piatto) | Vero per sprite (2133 → 4482) e 3D piatto (602 → 1195 triangoli) | QEMU non simula cache né bus |
| Gouraud / texture in QEMU | 3 sfere a 60 fps | **< 1** sfera | QEMU sottostima il costo per pixel sul Pi: non prevede i lavori pesanti sui pixel |
| Chaos Kitchen 1-1 | 15–17 ms in QEMU → "~8 ms sul Pi" | **14,1 ms, 54 fps** | Molti triangoli piccoli: pesava il costo per triangolo (divisioni e `ceilf` per riga), ora tolto |
| Titan Clash | — | **11,6 ms, 61 fps** | Solo sprite e fondali: il 2D grande regge |
| Astro Wing | 60 fps | **59,9 fps**, 6,34 ms (max 12,57) | Dopo la correzione RunFast |
| Frame via RAM + DMA | DMA più veloce (3,4× sulla fascia) | Frame intero **più lento** (14,06 contro 11,85 ms) | Il guadagno sulla fascia non vale sul frame intero |
| Stress C, sett. → 29 set. | uguale | sprite **−22%** (circa 3,5 ms fissi in più per frame), 3D **−50%**; parte Lua uguale | Da capire: lo stress ora stampa clock, temperatura, throttling e un ciclo di sola CPU |
| Input Bluetooth, 600 report in coda | nessun ritardo | < 0,5 s (test), niente ritardo percepito | Si legge solo lo stato più recente, lavoro limitato a 4 ms |
| Rasterizzatore di M30 sulle sfere (`qemu-arm`) | −9% di istruzioni | **2,3×** più sfere a 60 fps (31 → 70) | Il conteggio non vede le fermate della pipeline per i confronti in virgola mobile (`vmrs`) che la virgola fissa ha tolto |
| Stress C, 29 set. → 1 ott. | disturbo all'avvio | sprite di nuovo a 4456 (settembre 4482) | Confermato: con 20 s di attesa il calo sparisce |
| Prima prova della V3D (1 ott.) | il passo 3 passa | "no end of frame" | Errore del driver: il bit "binner senza memoria" di `PCS`, acceso fin dall'avvio, era preso per un errore; corretto e coperto da `make test-v3d` |
| V3D dopo la correzione (1 ott., `d0c7fe8`) | "centinaia di Mpixel/s, milioni di triangoli/s" | **811 Mpixel/s**, 3,0 M triangoli/s; 182 sfere a 60 fps contro 69 dell'ARM | Il limite ora è l'ARM che prepara i vertici (~2 µs per triangolo); le texture 256×256 in ordine di riga costano 9 ns/px contro 1 |

## 7. Evitato o rimandato

| Cosa | Perché | Stato |
|---|---|---|
| OpenGL ES sulla GPU | Vuole il firmware con VCHIQ o il driver DRM di Linux; troppo grande | Sostituito da un backend V3D a funzioni fisse (M30, in prova) |
| Modo 32 bit | 2× banda; beneficio marginale con il dithering | Rimandato |
| DMA per copiare i frame | Più lento della CPU sul frame intero | Solo test `D` e riempimenti |
| Luce per pixel | Troppo costosa | Griglia 4×4 |
| Texture affini | Deformazioni visibili | Prospettiche ogni 16 px |
| Divisione per pixel nelle texture | ~19 cicli ciascuna | Ogni 16 px |
| Hub USB / più dispositivi USB | Complessità del driver DWC2 | Solo Bluetooth per il multiplayer |
| Git e SSH completi (M19) | Pesanti per un kernel senza OS | Versioni "leggere" |
| JIT per Lua | Nessun OS, porting complesso | Lua 5.4 interprete |

## 8. Problemi trovati sul Pi (non visibili in QEMU)

| Sintomo | Causa | Correzione |
|---|---|---|
| Astro Wing si blocca dopo ~2 minuti, con ronzio audio | Numeri denormali → eccezione del VFP (RunFast spento) | RunFast all'avvio (FPSCR `FZ|DN`) |
| Blocco con LED fisso nel test `p` | DMA con burst e priorità aggressivi | Impostazioni di Circle, test `D` separato |
| Sfarfallio nella metà bassa del menu | Si disegnava su una pagina ancora in scansione | Triplo buffer |
| Blocco senza schermata d'errore | L'eccezione finiva sulla pagina nascosta | Schermata d'errore visibile, watchdog di 3 s, note in RAM non azzerata |
| Il secondo DS4 non si connette | Il pad chiedeva SDP prima dell'HID; il rifiuto lo faceva chiudere | Server SDP minimo; la console apre i canali HID dopo 1 s |

## 9. Da misurare alla prossima prova

- M30: stress `s` con le righe `quad 320x180` (ns per pixel) e le due righe della
  macchina (clock del core, interrupt); Texture Room con 8 e 32 casse dopo il
  rasterizzatore nuovo.

- Stress `s` con la riscrittura del rasterizzatore: righe 3D piatto, Gouraud e texture.
- Righe "before" e "after C part": clock, throttling e ciclo di sola CPU.
- Chaos Kitchen 1-1 e mondo 6 con Select: obiettivo stabile a 60 fps.
- Due DS4 insieme: abbinamento, luce, Pong a 2.
