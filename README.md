# bm33 — bare metal console per Raspberry Pi Zero W

MVP di una console bare metal (Assembly / C / Lua embedded) per
**Raspberry Pi Zero W v1.1** (SoC BCM2835, CPU ARM1176JZF-S, ARMv6).

## Roadmap

Dettagli, criteri di completamento e rischi in [docs/ROADMAP.md](docs/ROADMAP.md).
Risorse del Pi Zero W e quanto ne usano bm33/s32: [docs/HARDWARE.md](docs/HARDWARE.md).

| # | Obiettivo | Stato |
|---|-----------|-------|
| **M0** | Boot + test pattern HDMI | ✅ |
| **M1** | Debug: UART, eccezioni, chainloader seriale, CI | ✅ |
| **M2** | Console testuale su schermo | ✅ |
| **M3** | MMU, cache, heap, newlib | ✅ |
| **M4** | Interrupt, timer, double buffering 60 fps | ✅ |
| **M5** | Lua 5.4 embedded + REPL | ✅ |
| **M6** | Core **s32** in C: cartucce `.cart` compatibili con lua32 | ✅ |
| M7 | Input: pad GPIO, poi USB HID (→ input s32) | |
| M8 | SD + FAT, picker delle `.cart` | |
| M9 | **MVP**: launcher, giochi demo, immagine SD | |
| M10 | APU s32 su PWM (opzionale) | |

## Cosa fa il kernel (M0–M6)

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
8. attiva gli **interrupt**: tick di sistema a 1 kHz (system timer, compare 1), che
   fa anche lampeggiare il LED; misura la frequenza reale e la mostra
9. esegue per 15 s la cartuccia **s32** `demo.cart` (da lua32) a 320×224 in modalità
   *attract* (il quadrato si muove da solo), poi mostra le statistiche (tick, fps,
   tempo CPU per tick e per istruzione, tempo di rendering) e torna alla console
10. avvia **Lua 5.4.7** ed esegue lo script incorporato `src/script/boot.lua`:
    versione, alcune funzioni del linguaggio, un errore intercettato con `pcall`,
    micro-benchmark (fib, cicli, sort, stringhe) e memoria usata
11. avvia un **monitor** a tasto singolo sulla seriale:

| Tasto | Azione |
|-------|--------|
| `h` | aiuto |
| `l` | **REPL Lua** (Ctrl-D o `exit()` per tornare al monitor) |
| `i` | info di sistema |
| `c` | pulisce lo schermo |
| `m` | uso dell'heap |
| `k` | esegue di nuovo il benchmark |
| `d` | demo animata in C (60 fps, doppio buffer; un tasto la interrompe) |
| `g` | gioca `demo.cart` (s32): w/a/s/d o frecce, spazio = azione, q = esci |
| `t` | test pattern HDMI (un tasto qualsiasi torna alla console) |
| `r` | reboot via watchdog (con il chainloader, ricarica il kernel) |
| `u` `s` `b` `a` | test: undefined instruction, SVC, prefetch abort (BKPT), data abort |

Un'eccezione fatale stampa PC/LR/SP/CPSR, r0–r12, DFAR/DFSR o IFSR e
l'istruzione in errore, sulla seriale **e sullo schermo** (bianco su rosso),
e il LED lampeggia il codice. Senza cavo seriale basta quindi l'HDMI per il debug.

Senza adattatore seriale il monitor non riceve comandi, ma la console mostra
comunque il log di avvio e l'uptime nella barra di stato si aggiorna ogni secondo.

Stato del LED ACT:
- **acceso fisso**: inizializzazione in corso (se resta così, blocco prima degli interrupt)
- **lampeggio a 1 Hz**: kernel in esecuzione (generato dall'interrupt del timer:
  se si ferma, gli interrupt sono bloccati)
- **N lampeggi + pausa**: eccezione N (1 undef, 2 SVC, 3 prefetch abort, 4 data abort,
  6 IRQ, 7 FIQ, 9 panic)
- **lampeggio a 0,5 Hz** (cambia stato ogni secondo): chainloader in attesa del kernel

REPL Lua (M5, QEMU):

![lua](docs/m5-lua.png)

