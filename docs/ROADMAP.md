# Roadmap bm

**Nome (2026-09-30):** il progetto si chiama **bm**, BareMetal (prima bm33). Cartucce
`.bm` con intestazione `BMCART`, cartella `bm/` sulla SD, strumenti `tools/bm_net.py`,
`tools/bm_load.py`, `scripts/mkbm.py`, runtime in `src/bm/`. Compatibilità: il kernel
legge ancora la cartella e le cartucce di prima (`make install` sposta la cartella) e
`bm_net.py` parla anche coi kernel precedenti.

Obiettivo MVP: una *fantasy console* bare metal su Raspberry Pi Zero W che avvia
da SD, mostra un menu, carica giochi scritti in Lua ("cart") e li esegue a 60 fps
con grafica, input e (opzionale) audio.

Ogni milestone ha un **criterio di completamento** verificabile e, quando
possibile, un test automatico in QEMU (`-M raspi0`).
Dimensione: **S** = pochi giorni, **M** = 1–2 settimane, **L** = più di 2 settimane.

```
M0 ─ M1 ─ M2 ─ M3 ─ M4 ─ M5 ─ M6 (rimossa) ─┬─ M7 ─┬─ M9 (MVP) ─ M10…M14 (vedi "Dopo l'MVP")
                                       └─ M8 ─┘
```

---

## M0 — Boot e test pattern HDMI ✅ verificato su Pi Zero W (S)
- `start.S`, linker a 0x8000, mailbox, framebuffer 32 bpp, LED ACT, barre colore.
- **Fatto quando:** pattern visibile su HDMI; `make qemu-screenshot` produce l'immagine.

## M1 — Infrastruttura di debug ✅ (S)
- UART PL011 su GPIO14/15 (115200 8N1); `kprintf` minimale (`%d %u %x %s %c %p`).
- Tabella dei vettori delle eccezioni: undefined / prefetch abort / data abort
  → dump dei registri su UART + codice di errore sul LED.
- **Chainloader via seriale** (stile *raspbootin*): un kernel fisso sulla SD
  riceve il nuovo `kernel.img` dalla UART, così non si sposta più la SD a ogni build.
- CI GitHub Actions: build + screenshot QEMU confrontato con un'immagine di riferimento.
- **Fatto quando:** `make run-serial` avvia un nuovo kernel sul Pi in meno di 5 s;
  un accesso a indirizzo non valido stampa PC/LR/CPSR.

## M2 — Console testuale su schermo ✅ verificato su Pi Zero W (S)
- Font bitmap 8×16 (CP437, derivato da Terminus) in `rodata`, rendering carattere, cursore, a capo, scroll,
  colori ANSI, barra di stato; framebuffer 640×360 scalato dalla GPU.
- `kprintf` scrive sia su UART sia su schermo; il panic appare anche su HDMI.
- **Fatto quando:** i log di boot sono leggibili sul monitor senza cavo seriale.

## M3 — Memoria, cache e libc ✅ verificato su Pi Zero W (M)
- MMU con mappa identità a sezioni da 1 MB: RAM cacheable, periferiche
  device/strongly-ordered, framebuffer write-through (o cache + flush esplicito).
- Attivazione di I-cache, D-cache e branch prediction (senza cache l'ARM1176 è
  10–20× più lento).
- Integrazione di **newlib**: `_sbrk` (heap), `_write` → console, stub per gli altri syscall.
- Benchmark: fill a schermo intero e `memcpy`, prima e dopo le cache.
- **Fatto quando:** `malloc`/`printf` di newlib funzionano; il clear 320×240 richiede meno di 1 ms.

## M4 — Interrupt e temporizzazione ✅ verificato su Pi Zero W (M)
- Controller IRQ BCM2835, IRQ del system timer, contatore di tick a 1 kHz.
- Doppio buffer: framebuffer virtuale alto 2×, scambio tramite il tag *set virtual offset*.
- Frame loop a 60 Hz stabile (sync al vblank se il firmware lo espone, altrimenti timer).
- **Fatto quando:** un rettangolo in movimento scorre fluido, senza tearing visibile, a 60 fps.
- Risultato: 60 fps, 0 frame persi, 2,7 ms di disegno per frame; ritmo dal timer.
  Vsync: il firmware del Pi Zero risponde "tag not supported" a 0x4000E (wait for
  vsync). Alternativa da provare in M6: interrupt SMI (IRQ 48) che il firmware
  genera a ogni vsync, come faceva il driver Linux bcm2708_fb.

## M5 — Lua embedded ✅ verificato su Pi Zero W (M)
- Lua 5.4 in `third_party/lua`, compilato con newlib e VFP hard-float.
- Allocatore dedicato per `lua_State`, `print` → console, errori con traceback su schermo.
- REPL su UART; script di avvio incluso nell'immagine (`.incbin`).
- **Fatto quando:** dalla seriale `> print(2^10)` risponde `1024.0`; un errore Lua
  non blocca il kernel.
- Risultato su Pi Zero W (1 GHz, cache on): fib(25) 83 ms, 1M addizioni 104 ms,
  sort di 100k interi 657 ms, 20k stringhe 104 ms, boot.lua 987 ms.
  Circa 100 ns per operazione semplice della VM: ~150k operazioni Lua per frame
  a 60 fps, un budget simile a quello di PICO-8. Interi a 64 bit mantenuti.

## M6 — Un secondo formato di cartucce ✅ — rimosso il 2026-09-30
**Decisione 2026-09-30 (utente): tolto.** M6 aveva aggiunto un interprete in C per le
cartucce di un'altra console (formato `.cart`). Il kernel non lo contiene più: il menu
elenca solo i `.bm` e un `.cart` inviato dalla seriale o dalla rete viene rifiutato
(`test_upload_refused_and_corrupt`). Il kernel è passato da 1,57 a 0,98 MB.

## Tipi di cartuccia (decisione 2026-09-26)
| Tipo | Formato | Gira su | Priorità |
|---|---|---|---|
| **bm nativa Lua** | **`.bm`** | bm, sfrutta tutto il Pi | ✅ (M7) |
| bm nativa ARM (C) | `.bm` | bm, user mode + MMU | M13, chiusa senza implementazione |

### Cartucce native `.bm`: video (decisione 2026-09-26)
- **640×360**, 16:9, scala intera ×2 su 720p e ×3 su 1080p; è la risoluzione della
  console, quindi nessun cambio di modo. 320×180 facoltativa (header).
- **16 bit RGB565** (65 536 colori, colore diretto): metà banda del 32 bit
  (fill stimato ~1,1 ms contro 2,2 ms misurati).
- **Espandibile a 32 bit** in seguito: l'API riceve i colori come RGB888 e la
  grafica delle cartucce è salvata in un formato indipendente dal framebuffer;
  il formato di pixel è un campo dell'header `.bm` e il disegno in C è
  parametrizzato sulla profondità. Il 24 bit "impacchettato" (3 byte per pixel,
  non allineato) si evita: l'espansione utile è il 32 bit.
- **Budget**: disegno completo (mappa piena + 256 sprite) sotto il 25% del frame,
  verificato da un benchmark a schermo; tutto il disegno in C, Lua solo logica.

## M7 — Cartucce native `.bm` ✅ verificato su Pi Zero W (M)
- Formato `.bm` (header 128 byte + sezioni Lua / sheet RGBA / mappa, CRC), packer
  `scripts/mkbm.py` con PNG e CSV.
- Grafica C in RGB565 (`src/bm/gfx16.c`): forme, sprite con flip e trasparenza,
  scorciatoia per celle opache, mappa, testo, camera, clip.
- Runtime: stato Lua isolato per cartuccia, `_init/_update/_draw` a 60 fps, input
  seriale, limite di istruzioni per frame, errori mostrati sulla console, GC
  generazionale; caricamento dalla seriale (`U`).
- Benchmark C e demo nativa all'avvio.
- **Fatto quando:** mappa piena + 256 sprite sotto il 25% del frame sul Pi reale.
- Risultato (kernel `a63bfb0`): demo nativa a schermo pieno, controllo colori
  RGB565 corretto (rosso, verde, blu, bianco); 256 sprite 16×16 ≈ 0,9 ms
  (vedi docs/STRESS.md).

## M7b — Input ✅ tastiera verificata sul Pi Zero W (L)
- Stack USB scritto da zero (non USPi): controller DWC2 in modalità host, DMA a buffer,
  polling dal ciclo principale; enumerazione di un dispositivo sulla porta radice.
- **Tastiera** HID (protocollo boot): layout italiano/US, ripetizione; monitor e REPL
  leggono da seriale o tastiera. **Gamepad HID** generici (analisi del descrittore) e
  **Xbox 360** cablati. Mappatura su `btn()` delle `.bm`.
