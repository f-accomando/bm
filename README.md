# bm33 — bare metal console per Raspberry Pi Zero W

MVP di una console bare metal (Assembly / C / Lua embedded) per
**Raspberry Pi Zero W v1.1** (SoC BCM2835, CPU ARM1176JZF-S, ARMv6).

## Roadmap

| Milestone | Obiettivo | Stato |
|-----------|-----------|-------|
| **M0** | Boot + schermata di test a colori via HDMI | ✅ |
| M1 | UART di debug, font bitmap, console testuale | |
| M2 | MMU + cache, heap, newlib | |
| M3 | Lua embedded (REPL su seriale/schermo) | |
| M4 | Input (USB HID / GPIO), API grafica per Lua | |

## Milestone 0

All'avvio il kernel:
1. (`src/boot/start.S`) maschera gli IRQ, imposta lo stack, abilita la VFP, azzera `.bss`
2. accende il LED ACT (GPIO47, attivo basso)
3. chiede al firmware VideoCore, tramite mailbox (canale property 8), un framebuffer 1280×720 a 32 bpp
4. disegna barre colore SMPTE al 75%, castellazioni inverse, rampe grigio/R/G/B,
   un bordo bianco di 1 px (per verificare l'overscan) e una croce al centro

Stato del LED ACT:
- **acceso fisso**: inizializzazione in corso (se resta così, il kernel si è bloccato)
- **lampeggio lento (1 Hz)**: tutto ok, pattern disegnato
- **lampeggio veloce (5 Hz)**: errore della mailbox / del framebuffer

Risultato in QEMU (`make qemu-screenshot`):

![test pattern](docs/m0-test-pattern.png)

## Requisiti

```sh
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi qemu-system-arm make curl
```

## Build

```sh
make                  # -> build/kernel.img
make qemu             # esegue in QEMU (-M raspi0) con finestra
make qemu-screenshot  # esecuzione headless, salva build/screen.png
```

## Scheda SD

1. Formatta la SD con una partizione **FAT32** (tabella MBR).
2. Scarica il firmware closed-source e prepara i file:
   ```sh
   make firmware     # bootcode.bin, start.elf, fixup.dat in firmware/
   make sdcard       # tutto in dist/
   ```
   Per fissare una versione del firmware: `FW_REF=<tag> make firmware`.
3. Copia il contenuto di `dist/` nella root della SD:
   `bootcode.bin  start.elf  fixup.dat  config.txt  kernel.img`
4. Collega l'HDMI (mini-HDMI) *prima* di alimentare il Pi.

## Struttura

```
boot/config.txt        configurazione del firmware (HDMI forzato, no overscan)
linker.ld              kernel caricato a 0x8000
src/boot/start.S       entry point ARM
src/kernel/main.c      kernel_main + pattern di test
src/drivers/mmio.h     base periferiche (0x20000000), barriere, alias bus
src/drivers/mbox.c     mailbox VideoCore
src/drivers/fb.c       framebuffer via property tags
src/drivers/led.c      LED ACT (GPIO47)
src/drivers/timer.c    system timer a 1 MHz
src/lib/string.c       memset/memcpy freestanding
scripts/               download firmware, screenshot QEMU
```

## Note tecniche

- Le periferiche BCM2835 sono a `0x20000000` (lato ARM); la RAM vista dalla GPU
  è all'alias `0x40000000` (L2 cached). L'indirizzo passato alla mailbox viene
  convertito con `ARM_TO_BUS()`, quello del framebuffer restituito con `BUS_TO_ARM()`.
- MMU e D-cache sono spente in M0, quindi non serve flush della cache tra ARM e GPU.
- L'ordine dei pixel (RGB/BGR) viene letto dalla risposta del firmware e
  gestito da `fb_color()`.
- Se il monitor sceglie una risoluzione strana, decommenta `hdmi_group=1` /
  `hdmi_mode=4` (720p60) in `boot/config.txt`.
