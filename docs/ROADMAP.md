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
- **Gouraud** (2026-09-29): `draw3d(..., flag 4)` calcola la luce (direzionale e
  lampade) sui vertici, con le normali medie delle facce che li condividono, e la sfuma
  sulla faccia con un dithering ordinato 4×4 (niente bande del RGB565); vale anche per
  le facce con texture e dopo il clipping. `tri(..., c, c1, c2)` fa triangoli 2D
  sfumati. Le texture fanno la divisione prospettica ogni 16 pixel (lineari in mezzo)
  invece che a ogni pixel. Il nucleo del boss di Astro Wing è liscio. Test host
  (`tests/b33`) e due righe nuove nello stress test `s`: "3D smooth (Gouraud)" e
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
- Decisione 2026-09-29: il **modo 32 bit** è rimandato (fuori da M14): raddoppia la
  banda di memoria, che è il limite del Pi Zero, e le sfumature ora le copre il
  dithering.

Previsto:
- **DMA** del BCM2835 per riempimenti e copie (liberano la CPU: `cls`, mappe, copia
  dei frame) e misura sul Pi di cosa conviene (la lettura della SDRAM è il collo di
  bottiglia: vedi M9).
- ~~Modo **32 bit** (XRGB8888) per le `.b33`~~: rimandato (vedi sopra).
- 3D: texture sui triangoli e Gouraud (fatti); rimisurare `docs/STRESS.md` sul Pi.
- Menu grafico con anteprime delle cartucce (immagine nell'header `.b33`).
- **Fatto quando:** lo stress test mostra il guadagno del DMA e una demo 3D con
  texture gira a 60 fps.

---

## M15 — Editor sulla console (L) ✅ verificato sul Pi Zero W
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

## M16 — Multiplayer locale con controller Bluetooth (L) — 🛠 fatto, da provare sul Pi
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
- API `.b33`: `btn(i, [p])`, `btnp(i, [p])`, `players()` (quanti e quali), `stick([p])`
  (levetta analogica, o la croce). **Differenza dalla decisione iniziale:** senza `p`,
  `btn(i)` risponde a *qualsiasi* controller invece che al solo giocatore 1: così i
  giochi a un giocatore non cambiano davvero (con "default 1" la tastiera smetterebbe di
  funzionare appena si collega un pad, perché diventa il giocatore 2).
- s32: le porte `INPUT`–`INPUT4` hanno ciascuna il suo giocatore.
- Menu: `pads: 1 2 - -` nell'intestazione e nella barra di stato; `Y` mostra i tasti di
  ogni giocatore; Pong con la modalità 2 giocatori.

Da provare sul Pi: due DS4 insieme (ritardo d'input come in M12), la luce, Pong a 2.

Sul Pi (2026-09-29, `8298b15`): con un pad collegato, un secondo DS4 già abbinato si
connetteva, apriva il canale **SDP** (PSM 1) prima di quelli HID e, al rifiuto, chiudeva
(motivo 0x13), in ciclo. Ora bm33 ha un piccolo server SDP che risponde "nessun record"
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
- API `.b33`: `btn(i, [p])`, `btnp(i, [p])` con `p` = 1..4 (default 1: i giochi attuali
  non cambiano), `players()` = quanti giocatori sono collegati; l'uscita dal gioco
  (Start+Select, PS) resta per ogni controller.
- s32: il player riempie anche `INPUT2`–`INPUT8`.
- Menu: la barra di stato mostra i controller collegati; il test `Y` del monitor li
  elenca con i pulsanti premuti.
- Test in QEMU: il chip simulato (`FakeDs4Chip`) con due pad.
- **Fatto quando:** due DS4 collegati insieme giocano Pong uno contro l'altro (Pong con
  modalità 2 giocatori).

## M17 — Gioco cooperativo in stile Overcooked (L) — 🛠 fatto, da provare sul Pi
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

## M18 — WiFi e console di rete (L/XL)
Decisione 2026-09-28: versioni "leggere", in coda dopo M17.
- **WiFi**: il BCM43438 (lo stesso chip del Bluetooth) è sul bus SDIO. Driver SDIO sul
  secondo controller (la SD resta sul suo, o si scambiano come fa Linux), caricamento
  del firmware WiFi (`brcmfmac43430-sdio.bin`, `.txt`, `.clm_blob` da
  RPi-Distro/firmware-nonfree, come `BCM43430A1.hcd`), protocollo di controllo del chip
  (FullMAC: associazione e WPA2 li fa il firmware). Rete e password in
  `/bm33/config.txt` (`wifi_ssid`, `wifi_psk`), comando del monitor per scegliere la
  rete.
- **TCP/IP**: lwIP (licenza BSD) con DHCP; IP e stato mostrati sullo schermo.
- **Console di rete** ("pseudo-SSH" leggero): una connessione TCP in chiaro, con
  password, che dà lo stesso monitor e la stessa REPL Lua della seriale (`nc` o uno
  script dal PC); si attiva dalle impostazioni, pensata per la rete di casa.
- **Invio dal PC via WiFi**: `bm33_load.py` anche su TCP per `kernel.img` e cartucce
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
   (seguendo i collegamenti simbolici del repository), `make sdcard` li copia in `bm33/`.
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
   - la rete salvata (`wifi_ssid` / `wifi_psk` in `bm33/config.txt`, in chiaro) si
     ricollega da sola;
   - WPA2-PSK (AES), WPA-PSK o aperta, con il 4-way handshake fatto dal firmware
     (`sup_wpa`, `WSEC_PMK`);
   - esito dagli eventi SET_SSID, PSK_SUP, LINK, DEAUTH.
6. ✅ (Pi, `cbd9356`: IP 192.168.1.108 dal router, ping 4–9 ms) **Indirizzo IP**: lwIP 2.2.0 (`third_party/lwip`, BSD), senza
   sistema operativo e interrogato dai cicli di input (`net_poll`, al più una volta al ms):
   - interfaccia Ethernet `wl` sul canale dati SDPCM (2) con intestazione BDC; i frame
     ricevuti durante ioctl e join vanno in una coda di 8, controllo di flusso con i
     crediti del firmware;
   - dopo la connessione `W` chiede l'indirizzo con DHCP (nome `bm33`) e stampa
     `net: IP ...`; la rete resta attiva nel monitor, nel menu e nei giochi;
   - verifica: `ping <IP>` dal PC.
7. ✅ (Pi, `455e4ff`: login, `h`, `i`, REPL Lua dal PC) **Console di rete**: il monitor su TCP, porta 3333
   (`src/net/netcon.c`), un client alla volta:
   - password `net_password` in `bm33/config.txt`; se manca, un PIN di 6 cifre creato,
     salvato e mostrato sullo schermo dopo l'IP; 3 tentativi, poi la connessione si chiude;
   - tutto ciò che il kernel stampa va anche al client (anello di 32 KiB svuotato da
     `net_poll`, mai dentro lwIP); i tasti del client arrivano come quelli della tastiera;
   - dal PC: `tools/bm33_net.py IP` (terminale raw, Ctrl-] esce);
   - in chiaro: solo per la rete di casa (TLS con M19);
   - test sul PC: `make test-net` (lwIP con interfaccia di loopback).
