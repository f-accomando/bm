# Risoluzioni dello schermo — menu, console e giochi

Quali risoluzioni può usare bm, come arrivano al televisore e quanto costano.
Scritto per scegliere la risoluzione del menu (M27, BareMetal UI).

**Fonti**
- *(M)* misurato sul Pi reale; *(D)* documentazione del firmware o del codice.
- Banda della memoria misurata: `docs/PRESTAZIONI.md`.

---

## 1. Come arriva l'immagine sullo schermo

- bm chiede al firmware (mailbox) un framebuffer della risoluzione che vuole, con 2
  o 3 pagine per disegnare senza sfarfallio.
- La GPU ingrandisce quel framebuffer fino al modo dell'uscita HDMI, senza costi per
  l'ARM.
- Il modo HDMI è in `boot/config.txt` *(D)*:
  - `hdmi_group=1`, `hdmi_mode=16`: **1080p a 60 Hz** (1920×1080);
  - `scaling_kernel=8`: ingrandimento **nitido** (pixel pieni, senza sfumature);
  - `disable_overscan=1`: nessun bordo nero.
- Con un ingrandimento **intero** (2×, 3×...) ogni pixel diventa un quadrato uguale
  agli altri. Con uno non intero (1,5×) alcuni pixel escono più larghi di altri.

## 2. Risoluzioni usate oggi

| Cosa | Risoluzione | Colori | Pagine | Su 1080p |
|------|-------------|--------|--------|----------|
| Console di testo e monitor | 640×360 (80×22 caratteri 8×16) | 32 bit | 2 | 3× |
| Menu (BareMetal UI) | 640×360 | 16 bit (RGB565) | 3 | 3× |
| Giochi `.bm` | 640×360 oppure 320×180 | 16 bit (RGB565) | 3 | 3× oppure 6× |
| Giochi `.bm` quadrati | 256×256 al centro di 480×270 | 16 bit (RGB565) | 3 | 4× (1024×1024, bordi neri) |

- Le risoluzioni dei giochi `.bm` sono fissate dal formato (`src/bm/format.c`):
  "resolution must be 640x360, 320x180 or 256x256".
- 256×256 (`--res 256x256`): il firmware dà uno schermo di 480×270 (1920×1080 / 4,
  pixel interi su 1080p) e il gioco disegna nel quadrato al centro (`bm_video_enter`
  in `runtime.c`); i bordi restano neri. `SCREEN_W` e `SCREEN_H` valgono 256.
- Le copertine dei giochi nel `.bm` sono 128×80 (`BM_COVER_W`, `BM_COVER_H`).

## 3. Risoluzioni possibili per il menu

Il menu disegna direttamente nella memoria video, che **non ha cache**. A ogni
fotogramma copia tutto lo sfondo (la copertina sfocata) dalla RAM allo schermo: è la
voce che cresce con la risoluzione.

Stima della copia dello sfondo con `memcpy` a ~100 MB/s *(M)*, a 16 bit per pixel (2
byte). A 60 fps un fotogramma dura **16,7 ms**; i numeri sotto sono soltanto la copia, a
cui si aggiungono copertine, testo e barra.

### Con l'uscita a 1080p (quella attuale)

| Risoluzione | Scala | Byte per fotogramma | Copia dello sfondo | Note |
|-------------|-------|---------------------|--------------------|------|
| 320×180 | 6× | 0,12 MB | ~1,2 ms | le copertine 128×80 sono enormi: 2 per riga, una riga sola |
| 384×216 | 5× | 0,17 MB | ~1,7 ms | 2 copertine per riga |
| 480×270 | 4× | 0,26 MB | ~2,6 ms | 3 copertine per riga |
| **640×360** | **3×** | **0,46 MB** | **~4,6 ms** | **attuale**: 4 per riga, come giochi e console |
| 960×540 | 2× | 1,04 MB | ~10,4 ms | possibile ma stretto (vedi sotto) |
| 1280×720 | 1,5× | 1,84 MB | ~18,4 ms | ingrandimento non intero: pixel irregolari; oltre i 16,7 ms |
| 1920×1080 | 1× | 4,15 MB | ~41 ms | non sta nei 60 fps; il carattere 8×16 diventa minuscolo |

### Con l'uscita a 720p (`hdmi_mode=4`)

| Risoluzione | Scala | Note |
|-------------|-------|------|
| 320×180 | 4× | |
| 640×360 | 2× | il menu e i giochi di oggi, senza cambiare nulla |
| 1280×720 | 1× | ~18,4 ms di copia: come sopra, oltre il fotogramma |

A 720p la TV o il monitor fanno a loro volta l'ingrandimento fino al pannello (spesso
1080p): l'immagine può risultare meno nitida che con l'uscita a 1080p.

### 32 bit per pixel

Con 32 bit (XRGB8888) i byte raddoppiano: 640×360 costa ~9,2 ms di copia, 960×540
~20,7 ms. Il menu resta a 16 bit.

## 4. Cosa servirebbe per 960×540

- **Copia dello sfondo** col DMA (driver di M14) invece che con la CPU, oppure solo
  quando lo sfondo cambia (cambio di copertina, dissolvenza) invece che a ogni
  fotogramma.
- **Copertine** più grandi: ingrandite 1,5× (192×120, un po' sfocate) oppure una nuova
  copertina 192×120 nel formato `.bm`.
- **Impaginazione** rifatta: griglia, barra, pannelli e testo su 120×33 caratteri.
- **Cambio di modo** a ogni passaggio tra menu e gioco (i giochi restano a 640×360 o
  320×180). Oggi avviene già (il menu è a 16 bit, la console a 32), ma con la stessa
  dimensione.
- Memoria della GPU: 960×540 × 2 byte × 3 pagine = 3,1 MB, ben dentro i 64 MiB di
  default *(D)*.

## 5. Scelta

**640×360** resta la risoluzione del menu:
- 60 fps con margine (~4,6 ms di copia su 16,7);
- stessa dimensione dei giochi `.bm` e della console: nessuna differenza di proporzioni
  passando dall'uno all'altro;
- ingrandimento intero (3×) sull'uscita a 1080p, pixel nitidi.

960×540 è l'unica alternativa realistica per avere più definizione nel solo menu, con
le condizioni della sezione 4.
