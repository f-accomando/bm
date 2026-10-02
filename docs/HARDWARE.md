# Raspberry Pi Zero W v1.1: risorse hardware e utilizzo di bm

(Il Pi Zero 2 W, con il suo kernel `kernel7.img`, è nella sezione 8.)

Scopo: vedere **quanto dell'hardware usiamo** e **quanto margine resta**. Lo stato e le
scelte tecniche con le misure sono anche in [`PRESTAZIONI.md`](PRESTAZIONI.md).
Colonne:

- **Pi Zero W** — capacità dell'hardware (datasheet o misurate sul nostro Pi);
- **bm oggi** — cosa usa l'implementazione attuale;
- **Uso** — quota della risorsa totale.

Legenda fonti: *(M)* misurato su Pi Zero W reale, *(Q)* misurato in QEMU,
*(D)* datasheet/documentazione Raspberry Pi.

## 1. Processore

| Risorsa | Pi Zero W | bm oggi | Uso / margine |
|---|---|---|---|
| CPU | ARM1176JZF-S (ARMv6), 1 core, 700 MHz all'avvio → **1000 MHz** *(M)* | 1 core a 1000 MHz, MMU + cache I/D + branch prediction attive *(M)* | 1 core su 1 (100% disponibile per noi, nessun OS) |
| FPU | VFPv2, double in hardware *(D)* | usata da Lua (double), dal 3D e dal sintetizzatore; RunFast attivo | — |
| Cache | L1 16 KiB istruzioni + 16 KiB dati; L2 128 KiB nella VideoCore *(D)* | L1 attive | — |
| Divisione intera | non in hardware (libgcc) *(D)* | — | le divisioni costano, evitarle nei cicli caldi |
| Budget per frame | 16,7 ms a 60 fps | fill 640×360: 2,2 ms; demo C: 2,7 ms di disegno *(M)* | ~84% del frame libero nella demo C |
| Lua | — | ~100 ns per operazione semplice: fib(25) 83 ms, 1M addizioni 104 ms *(M)* | ~140 000 operazioni Lua per frame a 60 fps |

## 2. Memoria

| Risorsa | Pi Zero W | bm oggi | Uso |
|---|---|---|---|
| RAM totale | **512 MiB** LPDDR2, condivisa CPU/GPU *(D)* | — | — |
| RAM dell'ARM | 448 MiB (con `gpu_mem=64`) *(M)* | kernel ~1,1 MB (con SDK, Sound editor e TLS dentro) + stack 1,1 MiB + heap ~447 MiB | tutto disponibile |
| RAM della GPU | 64 MiB *(M)* | framebuffer 640×360×4×2 = 1,8 MiB | 2,8% della memoria GPU |
| Memoria per Lua | — | limite **64 MiB** *(M)*; boot.lua ne usa 84 KiB, picco 3,4 MiB | 14% della RAM ARM come tetto |
| Banco dei suoni | — | 2 copie di ~140 KiB (quella che suona e quella in cui si legge la nuova) | ~0 |
| Cartuccia | microSD (GB) | la più grande, Titan Clash, 1,7 MB; letta tutta in RAM | trascurabile |

In pratica **la memoria non è un vincolo**; lo sono il tempo per frame e la banda verso
la RAM (memcpy ~100 MB/s, riempimento ~430 MB/s *(M)*).

## 3. Video

| Risorsa | Pi Zero W | bm oggi | Uso |
|---|---|---|---|
| Uscita | mini-HDMI fino a 1920×1080 a 60 Hz; composito (pad TV) *(D)* | HDMI *(M)* | — |
| GPU | VideoCore IV, core 250 MHz *(M)*, OpenGL ES 2.0, scaler hardware (HVS) *(D)* | solo lo scaler (framebuffer ingrandito dalla GPU); il 3D delle cartucce è software sull'ARM | GPU 3D inutilizzata (vedi docs/STRESS.md) |
| Risoluzione logica | qualsiasi, scalata dalla GPU | console e menu 640×360 (32 bit); **cartucce 640×360 o 320×180 RGB565** | 640×360 = 11% dei pixel di 1080p |
| Colori | framebuffer 32 bit (16,7 milioni) *(M)* | 32 bit per la console; 16 bit RGB565 (65 536 colori) per le cartucce | — |
| Sprite | nessun limite hardware (disegno software) | 256 sprite 16×16 ≈ 0,9 ms *(M)* | — |
| Frequenza | 60 Hz (vsync del firmware **non disponibile** su Pi Zero, tag non supportato *(M)*) | 60 fps dal timer, 0 frame persi *(M)* | — |
| Doppio buffer | sì (virtual offset) *(M)* | attivo | — |