8. 🛠 (da provare sul Pi) **File dal PC e WiFi all'avvio**:
   - porta TCP 3334, stessa password della console (`src/net/netxfer.c`);
     richiesta `BM3X`, operazione, password, percorso, dimensione, crc32, dati;
   - ✅ (Pi, `ddca333`) `bm33_net.py IP --send gioco.b33` → salvata in `/carts` (nomi 8.3, `--name`,
     `--to`); il menu rilegge la SD da solo;
   - ✅ (Pi: Pong a 59,9 fps) `--play gioco.b33` → giocata subito (dal menu o dal monitor), senza salvarla;
   - ✅ (Pi: 1.3 MB in 7,7 s, 170 KiB/s) `--kernel build/kernel.img` → scritto come `kernel.img` (prima i dati, poi la
     voce della directory: un'interruzione lascia il vecchio o il nuovo) e riavvio;
   - ✅ (Pi, `ddca333`) all'avvio la rete salvata si ricollega da sola, senza scansione
     (`wifi_boot=0` in `bm33/config.txt` la spegne); l'IP compare nella barra di stato;
   - test sul PC: `make test-net` (salvataggio, password, crc, play, kernel);
   - i tasti del terminale di rete arrivano anche a menu, giochi, pager e demo, come
     quelli della seriale (`input_remote_getc`); Esc da solo esce dal menu.
9. Poi: aggiornamento del kernel da GitHub (M19), rete nelle cartucce.


## M19 — HTTPS: aggiornamenti e "git leggero" (L)
- **TLS**: mbedTLS (licenza Apache 2.0) sopra lwIP; certificati radice essenziali sulla
  SD.
- **Aggiornamenti da internet**: il Pi controlla le release di GitHub del progetto,
  scarica `kernel.img` e cartucce, verifica la firma e installa come in M18.
- **"git leggero"** invece di git completo: in lettura, l'archivio di un ramo o di una
  release di un repository (es. cartucce da un repository di giochi); in scrittura,
  le API di GitHub con un token personale per caricare un file (es. un `.b33` salvato
  dall'editor). Token in `config.txt`.
- Più avanti, solo se serve davvero: SSH vero, git completo (clone/push).
- **Fatto quando:** un aggiornamento pubblicato come release arriva sul Pi dal menu, e
  l'editor carica un gioco su un repository.

## M20 — Picchiaduro a robot giganti (XL)
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
- Kernel: sezione **SHEET8** del `.b33` (palette di ≤256 colori + RLE, decodificata al
  caricamento) e sheet fino a 4096 pixel di lato; `mkb33.py --sheet8`; test host in
  `tests/b33`.
- Arte pre-renderizzata (`mkrobot.py`): il robot VANGUARD è un modello 3D procedurale
  su uno scheletro, reso in vista 3/4 con cel shading a 6 toni e contorni, 62 frame in
  23 animazioni, **a strati** (armatura pesante, spallaccio integro/crepato, cannoni,
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

## M21 — Menu "home" e giochi sospesi (M)
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
   
   L'editor ora si chiama **bm33 SDK**.
2. ✅ (QEMU, `test_suspend_resume`) **Giochi sospesi**:
   - Esc, o PS sul controller, esce dal gioco ma lo lascia congelato in memoria (stato
     Lua, sheet, mappa, 3D; audio muto);
   - nel menu la copertina ha il badge **Playing** (già disegnato da `menu_ui`);
   - A sulla stessa copertina riprende dal punto esatto;
   - avviare un'altra applicazione chiede conferma, chiude quella sospesa e libera la
     memoria.
   
   Fatto: `b33_run` (con `suspendable`), `b33_resume`, `b33_close_suspended`.
   - Alla ripresa tornano:
     - la stessa area di disegno (clip e camera);
     - il disegno via RAM, se il gioco usa le luci;
     - `time()` senza il tempo passato nel menu.
   - I tasti ancora premuti non contano come nuove pressioni.
   - Una sola applicazione sospesa alla volta, come sulle console.
   - `quit()`, un errore e le prove dall'SDK chiudono davvero.
   - Le cartucce s32 non si sospendono (ancora).
- **Fatto quando:** sul Pi il menu è fluido a 60 fps con tutte le cartucce e un gioco
  sospeso riprende dal punto in cui era.

## M22 — SDK e strumenti dedicati (XL)
Decisione 2026-09-29: l'editor attuale diventa l'**SDK** (generico: progetto, prova,
salvataggio); intorno a lui strumenti specializzati, ognuno una cartuccia nella scheda
**Dev**, tutti con gli stessi formati.

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
- **22.2 Pixel art**:
  - sprite, tavolozze, animazioni (fotogrammi, onion skin, anteprima);
  - strumenti (linea, rettangolo, riempimento, selezione, specchio);
  - tile e **mappe** (proposta: la mappa sta qui, tile e mappe sono legati).
- **22.3 Render 3D**: mesh low-poly (vertici, estrusione, colori e UV sullo sheet),
  luci, camera, anteprima con Gouraud e texture, esportazione nella sezione MESH.
- **22.4 Musica ed effetti**:
  - tracker sul sintetizzatore a 8 voci;
  - effetti con forma d'onda, ADSR, inviluppi di tono;
  - sezioni SFX/MUSIC e API `sfx(n)`, `music(n)`.
- **22.5 Import/export**:
  - PNG ↔ sheet (con riduzione a ≤256 colori), OBJ/GLB → MESH, file Lua ↔ progetto,
    WAV/MIDI dove ha senso;
  - dalla SD, e con M18 dal PC via WiFi.
- **22.6 Da 3D a sprite**: come `carts/titan/mkrobot.py` ma sul Pi. Si parte da un
  modello con scheletro e pose, si scelgono viste e dimensione; poi cel shading, contorni,
  riduzione della tavolozza, fotogrammi nello sheet con hitbox e hurtbox.
- **22.7 Sprite stacking** (decisione 2026-09-29, come i layer di Aseprite):
  - un oggetto è una pila di **fette** 2D (un layer per altezza), disegnate in pixel art;
  - sovrapposte con un piccolo scarto verticale e ruotate, danno l'illusione di un volume
    3D, girabile su x, y, z;
  - editor: fette in griglia e sovrapposte, onion skin della fetta sotto, anteprima che
    gira dal vivo.
  
  In gioco, disegno in C con due livelli di costo:
  - **solo rotazione z** (lo stacking classico, visto dall'alto): N fette ruotate e
    spostate, economico;
  - **x, y, z libere**: la pila diventa un volume di voxel disegnato punto per punto
    dalla faccia visibile (per esempio 32×32×32), più costoso.
  
  API: `stack(sx, sy, w, h, n, x, y, [rz, rx, ry, scala])`.
  
  Legami con gli altri strumenti:
  - le fette sono sprite dello sheet (22.2 con i layer);
  - un volume può diventare MESH (22.3) o fotogrammi pre-renderizzati con 22.6.

Considerazioni:
- **Contenitore unico: il `.b33` stesso** (come le cartucce PICO-8): codice, sheet,
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

## M23 — Emulatore di cartucce `.p8` / `.p8.png` (stile PICO-8) (L/XL)
Decisione 2026-09-29: in coda. Scritto da zero in C sul runtime di bm33 (non il port di
fake-08, in C++). Nessun nome, logo o font di PICO-8 (prodotto di Lexaloffle): nome e
font nostri; le cartucce del forum sono per lo più CC BY-NC-SA (uso non commerciale).
1. **Caricatore**:
   - `.p8.png`: PNG con inflate, 32 KiB nascosti nei 2 bit bassi dei colori, codice
     compresso nei formati `:c:` e `pxa`;
   - `.p8` di testo;
   - l'immagine fa da copertina nel menu.
2. **Dialetto Lua** tradotto in Lua 5.4 prima di eseguirlo: `+=`, `!=`, `if (c) x`
   su una riga, `?`, `\`, operatori sui bit, `@`/`%`/`$`, commenti `//`.
3. **Macchina**: RAM di 32 KiB emulata (schermo 128×128 a 4 bit, stato del disegno,
   sprite, mappa, `peek`/`poke`/`memcpy`), convertita in colore a ogni fotogramma
   (framebuffer piccolo, ingrandito dalla GPU).
4. **API grafica e input** (`spr`, `sspr`, `map`, `tline`, `fillp`, `pal`, `print`
   con i codici di controllo, `btn`/`btnp` per più giocatori con M16).
5. **Audio**: 4 canali, 8 forme d'onda, strumenti personalizzati, effetti, musica.
6. **Numeri a virgola fissa 16.16** (Lua modificato): dal ~70–80% al ~95% delle
   cartucce compatibili.
- **Fatto quando:** un gioco senza suono gira dalla SD (primo traguardo), poi con audio
  e numeri 16.16.

## M24 — Scambio di giochi e risorse: store e P2P (L, dopo M18/M19)
Decisione 2026-09-29: in coda, da definire meglio; considerazioni iniziali.
- **Cosa si scambia**:
  - cartucce `.b33` (già un contenitore unico) e pacchetti di risorse (sprite, mesh,
    suoni: un `.b33` senza codice);
  - ogni pacchetto con un manifesto: nome, autore, versione, licenza, hash SHA-256.
- **Catalogo su un repository GitHub** (consigliato come "store"):
  - hosting gratuito, versioni e cronologia, moderazione con le pull request;
  - il Pi legge un indice firmato via HTTPS (M19) e scarica;
  - dall'SDK si pubblica con il token personale (M19, "git leggero").
- **P2P in rete locale** (dopo M18, economico):
  - le console si trovano con un annuncio UDP in broadcast;
  - si passano cartucce e risorse via TCP, per esempio tra amici sulla stessa rete.
- **P2P via internet** tra console: **sconsigliato** sul Pi Zero bare metal.
  - Servono traversamento del NAT, server di appoggio (relay) comunque, TLS e una DHT:
    molto codice, e senza un server centrale il valore aggiunto è poco.
  - Alternativa: file indirizzati per hash, scaricabili da qualunque fonte (repository o
    console vicina), con la stessa verifica.
- **Sicurezza**:
  - pacchetti firmati dagli autori (Ed25519, codice piccolo: monocypher/TweetNaCl) e
    hash verificati prima di installare;
  - le cartucce Lua girano già in un ambiente chiuso; il codice ARM nativo (M13) no:
    senza isolamento della memoria, solo da autori fidati o mai dallo store.
- **Market a pagamento**: account, pagamenti e licenze vanno su un servizio web, non sul
  Pi; il Pi scarica solo ciò che l'account ha sbloccato. È un progetto a sé.
- **Licenze**: il campo licenza è obbligatorio. La BM33 Community License vale per bm33,
  non per i contenuti degli utenti; attenzione a CC BY-NC-SA (uso non commerciale) e ai
  contenuti di terzi.
- **Passi proposti**:
  1. scambio in rete locale;
  2. catalogo in lettura da GitHub;
  3. pubblicazione dall'SDK;
  4. firme e scheda "Store" nel menu;
  5. (eventuale) market web.

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
