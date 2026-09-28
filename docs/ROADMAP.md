# Roadmap bm33

Obiettivo MVP: una *fantasy console* bare metal su Raspberry Pi Zero W che avvia
da SD, mostra un menu, carica giochi scritti in Lua ("cart") e li esegue a 60 fps
con grafica, input e (opzionale) audio.

Ogni milestone ha un **criterio di completamento** verificabile e, quando
possibile, un test automatico in QEMU (`-M raspi0`).
Dimensione: **S** = pochi giorni, **M** = 1–2 settimane, **L** = più di 2 settimane.

```
M0 ─ M1 ─ M2 ─ M3 ─ M4 ─ M5 ─ M6 (s32) ─┬─ M7 ─┬─ M9 (MVP) ─ M10…M14 (vedi "Dopo l'MVP")
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

## M6 — Core s32 (compatibilità con lua32) ✅ verificato su Pi Zero W (M)
Decisione: bm33 è compatibile con le cartucce `.cart` della console **s32**
(`f-accomando/lua32`): stessa macchina (risoluzioni, palette, tile, VRAM 552 KiB, OAM,
CGRAM, APU, porte), implementata in C nativo, non un'emulazione del motore di lua32.
- Specifica comune `spec/s32/s32-spec.md` (fonte: lua32, `docs/spec`), sincronizzata con
  `scripts/sync-s32-spec.sh`.
- `src/s32/`: CPU (83 opcode), PPU (tilemap 128×128 con celle coperte, tile 8–64, sprite
  con flip e priorità), loader `.cart` (header 264 byte, CRC), player a 320×224 con doppio
  buffer, 60 tick/s.
- Conformità: i vettori generati da lua32 (`spec/s32/conformance`) passano **byte per
  byte** sia su x86 sia sul codice ARM1176 in `qemu-arm` (`make test-s32 test-s32-arm`).
- All'avvio: `demo.cart` in modalità attract per 15 s; comando `g` per giocarla dalla seriale.
- **Fatto quando:** tutti i vettori passano; la demo gira a 60 fps sul Pi.
- Prossimo lato s32: cartucce Lua (`code_type` 1, spec §11, domande in lua32 PR #2) e APU,
  quando lua32 sarà più maturo.

## Tipi di cartuccia (decisione 2026-09-26)
| Tipo | Formato | Gira su | Priorità |
|---|---|---|---|
| s32 codice macchina | `.cart`, `code_type` 0 | bm33 + lua32 | ✅ fatto |
| **bm33 nativa Lua** | **`.b33`** (formato separato, non tocca la spec s32) | solo bm33, sfrutta tutto il Pi | **prossima** |
| s32 Lua | `.cart`, `code_type` 1 | bm33 + lua32 | quando lua32 è pronto |
| bm33 nativa ARM (C) | `.b33` | solo bm33, user mode + MMU | dopo l'MVP |

Priorità attuale: sviluppo della console bm33; la parte s32 avanza al ritmo di lua32.

### Cartucce native `.b33`: video (decisione 2026-09-26)
- **640×360**, 16:9, scala intera ×2 su 720p e ×3 su 1080p; è la risoluzione della
  console, quindi nessun cambio di modo. 320×180 facoltativa (header).
- **16 bit RGB565** (65 536 colori, colore diretto): metà banda del 32 bit
  (fill stimato ~1,1 ms contro 2,2 ms misurati).
- **Espandibile a 32 bit** in seguito: l'API riceve i colori come RGB888 e la
  grafica delle cartucce è salvata in un formato indipendente dal framebuffer;
  il formato di pixel è un campo dell'header `.b33` e il disegno in C è
  parametrizzato sulla profondità. Il 24 bit "impacchettato" (3 byte per pixel,
  non allineato) si evita: l'espansione utile è il 32 bit.
- **Budget**: disegno completo (mappa piena + 256 sprite) sotto il 25% del frame,
  verificato da un benchmark a schermo; tutto il disegno in C, Lua solo logica.

## M7 — Cartucce native `.b33` ✅ verificato su Pi Zero W (M)
- Formato `.b33` (header 128 byte + sezioni Lua / sheet RGBA / mappa, CRC), packer
  `scripts/mkb33.py` con PNG e CSV.
- Grafica C in RGB565 (`src/b33/gfx16.c`): forme, sprite con flip e trasparenza,
  scorciatoia per celle opache, mappa, testo, camera, clip.
- Runtime: stato Lua isolato per cartuccia, `_init/_update/_draw` a 60 fps, input
  seriale, limite di istruzioni per frame, errori mostrati sulla console, GC
  generazionale; caricamento dalla seriale (`U`).
- Benchmark C e demo nativa all'avvio.
- **Fatto quando:** mappa piena + 256 sprite sotto il 25% del frame sul Pi reale.
- Risultato (kernel `a63bfb0`): demo s32 e demo nativa a schermo pieno, controllo colori
  RGB565 corretto (rosso, verde, blu, bianco); 256 sprite 16×16 ≈ 0,9 ms
  (vedi docs/STRESS.md).

## M7b — Input ✅ tastiera verificata sul Pi Zero W (L)
- Stack USB scritto da zero (non USPi): controller DWC2 in modalità host, DMA a buffer,
  polling dal ciclo principale; enumerazione di un dispositivo sulla porta radice.
- **Tastiera** HID (protocollo boot): layout italiano/US, ripetizione; monitor e REPL
  leggono da seriale o tastiera. **Gamepad HID** generici (analisi del descrittore) e
  **Xbox 360** cablati. Mappatura su `btn()` delle `.b33` e sui bit 0–4 di s32.
- Esc o Start+Select escono dal gioco. Niente hub (decisione: un dispositivo alla volta),
  niente Bluetooth (BCM43438 condivide la UART della console; firmware + HCI: troppo costoso).
- Pad su GPIO (fase A) non necessario per ora.
- **Fatto quando:** una tastiera USB scrive nel REPL e un gamepad muove il giocatore
  (QEMU con `usb-kbd` e `usb-tablet`; sul Pi, kernel `25f5dbc`: Apple Magic Keyboard
  05ac:0267 via OTG, composita a 3 interfacce, report con ID: menu e giochi ok;
  gamepad non ancora provato sul Pi).

## M8 — Storage e caricamento delle cart ✅ verificato sul Pi Zero W (M)
- Driver SD sul controller EMMC (Arasan SDHCI) in PIO, bus a 4 bit a 25 MHz, SDSC e SDHC;
  FAT16/FAT32 con nomi lunghi, in sola lettura (scritto da zero, non FatFs).
- Menu delle cartucce: incorporate + `.b33`/`.cart` in `/carts` e nella radice; si apre
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
- `make image` → `dist/bm33.img` (64 MiB, MBR + FAT32) pronto per Raspberry Pi Imager /
  balenaEtcher / `dd`.
- Guida all'API e alla prima cartuccia: `docs/API.md`.
- Prestazioni sul Pi: l'ARM1176 legge la SDRAM circa 4 volte più lentamente di
  quanto ci scrive (`memcpy` 10,3 ms/MiB contro `memset` 2,4 ms/MiB, cache dati 16 KB),
  e la memoria video non ha cache. s32 ora disegna a strisce di 8 righe che restano in
  cache e le scrive una volta sullo schermo, senza riletture (`render` era 7997 µs/tick,
  poi 5577 con il primo tentativo). Per `.b33` il benchmark misura sia il disegno diretto
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
          ├─ M13 altri tipi di cartuccia (s32 Lua quando lua32 è pronto, ARM nativo)
          ├─ M14 grafica 2.0 (DMA, 32 bit, 3D con texture)
          ├─ M15 editor sulla console (codice, sprite, mappa)
          └─ M16 multiplayer locale (più controller Bluetooth) ─── M17 gioco cooperativo (ultima)
```

