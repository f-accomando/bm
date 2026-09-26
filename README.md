# bm33 — bare metal console per Raspberry Pi Zero W

MVP di una console bare metal (Assembly / C / Lua embedded) per
**Raspberry Pi Zero W v1.1** (SoC BCM2835, CPU ARM1176JZF-S, ARMv6).

## Roadmap

Dettagli, criteri di completamento e rischi in [docs/ROADMAP.md](docs/ROADMAP.md).

| # | Obiettivo | Stato |
|---|-----------|-------|
| **M0** | Boot + test pattern HDMI | ✅ |
| **M1** | Debug: UART, eccezioni, chainloader seriale, CI | ✅ |
| **M2** | Console testuale su schermo | ✅ |
| **M3** | MMU, cache, heap, newlib | ✅ |
| M4 | Interrupt, timer, double buffering 60 fps | |
| M5 | Lua 5.4 embedded + REPL | |
| M6 | API grafica Lua + ciclo `_update`/`_draw` | |
| M7 | Input: pad GPIO, poi USB HID | |
| M8 | SD + FAT, caricamento delle cart | |
| M9 | **MVP**: launcher, giochi demo, immagine SD | |
| M10 | Audio PWM (opzionale) | |

## Cosa fa il kernel (M0–M3)

All'avvio:
1. `src/boot/start.S`: maschera gli IRQ, imposta uno stack per ogni modo della CPU,
   installa i vettori delle eccezioni a `0x0`, abilita la VFP, azzera `.bss`