Demo animata su Pi Zero W reale: 600 frame in 10 s, intervallo tra frame
16667 µs costante, 0 frame persi, 2,7 ms di disegno per frame (su 16,7 disponibili),
timer IRQ misurato 999 Hz, nessun tearing visibile. Il ritmo è dato dal timer:
il vsync del firmware non è stato usato (vedi la riga `vsync probe` all'avvio).

Demo animata (M4, QEMU):

![demo](docs/m4-demo.png)

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

## s32

bm33 esegue le cartucce `.cart` della console **s32** del progetto
[lua32](https://github.com/f-accomando/lua32): stessa macchina (320×224, tile 8–64 px,
8 palette × 256 colori RGB888, VRAM 552 KiB, 512 sprite, APU a 8 canali), implementata
in C nativo. Il contratto comune è `spec/s32/s32-spec.md`; i vettori di conformità
generati da lua32 devono passare **byte per byte**:

```sh
make test-s32        # core s32 compilato per il PC
make test-s32-arm    # stesso codice compilato per ARM1176, in qemu-arm
scripts/sync-s32-spec.sh ../lua32   # aggiorna spec e vettori da lua32
```

Sul Pi Zero il core s32 esegue la demo a circa 26 µs per tick in QEMU (CPU s32 +
PPU in C); i numeri reali vanno misurati sul Pi (riga `s32:` all'avvio).

![s32 demo](docs/m6-s32-demo.png)

## Lua

Lua 5.4.7 completo (numeri double, interi a 64 bit, coroutine, string, table,
math, utf8, os, io su stdout/stdin). `print` scrive su seriale e schermo.
Gli errori non bloccano il kernel: vengono stampati in rosso con il traceback.
Lua ha un limite di 64 MiB di memoria; oltre, `not enough memory` (recuperabile).

Prestazioni misurate su Pi Zero W (1 GHz, MMU e cache attive): `fib(25)` 83 ms,
1 milione di addizioni in un ciclo 104 ms, `table.sort` di 100k interi 657 ms,
20k `tostring` + `table.concat` 104 ms. In un frame a 60 fps (16,7 ms, di cui
~2,7 ms per disegnare) restano circa 150k operazioni Lua semplici.

Modulo `bm33`:

| Funzione | Descrizione |
|----------|-------------|
| `bm33.micros()` | contatore a 1 MHz (intero) |
| `bm33.millis()` | millisecondi dal tick di sistema |
| `bm33.sleep(ms)` | attesa |
| `bm33.mem()` | byte usati da Lua, picco, byte in uso nell'heap C |
| `bm33.color(fg [, bg])` | colori della console 0–15 (ordine ANSI) |
| `bm33.cls()` | pulisce lo schermo |
| `bm33.reboot()` | riavvio (watchdog) |
| `bm33.version` | versione del kernel |

Senza seriale non puoi scrivere nel REPL; per ora lo script eseguito all'avvio
è `src/script/boot.lua` (modificalo e ricompila). Da M8 le cart Lua si
caricheranno dalla SD.

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
il kernel può essere grande fino a ~31 MiB. A 115200 baud la velocità è ~11 KB/s:
con Lua il kernel è ~340 KB, cioè ~30 s per caricarlo. Conviene `BAUD=921600` (deve essere uguale per build e `run-serial`,
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
src/kernel/irq.c         controller IRQ BCM2835, registrazione e dispatch
src/kernel/tick.c        tick di sistema (system timer compare 1)
src/kernel/demo.c        demo animata a 60 fps
src/gfx/draw.c           primitive: clear, rect, sprite 16×16, testo
src/s32/                 macchina s32: CPU, PPU, loader .cart, player 320×224
spec/s32/                specifica comune e vettori di conformità (da lua32)
tests/s32/               runner di conformità (host e ARM in qemu-arm)
src/script/luavm.c       stato Lua, allocatore con limite (64 MiB), esecuzione protetta
src/script/repl.c        REPL: espressioni, righe di continuazione, traceback
src/script/lib_bm33.c    modulo Lua `bm33`
src/script/boot.lua      script di avvio (incluso nell'immagine con .incbin)
third_party/lua/         Lua 5.4.7 non modificato (licenza MIT)
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
- Interrupt: `irq_entry` (vectors.S) salva il contesto con `srsdb`, passa in modo
  SVC, salva anche i registri VFP d0–d7 e FPSCR (il C hard-float può usarli),
  chiama `irq_handler()` e ritorna con `rfeia`. Gli IRQ non si annidano.
- Doppio buffer: framebuffer virtuale alto 2×360 righe; `fb_flip()` imposta il
  *virtual offset* sulla pagina appena disegnata e aspetta il vsync col tag
  *wait for vsync* (0x0004000E). Se il vsync manca o è finto (QEMU risponde subito),
  il ritmo dei frame lo dà il timer (60 Hz). QEMU inoltre sembra ignorare l'offset
  virtuale in visualizzazione: il tearing si verifica solo sul Pi reale.
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
