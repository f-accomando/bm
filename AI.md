# bm — AI sul Pi Zero W: cosa si può fare e cosa no

Considerazioni del 2026-09-30. Sostituiscono le vecchie milestone AI-01…AI-12, che **non**
sono una roadmap: qui c'è solo cosa ha senso per bm, in ordine di utilità.
Numeri di riferimento: [docs/HARDWARE.md](docs/HARDWARE.md), [docs/PRESTAZIONI.md](docs/PRESTAZIONI.md).

## Più sensate e utili per bm

**Nei giochi: è qui che l'AI serve davvero.** Avversario CPU di Titan Clash, riconoscimento di
combo e gesti dal gamepad, riconoscimento dei disegni nell'editor, piccoli generatori di sprite o
livelli. Costano pochissimo per frame e sono il caso d'uso più concreto.

**Motore di inferenza in C, con funzioni Lua: sì.** Poche centinaia di righe (dense, conv2d,
depthwise, ReLU, pooling, softmax) più `ai.load` / `ai.run` per le cartucce. In Lua puro no:
~100 ns per operazione, ~140 000 operazioni per frame.

**Tiny ML (MLP, INT8, fixed point): sì, è il punto forte.** Niente NEON, ma l'ARMv6 ha le SIMD
(SMLAD: 2 moltiplicazioni-accumulo a 16 bit per istruzione): un MLP da ~10 000 parametri gira in
decine di µs. Il limite vero è la banda RAM (~100–200 MB/s): pesi INT8 e reti piccole.

**Addestrare sul PC, eseguire sul Pi: sì.** Si addestra e quantizza sul PC (PyTorch), uno script
in `tools/` esporta, il Pi fa solo inferenza, senza PC né cloud mentre gira.

**Formato del modello: sì, semplice.** Intestazione + strati + pesi INT8 + scale, CRC come le
`.bm`; meglio come sezione dentro la cartuccia `.bm` (il gioco porta il suo modello). Niente
versioning o metadati elaborati finché c'è un solo consumatore.

**Nessun task AI separato: non serve.** bm non ha scheduler (un core, loop a 60 fps, interrupt).
Si spezza l'inferenza in passi con un budget per frame (es. 2 ms), nel tempo che il gioco lascia
libero; si misura e si mostra sullo schermo.

**Strumenti: pochi, sul PC.** Script (addestra → quantizza → esporta) e un test che confronta
bit per bit le uscite del motore con il modello di riferimento dentro `make test`. Emulatore
inutile: il C gira già sul PC e in QEMU; il profiler è una voce del monitor.

**Approccio ibrido: sì, da preferire.** Algoritmo classico che fa il grosso + rete minuscola che
sceglie i parametri (quanto affilare, quale palette) invece di calcolare ogni pixel: l'unico modo
di usarla nella grafica senza uscire dal budget del frame.

## Possibili, ma dopo

**Apprendimento durante il gioco: in piccolo.** Tabelle Q o un MLP minuscolo che si adatta al
giocatore nella partita; l'addestramento vero resta sul PC.

**Downscaling "1080p → ~244p": solo come import degli asset.** Sul Pi non c'è una sorgente a
1080p e bm usa 640×360 e 320×180. Utile per foto/render → sprite (il "3D→sprite" di M22), sul
PC o sul Pi in qualche secondo; prima va verificato contro media + nitidezza + dithering classici.

**Super-resolution: solo su immagini ferme.** In tempo reale no, e non serve: la GPU scala già
gratis 640×360 → 1080p. Su copertine e sprite sì, anche come SR-LUT (la rete addestrata diventa
una tabella), ma in secondi, non per frame.

**Tiny CNN: la rete sì, la sorgente manca.** Una MobileNet ridotta a 96×96 in grigi gira in
qualche decina di ms (pochi fps). Immagini solo da SD o dalla rete (vedi fotocamera sotto).

**GPU VideoCore IV (QPU): progetto a sé.** Unica accelerazione disponibile (~24 GFLOPS teorici),
avviabile senza Linux con la mailbox del firmware, come GPU_FFT; richiede assembly QPU. È ciò che
renderebbe fattibile la grafica neurale, non un primo passo.

**Modello linguistico minuscolo: solo demo.** ~15M parametri (stile llama2.c "stories") farebbero,
a stima, qualche token al secondo: favolette in inglese, non un assistente.

**Libreria e condivisione dei modelli: quando servirà.** Solo quando due o tre giochi usano davvero
un modello; allora passa dallo store su GitHub (M25), non da un canale a parte.

## Da scartare o fuori portata

**Grafica neurale in tempo reale (denoising, bordi, texture): no.** Una passata a schermo intero
legge e riscrive 0,46 MB, ~5 ms prima di calcolare; il rasterizzatore di bm non produce rumore da
togliere. Solo offline, sugli asset.

**Compressione neurale delle immagini: no.** RAM (448 MiB) e SD non sono un limite, e decodificare
con una rete costa molto più di RLE o PNG.

**Sensori, segnali, anomalie: oggi no.** bm non ha I2C/SPI, microfono né ingressi analogici; i dati
disponibili sono controller, tastiera, rete e temperatura della CPU.

**Fotocamera: no.** La CSI senza Linux dipende dallo stack chiuso della GPU; una webcam USB
occuperebbe l'unica porta del Zero (un dispositivo alla volta, niente hub).

**Voce: no.** Niente microfono (servirebbe un microfono I2S sui GPIO e il suo driver); sintesi
vocale solo a formanti col synth esistente, non neurale.

**Altri Pi e acceleratori (Zero 2 W, Pi 3/4/5, NPU): fuori portata.** bm gira solo sul BCM2835
(Zero W, Pi 1); un ARMv8 multicore è un kernel nuovo, l'AI del Pi 5 è una scheda PCIe esterna.
Basta tenere il formato portabile (INT8 + scale).

## Primo passo sensato

Motore INT8 in C + funzioni Lua + script di esportazione, provati su un caso vero (l'avversario
CPU di Titan Clash) con il tempo misurato mostrato sullo schermo. Visione, scaling e grafica
neurale vengono dopo, e solo se si affronta la GPU.