## M10 — Audio HDMI (M/L) ✅ verificato sul Pi Zero W
- Uscita **HDMI** (dagli altoparlanti del monitor), `src/audio/audio.c`: blocco audio
  HDMI del BCM2835 (FIFO MAI, rigenerazione del clock N/CTS, InfoFrame audio) come in
  Circle; campioni IEC 958 a 48 kHz (`iec958.c`) mandati da un canale DMA con DREQ
  HDMI su due buffer ad anello da 256 campioni (5,3 ms). L'interrupt di fine buffer
  genera il blocco successivo: nessun lavoro nel ciclo del gioco.
- Sintetizzatore `synth.c`: 8 voci con la semantica dell'APU di s32 (`apu.lua` di
  lua32: quadra con duty, triangolo, dente di sega, rumore LFSR a 15 bit, ADSR lineare,
  somma senza normalizzazione con saturazione). Test su host: `make test-audio`.
- **APU di s32**: durante una `.cart` il sintetizzatore legge direttamente i registri a
  `0x0AC900` della macchina.
- API `.b33`: `note`, `noteoff`, `freq`, `envelope`, `duty`, `playing`, `apu`
  (docs/API.md); effetti e melodie nei tre giochi demo.
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
- File `/bm33/config.txt` (layout tastiera, modo di disegno, volume, dispositivi
  Bluetooth abbinati) e `/bm33/save/<cart>.sav`.