2. inizializza il LED ACT e la seriale (PL011 su GPIO14/15, 115200 8N1)
3. stampa sulla seriale le info di sistema (revisione scheda, memoria, clock, temperatura)
4. ottiene dal firmware un framebuffer **640×360** a 32 bpp (la GPU lo scala
   sull'uscita HDMI: ×2 a 720p, ×3 a 1080p) e avvia la **console testuale**:
   80×21 caratteri, font 8×16, barra di stato con versione e uptime, colori ANSI
5. tutti i messaggi (`kprintf`, e `printf` di newlib) vanno sia sulla seriale sia sullo schermo
6. inizializza l'heap (da fine kernel a fine RAM ARM, ~447 MiB), esegue un
   **benchmark** tre volte: come lasciato dal firmware (700 MHz, senza cache), con il
   clock ARM al massimo (1 GHz, chiesto via mailbox) e con **MMU + cache** attive
7. esegue un self-test di newlib (stdio con float, libm, malloc/free) e mostra il riepilogo
8. avvia un **monitor** a tasto singolo sulla seriale:

| Tasto | Azione |
|-------|--------|
| `h` | aiuto |
| `i` | info di sistema |
| `c` | pulisce lo schermo |
| `m` | uso dell'heap |
| `k` | esegue di nuovo il benchmark |
| `t` | test pattern HDMI (un tasto qualsiasi torna alla console) |
| `r` | reboot via watchdog (con il chainloader, ricarica il kernel) |
| `u` `s` `b` `a` | test: undefined instruction, SVC, prefetch abort (BKPT), data abort |

Un'eccezione fatale stampa PC/LR/SP/CPSR, r0–r12, DFAR/DFSR o IFSR e
l'istruzione in errore, sulla seriale **e sullo schermo** (bianco su rosso),
e il LED lampeggia il codice. Senza cavo seriale basta quindi l'HDMI per il debug.

Senza adattatore seriale il monitor non riceve comandi, ma la console mostra
comunque il log di avvio e l'uptime nella barra di stato si aggiorna ogni secondo.

Stato del LED ACT:
- **acceso fisso**: inizializzazione in corso (se resta così, blocco prima della seriale)
- **lampeggio a 1 Hz**: kernel in esecuzione, monitor in attesa
- **N lampeggi + pausa**: eccezione N (1 undef, 2 SVC, 3 prefetch abort, 4 data abort,
  6 IRQ, 7 FIQ, 9 panic)
- **lampeggio a 0,5 Hz** (cambia stato ogni secondo): chainloader in attesa del kernel

Schermata di avvio (QEMU: i tempi non sono indicativi, QEMU non emula cache e clock):

![avvio](docs/m3-boot.png)

Benchmark misurato su Pi Zero W reale (µs, più basso è meglio):

| Test | 700 MHz, no cache | 1 GHz, no cache | 1 GHz + MMU/cache | Guadagno |
|------|------:|------:|------:|------:|
| fill 640×360 | 4270 | 4272 | 2160 | ×2,0 |
| memset 1 MiB | 7308 | 7316 | 2395 | ×3,1 |
| memcpy 1 MiB | 19390 | 19424 | 10269 | ×1,9 |
| crc32 64 KiB | 35720 | 35573 | 3670 | ×9,7 |
| float 100k | 8399 | 8304 | 1400 | ×5,9 |

Senza cache il clock non conta: ogni istruzione viene letta dalla SDRAM, quindi
700 MHz e 1 GHz danno gli stessi tempi. Il codice di calcolo (crc32, float) guadagna
6–10 volte con le cache; memset/memcpy/fill restano limitati dalla banda della RAM.

Console ed eccezione (M2):

![console](docs/m2-console.png) ![eccezione](docs/m2-exception.png)

Test pattern (comando `t`):

![test pattern](docs/m0-test-pattern.png)

## Requisiti

```sh
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi qemu-system-arm make curl python3
```

## Build e test

```sh
make                  # build/kernel.img + build/chainloader.img
make test             # test end-to-end in QEMU: boot, console, schermo, eccezioni, chainloader
make qemu             # esegue in QEMU (-M raspi0), seriale sul terminale
make qemu-screenshot  # esecuzione headless, salva build/screen.png
```

La CI GitHub Actions (`.github/workflows/ci.yml`) esegue build e `make test` a ogni push.
Se modifichi di proposito il test pattern: `python3 tests/qemu_test.py --update-ref`.
I test leggono il testo mostrato sullo schermo confrontando ogni cella 8×16
con i glifi del font, quindi verificano anche ciò che appare sull'HDMI.

## Collegamento seriale

Adattatore USB-seriale **a 3.3 V** (mai 5 V: danneggia il SoC). Non collegare il VCC.

| Pi Zero (header) | Adattatore |
|------------------|------------|
| pin 6 — GND | GND |
| pin 8 — GPIO14 TXD | RX |
| pin 10 — GPIO15 RXD | TX |

Su Linux aggiungi l'utente al gruppo `dialout`; su macOS la porta è `/dev/cu.usbserial-*`.

## Sviluppo con il chainloader (consigliato)

Il chainloader si scrive **una sola volta** sulla SD; da lì in poi ogni kernel
arriva dalla seriale.

```sh
make firmware
make sdcard-chainloader       # dist/ con chainloader.img come kernel.img
# copia dist/ sulla SD, inserisci la SD nel Pi

make run-serial PORT=/dev/ttyUSB0
# accendi il Pi: il kernel viene inviato e si apre il terminale (Ctrl-] per uscire)
```

Ciclo di sviluppo: modifica il codice, `make` in un altro terminale, premi `r`
nel terminale seriale → il Pi si riavvia e riceve il nuovo `build/kernel.img`.

Protocollo (vedi `chainloader/main.c`): il loader invia `\x03\x03\x03` ogni
secondo; il PC risponde `BM33` + dimensione + CRC-32; il loader verifica, copia il
kernel a `0x8000` e ci salta. Il chainloader si ricopia prima a `0x02000000`, quindi
il kernel può essere grande fino a ~31 MiB. A 115200 baud la velocità è ~11 KB/s;
per kernel più grandi usa `BAUD=921600` (deve essere uguale per build e `run-serial`,
e va rifatto anche il chainloader sulla SD).

## Aggiornare solo il kernel sulla SD (senza seriale)

Dalla cartella del progetto, con la SD montata (in WSL: `sudo mount -t drvfs D: /mnt/d`):

```sh
make sdcard
cp dist/kernel.img dist/config.txt /mnt/d/
```

## Scheda SD senza chainloader

1. Formatta la SD con una partizione **FAT32** (tabella MBR).
2. `make firmware && make sdcard` (per fissare una versione del firmware: `FW_REF=<tag> make firmware`).
3. Copia il contenuto di `dist/` nella root della SD:
   `bootcode.bin  start.elf  fixup.dat  config.txt  kernel.img`
4. Collega l'HDMI (mini-HDMI) *prima* di alimentare il Pi.

## Struttura

```
boot/config.txt          configurazione del firmware (HDMI forzato, no overscan)
linker.ld                kernel a 0x8000, stack per modo CPU
src/boot/start.S         entry point ARM, stack, vettori, VFP, .bss
src/kernel/main.c        kernel_main
src/kernel/vectors.S     tabella vettori + stub delle eccezioni
src/kernel/exceptions.c  dump dei registri, panic, schermo rosso, codice LED
src/kernel/monitor.c     monitor seriale a tasto singolo
src/kernel/sysinfo.c     info scheda via mailbox
src/kernel/testpattern.c test pattern HDMI
src/kernel/bench.c       benchmark (fill, memset, memcpy, crc32, float)
src/kernel/selftest.c    self-test di newlib
src/arch/mmu.c           tabella delle sezioni da 1 MiB, attivazione MMU e cache
src/arch/cache.c         clean/invalidate della D-cache per range (mailbox)
src/gfx/console.c        console testuale: celle, scroll, cursore, ANSI, barra di stato
src/gfx/font8x16.c       font 8×16 CP437 (derivato da Terminus, OFL: docs/LICENSE.font)
src/drivers/             mmio, mailbox, prop tags, framebuffer, gpio, uart (PL011),
                         timer, LED, watchdog
src/lib/                 kprintf, crc32, syscalls newlib (_sbrk, _write, ...)
chainloader/             bootloader seriale (si riloca a 0x02000000)
tools/bm33_load.py       invio del kernel + terminale seriale (solo stdlib Python)
tests/qemu_test.py       test end-to-end in QEMU
scripts/                 download firmware, screenshot QEMU, conversione font (psf2c.py)
```

## Note tecniche

- Le periferiche BCM2835 sono a `0x20000000` (lato ARM); la RAM vista dalla GPU
  è all'alias `0x40000000` (L2 cached). L'indirizzo passato alla mailbox viene
  convertito con `ARM_TO_BUS()`, quello del framebuffer restituito con `BUS_TO_ARM()`.
- Mappa di memoria (sezioni da 1 MiB, identità): RAM ARM cacheable write-back,
  memoria GPU (framebuffer) normal non-cacheable bufferable, periferiche device.
  Sull'ARM1176 il bit S rende la memoria non cacheable, quindi resta a 0.
- I buffer della mailbox stanno in RAM cacheable: `mbox_call()` fa clean+invalidate
  della D-cache prima e dopo la chiamata, perché la GPU legge la RAM, non la cache.
- Il firmware avvia l'ARM del Pi Zero a 700 MHz; il kernel chiede il massimo
  (`arm_freq`, 1 GHz) con i tag *get max clock rate* / *set clock rate*.
- Il kernel è C "hosted" su newlib (`libc.a`, `libm.a`, multilib `arm/v5te/hard`);
  le syscall sono in `src/lib/syscalls.c`. Il chainloader resta freestanding.
- L'ordine dei pixel (RGB/BGR) viene letto dalla risposta del firmware e
  gestito da `fb_color()`.
- Se il monitor sceglie una risoluzione strana, decommenta `hdmi_group=1` /
  `hdmi_mode=4` (720p60) in `boot/config.txt`.
- La PL011 del Pi Zero W è collegata di default al Bluetooth tramite GPIO32/33:
  il kernel la ricollega a GPIO14/15 (ALT0), quindi il Bluetooth non è
  utilizzabile (non serve per l'MVP).
- In QEMU le immagini vengono caricate con `-bios`, cioè a `0x8000` come fa il firmware reale.
