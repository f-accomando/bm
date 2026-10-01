# Raspberry Pi Zero W v1.1: risorse hardware e utilizzo di bm / s32

> **Nota (2026-09-30):** l'interprete s32 è stato tolto da bm, che non esegue più le
> cartucce `.cart`. Le colonne e le righe s32 qui sotto restano come riferimento storico.

Scopo: vedere **quanto dell'hardware usiamo** e **quanto margine resta**.
Alcune righe (DMA, USB, Bluetooth, audio) sono della fase MVP: lo stato attuale e le
scelte tecniche con le misure sono in [`PRESTAZIONI.md`](PRESTAZIONI.md).
Colonne:

- **Pi Zero W** — capacità dell'hardware (datasheet o misurate sul nostro Pi);
- **s32 (spec)** — cosa prevede la macchina s32 (vedi `docs/spec` in lua32);
- **bm oggi** — cosa usa l'implementazione attuale;
- **Uso** — quota della risorsa totale.

Legenda fonti: *(M)* misurato su Pi Zero W reale, *(Q)* misurato in QEMU,
*(D)* datasheet/documentazione Raspberry Pi, *(S)* specifica s32 v0.1.

## 1. Processore

| Risorsa | Pi Zero W | s32 (spec) | bm oggi | Uso / margine |
|---|---|---|---|---|
| CPU | ARM1176JZF-S (ARMv6), 1 core, 700 MHz all'avvio → **1000 MHz** *(M)* | CPU virtuale 16 bit, 83 opcode | 1 core a 1000 MHz, MMU + cache I/D + branch prediction attive *(M)*; CPU s32 interpretata in C | 1 core su 1 (100% disponibile per noi, nessun OS) |
| FPU | VFPv2, double in hardware *(D)* | — (solo interi 16 bit) | usata da Lua (double) e dal C | — |
| Cache | L1 16 KiB istruzioni + 16 KiB dati; L2 128 KiB nella VideoCore *(D)* | — | L1 attive | — |
| Divisione intera | non in hardware (libgcc) *(D)* | — | — | le divisioni costano, evitarle nei cicli caldi |
| Budget per frame | 16,7 ms a 60 fps | 1 tick = fino a 200 000 istruzioni s32 | fill 640×360: 2,2 ms; demo C: 2,7 ms di disegno *(M)* | ~84% del frame libero nella demo C |
| Lua | — | (cartucce Lua: proposta) | ~100 ns per operazione semplice: fib(25) 83 ms, 1M addizioni 104 ms *(M)* | ~140 000 operazioni Lua per frame a 60 fps |

## 2. Memoria

| Risorsa | Pi Zero W | s32 (spec) | bm oggi | Uso |
|---|---|---|---|---|
| RAM totale | **512 MiB** LPDDR2, condivisa CPU/GPU *(D)* | — | — | — |
| RAM dell'ARM | 448 MiB (con `gpu_mem=64`) *(M)* | spazio di indirizzi 16 MiB | kernel ~340 KiB + stack 1,1 MiB + heap ~447 MiB *(M)* | tutto disponibile |
| RAM della GPU | 64 MiB *(M)* | — | framebuffer 640×360×4×2 = 1,8 MiB | 2,8% della memoria GPU |
| WRAM | — | **128 KiB** *(S)* | dentro lo spazio s32 da 16 MiB (heap) | 0,03% della RAM ARM |
| VRAM | — | **552 KiB**: tilemap 32 KiB + directory 8 KiB + archivio 512 KiB *(S)* | come da spec | 0,12% |
| OAM | — | **4 KiB**, 512 sprite *(S)* | come da spec | ~0 |
| CGRAM | — | **6 KiB**, 8 palette × 256 colori RGB888 *(S)* | come da spec | ~0 |
| Porte + APU | — | 256 B + 128 B *(S)* | porte sì, APU registri senza uscita audio | ~0 |
| Macchina s32 completa | — | **16 MiB** di spazio di indirizzi, ~691 KiB assegnati *(S)* | 16 MiB allocati dall'heap al primo avvio di una cartuccia | 3,6% della RAM ARM |
| Memoria per Lua | — | — | limite **64 MiB** *(M)*; boot.lua ne usa 84 KiB, picco 3,4 MiB | 14% della RAM ARM come tetto |
| Cartuccia | microSD (GB) | demo.cart = 572 KiB (1 banco grafico da 520 KiB) | demo.cart incorporata nel kernel (kernel.img ~900 KiB) | trascurabile |

In pratica **la macchina s32 usa meno dell'1% della RAM del Pi**: la memoria non è un
vincolo; lo sono il tempo per frame e la banda verso la RAM (memcpy ~100 MB/s,
riempimento ~430 MB/s *(M)*).