- API `.b33`: `save(tabella)` / `saved()` per record e progressi; punteggi migliori nei
  giochi demo.
- File di salvataggio: `/bm33/save/<CRC-32 di titolo e autore>.SAV` (nomi 8.3: bm33 non
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
   firmware da `bm33/BCM43430A1.hcd`, indirizzo e versione, ricerca dispositivi
   (monitor `T`). Test in QEMU con un **chip simulato** in Python sulla UART0
   (`test_bt_start_and_scan`): è la base per provare anche i passi successivi.
   Sul Pi (kernel `7c36bb3`): chip e firmware ok (121 record), indirizzo
   b8:27:eb:62:7c:08, DS4 trovato (00:1f:e2:bf:d7:dd, classe 002508).
2. ✅ (QEMU) connessione ACL, abbinamento SSP "Just Works" (IO NoInputNoOutput,
   bonding generale), cifratura, canali L2CAP 0x11/0x13, report HID del DS4 nel layer
   di input (menu e giochi); chiave in `bm33/config.txt`; all'avvio page scan e
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

## M13 — Altri tipi di cartuccia (M)
- **s32 Lua** (`code_type` 1): **sbloccato** — le regole sono decise in `s32-bm33.md`
  (sincronizzato da lua32 `dbaa650`) e implementate in lua32: API `peek/poke`,
  `peek16/poke16`, `btn`, funzioni di comodo (`spr`, `mset`, `pal`, `camera`...),
  costanti nominate, sandbox, budget di istruzioni anche su `_init()`, sul codice di
  primo livello e su ogni coroutine.
- Modo s32 16:9 (`screen_mode`, già deciso in `s32-bm33.md`).
- **ARM nativo**: sezione di codice ARM in `.b33` (per giochi in C), caricata in una
  zona di memoria dedicata con API tramite tabella di funzioni; senza protezione
  della memoria (solo cartucce fidate).
- **Fatto quando:** una cart Lua di lua32 gira uguale su lua32 e bm33; un gioco demo
  in C gira come `.b33` nativa.

## M14 — Grafica 2.0 (M) — in corso
Fatto finora (da verificare sul Pi):
- driver DMA (`src/drivers/dma.c`, canali assegnati insieme all'audio). Sul Pi la
  prima versione (burst da 8, priorità alta, `WAIT_RESP`) dentro `p` ha bloccato il
  sistema: ora usa le impostazioni di Circle e si prova con il comando `D`, passo per
  passo, con ogni passo scritto sullo schermo prima di eseguirlo; solo se il test
  passa il disegno `.b33` "via RAM" copia i fotogrammi con il DMA;
- 3D: clipping sul piano vicino, nebbia (`fog3d`), rollio della camera, `project3d`;
- gioco di prova **Astro Wing** (`carts/astrowing`, in stile Star Fox).
- **texture** sui triangoli (prospettiva corretta, anche dopo il clipping; `mesh(v, f, uv)`
  in Lua con lo sprite sheet);
- **menu grafico**: ogni cartuccia è una scheda 3D a forma di Memory Stick Duo con la
  copertina stampata (sezione `COVER` 128×80 nel `.b33`, `mkb33.py --cover`, copertine
  dei giochi demo da `scripts/mkcovers.py`, etichetta col titolo per le altre) e i
  contatti in rame sul retro; la scheda scelta ondeggia e ogni 7 s si gira.
- **luce** per le `.b33` (`light_begin`/`light`/`light_end`, griglia 4×4 in C) e i
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

Previsto:
- **DMA** del BCM2835 per riempimenti e copie (liberano la CPU: `cls`, mappe, copia
  dei frame) e misura sul Pi di cosa conviene (la lettura della SDRAM è il collo di
  bottiglia: vedi M9).
- Modo **32 bit** (XRGB8888) per le `.b33`, previsto dal formato (pixel format 2).
- 3D: texture sui triangoli e Gouraud; rimisurare `docs/STRESS.md`.
- Menu grafico con anteprime delle cartucce (immagine nell'header `.b33`).
- **Fatto quando:** lo stress test mostra il guadagno del DMA e una demo 3D con
  texture gira a 60 fps.

---

## M15 — Editor sulla console (L) — prima versione fatta, da provare sul Pi
Decisione 2026-09-28: editor **sulla console** (come PICO-8), prima del multiplayer.
- **`bm33 editor`**, sempre ultimo nel menu delle cartucce (freccia su dal primo; e `e` dal monitor): una
  cartuccia `.b33` incorporata nel kernel (`carts/editor/main.lua`).
- **Codice** (F1): colori della sintassi Lua, numeri di riga, scorrimento, rientro
  automatico, Ctrl+Z annulla, Ctrl+K taglia riga, Ctrl+D duplica riga.
- **Sprite** (F2): pixel ingranditi (8×8 o 16×16), foglio intero a fianco, tavolozza di
  32 colori più trasparente, matita, riempimento, contagocce, specchio, copia/incolla,
  annulla.
- **Mappa** (F3): la mappa a grandezza reale, piazza/preleva tile, riempimento, scelta
  della tile dal foglio, annulla.
- **Menu** (Esc): nuovo, apri (i `.b33` della SD), salva, salva come (nome 8.3 in
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

## M16 — Multiplayer locale con controller Bluetooth (L)
Decisione 2026-09-28: solo **Bluetooth**; USB e hub restano con un solo dispositivo.
- Più controller abbinati: `bt_pad1`, `bt_pad2`, … in `config.txt` (il vecchio
  `bt_pad` diventa il giocatore 1); `T` abbina il prossimo controller libero; la
  riconnessione con il tasto PS funziona per tutti.
- Stack Bluetooth con più connessioni ACL e canali L2CAP HID contemporanei (il
  BCM43438 ne regge diverse; obiettivo: **4 giocatori**, 2 come minimo verificato sul
  Pi), il ritardo d'input misurato come in M12 con due pad insieme.
- Pulsanti **per giocatore**: il controller n è il giocatore n; la tastiera USB (e la
  seriale) è un giocatore a parte (il primo libero). Il LED dei DS4 prende il colore
  del giocatore (report di output).
- API `.b33`: `btn(i, [p])`, `btnp(i, [p])` con `p` = 1..4 (default 1: i giochi attuali
  non cambiano), `players()` = quanti giocatori sono collegati; l'uscita dal gioco
  (Start+Select, PS) resta per ogni controller.
- s32: il player riempie anche `INPUT2`–`INPUT8`.
- Menu: la barra di stato mostra i controller collegati; il test `Y` del monitor li
  elenca con i pulsanti premuti.
- Test in QEMU: il chip simulato (`FakeDs4Chip`) con due pad.
- **Fatto quando:** due DS4 collegati insieme giocano Pong uno contro l'altro (Pong con
  modalità 2 giocatori).

## M17 — Gioco cooperativo in stile Overcooked (L) — ultima milestone
- Cartuccia `.b33` per **1–4 giocatori** in cooperativa locale (M16): una cucina vista
  dall'alto, gli ordini arrivano a tempo e vanno preparati insieme: prendere gli
  ingredienti, tagliarli, cuocerli (con il rischio di bruciarli), comporre il piatto,
  servirlo, lavare i piatti.
- Più livelli con cucine che cambiano (piani che si muovono, ostacoli, fuoco da
  spegnere), punteggio a stelle per livello salvato sulla SD.
- Comandi: movimento in 8 direzioni, A prendi/posa, B usa (taglia, lava), X scatto.
- Con un solo giocatore: si passa da un cuoco all'altro con Y.
- Grafica e suoni con gli strumenti già fatti (sprite da script, luci, sintetizzatore).
- **Fatto quando:** 2+ giocatori completano un livello sul Pi senza cali di frame rate.

## Rischi principali
| Rischio | Mitigazione |
|---------|-------------|
| Stack USB (M7B) molto complesso | Stack minimo scritto da zero (un dispositivo, HID); testato in QEMU |
| Prestazioni Lua su ARM1176 a 1 GHz | Cache attive (M3), bassa risoluzione, API di blit in C |
| Firmware closed-source che cambia comportamento | Fissare la versione con `FW_REF` |
| Test solo su hardware | QEMU raspi0 in CI + chainloader via seriale |
| Bluetooth (M12) senza emulatore | un solo controller di riferimento, tracce HCI registrate sul Pi per i test |
| Scrittura su SD (M11) che corrompe la scheda | test in QEMU con `fsck.vfat`, file di bm33 in una cartella dedicata |

## Hardware consigliato per lo sviluppo
- Adattatore USB-seriale 3.3 V (**non 5 V**) su GPIO14/15 + GND
- Cavo mini-HDMI, alimentatore 5 V 2 A stabile
- Pulsanti o pad SNES + qualche resistenza per M7A
- Filtro RC (270 Ω + 33 nF) e jack per M10 (solo se l'audio va su PWM)
- Per M12: un controller Bluetooth di riferimento
