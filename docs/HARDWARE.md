# Raspberry Pi Zero W v1.1: risorse hardware e utilizzo di bm

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
| USB | 1 × micro-USB OTG (USB 2.0, controller DWC) *(D)* | host DWC2: **1 dispositivo HID** (tastiera o gamepad) in uso, anche dietro un hub *(M7b, M29)* | 1 porta |
| Bluetooth | BT 4.1 / BLE (BCM43438) *(D)* | fino a **4 DualShock 4** (M12, M16) e una tastiera BLE (M28) *(M)* | — |
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

Aggiornare questo documento quando cambiano le misure o l'implementazione.