## 3. Video

| Risorsa | Pi Zero W | s32 (spec) | bm oggi | Uso |
|---|---|---|---|---|
| Uscita | mini-HDMI fino a 1920×1080 a 60 Hz; composito (pad TV) *(D)* | — | HDMI *(M)* | — |
| GPU | VideoCore IV, core 250 MHz *(M)*, OpenGL ES 2.0, scaler hardware (HVS) *(D)*; 3D (V3D): 12 QPU in 3 slice, texture filtrate, tile 64×64 con z a 24 bit nel chip *(D)* | — | lo scaler (framebuffer ingrandito dalla GPU); il 3D delle cartucce è software sull'ARM; M30: driver V3D minimo e prova `g` | GPU 3D: prima prova (M30) |
| Risoluzione logica | qualsiasi, scalata dalla GPU | **320×224** (4:3); 384×224 (16:9) previsto *(S)* | console 640×360 (32 bit); cartucce s32 320×224 (32 bit); **cartucce native 640×360 RGB565** | 640×360 = 11% dei pixel di 1080p |
| Colori | framebuffer 32 bit (16,7 milioni) *(M)* | 8 palette × 256 colori a 24 bit; tile a 8 bit indicizzati *(S)* | 32 bit per console e s32; 16 bit RGB565 (65 536 colori) per le native | — |
| Tile | — | 2048, taglie 8/16/32/64 px *(S)* | — | — |
| Sprite | nessun limite hardware (disegno software) | **512** *(S)* | 64 nella demo C | — |
| Frequenza | 60 Hz (vsync del firmware **non disponibile** su Pi Zero, tag non supportato *(M)*) | 60 tick/s (30 opzionale) *(S)* | 60 fps dal timer, 0 frame persi *(M)* | — |
| Doppio buffer | sì (virtual offset) *(M)* | — | attivo | — |

## 4. Audio

| Risorsa | Pi Zero W | s32 (spec) | bm oggi | Uso |
|---|---|---|---|---|
| Uscite | **HDMI** audio; nessun jack; 2 canali PWM su GPIO (serve filtro RC) *(D)* | — | nessuna (M10) | — |
| Sintesi | software (CPU) + DMA per l'uscita | **8 canali**: quadra/triangolo/dente di sega/rumore con ADSR *(S)* | — | — |

## 5. Input e periferiche

| Risorsa | Pi Zero W | s32 (spec) | bm oggi | Uso |
|---|---|---|---|---|
| USB | 1 × micro-USB OTG (USB 2.0, controller DWC) *(D)* | — | host DWC2: **1 dispositivo HID** (tastiera o gamepad) in uso, anche dietro un hub *(M7b, M29)* | 1 porta |
| GPIO | header a 40 pin (28 GPIO, da saldare sul Zero W) *(D)* | — | GPIO14/15 UART, GPIO47 LED | 2 su 28 |
| UART | PL011 + mini UART *(D)* | — | PL011 a 115200 baud, clock 48 MHz *(M)* | — |
| Giocatori | limitati da USB/GPIO | **8** porte di input (5 bit usati: frecce + azione) *(S)* | — | — |
| Timer | system timer 1 MHz, 4 comparatori (2 liberi per l'ARM) *(D)* | — | comparatore 1 a 1 kHz *(M)* | 1 su 2 |
| DMA | 16 canali *(D)* | — | non usato | 0 |
| Wi-Fi / Bluetooth | 802.11 b/g/n 2,4 GHz + BT 4.1/BLE (BCM43438) *(D)* | — | **non usati** (driver bare metal molto complessi) | 0 |
| Fotocamera | connettore CSI *(D)* | — | non usato | 0 |
| Storage | microSD *(D)* | — | lettura FAT16/32 via EMMC (PIO, 4 bit, 25 MHz): cartucce in `/carts` *(M8)* | sola lettura |

## 6. Cosa ci dicono i numeri

- **Memoria**: enorme margine. s32 potrebbe avere più VRAM o più banchi caricati
  insieme senza problemi; la scelta di restare a 552 KiB è di **design**, non un limite.
- **Tempo per frame**: è la risorsa vera. Il disegno in C lascia oltre l'80% del frame;
  una CPU s32 interpretata in C e la PPU a 320×224 dovrebbero stare in pochi ms.
- **Banda di memoria**: il collo di bottiglia per grafica e copie (memcpy lenta,
  ottimizzabile).
- **Non usati**: GPU 3D, DMA, USB, Wi-Fi/BT, fotocamera: tutto potenziale per il futuro
  (input USB in M7, DMA per l'audio in M10).

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