## 4. Audio

| Risorsa | Pi Zero W | bm oggi | Uso |
|---|---|---|---|
| Uscite | **HDMI** audio; nessun jack; 2 canali PWM su GPIO (serve filtro RC) *(D)* | **HDMI** 48 kHz, mono sui due canali: FIFO MAI con campioni IEC 958 da un canale DMA *(M)* | 1 canale DMA |
| Sintesi | software (CPU) + DMA per l'uscita | 8 voci: quadra (duty), triangolo, dente di sega, seno, due rumori; ADSR; volume generale; limitatore morbido | il costo per blocco lo mostra il comando `a` |
| Sequencer | — | banco della cartuccia (sezione AUDIO): effetti sonori, pattern a 8 tracce, brani, effetti sui passi; ogni 64 campioni (1,3 ms) nell'interrupt audio | — |

## 5. Input e periferiche

| Risorsa | Pi Zero W | bm oggi | Uso |
|---|---|---|---|
| USB | 1 × micro-USB OTG (USB 2.0, controller DWC) *(D)* | host DWC2: **1 dispositivo HID** (tastiera o gamepad) in uso più un **mouse** (M32), anche dietro un hub *(M7b, M29)* | 1 porta |
| Bluetooth | BT 4.1 / BLE (BCM43438) *(D)* | fino a **4 DualShock 4** (M12, M16), una tastiera BLE (M28) e un mouse BLE o classico (M32) *(M)* | — |
| Wi-Fi | 802.11 b/g/n 2,4 GHz (BCM43438) *(D)* | WiFi, console di rete, invio di kernel e cartucce, HTTPS (M18, M19) *(M)* | — |
| GPIO | header a 40 pin (28 GPIO, da saldare sul Zero W) *(D)* | GPIO14/15 UART, GPIO47 LED | 2 su 28 |
| UART | PL011 + mini UART *(D)* | console sulla seriale (sul mini UART quando il PL011 va al Bluetooth) | — |
| Timer | system timer 1 MHz, 4 comparatori (2 liberi per l'ARM) *(D)* | comparatore 1 a 1 kHz *(M)* | 1 su 2 |
| DMA | 16 canali *(D)* | audio HDMI; copie dei frame quando attivate | 1–2 |
| Fotocamera | connettore CSI *(D)* | non usato | 0 |
| Storage | microSD *(D)* | FAT16/32 in lettura e scrittura (SDHOST): cartucce, salvataggi, impostazioni, pacchetti di suoni; un file si riscrive senza perderlo se manca la corrente | — |

## 6. Cosa ci dicono i numeri

- **Memoria**: enorme margine.
- **Tempo per frame**: è la risorsa vera. Il disegno in C lascia oltre l'80% del frame;
  la musica e gli effetti sonori girano nell'interrupt audio e non lo toccano.
- **Banda di memoria**: il collo di bottiglia per grafica e copie (memcpy lenta, il DMA
  aiuta).
- **Non usati**: GPU 3D, fotocamera, PWM audio (servirebbe il filtro RC).

## 7. Raspberry Pi 1 B / B+ (M29)

Stesso SoC (BCM2835), stesso kernel; le differenze che contano per bm *(D)*:

| Risorsa | Pi 1 B (rev 2.0) / B+ | Pi Zero W | bm |
|---|---|---|---|
| CPU | ARM1176 a **700 MHz** | 1 GHz | clock al massimo che il firmware consente |
| RAM | 512 MiB (B rev 1.0: 256) | 512 MiB | letta dal firmware |
| USB | 2 porte (B+: 4) dietro l'hub del **LAN9512** (B+: LAN9514), high speed | 1 OTG | hub + split transactions, 1 dispositivo HID in uso |
| Rete | **Ethernet 10/100** (LAN951x, USB 0424:ec00, porta 1 dell'hub) | WiFi (BCM43438) | driver `smsc95xx.c`, lwIP come col WiFi |
| Wi-Fi / Bluetooth | assenti | BCM43438 | `W` e `T` dicono che non ci sono |
| LED ACT | GPIO 16 attivo basso (B+: GPIO 47 attivo alto) | GPIO 47 attivo basso | scelto dal codice di revisione |
| Video | HDMI a grandezza piena (+ composito) | mini-HDMI | uguale |
| SD | SD (B) / microSD (B+) | microSD | uguale (SDHOST) |

## 8. Raspberry Pi Zero 2 W (M31)

Un altro SoC, il **BCM2710A1** (nel modulo RP3A0, come il Pi 3), e quindi un altro
kernel: **`kernel7.img`**, gli stessi sorgenti compilati per ARMv7 a 32 bit
(`-DBM_ZERO2`). Sulla stessa SD stanno tutti e due: `config.txt` fa partire
`kernel7.img` sul Zero 2 W (`[pi02]`) e `kernel.img` sulle altre schede. Differenze
che contano per bm *(D)*:

| Risorsa | Pi Zero 2 W | Pi Zero W | bm (`kernel7.img`) |
|---|---|---|---|
| CPU | **4 × Cortex-A53** (ARMv8) a 1 GHz, avviato dal firmware in modo HYP | 1 × ARM1176 (ARMv6) a 1 GHz | 32 bit (ARMv7: divisione intera in hardware, VFPv4 con 32 registri, NEON); passa da HYP a SVC; **1 core**, gli altri 3 restano fermi nello stub del firmware |
| Cache | L1 32 KiB + 32 KiB per core, **L2 512 KiB**, righe da 64 byte | L1 16 + 16 KiB, righe da 32 | operazioni ARMv7 (per indirizzo fino al punto di coerenza; "tutta la cache" per set/way, L1 e L2) |
| Periferiche | a **0x3F000000** (+ quelle dell'ARM a 0x40000000) | a 0x20000000 | stessi driver (UART, GPIO, timer, DMA, SDHOST, USB DWC2, HDMI, mailbox) |
| Indirizzi per GPU e DMA | alias non in cache 0xC0000000 | alias con L2 0x40000000 | `ARM_TO_BUS` in `src/drivers/mmio.h` |
| RAM | 512 MiB LPDDR2 | 512 MiB | uguale |
| LED ACT | **GPIO 29** attivo basso | GPIO 47 attivo basso | sul Zero 2 W GPIO 47 è l'I2C dell'alimentatore: mai toccato da `kernel7.img` |
| Wi-Fi | **CYW43436** (dice chip 43430; rev 2+ = 43436, rev 1 = 43436s) sugli stessi pin (SDIO GPIO 34–39, WL_ON GPIO 41) | BCM43438 | firmware `brcmfmac43436-sdio.*` o `brcmfmac43436s-sdio.*` in `bm/` |
| Bluetooth | stesso chip, UART GPIO 30–33, **BT_ON GPIO 42** | BT_ON GPIO 45 | patch `SYN43430B0.hcd` o `SYN43430A1.hcd` in `bm/`, scelta dalla sottoversione LMP |
| USB, video, audio, SD | come il Zero W (micro-USB OTG, mini-HDMI, audio HDMI, microSD) | | uguale |

- Avvio: il firmware carica `kernel7.img` a 0x8000 in modo HYP; `start.S` disattiva le
  trappole verso HYP, passa in SVC e mette i vettori delle eccezioni dove sono (VBAR),
  perché a 0x0 c'è lo stub dove aspettano gli altri tre core. MMU e cache partono prima
  di tutto il resto: senza MMU il Cortex-A53 vede la RAM come memoria "device", dove gli
  accessi non allineati che il codice ARMv7 fa liberamente sono eccezioni.
- La prima riga sullo schermo dice la scheda e il SoC (`Raspberry Pi Zero 2 W (BCM2710A1,
  revision 902120)`), la seconda il processore e il modo di avvio (`Cortex-A53 from HYP`).
- QEMU non ha il Zero 2 W: `kernel7.img` si prova in `raspi2b` (Pi 2 B: le stesse
  periferiche del BCM2710, un Cortex-A7, niente radio), `tests/qemu_test.py --kernel7`.

Aggiornare questo documento quando cambiano le misure o l'implementazione.