- Esc o Start+Select escono dal gioco. Niente hub (decisione: un dispositivo alla volta;
  M29 aggiunge l'hub per il Pi 1 B, sempre con un solo dispositivo HID in uso),
  niente Bluetooth (BCM43438 condivide la UART della console; firmware + HCI: troppo costoso).
- Pad su GPIO (fase A) non necessario per ora.
- **Fatto quando:** una tastiera USB scrive nel REPL e un gamepad muove il giocatore
  (QEMU con `usb-kbd` e `usb-tablet`; sul Pi, kernel `25f5dbc`: Apple Magic Keyboard
  05ac:0267 via OTG, composita a 3 interfacce, report con ID: menu e giochi ok;
  gamepad non ancora provato sul Pi).

## M8 — Storage e caricamento delle cart ✅ verificato sul Pi Zero W (M)
- Driver SD sul controller EMMC (Arasan SDHCI) in PIO, bus a 4 bit a 25 MHz, SDSC e SDHC;
  FAT16/FAT32 con nomi lunghi, in sola lettura (scritto da zero, non FatFs).
- Menu delle cartucce: incorporate + `.bm` in `/carts` e nella radice; si apre
  all'avvio se c'è un dispositivo USB di input.
- Da fare: scrittura (salvataggi, config), anteprime.
- **Fatto quando:** copiando una nuova cart sulla SD da PC, questa compare nel menu
  (QEMU con immagini FAT32 da 128 MiB e 4 GiB; sul Pi, kernel `25f5dbc`: SDHC 8 GB,
  FAT32 `BOOTFS`, 3 cartucce in `carts/`, giocate dal menu).

## M9 — MVP ✅ verificato sul Pi Zero W (M)
- Avvio in ~2 s direttamente sul **menu delle cartucce** (titolo e autore letti dalle
  cartucce, ordinate per titolo); la vecchia sequenza di diagnostica è nel monitor (`B`).
  Esc / Start+Select: dal gioco al menu, dal menu al monitor.
- 3 giochi demo in Lua: **Pong** (contro la console), **Snake**, **Star Shooter**
  (ondate, boss, esplosioni, sprite disegnati con `sset`).
- `make image` → `dist/bm.img` (64 MiB, MBR + FAT32) pronto per Raspberry Pi Imager /
  balenaEtcher / `dd`.
- Guida all'API e alla prima cartuccia: `docs/API.md` (in inglese; in italiano `docs/API-IT.md`).
- Prestazioni sul Pi: l'ARM1176 legge la SDRAM circa 4 volte più lentamente di
  quanto ci scrive (`memcpy` 10,3 ms/MiB contro `memset` 2,4 ms/MiB, cache dati 16 KB),
  e la memoria video non ha cache. Per `.bm` il benchmark misura sia il disegno diretto
  sia quello via RAM (`p`, `V`): sul Pi (kernel `89452b3`) mappa piena + 256 sprite
  costano **8,50 ms diretti** contro 12,65 ms via RAM (la copia rilegge la SDRAM), quindi
  il default resta il disegno diretto.
- **Fatto quando:** da una SD appena scritta si accende, si sceglie un gioco e si gioca
  senza PC collegato (in QEMU: `test_make_image`, `test_games`; sul Pi, kernel
  `83f4f82`: menu all'avvio e i tre giochi funzionano).

## Dopo l'MVP (decisione 2026-09-27)

Decisioni: M10 rimandato (uscita scelta: HDMI, dagli altoparlanti del monitor) e poi
fatto dopo M12; priorità **M11 → M12**; controller di riferimento per M12: **DualShock 4 (PS4)**,
provato prima via USB (stesso formato dei report che poi arrivano via Bluetooth).

```
M9 (MVP) ─┬─ M10 audio
          ├─ M11 SD in scrittura ─── M12 Bluetooth (controller)
          ├─ M13 cartucce con codice ARM nativo
          ├─ M14 grafica 2.0 (DMA, 32 bit, 3D con texture)
          ├─ M15 editor sulla console (codice, sprite, mappa)
          └─ M16 multiplayer locale (più controller Bluetooth) ─┬─ M17 gioco cooperativo
                                                                ├─ M20 picchiaduro a robot giganti
                                                                └─ M18 WiFi, console di rete ─── M19 HTTPS: aggiornamenti e "git leggero"
```

## M10 — Audio HDMI (M/L) ✅ verificato sul Pi Zero W
- Uscita **HDMI** (dagli altoparlanti del monitor), `src/audio/audio.c`: blocco audio
  HDMI del BCM2835 (FIFO MAI, rigenerazione del clock N/CTS, InfoFrame audio) come in
  Circle; campioni IEC 958 a 48 kHz (`iec958.c`) mandati da un canale DMA con DREQ
  HDMI su due buffer ad anello da 256 campioni (5,3 ms). L'interrupt di fine buffer
  genera il blocco successivo: nessun lavoro nel ciclo del gioco.
- Sintetizzatore `synth.c`: 8 voci guidate da 16 byte di registri ciascuna (quadra con
  duty, triangolo, dente di sega, rumore LFSR a 15 bit, ADSR lineare, somma senza
  normalizzazione). Test su host: `make test-audio`.
- API `.bm`: `note`, `noteoff`, `freq`, `envelope`, `duty`, `playing`, `apu`
  (docs/API-IT.md); effetti e melodie nei tre giochi demo.
- Dopo: banche di suoni, sequencer e Sound editor (M22.4, più sotto).
- Monitor: `a` stato dell'audio (clock, canale DMA, blocchi suonati, costo della sintesi)
  e una melodia di prova con tutte le forme d'onda. All'avvio, se l'audio funziona, due
  note brevi. Senza audio HDMI (QEMU, modo DVI) tutto funziona in silenzio.
- Verificato sul Pi (kernel `0831d5e`, monitor ASUS via HDMI): la melodia di prova del
  comando `a` (quattro forme d'onda e accordo) e i suoni dei tre giochi escono dagli
  altoparlanti del monitor.
- **Fatto quando:** i giochi demo hanno effetti sonori senza cali di frame rate e
  senza scatti audio per 10 minuti.

## M11 — SD in scrittura, salvataggi e impostazioni ✅ verificato sul Pi Zero W (M)
- Driver SD: scrittura a blocchi (CMD24/25) e FAT32 in scrittura (creare e riscrivere
  un file, allocare cluster, aggiornare le due FAT e la directory), con attenzione a
  non corrompere la scheda (ordine delle scritture, verifica in QEMU con `fsck.vfat`).
- File `/bm/config.txt` (layout tastiera, modo di disegno, volume, dispositivi
  Bluetooth abbinati) e `/bm/save/<cart>.sav`.
- API `.bm`: `save(tabella)` / `saved()` per record e progressi; punteggi migliori nei
  giochi demo.
- File di salvataggio: `/bm/save/<CRC-32 di titolo e autore>.SAV` (nomi 8.3: bm non
  scrive nomi lunghi); contenuto: tabella Lua come testo, riletta in un ambiente vuoto.
- **Fatto quando:** un record di Snake sopravvive allo spegnimento; `fsck.vfat` pulito
  dopo 1000 salvataggi (test sul PC `make test-fat`: 1000 riscritture, 200 file, fsck e
  mtools; QEMU `test_sd_save_and_config`: salvataggio e impostazioni dopo un riavvio).
  Sul Pi (kernel `89452b3`): il record di Snake resta dopo lo spegnimento, il layout
  scelto con `L` resta dopo il riavvio; DualShock 4 via USB funzionante.

## M12 — Controller Bluetooth ✅ verificato sul Pi Zero W (L)

Stato:
1. ✅ (QEMU) console sulla mini UART, UART0 al chip (GPIO30–33, RTS/CTS), clock 32 kHz
   (GPCLK2 su GPIO43), BT_ON su GPIO45 con power-cycle se il chip non risponde, patch
   firmware da `bm/BCM43430A1.hcd`, indirizzo e versione, ricerca dispositivi
   (monitor `T`). Test in QEMU con un **chip simulato** in Python sulla UART0
   (`test_bt_start_and_scan`): è la base per provare anche i passi successivi.
   Sul Pi (kernel `7c36bb3`): chip e firmware ok (121 record), indirizzo
   b8:27:eb:62:7c:08, DS4 trovato (00:1f:e2:bf:d7:dd, classe 002508).
2. ✅ (QEMU) connessione ACL, abbinamento SSP "Just Works" (IO NoInputNoOutput,
   bonding generale), cifratura, canali L2CAP 0x11/0x13, report HID del DS4 nel layer
   di input (menu e giochi); chiave in `bm/config.txt`; all'avvio page scan e
   riconnessione avviata dal DS4 (tasto PS) con la chiave salvata. SDP non serve (report
   noto). Test `test_bt_pair_and_reconnect` con un DS4 simulato. Sul Pi (kernel
   `73d199c`): abbinamento del DS4 riuscito e input funzionante, ma con oltre 1 s di
   ritardo: a 115200 baud i report del DS4 si accumulavano nel chip. Dopo il firmware la
   UART passa a 921600 baud (0xFC18) e `bt_poll` svuota tutto a ogni fotogramma.
   Con `97f7ea1` il ritardo è sceso ma restava (~7 passi di Snake): leggendo la UART solo
   una volta per fotogramma, la FIFO da 16 byte si riempiva, RTS fermava il chip e passava
   circa un pacchetto per fotogramma. Ora la ricezione è **a interrupt** in un buffer
   circolare da 16 KiB, e i tasti premuti tra due fotogrammi restano validi per uno
   (una pressione breve non si perde). Il test in QEMU manda una raffica di 600 report e
   poi un tasto: deve arrivare entro 0,5 s (misurati ~0,1 s).
   Sul Pi (kernel `7da5130`): **DS4 via Bluetooth perfettamente reattivo**, riconnessione
   con PS ok.
3. Rimandato (non necessario oggi): avvio più rapido (firmware caricato a velocità alta),
   abbinamento anche dal menu, più controller insieme, BLE (Xbox recenti), luce e
   vibrazione del DS4.

Analisi dei costi: ~3000 righe di C (5 volte lo stack USB), 10–15 prove sul Pi; nessun
emulatore del chip, quindi niente test in QEMU se non su tracce HCI registrate.
- La UART PL011 passa al chip BCM43438 (GPIO 30–33, RTS/CTS); la console seriale si
  sposta sulla **mini UART** (stessi pin GPIO14/15, `core_freq` fissa in `config.txt`).
- Accensione del chip e caricamento della patch firmware Broadcom (`BCM43430A1.hcd`,
  scaricata con `make firmware`, non nel repository), poi baud rate alto.
- HCI (reset, ricerca, connessioni), L2CAP, SDP client (descrittore HID), HID classico
  sui canali 0x11/0x13, abbinamento SSP "Just Works" dal menu (voce "Abbina
  controller"), chiavi salvate su SD (M11) e riconnessione automatica.
- Controller di riferimento: **DualShock 4** (Bluetooth classico, abbinamento SSP;
  report 0x11 dopo il comando che lo attiva). Il suo report viene prima mappato via USB
  (054c:05c4 / 09cc), poi riusato uguale via Bluetooth.
- BLE (controller Xbox recenti) in un secondo momento: GATT + abbinamento LE.
- Alternativa senza costo, già funzionante: ricevitore USB del controller
  (es. 8BitDo USB Adapter 2), visto come controller USB.
- **Fatto quando:** il controller di riferimento si abbina dal menu, si riconnette da
  solo alla riaccensione e i giochi demo si giocano senza fili.

## M13 — Cartucce con codice ARM nativo (M) — chiusa senza implementazione
Decisione 2026-09-30: chiusa dall'autore; il piano resta qui se servirà.
- **ARM nativo**: sezione di codice ARM in `.bm` (per giochi in C), caricata in una
  zona di memoria dedicata con API tramite tabella di funzioni; senza protezione
  della memoria (solo cartucce fidate).
- **Fatto quando:** un gioco demo in C gira come `.bm` nativa.

## M14 — Grafica 2.0 (M) — ✅ verificata sul Pi (2026-09-30)
Fatto finora (da verificare sul Pi):
- driver DMA (`src/drivers/dma.c`, canali assegnati insieme all'audio). Sul Pi la
  prima versione (burst da 8, priorità alta, `WAIT_RESP`) dentro `p` ha bloccato il
  sistema: ora usa le impostazioni di Circle e si prova con il comando `D`, passo per
  passo, con ogni passo scritto sullo schermo prima di eseguirlo; solo se il test
  passa il disegno `.bm` "via RAM" copia i fotogrammi con il DMA;
- 3D: clipping sul piano vicino, nebbia (`fog3d`), rollio della camera, `project3d`;
- gioco di prova **Astro Wing** (`carts/astrowing`, in stile Star Fox).
- **texture** sui triangoli (prospettiva corretta, anche dopo il clipping; `mesh(v, f, uv)`
  in Lua con lo sprite sheet);
- **menu grafico**: ogni cartuccia è una scheda 3D a forma di Memory Stick Duo con la
  copertina stampata (sezione `COVER` 128×80 nel `.bm`, `mkbm.py --cover`, copertine
  dei giochi demo da `scripts/mkcovers.py`, etichetta col titolo per le altre) e i
  contatti in rame sul retro; la scheda scelta ondeggia e ogni 7 s si gira.
- **luce** per le `.bm` (`light_begin`/`light`/`light_end`, griglia 4×4 in C) e i
  pulsanti **X/Y** (`btn(6)`, `btn(7)`); gioco **Hunter's Night** (`carts/hunt`, 320×180,
  città gotica 2048×2048 illuminata solo da lampade e candele, mannaia a sega, pistola
  che stordisce, schivata, fiale di sangue, lampade-checkpoint, boss nella cattedrale).
- Blocchi sul Pi (Astro Wing dopo un paio di minuti, con ronzio audio): il VFP11
  dell'ARM1176 non era in modalità **RunFast** e mandava in eccezione i numeri
  denormali (una velocità che si dimezza a ogni fotogramma lo diventa in ~90 s); la
  schermata d'errore finiva sulla pagina nascosta del gioco. Ora: RunFast all'avvio,
  schermata d'errore visibile anche nei giochi con "while: <cosa girava>", e un
  **watchdog** (3 s) che riavvia un Pi bloccato: all'avvio successivo la console dice
  in rosso cosa stava facendo (`src/kernel/crumbs.c`, area di RAM non azzerata).
- Misure sul Pi (kernel `af8942d`): `D` passa tutti i passi. 450 KiB RAM → RAM: CPU
  3,70 ms, DMA 2,99 ms; riempimento RAM 450 KiB: CPU 1,02 ms, DMA 0,85 ms; fascia di
  schermo (48 righe): riempimento CPU 0,29 ms / DMA 0,23 ms, copia RAM → schermo CPU
  0,87 ms / DMA 0,26 ms (3,4×: la CPU paga la lettura della SDRAM). `p` (prima di `D`,
  copia con la CPU): diretto 8,58 ms, via RAM 11,85 ms. Astro Wing: 59,9 fps,
  update+draw 6,34 ms di media (max 12,57), nessun blocco dopo RunFast.
- Misure sul Pi (kernel `d143ce1`, 2026-09-30, WiFi collegato): `D` passa tutti i passi.
  450 KiB RAM → RAM: CPU 3,78 ms, DMA 2,99 ms; riempimento RAM: CPU 1,01 ms, DMA 0,85 ms;
  fascia di schermo: riempimento CPU 0,29 / DMA 0,23 ms, copia RAM → schermo CPU 1,15 ms /
  DMA 0,245 ms (4,7×). Il DMA dà gli stessi tempi di `af8942d`; la copia verso lo schermo
  fatta dalla CPU è più lenta (0,87 → 1,15 ms), da capire (clock, throttling o la rete
  attiva: lo stress test `s` stampa clock e throttling).
- **Gouraud** (2026-09-29): `draw3d(..., flag 4)` calcola la luce (direzionale e
  lampade) sui vertici, con le normali medie delle facce che li condividono, e la sfuma
  sulla faccia con un dithering ordinato 4×4 (niente bande del RGB565); vale anche per
  le facce con texture e dopo il clipping. `tri(..., c, c1, c2)` fa triangoli 2D
  sfumati. Le texture fanno la divisione prospettica ogni 16 pixel (lineari in mezzo)
  invece che a ogni pixel. Il nucleo del boss di Astro Wing è liscio. Test host
  (`tests/bm`) e due righe nuove nello stress test `s`: "3D smooth (Gouraud)" e
  "3D textured" (le stesse sfere), da misurare sul Pi.

- Misure sul Pi (kernel `8298b15`, 2026-09-29), stress test `s` con un DS4 collegato:

  | Test | 60 fps | 30 fps | µs/oggetto |
  |---|---:|---:|---:|
  | sprites 16×16 (C) | 3500 | 8237 | 3,52 |
  | sprites 32×32 (C) | 1183 | 2791 | 10,37 |
  | triangles 2D ~170px | 2053 | 4838 | 5,98 |
  | 3D spheres 96 (C) | 15 (556 tri) | 93 (3601 tri) | 208 |
  | 3D smooth (Gouraud) | <1 | 45 (1726 tri) | 277 |
  | 3D textured | <1 | <1 | 262 |
  | sprites 16×16 (Lua) | 1854 | 3826 | 8,41 |
  | 3D spheres 96 (Lua) | 26 (1015 tri) | 108 (4200 tri) | 184 |

  Rispetto a `792787f` la parte C è più lenta (sprite −22% con la stessa pendenza:
  ~3,5 ms fissi in più per frame; 3D −50%) mentre la parte Lua, subito dopo, no: la
  causa non è chiara, per questo lo stress test ora stampa prima e dopo la parte C clock
  ARM/core, temperatura, stato di throttling del firmware e il tempo di un ciclo di sola
  CPU. Con n = 1 la sfera copre quasi tutto lo schermo (~180 000 pixel): Gouraud e
  texture non ci stanno in 16,7 ms, cioè costano per pixel molto più del piatto.
  Titan Clash 11,6 ms (61 fps); Chaos Kitchen 1-1 14,1 ms ma 54 fps (alcuni frame oltre
  16,7 ms); Astro Wing con il nucleo liscio ok.
- **Rasterizzatore più veloce** (dopo le misure): gradienti di profondità, colore e
  texture calcolati una volta per triangolo invece di 2–3 divisioni per riga, `ceilf`
  (funzione di libreria) sostituita da un arrotondamento in linea, Gouraud senza
  saturazioni per pixel (i colori vengono limitati sui vertici), la matrice della camera
  in cache tra un `draw3d` e l'altro e nessun seno/coseno per le mesh non ruotate. Le
  facce piatte danno gli stessi identici pixel di prima (test host).
- **Texture Room** (`carts/texroom`, 2026-09-30): la demo del criterio di chiusura. Una
  stanza in prima persona a 320×180 tutta con texture (pavimento di pietra e muschio,
  pareti di mattoni e assi, pilastri di metallo, casse di legno che ruotano; texture
  32×32 disegnate da `mkassets.py`), giro automatico della camera. In alto: ms di CPU
  del fotogramma, fps, triangoli e pixel con texture (circa 460 triangoli e 61 000
  pixel); B cambia il numero di casse (4, 8, 16, 32) per trovare il limite, X toglie la
  luce dal pavimento. Test QEMU `test_texroom` (le texture sullo schermo, nessun
  errore).
- **Texture Room sul Pi** (2026-09-30), giro automatico:
  - 8 casse (la partenza): 11,6 ms di CPU, **60 fps**, 456 triangoli, 60 516 pixel con
    texture (circa uno schermo 320×180 intero, 57 600 pixel);
  - 32 casse: 18,0 ms, 54 fps, 586 triangoli, 96 992 pixel (1,7 volte lo schermo: le
    casse coprono pavimento e pareti già disegnati).
  - Dalla differenza, circa 0,18 µs per pixel con texture in più: a 60 fps ci stanno
    intorno ai 90 000 pixel con texture per fotogramma.
- Decisione 2026-09-29: il **modo 32 bit** è rimandato (fuori da M14): raddoppia la
  banda di memoria, che è il limite del Pi Zero, e le sfumature ora le copre il
  dithering.

Previsto:
- **DMA** del BCM2835 per riempimenti e copie (liberano la CPU: `cls`, mappe, copia
  dei frame) e misura sul Pi di cosa conviene (la lettura della SDRAM è il collo di
  bottiglia: vedi M9).
- ~~Modo **32 bit** (XRGB8888) per le `.bm`~~: rimandato (vedi sopra).
- 3D: texture sui triangoli e Gouraud (fatti); rimisurare `docs/STRESS.md` sul Pi.
- Menu grafico con anteprime delle cartucce (immagine nell'header `.bm`).
- **Fatto quando:** lo stress test mostra il guadagno del DMA e una demo 3D con
  texture gira a 60 fps.

✅ Verificato sul Pi (2026-09-30): il test DMA (`D`) mostra il guadagno (copia RAM → schermo
4,7 volte più veloce della CPU, vedi le misure sopra) e Texture Room gira a 60 fps con 8
casse. Restano aperti, fuori da M14: la parte C dello stress test più lenta di `792787f`
(causa non chiara) e il modo 32 bit (rimandato).

---

## M15 — Editor sulla console (L) ✅ verificato sul Pi Zero W
Decisione 2026-09-28: editor **sulla console** (come PICO-8), prima del multiplayer.
- **`bm editor`**, sempre ultimo nel menu delle cartucce (freccia su dal primo; e `e` dal monitor): una
  cartuccia `.bm` incorporata nel kernel (`carts/editor/main.lua`).
- **Codice** (F1): colori della sintassi Lua, numeri di riga, scorrimento, rientro
  automatico, Ctrl+Z annulla, Ctrl+K taglia riga, Ctrl+D duplica riga.
- **Sprite** (F2): pixel ingranditi (8×8 o 16×16), foglio intero a fianco, tavolozza di
  32 colori più trasparente, matita, riempimento, contagocce, specchio, copia/incolla,
  annulla.
- **Mappa** (F3): la mappa a grandezza reale, piazza/preleva tile, riempimento, scelta
  della tile dal foglio, annulla.
- **Menu** (Esc): nuovo, apri (i `.bm` della SD), salva, salva come (nome 8.3 in
  `/carts`), titolo, autore, risoluzione, **prova** (Ctrl+R/F5): salva, gioca, poi
  torna all'editor; se il gioco si ferma con un errore l'editor apre la riga in rosso.
- Tastiera USB per scrivere; col gamepad (Bluetooth) si disegna: A disegna, B preleva,
  X colore/tile successivo, Y+sinistra/destra cambia pagina, Y+B menu.
- Kernel: `keyp()` (tasti come testo, anche frecce, F1–F5, Ctrl+lettera; la seriale
  capisce le sequenze dei terminali), `ls`, `cart_load`, `cart_new`, `cart_save`,
  `cart_run`, `cart_arg`; la tastiera in modalità testo non fa più da gamepad ed Esc
  non esce.
- Test in QEMU (`test_editor`): nuovo progetto, salva sulla SD, prova, errore riportato
  alla riga, `fsck.vfat` pulito.
- Dopo: editor di suoni ed effetti, colori della tavolozza modificabili, copertina
  disegnata nell'editor, cerca nel codice, sprite sheet più grande di 256×256 a zoom.

## M16 — Multiplayer locale con controller Bluetooth (L) — ✅ chiusa (2026-09-30)
Decisione 2026-09-28: solo **Bluetooth**; USB e hub restano con un solo dispositivo.

Fatto (QEMU, `test_bt_pair_and_reconnect`, `test_bt_two_pads`):
- `src/bt/bt.c`: fino a 4 collegamenti ACL contemporanei, ognuno con i suoi canali L2CAP
  (stessi CID locali: sono per collegamento), la sua chiave e il suo giocatore; chiavi
  `bt_pad1`…`bt_pad4` (il vecchio `bt_pad` è il giocatore 1 e viene convertito al primo
  abbinamento); `T` abbina il prossimo posto libero (se sono tutti presi, sostituisce il
  primo pad non collegato); riconnessione con PS per tutti.
- Luce del DS4 nel colore del giocatore: report di output 0x11 sul canale interrupt
  (intestazione 0xA2, CRC-32 come vuole il pad), che passa anche il pad al report
  completo a 125 Hz (8 ms: quattro pad stanno nei 921600 baud della UART).
- Input per giocatore (`hid_players`, `input_players`): tasti e levetta di ogni pad; la
  tastiera/USB e la seriale sono il primo giocatore senza pad.
- API `.bm`: `btn(i, [p])`, `btnp(i, [p])`, `players()` (quanti e quali), `stick([p])`
  (levetta analogica, o la croce). **Differenza dalla decisione iniziale:** senza `p`,
  `btn(i)` risponde a *qualsiasi* controller invece che al solo giocatore 1: così i
  giochi a un giocatore non cambiano davvero (con "default 1" la tastiera smetterebbe di
  funzionare appena si collega un pad, perché diventa il giocatore 2).
- Menu: `pads: 1 2 - -` nell'intestazione e nella barra di stato; `Y` mostra i tasti di
  ogni giocatore; Pong con la modalità 2 giocatori.

Da provare sul Pi: due DS4 insieme (ritardo d'input come in M12), la luce, Pong a 2.

Sul Pi (2026-09-29, `8298b15`): con un pad collegato, un secondo DS4 già abbinato si
connetteva, apriva il canale **SDP** (PSM 1) prima di quelli HID e, al rifiuto, chiudeva
(motivo 0x13), in ciclo. Ora bm ha un piccolo server SDP che risponde "nessun record"
e, se un pad abbinato non apre i canali HID entro 1 s, li apre la console (come
nell'abbinamento). Test QEMU: in `test_bt_two_pads` il secondo pad fa proprio così.

Piano iniziale:
- Più controller abbinati: `bt_pad1`, `bt_pad2`, … in `config.txt` (il vecchio
  `bt_pad` diventa il giocatore 1); `T` abbina il prossimo controller libero; la
  riconnessione con il tasto PS funziona per tutti.
- Stack Bluetooth con più connessioni ACL e canali L2CAP HID contemporanei (il
  BCM43438 ne regge diverse; obiettivo: **4 giocatori**, 2 come minimo verificato sul
  Pi), il ritardo d'input misurato come in M12 con due pad insieme.
- Pulsanti **per giocatore**: il controller n è il giocatore n; la tastiera USB (e la
  seriale) è un giocatore a parte (il primo libero). Il LED dei DS4 prende il colore
  del giocatore (report di output).
- API `.bm`: `btn(i, [p])`, `btnp(i, [p])` con `p` = 1..4 (default 1: i giochi attuali
  non cambiano), `players()` = quanti giocatori sono collegati; l'uscita dal gioco
  (Start+Select, PS) resta per ogni controller.
- Menu: la barra di stato mostra i controller collegati; il test `Y` del monitor li
  elenca con i pulsanti premuti.
- Test in QEMU: il chip simulato (`FakeDs4Chip`) con due pad.
- **Fatto quando:** due DS4 collegati insieme giocano Pong uno contro l'altro (Pong con
  modalità 2 giocatori).

## M17 — Gioco cooperativo in stile Overcooked (L) — ✅ chiusa (2026-09-30)
Decisione 2026-09-28: il gioco è **Chaos Kitchen** (`carts/kitchen`), opera originale
(personaggi, ricette, cucine, musica e interfaccia nostri), in 3D. Comandi scelti
dall'utente: **A** prendi/posa/usa, **X** taglia/lava/usa (tenuto premuto), **B** scatto,
**Y** lancia; Start pausa. **Differenza dal piano iniziale:** niente cambio di cuoco con Y
per il giocatore solo (Y lancia): chi gioca da solo guida un cuoco, e le cucine sono
tarate sul numero di giocatori.

Fatto (host: `make test-kitchen`; QEMU: `test_kitchen`):
- **3D** con il renderer software: cucina come poche mesh grandi (pavimento senza
  z-buffer, facce nascoste tolte), quattro cuochi articolati con animazioni proprie,
  23 ingredienti (crudi e tagliati) e i piatti composti fatti in codice; una sola
  telecamera che inquadra tutti e si allontana quando i cuochi si separano.
- **Quattro cuochi diversi**: Basil (standard), Bun (lento, taglia più forte, niente
  scatto: saltella), Noodle (veloce), Pepper (piccolo, taglia bene, non lancia).
- **Cibo**: 23 ingredienti con stati (crudo, tagliato, cotto, bruciato), pentola,
  padella, forno, frullatore, piatti che si compongono, lavello e piatti sporchi.
  **51 ricette** in 4 livelli di difficoltà (ingredienti, passi, valore e pazienza).
- **Lanci** lungo un arco visibile, e prese al volo (anche in una pentola o su un
  bancone).
- **Campagna**: 31 cucine in 6 mondi, ognuno con una meccanica nuova (nastri, carretti,
  piattaforme sull'acqua, ghiaccio, vapore, porte, buio...), 3 stelle a cucina.
- **Disastri** comici e sempre risolvibili: topi, anatre, tubo che perde, fantasmi (luci
  spente), utensili posseduti, poltergeist, cibo che scappa, tornado.
- **Ordini** con pazienza, combo da x1 a x4 sulle mance.
- **Infinita**: la cucina parte piccola e cresce; la **cassa** è nella cucina e mostra 3
  offerte senza fermare il gioco; ciò che si compra cade in cucina da solo, vicino alle
  stazioni simili, mai dove chiuderebbe un passaggio (test: 40 acquisti di fila);
  fasce di prezzo, 7 categorie, difficoltà che sale, 5 cuori.
- **Pratica**: una ricetta, una cucina fatta apposta, niente orologio né pazienza.
- **Ricettario** (piatto in 3D che gira), **opzioni** (musica, suoni) e **statistiche**.
- **Salvataggi** sulla SD: stelle, record, ricette viste, record dell'infinita,
  statistiche.
- Effetti: particelle, vapore, fumo, sfrigolio, monete che volano, scosse dello schermo;
  la musica accelera quando gli ordini si accumulano.
- Il simulatore host (`tests/kitchen/sim.lua`) fa giocare cuochi del computer in ogni
  cucina e in ogni ricetta (tutte cucinabili), controlla che ogni stazione sia
  raggiungibile e misura istruzioni Lua e triangoli per frame.
- Misure in QEMU (circa 2× più lento del Pi nel 3D): 1-1 con un cuoco ~15–17 ms per
  frame, ~800 triangoli; l'infinita ~15 ms.

Da provare sul Pi: **Select** mostra in basso a destra ms per frame, fps e triangoli;
2+ giocatori con i DS4 in una cucina con piattaforme (mondo 3) e nella cucina più piena
(mondo 6) senza cali sotto i 60 fps. Codice per aprire tutte le cucine: sul titolo
su, su, giù, giù (o dalle opzioni).

Modelli degli chef dell'utente (2026-09-29): `carts/kitchen/models/chef1-4.glb` (low poly,
una texture ciascuno) importati da `carts/kitchen/import_chefs.py`: toglie ciò che hanno in
mano (mestolo, tagliere e coltello, insalata, padella) e gli occhi incavati (pezzi
sporgenti o schegge in orbite vuote) con occhi nuovi sul viso, divide ogni modello in
corpo, gambe e braccia con i perni alle articolazioni, abbassa le braccia alzate
(raddrizzando il gomito dove serve), ricuoce le texture in quattro atlanti 128×128 nello
sheet (y = 128) e scrive `src/16_chef_models.lua`. Nelle opzioni si torna agli chef
"classici" fatti di scatole.

Dopo: variazioni di lancio per cuoco.

- Decisione 2026-09-30: gli chef 3D hanno di nuovo le **dimensioni dei modelli
  originali** (tutti alti circa 1 casella: Basil 0,92, Bun 1,00, Noodle 1,00, Pepper
  0,88), invece delle altezze dei vecchi chef a blocchi (1,35 / 1,05 / 1,65 / 0,92)
  imposte dall'import; texture e forme invariate (`import_chefs.py`).
- **Restyling con texture mirate** (2026-09-30, dopo M14): piani dei banconi in legno,
  ante sui fronti, fornelli in acciaio con griglia e manopole, sportello del forno con
  la finestra accesa, casse a doghe, lavello con vasca, passe in acciaio, nastro a
  rulli (9 texture 32×32 nel foglio in (128, 80), `mkassets.py`), in **stile cartoon**
  come il resto del gioco: tinte piatte, contorni scuri, un riflesso chiaro, niente
  grana; pavimento e pareti
  restano a tinta unita (costano meno) con uno zoccolo scuro alla base dei muri.
  Opzione **KITCHEN: TEXTURED / FLAT** per tornare ai colori pieni se sul Pi non
  bastano i 60 fps. Da misurare sul Pi.
- **Sul Pi (2026-10-05, foto dell'overlay del dev kit, 3D sulla GPU):** 1-1 (Tomato Soup), un
  cuoco, TEXTURED, 640×360: **60 fps, 6,1 ms** a fotogramma (il massimo dell'ultimo secondo
  6,3), 5 mila istruzioni Lua, RAM 2,0 MB (massimo 2,3), 59 492 token. Il 29 settembre, con il
  3D sull'ARM, 14,1 ms e 54 fps. Restano il mondo 3 e il mondo 6 con 2+ giocatori.

## M18 — WiFi e console di rete (L/XL) — ✅ chiusa (2026-09-30)
Decisione 2026-09-28: versioni "leggere", in coda dopo M17.
- **WiFi**: il BCM43438 (lo stesso chip del Bluetooth) è sul bus SDIO. Driver SDIO sul
  secondo controller (la SD resta sul suo, o si scambiano come fa Linux), caricamento
  del firmware WiFi (`brcmfmac43430-sdio.bin`, `.txt`, `.clm_blob` da
  RPi-Distro/firmware-nonfree, come `BCM43430A1.hcd`), protocollo di controllo del chip
  (FullMAC: associazione e WPA2 li fa il firmware). Rete e password in
  `/bm/config.txt` (`wifi_ssid`, `wifi_psk`), comando del monitor per scegliere la
  rete.
- **TCP/IP**: lwIP (licenza BSD) con DHCP; IP e stato mostrati sullo schermo.
- **Console di rete** ("pseudo-SSH" leggero): una connessione TCP in chiaro, con
  password, che dà lo stesso monitor e la stessa REPL Lua della seriale (`nc` o uno
  script dal PC); si attiva dalle impostazioni, pensata per la rete di casa.
- **Invio dal PC via WiFi**: `bm_load.py` anche su TCP per `kernel.img` e cartucce
  (scritti sulla SD, poi riavvio o gioco) — fine delle copie a mano sulla SD.
  Aggiornamento del kernel sicuro: file nuovo verificato (CRC + firma), scambio con il
  vecchio, copia di riserva e ritorno al kernel precedente se il nuovo non parte
  (watchdog + chainloader).
- Niente emulatore del WiFi in QEMU: prove sul Pi con diagnostica a schermo (come M12);
  in QEMU si provano lwIP e la console di rete con una scheda di rete emulata, se
  disponibile, o con test su host.
- **Fatto quando:** il Pi prende un IP dalla rete di casa, dal PC si apre la console e si
  manda una cartuccia che parte subito.

**Passi** (2026-09-29, avviata):
1. ✅ (QEMU) **SD su SDHOST**: la scheda passa al controller SDHOST (`src/drivers/sdhost.c`,
   come Linux), così l'Arasan resta al WiFi. Se SDHOST non porta su la scheda, o fallisce
   un trasferimento due volte, si torna all'Arasan (`sd_emmc.c`) e il WiFi non parte.
   All'avvio la console scrive quale controller usa: `sd: SDHC card (sdhost), ...`.
2. ✅ (Pi, `ae2c226`: `chip 43430 rev 1`) **Chip WiFi**: comando `W` del monitor, un passo per riga:
   - clock a 32 kHz;
   - accensione (WL_REG_ON = GPIO41);
   - Arasan sui GPIO34-39 a 400 kHz;
   - CMD5 (condizioni SDIO), CMD3 (indirizzo), CMD7 (selezione);
   - CCCR, bus a 4 bit, 25 MHz;
   - funzione 1 e clock ALP;
   - identificativo del chip (atteso 43430).
   
   In QEMU (senza chip) si ferma a CMD5 senza bloccarsi.
3. ✅ (Pi, `26d66b8`: firmware `7.45.98`, MAC, CLM caricato) **Firmware**: `make firmware` scarica `brcmfmac43430-sdio.bin`,
   `.txt` (impostazioni della scheda Zero W) e `.clm_blob` da RPi-Distro/firmware-nonfree
   (seguendo i collegamenti simbolici del repository), `make sdcard` li copia in `bm/`.
   `W` poi, un passo per riga:
   - ricerca dei blocchi del chip nella ROM di enumerazione;
   - reset dei blocchi, dimensione della RAM del chip;
   - firmware scritto e riletto (inizio e fine), NVRAM condensata in fondo alla RAM;
   - avvio del processore ARM del chip, clock veloce (HT), funzione 2;
   - sul canale di controllo (SDPCM + BCDC): `ver` (versione del firmware),
     `cur_etheraddr` (MAC) e `clmload` (dati regolatori).
4. ✅ (Pi, `1c3d6a1`: 3 reti WPA2 trovate) **Scansione delle reti**: dopo il firmware `W` accende la radio
   (`WLC_UP`, niente risparmio energetico, eventi abilitati) e fa una `escan`. I risultati
   arrivano come eventi sul canale 1; l'elenco è ordinato per segnale, con canale,
   sicurezza (WPA2/WPA/WEP/aperta, dagli IE) e nome.
   Elenco numerato.
5. ✅ (Pi: "connesso") **Connessione**:
   - dopo l'elenco, `W` chiede il numero della rete e la password (asterischi);
   - la rete salvata (`wifi_ssid` / `wifi_psk` in `bm/config.txt`, in chiaro) si
     ricollega da sola;
   - WPA2-PSK (AES), WPA-PSK o aperta, con il 4-way handshake fatto dal firmware
     (`sup_wpa`, `WSEC_PMK`);
   - esito dagli eventi SET_SSID, PSK_SUP, LINK, DEAUTH.
6. ✅ (Pi, `cbd9356`: IP 192.168.1.108 dal router, ping 4–9 ms) **Indirizzo IP**: lwIP 2.2.0 (`third_party/lwip`, BSD), senza
   sistema operativo e interrogato dai cicli di input (`net_poll`, al più una volta al ms):
   - interfaccia Ethernet `wl` sul canale dati SDPCM (2) con intestazione BDC; i frame
     ricevuti durante ioctl e join vanno in una coda di 8, controllo di flusso con i
     crediti del firmware;
   - dopo la connessione `W` chiede l'indirizzo con DHCP (nome `bm`) e stampa
     `net: IP ...`; la rete resta attiva nel monitor, nel menu e nei giochi;
   - verifica: `ping <IP>` dal PC.
7. ✅ (Pi, `455e4ff`: login, `h`, `i`, REPL Lua dal PC) **Console di rete**: il monitor su TCP, porta 3333
   (`src/net/netcon.c`), un client alla volta:
   - password `net_password` in `bm/config.txt`; se manca, un PIN di 6 cifre creato,
     salvato e mostrato sullo schermo dopo l'IP; 3 tentativi, poi la connessione si chiude;
   - tutto ciò che il kernel stampa va anche al client (anello di 32 KiB svuotato da
     `net_poll`, mai dentro lwIP); i tasti del client arrivano come quelli della tastiera;
   - dal PC: `tools/bm_net.py IP` (terminale raw, Ctrl-] esce);
   - in chiaro: solo per la rete di casa (TLS con M19);
   - test sul PC: `make test-net` (lwIP con interfaccia di loopback).
8. ✅ (Pi; confermato il 2026-09-30 con `--kernel` e `--send` dopo il cambio di nome) **File dal PC e WiFi all'avvio**:
   - porta TCP 3334, stessa password della console (`src/net/netxfer.c`);
     richiesta `BM3X`, operazione, password, percorso, dimensione, crc32, dati;
   - ✅ (Pi, `ddca333`) `bm_net.py IP --send gioco.bm` → salvata in `/carts` (nomi 8.3, `--name`,
     `--to`); il menu rilegge la SD da solo;
   - ✅ (Pi: Pong a 59,9 fps) `--play gioco.bm` → giocata subito (dal menu o dal monitor), senza salvarla;
   - ✅ (Pi: 1.3 MB in 7,7 s, 170 KiB/s) `--kernel build/kernel.img` → scritto come `kernel.img` (prima i dati, poi la
     voce della directory: un'interruzione lascia il vecchio o il nuovo) e riavvio;
   - ✅ (Pi, `ddca333`) all'avvio la rete salvata si ricollega da sola, senza scansione
     (`wifi_boot=0` in `bm/config.txt` la spegne); l'IP compare nella barra di stato;
   - test sul PC: `make test-net` (salvataggio, password, crc, play, kernel);
   - i tasti del terminale di rete arrivano anche a menu, giochi, pager e demo, come
     quelli della seriale (`input_remote_getc`); Esc da solo esce dal menu.
9. Poi: aggiornamento del kernel da GitHub (M19), rete nelle cartucce.


## M19 — HTTPS: aggiornamenti e "git leggero" (L) — ✅ chiusa (2026-10-05)
- **TLS**: mbedTLS (licenza Apache 2.0) sopra lwIP; certificati radice essenziali sulla
  SD.
- **Aggiornamenti da internet**: il Pi controlla le release di GitHub del progetto,
  scarica `kernel.img` e cartucce, verifica la firma e installa come in M18.
- **"git leggero"** invece di git completo: in lettura, l'archivio di un ramo o di una
  release di un repository (es. cartucce da un repository di giochi); in scrittura,
  le API di GitHub con un token personale per caricare un file (es. un `.bm` salvato
  dall'editor). Token in `config.txt`.
- Più avanti, solo se serve davvero: SSH vero, git completo (clone/push).
- **Fatto quando:** un aggiornamento pubblicato come release arriva sul Pi dal menu, e
  l'editor carica un gioco su un repository.

Passi (2026-09-29):
1. ✅ (Pi, `3e1bedb`: example.com 200 in 114 ms, ora da SNTP) **HTTP e ora di rete**: `src/net/stream.c` (connessione TCP usata
   come un socket bloccante: DNS, scrittura, lettura con timeout, attese che fanno girare
   `net_poll`), `src/net/http.c` (HTTP/1.1: GET/PUT, redirect, corpo a lunghezza, a
   blocchi o fino alla chiusura, trasporto intercambiabile), ora da `pool.ntp.org`
   (SNTP di lwIP) stampata come `net: time ...`. Monitor `G`: scarica un indirizzo e
   mostra stato, dimensione, velocità e l'inizio. Test sul PC: `make test-http` (client
   HTTP contro un server Python locale) e `make test-net` (stream su lwIP).
2. ✅ (Pi, 2026-09-30: github.com 200, 576 220 byte in 2977 ms, 189 KiB/s; api.github.com 200,
   2396 byte in 432 ms) **HTTPS**: mbedTLS 3.6.2 LTS (Apache 2.0, `third_party/mbedtls`,
   41 sorgenti) sotto lo stesso trasporto (`src/net/tls.c`); configurazione
   `src/net/bm_mbedtls.h`: client TLS 1.2, ECDHE (P-256, P-384, X25519), AES-GCM e
   ChaCha20-Poly1305, certificati RSA ed ECDSA; entropia dal generatore hardware del
   BCM2835 (`src/drivers/rng.c`); 21 certificati radice in `bm/ca.pem`
   (`scripts/make-ca.sh`, dalla lista Mozilla: GitHub, Let's Encrypt, Google, Amazon,
   DigiCert, SSL.com); verifica di catena, nome e date (aspetta l'ora SNTP). Kernel +150 KB.
   Test sul PC: `make test-https` (certificati ECDSA e RSA, 1 MiB, chunked, redirect;
   rifiuti per nome sbagliato, certificato scaduto, CA sconosciuta; una catena che prosegue
   oltre la radice nota con un cross-certificato di una CA sconosciuta).
   - Sul Pi `https://example.com/` è rifiutato ("not correctly signed by the trusted CA").
     L'errore ora dice quale radice manca ("no root in bm/ca.pem for ..."): sul Pi (`2a62ce0`)
     "AAA Certificate Services" (Comodo), che non è più nella lista Mozilla. example.com ha un
     certificato SSL.com (Cloudflare) e la catena finisce con il cross-certificato di "SSL.com
     TLS ... Root CA 2022" firmato da AAA: aggiunte le due radici SSL.com 2022, mbedTLS si ferma
     lì. ✅ Sul Pi (`9cda3c7`, 21 radici): example.com 200, 713 byte in 453 ms.
   - `scripts/make-ca.sh` con la lista Mozilla di giugno 2026 si ferma: `DigiCert_Global_Root_CA`
     non c'è più. Le radici SSL.com sono state aggiunte in coda al file, senza rigenerare le 19
     già provate sul Pi.
   - Dopo un riavvio (kernel `14600e6`) `G` ha dato "no random numbers (-52)": il generatore
     hardware, acceso solo alla prima richiesta, scarta i primi numeri (riscaldamento) e ci
     metteva più dei 200 ms concessi; prima lo accendeva il Bluetooth LE. Ora parte all'avvio
     del kernel e la prima lettura aspetta fino a 3 s.
3. ✅ (2026-10-04/05: la chiave nel secret `BM_RELEASE_KEY`, le release v0.1.0, v0.2.1 e v0.2.3
   pubblicate dalla CI con il manifesto firmato) **Release**: GitHub Actions costruisce
   `kernel.img` e le cartucce a ogni tag `v*`, le allega alla release con un manifesto
   (versione, SHA-256, firma ECDSA P-256 con una chiave nei secret del repository; la chiave
   pubblica è nel kernel).
   - Job `release` in `.github/workflows/ci.yml`: solo sui tag `v*` e dopo i test; `make
     release VERSION=<tag>` (il kernel dice il tag come versione) e `gh release create`.
   - `scripts/mkrelease.py`: `manifest.txt` di testo (`bm release`, `version`, `commit`, una
     riga `file <nome> <percorso sulla SD> <byte> <sha256>` per file) e `manifest.sig` (DER),
     firmato con `openssl` e controllato con `keys/release-pub.pem`: un secret che non è la
     coppia della chiave nel kernel ferma il CI, non il Pi.
   - Nel kernel `src/net/release.c`: chiave da `keys/release-pub.pem` (incorporata con
     `embed.S`; finché è il segnaposto niente aggiornamenti), firma, righe (percorsi senza `..`,
     nomi sicuri per gli indirizzi), SHA-256 dei file. Test sul PC: `make test-release`.
   - Nella release: `kernel.img`, gli 8 giochi, `ca.pem` (va in `/bm`), `manifest.txt`,
     `manifest.sig`. Il Pi potrà leggere l'ultima da
     `https://github.com/f-accomando/bm/releases/latest/download/manifest.txt` (repository
     pubblico, niente JSON).
4. ✅ (QEMU, `test_update`; PC, `make test-release`) **Aggiornamento dal Pi**
   (`src/kernel/update.c`, 2026-10-03): Settings > System > *Check for updates* (e `u` nel
   monitor) legge il manifesto dell'ultima release, ne controlla la firma, confronta la
   versione (`release_compare`: più nuova, la stessa, più vecchia, o il kernel è una build
   dei sorgenti) e mostra i file che cambierebbero; *Install the update* chiede conferma,
   scarica e controlla tutto prima di scrivere (SHA-256, marchio `bmK6`/`bmK7` dei kernel),
   tiene i kernel di prima in `/bm/backup`, aggiorna i giochi presenti sulla SD (quelli tolti
   restano tolti: li ha il Market), `bm/ca.pem`, poi i due kernel (quello della scheda per
   ultimo) e riavvia. `update_url=sd:/...` e `bm/release.pem` per le prove. Il client
   HTTP segue indirizzi fino a 2 KB: GitHub manda i download delle release a link firmati
   di circa 1 KB, che prima venivano tagliati a 400 caratteri.
   - **Da fare sul PC**: la chiave (`scripts/release-key.sh`, secret `BM_RELEASE_KEY`) e il
     primo tag `v*`; il job `release` del CI ora installa numpy e Pillow (giochi).
   - **Da verificare sul Pi**: il controllo e l'installazione da GitHub via WiFi (le release
     scaricano da `release-assets.githubusercontent.com`: se manca la radice in `bm/ca.pem`
     l'errore lo dice).
   - ✅ Sul Pi (2026-10-05): gira il kernel v0.2.3 costruito dalla CI per la release (i report
     del Pi dicono kernel e branch `v0.2.3`); anche la RGB30 ha preso la v0.2.1 della release.
     Sulla RGB30 il riavvio dopo l'installazione lasciava lo schermo nero: corretto nel codice
     (`plat_reset`, spegne schermo e WiFi prima), da riprovare sulla console (sezione M40).
5. ~~**Git leggero in lettura**: cartucce da un repository (API "contents" di GitHub,
   file per file, senza archivi da decomprimere).~~ Sostituito dal Market (M25): catalogo
   firmato da GitHub Pages, download e SHA-256 dei giochi.
6. **Git leggero in scrittura**: l'editor carica un `.bm` su un repository con un
   token personale (`github_token` in `bm/config.txt`, API "contents", PUT).
   - In parte (2026-10-04, branch `bm-core`): i **report dei test** vanno da soli nel branch
     `reports` di `f-accomando/bm` (`github_put` in `src/net/github.c`, `src/kernel/reports.c`;
     nome con kernel e branch), così chi sviluppa li legge senza foto.
   - ✅ Sul Pi e sulla RGB30 (2026-10-05): nove report arrivati nel branch `reports` con il
     token in `bm/config.txt` (PUT dell'API "contents" via HTTPS). Un gioco dal Pi a un
     repository è la pubblicazione nel Market (`src/kernel/publish.c`, pull request a
     `f-accomando/bm-market`): la sua prova sul Pi resta in M25.

**Chiusa il 2026-10-05** (decisione dell'utente): HTTPS, ora di rete, release firmate e
aggiornamento dal menu funzionano sul Pi; la scrittura su GitHub con un token (i report)
funziona sul Pi e sulla RGB30. Restano fuori: la pull request di un gioco verso il Market
(da provare sul Pi, M25), il riavvio della RGB30 dopo un aggiornamento (M40, corretto nel
codice, da riprovare), SSH e git completo (non servono).

## M20 — Picchiaduro a robot giganti (XL) — ✅ chiusa (2026-09-30: base giocabile)
Decisione 2026-09-28: in coda dopo M19. Concept completo dell'autore:
[`docs/giochi/mecha-fighter-concept.md`](giochi/mecha-fighter-concept.md).
Picchiaduro 2D a incontri tra robot modulari alti come grattacieli: struttura e feeling
di Street Fighter II Turbo (SNES), con dash, air combo, juggle e tag team come in Marvel
Super Heroes vs. Street Fighter. Stile 16-bit ricco, **non** minimalista: robot grandi
quanto i personaggi di SF2 (metà schermo o più), molte animazioni, fondali in
parallasse. Hangar animato dove si costruisce il robot; peso dell'equipaggiamento che
cambia davvero la mobilità; risorse vita, armatura, energia, calore, tag; armatura
localizzata che si rompe sullo sprite insieme alla barra.

Si procede per incrementi, sempre con una build giocabile:
1. **Progettazione** (`docs/giochi/mecha-fighter-design.md`): i 15 punti del concept
   (conflitti tra meccaniche, loop, risorse, movimento, combo, tag, danni,
   equipaggiamento, statistiche, arene, HUD, hangar, vertical slice), approvata
   dall'autore prima di scrivere codice.
2. **Kernel, se serve**: sprite sheet grandi o più sheet per cartuccia (i robot non
   stanno in 256×256), disegno di sprite scalati/specchiati veloce in C, eventuale
   modalità a 32 bit (M14.3) per palette più ricche; misure di frame rate sul Pi.
3. **Vertical slice 1v1**: 2 robot, movimento, salto, dash, pugni e calci, parata,
   combo, un'arma con calore, energia, barre vita e armatura, un pezzo di armatura
   distruttibile, una piccola arena, robot grandi. Contro la CPU o con due
   controller (M16).
4. **Personalizzazione e hangar**: robot composti da parti (telaio, armatura, braccia,
   armi, booster, scudo) disegnate a strati sullo stesso scheletro di animazione, così
   ogni pezzo cambia l'aspetto e le statistiche; hangar animato.
5. **Tag team e 2v2** (4 controller con M16), scudi olografici, danni localizzati
   completi.
6. **Contenuti**: arcade/campagna con boss, altre arene e armi.
- Prime idee tecniche (da confermare nella progettazione): robot "a strati" (parti
  separate su uno scheletro con pose chiave) per avere molti frame e tutte le
  combinazioni di equipaggiamento senza disegnare ogni robot intero; danno per zona che
  sostituisce lo sprite della singola piastra (integra → danneggiata → staccata, con
  il pezzo che cade come detrito); logica del gioco a 60 Hz fissi con hitbox e
  hurtbox per frame.
- **Fatto quando:** il vertical slice è giocabile sul Pi a 60 fps con robot grandi e
  l'autore conferma il feeling; poi ogni passo successivo ha il suo criterio.

**Stato (2026-09-29): prima base giocabile, da provare sul Pi.** Su richiesta
dell'autore si è partiti subito da un MVP (passi 1–3 insieme, con un solo robot):
progettazione in [`docs/giochi/mecha-fighter-design.md`](giochi/mecha-fighter-design.md),
cartuccia **Titan Clash** (`carts/titan`).
- Kernel: sezione **SHEET8** del `.bm` (palette di ≤256 colori + RLE, decodificata al
  caricamento) e sheet fino a 4096 pixel di lato; `mkbm.py --sheet8`; test host in
  `tests/bm`.
- Arte pre-renderizzata (`mkrobot.py`): il robot VANGUARD è un modello 3D procedurale
  su uno scheletro, reso in vista 3/4 con cel shading a 6 toni e contorni, 63 frame in
  24 animazioni, **a strati** (armatura pesante, spallaccio integro/crepato, cannoni,
  spada sulla schiena o in mano) e in due livree (P1 acciaio e arancio, P2 cremisi e
  oro); hurtbox e hitbox per frame dalle ossa. `art.py`: città al tramonto in 4 piani
  di parallasse, hangar, effetti, scritte. Tutto in `sheet.png` (2048×3376, 166 colori).
- Gioco: 1P contro CPU (3 livelli), 2 giocatori, CPU contro CPU e demo; armatura
  leggera/pesante (velocità, salto e doppio salto, scatto, resistenza) e spada
  (fendente a energia) o cannoni (raffiche con calore); pugni e calci leggeri/pesanti
  anche accovacciati e in aria, parata alta/bassa, scatti anche aerei, cancel in mosse
  più forti, launcher e juggle, knockdown; vita e armatura con lo spallaccio che si
  crepa a metà e vola via a zero; round al meglio di 3 da 99 s; hangar animato
  (operai, saldature, gru); musica e suoni; pausa con la lista delle mosse.
- Test: `make test-titan` (menu, hangar, partita contro la CPU, pausa, tempo scaduto,
  24 incontri CPU contro CPU con ogni coppia di configurazioni e ogni livello,
  2 giocatori, demo; costo dei frame) e `test_titan` in QEMU. Select mostra il tempo
  di frame sul Pi.

- Correzioni 2026-09-30 (dopo le prove sul Pi): la **guardia** sta in equilibrio (bacino
  sopra il punto medio dei piedi, piante parallele al suolo: `balance` e `flat_feet`
  in `mkrobot.py`), e così camminata, accovacciata e piede d'appoggio dei calci; il
  **diretto** del braccio in primo piano (in piedi e accovacciato) si stende oltre il
  torace verso l'avversario (`reach`: braccio, rotazione del busto e piccolo affondo
  che portano il pugno più avanti possibile), quindi colpisce anche più lontano. Il
  pannello di debug (Select) mostra come il gioco legge i controlli (qualsiasi
  controller, pad 1 e 2, levetta, e per ogni robot pad, direzione e stato), per
  capire il problema della croce del controller segnalato sul Pi.
- Correzione 2026-09-30 (dalla foto del pannello di debug: `p1 .R..` ma `F1 ... dir 5
  stand`): il robot girato a destra non leggeva avanti e indietro. `numpad` scriveva
  `f.face > 0 and r or l`, che con `r` falso vale `l`: con una sola freccia premuta
  risultavano avanti e indietro insieme, cioè fermo; restavano solo su e giù, quindi
  niente camminata, parata o salti in diagonale. Il robot girato a sinistra (di solito
  P2) funzionava per caso, e il gioco contro la CPU aveva lo stesso difetto per P1.
  `make test-titan` ora prova le 8 direzioni su entrambi i pad e la camminata in avanti.
- Sigla della versione (2026-09-30): sul Pi sinistra e destra di P1 continuavano a non
  andare, cioè girava una `titan.bm` vecchia. `build.py` aggiunge `TITAN_BUILD`, le prime
  7 cifre dello SHA-1 dei sorgenti: si vede in basso nel titolo, nel pannello di Select
  (con `face` di ogni robot) e nel registro (`titan build ...`).
  ✅ Verificato sul Pi con la build `e34f7e8`: P1 cammina, para e salta in diagonale.

## M21 — Menu "home" e giochi sospesi (M) — ✅ chiusa (2026-09-30)
Decisione 2026-09-29: menu più pulito in stile console moderna (Nintendo Switch), per ora
solo per la scelta dei giochi.
1. ✅ (QEMU) **Griglia di copertine** (`src/kernel/menu_ui.c`, sostituisce il carosello 3D):
   - due schede in alto, **Games** e **Dev** (strumenti di sviluppo: l'SDK);
   - copertine 128×80 con angoli arrotondati, 4 per riga, scorrimento verso il basso;
   - movimento ortogonale con le frecce (sinistra/destra passano anche alla riga
     successiva; su dalla prima riga porta alle schede, dove
     sinistra/destra le cambiano; anche Tab, 1 e 2);
   - sfondo con la copertina scelta sfocata e scurita, dissolvenza al cambio;
   - anello azzurro "che respira" sulla copertina scelta, nome in una pillola sopra la
     griglia, dettagli e pulsanti in basso;
   - A (anche dalle schede) avvia la copertina evidenziata.
   
   L'editor ora si chiama **bm SDK**.
2. ✅ (QEMU, `test_suspend_resume`) **Giochi sospesi**:
   - Esc, o PS sul controller, esce dal gioco ma lo lascia congelato in memoria (stato
     Lua, sheet, mappa, 3D; audio muto);
   - nel menu la copertina ha il badge **Playing** (già disegnato da `menu_ui`);
   - A sulla stessa copertina riprende dal punto esatto;
   - avviare un'altra applicazione chiede conferma, chiude quella sospesa e libera la
     memoria.
   
   Fatto: `bm_run` (con `suspendable`), `bm_resume`, `bm_close_suspended`.
   - Alla ripresa tornano:
     - la stessa area di disegno (clip e camera);
     - il disegno via RAM, se il gioco usa le luci;
     - `time()` senza il tempo passato nel menu.
   - I tasti ancora premuti non contano come nuove pressioni.
   - Una sola applicazione sospesa alla volta, come sulle console.
   - `quit()`, un errore e le prove dall'SDK chiudono davvero.
   - (2026-10-04) In una partita in rete (`online(true)`) PS non sospende: chiede al
     giocatore se uscire e disconnettersi (M38, passo 5).
- **Fatto quando:** sul Pi il menu è fluido a 60 fps con tutte le cartucce e un gioco
  sospeso riprende dal punto in cui era.

## M22 — SDK e strumenti dedicati (XL) — ✅ chiusa (2026-10-05)
Decisione 2026-09-29: l'editor attuale diventa l'**SDK** (generico: progetto, prova,
salvataggio); intorno a lui strumenti specializzati, ognuno una cartuccia nella scheda
**Dev**, tutti con gli stessi formati.

**Stato (2026-10-01, sviluppato sul branch `sviluppo-sdk`, ora in `bm-core`):
bm Studio, sul PC.** Su richiesta
dell'utente, 22.3 (3D), parte di 22.2 (pixel art dello sheet) e di 22.5 (import/export)
arrivano prima come applicazione per il PC (`sdk/studio`,
[sdk/README.md](../sdk/README.md)); i formati sono quelli previsti qui sotto, quindi gli
strumenti sulla console potranno leggerli e scriverli.
- **Formato**: sezione **MESH** del `.bm` (tipo 8, `src/bm/bm.h`; era 6 nei primi file,
  ancora letti, prima che 6 andasse al banco di suoni): modelli con nome,
  vertici in float, facce con colore o texture dello sprite sheet (angoli in 1/8 di
  pixel), margine delle texture applicato al caricamento. Il kernel la controlla in
  `bm_parse`; API `model(nome)`, `models()`, `bounds3d(m)`.
- **bm Studio**: pagina web senza dipendenze (doppio clic su `index.html`, o
  `make studio`). Tessere dello sheet posate su una griglia (piano automatico o fisso,
  sulle facce esistenti), blocchi (le pareti tra blocchi vicini spariscono), selezione e
  spostamento, angoli (tetti, rampe, unione), pittura sul modello; editor dei pixel
  dello sheet; copertina dalla vista 3D; codice `main.lua`. Apre e salva il `.bm` al suo
  posto (File System Access di Chrome/Edge; altrove scarica). Scambia `.glb` (con lo
  sheet come texture; i `.glb` altrui portano le texture nello sheet) e `.png`.
- L'**SDK sulla console** tiene le sezioni che non modifica (i modelli) quando salva.
- **Build**: `mkbm.py --models file.glb` (o un `.bm`); `carts/<gioco>/models.glb` entra
  nella cartuccia da solo, con lo sheet se il gioco non ha `sheet.png`.
- **Esempio**: *Studio Village* (`carts/village`, 320×180), modelli costruiti con gli
  strumenti dello Studio (`mkmodels.js` → `models.glb`).
- **Test**: `make test-studio` (Node; gli stessi file letti da `bmmesh.py` e dal parser
  del kernel), `make test-studio-ui` (Playwright), QEMU `test_models`,
  `test_sdk_keeps_models`, `test_village`, `test_studio_cart`.
- Corretta la documentazione dell'ordine dei vertici: una faccia si vede dal lato da
  cui appare in senso **orario** (API e guida dicevano antiorario; l'esempio della
  piramide nella guida mostrava l'interno).
- **Da verificare sul Pi**: Studio Village (scheda Games); un `.bm` salvato da bm Studio
  copiato in `carts/` (il visualizzatore dei modelli di un progetto nuovo).

**bm Animator (2026-10-01, stesso branch): rigging, keyframe, animazione scheletrica,
3D→sprite (22.6).** Terza applicazione per il PC (`sdk/animator`), con le stesse regole
delle altre due (lavora sul `.bm`, porta il progetto da e verso bm Studio).
- **Formato**: sezione **ANIM** del `.bm` (tipo 9, `src/bm/bm.h`; era 7): per ogni modello le
  ossa (testa, coda, padre), l'osso di ogni vertice, le animazioni (keyframe di posa
  intera, linear / smooth / step, ciclo). Un osso gira intorno alla testa:
  M = M_padre · T(testa + t) · R(q) · T(−testa); ogni vertice segue un osso solo (parti
  rigide come sulla PS1, o stirate alle giunture se gli angoli condivisi sono di ossa
  diverse). Il kernel la controlla in `bm_parse`.
- **API**: `model()` porta lo scheletro, `animate(m, anim, t, [anim2, t2, k])` (anche il
  misto di due animazioni), `clips(m)`, `bone3d(m, osso)`; gli stessi conti in C
  (`runtime.c`) e in JavaScript (`sdk/studio/js/rig.js`).
- **bm Animator**: Rig (ossa trascinate per le giunture, specchio sinistra/destra, pelle
  per faccia o per angolo, a mano o all'osso più vicino), Animate (anelli per girare,
  coda per puntare, testa per spostare; linea del tempo con keyframe automatici, ciclo,
  riproduzione, onion skin, specchio e copia della posa, annulla), Sprites (fotogrammi da
  1–8 direzioni, camera piatta o in prospettiva, luce a bande, contorno, riduzione dei
  colori, nello sheet con il codice Lua per `sspr()`). Esporta `.glb` con giunture, pelle e
  animazioni. Si apre con un esempio: il paesano (idle, walk, wave).
- **22.6 (3D→sprite)**: fatto sul PC con un rasterizzatore software in JavaScript (gli
  stessi pixel nel browser e in Node). Restano da fare hitbox e hurtbox per fotogramma.
- **Esempio**: in Studio Village il paesano cammina sul sentiero, saluta alle estremità
  (due animazioni mescolate), porta una luce di notte (`bone3d`); nell'angolo la sua
  camminata pre-renderizzata a sprite. `carts/village/models.bm` (da `mkmodels.js`).
- **Build**: `mkbm.py --models file.bm` porta modelli, scheletri e sheet;
  `carts/<gioco>/models.bm` entra nella cartuccia da solo.
- **Test**: `make test-studio` (rig, ANIM, sprite, glTF animato verificato con un
  valutatore glTF; il parser del kernel sullo stesso file), `make test-studio-ui`
  (Playwright, anche l'Animator), QEMU `test_animation`.
- **Da verificare sul Pi**: Studio Village (il paesano che cammina e lo sprite nell'angolo).

**Studio 3D sulla console (2026-10-01, stesso branch): il player e la versione
semplificata.** Su richiesta dell'utente gli strumenti del PC restano quelli principali e
sulla console arriva una loro versione `.bm`: una cartuccia incorporata nel kernel
(`carts/studio3d`), nella scheda **Dev** e nelle opzioni di ogni gioco (*Open in the 3D
studio*; monitor `3`), sugli stessi file. Poi diviso in bm Studio e bm Animator (sotto).
- **Play** (il player): modelli, vertici, triangoli, ossa; camera che gira, animazioni
  (fotogramma per fotogramma, velocità), scheletro sovrapposto, misto di due animazioni.
- **Build**: cursore a celle, blocchi (senza pareti tra blocchi vicini), tessere su un
  lato della cella, pittura, tessere e colori dallo sheet del progetto, annulla; le facce
  sono identiche a quelle di bm Studio con gli stessi attrezzi.
- **Rig** e **Animate**: ossa aggiunte, spostate e cancellate, pelle all'osso più vicino;
  animazioni a keyframe (giri di 15° o 5° intorno a x/y/z, spostamenti, copia della posa,
  ciclo, ease, durata).
- **Menu**: apri, nuovo progetto (con le tessere iniziali di bm Studio), salva, prova il
  gioco e torna, modelli nuovi/rinominati/duplicati/cancellati.
- **Kernel**: `cart_data(tipo, [byte])` legge e sostituisce le sezioni MESH e ANIM del
  progetto (controllate prima; `model()`/`animate()` le usano subito, `cart_save` le
  scrive); `bone3d()` restituisce anche la coda dell'osso; il progetto aperto si azzera
  all'avvio di ogni cartuccia.
- **Input**: tastiera e gamepad; il puntatore arriverà con il mouse Bluetooth.
- **Test**: lo studio sul PC con le API sostituite (`tests/studio/studio3d_host.lua`, 70
  controlli, in `make test-studio`), i suoi file riletti da bm Studio, `bmmesh.py` e dal
  parser del kernel; QEMU `test_studio3d`.
- **Da verificare sul Pi**: scheda Dev → *3D studio*; aprire Studio Village, guardare il
  paesano (k: scheletro, b: misto), costruire qualche blocco in un progetto nuovo,
  salvarlo e provarlo (F5); fluidità del player e della costruzione sul Pi Zero.

**bm Studio e bm Animator sulla console (2026-10-01, branch `sdk-dev`).** Su richiesta
dell'utente lo studio 3D della console arriva al livello dei programmi per il PC e si
divide in due, con gli stessi nomi: **bm Studio** (`carts/studio`, monitor `3`) e **bm
Animator** (`carts/animator`, monitor `6`), nella scheda Dev e nelle opzioni di ogni gioco;
dal menu dell'uno si passa all'altro sullo stesso file
([sdk/README.md](../sdk/README.md#sulla-console-bm-studio-e-bm-animator)). Per ora tastiera
e gamepad; il puntatore di sistema (M32, `mouse(true)`) c'è, ma Studio e Animator non lo usano ancora.
- **bm Studio**: attrezzi block, tile (anche più tessere insieme), select (puntatore della
  tastiera sulle facce: scegli, tutte, unite; sposta, gira, capovolgi, specchia, altro
  lato, nuova tessera, gira la texture, scala, copia, cancella, in un modello nuovo),
  vertex (sposta e unisci gli angoli), paint (i pixel della tessera di una faccia, sul
  modello); viste luce / colori / fil di ferro e facce posteriori; conteggi con avvisi;
  pagina models (nuovo, rinomina, duplica, cancella, ordine, margine delle texture);
  titolo e autore.
- **bm Animator**: play; rig con specchio delle ossa (`.L` / `.R`), nome, padre, giunture
  che si muovono insieme, pelle rigida o liscia, facce assegnate a un osso; animate con
  keyframe prima/dopo e spostabili, posa specchiata, onion skin, lista delle animazioni
  (nuova, duplica, rinomina, cancella); **sprites**: un'animazione disegnata dal motore 3D
  della console nello sprite sheet (fotogrammi, misura, direzioni, camera, luce, contorno,
  colori) con il codice `sspr()`.
- **Kernel**: la libreria `require "bm3d"` (`src/script/bm3d.lua`, il codice comune),
  `cart_tool(nome, percorso)` (un altro strumento sullo stesso file), `cart_write` con
  `from = false` (cartuccia nuova). I due salvano con `cart_write`: nel file cambiano solo
  MESH, ANIM e lo sheet se dipinto (prima `cart_save` riscriveva tutto e lo sheet SHEET8
  diventava SHEET).
- **Test**: `tests/studio/tools3d_host.lua` (112 controlli, in `make test-studio`), i file
  riletti da bm Studio (`check_studio3d.js`), `bmmesh.py` e dal kernel; QEMU
  `test_studio_animator` (con le schermate).
- **Da verificare sul Pi**: Studio Village → X → *Open in bm Studio*: attrezzi 1-5,
  Tab (le tessere), F2 (i modelli); un progetto nuovo, *Save as*, F5; menu → *Open in bm
  Animator*: F2 rig (n, m, v), F3 animate (n, frecce, w, o), F4 sprites (Invio); la
  fluidità della selezione e della pittura con molti triangoli (il terreno del villaggio).

**bm Mesh (2026-10-01, stesso branch): le mesh di un `.bm`, anche quelle del codice.**
Su richiesta dell'utente: un editor `.bm` che legge le mesh, le modifica e le scrive nel
`.bm`, con il passaggio da mesh a modello e da modello a mesh, compatibile con le altre
app. Cartuccia incorporata (`carts/mesh`), nella scheda **Dev** e nelle opzioni di ogni
gioco (*Open in bm Mesh*; monitor `4`) ([sdk/README.md](../sdk/README.md#sulla-console-bm-mesh)).
- **Lettura**: i modelli della sezione MESH (bm Studio, studio 3D; con lo scheletro di bm
  Animator), le mesh scritte da bm Mesh nel codice e quelle che il **codice del gioco**
  costruisce con `mesh()`, `mesh_sphere()`, `mesh_cube()` (Astro Wing: 13, con i nomi
  delle variabili: ship, dart, ring, ...).
- **Modifica**: puntatore (frecce o croce), vertici e facce scelti uno per uno, tutti o
  collegati; sposta, ruota, scala (passi, assi x y z o la normale), estrudi, duplica,
  specchia e copia specchiata, faccia nuova su 3-4 vertici, unisci e salda i vertici,
  suddividi, gira le facce, colora, cancella; annulla e rifai; primitive (cubo, piano,
  sfera).
- **Mesh → modello** (`m`): una copia nella sezione MESH, `model("nome")` nel gioco;
  **modello → mesh** (`c`): una funzione `mesh_nome()` alla fine di `main.lua`, tra
  `-- [bm Mesh begin]` e `-- [bm Mesh end]` (lo stesso testo di "Copy as Lua" di bm Studio;
  bm Mesh riscrive solo quelle righe). Modificare una mesh del gioco ne fa una copia come
  modello (il codice del gioco non si riscrive).
- **Compatibilità**: le sezioni MESH (8) e ANIM (9) come le scrivono bm Studio, bm Animator
  e lo studio 3D; un modello con scheletro lo tiene (l'osso di ogni vertice segue i
  vertici aggiunti e tolti); il resto del file (sheet, mappa, copertina, suoni, codice
  fuori dal blocco) resta byte per byte; il codice si apre in bm Code.
- **Kernel**: `cart_meshes(percorso)` esegue il codice di un `.bm` in uno stato Lua a parte
  (`src/bm/meshcap.c`: `mesh()` e le primitive tengono una copia, le altre funzioni di bm
  non fanno niente, limite di istruzioni) e dà le mesh con il nome della variabile;
  `cart_write` accetta `sections` (MESH e ANIM, controllate) e il codice è facoltativo
  (`bm_rewrite_with` in `format.c`).
- **Test**: `test_meshcap` (la cattura sulle cartucce vere: Astro Wing, Texture Room,
  Chaos Kitchen), bm Mesh sul PC con le API sostituite (`tests/studio/mesh_host.lua`, 65
  controlli), i suoi file riletti da bm Studio (`check_mesh.js`), `bmmesh.py` e dal kernel;
  `test_bm` per `bm_rewrite_with`; QEMU `test_mesh` (Astro Wing: la nave come modello
  spostato e come codice, salvata).
- **Da verificare sul Pi**: scheda Dev → *bm Mesh*, oppure Astro Wing → X → *Open in bm
  Mesh*: la lista delle 13 mesh, la nave copiata come modello (m), vertici spostati (F2,
  a, g, frecce, Invio), Ctrl+S; tempi di lettura del codice e fluidità dell'editor.

**bm SDK, il centro della suite (2026-10-04, branch `sdk-update`).** Su richiesta
dell'utente l'SDK diventa l'hub del progetto previsto in 22.0, con l'estetica delle altre
app ([sdk/README.md](../sdk/README.md#sulla-console-bm-sdk-il-centro-del-progetto)):
- **pagine**: F1 progetto (gli altri programmi della suite 1-6 sullo stesso file, titolo,
  autore, schermo, target; cosa c'è nel file, misurato a pezzi con `timeslice`), F1 di
  nuovo il **dev kit**, F2 codice, F3 sprite e di nuovo mappa, F4 **3D** (modelli e
  animazioni del progetto che girano, il codice per usarli con `i`);
- **la suite**: `cart_arg().from` dice chi ha aperto uno strumento; bm Code, bm Pixel, bm
  Studio, bm Animator, bm Mesh e bm Sound aperti dall'SDK hanno *Back to bm SDK* nel
  menu;
- **modelli di gioco** (R16): Empty 2D, Platform 2D, Top-down 2D, Shooter 2D, 3D scene,
  3D with models, con codice, sprite e mappa;
- **dev kit**: `src/bm/tokens.c` conta i token (`stat(11)`, `code_tokens()`), `stat(12)`
  la memoria di Lua più alta, `stat(13)` quella dei dati, `stat(14)` il fotogramma più
  pesante; l'overlay (F11) ha due righe in più (RAM, token); dopo una prova l'SDK riceve
  i numeri della partita (`cart_arg().run`) e la seriale scrive la riga `dev kit:`;
- **.b16** (futuro): target del progetto, il file contro gli 8 MiB, le righe che il
  formato non avrà ([B16.md](B16.md) §8.5);
- **assistente**: il tipo di voce `guide` (28 guide per fare giochi 2D e 3D con l'SDK,
  `src/ai/kb/guide_sdk.txt`) e 22 voci API del 3D che non conosceva (modelli,
  animazioni, ossa, luce, effetti, mondi di collisione, `screen`); rete riaddestrata
  (343 voci; 240 domande su 253 con la risposta giusta tra le prime tre). F6 nell'SDK
  apre il modo giusto per la pagina (guide, codice, sprite, 3D: il modello entra nel
  progetto).
- **Test**: `tests/studio/sdk_host.lua` (in `make test-studio`), `test_tokens` (in `make
  test-bm`), il modo `guide` in `tests/ai/panel_test.lua`, QEMU `test_editor` (rifatto),
  `test_sdk_suite`.
- **Da provare sul Pi**: la pagina del progetto e il dev kit (dopo F5), la pagina 3D con
  Studio Village, un modello di gioco provato, 3 → bm Studio → *Back to bm SDK*, F11 in
  un gioco (le righe RAM e token).

Sotto-milestone:
- **22.0 Base comune**:
  - formato del progetto;
  - libreria di interfaccia condivisa in Lua (finestre, liste, dialogo dei file, aiuto
    su F12, puntatore);
  - l'SDK come hub che apre gli strumenti sul progetto corrente.
- **22.1 Codice**:
  - più file per progetto (come `carts/kitchen/src/`);
  - colori della sintassi, cerca e sostituisci;
  - salto all'errore;
  - aiuto sulle API (F1 sulla parola), completamento dei nomi delle API.

  **bm Code** (2026-10-01, fatto in QEMU): l'editor del codice nella scheda Dev (anche `C`
  dal monitor e "Open in bm Code" nelle opzioni di una cartuccia), `carts/code/main.lua`.
  - Più cartucce aperte in **tab** (F2/F3), **due pagine affiancate** (F4, F7 passa
    all'altra: due file, o due punti dello stesso file per la revisione).
  - **Font** 6x12 (106 colonne, 28 righe di codice), 8x14 o 8x16 (F10), da Terminus come
    quello della console: niente caratteri enormi.
  - Legge e scrive le cartucce **al loro posto**: `cart_read` / `cart_write` cambiano solo
    il codice, sprite, mappa, copertina e sezioni sconosciute restano; i nomi lunghi pure.
    Ctrl+N crea una cartuccia nuova, "Save as" una copia.
  - Colori della sintassi, numeri di riga, rientro automatico (e `end` che torna a posto),
    annulla/ripeti, selezione (Ctrl+B), copia/taglia/incolla, cerca e sostituisci, vai
    alla riga, F5 prova il gioco e torna sulla riga dell'errore.
  - L'assistente: F6 (la parola sotto il cursore), F9 spiega l'errore, righe
    `#entry: ... #` (M30).
  - Le tab e le modifiche non salvate restano per la volta dopo (sessione in `save()`).
  - Col pad: croce, Y+croce per pagine e tab, X l'assistente, Start il menu.
  - Restano per 22.1: più file per progetto, completamento dei nomi delle API.
  - Test: `test_code_editor` in QEMU (un `.bm` con nome lungo e sprite, due tab, due
    pagine, il salvataggio che tiene sheet e mappa, prova con errore e ritorno, F9,
    `#entry:` e annulla, cartuccia nuova, `fsck.vfat`); `make test-bm` (`bm_rewrite`, font
    largo 6), `make test-fat` (riscrittura di un file con nome lungo).
  - Corretto per strada: `save()` con tabelle oltre ~1 KiB rompeva lo stack di Lua
    ("cannot store a thread"); ora il testo si costruisce in un buffer fisso.

  **Da provare sul Pi** (Dev > Code, tastiera USB):
  - il font 6x12 sul monitor: si legge bene? (F10: 8x14 e 8x16);
  - Ctrl+O, un gioco della SD, una modifica, Ctrl+S: il gioco parte ancora con i suoi
    sprite e la sua mappa;
  - F4 due pagine (F7 passa dall'una all'altra, F2/F3 cambiano tab), fluido anche così;
  - F5 prova il gioco; con un errore torna sulla riga rossa, F9 lo spiega;
  - una riga `#entry: aggiungi operatore ternario #` dentro una funzione con un if/else,
    Invio, poi Ctrl+Z;
  - Esc > Exit con modifiche non salvate: "Keep for later" e alla riapertura ci sono.
- **22.2 Pixel art** — ✅ in QEMU (2026-10-01, branch `pixel-art`), da verificare sul Pi:
  - **bm Pixel** (`carts/pixel/main.lua`, nel kernel come gli altri strumenti): scheda
    **Dev**, monitor `5`, "Open in bm Pixel" nelle opzioni di una cartuccia
    ([sdk/README.md](../sdk/README.md#sulla-console-bm-pixel)). Tre pagine:
    - DRAW: lo sprite ingrandito (8–128 pixel), matita (tratto con spazio tenuto), gomma,
      riempimento, contagocce, linea, rettangolo e ovale (vuoti o pieni), selezione
      (copia, taglia, incolla, solleva e sposta, specchia, gira, cancella), scorrimento,
      disegno a specchio, griglia, animazione dei fotogrammi che seguono con onion skin e
      velocità, la base di uno sprite dall'assistente (F6);
    - SHEET: lo sheet intero con zoom, scelta dello sprite, copia e incolla tra sprite,
      misura dello sheet fino a 4096×4096;
    - PALETTE: fino a 256 colori, modifica RGB (con il colore RGB565 della console),
      aggiungi, togli, sposta, ordina, colori dello sheet, tavolozze dell'SDK e di bm
      Studio, sostituzione di un colore nello sprite o nello sheet.
    Menu: apri, sheet nuovo (diventa una cartuccia con un visualizzatore), salva, salva
    come, prova il gioco e torna, misura dello sheet; annulla e rifai; tastiera e gamepad.
  - **Formato e compatibilità**: lo sheet si salva come SHEET8 (≤ 256 colori) con la
    tavolozza di bm Pixel per prima, che torna uguale riaprendo; il resto del file resta
    byte per byte; i pixel non ridisegnati tengono i 24 bit che avevano (la console lavora
    in RGB565). SDK, bm Studio, studio 3D, `mkbm.py` e giochi leggono lo stesso sheet.
  - **Kernel**: `sspr(..., zoom)` (ingrandito o rimpicciolito, `g16_sspr_zoom`),
    `cart_sheet([w, h])` (misura dello sheet del progetto), `cart_write(path, {sheet =
    true, palette = ...})` (`sheet_section`, `bm_sheet8_pack` con le sequenze del
    codificatore di bm Studio, lo sheet al posto del vecchio in `bm_rewrite_with`),
    `cart_load(...).palette`.
  - **Test**: `test_bm` (packer SHEET8, sheet sostituito, zoom), bm Pixel sul PC con le API
    sostituite (`tests/studio/pixel_host.lua`, 55 controlli), i suoi file riletti da bm
    Studio (`check_pixel.js`) e dal kernel; QEMU `test_pixel` (Studio Village: solo i pixel
    disegnati cambiano nel file) e `test_pixel_big` (Titan Clash, sheet 2048×3448: si apre a
    1/4, "saving ..." mentre scrive, cambia un pixel solo).
  - Tasti come chip di bm-ui (`prompt()`, `lastinput()`), come nelle altre app di sviluppo.
  - **Da verificare sul Pi**: scheda Dev → *bm Pixel*, oppure Studio Village → X → *Open in
    bm Pixel*: disegnare con matita e linee, tavolozza (F3), sheet (F2), Ctrl+S, provare il
    gioco (F5); fluidità del disegno e dell'animazione; quanto ci mette a salvare uno sheet
    grande (Titan Clash: 13 s in QEMU).
  - Restano: tile e **mappe** (oggi nell'SDK, pagina mappa), livelli, tavolozze per
    sprite.
- **22.3 Render 3D**: mesh low-poly (vertici, estrusione, colori e UV sullo sheet),
  luci, camera, anteprima con Gouraud e texture, esportazione nella sezione MESH.
- **22.4 Musica ed effetti** — ✅ in QEMU (2026-10-01), da verificare sul Pi:
  - **Sound editor** (`carts/sound/main.lua`, nel kernel come l'SDK): scheda **Dev**,
    comando `A` del monitor, "Open in the Sound editor" nelle opzioni di una cartuccia.
    Quattro pagine:
    - SOUNDS: gli strumenti (forma d'onda, duty, volume, intonazione fine, ADSR, bend
      dell'altezza all'attacco, vibrato), con i grafici di onda, inviluppo e altezza;
    - SFX: effetti per i giochi, fino a 32 passi, velocità in ms, loop;
    - PATTERN: sequencer a 8 tracce (una per voce), fino a 64 passi, celle a pad in
      gruppi di 4 come una drum machine;
    - SONG: l'ordine dei pattern (lo stesso pattern ha lo stesso colore), tempo,
      swing, punto di loop.
  - Comandi col solo pad: A aggiunge / toglie e, tenuto, cambia la nota; Y tenuto suono
    e volume; B tenuto effetto e quantità; X cancella; START suona; SELECT + frecce
    pagine e elementi, SELECT da solo il menu. Con la tastiera: due file di tasti fanno
    da pianoforte (Z S X D C... e Q 2 W 3 E...), F1–F4 le pagine, F12 tutti i tasti.
  - File: apre un gioco e ne modifica direttamente i suoni ("Save" li riscrive nel
    gioco, il resto del file resta uguale byte per byte); pacchetti di suoni in
    `bm/sounds/`; importa da un'altra cartuccia un suono, un effetto, un pattern o una
    canzone (con i pattern e i suoni che usa, rinumerati), o tutto; esporta in un
    gioco; "Try it in the game" salva, avvia il gioco e torna. Undo, copia e incolla.
  - Formato: sezione **AUDIO** del `.bm` (tipo 6, descritta in `src/audio/player.h`);
    `scripts/bmaudio.py` la converte in JSON e ritorno; `mkbm.py --audio`; `make wav`
    suona un brano sul PC con lo stesso sintetizzatore.
  - Motore (`src/audio/player.c`): gira nell'interrupt audio ogni 64 campioni
    (1,3 ms), indipendente dal frame rate del gioco; effetti dei passi: glide, bend
    su/giù, vibrato, tremolo, accordo (arpeggio veloce), arpeggio a tempo, fade in e
    out, retrigger, ritardo, taglio; 16 accordi. Gli effetti sonori prendono una voce
    libera, preferendo quelle che la musica non usa. Senza audio HDMI il player avanza
    lo stesso, in silenzio.
  - Sintetizzatore: frazioni di hertz (registro 10), onde SINE e METAL, volume
    generale, limitatore morbido al posto del taglio, una nuova nota parte dal livello
    della voce (niente clic).
  - API: `sfx`, `music`, `sfxpos`, `tempo`, `mute`, `volume`, `hz` e nomi delle note
    (`"C4"`), `slide`, `vibrato`, `arp` (docs/API-IT.md).
  - Volume generale nelle impostazioni (Settings > Volume) e nei menu di pausa di tutti
    i giochi, salvato in `bm/config.txt`.
  - Test: `make test-audio` (sintesi e player), `make test-sound` (l'editor in un bm
    finto: il banco demo torna identico byte per byte, input casuale), in QEMU
    `test_audio_bank`, `test_volume_saved`, `test_sound_editor`.
  - WAV/MIDI, uscita stereo, editor delle forme d'onda: spostati tra gli spunti (R25,
    2026-10-05).
  - **Da verificare sul Pi** (tutto a schermo): Dev → *Audio test* (sei forme d'onda,
    accordo, glide, vibrato, arpeggio); Dev → *Sound*: parte sulla pagina SONG, START
    suona il brano DEMO (batteria, basso, arpeggio, melodia) a tempo anche mentre si
    modifica; SFX → START suona l'effetto; PATTERN → A sulle celle aggiunge note che si
    sentono; *Save* in un gioco e *Try it in the game*; Settings → Volume (un bip al
    nuovo livello); START nei giochi → PAUSED → VOLUME.
- **22.5 Import/export**:
  - PNG ↔ sheet (con riduzione a ≤256 colori), OBJ/GLB → MESH, file Lua ↔ progetto,
    WAV/MIDI dove ha senso;
  - dalla SD, e con M18 dal PC via WiFi.
  - Fatto: un'immagine diventa un modello sulla console (ritaglio, tornio, Meshy: `.glb` →
    MESH), i file dal PC arrivano via WiFi (`bm_net.py --send`). Resta in M22 il PNG dalla
    SD nello sheet (bm Pixel); WAV/MIDI vanno in R25.
- **22.6 Da 3D a sprite**: come `carts/titan/mkrobot.py` ma sul Pi. Si parte da un
  modello con scheletro e pose, si scelgono viste e dimensione; poi cel shading, contorni,
  riduzione della tavolozza, fotogrammi nello sheet con hitbox e hurtbox.
- ~~**22.7 Sprite stacking**~~: spostato tra gli spunti (R24, 2026-10-05), poi la milestone M43.

Considerazioni:
- **Contenitore unico: il `.bm` stesso** (come le cartucce PICO-8): codice, sheet,
  mappa, copertina e le nuove sezioni (MESH, SFX, MUSIC, più file Lua). Ogni strumento
  legge e riscrive solo le sue sezioni. PNG, OBJ, GLB e WAV servono solo per scambiare
  con il PC (22.5).
- **Interfaccia comune** in una libreria Lua inclusa nel kernel (`require "ui"`), non
  copiata in ogni strumento.
- **Puntatore**: la USB regge un solo dispositivo (la tastiera). Per disegnare si usa il
  **touchpad del DS4** come mouse: il report completo via Bluetooth contiene già le
  coordinate del tocco.
- **Unioni proposte**:
  - 22.3 e 22.6 condividono caricatore e renderer (sono due modalità di uno "studio
    3D"), anche se restano due voci del menu;
  - la mappa sta nella pixel art;
  - l'SDK perde le sue pagine codice/sprite quando arrivano 22.1 e 22.2, e resta hub e
    impostazioni del progetto.
- Ordine proposto: 22.0 → 22.1 → 22.2 → 22.4 → 22.5 → 22.3 → 22.6 → 22.7.

**Stato e chiusura (2026-10-05, decisione dell'utente).**
- **Fatto quando:** sul Pi un gioco fatto dall'inizio alla fine con la suite (codice, sprite,
  un modello 3D e un suono), provato e salvato.
- Fatto in QEMU e sul PC: 22.0 (l'SDK come centro della suite, `bm3d`, F12 e `keyhelp`, i
  modelli di gioco), 22.1 (bm Code), 22.2 (bm Pixel), 22.3 (bm Studio e bm Mesh sulla console,
  riduttore, modello da un'immagine), 22.4 (Sound editor), 22.6 (la pagina sprites di bm
  Animator).
- Resta nel codice: più file per progetto in bm Code (22.1), un PNG dalla SD nello sheet in
  bm Pixel (22.5). Lo sprite stacking (22.7) e WAV/MIDI con lo stereo (22.4, 22.5) sono gli
  spunti R24 e R25. Nessuno strumento della suite usa ancora il puntatore (M32).
- **Da provare sul Pi**, a schermo: l'SDK (pagina del progetto, dev kit dopo F5, un modello
  di gioco, *Back to bm SDK*); bm Code (il font 6x12, apri, modifica, Ctrl+S, F5 con un errore,
  F9, `#entry:` e Ctrl+Z); bm Pixel (dipingere lo sheet di un gioco e provarlo); bm Studio e bm
  Animator (Studio Village → *Open in bm Studio*, attrezzi, salvataggio, F5; rig, animazione,
  sprites); bm Mesh (Astro Wing → *Open in bm Mesh*); Dev → *Audio test* e il Sound editor
  (START suona il DEMO, salvataggio in un gioco, volume); Studio Village nella scheda Games.

**Chiusa il 2026-10-05** (decisione dell'utente): la suite gira sul PC e in QEMU (l'SDK al
centro, bm Code, bm Pixel, bm Studio e bm Mesh sulla console, il Sound editor, la pagina
sprites di bm Animator). Restano fuori: le prove sul Pi elencate sopra, più file per progetto
in bm Code, un PNG dalla SD in bm Pixel (22.5), lo sprite stacking e WAV/MIDI (spunti R24 e
R25), il puntatore negli strumenti (M32).

## M23 — Emulatore di cartucce `.p8` / `.p8.png` (stile PICO-8) (L/XL) — ✅ chiusa (2026-10-05)
Decisione 2026-09-29: in coda. Scritto da zero in C sul runtime di bm (non il port di
fake-08, in C++). Nessun nome, logo o font di PICO-8 (prodotto di Lexaloffle): nome e
font nostri; le cartucce del forum sono per lo più CC BY-NC-SA (uso non commerciale).

Decisione 2026-10-01: l'emulatore si chiama **nano8** ed è una cartuccia `.bm` della
scheda Games (`carts/nano8`, Lua), con la macchina in C nel kernel (`src/bm/n8*.c`, la
tabella `n8` di ogni cartuccia) e il suono nell'interrupt audio (`src/audio/n8snd.c`).
I tasti si mappano su tastiera e controller.

Stato (2026-10-01): tutto nel PC e in QEMU, **da provare sul Pi**.
1. **Caricatore** ✅ (`src/bm/n8cart.c`):
   - `.p8.png`: PNG con inflate (nostro), 32 KiB nascosti nei 2 bit bassi dei colori,
     codice in chiaro o compresso nei formati `:c:` e `pxa`;
   - `.p8` di testo, con i simboli in UTF-8 convertiti in P8SCII;
   - l'etichetta (label) fa da anteprima nella lista di nano8, titolo e autore dalle prime
     due righe di commento.
2. **Dialetto Lua** ✅ tradotto in Lua 5.4 prima di eseguirlo (`carts/nano8/src/10_xlat.lua`,
   un vero parser, i numeri di riga restano quelli della cartuccia): `+=` e tutti gli altri,
   `!=`, `if (c) x` e `while (c) x` su una riga, `?`, `\`, operatori sui bit (`& | ^^ ~ <<
   >> >>> <<> >><`), `@`/`%`/`$`, commenti `//`, i simboli come nomi (⬅️ ➡️ ⬆️ ⬇️ 🅾️ ❎, i
   fill pattern ▒ ░ …), gli escape `\^ \# \- \| \+ \*` nelle stringhe. Le funzioni di sistema
   restano raggiungibili dentro `function(_ENV)` con un oggetto come `_ENV` (newleste).
3. **Macchina** ✅ (`src/bm/n8.c`): 64 KiB con la mappa di memoria delle cartucce (schermo
   128×128 a 4 bit, stato del disegno a 0x5f00, sprite, mappa, flag, memoria alta,
   rimappatura di schermo / sprite / mappa a 0x5f54–0x5f57), convertita in RGB565 dalla
   palette dello schermo a ogni fotogramma, con le modalità di 0x5f2c e la seconda palette per
   riga. Diversamente dal piano, il framebuffer resta 640×360: lo schermo è ingrandito 2×
   (256×256, nitido) o a tutta altezza (360×360), a scelta nel menu di pausa.
4. **API grafica e input** ✅ (`src/bm/n8lua.c`): forme (anche `oval`, `rrect`), `spr`,
   `sspr`, `map`, `tline`, `fillp`, `pal` / `palt` (anche con tabelle e la palette dello
   schermo), `print` con i codici di controllo (colori, sfondo, `\^w` `\^t` e gli altri
   attributi, glifi `\^.` e `\^:`, font personalizzato a 0x5600), `peek`/`poke` a 1, 2 e
   4 byte, `memcpy`, `reload`, `cartdata`/`dget`/`dset` (salvati sulla SD), `menuitem`,
   `flip`, `load` fra cartucce della stessa cartella, `btn`/`btnp` (ripetizione come
   l'originale) per 8 giocatori, i controller di M16 ai giocatori 1–4.
   Mappatura dei tasti (nella lista e nel menu di pausa, "Controls"): tastiera 1 e 2 e
   controller, salvata in `bm/save`.
5. **Audio** ✅ da provare sul Pi (`src/audio/n8snd.c`): 4 canali, 8 forme d'onda, strumenti
   personalizzati (sfx 0–7), effetti delle note (slide, vibrato, drop, fade, arpeggi),
   loop, musica a pattern con loop e stop, dissolvenze, filtri buzz e dampen.
6. **Numeri a virgola fissa 16.16** (Lua modificato): **non fatto**. I numeri sono double di
   Lua 5.4: le costanti sono arrotondate a 16.16 come nell'originale, divisioni per zero e
   modulo non fermano il gioco, le operazioni sui bit lavorano in 16.16; resta diverso solo
   l'overflow oltre ±32767 (raro nei giochi).
- **Prove**: `make test-nano8` (caricatore, traduttore, 151 controlli di
  `tests/nano8/carts/api.p8`, le cartucce incluse giocate 20 s con i tasti premuti),
  `tests/qemu_test.py -k nano8` (la stessa cartuccia di prova sul kernel vero). Sul PC
  `build/host/n8host` gioca nano8 con screenshot, WAV e misure (`--perf`): girano senza
  errori Celeste (originale e newleste), Just One Boss, Desert Strike, Zepton, Piconian,
  Bon Planto, Sixlets II, Starmoo Valley, Momma Zilla, Boo is missing!.
- **Cartucce incluse** (`carts/nano8/roms`, licenze in `CREDITS.md`): sette di altri autori
  con licenza CC0 o MIT e `nanodemo.p8` (Comet Catcher, nostra, da `carts/nano8/mkdemo.py`).
- **Mouse e tastiera delle cartucce** (`poke(0x5f2d, 1)`): bm non ha un mouse, quindi il
  cursore (`stat(32)`, `stat(33)`) segue la levetta, la croce o le frecce, 🅾️ e ❎ sono i suoi
  tasti (`stat(34)`); i tasti premuti diventano testo per `stat(30)` / `stat(31)` (layout US).
  `load("#nome")` (le cartucce in più parti del forum) cerca `nome.p8.png` / `nome.p8` nella
  stessa cartella.
- **Cartucce pesanti**: un fotogramma che supera ~400 mila istruzioni Lua si ferma e riprende al
  fotogramma dopo (`timeslice()` del runtime): il gioco rallenta invece di fermarsi con "cart
  timeout"; sullo schermo resta l'ultimo fotogramma completo (al `flip`).
- **Limiti noti**: i kana sono un riquadro, i `.p8` con `#include` vanno esportati prima.
  Le cartucce più pesanti (pseudo-3D a 60 fps come Zepton) possono scendere sotto i 60 fps
  sul Pi Zero.
- **Fatto quando:** un gioco senza suono gira dalla SD (primo traguardo), poi con audio
  e numeri 16.16.
- **Chiusura (2026-10-05, decisione dell'utente).** I numeri 16.16 escono dal criterio: restano
  un limite noto (i double di Lua arrotondati come l'originale; diverso solo l'overflow oltre
  ±32767). Il mouse vero per le cartucce che lo chiedono (`poke(0x5f2d, 1)`, ora che c'è il
  puntatore di M32) è lo spunto R26. Si chiude con la prova sul Pi, a schermo: la lista di
  nano8 con le anteprime; Comet Catcher con pad e tastiera, con il suono; una delle cartucce
  incluse con il menu di pausa (ingrandimento 2× o a tutta altezza, *Controls*); un gioco che
  salva (`cartdata`) ritrovato dopo l'uscita; con F11 due volte il peso di un fotogramma della
  cartuccia più pesante.

**Chiusa il 2026-10-05** (decisione dell'utente): nano8 gira sul PC e in QEMU (caricatore,
dialetto, macchina, grafica, input, audio, salvataggi). Restano fuori: la prova sul Pi
descritta sopra, i numeri 16.16 (un limite noto) e il mouse delle cartucce (spunto R26).

## M24 — Scambio in rete locale tra console (M) — parte del Market (M25) — ✅ chiusa (2026-10-05)
Decisione 2026-09-30: M24 originale diviso in tre (M24 rete locale, M25 store su
GitHub, M26 market gratuito); il 2026-10-01 M25 e M26 sono diventati una sola milestone,
il **Market** (M25), e questa ne è la parte P2P. Considerazioni iniziali del 2026-09-29.
- **Cosa si scambia**: cartucce `.bm` (già un contenitore unico) e pacchetti di risorse
  (sprite, mesh, suoni: un `.bm` senza codice).
- **Pacchetti di risorse** (proposta del 2026-10-03, in [RISORSE.md](RISORSE.md)): un file
  per risorsa (contenitore `BMRES`: `.bmm` modelli, `.bmi` immagini, `.bms` suoni, `.bmt`
  mappe, `.bmc` palette, `.bmk` kit, con la sezione INFO per nome, autore, versione e
  licenza) e la scheda **Lib** del menu dopo Dev.
- **Scoperta**: le console si trovano con un annuncio UDP in broadcast sulla rete di casa.
- **Trasferimento**: via TCP (come `netxfer` di M18), tra amici sulla stessa rete;
  conferma sullo schermo di chi riceve.
- **Verifica per hash** (2026-10-01): il catalogo firmato del Market elenca lo SHA-256 di
  ogni gioco, quindi la provenienza dei byte non conta più: un gioco del catalogo
  ricevuto da una console vicina vale come scaricato da GitHub. Un gioco fuori catalogo
  (fatto da un amico) si accetta solo con conferma e resta "non verificato".
- **P2P via internet** tra console: **no** (decisione 2026-10-01). Servono traversamento
  del NAT, un server di appoggio comunque, TLS e una DHT: molto codice su un core solo che
  deve tenere i 60 fps, e l'IP di casa esposto.
- **Fatto quando:** due console sulla stessa rete si vedono nel Market e una manda un
  gioco all'altra, che lo verifica con il catalogo.

**Chiusa il 2026-10-05** con M25 (decisione dell'utente): annuncio, domanda al giocatore e
controllo con lo SHA-256 provati sul PC e in QEMU; sul Pi la console si annuncia. Resta fuori:
la prova con due console sulla stessa rete.

## M25 — Market: giochi da GitHub (L, dopo M19) — ✅ chiusa (2026-10-05)
Decisioni 2026-10-01 (prima in M25 "store" e M26 "market", ora una cosa sola):
- **Tutto gratuito**: niente account, pagamenti, commissioni né licenze da sbloccare.
- **Scheda Market**, la prima del menu: **Market | Games | Dev | Settings** (con la scheda Lib
  dal 2026-10-04: Market | Games | Dev | Lib | Settings). All'avvio il
  menu apre **Games**.
- **Repository dedicato e pubblico**, `f-accomando/bm-market`. Deve essere pubblico: il Pi
  scarica senza autenticarsi (un repository privato vorrebbe un token su ogni console,
  in chiaro nel `config.txt`) e le pull request degli autori arrivano dai fork.
  - una cartella per gioco: `games/<id>/` con il `.bm` e `info.txt` (versione, licenza
    obbligatoria, descrizione);
  - il CI del market controlla le pull request (`scripts/mkmarket.py --check`) e, a ogni
    merge, costruisce il catalogo, lo firma e lo pubblica con GitHub Pages
    (`https://f-accomando.github.io/bm-market/`);
  - moderazione: le pull request le approva il proprietario del repository.
- **Tutti i giochi del progetto sono scaricabili** dal Market. Per ora restano anche
  nell'immagine della SD (sviluppo); più avanti, forse, immagini diverse (leggera,
  sviluppo con gli strumenti, giochi).
- **Caricamento a pezzi e solo con la scheda attiva**: la scheda si apre subito con dei
  segnaposto (copertine grigie con il titolo) e le risorse arrivano una alla volta
  (catalogo, poi le copertine visibili, poi le altre); con un'altra scheda, un pannello o
  un gioco aperti il Market non scarica niente (il lavoro in corso si interrompe e
  riprende quando si torna).
- **Pubblicazione**: pull request al repository del market, dal PC e, più avanti,
  dall'SDK del Pi con un token personale (`github_token` in `bm/config.txt`).
- **Sicurezza**:
  - catalogo firmato (ECDSA P-256, come le release di M19) con una **chiave sua**
    (`keys/market-pub.pem`, `scripts/market-key.sh`), diversa da quella del kernel: chi
    avesse la chiave del market non potrebbe installare un `kernel.img`;
  - ogni file scaricato si controlla con lo SHA-256 del catalogo prima di scriverlo;
  - il Market scrive solo in `/carts` (i giochi) e in `/bm/market` (la cache);
  - le cartucce della SD scrivono solo file `.bm` in `/carts` e i loro salvataggi: prima
    `cart_save` e `cart_write` accettavano qualunque percorso (anche `/kernel.img`);
  - le cartucce Lua non hanno `io`/`os` (la rete UDP sì, dal 2026-10, per i giochi online:
    `bmnet`); il codice ARM nativo (M13)
    non ci sarà mai nel Market.
- **Licenze**: il campo licenza è obbligatorio. La BM Community License vale per bm e per
  i giochi del progetto, non per i contenuti degli utenti; attenzione a CC BY-NC-SA (uso
  non commerciale) e ai contenuti di terzi (in `carts/nano8/roms` solo cartucce
  ridistribuibili, come già oggi).

Passi:
1. ✅ (QEMU, `test_cart_write_limits`) **Scritture delle cartucce limitate**: `cart_save`,
   `cart_write` e `cart_put_audio` accettano solo file `.bm` in `/carts`, tranne per gli
   strumenti incorporati (`bm_set_tool` in `carts_tool_session`).
2. ✅ (PC, `make test-catalog`) **Catalogo**: `scripts/mkmarket.py` (controllo, firma,
   copertine PNG, `index.html`, `--add`), `scripts/market-key.sh`, `src/net/catalog.c`
   (firma, record, percorsi solo relativi, SHA-256, serial che cresce); le copertine si
   leggono con il decoder PNG del kernel.
3. ✅ (QEMU) **Rete senza bloccare il menu**: `src/kernel/fiber.c` + `src/arch/fiber.S`;
   `net_wait_step` cede il controllo dentro una fibra e restituisce -1 se annullata (DNS,
   connessione, lettura, scrittura, attesa dell'ora); il menu dà alla fibra il tempo che
   avanza in ogni frame (`menu_view_t.idle`).
4. ✅ (QEMU, `test_market`) **Scheda Market** (`src/kernel/market.c`): segnaposto subito,
   catalogo e copertine uno alla volta (prima la selezione e le vicine; cache in
   `/bm/market`), domanda, percentuale sulla copertina, controllo, installazione in
   `/carts` (`GAMES.TXT`), "Play"; badge Installed / Update / Retry; X per i dettagli
   (Download again, Delete); uscire dalla scheda interrompe il lavoro. Sorgente:
   `market_url` (HTTPS) o una cartella della SD (`sd:/market/`, usata dal test in QEMU, dove
   non c'è rete); `bm/market.pem` aggiunge una chiave.
   - Sul Pi (2026-10-05, kernel `54d60b9`): il catalogo arriva da GitHub Pages via WiFi e la
     firma torna con la chiave nel kernel ("catalog saved on the SD card", niente "no key").
   - **Da verificare sul Pi**: le copertine, un gioco scaricato che parte, il menu a 60 fps
     durante i download (l'handshake TLS è calcolo puro: forse qualche frame perso).
5. ✅ (2026-10-05) **Repository del market**: modello in `market/` (README con le regole,
   workflow che controlla le pull request e pubblica il catalogo firmato con GitHub Pages,
   descrizioni dei giochi), `make market-seed`. `f-accomando/bm-market` è in linea con i 12
   giochi del progetto: la preparazione l'ha fatta `scripts/market.sh` (`easy_install` m:
   chiave, secret `BM_MARKET_KEY`, chiave pubblica su `bm-core`, Pages dal workflow, i
   giochi, il catalogo riletto da GitHub Pages; le volte dopo solo i giochi). Due errori
   trovati al primo giro vero: Pad Typing senza licenza in `market/about.txt` e Pages
   spento letto come "costruito da un branch" (404).
   - Permessi delle cartucce (decisione dell'utente, 2026-10-05): un gioco scrive solo `.bm`
     **nuovi** in `/carts` e cambia solo quelli fatti nella stessa partita (cambiare un `.bm` che
     c'era, cioè un altro gioco, è degli strumenti di bm) e non usa i servizi con le chiavi della console (`picture3d`, la chiave Meshy):
     `test_cart_write_limits`. La rete UDP (giochi online, `bmnet`) e `report()` (manda con il
     `github_token`): la prima volta che un gioco li usa la console chiede al giocatore, sopra
     il gioco fermo nella chiamata; la risposta resta in `bm/config.txt` (`allow_<salvataggio>`,
     via la riga per essere richiesti): `test_permissions`. Nessun limite di dimensione per i `.bm`
     (decisione dell'utente): solo i 100 MiB per file di GitHub; i limiti saranno del `.b16`.
6. ✅ (PC, `make test-github`; QEMU, `test_publish`) **Pubblicazione dal Pi**: X su un
   gioco della SD, *Publish to the Market* (`src/kernel/publish.c`): cartella (del catalogo
   se il gioco c'è già, se no dal nome del file), versione (la data), licenza a scelta,
   token `github_token` in `bm/config.txt`; poi `src/net/github.c` con le API REST:
   utente del token, ramo nel market per il proprietario o fork (atteso, poi allineato) per
   gli altri, `.bm` e `info.txt` (un aggiornamento tiene il nome del `.bm` già presente),
   pull request; l'esito sulla console di testo.
   - **Da verificare sul Pi**: una pull request vera verso bm-market con un token.
7. ✅ (PC, `make test-lan`; QEMU, il pannello) **Scambio in rete locale** (M24,
   `src/net/lan.c`): con la scheda Market (o il pannello di invio) aperta la console si
   annuncia in UDP ogni 2 s (porta 3335) e ascolta in TCP (3336); X su un gioco, *Send to a
   nearby console*, la console scelta chiede al suo giocatore (60 s, poi no) e tiene il
   gioco solo con lo SHA-256 annunciato; se i byte sono un gioco del catalogo vale come
   scaricato dal market ("checked"), se no "from friends only". Una console occupata
   risponde "busy"; uscire dalla scheda chiude tutto.
   - Sul Pi (2026-10-05): con la scheda Market aperta la console si annuncia ("nearby
     consoles: listening as bm-108").
   - **Da verificare sul Pi**: due console sulla stessa rete (Pi Zero W e Pi 1 B) si
     vedono e si mandano un gioco.
8. ✅ (QEMU, `test_market_b16`, RGB30) **Il Market sulla RGB30** (richiesta dell'utente,
   2026-10-05, branch `claude/rgb30-market`): la scheda Market nel menu della RGB30, prima e
   fuori dallo schermo come sul Pi, con lo stesso `market.c` nelle fibre (le fibre a 64 bit:
   `src/rgb30/fiber.S`) e la rete locale; del catalogo solo i `.b16` (scelta dell'utente: il
   Pi mostra `.bm` e `.b16`, che girano uguali), scaricati in `bm/` e giocati da lì.
   `mkmarket.py` accetta i `.b16` (fino a 8 MiB); `test_b16_on_pi` sul Pi.
   - **Da verificare sulla RGB30**: il catalogo vero (oggi senza `.b16`: arriveranno con M42).

- **Fatto quando:** dalla scheda Market del Pi si sceglie un gioco del catalogo, si
  scarica senza che il menu si fermi, si verifica, si installa e si gioca; un gioco
  pubblicato con una pull request compare nel Market dopo il merge.

**Chiusa il 2026-10-05** (decisione dell'utente; nata nel branch `bm-store` il 2026-10-01,
unita al principale il 2026-10-04): il Market è in linea (`f-accomando/bm-market`, catalogo
firmato su GitHub Pages); sul Pi il catalogo arriva con la firma giusta e i giochi si scaricano
e si giocano; la RGB30 ha la sua scheda Market con i soli `.b16`; i giochi girano nella sandbox
(scritture limitate, niente chiavi della console, la domanda per la rete e i report). Restano
fuori: sul Pi la pubblicazione con una pull request, le copertine e i 60 fps durante i
download, lo scambio fra due console (M24); sulla RGB30 il catalogo vero con i primi `.b16`
(M42).

## M26 — unita a M25 (2026-10-01)
Il "market gratuito legato allo store" e lo store su GitHub sono la stessa cosa: vedi M25.

## M27 — BareMetal UI (menu giochi/dev) (L) — ✅ chiusa (2026-09-30: task 1–4)
Chiusa dall'autore il 2026-09-30 con i task 1–4 verificati sul Pi e le icone della barra
(parte del task 5); il resto dei task 5–9 non è stato fatto e resta qui come piano.

Decisione 2026-09-30: il menu home di M21 diventa **BareMetal UI**, l'interfaccia di
sistema della console. Oltre alla scelta dei giochi ha sottomenu per le opzioni di ogni
cartuccia, per gli strumenti di sviluppo e per le impostazioni. Tutto si raggiunge
**col solo controller**, senza tastiera né seriale.

Task:
1. **Pannelli (sottomenu)**: la base comune.
   - Un pannello sopra la griglia, con lo sfondo scurito.
   - In alto il titolo con il percorso (`Settings > Controllers`).
   - Righe con etichetta e valore, di quattro tipi: azione, sottomenu (`>`), scelta
     (`< valore >`), informazione.
   - La riga scelta è evidenziata, la lista scorre; in basso la spiegazione della riga.
   - Una pila di sottomenu: B torna indietro di un livello.
   - Comandi uguali da pad, tastiera USB, seriale e console di rete: su/giù, A o Invio,
     B o Esc, sinistra/destra per cambiare un valore.
   - Domande di conferma con il pulsante giusto ("Delete", "Restart").
2. **Opzioni della cartuccia** (X sul pad, `x` dalla tastiera):
   - gioca o riprendi, chiudi il gioco sospeso;
   - informazioni: autore, file, dimensione, tipo, salvataggio;
   - apri nell'SDK;
   - cancella il salvataggio, elimina la cartuccia dalla SD, con conferma (FAT:
     `fat_delete`, provato con `fsck.vfat`).
3. **Scheda Dev piena**: oltre all'SDK, gli strumenti del monitor come copertine, con
   icone disegnate in C: Monitor, Lua, Sistema, Registro, Test input, Test audio,
   benchmark, stress test, test DMA, demo, test pattern, diagnostica.
   - Si aprono in modalità testo; alla fine "A: torna al menu", anche dal pad.
   - Registro e test pattern si usano anche col pad.
4. **Impostazioni** (voce **Settings** dopo le schede, scritta come loro):
   - Controller: i 4 giocatori con indirizzo e stato, abbina un nuovo controller, prova
     i tasti, dimentica tutti;
   - WiFi: rete, IP, ora, console di rete (porta e PIN), connetti, connessione
     all'avvio sì/no;
   - disegno dei giochi `.bm` (diretto / via RAM), layout della tastiera (italiano / US);
   - Sistema: versione, memoria, SD, tempo di accensione; riavvia; monitor.
5. **Barra di stato e notifiche**:
   - ✅ (QEMU) icone al posto di `pads: 1 2 - -` e della scritta bm (vedi sotto);
   - ora (SNTP, fuso orario nelle impostazioni);
   - notifiche che compaiono e spariscono ("Controller 2 connected", "Cartridge
     received", "Save data deleted").
6. **Suoni e animazioni del menu**: clic di navigazione, suono di avvio, copertina che
   si ingrandisce; suoni del menu nelle impostazioni. Il volume generale c'è
   (Settings > Volume, 2026-10-01).
7. **Ordine e preferiti**: giocati di recente per primi, preferiti fissati in alto,
   ordine per titolo o autore; salvati in `/bm/menu.txt`.
8. **Tastiera sullo schermo** (col pad), per password WiFi, nomi dei file e PIN. Poi WiFi
   e abbinamento del tutto grafici (elenco delle reti con il segnale, avanzamento
   dell'abbinamento), senza passare dalla console testuale.
9. **Menu rapido nel gioco** (PS tenuto premuto), sopra il gioco congelato: riprendi,
   torna alla home, chiudi il gioco, volume, controller.
- Ordine: 1 → 2 → 3 → 4, poi 5–9.

**Stato (2026-09-30): task 1–4 fatti in QEMU** (`test_home_ui`, `test_sd_sdhc_and_usb_menu`,
`make test-fat`), da provare sul Pi.
- Pannelli in `src/kernel/menu_ui.c`: sotto un pannello sfondo e copertine sono disegnati a
  metà luminosità mentre si copiano dalla RAM, perché la memoria video (senza cache) non
  va mai riletta. Impostazioni e strumenti in `src/kernel/home.c`, opzioni della
  cartuccia in `src/kernel/carts.c`.
- Comandi: X (tasto C sulla tastiera USB, `x` dalla seriale) apre le opzioni; B (tasto X,
  Esc, `q`) torna indietro di un livello; L1 / R1 cambiano scheda (vedi "Schede e PS").
- `fat_delete`: prima la voce della directory (anche i pezzi del nome lungo), poi i
  cluster. Test sul PC: 50 file e un nome lungo cancellati, `fsck.vfat` pulito; una
  versione che lascia il nome lungo viene scoperta dal test.
- L'SDK aperto dal menu dice "opened /carts/...": `cart_arg().back` distingue il file
  aperto dal ritorno da una prova.
- Col pad: il registro scorre con su/giù (sinistra/destra: pagina) e B esce; il test
  pattern si chiude con qualsiasi tasto; dopo gli strumenti "A: torna al menu".
- Restano sulla console testuale: abbinamento (`T`), connessione WiFi (`W`, la password
  si scrive con la tastiera USB), Lua. Diventano grafici con il task 8.
- **Barra** (decisione 2026-09-30): niente scritta bm né `pads: 1 2 - -`; Settings è
  scritta come le schede, senza l'icona dell'ingranaggio.
  - A destra, un'icona bianca per ogni giocatore collegato: tastiera (tastiera USB)
    o controller in stile DS4 (pad Bluetooth o USB).
  - Sopra il centro in basso di ogni icona, il numero del giocatore (1–4, l'ordine di
    abbinamento) in un cerchio, con uno stacco intorno che lascia leggibile l'icona.
    Tastiera e controller possono avere qualsiasi numero da 1 a 4 (la tastiera è il
    primo giocatore senza pad).
  - Cerchio bianco con il numero scuro per USB (tastiera, gamepad), blu con il numero
    bianco per Bluetooth (DS4).
  - Poi l'icona WiFi (Pi Zero W) o quella Ethernet (Pi 1 B / B+ col cavo, M29), se la
    console è collegata; grigia finché il router non dà l'indirizzo.
  - Tutte nello stesso riquadro di 27×18 pixel (`src/kernel/icons.c`): forme disegnate
    all'avvio con antialiasing (4×4 campioni per pixel), bordi sui pixel interi.
  - `net_link_kind()` dice quale collegamento è attivo (unione con M29: l'Ethernet del
    LAN9512); sul Pi 1 B il pannello Settings > Network mostra il cavo invece della rete
    WiFi, e Settings > System la scheda.
  - Test: `bar_icons` conta le icone (3 pad col numero blu in `test_bt_two_pads`, la
    tastiera col numero bianco in `test_sd_sdhc_and_usb_menu`, nessuna in
    `test_home_ui`).
- **Schede e PS** (decisione 2026-09-30, dopo le prove sul Pi):
  - L1 / R1 scorrono Games, Dev e Settings (non girano in tondo); su dalla prima riga
    non porta più alla barra, che non prende più il "fuoco";
  - Settings è l'ultima scheda: arrivandoci il suo pannello si apre da solo e Dev si
    spegne; B fuori dal pannello torna alla scheda di prima;
  - sulla tastiera USB Q / E e PagSu / PagGiù (bit `HID_L1`, `HID_R1`: DS4, LB/RB
    dell'Xbox 360, tasti 5 e 6 dei gamepad generici); dalla seriale `[` `]`, Tab e
    `1` `2` `3`. Le pillole `L1` e `R1` accanto alle schede sono state tolte dopo la
    prova sul Pi (la barra resta come prima);
  - PS (e Guide dell'Xbox) nel menu non porta più al monitor: torna a Games e chiude
    pannelli e domande. Nel monitor apre il menu. Nei giochi e negli strumenti fa
    uscire come prima. `hid_quit_pressed()` distingue `HID_QUIT_KEY` (Esc,
    Start+Select) da `HID_QUIT_PS`.
  - Test: `test_menu_tabs` (DS4 simulato: R1, L1, B, su, PS nel menu e nel monitor),
    Q / E dalla tastiera in `test_sd_sdhc_and_usb_menu`.
- **Griglia** (decisione 2026-09-30): 16 pixel tra le copertine (erano 24 in orizzontale
  e 20 in verticale) e più in basso: due righe intere, poi la riga successiva che
  spunta per 12 pixel (due volte il raggio degli angoli) a dire che la lista continua.
  Il nome della copertina scelta scende di una riga, a metà tra la barra e la griglia.
  - Sul Pi, dopo essere scesi fino a Titan Clash, la prima riga (Astro Wing, Chaos
    Kitchen...) usciva dall'alto senza alcun segno e sembrava sparita. Con più di due
    righe, nel margine destro c'è ora una **barra di scorrimento**: il cursore è lungo
    quanto le due righe visibili e segue lo scorrimento (`test_menu_scroll`; nessuna
    barra con una riga sola in `test_home_ui`).

✅ Verificato sul Pi (2026-09-30, `d143ce1`): 60 fps anche con un pannello aperto; col solo
DS4 le opzioni della cartuccia, i salvataggi, l'abbinamento di un secondo DS4; gli strumenti
della scheda Dev; Settings > System > Restart; le icone della barra (DS4 blu, tastiera USB
bianca, tastiera Bluetooth blu, WiFi); la barra di scorrimento; L1 / R1 e Settings che si
apre da sola; PS nel menu e nel monitor. I task 5–9 non sono stati fatti (milestone chiusa).

Dopo la chiusura (decisione 2026-10-01): dal menu al monitor si va con **Ctrl+Esc** (o
Start+Select, `q` dalla seriale), non più con Esc: Esc da solo torna indietro come B (anche
dalla seriale e dalla console di rete). Nei giochi Esc esce come prima.

Dopo la chiusura (decisione 2026-09-30): **bm native demo** e **bm stress test** non sono più
nella scheda Games. Non vanno più sulla SD (`make sdcard` e `make image` mettono solo i giochi;
`make install` toglie `carts/demo.bm` e `carts/stress.bm` dalla scheda) e il menu non mostra più
la demo incorporata quando la SD non ha giochi (la scheda dice "nothing here yet"). Restano nel
kernel: lo stress test nella scheda Dev, la demo col comando `n` del monitor.

- **Fatto quando:** sul Pi, con il solo DS4, dal menu si raggiungono tutti i giochi,
  gli strumenti e le impostazioni (abbinare un secondo controller, collegare il WiFi,
  cambiare layout, riavviare); il menu resta a 60 fps; ogni funzione ha un test in
  QEMU.

**Icone dei tasti** (branch `bm-ui`, 2026-10-01): un set di icone piatte per i tasti del
DS4, dei pad generici e della tastiera, nello stile delle icone della barra.
- Stile scelto dall'utente fra quattro varianti (pieno, contorno, rilievo, scuro a colori):
  **rilievo**. Faccia bianca alta 14 pixel su un bordino grigio di 2 pixel, simbolo o
  scritta ritagliati nella faccia. I quattro tasti frontali del DS4 hanno anche la
  versione a colori: faccia scura con bordo e bordino grigi, simbolo nel suo colore
  (croce blu, cerchio rosso, quadrato rosa, triangolo verde).
- `src/kernel/prompts.c` (C semplice, come `icons.c`): forme con antialiasing (8×8
  campioni ai bordi), fatte al primo uso e tenute. Tutte alte 16 pixel, come una riga di
  testo.
  - DS4: croce, cerchio, quadrato, triangolo; croce direzionale (tutta o una direzione
    in bianco, le altre grigie); L1 R1, L2 R2 (più tondi in alto), L3 R3, levette L e R;
    OPTIONS, SHARE, PS, touchpad.
  - Pad generici: A B X Y, START, SELECT.
  - Tastiera: frecce, Enter, Esc, Space, Tab, Backspace, Shift, Ctrl, Alt, Del, Home,
    End, PgUp, PgDn, F1–F12 e un tasto per ogni carattere (`prompt_key`). Una lettera è
    nel font 8×16 in grassetto, le parole nel 6×12.
- **Menu**: i suggerimenti in basso mostrano i tasti dell'ultimo dispositivo premuto
  (`hid_last_source()`), prima di allora la tastiera del giocatore 1 o il DS4.
  - DS4: croce Play, quadrato Options, SHARE+OPTIONS Monitor; nei pannelli croce
    Select, croce direzionale Change, cerchio Back.
  - Tastiera: Enter, C, Ctrl+Esc (Esc da solo torna indietro); nei pannelli le frecce ← →.
  - Pad generici: A, X, SELECT+START.
  - Settings > Controllers > **Button icons**: White / Colour (`prompts=` in
    `config.txt`).
- Test: `make test-prompts` controlla ogni icona e disegna il set intero, 3x come sulla
  TV, in `build/prompts/prompts.png`; `test_hid` controlla `hid_last_source()`; in QEMU
  `prompt_spans` trova le icone dei suggerimenti dal loro bordino grigio.
- **Da provare sul Pi:** i suggerimenti col DS4 (bianchi e, da Settings > Controllers >
  Button icons, a colori), poi con la tastiera USB (Enter / C / Ctrl+Esc) premendone un
  tasto.

**Secondo set: le app di sviluppo** (2026-10-01). Prima ogni app scriveva i tasti a modo
suo: solo il Sound editor li disegnava (chip con gli angoli tagliati, colori Xbox, solo i
nomi del pad), SDK, 3D studio, bm Code e assistente li scrivevano come testo ("F1 code",
"Enter/A: insert"). Fra quattro varianti l'utente ha scelto i **chip colorati**.
- Chip piatti pieni di colore, scritta ritagliata: i tasti della tastiera nell'ambra delle
  app, i quattro tasti frontali del DS4 nei loro colori, A B X Y dei pad generici nei loro
  (verde, rosso, blu, giallo), gli altri pulsanti del pad grigio chiaro, la croce
  direzionale con le direzioni giuste chiare. Alti 16 pixel accanto al testo 8×16, 12
  accanto al 6×12 di bm Code (`prompt_chip`, `prompt_chip_key` in `prompts.c`; il set del
  menu resta identico, pixel per pixel).
- API Lua: `prompt(nome, x, y, [piccolo])` (maiuscolo i pulsanti del pad, come sul pad
  usato per ultimo; minuscolo i tasti, coi nomi di `keyp()`), `prompt(nome)` misura,
  `lastinput()` dice se l'ultimo tasto era della tastiera, di un DS4 o di un altro pad
  (`docs/API-IT.md`).
- Nelle app (non in nano8, che è un emulatore): le schede di SDK e 3D studio (F1…F4,
  Esc), le barre in basso ("hold F12 keys"), la riga dei suggerimenti del 3D studio, il
  Sound editor (coi tasti della tastiera se è quella l'ultima usata: Enter, - =, [ ],
  Backspace, Space…), il menu di bm Code con le scorciatoie e i suoi dialoghi, il pannello
  dell'assistente e la sua barra. Le scritte dopo i chip restano sulle colonne del font (i
  test in QEMU leggono lo schermo). Le liste dei tasti di F12 restano testo.
- Test: `make test-prompts` controlla anche i chip e disegna `build/prompts/chips.png`; i
  simulatori sul PC (Sound, 3D studio, pannello) hanno `prompt` e `lastinput`, e il
  pannello prova tastiera e pad.
- **Da provare sul Pi:** le schede, le barre e i suggerimenti di SDK, 3D studio, Sound
  editor, bm Code e assistente, con la tastiera e poi col DS4 (Sound, i menu e il pannello
  passano ai pulsanti del pad).

**Dopo la chiusura (2026-10-04, Pi e RGB30).** All'avvio il logo di bm (splash) mentre il kernel
scrive nella console dietro; il LED fisso se va tutto bene e un lampeggio lento per il resto
(avvio, SD, alimentazione o batteria, schermo, kernel o aggiornamento in arrivo); un kernel dalla
rete mostra un riquadro con l'avanzamento (menu e giochi) e i 3 s contati prima del riavvio, come
l'aggiornamento; Restart e Shut down sono le ultime voci di Settings (sul Pi l'arresto del
firmware); l'icona del mouse USB solo dopo che il mouse ha fatto qualcosa (il ricevitore di una
tastiera wireless ne dichiara uno anche senza mouse). Prove: QEMU `test_home_ui`, `test_update`,
`test_usb_mouse`, `make test-net` (il conto alla rovescia). **Da provare sul Pi e sulla RGB30**:
lo splash, il LED, un kernel mandato con `bm_net.py --kernel`, Shut down.
Poi lo **splash di caricamento delle applicazioni** (al posto del log, per ogni gioco e strumento
aperto dal menu: il "bm" in pixel art che cade e atterra con un jingle, poi un circolino che gira,
niente titolo, finché l'applicazione non è caricata; `game_intro=0` lo spegne); prova `make
test-loading`; da sentire sul Pi: il jingle.
Poi il **lavoro del menu in background** (fibre di M25 per tutte le schede): le copertine di Games
e Dev arrivano dopo l'apertura del menu leggendo solo l'inizio dei file, la scheda Lib legge
elenco, file e anteprime a fette (prima apriva tutti i `.bm` per intero di colpo); il CRC è a
tabella, circa 5 volte più veloce (anche il caricamento dei giochi). In QEMU con tutti i giochi
sulla SD il menu aspetta al più 3 ms; prove `make test-fat`, `make test-bm` (`bm_cover_peek`), QEMU
`test_lib_tab`, `test_menu_tabs`. **Da provare sul Pi**: l'apertura della scheda Lib, le copertine.

## M28 — Tastiera Bluetooth LE (M) — ✅ verificata sul Pi (2026-09-30)
Richiesta 2026-09-30: una Logitech **MX Keys S** (con tastierino). È Bluetooth **Low
Energy** (HID over GATT), non Bluetooth classico come il DS4: serve una parte nuova
dello stack.

Fatto (QEMU, `test_bt_keyboard` con una MX Keys simulata; `make test-smp`):
- `src/bt/ble.c`: scansione LE, connessione come central, frammentazione e ricomposizione
  L2CAP sui buffer LE del chip (27 byte), segnalazione LE (aggiornamento dei parametri
  di connessione accettato), server ATT vuoto che risponde "non trovato".
- **Pairing SMP** come iniziatore, con codice: bm mostra 6 cifre, si digitano sulla
  tastiera seguite da Invio (Passkey Entry). **LE Secure Connections** (P-256 con
  mbedTLS, f4/f5/f6 su AES-CMAC) se la tastiera lo supporta, altrimenti LE legacy
  (c1/s1); Just Works se la tastiera non può digitare. `src/bt/smp_crypto.c`, provato con
  i dati d'esempio della specifica (Vol 3 Part H, appendice D) e RFC 4493.
- Legame salvato in `bm/config.txt`: `bt_kbd` (indirizzo d'identità, tipo, IRK) e
  `bt_kbd_key` (LTK, EDIV, Rand). **Riconnessione**: finché la tastiera manca, scansione
  passiva; quando si fa vedere (basta un tasto), anche da un indirizzo privato risolto
  con l'IRK, bm si collega e cifra con la LTK salvata.
- **GATT/HOGP**: servizio HID, caratteristiche e descrittori, Report Reference, report
  map letta a pezzi (MTU 23); dalla report map si ricava dove sono modificatori e tasti
  del report tastiera (array o bitmap), notifiche attivate solo su quello; se manca, il
  report di boot (Protocol Mode = boot).
- I tasti vanno nello stesso livello della tastiera USB (layout italiano/US, ripetizione,
  Esc, tasti come pulsanti nei giochi). Tastierino: cifre, `/ * - + .`, Invio; 8/4/6/2
  come frecce nei giochi.
- Menu: Settings > Controllers mostra la tastiera Bluetooth e ha **Pair a keyboard** (si
  abbina col solo DS4); monitor: `K` fa lo stesso; `P` / Forget dimenticano pad **e**
  tastiera.
- Driver RNG: tolto l'accesso al registro di maschera (0x10), inutile col polling e
  assente in QEMU.
- Barra del menu (2026-09-30, dalla prova sul Pi con DS4 e MX): la tastiera Bluetooth non
  aveva un'icona; quella che si vedeva, "2" bianco, era la tastiera USB. Ora la tastiera
  Bluetooth ha la sua icona col numero **blu**, sul giocatore per cui scrive (il primo
  senza pad, come la tastiera USB, che resta accanto col numero bianco); conta anche per
  `players()`. Settings > Controllers dice cosa gioca lì ("Bluetooth keyboard", "USB
  keyboard", "USB + Bluetooth keyboard", "USB gamepad"). Test: `test_bt_keyboard`.
- **Un giocatore a sé** (decisione 2026-09-30, utente): la tastiera USB (o il gamepad USB)
  resta il primo giocatore senza pad, la tastiera Bluetooth prende il successivo senza pad
  (il primo, se su USB non c'è niente); con tutti i posti presi gioca con la USB. Senza pad:
  USB 1, MX 2; con un DS4: DS4 1, USB 2, MX 3. In `hid.c` le due tastiere hanno ognuna i
  propri tasti (`hid_players(..., local, ble)`); il testo lo scrivono entrambe, come prima.
  `input_ble_player()` decide il posto; icona, Settings > Controllers ("Bluetooth
  keyboard" sulla sua riga) e `players()` lo seguono. Test: `make test-usb` (i tasti
  della tastiera LE al suo giocatore), `test_bt_keyboard` (USB 1 bianca, MX 2 blu; in
  un gioco ognuna muove il proprio giocatore), `test_bt_keyboard_legacy` (la MX da sola
  è il giocatore 1).

✅ Verificato sul Pi (2026-09-30): abbinamento della MX Keys S (un tasto Easy-Switch tenuto
premuto 3 s, poi Settings > Controllers > Pair a keyboard), digitazione, ritorno dopo lo
spegnimento o il cambio di canale, insieme a un DS4 e a una tastiera USB; l'icona blu
nella barra.

**Fatto quando:** la MX Keys S scrive nel monitor e nell'editor e si ricollega da sola.

## M29 — Pi 1 B: hub USB ed Ethernet (L/XL) — ✅ verificata sul Pi 1 B (2026-09-30)
Nata come "M25" sul ramo `claude/charming-heisenberg-7e1xjh`; rinumerata M29 all'unione
con il ramo principale, dove M25 era già lo Store su GitHub (seguito da M26–M28).
Decisione 2026-09-30: **un solo `kernel.img`** per tutte le schede BCM2835 (Pi Zero / Zero W,
Pi 1 A, B, A+, B+), che riconosce la scheda all'avvio; due immagini SD (`make image` per il
Zero W, `make image-pi1` senza il firmware del chip WiFi/Bluetooth). Il firmware di avvio
(`bootcode.bin`, `start.elf`, `fixup.dat`) è lo stesso per tutte; gli aggiornamenti di bm
(M18 `--kernel`, M19 da GitHub) restano un solo file per entrambe le schede.
- Sul Pi 1 B le due porte USB e l'Ethernet stanno dietro il **LAN9512** (hub USB 2.0
  high speed + Ethernet 10/100; B+: LAN9514 con 4 porte): servono l'hub, le *split
  transactions* (tastiere e pad low/full speed dietro un hub high speed) e un driver
  Ethernet. Rispetto a M7b cambia solo che l'hub è supportato (anche sul Zero, con un hub
  OTG); resta **un solo dispositivo HID** in uso (la tastiera se c'è, altrimenti un pad).
- Il Pi 1 ha un ARM a 700 MHz (il Zero a 1 GHz): i giochi più pesanti possono scendere
  sotto i 60 fps.
- **Fatto quando:** sul Pi 1 B con il cavo di rete il Pi prende un IP dal router, dal PC
  si apre la console di rete e si manda una cartuccia; una tastiera USB funziona nel menu.

**Passi** (2026-09-30):
1. ✅ (QEMU `raspi1ap`, test sul PC) **Scheda**: `src/drivers/board.c` decodifica il codice
   di revisione (vecchio e nuovo formato). LED ACT: GPIO 16 attivo basso sul Pi 1 A/B,
   GPIO 47 attivo alto su A+/B+, GPIO 47 attivo basso sul Zero. Sul Pi 1 WiFi e Bluetooth
   non partono (`W` e `T` lo dicono; il Bluetooth sposterebbe la console seriale).
   Il banner mostra la scheda: `bm kernel ... - Raspberry Pi 1 B rev 2.0 (BCM2835)`.
   Test: `make test-usb` (codici di revisione), `test_pi1_board` in QEMU.
2. ✅ (QEMU, hub full speed) **Hub USB** (`src/usb/usb.c`): se sulla porta radice c'è un
   hub, si accendono le sue porte e ogni dispositivo collegato viene resettato ed
   enumerato (indirizzi 2, 3, ...). Righe `usb: hub ...`, `usb: port N: ...`; il
   dispositivo in uso dice `(hub port N)`. Test: `test_usb_hub` (tastiera e tablet dietro
   un hub).
3. ✅ (Pi 1 B) **Split transactions** (`src/usb/dwc2.c`): un pacchetto alla
   volta, start split e complete split; per gli endpoint interrupt si seguono i
   microframe (start split mai nel 6, complete split da Y+2, come USPi e Circle). La riga
   dice `(hub port N, split)`. QEMU non ha un hub high speed: si prova sul Pi 1 B, oppure
   sul Zero W con un hub USB 2.0 tra l'adattatore OTG e la tastiera.
   Anche: un trasferimento interrotto a metà (NYET, errore) riprende dal pacchetto dove
   si era fermato invece di ricominciare.
4. ✅ (Pi 1 B; test sul PC) **Ethernet** (`src/usb/smsc95xx.c`, come il driver
   smsc95xx di Linux): reset, indirizzo MAC dal firmware (`b8:27:eb:...`, la scheda non ha
   EEPROM), un frame per trasferimento, lettura senza attese (con niente da ricevere il
   chip risponde con un pacchetto vuoto), PHY in autonegoziazione, duplex del MAC come
   quello del link. Monitor `E`: link, contatori, registri del chip.
   Test: `make test-usb` (chip simulato: registri, PHY, frame).
5. ✅ (Pi 1 B; test sul PC) **Rete sull'Ethernet**: `src/net/net.c` usa un
   "percorso dati" (WiFi `wl` o Ethernet `en`). Se all'avvio c'è il LAN951x la rete
   parte da sola; DHCP aspetta il link del cavo; togliendo e rimettendo il cavo l'indirizzo
   resta. Console di rete, invio di file e kernel, HTTP (M18, M19) sono gli stessi.
   Test: `make test-net` (lwIP + driver + chip simulato + un router finto: DHCP, ARP, ping).
6. ✅ **Immagine SD per il Pi 1**: `make firmware && make image-pi1` → `dist/bm-pi1.img`.

✅ Verificato sul Pi 1 B (2026-09-30), tutto sullo schermo: riga `usb: port 1: Ethernet 0424:ec00 ...`
con il MAC; `eth: link up, 100 Mbit/s full duplex` dopo qualche secondo col cavo;
`net: IP ...`; `ping` dal PC; `tools/bm_net.py IP`; una tastiera su una porta USB
(`usb: keyboard ... (hub port 2, split)`) che scrive nel menu e nei giochi.

## M30 — Assistente AI per lo sviluppo (L) — ✅ chiusa (2026-10-05)
Decisione 2026-10-01 (dopo [AI.md](../AI.md)): la prima AI di bm è un **assistente per
gli strumenti di sviluppo**, non per i giochi: mentre si scrive un gioco si richiama con
un tasto per chiedere come si scrive qualcosa, o per avere la base di uno sprite. Si
richiama solo direttamente, non è invasivo e non pesa sul sistema: nessun processo in
background, niente RAM finché non si chiede, una risposta in meno di un millisecondo.

Cosa è, e cosa no: sul Pi Zero un modello linguistico che scrive codice libero darebbe
pochi token al secondo e testo inaffidabile. L'assistente è una **rete INT8 minuscola**
che capisce la domanda (italiano o inglese, errori di battitura, sinonimi) e sceglie tra
le voci di una base di conoscenza di bm; gli sprite vengono da **ricette procedurali**
guidate dalle parole. È l'approccio ibrido di AI.md: la rete sceglie, l'algoritmo
classico fa il grosso.

Task:
1. ✅ **Motore INT8** (`src/ai/nn.c`): parole e tris di lettere (hash FNV-1a in 4096
   caratteristiche) → 64 neuroni (ReLU) → un'uscita per voce. Somme intere a 32 bit,
   SIMD dell'ARMv6 (SXTB16, SADD16, SMLAD). Bit per bit uguale al riferimento in Python
   (`scripts/assistlib.py`).
2. ✅ **Testo** (`src/ai/text.c`): lettere accentate di CP437 (tastiera) e UTF-8 piegate,
   parole funzione tolte, parole, coppie di parole e tris di lettere.
3. ✅ **Base di conoscenza** (`src/ai/kb/*.txt`, formato in `src/ai/kb/README.md`): 235
   voci. Ogni funzione delle API (anche il banco di suoni del Sound editor: `sfx`,
   `music`, `tempo`, `slide`, `arp`...), 79 esempi con il codice (movimento, salti,
   collisioni, spari, nemici, mappe, menu, suono, salvataggi, 3D, luci, Lua), 23 errori di
   Lua con la correzione, consigli, le azioni sul codice, le ricette degli sprite. Testo
   in italiano, codice ASCII che compila con Lua 5.4 (controllato).
4. ✅ **Addestramento sul PC** (`make ai-model`, numpy, ~40 s): esempi dalle domande `ask:`
   delle voci con varianti (parola tolta, errore di battitura, sinonimo), poi
   quantizzazione a 8 bit. Il risultato (`src/ai/assist.weights`) è nel repository: `make`
   non ha bisogno di numpy. `scripts/mkassist.py` impacchetta voci e rete nel file BMAI
   incluso nel kernel.
5. ✅ **Ricette di sprite** (`src/ai/sprite.c`): 38 (astronave, alieno, mostro, robot,
   personaggio di fronte e di profilo, automobile, slime, fantasma, pipistrello, teschio, moneta, gemma, cuore, stella,
   chiave, spada, pozione, forziere, cassa, albero, fiore, fungo, roccia, bomba, fiamma,
   proiettile, esplosione, mela, nuvola; tile di erba, mattoni, pietra, acqua, legno,
   sabbia, lava, terreno). 8x8, 16x16 o 32x32; forme, simmetria, cinque toni per colore
   con la luce dall'alto a sinistra, contorno; colori e misura dalle parole ("slime rosso
   32x32"); ogni seme una variante (alcune fanno animazioni: moneta che gira, ali,
   fiamma, passi del personaggio di profilo).
6. ✅ **Lua**: `ai.ask`, `ai.entry`, `ai.list`, `ai.near`, `ai.sprite`, `ai.recipes`
   (docs/API-IT.md) e `require "assist"`, il **pannello** che ogni strumento apre con un tasto:
   risponde mentre scrivi, Invio inserisce il codice o lo sprite, modalità errore (riga,
   nome scritto male, cosa vuol dire), voci collegate, "non sono sicuro"; col pad si
   sfoglia tutto. `keyp()` conosce F6–F12.
7. ✅ **Assistant** nella scheda Dev (e `I` dal monitor): il pannello da solo, il codice
   inserito e gli sprite nello sheet su due riquadri, F8 misura la velocità sullo schermo.
8. **Integrazione**: ✅ in **bm Code** (22.1): F6 con la parola sotto il cursore e il codice
   inserito al cursore, F9 sulla riga dell'errore, e le righe `#entry: ... #` (sotto).
   Restano, quando le app saranno pronte (l'editor di M15, poi gli strumenti di M22):
   - editor del codice: F6 apre il pannello con la parola sotto il cursore, Invio inserisce
     il codice al cursore; la riga rossa dell'errore apre il pannello in modalità errore;
   - editor degli sprite: F6 (modo sprite) mette la base nella cella scelta, nella misura
     della pagina (8x8 o 16x16) e con la tavolozza dell'editor;
   - una combinazione per il pad (proposta: Y+X), e l'aiuto F12 che la ricorda.
9. ✅ **Azioni sul codice** (API di prova, `assist.act`): una riga `#entry: richiesta #` in bm
   Code, Invio, e l'assistente lo fa sulla funzione intorno o sotto: operatore ternario,
   commento, log, togli i log, controllo dei nil, rendi locale, indentazione, rinomina,
   commenta/scommenta, ottimizza (API come locali), spiega; oppure inserisce un esempio
   ("crea uno snippet per un effetto di particelle") o uno sprite scritto come codice
   ("crea uno sprite slime rosso"). Non cambia niente se non è sicura; Ctrl+Z annulla.
10. ✅ **Completamento delle parole** (2026-10-02, guida e numeri in
   [PREDICT.md](PREDICT.md)): in bm Code, mentre si scrive una parola, il resto della più
   probabile in blu-grigio dopo il cursore; **Tab** la scrive e resta verde fino al tasto
   dopo. Un n-gramma (`require "predict"`, dizionari `require "words"` da
   `scripts/mkwords.py`) che segue il punto del cursore: nel codice Lua, le API della base
   di conoscenza e i nomi della scheda (`f` → `function`, `if bt` → `btnp`); dopo `--` e
   nelle stringhe l'italiano o l'inglese (menu); nelle righe `#entry:` e nella domanda
   del pannello le domande all'assistente; in Trova e Sostituisci i nomi del codice. Su
   100 caratteri italiani 83 tasti invece di 100, sul Lua il 19% in meno, sulle domande
   mai viste all'assistente il 26%. Con il conteggio delle sillabe dell'italiano
   (`make syllables`).
11. ✅ **Ricette 3D per bm Studio e bm Animator** (2026-10-02, `src/ai/mesh.c`,
   `mesh_chars.c`, `kb/meshes.txt`): 53 ricette low-poly nello stile dei blocchi della
   console, scelte dalla rete dalle parole della richiesta ("una casa rossa grande",
   "mech", "drago verde che vola"): forme semplici (cubo, sfera, cilindro, cono, piramide,
   colonna, muro, scala, rampa, piattaforma, arco), oggetti (albero, pino, casa, torre,
   sedia, tavolo, barile, cassa, forziere, spada, scudo, auto, lampione, staccionata,
   roccia, cespuglio, fungo, cactus, ponte, pozzo, cartello, torcia, barca, cannone,
   letto, fiore, moneta, gemma) e personaggi con **scheletro e animazioni** pronte per
   bm Animator (persona, cavaliere, robot, mech, cane, cavallo, uccello, pesce, slime,
   ragno, drago, fantasma, scheletro, pupazzo di neve: idle, walk, wave, attack, fly,
   swim, bounce, fire...). Primitivi (scatola, tubo, sfera, prisma, specchio), materiali
   con il tono dall'alto in basso, colori, misura, proporzioni e "senza scheletro" dalle
   parole, un seme per variante. `ai.mesh()` e il modo `mesh` del pannello (il modello
   gira nel pannello, Invio lo consegna); in bm Studio e bm Animator **F6** (pad Y + X):
   il modello entra nel progetto (nel modello vuoto o in uno nuovo), con scheletro e
   animazioni, e la prima pagina lo mostra. Il benchmark di qualità è stato il mech di
   D.Va (Overwatch): cabina col pilota, gambe piegate, cannoni a tre canne, alette e
   propulsori. Sul PC `build/host/meshview` disegna tutte le ricette (`sheet`) o una
   da quattro lati e nelle pose delle animazioni (`one`): le immagini vanno guardate.
   Costo: ~56 KB di codice nel kernel, niente RAM finché non si chiede (il modello di
   2048 facce vive solo mentre `ai.mesh` costruisce le tabelle), 0,03 ms sul PC per il
   mech. Test: `make test-ai` (ogni ricetta: facce valide, a terra, intorno all'origine,
   ossa e clip, variante, stesso seme stesso modello, parole), il pannello sul PC,
   `tools3d_host.lua` (F6 nel Studio con un `ai` finto), `test_studio_assistant` in QEMU.
   Dopo: texture dallo sheet, ricette composte ("un villaggio"), più varianti per ricetta.
12. ✅ **img2mesh: un'immagine diventa un modello** (2026-10-03, `tools/img2mesh.py`,
   `src/ai/mesh_script.c`). Richiesta dell'utente: riprodurre come strumento quello che ha
   fatto il mech dall'immagine di D.Va, cioè un modello con la visione che scompone la
   figura in parti e scrive la ricetta. Sul Pi non può girare: lo strumento chiama Claude
   attraverso l'API Anthropic (`claude-opus-5-5`, pensiero adattivo, `fallbacks:
   "default"`) e lascia al nostro motore la parte deterministica. Il **linguaggio delle
   parti** (`mesh_script.c`: `mat`, `box`, `bx`, `tube`, `cyl`, `ell`, `prism`, `wedge`,
   `tf`, `bone`, `use`, `side`/`mirror`, `clip`, `key`, `turn`, `shift`, una riga ciascuno,
   errori con il numero di riga) è interpretato nel kernel (`ai.script`) e sul PC
   (`meshview script`/`json`); il mech della ricetta scritto nel linguaggio
   (`tests/ai/img2mesh/mech.txt`) dà lo stesso modello ed è l'esempio nel prompt. Il tool:
   immagine e descrizione → script → `meshview` lo costruisce e disegna da quattro lati e
   nelle pose → i render tornano al modello che corregge (due giri, di più con `--rounds`;
   uno script che non compila torna con l'errore) → `.bm` nuovo con il viewer di bm Studio,
   o il modello aggiunto/sostituito in una cartuccia esistente (`bmmesh.encode_faces`,
   `encode_anim`, `decode_anim`). `--record`/`--replay` salvano e rileggono le risposte:
   `make test-img2mesh` e `test_img2mesh` in QEMU girano senza API. Costo: una manciata di
   centesimi per giro; serve una chiave API (`ANTHROPIC_API_KEY`) o `ant auth login`.
   Non provato con l'API vera in questa sessione (nessuna chiave): la parte di rete segue
   l'SDK alla lettera, da verificare al primo uso. Dopo: la stessa cosa dalla console via
   WiFi (bm Studio, F6 "da immagine": un PNG dalla SD, la chiave nelle impostazioni, una
   passata sola).
13. ✅ **meshy2mesh: Meshy (image-to-3D neurale) → console** (2026-10-03,
   `tools/meshy2mesh.py`). L'utente ha una chiave di meshy.ai: il tool manda l'immagine
   all'API image-to-3D (`target_polycount`, `should_texture`), aspetta il task, scarica il
   `.glb` e lo porta nel formato della console: nodi e trasformazioni del glTF, z
   capovolta e facce girate (la convenzione di `r3d.c`), a terra, centrato, alto
   `--height` blocchi; **texture** nello sheet 256×256 della cartuccia nuova (facce
   texturate con le UV in pixel) o **colori piatti** campionati dalla texture (o dai colori
   dei vertici) quando entra in una cartuccia esistente o con `--flat`; sopra `--max-tris`
   i vertici si fondono su una griglia (`--grid`): il modello diventa a blocchi, come gli
   altri. `--glb` rifà la conversione da un `.glb` già scaricato. Test:
   `tests/ai/check_meshy.py` (un `.glb` fatto dal test: scatola texturata, piramide
   colorata, indici 16 e 32 bit, trasformazione di nodo) in `make test-img2mesh`,
   `test_meshy2mesh` in QEMU (la cartuccia nel viewer e in bm Studio). Non provato con
   l'API vera: la rete di questo ambiente nega `api.meshy.ai`; i campi dell'API
   (`image_url` data URI, `ai_model`, `topology`, `target_polycount`, `should_texture`,
   `model_urls.glb`, `status`) sono quelli della documentazione v1 e vanno verificati al
   primo uso. Le texture JPEG nel `.glb` chiedono Pillow (`pip install pillow`); i PNG no.
14. ✅ **Riduttore di poligoni** (2026-10-03, `src/bm/decimate.c`, niente AI): collasso
   degli spigoli con le quadriche di Garland-Heckbert, in C portabile (kernel e PC). Mezzo
   spigolo: i vertici restano quelli del modello, così ogni vertice tiene il suo osso (lo
   scheletro segue) e le facce tengono i loro angoli di texture; l'angolo che si sposta
   prende quello delle facce che spariscono (texture continua) o la texture della faccia
   stirata. Bordi aperti, linee di colore e cuciture della texture sono tenuti con un piano
   attraverso lo spigolo (peso 10 i bordi, 1 le linee); una faccia non si rovescia mai
   (prova delle normali) e uno spigolo tra due ossa va per ultimo. Il Beast di Meshy da
   2540 a 1200 e a 500 triangoli resta riconoscibile con la texture. Console:
   `mesh_reduce(record, triangoli, ossa)` in `runtime.c`, `T.reduce_model` in `bm3d.lua`,
   tasto `-` nella pagina models di bm Studio (chiede quanti triangoli, la metà per default;
   Ctrl+Z annulla). PC: `tools/bmreduce.py CART.bm --faces 1200` (ctypes su
   `build/host/libbmdecimate.so`, `scripts/bmdecimate.py`) e meshy2mesh sopra `--max-tris`
   (la griglia resta solo oltre i limiti del formato). Test: `tests/bm/test_decimate.c`
   (piani, due colori, texture, cucitura, sfera chiusa, record, i modelli di village e
   kitchen dimezzati) in `make test-bm`, lo stand-in in `tools3d_host.lua`,
   `test_mesh_reduce` in QEMU.
15. ✅ **Un modello da un'immagine sulla console** (2026-10-03): bm Studio, pagina models,
   `m` (o "Model from picture..." nel menu): l'immagine di `pics/` sulla SD va a un servizio
   image-to-3D, la riga di stato segue il lavoro (uno sguardo ogni 5 s, Esc lo abbandona), il
   `.glb` torna come modello: texture sullo sheet se è vuoto (altrimenti colori piatti presi
   dalla texture), 2 blocchi di altezza, 1200 triangoli col riduttore. Non è legato a Meshy:
   i servizi sono una tabella in `src/net/img3d.c` (nome, indirizzo, nome della chiave in
   `bm/config.txt`, le tre chiamate); Meshy è il primo. Installazione: WiFi, `bm/ca.pem`,
   una riga `meshy_key=...` in `bm/config.txt`, le immagini in `pics/`. Nel kernel sono
   arrivati un lettore `.glb` (`src/bm/glb.c`, lo stesso lavoro di meshy2mesh), un parser
   JSON (`json.c`), un decodificatore JPEG baseline (`jpeg.c`, le texture di Meshy sono
   JPEG 2048×2048, 2,7 MB: decodificate in C e ridotte a 256×256) e il PNG di nano8 in
   `png.c`. Il kernel cresce di ~20 KB. Test: `make test-img3d` (servizio finto),
   `tests/bm/run_glb_test.py`, lo stand-in nel test di bm Studio, `test_picture_model` in
   QEMU. Non provato con l'API vera dalla console (questo ambiente non ha la rete): la
   stessa richiesta è quella di meshy2mesh, che ha fatto i modelli di `meshy-out`.
16. ✅ **local2mesh: modelli aperti in locale, senza chiave** (2026-10-03,
   `tools/local2mesh.py`): TripoSR o Hunyuan3D 2 installati da lui in `~/.bm/local3d` (git
   clone, venv, PyTorch per CUDA se c'è `nvidia-smi`, altrimenti CPU), il `.glb` che
   producono passa per la conversione di meshy2mesh (texture, riduttore); `--backend
   command` per qualunque strumento. I backend veri si provano dal PC dell'utente (qui
   niente GPU né huggingface): `tests/ai/check_local2mesh.py` prova lo script con un
   comando finto. Le opzioni di `run.py` di TripoSR (`--bake-texture`, `mesh.glb` in
   `0/`) e l'API di Hunyuan3D (`Hunyuan3DDiTFlowMatchingPipeline`, `Hunyuan3DPaintPipeline`)
   sono quelle dei repository alla data: da verificare al primo uso.
17. ✅ **Un metodo nostro: dal contorno dell'immagine** (2026-10-03, `src/bm/cutout.c`,
   niente rete né AI, gira sul Pi in meno di un secondo): lo sfondo (trasparente, o il colore
   degli angoli) va via, il contorno diventa un poligono (spigoli delle celle di una griglia
   di 96, i frammenti piccoli scartati, Douglas-Peucker) e il poligono un solido chiuso:
   ritaglio con spessore (ear clipping davanti e dietro, i lati lungo il contorno; immagine
   davanti, specchiata dietro, i colori del bordo sui lati) o tornio (il mezzo contorno
   girato intorno all'asse in 12 passi, l'immagine proiettata davanti). Onesto sui limiti:
   non inventa quello che non si vede, i buchi del contorno si riempiono. bm Studio, `m`:
   la scelta del modo (cutout, lathe, meshy.ai) e poi l'immagine; `cutout3d` in Lua; sul PC
   `tools/cutout2mesh.py` (ctypes su `libbmcutout.so`). Test: `tests/bm/run_cutout_test.py`
   (lecca-lecca: 48 triangoli chiusi, 2 blocchi, l'immagine davanti; tornio di 120), lo
   stand-in nel test di bm Studio, `test_picture_model` in QEMU (il disco rosso diventa il
   modello hero sul kernel ARM, salvato nel file).
18. ✅ **Scrittura col pad** (2026-10-05, branch `assistive-typing`, guida in
   [PADTYPE.md](PADTYPE.md); riprende `archive/pad-typing` con le regole nuove
   dell'utente): `require "padtype"`. Composizione rapida: la croce (solo quattro
   direzioni) scrive le consonanti, la predizione finisce la sillaba, □ e △ la girano;
   una pressione aspetta la sua doppia (↑ t, ↑↑ d), L2 / R2 / L2+R2 altri livelli (lettere,
   parole, numeri e codice); ✕ spazio, ✕✕ punto, ○ cancella, L1 / R1 si muovono, L1 + R1
   a capo. Share passa alla tastiera su schermo. Overlay del controller con le sillabe delle
   frecce e le tre parole di R2. In bm Code (Share) e nella cartuccia **Pad Typing**
   (esercizio e misura in italiano, inglese, Lua). 1,09 pressioni a carattere in italiano,
   1,06 in inglese, 1,33 in Lua (tastiera su schermo 3,7–4,0). Test: `make test-padtype`,
   `test_pad_typing` in QEMU. Da provare sul Pi con un DS4 (l'attesa per la doppia).
19. Dopo: numeri e nomi della domanda dentro il codice proposto ("muovi a velocità 3"),
   le domande senza risposta giusta che diventano voci nuove, ricette di sprite animate
   (più fotogrammi nello sheet), le parole nuove dell'utente nel dizionario del
   completamento.

Numeri: in QEMU 0,45 ms per domanda (sul PC 0,03 ms) e 1 ms per uno sprite 16x16; il
kernel cresce di ~410 KB (rete 270 KB, voci e testi 80 KB). RAM: niente finché non si
chiede (rete e voci restano nel kernel), poi le tabelle Lua della risposta. Con 2048 o 1024
caratteristiche invece di 4096 la rete sarebbe più piccola di 130-200 KB ma, su tre
addestramenti, perde in media 2 domande di prova su 133: restano 4096.

Test: `make test-ai` (rete C contro Python bit per bit; 154 domande mai viste in
addestramento: la risposta giusta prima per 141, nelle prime tre per 149; i 176 esempi
che compilano; 23 controlli del pannello sul PC: domanda, Invio, varianti, errore, pad,
320x180; 24 delle azioni `#entry:`; le 38 ricette in tre misure), `test_assistant` in QEMU (la rete sull'ARM,
con le SIMD, dà gli stessi interi di Python: checksum di `ai.checksum`; domanda scritta,
codice inserito, sprite nello sheet, test di velocità, F9), `make ai-model` per riaddestrare.

**Da provare sul Pi** (Dev > Assistant, o `I` dal monitor):
- con la tastiera USB: una domanda ("come salto", "collisione tra rettangoli",
  "attempt to call a nil value"); la risposta e il tempo in ms in alto a destra; Invio
  inserisce il codice nel riquadro "code";
- F7, "slime rosso" (o "astronave blu 32x32"), destra/sinistra per le varianti, Invio:
  lo sprite nel riquadro "sprites";
- F8: domande al secondo e tempo di uno sprite sul Pi vero; ✅ sul Pi (2026-10-05): la risposta
  e lo sprite arrivano subito ("immediato", riferisce l'utente);
- F9: un errore d'esempio ("attempt to call a nil value (global 'sprr')"): la riga, "did
  you mean spr?" e la spiegazione, come farà l'editor con la riga rossa;
- col solo DS4: A apre, su/giù sfogliano, A inserisce, X cambia modo, B chiude.

- **Fatto quando:** sul Pi l'editor del codice e quello degli sprite aprono l'assistente
  con F6 (e col pad), il codice entra al cursore e lo sprite nella cella; il menu e
  l'editor restano a 60 fps; i test in QEMU coprono l'integrazione.

**Chiusa il 2026-10-05** (decisione dell'utente): l'assistente è nelle app della suite (bm
Code, SDK, bm Studio, bm Animator: F6), con le ricette 3D, img2mesh, meshy2mesh, il riduttore,
i modelli da un'immagine e dal contorno, il completamento delle parole e la scrittura col pad;
sul Pi F8 risponde subito. Restano fuori: le prove sul Pi elencate sopra (F6 nelle app e col
pad, la scrittura col pad con un DS4).

## M31 — Raspberry Pi Zero 2 W (L) — ✅ chiusa (2026-10-05)
Richiesta 2026-10-02: una versione di bm per il **Pi Zero 2 W**. Il Zero 2 W ha un altro
SoC, il BCM2710A1 (RP3A0: quattro Cortex-A53, le periferiche del BCM2835 a un altro
indirizzo), quindi non può avviare `kernel.img` (ARMv6).

Decisioni:
- **Un secondo kernel, `kernel7.img`**: gli stessi sorgenti compilati per ARMv7 a 32 bit
  (`-march=armv7ve -mtune=cortex-a53`, VFPv4 + NEON; `-DBM_ZERO2` per gli indirizzi del
  SoC). Niente port a 64 bit: il codice (Lua, driver, puntatori a 32 bit) resta uno solo,
  e ARMv7 gira anche in QEMU `raspi2b` (Cortex-A7) per i test.
- **Una sola SD per tutte le schede**: `kernel.img` e `kernel7.img` stanno insieme e
  `config.txt` fa partire `kernel7.img` sul Zero 2 W (`[pi02]`). `make sdcard`,
  `install`, `image` e `release` mettono tutti e due i kernel.
- **Un core**: gli altri tre restano nello stub del firmware (a 0x0, per questo i vettori
  delle eccezioni non si copiano più lì ma si usa VBAR). Usarli è un passo successivo.

**Passi** (2026-10-02):
1. ✅ (QEMU `raspi2b`, test `make test-hyp`) **CPU**: `start.S` passa da HYP (dove il firmware
   avvia il Cortex-A53) a SVC come lo stub di Linux (nessuna trappola verso HYP: VFP,
   NEON, contatore generico), vettori con VBAR; l'interrupt salva anche d16–d31;
   barriere e cache ARMv7 (`src/arch/cache.c`: righe da 64 byte, "tutta la cache" per
   set/way su L1 e L2); MMU con le periferiche a 0x3F000000 e quelle dell'ARM a
   0x40000000, la RAM solo fin dove c'è; MMU e cache prima di tutto il resto (senza MMU
   il Cortex-A53 vede la RAM come memoria "device": gli accessi non allineati sono
   eccezioni). `make test-hyp` avvia `start.S` nella macchina `virt` di QEMU con le
   estensioni di virtualizzazione, che parte in HYP come il Pi (`raspi2b` parte in SVC).
2. ✅ (test sul PC) **Scheda**: revisione `902120` → `Pi Zero 2 W`, LED ACT sul GPIO 29
   attivo basso (sul Zero 2 W il GPIO 47 è l'I2C dell'alimentatore: `kernel7.img` non lo
   tocca mai); `Pi 2 B` per QEMU. La prima riga dice SoC e revisione, la seconda il
   processore e il modo di avvio (`Cortex-A53 from HYP`); la riga CPU del menu System il
   processore. Test: `make test-usb` (`test_board`, `test_board7`).
3. ✅ (da provare sul Pi) **WiFi e Bluetooth del CYW43436**: stessi pin del Zero W tranne
   BT_ON (GPIO 42 invece di 45). Il chip dice 43430: rev 2+ è il 43436
   (`brcmfmac43436-sdio.bin/.txt/.clm_blob`), rev 1 il 43436s (`brcmfmac43436s-sdio.bin/.txt`
   con la CLM del 43430); la patch Bluetooth dalla sottoversione LMP (`0x410c` →
   `SYN43430B0.hcd`, `0x2209` → `SYN43430A1.hcd`), come Linux e Raspberry Pi OS.
   `make firmware` scarica anche questi file, `make sdcard` li mette in `bm/`.
4. ✅ (test sul PC) **Kernel dalla rete**: all'offset 4 di ogni immagine c'è `bmK6` o `bmK7`;
   `bm_net.py --kernel` su un Zero 2 W scrive `kernel7.img`, e un kernel per l'altra
   scheda viene rifiutato (`KA`) senza scrivere niente. Test: `make test-net`.
5. ✅ (QEMU) **Test**: `make test-zero2` = tutti i test in QEMU con `kernel7.img` in
   `raspi2b` (saltati quelli di Pi 1, radio e chainloader); anche nella CI.

**Da provare sul Pi Zero 2 W** (tutto sullo schermo): `make firmware && make install`
(o `make image`), poi accendere il Zero 2 W:
- prima riga `Raspberry Pi Zero 2 W (BCM2710A1, revision 902120)`, seconda
  `Cortex-A53 from HYP, MMU+caches on`; il LED ACT lampeggia una volta al secondo;
- menu, giochi a 60 fps, audio HDMI, tastiera o pad USB (adattatore OTG), salvataggi;
- `W` (WiFi): la riga `wifi: chip 43430 ... CYW43436` (o `CYW43436s`) e la connessione;
  `T` (Bluetooth): `bt: chip LMP subversion ...` con il nome della patch, poi un DS4;
- la stessa SD nel Zero W: parte `kernel.img` come prima.
- **Fatto quando:** tutto questo funziona sul Pi Zero 2 W.

Poi, se servono: gli altri tre core (audio, rete o rendering su un core a parte), i
giochi più pesanti a 60 fps grazie alla CPU più veloce, misure in `docs/PRESTAZIONI.md`.

**Chiusa il 2026-10-05** (decisione dell'utente): `kernel7.img` funziona in QEMU (`raspi2b`,
avvio in HYP nella macchina `virt`); dal 2026-10-03 è spento nelle build (`make ZERO2=1` lo
riaccende) e il codice `BM_ZERO2` continua a compilare. Resta fuori: la prova sul Pi Zero 2 W
descritta sopra.

## M32 — Mouse USB e Bluetooth, puntatore di sistema (M) — ✅ chiusa (2026-10-05)
Richiesta 2026-10-01 (utente): supporto mouse **USB e Bluetooth**, anche con le **levette
analogiche** dei pad. Decisioni:
- il mouse si può spegnere **per tutto il sistema** ma non dall'utente: `mouse=off` in
  `bm/config.txt`, nessuna voce nel menu; di base è acceso;
- è attivo a seconda dell'**ambiente**: nel menu di bm sì, nelle app no, a meno che l'app
  lo chieda (`mouse(true)`);
- è **nascosto** se l'ambiente non lo supporta o se non c'è niente che lo muova;
- nella barra un'**icona del mouse bianca**, senza numero (non è un giocatore), con un
  **pallino blu** se è Bluetooth.

Fatto (QEMU, test sul PC):
- `src/usb/hid.c`: dove sono tasti, X, Y, rotellina e AC Pan nel report di un mouse, dal
  descrittore USB o dalla mappa dei report LE (ID, campi da 12 o 16 bit), oppure il report
  del protocollo boot; posizioni assolute (tavolette, l'`usb-tablet` di QEMU); la **levetta
  destra** e L2 R2 L3 R3 di DS4, Xbox 360 e gamepad HID (Rx/Ry o Z/Rz). `pad()` vede i
  nuovi bit (4096 L2 ... 32768 R3; nano8 li sa mappare).
- `src/usb/usb.c`: un'interfaccia mouse **oltre** alla tastiera o al gamepad, sullo stesso
  dispositivo (ricevitore con tastiera e mouse) o su un'altra porta dell'hub, col suo
  endpoint. La decisione di M7b/M29 ("un solo dispositivo HID") diventa "uno più il
  mouse". L'`usb-tablet` di QEMU, prima un gamepad, ora è un mouse.
- `src/bt/ble.c`: **tastiera e mouse LE insieme** (lo stato è per dispositivo; una sola
  scansione passiva per quelli lontani, una connessione alla volta). Il mouse si abbina
  senza codice (Just Works, LE Secure Connections o legacy); dalla mappa dei report si
  prende il report del mouse, altrimenti il Boot Mouse Input Report. Chiavi `bt_mouse` e
  `bt_mouse_key`; si ricollega da solo (anche da un indirizzo privato, con l'IRK).
- `src/bt/bt.c`: **mouse Bluetooth classico** in protocollo boot (SET_PROTOCOL sul canale
  di controllo, report `A1 02`), un collegamento e una chiave suoi (`bt_mouse_classic`).
  `O` dal monitor o Settings > Controllers > *Pair a mouse*: prima 10 s di ricerca LE, poi
  8 s di ricerca classica.
- `src/kernel/pointer.c`: il **puntatore di sistema**. La posizione è una frazione dello
  schermo (passando dal menu 640x360 a un gioco 320x180 resta allo stesso punto); il
  movimento è scalato come su 640x360, con accelerazione (lento preciso, veloce lontano);
  la levetta destra con zona morta e curva (560 pixel/s al massimo); con la levetta, R2 o
  R3 è il tasto sinistro e L2 il destro. Si vede quando c'è chi lo muove ed è "attivo":
  un mouse appena collegato, o un movimento; nel menu i tasti e la croce lo nascondono.
  Un clic col puntatore nascosto lo mostra soltanto. Freccia bianca bordata di nero,
  12x20, o 8x12 sugli schermi alti meno di 288 pixel.
- **Menu**: passando sopra una copertina intera o una riga di un pannello la si sceglie;
  tasto sinistro = A su quello che c'è sotto (copertina, scheda, Settings, riga, i
  pulsanti A / B / X in basso; una copertina tagliata dal bordo si sceglie soltanto),
  destro = opzioni della copertina o indietro; un clic fuori da un pannello o da una
  domanda li chiude; la rotellina scorre le righe. `menu_ui_hit()` dice cosa c'è sotto un
  punto dell'ultimo fotogramma (zone registrate mentre si disegna).
- **Barra**: icona `ICON_MOUSE` in `icons.c`, con il pallino `ICON_DOT` (blu) per il
  Bluetooth; una per il mouse USB e una per quello Bluetooth se ci sono entrambi.
  2026-10-02 (utente): il mouse ha la forma dell'immagine di riferimento (capsula
  verticale, linea tra i tasti dall'alto fino sotto la rotellina, rotellina a pillola), piena
  come le altre icone: linea e contorno della rotellina sono tagli di 1 pixel; `ICON_DOT` è lo stesso
  disco dei numeri, in basso al centro, senza la cifra. `ICON_DOT` vale per ogni icona:
  tastiera e controller col pallino senza numero esistono (`icon_mask(ICON_PAD, ICON_DOT)`),
  la barra per ora non li usa.
- **Cartucce**: `mouse(on, [freccia])`, `mouse()` → `x, y, tasti, rotellina, visibile`,
  `mousep([i])` (docs/API-IT.md). La freccia si disegna sulla pagina mostrata, mai nel buffer
  della cartuccia (anche disegnando via RAM). nano8: le cartucce con `poke(0x5f2d, 1)`
  seguono il puntatore appena compare (sopra l'immagine 128x128, senza freccia: la
  disegnano loro). L'assistente (M30) conosce `mouse` e `mousep`.
- Settings > Controllers: la riga **Mouse** ("USB", "Bluetooth on/off", "off" con
  `mouse=off`) e **Pair a mouse**; *Forget all controllers* e `P` dimenticano anche il mouse.
- Test: `make test-usb` (descrittori del mouse e del tablet di QEMU, una mappa stile
  Logitech, il protocollo boot, levetta destra e grilletti); in QEMU `test_usb_mouse`
  (tastiera e tablet dietro un hub: icone, freccia, passaggio sopra, rotellina, tasti che
  la nascondono, schede, tasto destro, clic fuori, clic che gioca), `test_mouse_cart`
  (API in un gioco 320x180 e `mouse=off`), `test_bt_mouse` (MX Keys e MX Master simulati
  insieme: abbinamento Just Works, icone blu, puntatore, Settings, riconnessione con
  l'IRK, chiavi), `test_bt_mouse_classic`, `test_stick_pointer` (levetta destra, R2, L2; dal
  2026-10-05 `test_pad_no_pointer`: i controller non lo muovono più).
  La simulazione dei dispositivi LE (`FakeMxKeys`) ora regge più collegamenti.

**Da provare sul Pi** (senza seriale, tutto sullo schermo):
- il mouse Bluetooth LE: mouse in modalità abbinamento, poi Settings > Controllers > *Pair
  a mouse* (col DS4 o la tastiera); la schermata dice "found mouse ...", "no code (Just
  Works)", "mouse ... connected"; nel menu l'icona del mouse col pallino blu e la freccia;
- il puntatore nel menu: passare sulle copertine, clic per giocare, tasto destro per le
  opzioni, rotellina; la **velocità** (da regolare se è troppo lenta o veloce);
- spegnere e riaccendere il mouse (o muoverlo dopo un po'): si ricollega da solo, anche con
  la MX Keys collegata;
- la levetta destra del DS4: la freccia compare muovendola; R2 clicca;
- `mouse=off` in `bm/config.txt`: niente freccia né icona.

- **Fatto quando:** sul Pi il mouse Bluetooth LE si abbina dal menu, muove il puntatore a
  60 fps insieme alla MX Keys e si ricollega da solo; la levetta destra fa lo stesso.

**Chiusa il 2026-10-05** (decisione dell'utente): mouse USB e mouse Bluetooth LE e classico
muovono il puntatore di sistema in QEMU, nel menu e nelle cartucce che lo chiedono. Restano
fuori: la prova sul Pi descritta sopra e il puntatore negli strumenti della suite (nessuno lo
chiede ancora).

**Dopo la chiusura (2026-10-05, richiesta dell'utente):** un controller collegato faceva
comparire la freccia nel menu e nelle app come se ci fosse un mouse (la levetta destra la
muoveva, R2 e L2 cliccavano). Ora solo un mouse muove il puntatore: `pointer.c` non legge più
i pad (via `hid_pointer_buttons`), la levetta destra resta ai giochi (`stick(p, 1)`). Prova:
`test_pad_no_pointer` in QEMU (DS4 abbinato: niente freccia nel menu, `mouse()` nil nella
cartuccia che lo chiede, nessun clic).

## M33 — GPU e 3D più veloce (L/XL) — ✅ verificata sul Pi (2026-10-01)
Decisione 2026-09-30, dopo l'analisi delle prestazioni 3D: il rasterizzatore software ha
ancora un margine (circa 2× sui pixel con texture), ma il salto vero è la **GPU 3D del
VideoCore IV** (V3D: 12 QPU, texture filtrate, z-buffer nel chip), finora mai usata.
Niente OpenGL: né quello del firmware (VCHIQ, userland Broadcom, thread) né Mesa (Linux
DRM). Un **driver V3D nostro, piccolo e a funzioni fisse**, sotto l'API che le cartucce
usano già (`mesh`, `draw3d`, `camera3d`, `light3d`, `fog3d`, `lamp3d`): le cartucce non
cambiano, vanno più veloci. Il rasterizzatore software resta per QEMU (che non emula la
V3D), per i test sul PC e come riserva.
- **Fatto quando:** Texture Room gira a 60 fps a 640×360 con la GPU (la cartuccia
  *Texture Room HD*, poi il benchmark Texture Room della scheda Dev), e lo stress test
  mostra le righe GPU accanto a quelle software.

**Passi:**
1. **Misure** (stress test `s`): la parte C parte dopo che l'avvio si è calmato (WiFi,
   Bluetooth); righe con un quad a tutto schermo (piatto, Gouraud, texture) per separare
   il costo per pixel da quello per triangolo; clock del core (fissato a 250 MHz da
   `enable_uart=1`: regola la L2 e il bus della memoria).
2. **Rasterizzatore più veloce** (software): divisione per l'area una volta per
   triangolo, mesh fuori dallo schermo scartate prima di trasformarle, ciclo delle
   texture più corto, Gouraud a rampa quando la luce è bianca, z-buffer pulito dal DMA.
3. **Modo 480×270** per le cartucce (4× esatto su 1080p): la risoluzione naturale per il
   3D in software.
4. **Prova della V3D** (monitor `g`, passo per passo sullo schermo come `D`): accensione,
   identificativo, pulizia dello schermo con la sola lista di rendering, un triangolo
   Gouraud, molti triangoli con i tempi.
5. **Backend V3D per `draw3d`**: piatto, Gouraud, z-buffer; righe GPU nello stress test.
6. **Texture, nebbia, trasparenze, MSAA 4×**; poi, se servono: vertex shader sulle QPU,
   luce per pixel, sprite 2D sulla GPU.

Fatto (2026-10-01, da misurare sul Pi):
- Passo 1: lo stress test aspetta 20 s dopo l'avvio, scrive clock (core e massimo, V3D,
  SDRAM), throttling e tempo negli interrupt (con i due più pesanti), e ha quattro righe
  `quad 320x180` (piatto, senza z, Gouraud, texture) con il costo di un pixel in ns.
- Passo 2: bordi dei triangoli in virgola fissa 32.32 (niente confronti in virgola mobile
  per riga), cicli delle texture specializzati (clamp controllato per segmento,
  trasparenza per cella dello sheet, luce con 2 moltiplicazioni), dither del Gouraud in un
  registro che ruota, mesh fuori dalla vista scartate prima di trasformarle, z-buffer
  pulito dal DMA a fine frame (`dma_zclear=0` in `bm/config.txt` lo spegne). Pixel
  identici a prima; istruzioni ARM per pixel in `docs/PRESTAZIONI.md` (texture con luce
  70 → 46, Texture Room 85 → 58). Strumenti: `make bench3d` (checksum delle scene) e
  `make count-insns` (istruzioni contate con `qemu-arm`).
- Passo 4 (da provare sul Pi): **prova della V3D**, monitor `g` o "GPU test" nella
  scheda Dev. Driver minimo `src/gpu/v3d.c` (accensione col mailbox, identità, cache
  della V3D, liste di controllo con timeout e registri sullo schermo in caso d'errore),
  shader QPU assemblati da `tools/qpuasm.py` (che riproduce bit per bit gli shader di
  due esempi bare metal già provati su un Pi Zero W). Passi, ognuno scritto prima di
  partire: 1 accensione, 2 identità (slice, QPU, TMU), 3 pulizia dello schermo con la
  sola lista di rendering (e l'ordine dei colori in RGB565), 4 un triangolo Gouraud,
  5 z-buffer (il triangolo più vicino vince in entrambi gli ordini), 6 velocità con
  20 000 triangoli piccoli, 7 velocità con 20 schermi interi, 8 un quadrato con texture
  letta dalla TMU (texture RGBA a 32 bit in ordine di riga, come nell'esempio per il Pi
  Zero W), 9 un'immagine disegnata dalla GPU direttamente nella pagina della console
  (resta 10 s o fino a un tasto).
  In QEMU (che non ha la V3D) si ferma al passo 1 e lo dice (`test_gpu_absent`).
- Passo 3: modo **480×270** per le cartucce (`mkbm.py --res 480x270`, `SCREEN_W` 480,
  l'editor lo propone tra 640×360 e 320×180); test QEMU `test_res_480` (modo video, 3D
  con texture, z-buffer pulito dal DMA).
- Passi 5 e parte del 6: **backend GPU per
  `draw3d`** (`src/gpu/gpu3d.c`). r3d trasforma, illumina, taglia e scarta come prima e
  passa i triangoli dello schermo al backend, che li raccoglie in un lavoro per la V3D
  (gruppi di triangoli con lo stesso shader, z e texture; vertici nel formato NV);
  la V3D carica ogni tile dalla pagina (il 2D disegnato prima resta sotto), disegna con
  uno z-buffer a 24 bit e la rimette nella pagina. Tre shader: colore sfumato (piatto e
  Gouraud, nebbia e luci già nel colore), texture × luce, texture × luce con i texel
  trasparenti scartati. Triangoli oltre il range delle coordinate tagliati in una banda
  di guardia; sheet più grandi di 2048 in grigio. All'avvio una prova in un buffer
  piccolo trova da sola l'ordine dei byte di colori e texel. Il 3D in attesa viene
  disegnato prima del 2D che lo segue, di `pget`/`sset`/`light_begin` e a fine
  fotogramma (contato in `stat(1)`); se la GPU non finisce un lavoro il kernel torna
  all'ARM e scrive i registri nel log. Si accende in *Impostazioni > 3D of the games*
  (`gpu3d=1`); `stat(9)` lo dice alla cartuccia (Texture Room scrive `GPU` nell'HUD);
  a fine partita il log ha lavori, triangoli e ms della GPU per fotogramma.
  Prova sul Pi: passo 10 di `g` (la stessa scena da ARM e GPU, tempi, pixel diversi,
  le due immagini affiancate). Sul PC: emulatore della V3D (`tests/gpu/v3d_emu.c`:
  liste di controllo, binning per tile, shader eseguiti per tipo) e `make test-gpu3d`
  (scene confrontate col rasterizzatore software, nei quattro ordini di byte possibili).
  Poi: **righe GPU nello stress test** (sfere piatte, Gouraud, con texture e i quad a
  tutto schermo disegnati dal backend, fino a 2000 quad con lo z a 24 bit; in QEMU la
  riga `GPU rows: none (...)` dice perché mancano) e **z-buffer conservato tra un
  lavoro e l'altro**: quando un lavoro si chiude a metà fotogramma (lavoro pieno,
  texture da rifare, oppure 2D seguito da altro 3D) la V3D salva lo Z in memoria in
  formato T e il lavoro dopo lo ricarica, come fa il driver vc4 di Linux (un load per
  volta, uno store vuoto in mezzo). Per il 2D lo fa solo per le cartucce che ne hanno
  bisogno (lo impara al primo fotogramma), per non pagare 1 MB a fotogramma per ogni
  HUD. Prova sul Pi: passo 11 di `g` (3D, 2D, 3D confrontato con l'ARM).
  Infine **Texture Room HD**: lo stesso `main.lua` di Texture Room costruito a 640×360
  (`build/carts/texroom_hd.bm`), la cartuccia del criterio di chiusura; con il 3D
  sull'ARM scrive in basso come accendere la GPU.
  Report prima/dopo (istruzioni, funzioni, correttezza della GPU sull'emulatore, cosa
  misurare sul Pi): `docs/M33-PRIMA-DOPO.md`.
- **Sul Pi (2026-10-01, `6c2fdaa`):** il 3D sull'ARM va 2,3× (sfere 31 → 70 a 60 fps), i
  triangoli 2D 1,9×, e Texture Room HD sull'ARM fa 37–41 fps. La V3D si accende e
  risponde (3 slice × 4 QPU, 250 MHz), ma la prima pulizia si è fermata con "no end of
  frame": l'attesa del driver prendeva per un errore il bit "binner senza memoria",
  acceso fin dall'avvio. Corretto, con un test sul PC che simula i registri come li ha
  mostrati il Pi (`make test-v3d`).
- **GPU sul Pi (2026-10-01, `d0c7fe8`):** il test `g` passa tutti gli 11 passi (stessa
  scena 3309 triangoli: ARM 28,8 ms, GPU 9,1 ms, 0,1% di pixel diversi; z conservato tra
  2D e 3D: 0,0%). La V3D riempie 811 Mpixel/s e fa 3 milioni di triangoli/s. Righe GPU
  dello stress: 182 sfere a 60 fps (7142 triangoli) contro 69 sull'ARM, quad 1 ns per
  pixel (9 con texture). Il limite ora è l'ARM (~2 µs per triangolo). Per chiudere M33
  manca Texture Room HD con la GPU.

✅ Verificata sul Pi (2026-10-01, `d0c7fe8`): **Texture Room HD con la GPU** a 640×360,
32 casse (573 triangoli), **5,8 ms, 60 fps** (sull'ARM: 25–26 ms, 37–41 fps); Chaos
Kitchen con la GPU 6,8 ms, 60 fps, 758 triangoli (prima di M33: 14,1 ms, 54 fps); lo
stress test ha le righe GPU accanto a quelle software. Il criterio di chiusura è
raggiunto, e da qui **la GPU è il default** per il 3D dei giochi (`gpu3d=0` o
*Impostazioni > Graphics > 3D of the games: ARM* per l'ARM; in QEMU e se la V3D non risponde si
torna all'ARM da soli). Report prima/dopo: `docs/M33-PRIMA-DOPO.md`.

Dopo la chiusura (2026-10-01): Texture Room e Texture Room HD diventano **un benchmark
nella scheda Dev** (comando `R` del monitor), non più giochi. Il kernel porta la
cartuccia al suo interno e la rilancia passo per passo (`bm_next_run`: risoluzione,
renderer e numero di casse, che la cartuccia legge in `BENCH`): casse raddoppiate da 8
finché tiene 30 fps, a 320×180 e poi a 640×360, con l'ARM e poi con la GPU. Ogni passo
dura 2 s e scrive casse, triangoli, ms e fps; alla fine un riepilogo con le casse
massime a 60 e a 30 fps per ogni caso (`src/bm/roombench.c`). Restano fuori da
M33, e passano a M34: MSAA 4×, texture in T-format per la TMU (oggi 9 ns per pixel con
texture contro 1), il costo per triangolo dell'ARM (~2 µs, ora il limite); per dopo il
filtro bilineare (cambia l'aspetto delle texture rispetto all'ARM) e gli sprite 2D
sulla GPU.

## M34 — GPU 2: anti-aliasing, texture a tile, meno lavoro per l'ARM (L) — in corso
Il seguito di M33 (decisione 2026-10-01): con la GPU il 3D è limitato dall'ARM (~2 µs
per triangolo) e, con le texture, dalla lettura in ordine di riga (9 ns per pixel contro
1). In più la GPU sa fare l'anti-aliasing che l'ARM non può permettersi.
- **Fatto quando:** sul Pi il test `g` passa i passi 12 e 13 (texture a tile identiche
  e più veloci, MSAA con i bordi smussati), lo stress test ha le righe AA, e il costo
  per triangolo dell'ARM con la GPU scende in modo misurabile (righe GPU spheres,
  benchmark Texture Room).

**Passi:**
1. **Pagina pulita senza load**: un fotogramma che comincia con `cls` non fa rileggere
   la pagina alla GPU; le tile partono dal colore di `cls`.
2. **Texture in T-format** (il formato a tile della TMU, meglio per la sua cache), con il
   layout imparato dalla GPU stessa e il ritorno all'ordine di riga se la prova non torna.
3. **MSAA 4×** per il 3D della GPU, scelto in *Impostazioni > Graphics*.
4. **Meno lavoro per triangolo sull'ARM** (trasformazioni, vertici NV, liste).

Fatto (2026-10-01, da verificare sul Pi):
- Passo 1: `cls()` dice al backend il colore della pagina (`gpu3d_page`); il lavoro
  che parte da una pagina di un solo colore pulisce le tile con quel colore invece di
  caricarle (un load della pagina in meno per fotogramma). Lo stress test fa lo
  stesso con le sue righe GPU.
- Passo 2: all'avvio, dopo gli ordini dei byte, la prova disegna una texture 64×64
  RGBA8888 in cui la parola *i* si vede come il colore RGB565 *i*: dai pixel il backend
  impara, texel per texel, quale parola legge la TMU (tile da 4 KiB di 32×32 texel, il
  layout dentro la tile nelle righe pari e dispari, l'ordine delle tile) e mette le
  texture con i lati multipli di 32 in T-format solo se tutto torna; altrimenti restano
  in ordine di riga. Lo stato della GPU lo dice (`textures in tiles` o `in rows`).
  Prova sul Pi: passo 12 di `g` (pavimento 256×256 con texture, in ordine di riga e a
  tile: tempi e immagini identiche, altrimenti le tile si spengono e lo scrive).
  Sul PC l'emulatore ha tre layout T-format (`make test-gpu3d`).
- Passo 3: **MSAA 4×**. Tile di 32×32 con 4 campioni, rasterizzazione a 4 campioni
  (`CONFIGURATION_BITS`), la media scritta nella pagina allo store. Caricare la pagina
  in un tile multicampione non è un percorso usato da Mesa né dal driver di Linux,
  quindi una prova all'avvio lo controlla: un lavoro MSAA che pulisce deve dare il
  colore giusto, e un load deve riempire i 4 campioni (allora MSAA su ogni pagina;
  se ne riempie uno solo, MSAA solo sulle pagine pulite con `cls`; se il lavoro non
  finisce, niente MSAA). Mai con lo z-buffer conservato tra un lavoro e l'altro (i
  campioni sarebbero 4 per pixel). Si accende in *Impostazioni > Graphics > 3D
  anti-aliasing* (`gpu3d_aa=1`, spento di default); le Impostazioni ora hanno il
  sottomenu *Graphics* (disegno dei giochi, 3D, anti-aliasing). Prova sul Pi: passo 13
  di `g` (la scena del passo 10 senza e con MSAA, tempi, quota di pixel smussati e un
  ritaglio ingrandito 2× delle due immagini affiancate) e le righe `GPU spheres AA 4x`
  e `GPU quad AA 4x` dello stress test. Sul PC l'emulatore fa l'MSAA (campioni, media)
  anche nella variante in cui il load riempie un solo campione.
- Passo 4: **meno istruzioni ARM per triangolo** con la GPU, pixel identici (stessi
  checksum di `make bench3d`, sull'ARM e sulla GPU emulata): sfere 936 → 706 (−25%),
  con texture 1131 → 888, Texture Room 1386 → 1109 (`make count-insns`, scene `+gpu`;
  dettagli in `docs/PRESTAZIONI.md`). `count-insns` non traccia più l'emulatore della
  V3D (da 10 minuti a 80 secondi per scena). Prova sul Pi: righe GPU dello stress test
  (µs per sfera, prima 80) e benchmark Texture Room.

**Unita a Overbit (2026-10-03, branch `claude/overclone`).** Nel branch
`3d-performance` M33 e M34 erano M30 e M31 (qui M30 è l'assistente, M31 il Pi Zero 2 W, M32 il
mouse del branch principale). r3d unisce le due strade: i bordi in virgola fissa, i
cicli delle texture specializzati, le mesh fuori dalla vista scartate (non quelle con
scheletro) e il backend della GPU, con quello che Overbit aveva aggiunto (materiali,
luce del cielo, riflessi, retino, livelli di dettaglio, ossa, luce precalcolata agli
angoli, ombre, effetti, strato in prima persona, Gouraud impacchettato, righe tagliate
ai lati). Il primo piano di `R3D_FRONT` con la GPU è uno `zclear` del backend.
`stat(6)` del branch (la GPU disegna) diventa `stat(9)`: 6–8 sono già i tempi di
Overbit. Pixel del rasterizzatore uguali a prima del merge (`bench3d`: 1–14 pixel su
230 400 cambiano di un livello di retino).

**Overbit sulla GPU (2026-10-03).** Il backend ha imparato quello che a Overbit
mancava: le facce a retino (uno shader che scarta i pixel dispari), le texture con la
luce precalcolata RGB e la nebbia sugli angoli (shader `TEX_RGB`, anche con le
`lamp3d`), le ombre (nere a retino con la prova dello z, senza leggere lo schermo) e
gli effetti 3D (punti, linee, sprite come triangoli). Resta all'ARM solo una faccia
con texture *e* retino insieme (`r3d_t.arm_hook`; il fotogramma misto non si mostra).
Nel gioco: menu "3D" (GPU, GPU+AA, ARM), `gpu3d([on, aa])` per le cartucce, Gouraud da
MEDIUM e ombre da HIGH quando disegna la GPU, e il **benchmark** (*Overbit >
BENCHMARK*): i bot giocano la stessa partita con ogni renderer e qualità, poi un anello
di eroi che cresce fino a 30 fps; tre pagine di report. Poi le ottimizzazioni misurate
con `tests/overbit/frames.py` (istruzioni dell'ARM per fotogramma, per funzione, con
`qemu-arm`): driver più snello per triangolo, ombre e trasformazioni più leggere,
particelle e anelli del Lua riscritti. Numeri, limiti e budget di un 4 contro 4 in
`docs/M33-PRIMA-DOPO.md` (sezione 7).

**Per chiudere M34:** sul Pi il test `g` (passi 12 texture a tile e 13 MSAA), le righe AA
dello stress test, il benchmark Texture Room e il benchmark di Overbit con ARM, GPU e
GPU+AA; la faccia con texture *e* retino sulla GPU (oggi l'unico caso che torna
all'ARM); ~~il flicker dei menu con il 3D sull'ARM~~ (non si è più visto sul Pi, riferisce
l'utente il 2026-10-04).

**Sul Pi (2026-10-05, kernel v0.2.0; report `gpu`, `stress`, `room` e `bench3d` nel branch
`reports`).** ARM 1000 MHz, core 250, V3D 250, SDRAM 400 MHz, 45–53 °C, nessun throttling.
- Test `g`: passano i passi 1–13 e 15 (il 14 è saltato: vertex shader spento, M36). Passo 12:
  texture a tile 1286 µs contro 1506 in ordine di riga (1,2×), stessa immagine (`textures in
  tiles`). Passo 13: MSAA su ogni pagina (`MSAA on any page`), 1421 µs contro 1313 (+8%),
  6,3% dei pixel smussati. Passi 6 e 7 come il 1° ottobre: 2,98 milioni di triangoli e
  811 Mpixel al secondo.
- Stress test: GPU spheres **207** a 60 fps (il 1° ottobre 182; 73 µs a sfera, prima 80), GPU
  smooth 159 (170), GPU textured 165 (156); GPU quad texture **3 ns per pixel** (prima 9: le
  tile), 91 quad a 60 fps (28). Con l'MSAA quasi niente in meno: sfere 204, quad 199.
- Benchmark Texture Room: con la GPU **512 casse** (2199 triangoli) a 60 fps sia a 320×180
  sia a 640×360, 1024 a 30 fps; con l'ARM 64 a 320×180, meno di 8 a 640×360 (16 a 30 fps).
- Benchmark di Overbit con GPU e GPU+AA: fatto a 1920×1080 (sezione M38), quindi senza l'ARM.
- Il **criterio di chiusura è raggiunto**: passi 12 e 13, righe AA, costo per triangolo
  dell'ARM sceso (80 → 73 µs a sfera, 182 → 207 sfere). Della lista qui sopra restano la
  faccia con texture e retino sulla GPU e il benchmark di Overbit con l'ARM (a 640×360 o
  meno).

### Dopo M34: come si lavora (decisione 2026-10-03)
- **Tutto su `3d-performance`** (decisione dell'utente, 2026-10-03): il motore
  (`src/gpu`, `r3d.c`, shader, emulatore, benchmark) e Overbit. `claude/overclone` è
  fermo a `600e600`, già unito qui; non ci si lavora più.
- Ogni novità della GPU che cambia aspetto, latenza o memoria è **un'opzione**: una
  chiave in `bm/config.txt`, una riga in *Impostazioni > Graphics* e un argomento di
  `gpu3d()` per le cartucce e i benchmark. Il default resta quello verificato sul Pi; le
  ottimizzazioni a pixel identici non hanno interruttore.
- Ogni passo si misura su Overbit prima e dopo: il suo benchmark (bot, qualità,
  renderer, anello di eroi), `tests/overbit/frames.py`, `tests/bm/herobench.c`,
  `tests/bm/mapbench.c`, `tools/armprof.py`, `make bmhost-gpu`; e sul Pi con le foto del
  report.

## M35 — ARM e GPU insieme (M)
Oggi a fine fotogramma l'ARM aspetta che la V3D finisca (nella scena di prova 2,1 ms su
9,1). Obiettivo: quel tempo fuori dal fotogramma, e meno lavori per fotogramma.
1. **Fotogramma in coda:** il lavoro del fotogramma parte e l'ARM va avanti (Lua,
   trasformazioni del fotogramma dopo); si aspetta solo prima di toccare la stessa
   pagina. Opzione (un fotogramma di latenza in più).
2. **Memoria dei vertici e delle liste senza cache** (scrittura combinata): niente
   letture di righe di cache per dati che l'ARM non rilegge. Opzione, misurata sul Pi.
3. **Meno lavori per fotogramma** quando 3D e 2D si alternano (HUD di Overbit, editor
   3D): il 2D dopo il 3D raccolto e disegnato in un colpo dove l'ordine lo permette.
- **Fatto quando:** il benchmark di Overbit e lo stress test mostrano il guadagno sul Pi,
  con l'opzione accesa e spenta.

**Stato (2026-10-03, branch `3d-performance`; sul PC, da provare sul Pi).** Fatto il passo 1,
**bm3d 4.0**: `gpu3d_submit` avvia il lavoro di fine fotogramma con `v3d_start` (il binning
finisce con `INCREMENT_SEMAPHORE`, il rendering aspetta con `WAIT_ON_SEMAPHORE` prima del
primo tile, come il driver vc4 di Linux: i due thread partono insieme) e il runtime chiama
intanto il `_update` del fotogramma dopo; `gpu3d_sync` aspetta prima di toccare la pagina
(2D, letture, la copia sullo schermo), il driver prima del lavoro dopo o di cambiare mesh e
texture che il lavoro legge. Prova all'avvio `probe_queue` (stato: `queue yes`); opzione
*Graphics > 3D frame queue* (`gpu3d_queue`, spenta di default), quarto argomento di
`gpu3d()`; test `queue` del 3D Bench (GPU+VS e GPU+VS+Q, con 4 milioni di istruzioni di
logica dopo il 3D). L'emulatore controlla i semafori (un lavoro avviato che legge le liste
dei tile prima di aspettare è un errore) e ogni scena dei test esce identica in coda.

Fatto il passo 3, **bm3d 4.1**: in Overbit l'HUD dopo il 3D faceva aspettare la GPU a metà
fotogramma e lo `zclear()` delle braccia in prima persona apriva un secondo lavoro, quindi
la coda non serviva. Ora il 2D disegnato dopo il 3D mentre la GPU ha un lavoro sulla pagina
fa partire il lavoro e si registra (`d2` in `runtime.c`), poi va sulla pagina nello stesso
ordine quando la GPU ha finito (prima di altro 3D, di letture della pagina, della copia
sullo schermo); lo `zclear()` resta nel lavoro (`fs_zclear`: un quadrato che scrive la
profondità più lontana e rimette il colore letto dal tile buffer; prova `probe_zclear`,
stato `zclear in job yes`); il 2D del `_update` anticipato va sulla pagina dopo, e un
`_update` che disegna 3D o legge la pagina torna dopo il fotogramma. Sul PC (bmhost con
l'emulatore della V3D) Overbit fa un lavoro a fotogramma, avviato e non aspettato, con 53
disegni 2D registrati intanto, e fotogrammi identici a quelli senza coda;
`make test-queue2d` confronta i fotogrammi di tre cartucce di prova con la coda accesa e
spenta. In Overbit il renderer **GPU+VS+Q** (menu "3D" e benchmark) è quello da misurare
sul Pi; il passo 15 del test `g` (monitor) disegna la scena del passo 10 con un cubo in prima
persona, il lavoro avviato e aspettato dopo, contro l'ARM, e scrive quanti lavori sono
partiti e quanti `zclear()` sono rimasti nel lavoro. Una prova all'avvio di una cosa
facoltativa (vertex shader, clipping, coda, zclear) che blocca la V3D spegne solo quella,
se dopo uno sfondo pieno la GPU risponde ancora. Resta il passo 2.

**Sul Pi (2026-10-05, v0.2.0):** la prova della coda passa (`queue yes`) e il passo 15 del
test `g` disegna come l'ARM con il lavoro avviato e aspettato dopo; lo `zclear()` nel lavoro
invece **no** (`zclear in job no`: le braccia in prima persona chiudono il lavoro come
prima). Il guadagno non si è ancora potuto misurare: il renderer GPU+VS+Q di Overbit e il
test `queue` del 3D Bench vogliono anche il vertex shader, spento sul Pi (M36). Prossimi
passi: la coda senza vertex shader (un profilo GPU+Q nel 3D Bench e un renderer in
Overbit), misurabile subito; perché la prova dello `zclear()` fallisce (le righe `gpu3d:`
del log dall'avvio).

## M36 — Vertici sulla GPU (L/XL)
Con la GPU il limite è l'ARM (~1,5–2 µs per triangolo: trasformare, illuminare,
scartare, scrivere i vertici); la V3D da sola fa 3 milioni di triangoli/s. La mesh va
alla GPU una volta sola (finché non cambia) e a ogni `draw3d` l'ARM manda solo matrice,
luci e ossa; le QPU trasformano e illuminano, la V3D scarta le facce posteriori e
taglia.
1. **Prova sul Pi** (un passo del test `g`): un triangolo trasformato da uno shader
   delle QPU (shader di coordinate e di vertici, VPM), confrontato con l'ARM.
2. **Mesh piatte e Gouraud** sulla GPU, con l'emulatore che esegue gli stessi shader per
   tipo; dove la GPU non arriva si resta sul percorso di oggi, mesh per mesh.
3. **Luci, nebbia, lampade, texture**, poi **ossa** (l'animazione rigida di Overbit:
   una matrice per osso).
4. **Overbit** con i vertici sulla GPU: eroi e mappa.
- Opzione: *vertici: ARM / GPU*.
- **Fatto quando:** sul Pi l'anello di eroi di Overbit e le sfere dello stress test
  crescono di almeno 2× rispetto a M34 a 60 fps.

**Stato (2026-10-03, branch `3d-performance`; sul PC, da provare sul Pi).** Fatti i passi
2–4, più ombre e primo piano, sull'emulatore, che esegue gli shader veri (interprete
delle QPU) e taglia come GL:
- shader `vs_baked` e `vs_tex_rgb` (luce agli angoli, nebbia, 4 lampade, livelli di
  dettaglio, texture: la mappa di Overbit) e `vs_lit` (`light_fast` di r3d: cielo e
  terra, sole, bordo, riflesso con `exp2`/`log2` della SFU, lampade e nebbia nel punto
  dove le mette l'ARM: l'angolo, o il centro di una faccia piatta); le ossa come gruppi
  di facce per osso con un blocco di uniform a osso (negli eroi ogni faccia sta su un
  osso);
- le ombre (`vs_shadow`, `cs_shadow`: l'angolo nel mondo con il suo osso, giù lungo il
  sole fino al piano, nere a retino con la prova dello z): solo le facce che il sole
  vede, come l'ARM, perché schiacciate lungo il sole tengono il verso con cui il sole le
  vede e la GPU scarta le altre come facce posteriori; il primo piano (`R3D_FRONT`)
  dopo lo `zclear`, con le profondità 10 volte più vicine;
- la GPU taglia le mesh che passano il piano vicino o la guard band (record con il
  flag 4, `CLIPPER_XY/Z_SCALING`, angoli dal centro dello schermo con
  `VIEWPORT_OFFSET`);
- r3d: la sfera di ogni mesh centrata sul suo box (i pezzi della mappa, in coordinate
  del mondo, erano sempre "fuori"); cache di 256 copie delle mesh;
- prove all'avvio: `probe_gl` (bit del senso orario), `probe_clip` (piano vicino, guard
  band, senza e con `Z_MIN_MAX`), `probe_lit` (colore di `vs_lit` contro il calcolo);
  quello che non torna si spegne da solo;
- opzione `gpu3d_vs` (0 ARM, 1 lo scenario, 2 tutti i modelli, anche ombre e primo
  piano), *Graphics > 3D vertices*, terzo argomento di `gpu3d()`, renderer GPU+VS1 e
  GPU+VS di Overbit (menu "3D" e benchmark); passo 14 del test `g` (stessa scena con
  gli angoli dell'ARM e del vertex shader, a confronto, con i tempi); righe `GPU+VS`
  dello stress test; `bench3d`/`count_insns.py` con le scene `+vs` (sfere: 1,02 M
  istruzioni dell'ARM con la GPU, 72 k con il vertex shader).

Overbit, `tests/overbit/frames.py` (HIGH, combattimento di 10 bot, istruzioni dell'ARM
a fotogramma), prima persona / vista dall'alto:
- GPU (M34): 12,51 M / 11,03 M (~27,5 / 24,3 ms sul Pi);
- GPU+VS1, lo scenario sulla GPU con il clipping: 10,45 M / 8,55 M (−16% / −22%);
- GPU+VS, anche gli eroi: 7,20 M / 6,08 M (−42% / −45%);
- GPU+VS con ombre e primo piano: 5,99 M / 5,16 M (−52% / −53%; ~13,2 / 11,3 ms). r3d
  da 5,25 a 0,26 M, il driver da 2,13 a 0,57 M; ora conta il Lua del gioco (3,9 M).

Da vedere sul Pi: la riga di stato della GPU (`vertex shader yes, clipping yes, lit
models yes`), il passo 14 del test `g`, il benchmark di Overbit con GPU+VS1 e GPU+VS (i
µs della GPU: nella V3D il vertex shader gira di nuovo per ogni tile che il triangolo
tocca).

**Modelli Meshy (2026-10-03).** Gli eroi, i mech e i piloti di Overbit sono diventati
figure Meshy con texture (1200 triangoli) messe sugli scheletri degli eroi
(`art/meshyrig.py`). Per tenerle sulla GPU:
- **bm3d 3.3**: le facce con texture dei modelli illuminati dal sole, con la luce di ogni
  angolo (Gouraud) sull'ARM e sulla GPU, e sul vertex shader (`vs_lit_tex`);
- **bm3d 3.4**: le pelli, cioè le facce a cavallo di due ossa (un quarto delle facce di
  una figura Meshy, gli anelli attorno alle articolazioni): gruppi per coppia di ossa,
  ogni angolo messo dalla matrice del suo osso (`vs_lit_tex2`, `cs_colour2`, ombre
  `vs_shadow2`/`cs_shadow2`); le facce su tre ossa (dove tre parti si toccano) le toglie
  `meshyrig.py` con una copia dell'angolo.
Test: scene `vshader textured` e `vshader skin` dell'emulatore, `heroes_tex` e
`heroes_skin` del 3D Bench. Con le figure Meshy e bm3d 3.4, `frames.py` (stessa partita,
prima persona / vista dall'alto): GPU 15,01 M / 11,88 M istruzioni dell'ARM a fotogramma
(~33,0 / 26,1 ms sul Pi: r3d illumina e mette tre volte più angoli), GPU+VS 6,41 M /
5,71 M (~14,1 / 12,6 ms; r3d 0,27 M, il driver 0,85 M): con il vertex shader le figure
nuove costano all'ARM quasi quanto le vecchie (5,99 M / 5,16 M).

**Sul Pi (2026-10-05, v0.2.0 e v0.2.3): il vertex shader è spento.** La riga di stato dice
`vertex shader no, clipping no, lit models no`: la prova all'avvio (`probe_gl`) non è
passata, quindi tutto resta sul percorso dell'ARM (bm3d 2.1). Il passo 14 del test `g` è
saltato, lo stress test non ha le righe GPU+VS, il 3D Bench non ha i profili GPU+VS1 e
GPU+VS, Overbit non ha GPU+VS1, GPU+VS e GPU+VS+Q. Cosa ha disegnato la V3D lo scrive il log
all'avvio della GPU (righe `gpu3d: vertex shader probe: ...`): serve un report del log
(*Report the log*, `Z`) fatto dopo il test `g`. Poi correggere lo shader o il record, e
l'emulatore insieme (lì la prova passa).

## M37 — 2D e qualità sulla GPU (M, se serve)
- Sprite, tile e testo come quad della GPU, per i giochi con molto 2D sopra il 3D.
- Il **menu a 1080p** con la GPU attiva (decisione del 2026-10-04): sfondo, copertine, barre e
  testo come quad della V3D, a 60 fps. Oggi `menu_scale=3` lo mostra a 1920×1080 con lo stesso
  layout ×3 ingrandito dall'ARM (una prova, lenta; `docs/RISOLUZIONI.md`, sezione 6).
- Filtro bilineare delle texture (opzione: cambia l'aspetto rispetto all'ARM).
- Matrice unica oggetto→camera e luce nello spazio dell'oggetto: meno istruzioni per
  vertice, pixel non più identici al bit (opzione).

## M38 — Overbit: sparatutto a eroi in 3D (XL) — ✅ chiusa (2026-10-05)
Richiesta dell'autore (2026-10-02): un clone di Overwatch in `.bm`, **8 eroi** (2 tank,
4 DPS, 2 supporto), 3D in prima persona, **una mappa**, multiplayer online, dev kit, la
nostra AI per il gioco da soli. Lo scopo è **spingere la grafica** del Pi Zero al limite:
qualità massima finché si resta a 60 fps; se spingendo si tocca 30 fps si torna indietro.

Decisioni:
- **Nomi e aspetto nostri** (come nano8 non è PICO-8): stesso kit di ogni eroe (ruolo,
  numero di abilità e ultimate, meccaniche e numeri), ma nome del gioco, nomi degli eroi
  e design originali. Niente marchi di Blizzard né il logo Porsche del riferimento.
- Gioco **Overbit** (`carts/overbit`), branch `3d-performance` (fino al 2026-10-03
  `claude/overclone`).
- Eroi (il kit di partenza tra parentesi):
  - tank: **Rally** (D.Va) — una giovane pilota di corse in un mech bianco lucido con
    strisce arancio e due mitragliatrici rotanti al posto degli avambracci; **Kaiju**
    (D.Mon) — una giovane pilota in un grande mech rosso con spada e scudo (dal
    2026-10-03; prima un piccolo mostro con lama e scudo di plasma). I mech sono alti 3 e
    3,4 m, gli altri eroi 1,6–1,9;
  - DPS: **Sarge** (Soldier: 76), **Frost** (Mei), **Fuse** (Junkrat), **Rail** (Sojourn);
  - supporto: **Orbit** (Juno), **Akari** (Kiriko).
- 5 contro 5 come Overwatch 2 (1 tank, 2 DPS, 2 supporto), i posti vuoti ai bot.
- **320×180** (lo scaler della GPU porta a 720p/1080p): 4 volte meno pixel di 640×360,
  che con il rasterizzatore software è la scelta che lascia spazio alla qualità. Dal
  2026-10-03 **480×270**, ora che il 3D lo disegna la GPU (vedi "Risoluzione 480×270" in
  fondo a M38).
- Modelli e animazioni nel formato di bm Studio / bm Animator (sezioni MESH e ANIM),
  generati da script Python (`carts/overbit/art`): si aprono anche con gli strumenti della
  console.
- **bmhost** (`make bmhost`): il runtime vero delle cartucce sul PC, per i reel (frame e
  WAV), gli screenshot e i test. Il PC è circa 21 volte più veloce del Pi su Texture Room
  (0,55 ms contro 11,6 ms): i costi sul Pi si stimano così, poi si misurano sul Pi.
- **Qualità automatica**: il gioco misura il tempo dei frame (`stat(1)`) e alza o abbassa
  la qualità (dettaglio dei modelli, ombre, effetti, distanza) per restare a 60 fps; il
  dev kit ha una prova che spinge la qualità fino a 30 fps e mostra i limiti sul Pi.

Passi (in quest'ordine, richiesto dall'autore):
1. **Rally** (kit di D.Va) con animazioni e abilità, poi un **reel delle animazioni**:
   - motore: materiali per faccia (emissivi, lucidi, trasparenza a retino, livelli di
     dettaglio), luce colorata del cielo e del terreno, riflessi e luce di contorno, ombre
     proiettate, modello in prima persona sempre davanti, colpi contro le ossa (hitbox),
     strati di animazione (gambe e busto), effetti 3D;
   - Rally: mech (cannoni a fusione, propulsori, matrice difensiva, micro-missili,
     autodistruzione, espulsione) e pilota (pistola, richiamo del mech), HUD, suoni,
     poligono di tiro;
   - reel: ogni animazione in terza persona, renderizzato con bmhost (`docs/img`).
2. Gli altri **7 eroi**, con il numero di abilità e ultimate del kit di partenza.
3. La **mappa** e il motore del mondo: **Partenope**, un porto di una Napoli futura al
   tramonto (Vesuvio sullo sfondo), modalità controllo; geometria statica con BSP e span
   buffer (ogni pixel disegnato una volta), luce precalcolata (ombre e occlusione
   ambientale) nelle texture, cielo, collisioni e raggi in C; selezione degli eroi.
4. **AI**: bot con grafo di navigazione, combattimento e uso delle abilità; scelte
   tattiche da una piccola rete INT8 (come quella di M30) addestrata sul PC.
5. **Rete**: UDP per le cartucce, partite in LAN (una console ospita), predizione e
   interpolazione; online con un relay (`tools/`) per giocare via internet.
- **Dev kit**: menu di sviluppo nel gioco (tempi per fase, qualità, telecamera libera,
  hitbox, bot, reel), script degli asset, guida per aggiungere eroi e modificare la mappa.

**Passo 1 — Rally: fatto in QEMU e sul PC (2026-10-02), da provare sul Pi.**
- Motore (`src/bm/r3d.c`): bit di materiale nel colore delle facce (emissive, lucide, a
  retino, piatte, livelli di dettaglio), luce del sole colorata con cielo e terreno,
  riflessi e luce di bordo con una sola direzione di vista per oggetto (niente radici
  quadrate per vertice), lampade colorate, ombre proiettate (una maschera, poi ogni pixel
  scurito una volta), il livello in primo piano per la prima persona, punti, linee e
  sprite 3D. Lo skinning rigido è dentro la trasformazione dei vertici (una matrice per
  osso, solo i vertici del livello di dettaglio): `animate()` calcola solo le ossa.
  Nuove funzioni: `sky3d`, `shine3d`, `shadow3d`, `point3d`, `line3d`, `sprite3d`,
  `bone_turn`, `bones3d`, `hit3d`, `animate(..., osso)`, `stat(6..8)`; collisioni in C
  (`src/bm/world3d.c`: `world3d`, `world_box`, `world_ray`, `world_move`, `world_floor`).
- Input: levetta destra (`stick(p, 1)`), L2/R2/L3/R3 in `pad()` (DS4, Xbox 360, pad generici).
- Arte (`carts/overbit/art`): primitive, scatole smussate, gusci convessi, profili ruotati
  (`geo.py`), scheletri, pose e cinematica inversa delle gambe (`rig.py`); Rally: mech
  (842/702/484/220 triangoli per livello di dettaglio, 16 ossa, 17 animazioni), pilota (12
  animazioni), cannoni e mani in prima persona; banco di suoni (`sounds.py`).
- Gioco (`carts/overbit/src`): prima persona a 96°, HUD come Overwatch (vita a segmenti di
  25 con l'armatura, abilità con i tasti del pad o della tastiera, ultimate, mirino, colpi,
  kill feed), poligono di tiro con quattro manichini (fermo, che si sposta, che salta, che
  spara missili), effetti (particelle, traccianti, esplosioni con luce), qualità
  automatica (5 livelli), overlay di sviluppo (Select/Tab/F1: ms per fase, triangoli,
  vertici, pixel, grafico degli ultimi 64 frame; F2 qualità, F3 automatica, F4 ultimate),
  benchmark (mech che corrono e sparano, sempre di più ogni 3 s, fino a 30 fps: la
  tabella dice quanti ne reggono 60 fps).
- Kit di Rally: cannoni a fusione (11 pallini, 6,7 colpi/s, rallentano), Null Field
  (risorsa di 3 s che si ricarica, mangia i proiettili davanti), propulsori (volo di 2 s
  lungo la mira, urto con spinta), 18 micro-missili, Redline (il pilota salta fuori, il
  mech esplode dopo 3 s: 1000 danni fino a 20 m, i muri riparano), espulsione quando il
  mech è distrutto, Pit Pistol e Pit Stop (un mech nuovo dal cielo).
- **Reel delle animazioni**: modalità del gioco (`make overbit-reel` →
  `docs/img/overbit-reel-rally.mp4` con l'audio e la GIF dei primi 32 s): 11 riprese delle
  abilità in terza persona, le 29 clip una per una, un po' di prima persona.
- Strumenti: **bmhost** (il runtime delle cartucce sul PC: frame, WAV, input da script,
  `--clock-scale 21` per stimare i ms del Pi), **`tools/armprof.py`** (istruzioni ARM per
  funzione contate da QEMU con un costo approssimato dell'ARM1176: un profiler senza il
  Pi), `tests/bm/r3dbench.c`. Il rasterizzatore è stato riscritto nel percorso per
  vertice e per faccia (−23% di istruzioni sulla scena di prova).
- Test: `make test-overbit` (poligono giocato con tasti scriptati: il manichino cade,
  Redline, Pit Stop; tutto il reel; il benchmark), `test_overbit` in QEMU.
- Stime (PC ×21, da verificare): poligono a qualità alta ~12–14 ms per frame; il disegno
  3D costa ~5 µs a triangolo come misurato in passato sul Pi.
- **Da provare sul Pi**: Overbit dal menu; nel menu "BENCHMARK" (con la qualità scelta in
  "QUALITY": si può provarne più d'una) e una foto della tabella finale; nel poligono
  Select (o Tab) per l'overlay: una foto dei ms di update, del disegno 3D e del totale,
  ferma e mentre si spara; il pad (levetta destra per mirare, R2 sparare, L2 Null Field,
  L1 propulsori, R1 missili, triangolo ultimate).

**Passo 2 — gli altri 7 eroi: fatto sul PC (2026-10-02), da provare sul Pi.**
- Arte: un corpo comune per le persone (`humanoid.py`: scheletro con le proporzioni di
  ognuno, braccia sull'arma con la cinematica inversa, le clip standard, `body()` con i
  pezzi lungo le ossa), mani e braccia in prima persona (`fp.py`), `geo.tube`. Ogni eroe ha
  il suo modello, le sue animazioni speciali, l'arma in prima persona e i suoni.
- **Kaiju** (D.Mon): BIG RED, un mech rosso con la cupola verde (Plasma Saber 65 danni a
  colpo largo, Fusion Repeater da 30 colpi, Propulsors con 3 scatti di carburante, Power
  Barrier esagonale che ferma i colpi e con l'attacco Surging Strike, Limit Break: un giro
  completo della lama, 220 danni) e la pilota, una ragazza in bomber rosso (Mini
  Repeater, Call Mech; fino al 2026-10-03 un mostriciattolo verde).
- **Sarge** (Soldier: 76): fucile a impulsi (30 colpi, 9/s, la dispersione cresce),
  Helix Rockets, Sprint, Biotic Field, Tactical Visor (i colpi trovano il bersaglio).
- **Frost** (Mei): getto di gelo che rallenta, ghiacciolo (doppio alla testa),
  Cryo-Freeze, Ice Wall (5 colonne con la loro vita che fermano tutti e tutti i colpi),
  Blizzard (rallenta, ferisce, poi congela).
- **Fuse** (Junkrat): granate che rimbalzano, Concussion Mine (anche per saltare), Steel
  Trap, Boom Wheel guidata in terza persona, Total Mayhem (bombe quando cade).
- **Rail** (Sojourn): proiettili che caricano l'energia, colpo rail (30 + l'energia),
  Power Slide (e il salto alto), Disruptor Shot, Overclock (i colpi attraversano tutti).
- **Orbit** (Juno): Mediblaster che cura gli amici e ferisce i nemici, Pulsar Torpedoes
  (fino a 4 bersagli), doppio salto e volo planato, Glide Boost, Hyper Ring, Orbital Ray.
- **Akari** (Kiriko): ofuda che trovano gli amici, kunai (triplo alla testa), Swift Step,
  Protection Suzu, Kitsune Rush (una strada di portali: più veloci, più colpi, ricariche).
- Sistemi comuni: barriere che fermano i colpi, oggetti con la loro vita nel mondo,
  stati (congelato, bloccato, rallentato, più veloce, intoccabile), i nemici non si
  attraversano, numeri verdi delle cure, telecamere degli eroi, un amico ferito nel
  poligono per i supporti; nel menu **HERO** (sinistra/destra) per scegliere; F5 nel dev
  kit fa perdere la vita (un mech si rompe e il pilota salta fuori).
- **Reel**: tutti gli eroi, abilità per abilità e un po' di prima persona (100 riprese);
  `make overbit-reel-heroes` → `docs/img/overbit-reel-heroes.mp4` e la GIF delle 7
  ultimate.
- **Benchmark**: ora con tutti gli eroi in cerchio a 9 m (il caso peggiore: tutti vicini),
  con le colonne di update e disegno 3D. Stime (PC ×21, rumorose di ±3 ms): 12 eroi
  ~16–18 ms in HIGH, ~25 ms in ULTRA, ~16 ms in MEDIUM; la qualità automatica scende quando
  serve. Il disegno è la spesa maggiore: un eroe a pieno dettaglio ~0,6 ms (Rally, Sarge),
  ~0,9 ms (Frost, Fuse), la parte per faccia di `r3d_draw_flags` per prima (armprof):
  da ottimizzare con il motore del mondo (passo 3). Dettaglio pieno sotto i 7 m × la
  qualità, ombre dei personaggi con il modello povero.
- Test: `make test-overbit` gioca anche ogni nuovo eroe nel poligono (abilità e ultimate
  controllate nel log) e tutto il reel.
- **Da provare sul Pi**: nel menu HERO per scegliere l'eroe, poi il poligono; il
  BENCHMARK (foto della tabella: ora dice anche update e 3D).

**Passo 3 — la mappa Partenope e la modalità Controllo: fatto sul PC (2026-10-03), da provare sul Pi.**
- **Partenope** (`art/partenope.py`): il lungomare di una Napoli futura al tramonto, il
  Vesuvio oltre la baia. Simmetrica (una metà per squadra): la stanza di spawn in fondo,
  tre strade verso il centro (la via principale con la PIZZERIA BIT e la sua insegna al
  neon, il vicolo con i panni stesi e il passaggio sotto l'arco, il molo con i container
  e i chioschi), la piazza con la fontana della sirena e il **punto**, la chiesa con la
  cupola gialla, i campanili, la terrazza con le scale; facciate colorate con le persiane
  verdi, lampioni, neon, pannello olografico, pini, cielo a bande, sole basso, mare con il
  riflesso, il Vesuvio e la città sulla collina.
- **Motore del mondo**: invece di BSP e span buffer (il piano) la mappa è fatta di
  **pezzi di 8 m** con la luce già cotta agli angoli (modelli "lit" della sezione MESH).
  Sul Pi conta il triangolo, non il pixel (~4–5 µs a triangolo), quindi tutto lavora sul
  numero di triangoli: visibilità precalcolata (`tools/mappvs.c`: da ogni quadrato di 4 m
  disegna i pezzi con il rasterizzatore della console in colori che sono i loro numeri,
  sulle sei facce di un cubo; in media 44 pezzi su 96), poi i pezzi tagliati contro la
  vista e disegnati dal più vicino, quelli lontani senza le cose piccole.
- **Luce cotta** (`art/mapbake.py`, numpy): il sole basso con ombre morbide (più raggi),
  il cielo con l'occlusione ambientale, il rimbalzo caldo dei muri, lampioni, neon e la
  luce delle squadre negli spawn, con le loro ombre. Facce divise solo dove serve: lati
  fino a 12 m, le cose lunghe e sottili (cornicioni, cordoli, ringhiere) intere.
- **Texture delle facciate** (`art/maptex.py`): un atlante 512×256 disegnato pixel per
  pixel, che diventa lo sprite sheet della cartuccia (SHEET8, 255 colori): finestre con le
  persiane aperte o chiuse, accese, con il balcone, ad arco, portoni, vetrine, le insegne
  (PIZZERIA BIT, GELATO, CAFFE', PARTENOPE), tende a righe, panni stesi. 249 quadri al
  posto di ~2000 triangoli di persiane, ringhiere e vetri; le facce con texture dei modelli
  lit hanno una luce colorata e la nebbia.
- **Rasterizzatore**: il colore di Gouraud in un numero solo (rosso, verde e blu
  impacchettati, il dithering sommato, z con `usat`: 23 istruzioni a pixel invece di 31);
  i triangoli che escono dai lati dello schermo (un muro accanto alla telecamera) saltano
  le righe vuote (11 000 righe a frame nella vista della strada); texture lineari dove la
  profondità cambia poco e un percorso veloce per quelle della mappa. La vista della
  strada: da ~10 a ~6 milioni di cicli stimati (armprof). Un modello trovava il suo
  scheletro controllando di nuovo ogni chiave di ogni animazione: ora salta gli altri.
- Collisioni: 131 scatole in C (`world3d`). **Grafo dei bot**: 75 nodi lungo le strade,
  collegati quando un corpo può camminare in linea retta (nessuna scatola sulla linea e ai
  lati, il pavimento che sale al massimo di 0,5 m: le scale sì, i salti in giù in un verso).
- **Controllo** (`src/81_match.lua`): 5 contro 5 come la coda per ruoli di Overwatch 2
  (1 tank, 2 danni, 2 supporto), i bot negli altri nove posti. Scelta dell'eroe all'inizio
  e nella stanza di spawn (H, o giù sul pad: un bot del ruolo nuovo prende il vecchio
  eroe); il punto si apre dopo 20 s, una squadra da sola lo cattura in 8 s (più in fretta
  in due o tre), poi la sua percentuale sale di 1 ogni 0,9 s; contestato si ferma, al 99%
  con un nemico sul punto è tempo supplementare; vince il round chi arriva a 100, la
  partita chi vince due round. Si rinasce dopo 10 s. HUD in alto (percentuali, punto con
  la cattura, round vinti, messaggi), l'anello del punto sul terreno nel colore di chi lo
  tiene, suoni (apertura, cattura, punto perso, vittoria, sconfitta, conto alla rovescia),
  il tabellone alla fine.
- Bot provvisori (il passo 4 li fa davvero): seguono il grafo verso il punto, aspettano
  che si apra, mirano con un po' di errore e sparano nel raggio del loro eroe, usano le
  abilità ogni tanto e l'ultimate con un nemico vicino; i supporti curano chi è ferito.
- **Reel della partita**: dieci bot, una telecamera che gira intorno al punto, segue un
  eroe e guarda con i suoi occhi (`make overbit-reel-match` → `docs/img/overbit-match.mp4`
  e la GIF). Dev kit: F6 la **telecamera libera**.
- Strumenti: `tests/bm/mapbench.c` (la mappa da un punto di vista, per armprof), bmhost
  compilato per ARM Linux sotto QEMU (il profilo di un frame intero, Lua compreso),
  `BMHOST_SLOW=ms` (i frame lenti); `build.py --define` per le regole dei test.
- Test: `make test-overbit` gioca una partita dal menu (Rally, poi Kaiju nello spawn, il
  punto si apre) e una partita veloce intera di bot (cattura, uccisioni, una squadra vince).
- Stime (armprof: istruzioni pesate con i costi dell'ARM1176; il Pi di solito è un 30% più
  lento): la vista della strada ~6 M cicli per la sola mappa; una scaramuccia sul punto
  con dieci eroi in vista, qualità 2: ~14 M cicli a frame (la parte per vertice e per
  faccia degli eroi 3,7 M, Gouraud 2,6 M, Lua 2 M), cioè ~18 ms sul Pi: lì la qualità
  automatica scende. Il prossimo da ottimizzare è la parte per vertice e per faccia degli
  eroi. Una toppa alla VM di Lua (il contatore del watchdog senza una chiamata per
  istruzione, `third_party/lua/lvm.c`) ha tolto più di metà del tempo del Lua.
- **Da provare sul Pi**: "PLAY: CONTROL" nel menu; con Select (o Tab) l'overlay: una foto
  dei ms nella strada, nella piazza durante uno scontro e dallo spawn.

**Passo 4 — l'AI dei bot: fatto sul PC (2026-10-03), da provare sul Pi.**
- **Tre livelli** (`src/75_bots.lua`): la *tattica* due volte al secondo (andare al punto,
  combattere, ritirarsi, aggirare da un fianco, proteggere un amico, aspettare), la
  *strada* sul grafo dei posti della mappa (il prossimo nodo verso la meta, calcolato una
  volta per meta: il punto, lo spawn di ogni squadra, i tre fianchi), le *mani*: vedere
  (raggi verso i corpi), mirare con un tempo di reazione e un errore che vaga e cresce con
  la distanza, anticipare i colpi lenti, scansare di lato, e le abilità di ogni eroe
  quando hanno senso (il Null Field sotto il fuoco, la barriera di Kaiju con gli amici
  dietro, il muro di Frost per ritirarsi, la mina di Fuse fatta esplodere, i kunai di
  Akari a ripetizione, l'ultimate con più nemici vicini...).
- **La nostra rete** (come quella dell'assistente di M30): 24 numeri che il bot vede
  (vita, ultimate, ruolo, distanza e stato del punto, percentuali, quanti amici e nemici,
  il nemico più vicino, l'amico più ferito, il danno appena preso...) → 32 → 32 → 6
  tattiche, pesi INT8. Nel kernel `nnet()` (`src/ai/net.c`: gli strati di `nn.c` con le
  istruzioni SIMD dell'ARMv6) la fa girare per ogni cartuccia; `scripts/nnetlib.py` la
  quantizza e dà i numeri esatti della console (test: uguali bit per bit).
- **Addestrata giocando** (`art/brain.py`): partite di dieci bot sul PC senza disegno
  (bmhost, 4 partite per volta, ~3 s l'una), ogni scelta con quello che è successo dopo
  (uccisioni, morti, danni fatti e cure, la cattura e le percentuali della squadra, 15
  secondi scontati). Una rete del valore impara quanto rende un momento; la rete delle
  tattiche impara le scelte fatte, ognuna pesata da quanto è andata meglio del previsto
  (*advantage-weighted regression*: resta vicina a quello che si è provato). La
  generazione 0 sceglie con le regole (e a caso una volta su cinque), le altre con la rete:
  cinque generazioni, 200 partite, 660 mila scelte, 17 minuti sul PC. **Risultato
  onesto**: contro le regole la rete vince 21 partite e ne perde 19 su 40, cioè è alla
  pari (la prima prova, che stimava il valore di ogni tattica con i premi del singolo bot,
  perdeva 5 a 12). È nella cartuccia (`src/76_brain.lua`, 2 KB) e sceglie le tattiche di
  tutti i bot; le regole restano come riserva. Per farla crescere: più generazioni, più
  ingressi (dove sono gli amici, le ultimate dei nemici), regole migliori da cui partire.
- Tre livelli di bravura nel menu (**BOTS**: EASY, NORMAL, HARD: reazione 0,5/0,3/0,17 s,
  errore di mira, velocità di rotazione; i difficili saltano per schivare). Le stanze di
  spawn curano la propria squadra. Nel log di una partita di prova (`OVERBIT_AI_LOG`)
  ogni scelta con quello che il bot vedeva.
- Test: `make test-overbit` (una partita intera di bot fino alla vittoria; la rete in C
  uguale a quella in Python).

**Passo 5 — la rete: fatto sul PC (2026-10-03), da provare sul Pi.**
- **UDP per le cartucce** nel kernel (`src/net/cartnet.c` su lwIP): `udp_open`,
  `udp_send` (anche `"*"`, il broadcast della LAN), `udp_recv`, `udp_close`, `net_ip`,
  `net_resolve`; in bmhost le stesse funzioni sui socket del PC (`tests/host/hostnet.c`,
  `BMHOST_NET_ID` per più console sullo stesso PC, `--realtime` a 60 frame al secondo).
- **Lockstep** (`src/83_net.lua`): ogni console fa girare tutta la partita (lo stesso
  codice, lo stesso seme, anche i bot) e viaggiano solo i comandi dei giocatori: ogni
  console manda i suoi per il frame 4 più avanti (7 con il relay), l'host raccoglie quelli
  di tutti e rimanda per ogni frame il pacchetto di tutti; un frame gira quando il suo
  pacchetto è arrivato. Ogni pacchetto ripete gli ultimi 8 frame (le perdite di UDP non
  contano). La visuale gira subito, l'eroe segue di 4 frame (67 ms). Ogni secondo un
  hash della partita: se due console divergono si vede e si scrive nel log. Per il
  lockstep il gioco ha un suo generatore di numeri a caso (`grandom`, quello degli effetti
  resta a parte), l'orologio e gli id ripartono da zero a ogni partita, i bot contano i
  frame della partita.
- **PLAY ONLINE** nel menu: ospitare una partita o entrare in una di quelle trovate sulla
  LAN; fino a 10 persone, alternate tra blu e rossi, i posti vuoti ai bot; la scelta
  dell'eroe viaggia con i comandi; chi se ne va (3 s di silenzio) lascia il posto a un bot,
  lo stesso su tutte le console.
- **Via internet**: `tools/overbit_relay.py` su un PC o un server con un indirizzo
  pubblico (UDP 47310): passa i pacchetti di una console alle altre della stessa stanza,
  come un broadcast. Nella console: RELAY (indirizzo, anche `nome:porta`) e ROOM (4
  lettere), scritti con la tastiera o con il pad e salvati.
- **PS nella partita in rete** (decisione dell'utente, 2026-10-04): il gioco non si
  sospende. `online(true)` del kernel: PS (Ctrl+Esc, Start+Select) chiede solo a chi esce,
  sulla sua console, "Leave the match?" (uscirà dal gioco e si disconnetterà dal server;
  all'host anche "the match ends for all"), sopra la partita che va avanti senza i suoi
  tasti; sì chiama `_leave()`: il "Q" porta il posto, l'host lo dà subito a un bot (non dopo
  3 s), e se esce l'host la partita finisce per tutti; indietro resta.
- Test: `make test-overbit` fa giocare due bmhost insieme, sulla LAN e attraverso il
  relay: le stesse uccisioni, catture e round su entrambi, nessuna divergenza; poi l'ospite
  esce con PS e l'host lo sente. `make test-online` (bmhost) e `test_online_leave` (QEMU)
  provano la domanda.
- **Da provare sul Pi**: due console sulla stessa WiFi, PLAY ONLINE: una HOST A MATCH,
  l'altra JOIN, poi START; una foto se compare "OUT OF SYNC" o "WAITING FOR THE OTHERS";
  poi PS sull'ospite: la domanda solo lì, X esce, sull'host un bot prende il posto.

**Risoluzione 480×270: fatto sul PC (2026-10-03, branch `claude/overbit-480`), da provare
sul Pi.**
- Il primo benchmark sul Pi (foto dell'autore, a 320×180): partita di 10 bot a 22–27 fps
  con l'ARM e 29–37 con la GPU, Lua 7,2–7,7 ms, 3D 14–21 ms con la GPU e 25–34 con l'ARM;
  nessuna qualità a 60 fps; anello di eroi: 5 a 60 fps con l'ARM, 6 con la GPU. Con la GPU
  la colonna PX è 0: i pixel non costano all'ARM, il limite sono il Lua e i vertici.
- L'immagine era **sgranata**: 320×180 su 1080p fa ogni pixel 6×6 (e un monitor 21:9
  che allarga il 16:9 lo rende anche irregolare). Ora **480×270**: 4×4 su 1080p, 2,25
  volte i pixel, la risoluzione del formato pensata per il 3D (`OVERBIT_RES` nel
  `Makefile`, anche per i reel e `tests/overbit/frames.py`).
- Il 2D usa `SW`, `SH` di `00_core.lua`: l'HUD resta alle stesse misure in pixel (più fine
  sullo schermo), ancorato ai bordi, con la barra della vita fino a 160 px; le cose del
  mondo disegnate in 2D (sole, nuvole, bande del cielo, lampi delle armi, freccia del
  danno) crescono con `ZOOM` (1,5). Menu, selezione degli eroi, tabellone, reel, lobby e
  le sovrapposizioni degli eroi (visore, ghiaccio, ruota, energia, aggancio, kunai) rifatti
  per lo schermo intero; il menu non si sovrappone più alla scritta della build.
- **Benchmark** in 2 pagine invece di 5: tempi del fotogramma e lavoro per fotogramma
  nella stessa tabella (fino a 18 righe a pagina), poi anello e driver.
- Costo previsto: con la GPU l'ARM disegna in più solo il cielo 2D (0,26 MB a fotogramma
  invece di 0,12) e la GPU riempie 40 tile invece di 15; con l'ARM i pixel sono circa 2,3
  volte (bmhost, la stessa scena: 122.578 → 280.661).
- **Da provare sul Pi**: il benchmark di nuovo (foto delle due pagine, per confrontare i
  ms con quelli di 320×180) e un giudizio sulla nitidezza in partita.

**La risoluzione nel gioco, fino a 1080p: fatto sul PC (2026-10-04, branch
`claude/overbit-480`), da provare sul Pi.** Richiesta dell'autore: sceglierla in gioco.
- Kernel: **`screen(w, h)`** cambia la risoluzione della cartuccia tra un fotogramma e
  l'altro (`screen_apply` in `runtime.c`: framebuffer di nuovo, z-buffer con
  `r3d_resize`, buffer della luce e delle dissolvenze, puntatore, `SCREEN_W`/`SCREEN_H`).
  Modi 16:9: 320×180, 384×216, 480×270, 640×360, 960×540, 1280×720, 1920×1080 (pixel
  interi su un TV 1080p tranne 1280×720). `prompt()` ha uno zoom come `print`.
- GPU (**bm3d 4.2**): buffer dei lavori per 1080p anche con l'MSAA, guard band che si
  stringe sugli schermi larghi (gli angoli in 12.4 restano sotto 2048 pixel), e il
  `cls()` lo fa il lavoro della GPU invece dell'ARM (4 MB a fotogramma a 1080p).
- Overbit: voce **RESOLUTION** nel menu (frecce o A/spazio), salvata sulla SD e rimessa
  all'avvio; con l'ARM fino a 640×360 (paga ogni pixel; passando all'ARM da una
  risoluzione più alta torna a 640×360, e il benchmark lì salta l'ARM). HUD e menu su uno
  schermo logico di `LW`×`LH` ingrandito `UI` volte (2 a 960×540 e 1280×720, 4 a 1080p:
  sul TV grandi come a 480×270), con versioni compatte di menu e selezione a 320×180 e
  384×216. Con la GPU il cielo è in 3D (una parete davanti alla camera girata con lei, le
  bande alle altezze di quelle 2D per ogni campo visivo; sole con `point3d`, nuvole come
  quadrati): niente 2D prima del 3D, così la GPU pulisce la pagina invece di rileggerla.
- Test: `test_screen_modes` in QEMU (640×360 → 1920×1080 → 320×180 → 960×540 sul kernel
  vero), `make test-overbit` (cambio dal menu, salvataggio, ARM limitato, poligono a
  1920×1080 sull'emulatore della V3D), i test della GPU (la tabella 1080p combacia pixel
  per pixel tra ARM ed emulatore).
- **Da provare sul Pi**: RESOLUTION a 960×540 e 1920×1080 con il renderer GPU+VS+Q (il
  migliore per gli schermi grandi), una foto dell'overlay (Select) in partita a ciascuna
  e il benchmark a 1080p. Il costo che resta: la GPU scrive la pagina (4 MB a 1080p) e
  riempie 510 tile, l'ARM disegna l'HUD ingrandito.
- **Sul Pi (2026-10-05, v0.2.3, renderer GPU: il vertex shader è spento, M36).** Il
  benchmark a 1920×1080 (report `overbit-bench`): GPU MEDIUM 22 fps (44,9 ms: update 7,7,
  3D 22,0; 4164 triangoli), HIGH 20, ULTRA 19, EXTREME 18 (56,5 ms, 3D 29,3; 5244
  triangoli); con l'MSAA 2–3 fps in meno. Anello di eroi: 1 a 60 fps, 8 a 30 (un eroe solo
  14,8 ms, di cui 3,5 di 3D). Le foto dell'overlay in partita (10 attori):
  - 480×270, LOW auto: sul punto 44 fps (23,2 ms: update 5,8, 3D 13,3; 3620 triangoli);
    dallo spawn verso la strada 41 fps (3D 13,8; 4091 triangoli);
  - 960×540: sul punto, EXTREME, 28 fps (33,7 ms: update 6,4, 3D 18,1; 3621 triangoli);
    nella strada 25 fps (40,4 ms);
  - 1920×1080, LOW auto: dallo spawn 22 fps (40,0 ms: update 8,6, 3D 25,9; 4083 triangoli).

  Con gli stessi triangoli il 3D passa da 13 ms a 480×270 a 26 ms a 1080p: senza la coda
  l'ARM aspetta la GPU, che a 1080p riempie 510 tile e scrive 4 MB. Il Lua del gioco resta
  6–9 ms. A 480×270 la partita va a 41–44 fps (il primo benchmark a 320×180 dava 29–37 fps
  con la GPU); nessuna risoluzione arriva a 60 fps. Servono il vertex shader (M36) e la coda
  (M35), poi M39.
- **Il tempo della partita e il dev kit (2026-10-05, richiesta dell'utente).** Sotto i 60 fps
  Overbit andava al rallentatore: ogni `_update` muove la partita di 1/60 s e ne girava uno a
  fotogramma (a 22 fps il 37% della velocità). Ora `frameskip(4)` (API nuova del runtime):
  fino a 4 `_update` prima di ogni `_draw`, la partita al tempo vero fino a 15 fotogrammi
  disegnati al secondo; il benchmark resta a uno. L'overlay di Overbit è tolto: quello del
  sistema ha una pagina dettagliata (F11 due volte, o Select, Tab, F1 nel gioco) con le fasi
  del fotogramma, la GPU e le righe di Overbit (`devinfo`: qualità, regolatore, attori).

**Chiusa il 2026-10-05** (decisione dell'utente): Overbit c'è tutto (8 eroi con i modelli
Meshy, la mappa Partenope e la modalità Controllo, i bot con la rete INT8, la partita in rete
in lockstep, la risoluzione da 320×180 a 1920×1080, il benchmark) e gira sul Pi con la GPU:
41–44 fps in partita a 480×270, 22 fps a 1080p, la partita al tempo vero sotto i 60 fps
(`frameskip`). Restano fuori: i 60 fps, che vanno ai driver (vertex shader M36, coda M35,
poi M39); la partita in rete tra due console provata sul Pi; Overbit in `.b16` sulla RGB30
(M41).

## M39 — GPU 3: verso il limite della V3D (L/XL)
Dove siamo (2026-10-03, stime dal PC per le versioni 3.0–4.1): il riempimento è all'80%
di quello dichiarato (811 Mpixel/s misurati sul Pi su 1 Gpixel/s), il lavoro dell'ARM per
triangolo vicino al minimo (~65 istruzioni con il vertex shader), ma i triangoli della
GPU sono a ~3 milioni al secondo contro i 16 milioni visti in un demo reale sulla stessa
GPU (con texture e luce, 1280×720) e i 25 milioni dichiarati da Broadcom. La distanza
sta in come i vertici arrivano alla V3D e in come ARM e GPU si passano il lavoro. Driver
**bm3d 5.x**; ogni passo è un'opzione con una prova all'avvio dove la V3D fa qualcosa che
il Pi non ha ancora mostrato, come in M34–M36.
1. **Misure di partenza sul Pi.** Il 3D Bench con tutti i profili (ARM 0.2, GPU 2.1,
   GPU+VS1 3.0, GPU+VS 3.4, GPU+VS+Q 4.1), i passi 14 e 15 del test `g`, il benchmark di
   Overbit con GPU+VS e GPU+VS+Q: le versioni 3.0–4.1 oggi sono stime. Un test nuovo del
   3D Bench, `big`: due modelli da 10 000 triangoli e una mappa da 14 000, con e senza
   logica del gioco (la domanda "regge 34 000 triangoli?").
2. **Mesh indicizzate e attributi compatti.** Oggi ogni faccia porta i suoi tre angoli in
   virgola mobile (44–56 byte l'uno): la GPU calcola ogni vertice circa tre volte. Vertici
   condivisi dove le facce lo permettono (normali morbide, stessa texture: le figure
   Meshy), primitive indicizzate a 16 bit, gli angoli in un ordine che usi la cache dei
   vertici della V3D (al build, come meshoptimizer), posizioni separate dagli altri
   attributi (lo shader di coordinate legge solo quelle), attributi a 16 e 8 bit
   (posizioni scalate, normali e colori a 8 bit, uv a 16). L'emulatore esegue gli stessi
   shader con gli indici.
3. **Più vertici per mesh.** Il limite di 4096 vertici (`MAX_VERTS` di r3d, `mesh()`, i
   modelli del `.bm`) sale (16 384 o 65 535): un modello da 10 000 triangoli oggi va
   spezzato.
4. **Due lavori della GPU in volo.** Due set di memorie (liste, record, uniform, angoli
   del fotogramma) alternati: l'ARM prepara il 3D del fotogramma dopo mentre la GPU disegna
   quello di adesso, e il fotogramma dura quanto il più lento dei due invece della somma.
   Il resto (texture e mesh cambiate mentre un lavoro le legge) aspetta come oggi.
5. **Texture più leggere.** Formati a 16 bit (RGB565, RGBA4444/5551 per i ritagli) ed
   ETC1 compresso (4 bit a pixel, fatto al build dagli strumenti del PC), mipmap per le
   superfici lontane (meno texel letti, meno sfarfallio). L'ARM resta sulle texture di
   oggi; la prova all'avvio controlla come la TMU legge ogni formato.
6. **Fragment shader a due thread.** Lo shader cede il QPU all'altro thread mentre aspetta
   la texture (`thrsw`, il flag del record per gli shader a più thread, metà dei registri
   per thread): nasconde l'attesa della TMU come fa Mesa. L'emulatore controlla le regole
   (registri, segnali, posizione del cambio).
7. **Ordine di disegno.** I modelli opachi dal più vicino al più lontano (la V3D scarta
   prima i pixel nascosti con lo z anticipato) e i disegni raggruppati per shader e
   texture (meno cambi di stato nella lista): dentro il lavoro della GPU, senza cambiare
   cosa vede la cartuccia, o con un aiuto per le cartucce (`sort3d`).
8. **Occlusione anche per gli attori.** La visibilità precalcolata della mappa (PVS) oggi
   scarta i pezzi di mappa; la stessa prova per eroi, effetti e oggetti (un aiuto in C,
   `visible3d(x, y, z, r)`, usato da Overbit).
9. **Memoria dei lavori senza cache** (il passo 2 di M35): liste e angoli scritti
   dall'ARM senza passare dalla cache, se il Pi mostra che conviene.
10. **Il Lua dei giochi.** In Overbit il Lua è ormai circa il 60% del tempo dell'ARM a
    fotogramma: le parti calde in C (raggi e collisioni, strade dei bot, particelle), meno
    allocazioni per fotogramma (meno lavoro del garbage collector), `frames.py` per
    misurare. Uno studio a parte su LuaJIT (supporta l'ARMv6): quanto darebbe e cosa
    costerebbe portarlo sul bare metal.
- Opzioni: ognuna in *Settings > Graphics* o in `bm/config.txt`, spenta finché il Pi non
  la verifica; i renderer di Overbit e i profili del 3D Bench le confrontano.
- **Fatto quando:** sul Pi il test `big` regge almeno il doppio dei triangoli a 60 fps di
  bm3d 4.1, il test `spheres` del 3D Bench almeno 3× quelli di 3.4, e Overbit (GPU+VS+Q)
  sta nei 60 fps a HIGH nello scontro di 10 bot.
- Escluso (decisione dell'utente): impostor per gli oggetti lontani, cambio di
  risoluzione, overclock della GPU e dell'ARM. (La risoluzione scelta nel menu dei giochi
  c'è dal 2026-10-04, M38: qui si intende cambiarla da sola per guadagnare fotogrammi.)

**Misure di partenza sul Pi (2026-10-05, kernel v0.2.0, report `bench3d`, copia in
`docs/bench/`): solo ARM 0.2, GPU 2.1 e GPU+AA**, perché i profili col vertex shader mancano
(M36). Carico a 60 fps, ARM / GPU:
- sfere: `spheres` 72 / 209 (GPU+AA 206), `spheres_smooth` 22 / 163, `spheres_tex` <1 (54 a
  30 fps) / 166, `spheres_unlit` 78 / 244, `spheres_baked` 26 / 195, `spheres_shine` 21 / 113;
- eroi: `heroes` 3,9 / 6,4, `heroes_tex` 3,5 / 6,0, `heroes_skin` 3,4 / 6,0,
  `heroes_shadow` <1 / 3,4;
- `clip` 18 / 827, `tiny` 14 / 20, `draws` 453 / 1229;
- riempimento: `quad_flat` 12 / 203, `quad_smooth` 6 / 200, `quad_tex` 3,4 / 121,
  `quad_alpha` 3,5 / 115, `quad_screen` 14 / 184;
- `texswap` **89 / 7,8**, `split` **14 / 5,6**, `match` <1 / <1 (a 30 fps 1,5 / 5,3).

Con la GPU l'ARM resta il limite dove i vertici sono tanti (`heroes`, `match`: 5–6 milioni di
istruzioni a fotogramma). Due casi vanno peggio che sull'ARM: `texswap` (tre texture a turno:
la GPU ne tiene due e la terza chiude il lavoro) e `split` (ogni 2D tra due 3D chiude il
lavoro: 5 lavori a fotogramma). Si aggiungono ai passi: più texture in un lavoro, e meno
lavori quando 3D e 2D si alternano (M35, passo 3). Il report sulla SD fa da "report di prima"
per il prossimo giro.

## M40 — bm per PowKiddy RGB30 (XL) — ✅ chiusa (2026-10-04)
**Chiusa dall'utente il 2026-10-04** (nata nel branch `rgb30-powkiddy`, unita a `bm-core`):
il lavoro sulla RGB30 continua su `bm-core` con il resto di bm.

Decisione 2026-10-01 (utente): una versione **bare metal** di bm per la PowKiddy RGB30
(Rockchip RK3566, 4 × Cortex-A55 a 64 bit, 1 GiB, schermo 720×720 MIPI-DSI, RTL8821CS).
Menu **512×512** al centro dello schermo (formato 1:1; dal 2026-10-03 360×360 ×2, a tutto
schermo, task 8); giochi e app
in un formato nuovo, **`.b16`** (fino al 2026-10-04 `.s16`; la cartuccia a risorse limitate per
le console portatili, [B16.md](B16.md)), da definire; le cartucce `.bm` del Pi nascoste (`show_bm=1` in
`bm/config.txt` le elenca soltanto; dal 2026-10-03, per le prove, visibili e avviabili senza
impostazioni, `show_bm=0` le nasconde). Tutto in [RGB30.md](RGB30.md).

Task:
1. ✅ **Base a 64 bit** (`make TARGET=rgb30`, `rgb30.mk`, `src/rgb30/`): avvio come Image
   arm64 da U-Boot (EL2 → EL1, si sposta a 0x10000000), MMU, cache, GICv3, timer generico,
   eccezioni, picolibc, Lua; la stessa base gira nella macchina virt di QEMU, con i test
   (`make TARGET=rgb30 test`).
2. ✅ **Scheda SD** (`make TARGET=rgb30 firmware image`): bootloader di ROCKNIX (U-Boot 2026.01,
   scaricato e controllato), FAT32 "BM" con extlinux e `kernel8.img`; avvio verificato in QEMU
   attraverso lo stesso U-Boot.
3. **Schermo**: VOP2 → DSI0 → D-PHY → pannello ST7703 + retroilluminazione PWM4 (scritto, da
   provare sulla console); LED come segnale; `bm/bootlog.txt` scritto sulla SD a ogni avvio.
   Modalità video pronte per la GPU Mali (richiesta 2026-10-03): framebuffer con il layout delle
   destinazioni di rendering della GPU in 64 MiB di memoria video e GPU, ingrandimento del
   controller video (720×720 1:1, 360×360 ×2, 240×240 ×3), pagina *Display* di prova.
4. **Comandi, SD, menu**: tasti GPIO, levette (SARADC + commutatore), SDMMC0 in PIO, PMIC RK817
   (spegnimento, batteria), menu 512×512 con `.b16` e strumenti (in QEMU il menu e la FAT sono
   provati; sulla console i tasti vanno, 2026-10-03, e l'asse verticale delle levette è stato
   girato).
5. **Bluetooth**: RTL8821CS su UART1 con H5 (trasporto in `src/bt/h5.c`, già sotto lo stack
   HCI) e firmware Realtek (`rtl8821cs_fw.bin` + config), controller e tastiere con lo stack di
   M12/M28.
6. **WiFi**: RTL8821CS su SDIO (sdmmc2): driver come rtw88 (GPL-2.0 OR BSD-3-Clause), 4-way
   handshake WPA2 nell'host, lo stack di rete di M18 (scritti: scansione, reti aperte e WPA2-PSK,
   chiavi nella CAM, lwIP; provati sul PC su un chip e access point simulati; sulla console da
   provare).
7. **Cartucce del Pi per le prove** (richiesta 2026-10-03): il runtime `.bm` compilato a 64 bit
   (sostituti dei driver del Pi in `src/rgb30/bm_port.c`, comandi in `bm_input.c`), avviato dal
   menu (visibili per le prove; `show_bm=0` le nasconde); Yharnam (256×256, dal branch `claude/yharnam`) nell'immagine SD,
   ingrandita a tutto schermo. Provata in QEMU; il suono e la GPU Mali mancano ancora.
8. ✅ **Menu a tutto schermo con le schede** (richiesta 2026-10-03): 360×360 ingrandito ×2 sul
   pannello 720×720; schede Games / Dev / System come sul Pi (L1/R1); in Dev il **3D Bench**
   (`src/bm/b3d.c` con i contatori del Cortex-A55, rapporto in `bm/bench`), Render bench,
   Display, Input test, Boot log, Lua. Provato in QEMU (`test_bench3d` e gli altri).
9. ✅ (QEMU) **Aggiornamenti da GitHub e kernel dalla rete** (richiesta 2026-10-04): *System >
   Updates* come Settings > System sul Pi (`src/kernel/update.c` con HTTPS, `release.c` e la
   chiave delle release nel kernel della RGB30); la release ha un manifesto suo,
   `manifest-rgb30.txt` firmato (`kernel8.img`, `bm/ca.pem`: `make release RGB30_KERNEL=...`, la
   CI sui tag lo fa); il kernel è riconosciuto dall'intestazione arm64 (`ARM\x64` a +56).
   `bm_net.py --kernel` scrive `kernel8.img` sulla RGB30 (prima `kernel.img`, che U-Boot non
   avvia), e un Pi rifiuta un `kernel8.img`. Prova: `test_update_from_sd` in QEMU; sulla
   console da verificare via WiFi.
10. ✅ (QEMU) **Lo stesso menu del Pi** (richiesta 2026-10-04, branch `bm-core`): `src/kernel/menu_ui.c`
   a 360×360 ×2, due copertine per riga, schede Games / Dev e Settings col suo pannello (al
   posto della scheda System), copertine dei `.bm` e degli strumenti come sul Pi, suggerimenti
   coi tasti del controller (B conferma, A indietro). Le pagine dietro le voci restano sulla
   console di testo. Prove: tutto `tests/rgb30/qemu_test.py`; sulla console da vedere.
- **Fatto quando:** sulla RGB30 il menu appare, i tasti e le levette rispondono, un controller
  Bluetooth si accoppia e la console entra nella rete WiFi salvata in `bm/config.txt`.

**Sulla console (2026-10-05, kernel v0.2.1).** Il WiFi funziona: RTL8821CS, WPA2 (CCMP),
DHCP, ora dalla rete, la console sulla porta 3333 (report `log`); i report arrivano su GitHub
via HTTPS. 3D Bench solo ARM (Cortex-A55; copia in `docs/bench/`), carico a 60 fps contro
l'ARM del Pi Zero W: `spheres` 180 (72), `spheres_smooth` 100 (22), `heroes` 8,6 (3,9),
`heroes_shadow` 2,3 (<1), `clip` 450 (18), `draws` 900 (453), `quad_flat` 16 (12),
`quad_tex` 8,0 (3,4), `match` 1,3 (<1; 8,1 a 30 fps). Render bench: mappa e 256 sprite 4,90
ms disegnando diretto, 7,30 ms via RAM (sul Pi 8,58 e 11,85).

**Dopo la chiusura (2026-10-05, branch `claude/rgb30-market`).** La scheda Market (M25, passo
8) e la **batteria sulla barra** (richiesta dell'utente): quattro tacche dal 75%, rossa sotto il
10%, il fulmine sul caricatore (sulla barra della RGB30 solo WiFi e batteria: niente icone di
controller, mouse e tastiere, decisione dell'utente), la carica dalla tensione del RK817 (`battery_percent`) anche in
*Settings > System*; le schede non passano più sotto le icone. Prove: `test_battery_icon`
(QEMU, `test_battery=` in `bm/config.txt`); sulla console da vedere.

## M41 — RGB30: la GPU Mali e Overbit in `.b16` (XL)
Richiesta dell'utente (2026-10-05): lo stesso banco di prova della GPU del Pi anche sulla
RGB30, con Overbit come gioco di misura, sviluppato insieme al driver della sua GPU
(Mali-G52, Bifrost). Oggi sulla RGB30 il 3D lo fa l'ARM (Cortex-A55: nel 3D Bench 2,5 volte
le sfere dell'ARM del Pi, M40) e il 3D Bench ha le colonne GPU vuote.
1. **Overbit in `.b16`**: per ora un `.bm` con l'estensione diversa (il formato vero è da
   definire, `docs/B16.md`), nell'immagine della RGB30. Due risoluzioni quadrate, scelte
   nel menu RESOLUTION come quelle 16:9 sul Pi: **360×360** (ingrandita ×2 dal controller
   video, come il menu) e **720×720** (il pannello pixel per pixel). Servono `screen()` con
   i modi quadrati della RGB30 (righe a 64 byte e tessere da 16 della memoria video,
   `fb_init_mode`), e in Overbit HUD, menu, cielo e campo visivo per lo schermo 1:1 (`SW`,
   `SH`, `LW`×`LH` e `UI` di `00_core.lua`).
2. **Le misure con l'ARM**: il benchmark di Overbit (bot e anello di eroi) alle due
   risoluzioni, con il report come sul Pi; il 3D Bench resta quello di oggi.
3. **Il driver Mali, a passi come M33–M36**: prima i triangoli preparati dall'ARM e
   disegnati dalla GPU, poi i vertici sulla GPU (vertex shader), poi la coda; per ogni
   passo una prova all'avvio che spegne ciò che non torna, un profilo nel 3D Bench e un
   renderer in Overbit, misurati con le stesse due risoluzioni.
- **Fatto quando:** sulla RGB30 Overbit `.b16` gira a 360×360 e a 720×720 e il suo
  benchmark manda il report, prima con l'ARM e poi con la GPU Mali; il 3D Bench della
  RGB30 ha le righe GPU.

## M42 — Il profilo `.b16` (L)
Decisioni dell'utente del 2026-10-05, in [B16.md](B16.md) §0: un ambiente limitato come
PICO-8, uguale sul Pi e sulla RGB30. Lo stesso contenitore dei `.bm` con il campo del
profilo; schermo 360×360 o 720×720 fisso; 256 colori; Lua 4 MiB, grafica a banchi da
1024×1024, 8 voci, salvataggio 64 KiB; CPU a budget fisso (istruzioni Lua e costo dei
disegni) a 60 fps che scende da sola a 30; 3D con un tetto di triangoli; pad stile SNES;
niente file, `rnd()` con seme; 8 MiB; nessun limite di token.
1. Il campo del profilo nel contenitore, `mkbm.py --b16`, i controlli (lettore e
   impacchettatore) e il menu: il Pi mostra `.bm` e `.b16`, la RGB30 solo `.b16`.
2. Il runtime nel profilo: schermo fisso, tavolozza, memoria, sandbox, banchi grafici.
3. La CPU a budget: costi dei disegni, misura sul Pi, 60 → 30 fps da soli, la percentuale nel
   dev kit.
4. L'SDK: il target `.b16` salva un `.b16` vero (oggi solo i promemoria del dev kit, §8.5).
5. Yharnam in `.b16` (360×360, sheet a banchi), poi Overbit (M41).
- **Fatto quando:** Yharnam `.b16` gira uguale sul Pi e sulla RGB30 nel profilo, con la CPU
  che scende a 30 fps negli stessi punti sulle due console.

## M43 — Sprite stacking (L)
Dallo spunto R24 (richiesta dell'utente, 2026-10-06; era 22.7 di M22). Un oggetto è una
pila di fette 2D (una per altezza, come i layer di Aseprite) disegnate una sopra l'altra con
un piccolo scarto e ruotate: l'illusione di un volume 3D con il costo del 2D (auto viste
dall'alto, case, alberi, personaggi in giochi top-down). Due costi: solo la rotazione
attorno a z (lo stacking classico, N fette ruotate) e x, y, z libere (un volume di voxel,
più caro).
1. **Lo stacking classico nel runtime**: `stack(sx, sy, w, h, n, x, y, [rz, scala, passo])`,
   le `n` fette in fila nello sheet a partire da `(sx, sy)` (a destra, poi sotto), ruotate di
   `rz` attorno al centro e alzate di `passo` pixel l'una sull'altra; in C (`gfx16.c`: una
   fetta ruotata è un `sspr` campionato all'indietro, il colore 0 trasparente), anche nella
   coda del 2D (`draw2d()`) e in bmhost. Misura sul Pi: quante pile da 16 fette 16×16 in un
   fotogramma a 60 fps (report con un test del dev kit).
2. **Le pile dello sheet con il nome**: una zona di SPRITES con il tipo `stack` e il numero
   di fette (`sprites.txt`, `mkbm.py --sprites`, `bmres.py`), dai giochi `zstack(nome, x, y,
   [rz, scala])`; le pile nella scheda Lib (l'anteprima che gira) e nei `.bmi`.
3. **L'editor in bm Pixel**: una pagina *Stack* con le fette in griglia e sovrapposte, la
   fetta sotto e quella sopra in trasparenza (onion skin), l'anteprima che gira (tasti per
   l'angolo e il passo), copia di una fetta nella successiva, nuova pila da una zona.
4. **x, y, z libere**: `stackv(...)` con `rx`, `ry`, `rz` disegna la pila come un volume di
   voxel (fette rifatte lungo l'asse più vicino alla camera, ordinate da dietro in avanti);
   più caro, la misura come al passo 1.
5. **Verso gli altri strumenti**: una pila diventa MESH (le facce visibili dei voxel unite,
   con i colori dello sheet) per bm Studio e bm Mesh, e fotogrammi in 8 direzioni (bm
   Animator, pagina sprites) per chi non vuole ruotare nel gioco.
6. **Documentazione e esempi**: i quattro file delle API e la base dell'assistente; un
   modello dell'SDK *Top-down stack* (un'auto che gira e qualche casa); bmlib senza
   novità se non servono.
- Prove: bmhost (fotogrammi confrontati con un riferimento in Python), `make test-res` per le
  zone `stack`, QEMU con un gioco di prova e bm Pixel.
- **Fatto quando:** un gioco top-down con decine di pile che girano va a 60 fps sul Pi, le
  pile si disegnano e si modificano in bm Pixel e diventano modelli 3D e fotogrammi.

## M44 — La GPU come coprocessore: programmi sulle QPU (L/XL)
Dallo spunto R23 (richiesta dell'utente, 2026-10-06). Le 12 QPU della V3D fanno la stessa
operazione su 16 numeri alla volta: non il Lua né il codice pieno di scelte, ma i calcoli
uguali su tanti dati, quando il 3D non le usa (nei giochi 2D, nel menu) o con alcune QPU
riservate a questi programmi nei giochi 3D. Il più utile all'ARM lo fa già M36 (vertici e
ossa nel vertex shader).
1. **Il driver**: il lancio di un programma QPU "utente" fuori dal disegno (le richieste
   di programma della V3D: indirizzo del codice, uniform, numero di QPU, contatore dei
   completati; oggi `v3d.c` dà tutta la VPM ai vertici, come Linux), una coda di lavori,
   l'attesa con il timeout, la prova all'avvio che lo spegne se non torna e l'ARM come
   riserva per ogni programma; l'emulatore `tests/gpu/v3d_emu.c` che li esegue (interprete
   QPU di M36), un passo del test `g` del monitor e un profilo del 3D Bench.
2. **Particelle sulla GPU**: una funzione di sistema per emetterle (`particles()`: posizione,
   velocità, gravità, durata, colori), la GPU le muove e le disegna senza l'ARM; oggi sono
   in Lua e contate (Yharnam ne lascia 40 alle fiamme, Overbit le ha ottimizzate a mano):
   migliaia invece di centinaia, per tutti i giochi. Poi in bmlib (`lib.particles` sopra la
   funzione di sistema quando c'è).
3. **Effetti a schermo intero dei giochi 2D**: il buio a livelli e i bagliori di Yharnam
   (oggi `g16_fade_*`, pixel per pixel sull'ARM), dissolvenze, sfocature, un filtro CRT.
4. **Effetti audio** sul sintetizzatore (riverbero, eco, filtri) calcolati a blocchi, anche
   per nano8.
5. **Tanti raggi insieme** (i colpi contro le mesh degli eroi, `hit3d`; la visibilità dei
   bot): solo se servirà, oggi costano poco.

Non conviene per il Lua, CRC, SHA-256 e decompressione (sequenziali), il riduttore di
poligoni (pieno di scelte), la rete dei bot (24 ingressi, già in C), il menu a 1080p (meglio
lo scaler video HVS o M37). I programmi si scrivono con `tools/qpuasm.py` e si provano sul
PC con l'emulatore che esegue gli shader, come quelli di M36. Esempi esterni dello stesso
uso: GPU_FFT tra gli esempi del Raspberry Pi, QPULib, py-videocore (reti neurali sul Pi
Zero), VC4CL. Sulla RGB30 (Mali) gli stessi lavori aspettano il suo driver (M41): fino ad
allora l'ARM.
- **Fatto quando:** sul Pi le particelle di Yharnam e il suo buio a livelli li fa la GPU,
  con le misure del 3D Bench e di Yharnam prima e dopo nel report, e senza GPU (QEMU,
  `gpu3d=0`) tutto va come oggi sull'ARM.

## Rischi principali
| Rischio | Mitigazione |
|---------|-------------|
| Stack USB (M7B) molto complesso | Stack minimo scritto da zero (un dispositivo, HID); testato in QEMU |
| Prestazioni Lua su ARM1176 a 1 GHz | Cache attive (M3), bassa risoluzione, API di blit in C |
| Firmware closed-source che cambia comportamento | Fissare la versione con `FW_REF` |
| Test solo su hardware | QEMU raspi0 in CI + chainloader via seriale |
| Bluetooth (M12) senza emulatore | un solo controller di riferimento, tracce HCI registrate sul Pi per i test |
| Scrittura su SD (M11) che corrompe la scheda | test in QEMU con `fsck.vfat`, file di bm in una cartella dedicata |
| Split transactions e LAN951x (M29) senza emulatore | schema di USPi/Circle (provati sul Pi 1), chip simulato nei test sul PC, diagnostica a schermo (`y`, `E`) |
| Pi Zero 2 W (M31) senza emulatore | `raspi2b` per le periferiche, `virt` per l'avvio in HYP, scelte di firmware e pin come Linux e Raspberry Pi OS, diagnostica a schermo |
| GPU V3D (M33) senza emulatore e senza seriale | prova passo per passo sullo schermo (`g`), timeout su ogni attesa, emulatore della V3D per i test sul PC, rasterizzatore software come riserva (anche automatica) |

## Hardware consigliato per lo sviluppo
- Adattatore USB-seriale 3.3 V (**non 5 V**) su GPIO14/15 + GND
- Cavo mini-HDMI, alimentatore 5 V 2 A stabile
- Pulsanti o pad SNES + qualche resistenza per M7A
- Filtro RC (270 Ω + 33 nF) e jack per M10 (solo se l'audio va su PWM)
- Per M12: un controller Bluetooth di riferimento

---

## Spunti R1, R2, … (2026-10-03, da riprendere)
Cose utili che a bm mancano, viste sullo stato del branch principale del 2026-10-03,
escluso quello che è in sviluppo su altri branch (GPU e 3D M33–M37, Overbit e la rete UDP
dei giochi, Market e scambio in LAN M24–M26, RGB30, `.b16`). Nessuno è deciso: l'utente li
richiama per nome ("facciamo R7"), e allora si chiede il branch come per ogni sviluppo.
Suggeriti per primi: R3, R1 con R2, R7 (sul Pi si prova senza seriale e spesso senza
tastiera).

### Usare la console senza PC né seriale
- **R1 — WiFi dal menu.** Oggi la rete si sceglie solo dal monitor (`W`: elenco, numero,
  password scritta con la tastiera, `wifi.c`) o scrivendo `bm/config.txt`. Una pagina in
  Settings > WiFi and network: le reti trovate col segnale, la scelta, la password (R2),
  salvate come adesso (`wifi_ssid`, `wifi_psk`, `wifi_security`).
- **R2 — Tastiera a schermo.** Una griglia di lettere guidata dal pad, servizio del kernel
  chiamabile anche dalle cartucce (es. `textinput(titolo, testo)`): password del WiFi,
  nomi dei file, Market. La tastiera e la composizione ci sono già come libreria Lua
  (`require "padtype"`, M30 passo 18, 2026-10-05): manca il servizio del kernel per gli
  schermi in C (WiFi, nomi dei file).
- **R3 — Log su SD e visibile dal menu.** `log()`, i messaggi del kernel e il traceback
  dell'ultimo errore di una cartuccia vanno solo sulla seriale. Le ultime righe in
  `bm/log.txt` e una pagina "Log" in Settings > System: dal Pi si vede quello che oggi si
  vede solo in QEMU.
- **R4 — Screenshot sulla console.** Una combinazione di tasti salva un PNG in
  `bm/shots/` (segnalare problemi, copertine del Market). `src/bm/png.c` oggi legge
  soltanto: per scrivere basta il deflate senza compressione.
- **R5 — Aggiornamento dal menu.** È M19, passi 3–4: `release.c` controlla già manifesto
  e firma, mancano la chiave (`scripts/release-key.sh`) e la voce del menu; oggi per
  aggiornare si toglie la SD.
- **R6 — Pagina web della console.** Un piccolo server HTTP e il nome `bm.local` (mDNS;
  di lwIP oggi c'è solo SNTP): dal browser del telefono si carica un `.bm`, si scaricano
  salvataggi e screenshot, si modifica `bm/config.txt`, senza `bm_net.py`.

### API dei giochi
- **R7 — Lettere accentate in `print()`.** Il testo è disegnato byte per byte nell'ordine
  CP437 (`g16_text` in `gfx16.c`): "città" scritto in UTF-8 esce con due simboli
  sbagliati. Conversione da UTF-8 a CP437 (à è é ì ò ù ci sono); poi, se serve, font
  personalizzati dallo sheet.
- **R8 — Vibrazione e luce del DS4.** `rumble(p, forte, debole, ms)` e `padlight(p,
  colore)`: il report d'uscita del DS4 (0x11, `bt.c`) parte già, oggi solo per il colore
  del giocatore.
- **R9 — Suoni campionati (PCM/WAV)** nel banco, accanto alla sintesi: voci, batterie
  vere, effetti registrati; import WAV nel Sound editor. Il formato del banco cambia nei
  tre posti (`au_parse`, l'editor, `scripts/bmaudio.py`).
- ✅ **R10 — Libreria di gioco comune** (`require "bmlib"`): collisioni con la mappa e tra
  rettangoli, easing, particelle, camera che segue, macchina a stati. Oggi ogni gioco se
  le riscrive e l'assistente le spiega soltanto (`kb/howto_physics.txt`).
  **Fatto** (2026-10-04, branch `game-api`, in QEMU e sul PC): `src/script/bmlib.lua`, nel
  kernel come `bm3d` (anche in bmhost e nella build della RGB30). Dentro c'è quello che
  un'analisi delle cartucce ha trovato riscritto in 5–10 giochi: numeri (`clamp`, `lerp`,
  `approach`, `sign` con 0 per 0...), caso (`rnd`, `choose`, `shuffle`, `rng(seme)`
  xorshift per i mondi e la rete), collisioni tra rettangoli e cerchi e con la mappa per i
  flag delle tile (`lib.move` che scivola sui muri, `lib.step` per i platform con le
  piattaforme da sotto, `lib.ray` per la linea di vista), easing e tween, timer, script con
  `lib.wait`, particelle, camera con zona morta e tremolio, stati con push e pop, testo
  centrato e con ombra, barre, `lib.btnr` (btnp che si ripete), menu e il menu di pausa
  che 5 giochi copiavano, jingle, `lib.store`/`lib.best`, fotogrammi, colori e il
  costruttore 3D di Astro Wing. Tempi in secondi con `lib.update()` in `_update`. bm Mesh
  carica la vera bmlib (le mesh del costruttore si trovano). Prove: `make test-gameapi`
  (bmhost, 150 controlli, anche i tasti con uno script), QEMU `test_game_api`.
- ✅ **R11 — Flag delle tile e mappa a più livelli.** `fget`/`fset` (muro, acqua, scala) e
  livelli sopra e sotto il personaggio; oggi la mappa è un solo strato, da CSV.
  **Fatto** (2026-10-04, branch `game-api`): sezioni LAYERS (12, fino a 8 livelli con il
  nome; la MAP è il primo, un kernel di prima disegna quello) e FLAGS (13, 8 flag per cella
  dello sheet, letti per posto) in `src/bm/bm.h`; `map(..., livello, maschera)`,
  `mget`/`mset` con il livello, `fget`/`fset`, `mflags` (i flag sotto un rettangolo in
  pixel: le collisioni in una chiamata), `msize`, `mlayers`. `mkbm.py --map nome=file.csv`
  (ripetuto), `--flags`; nella build `map_<nome>.csv` con `layers_<gioco>`, `flags.csv`.
  L'SDK (dopo il suo aggiornamento): nella pagina della mappa `l` il livello dopo, Shift+L
  uno nuovo (anche *New map layer* nel menu), `o` solo quello, `c` i flag sopra la mappa;
  tasti `0`–`7` per i flag della cella nella pagina degli sprite;
  `cart_save`/`cart_load`/`cart_write(sheet=)` li portano; `scripts/bmres.py` estrae,
  integra e converte mappe con livelli e flag; la scheda Lib mostra tutti i livelli. Prove:
  QEMU `test_sdk_layers`, `make test-res`.
  **Insieme** (le altre API comuni che mancavano): le zone con nome dello sheet (SPRITES,
  che bmres.py e bm Pixel scrivono) si usano dai giochi: `zspr(nome, x, y)` (animata da
  sola), `zone`, `zones`, `mkbm.py --sprites`; `keyheld` documentata. Documentazione:
  `docs/API.md` è diventato `docs/API-IT.md`, con la versione inglese (`docs/API.md` dopo
  l'allineamento) e `docs/GAME-GUIDE.md`; l'assistente ha le voci di tutto (185/193 domande
  di prova tra le prime tre).
  Da fare, se servono: portare Hunter's Night dai numeri 32–63 ai flag; più nomi per una
  voce dell'assistente (oggi `lib.printr` porta a `lib.printc` al secondo posto); import
  delle mappe di Tiled (`.tmj`) con i livelli e le proprietà delle tile.
- ✅ **R12 — Più salvataggi per cartuccia.** Oggi uno, da 32 KiB (`/bm/save/XXXXXXXX.SAV`):
  `save(t, slot)` / `saved(slot)`.
  **Fatto** (2026-10-06, branch `claude/dev-tools`): 8 slot da 32 KiB, lo slot 1 è il file di
  prima (`save(t)` e `saved()` senza slot non cambiano), gli altri `XXXXXXXX.S02` ... `.S08`
  (`bm_save_slot` in `runtime.c`); `saves()` dà `{[slot] = byte}` e il numero degli slot,
  `delsave(slot)` ne svuota uno. Le opzioni del gioco nel menu mostrano i byte di tutti gli
  slot ("N bytes in K slots") e *Delete the save data* li cancella tutti. Prove: `make
  test-gameapi`, QEMU `test_home_ui` (due slot, cancellati dal menu).
- ✅ **Hitbox e hurtbox** (richiesta dell'utente, 2026-10-04, branch `game-api`; erano
  rimaste da fare in M22.6 e nei giochi ognuno le scriveva da sé, come Titan Clash). Sezione
  **BOXES** (14) del `.bm`: i riquadri dei fotogrammi delle zone di SPRITES (`hurt`, `hit`,
  `body` o un tipo del gioco, per un fotogramma o per tutti); `zboxes(nome, [fotogramma,
  tipo])` nel runtime; `mkbm.py --sprites` li legge dalle righe sotto la zona, `bmres.py` li
  porta con le loro zone, `cart_save` li scrive. In bmlib `lib.hits()` (le hurtbox e le
  hitbox del fotogramma, `H:check()` i contatti: squadre, un colpo per attacco con `id`,
  parti, corsie con `z`/`depth`, lame che si scontrano; `H:zone` dai riquadri dello sheet,
  specchiati), `lib.box`, `lib.separate`. Prove: `make test-gameapi`, `make test-res`, QEMU
  `test_game_api`. Da fare: bm Animator (Sprites) e bm Pixel che li disegnano sui
  fotogrammi; i riquadri dalle ossa quando un modello diventa sprite (M22.6).
- ✅ **Più giocatori sulla stessa console e in rete** (richiesta dell'utente, 2026-10-04,
  branch `game-api`). Sulla stessa console: `controller(p).color` (il colore della luce del
  pad), in bmlib `lib.PLAYER_COLORS`, `lib.pads()`, `lib.party()` (la schermata dove si
  entra: ok, indietro, Start), `lib.split(n)` (lo schermo diviso; `camera:apply(vista)`).
  In rete: la libreria **bmnet** (`require "bmnet"`, `src/script/bmnet.lua`, nel kernel, in
  bmhost e sulla RGB30): il protocollo di Overbit per ogni gioco: LAN o relay
  (`tools/overbit_relay.py`), lobby (host, join, l'elenco), messaggi persi o sicuri e in
  ordine, lockstep con gli input a 32 bit raccolti da chi ospita (`net.input`,
  `net.frames`, `net.pad`), controllo della sincronia (`net.check`), uscita con `_leave`.
  Modelli dell'SDK *Versus 2D* (due giocatori, pugni con le hitbox) e *Online 2D* (lobby e
  lockstep); i modelli di prima usano bmlib e i flag. Prove: `make test-bmnet` (due bmhost,
  un quinto dei pacchetti persi, LAN e relay), `make test-gameapi`, QEMU `test_editor`,
  `test_sdk_layers`. **Da provare sul Pi**: due console in rete con *Online 2D* (e con il
  relay), due pad con *Versus 2D*. Da fare: Overbit sopra bmnet (oggi ha la sua copia del
  protocollo).

### Strumenti di sviluppo
- **R13 — Debugger Lua in bm Code.** Punti di interruzione, passo passo, variabili
  locali, con l'hook di debug di Lua.
- **R14 — Profiler per funzione.** L'overlay delle prestazioni dà il totale del
  fotogramma; questo le 10 funzioni che costano di più.
- **R15 — Ricarica dal PC.** `bm_net.py --watch`: a ogni salvataggio di `main.lua` sul PC
  la cartuccia torna sulla console e riparte.
- **R16 — Modelli di gioco.** "New game" parte da uno scheletro vuoto (`TEMPLATE` in bm
  Code, "New project" nell'SDK): modelli pronti per platform, visuale dall'alto,
  sparatutto e 3D, con codice, sheet e mappa.
  **Nell'SDK fatto** (con il suo aggiornamento e il branch `game-api`, 2026-10-04): Ctrl+N
  dà Empty 2D, Platform 2D, Top-down 2D, Shooter 2D (con bmlib e i flag delle tile), Versus
  2D (due giocatori, hitbox), Online 2D (bmnet), 3D scene, 3D with models; resta bm Code.
- **R17 — Import MIDI nel Sound editor.** Un file MIDI diventa i pattern del banco.
- **R18 — Documentazione API in inglese.** Il README è in inglese, ma `docs/API.md`,
  `docs/GUIDA-GIOCHI.md` e la base dell'assistente sono solo in italiano. **In gran parte
  fatto** (2026-10-04, con R10 e R11): `docs/API.md` (era `docs/API-EN.md`, allineato a quello
  italiano e rinominato) e `docs/GAME-GUIDE.md` (le versioni italiane sono `docs/API-IT.md` e
  `docs/GUIDA-GIOCHI.md`); manca la base dell'assistente,
  che ha le domande anche in inglese ma le spiegazioni in italiano.
- **R24 — Sprite stacking** (era 22.7 di M22): diventato la milestone **M43** (2026-10-06).
- **R25 — Suoni da e verso il PC** (era in 22.4 e 22.5 di M22, 2026-10-05). Nel Sound editor:
  WAV (campioni brevi) e MIDI (note di un brano) importati ed esportati, l'uscita stereo e un
  editor delle forme d'onda.
- **R26 — Il mouse vero in nano8** (era in M23, 2026-10-05). Le cartucce `.p8` che chiedono il
  mouse (`poke(0x5f2d, 1)`) hanno oggi un cursore mosso da levetta, croce o frecce: con il
  puntatore di sistema di M32 (`mouse(true)`) il mouse USB o Bluetooth, i suoi tasti e la
  rotella (`stat(32)`–`stat(36)`).

### Hardware
- **R19 — Altri controller Bluetooth.** Oggi via Bluetooth solo il DS4 (più tastiere e
  mouse): DualSense, Switch Pro, 8BitDo, i pad Xbox (Bluetooth LE, come `ble.c`).
- **R20 — Telecomando della TV (HDMI-CEC).** Frecce e OK per muoversi nel menu senza pad.
- **R21 — Audio senza HDMI.** PWM su GPIO con il filtro RC (vedi l'hardware consigliato)
  o un DAC I2S, per i monitor senza altoparlanti.
- **R22 — Pulsanti su GPIO e schermo piccolo.** Il Pi Zero dentro un guscio portatile
  (pad sui GPIO, LCD DPI o SPI): una strada diversa dall'RGB30.

### La GPU come coprocessore
- **R23 — Programmi sulle QPU fuori dal disegno 3D** (2026-10-04): diventato la
  milestone **M44** (2026-10-06).
