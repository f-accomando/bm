# bm — BareMetal

> Il vecchio README, in italiano e completo: la presentazione in inglese, con lo showreel,
> è in [README.md](README.md).

**bm** (BareMetal) è una console bare metal (Assembly / C / Lua embedded) per
**Raspberry Pi Zero W v1.1** (SoC BCM2835, CPU ARM1176JZF-S, ARMv6): nessun sistema
operativo, il kernel parte direttamente dalla SD. Le sue cartucce native hanno
l'estensione **`.bm`**; impostazioni, salvataggi e firmware stanno nella cartella
**`bm/`** della SD.
Lo stesso kernel gira sul **Raspberry Pi 1** (A, B, A+, B+: stesso SoC), con l'Ethernet
del Pi 1 B / B+ al posto del WiFi (M29, vedi [Raspberry Pi 1](#raspberry-pi-1-b-e-b-m29)).

## In breve: giocare

```sh
make firmware && make image      # dist/bm.img (64 MiB): firmware, kernel e giochi
```

Scrivi `dist/bm.img` sulla microSD con **Raspberry Pi Imager** ("Use custom"),
balenaEtcher o `dd`; collega HDMI e una **tastiera o un gamepad USB** (adattatore OTG
sulla porta micro-USB centrale) e accendi. Il Pi si avvia in un paio di secondi sul
**menu delle cartucce** (in 3D: ogni gioco è una piccola scheda a forma di Memory
Stick Duo con la copertina stampata sopra e i contatti in rame sul retro): Pong,
Snake, Star Shooter, Astro Wing (3D), Hunter's Night (gotico, 320×180 con luci), Chaos Kitchen, Titan Clash,
**nano8** (le cartucce `.p8` / `.p8.png`, vedi [nano8](#nano8-cartucce-p8-e-p8png-m23)) e le demo; Texture Room (3D con
texture) è il benchmark 3D della scheda Dev. Frecce per scegliere,
Nel menu le copertine stanno in una griglia (schede **Games** e **Dev**); frecce per
muoversi, Invio (o A) per giocare, **L1 / R1** (Q / E sulla tastiera) per cambiare scheda.
**Esc** (o Start+Select, o PS) torna al menu e lascia
il gioco **sospeso**: la copertina mostra "Playing" e A lo riprende dal punto in cui era.
Avviare un altro gioco chiede prima di chiudere quello sospeso.
**X** su una copertina apre le sue opzioni (riprendi, chiudi, apri nell'SDK, nel Sound
editor, nello studio 3D, in bm Mesh o in bm Pixel, informazioni, cancella il salvataggio,
elimina dalla SD); la scheda **Dev** ha l'SDK, il **Sound editor** (suoni, effetti sonori e
musica dei giochi), lo **studio 3D** (modelli e animazioni), **bm Mesh** (le mesh, anche
quelle del codice), **bm Pixel** (la pixel art dello sprite sheet) e gli strumenti del
monitor (Lua, sistema,
registro, test, benchmark); **Settings**, l'ultima scheda, apre
subito il suo pannello: controller, WiFi, layout della tastiera, grafica (disegno dei
giochi, chi disegna il 3D, anti-aliasing), **volume** e sistema (M27, BareMetal UI). Nei giochi **START** mette in pausa: lì si
regola anche il volume. Tutto si usa col solo controller. Nel menu **PS** torna a
Games e chiude i pannelli; nel monitor apre il menu.
Per scrivere un gioco: [docs/GUIDA-GIOCHI.md](docs/GUIDA-GIOCHI.md) (guida pratica) e
[docs/API-IT.md](docs/API-IT.md) (riferimento); in inglese [docs/GAME-GUIDE.md](docs/GAME-GUIDE.md) e
[docs/API.md](docs/API.md).

## bm Studio e bm Animator: le risorse sul PC

Due applicazioni per il PC ([sdk/README.md](sdk/README.md)) fanno le risorse delle
cartucce. Lavorano direttamente sul `.bm` (lo aprono e lo salvano al suo posto, anche
sulla SD) e scambiano `.glb` e `.png` con gli altri programmi. Sono pagine web senza
dipendenze: doppio clic su `sdk/studio/index.html` o `sdk/animator/index.html` (Chrome o
Edge), oppure `make studio`.

- **bm Studio**: **modelli 3D a tessere** (si posano le tessere
  dello sprite sheet su una griglia, si impilano blocchi, si spostano gli angoli per tetti e
  rampe, si dipinge sul modello) e la **pixel art dello sprite sheet**. Nel gioco:
  `m = model("casa")`, poi `draw3d(m, x, y, z)`.
- **bm Animator**: **scheletro** (ossa e pelle), **animazioni a keyframe** sulla linea del
  tempo, riprodotte dalla console con `animate(m, "walk", t)` (**animazione scheletrica**),
  e le animazioni **pre-renderizzate in sprite** (da 1 a 8 direzioni) nello sprite sheet.

Esempio: *Studio Village* (`carts/village`): i modelli dello Studio e un paesano animato.

Sulla console, nella scheda **Dev**, lo **studio 3D** ne è la versione semplificata, sugli
stessi file: un **player** dei modelli e delle animazioni (con lo scheletro e il misto di
due animazioni) e gli attrezzi essenziali per **costruire** a blocchi e tessere, fare lo
**scheletro** e **animare** a keyframe, con la tastiera o il gamepad
([sdk/README.md](sdk/README.md#sulla-console-lo-studio-3d)). **bm Mesh**, sempre nella
scheda Dev, modifica vertici e facce di tutte le mesh di un `.bm`, anche quelle che il
codice del gioco costruisce con `mesh()` (le navi di Astro Wing): le copia come modelli
(da mesh a modello) o come funzioni `mesh_nome()` nel codice (da modello a mesh)
([sdk/README.md](sdk/README.md#sulla-console-bm-mesh)). **bm Pixel** disegna lo sprite
sheet: matita, riempimento, linee, rettangoli, ovali, selezioni da copiare, specchiare e
girare, disegno a specchio, tavolozza fino a 256 colori salvata con lo sheet, animazione
dei fotogrammi con onion skin, la base di uno sprite dall'assistente
([sdk/README.md](sdk/README.md#sulla-console-bm-pixel)).

## Roadmap

Dettagli, criteri di completamento e rischi in [docs/ROADMAP.md](docs/ROADMAP.md).
Risorse del Pi Zero W e quanto ne usa bm: [docs/HARDWARE.md](docs/HARDWARE.md).
Stress test di rendering (soglie 60/30 fps): [docs/STRESS.md](docs/STRESS.md) — `make sdcard-stress`.
Prestazioni e scelte tecniche (limiti del Pi, atteso contro misurato): [docs/PRESTAZIONI.md](docs/PRESTAZIONI.md).
Risoluzioni di menu, console e giochi, e quanto costano: [docs/RISOLUZIONI.md](docs/RISOLUZIONI.md).

| # | Obiettivo | Stato |
|---|-----------|-------|
| **M0** | Boot + test pattern HDMI | ✅ |
| **M1** | Debug: UART, eccezioni, chainloader seriale, CI | ✅ |
| **M2** | Console testuale su schermo | ✅ |
| **M3** | MMU, cache, heap, newlib | ✅ |
| **M4** | Interrupt, timer, double buffering 60 fps | ✅ |
| **M5** | Lua 5.4 embedded + REPL | ✅ |
| M6 | Un secondo formato di cartucce (`.cart`), interpretato in C | ✅, poi rimosso (2026-09-30) |
| **M7** | Cartucce native **`.bm`**: Lua 5.4 + grafica C a 640×360 RGB565 | ✅ |
| **M7b** | Input: tastiera e gamepad **USB** (HID) | ✅ tastiera verificata sul Pi (gamepad solo QEMU) |
| **M8** | **SD** + FAT32, menu delle cartucce | ✅ verificato sul Pi |
| **M9** | **MVP**: avvio sul menu, giochi demo, immagine SD, guida API | ✅ verificato sul Pi |
| M10 | Audio: HDMI, sintetizzatore a 8 voci, suoni nei giochi | ✅ verificato sul Pi |
| **M11** | SD in scrittura: salvataggi, record, impostazioni | ✅ verificato sul Pi |
| **M12** | Controller **Bluetooth** (DualShock 4) | ✅ verificato sul Pi |
| M13 | Cartucce con codice ARM nativo | chiusa senza implementazione |
| **M14** | Grafica 2.0: DMA, 3D con texture e Gouraud, menu con anteprime (32 bit rimandato) | ✅ verificato sul Pi |
| **M15** | Editor sulla console: codice, sprite, mappa, prova e torna | ✅ verificato sul Pi |
| **M16** | Multiplayer locale: fino a 4 controller Bluetooth, `btn(i, giocatore)`, Pong a 2 | ✅ |
| **M17** | **Chaos Kitchen**: cucina cooperativa in 3D per 1–4 giocatori (campagna, infinita, pratica) | ✅ |
| **M18** | WiFi, console di rete con password, invio di kernel e cartucce dal PC | ✅ verificato sul Pi |
| M19 | HTTPS: aggiornamenti da GitHub, "git leggero" (archivi e API con token) | ✅ chiusa (2026-10-05) |
| **M20** | **Titan Clash**: picchiaduro 2D a robot giganti; prima base giocabile (1 robot, armatura leggera/pesante, spada o cannoni, hangar, contro CPU o in 2) | ✅ base giocabile |
| **M21** | Menu "home" a griglia (Games / Dev) e giochi sospesi in memoria | ✅ |
| M22 | SDK e strumenti dedicati: codice, pixel art, 3D, musica, import/export, 3D→sprite, sprite stacking | ✅ chiusa (2026-10-05) |
| M23 | Emulatore di cartucce `.p8` / `.p8.png` (stile PICO-8): **nano8** | ✅ chiusa (2026-10-05) |
| M24 | Scambio in rete locale tra console (P2P) | ✅ chiusa con M25 (2026-10-05) |
| M25 | Store su GitHub: catalogo, download verificati, pubblicazione dall'SDK | ✅ chiusa (2026-10-05): il **Market** |
| M26 | Market gratuito, legato allo store di M25 | unita a M25 (2026-10-01) |
| **M27** | **BareMetal UI**: sottomenu, opzioni delle cartucce, strumenti nella scheda Dev, impostazioni | ✅ chiusa: task 1–4 verificati sul Pi |
| **M28** | Tastiera Bluetooth LE (MX Keys S): pairing con codice, HID over GATT, riconnessione | ✅ verificato sul Pi |
| **M29** | **Pi 1 B**: stesso kernel, hub USB (split transactions), Ethernet LAN9512, immagine `bm-pi1.img` | ✅ verificato sul Pi 1 B |
| M30 | **Assistente AI** per lo sviluppo: domande su API e codice, errori, base degli sprite; rete INT8 sulla console | ✅ chiusa (2026-10-05) |
| M31 | **Pi Zero 2 W**: `kernel7.img`, gli stessi sorgenti per ARMv7, una SD per tutte le schede | ✅ chiusa (2026-10-05; spenta nelle build) |
| M32 | **Mouse** USB e Bluetooth (LE e classico) e puntatore: nel menu, nelle cartucce che lo chiedono (`mouse()`), anche con la levetta destra dei pad | ✅ chiusa (2026-10-05) |
| **M33** | **GPU e 3D più veloce**: driver V3D nostro sotto `draw3d`, rasterizzatore ARM 2×, modo 480×270, Texture Room a 640×360 e 60 fps | ✅ verificato sul Pi |
| M34 | **GPU 2**: anti-aliasing MSAA 4×, texture in T-format, pagina pulita senza load, meno lavoro per triangolo sull'ARM | in corso (passi 1–4 da provare sul Pi) |

## Cosa fa il kernel

All'avvio (circa 2 secondi):
1. `src/boot/start.S`: maschera gli IRQ, imposta uno stack per ogni modo della CPU,
   installa i vettori delle eccezioni a `0x0`, abilita la VFP, azzera `.bss`
2. inizializza il LED ACT e la seriale (PL011 su GPIO14/15, 115200 8N1)
3. ottiene dal firmware un framebuffer **640×360** a 32 bpp (la GPU lo scala
   sull'uscita HDMI: ×2 a 720p, ×3 a 1080p) e avvia la **console testuale**:
   80×21 caratteri, font 8×16, barra di stato con versione e uptime, colori ANSI;
   tutti i messaggi (`kprintf`, e `printf` di newlib) vanno sia sulla seriale sia sullo schermo
4. heap (da fine kernel a fine RAM ARM, ~445 MiB), clock ARM al massimo (1 GHz),
   **MMU + cache**; riga con scheda, clock, memoria e temperatura
5. **interrupt**: tick di sistema a 1 kHz (system timer, compare 1), che fa anche
   lampeggiare il LED; misura la frequenza reale e la mostra
6. **USB**: riconosce il dispositivo collegato (righe `usb: ...`), poi legge la **SD**
   e cerca le cartucce (riga `sd: SDHC card, FAT32, ...; N cartridges`)
7. apre il **menu delle cartucce**; **Ctrl+Esc** (o Start+Select, o `q` dalla seriale) porta
   al **monitor** a tasto singolo (dalla seriale o dalla tastiera USB); Esc da solo torna
   indietro come B; il PS di un controller, dal monitor, riporta al menu

La sequenza di avvio delle versioni precedenti (benchmark CPU, self-test di newlib,
benchmark e demo `.bm`, sonda del vsync, script Lua `boot.lua`) si esegue dal monitor
con **`b`**.

| Tasto | Azione |
|-------|--------|
| `h` | aiuto, a pagine: frecce su/giù, PagSu/PagGiù, spazio (dalla seriale `w`/`s`); `q` o Esc esce |
| `b` / `B` | diagnostica: la vecchia sequenza di avvio (benchmark, demo bm, `boot.lua`) |
| `l` | **REPL Lua** (Esc su riga vuota, Ctrl-D o `exit()` per tornare al monitor) |
| `i` | info di sistema |
| `c` | pulisce lo schermo |
| `m` | uso dell'heap |
| `k` | esegue di nuovo il benchmark |
| `d` | demo animata in C (60 fps, doppio buffer; un tasto la interrompe) |
| `n` | gioca `demo.bm` (nativa): frecce/wasd, spazio = A, k/x = B, q o Esc = esci |
| `M` | **menu delle cartucce** (SD; le demo incorporate se la SD non ne ha) |
| `f` / `F` | elenca le cartucce / rilegge la SD |
| `y` | USB: cerca di nuovo il dispositivo (dopo averlo collegato), anche dietro un hub |
| `Y` | input: test USB dal vivo (contatori ok/nak/err e ultimo report), poi per 10 s i tasti tenuti da ogni giocatore (P1–P4; `*` = tastiera/seriale) |
| `L` | layout tastiera: italiano ↔ US |
| `D` | test del DMA passo per passo (copie e riempimenti, tempi CPU contro DMA) |
| `g` | test della GPU 3D (V3D) passo per passo: accensione, un triangolo, z-buffer, velocità, un'immagine disegnata dalla GPU, poi la stessa scena 3D da ARM e GPU affiancate con i tempi, una scena 3D-2D-3D (z-buffer conservato), texture in ordine di riga e a tile, la scena senza e con MSAA 4× (M33, M34; anche "GPU test" nella scheda Dev) |
| `R` | **Texture Room**, benchmark 3D: la stanza con le casse raddoppiate da 8 finché tiene 30 fps, a 320×180 e poi a 640×360, con l'ARM e poi con la GPU; una riga per passo (triangoli, ms, fps) e il riepilogo delle casse a 60 e a 30 fps (M33; anche "Texture Room" nella scheda Dev) |
| `e` | **editor** dei giochi `.bm` (codice, sprite, mappa; è anche nella scheda Dev del menu) |
| `A` | **Sound editor**: suoni, effetti sonori e musica dei giochi `.bm` (anche nella scheda Dev) |
| `3` | **studio 3D**: modelli e animazioni di un `.bm` (player, blocchi e tessere, ossa, keyframe; anche nella scheda Dev) |
| `4` | **bm Mesh**: le mesh di un `.bm`, anche quelle che costruisce il suo codice: vertici e facce, da mesh a modello e da modello a codice (anche nella scheda Dev) |
| `5` | **bm Pixel**: lo sprite sheet di un `.bm`: pixel art, tavolozza, animazione dei fotogrammi (anche nella scheda Dev) |
| `C` | **bm Code**: l'editor del codice (tab, due pagine affiancate, font 6x12; anche nella scheda Dev) |
| `I` | **Assistant** (M30): come si scrive il codice, basi di sprite (F6 negli strumenti; anche nella scheda Dev) |
| `a` | audio: stato dell'uscita HDMI (clock, canale DMA, costo della sintesi, volume) e una prova: le sei forme d'onda, un accordo, glide, vibrato e arpeggio |
| `T` | Bluetooth: cerca per 8 s e **abbina il primo controller** trovato come **prossimo giocatore** (fino a 4; DS4: Share + PS finché lampeggia); la console seriale passa alla mini UART (stessi pin) |
| `O` | Bluetooth: **abbina un mouse** (M32) in modalità abbinamento: prima cerca per 10 s un mouse Bluetooth LE, poi per 8 s uno classico; nessun codice da digitare |
| `P` | Bluetooth: **dimentica tutti i pad**, la tastiera e il mouse abbinati (chiede conferma con `y`): chiavi tolte da `bm/config.txt`, dispositivi scollegati; poi si riabbinano con `T`, `K`, `O` |
| `o` | **log dell'avvio**: tutto quello che il kernel ha scritto dall'accensione (primi 64 KiB), a pagine |
| `W` | WiFi (M18): accende il chip e lo identifica, un passo per riga |
| `E` | Ethernet (Pi 1 B / B+, M29): link, contatori dei frame, registri del chip, indirizzo IP |
| `p` | benchmark di rendering 640×360 RGB565, disegnando direttamente sullo schermo e via RAM |
| `V` | cartucce `.bm`: disegno diretto sullo schermo (default) o via buffer in RAM |
| `U` | riceve una cartuccia dalla seriale (`bm_load.py PORTA --cart file.bm`) e la esegue |
| `s` / `S` | stress test di rendering (sprite, triangoli, 3D; C e Lua): vedi [docs/STRESS.md](docs/STRESS.md) |
| `t` | test pattern HDMI (un tasto qualsiasi torna alla console) |
| `r` | reboot via watchdog (con il chainloader, ricarica il kernel) |
| `X` poi `u` `s` `b` `a` | test di crash: undefined instruction, SVC, prefetch abort (BKPT), data abort (due tasti, per non fermare la console per errore) |
| `X` poi `f` | blocco simulato (interrupt spenti): il watchdog riavvia il Pi in 3 s e all'avvio compare cosa stava facendo |

Un'eccezione fatale stampa PC/LR/SP/CPSR, r0–r12, DFAR/DFSR o IFSR e
l'istruzione in errore, sulla seriale **e sullo schermo** (bianco su rosso),
e il LED lampeggia il codice. Senza cavo seriale basta quindi l'HDMI per il debug.

Senza adattatore seriale basta una **tastiera USB**: i comandi del monitor e il
REPL Lua funzionano anche da lì.

## Tastiera, gamepad e SD (M7b, M8)

**USB.** Il Pi Zero W ha una sola porta micro-USB OTG (quella vicino al centro,
*non* quella di alimentazione): serve un adattatore OTG micro-USB → USB-A.
Si usa **un dispositivo alla volta** collegato direttamente (niente hub USB).
Il dispositivo va collegato prima dell'accensione (o dopo, con il comando `y`).

All'avvio compare una riga `usb: ifN class ...` per ogni interfaccia del dispositivo
e poi quella scelta; con tastiere composite (es. Apple Magic Keyboard, verificata)
viene scelta l'interfaccia tastiera, anche se il dispositivo usa i report con ID.

- **Tastiera** (protocollo boot HID): layout **italiano** (`L` passa a US), lettere
  accentate, ripetizione dei tasti. Nei giochi: frecce o WASD, spazio/Z/J = A,
  X/K = B, Invio = Start, Tab = Select, **Esc = esci**.
- **Gamepad HID generici** (il descrittore HID viene analizzato: pulsanti, assi X/Y,
  croce direzionale) e **controller Xbox 360 cablati**: croce o levetta sinistra,
  A/X = A, B/Y = B, **Start+Select (Back) = esci**.
- **DualShock 4 (PS4) via Bluetooth** (M12, M16): dal monitor `T` con il controller in
  abbinamento (Share + PS finché la luce lampeggia). Fino a **4 controller**, uno per
  giocatore: ogni `T` abbina il prossimo, la chiave va in `bm/config.txt`
  (`bt_pad1=` … `bt_pad4=`; il vecchio `bt_pad=` diventa il giocatore 1). Dalle accensioni
  successive il Bluetooth parte da solo (circa 3 s in più all'avvio per il firmware del
  chip) e basta premere **PS**: la luce del pad prende il colore del giocatore (1 blu,
  2 rosso, 3 verde, 4 rosa). Il menu mostra in alto a destra un'icona per ogni giocatore collegato (controller o tastiera,
  con il numero del giocatore in un cerchio) e l'icona WiFi o Ethernet quando la console è in rete.
  Stessi tasti del cavo USB; tastiera e gamepad USB sono il primo giocatore senza pad,
  la tastiera Bluetooth il successivo (ognuna col suo personaggio nei giochi).
- **DualShock 4 (PS4)** via cavo USB: croce direzionale o levetta sinistra,
  croce/quadrato = A, cerchio/triangolo = B, Options = Start, Share = Select,
  **tasto PS (o Share+Options) = esci**. Lo stesso decodificatore servirà per il
  Bluetooth (M12).

**Mouse (M32).** Un mouse USB (anche con la tastiera, sullo stesso ricevitore o dietro un
hub) o Bluetooth (LE, come i Logitech MX, o classico: Settings > Controllers > *Pair a
mouse*, o `O` dal monitor) muove il **puntatore**; senza mouse lo muove la **levetta
destra** di un pad (R2 o R3 = tasto sinistro, L2 = destro). Vedi sotto.

**SD.** All'avvio il kernel legge la prima partizione **FAT32** (o FAT16) della SD
(quella da cui si avvia il Pi) e cerca i file **`.bm`** nella
cartella `carts/` e nella radice. Nomi lunghi supportati. `make sdcard` mette in
`dist/carts/` i giochi (`pong.bm`, `snake.bm`, `shooter.bm`, `astrowing.bm`, `hunt.bm`,
`kitchen.bm`, `titan.bm`, `village.bm`, ...); `make image` li mette nell'immagine SD. La demo
nativa, lo stress test e Texture Room non sono giochi: restano nel kernel (comando `n` del
monitor, Stress test e Texture Room nella scheda Dev) e `make install` li toglie dalla SD.

**Menu delle cartucce.** Mostra titolo e autore letti dalle cartucce (ordinate per
titolo) e sotto il nome del file scelto. Su/giù per scegliere, Invio (o A) per giocare,
Esc (o Start+Select) per tornare al menu dal gioco; dal menu al monitor **Ctrl+Esc** (o
Start+Select, `q` dalla seriale): Esc da solo torna indietro come B; `R` rilegge la SD.
L1 / R1 (Q / E o PagSu / PagGiù sulla tastiera USB, Tab) cambiano scheda.
X (tasto C sulla tastiera USB) apre le opzioni della cartuccia, B (tasto X) torna indietro
nei pannelli. Dalla seriale: w/a/s/d, Invio, `x` opzioni, `[` `]` o `1` `2` `3` schede, q. Per
aggiungere un gioco basta copiarlo in `carts/` sulla SD dal PC.

**Scrittura (M11).** bm scrive solo nella cartella `bm/` della SD:
`bm/config.txt` (layout della tastiera, modo di disegno; si può modificare anche dal
PC) e `bm/save/*.SAV` (salvataggi e record delle cartucce: `save()`/`saved()`).
In `bm/config.txt`, `dma_zclear=0` fa pulire lo z-buffer dalla CPU invece che dal DMA
(M33; da usare solo se un gioco 3D si blocca), `gpu3d=0` fa disegnare il 3D dei
giochi all'ARM invece che alla GPU (M33: *Impostazioni > Graphics > 3D of the games*; se
la GPU non risponde si torna all'ARM da soli) e `gpu3d_aa=1` accende l'anti-aliasing
MSAA 4× del 3D sulla GPU (M34: *Impostazioni > Graphics > 3D anti-aliasing*).

Limiti attuali: un solo dispositivo USB (più un mouse), collegato prima dell'accensione.

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

## Mouse e puntatore (M32)

- **Dove c'è**: nel menu di bm sempre; nelle app (giochi, strumenti) solo se l'app lo chiede
  con `mouse(true)` (vedi [docs/API-IT.md](docs/API-IT.md)); nella console testuale no.
- **Quando si vede**: quando qualcosa lo muove. Con un mouse collegato compare subito; con
  la sola levetta destra appena la si muove. Nel menu i tasti e la croce lo nascondono
  (la selezione si sposta) finché non si muove di nuovo.
- **Nel menu**: passando sopra una copertina (o una riga di un pannello) la si sceglie; il
  tasto sinistro fa quello che farebbe A lì (gioca, apre, cambia scheda, i pulsanti in
  basso), il destro apre le opzioni della copertina o torna indietro; un clic fuori da un
  pannello o da una domanda li chiude; la rotellina scorre le righe.
- **Barra in alto**: un mouse bianco, senza numero (non è un giocatore); con un **pallino
  blu** se è Bluetooth.
- **Spegnerlo per tutto il sistema**: `mouse=off` in `bm/config.txt` (dal PC); non c'è una
  voce nel menu. Di base è acceso.
- **Bluetooth**: i mouse LE (HID over GATT, abbinamento senza codice) e quelli classici
  (protocollo boot). Chiavi in `bm/config.txt`: `bt_mouse` e `bt_mouse_key` (LE) o
  `bt_mouse_classic`. Il mouse si ricollega da solo quando lo si muove o si clicca; la
  tastiera Bluetooth e il mouse restano collegati insieme.
- **nano8**: le cartucce `.p8` che usano il mouse (`poke(0x5f2d, 1)`) seguono il puntatore
  appena compare; prima, come sempre, la levetta sinistra, la croce o le frecce.

## Che versione ho sulla SD?

La sigla nella barra azzurra in alto a sinistra (es. `bm 1b31924`) è il commit git
del kernel. `git log --oneline` mostra a quale milestone corrisponde; se compare
`-dirty` il kernel contiene modifiche locali non committate.

| Commit | Contenuto |
|---|---|
| `7044581` | M5: Lua embedded |
| `ab9af30` | M6: il secondo formato di cartucce (poi rimosso) |
| `c7ec2c3` | M7: cartucce native .bm (demo nativa all'avvio) |
| `1b31924` | stress test di rendering e 3D software (`make sdcard-stress`) |
| `a2a8b6f` | M7b + M8: tastiera/gamepad USB, SD e menu delle cartucce |
| `25f5dbc` | tastiere USB composite (Apple Magic Keyboard) |
| `a7223f7` | M9: avvio direttamente sul menu (diagnostica con `B`); dopo: giochi demo, `make image` |

## Requisiti

```sh
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi qemu-system-arm make curl python3 \
    dosfstools mtools     # per i test SD in QEMU
```

## Dalla SD con WSL: `./easy_install.sh`

Nella root del repository, da WSL: `./easy_install.sh` (o `easy_install` da qualunque
cartella, se al primo avvio accetti di aggiungere il comando). La prima volta salva la cartella
del repository e la lettera della SD in `.easy_install.conf` (resta sul PC: è in `.gitignore`,
e tiene anche cosa ha installato e l'ultima operazione), installa i pacchetti che servono alla
build (`apt`) e scarica il firmware del Pi (`make firmware`, in `firmware/`). Poi mostra il
branch (e se è indietro rispetto a GitHub) e un menu:

- `1` **[NET]** kernel dalla rete (vedi sotto);
- `2` **[SD]** solo il kernel: `kernel.img` sulla SD, quello di prima in `bm/backup`;
- `3` **[SD]** installazione completa: `make install` (kernel, file di avvio, giochi, `bm/`);
- `4` **[SD]** immagine: `make image` (`dist/bm.img`), poi la SD cancellata e formattata FAT32 (tutta,
  fino a 31 GB; Windows chiede i permessi di amministratore) con sopra i file dell'immagine;
  impostazioni, salvataggi e giochi tuoi (`bm/`, `carts/`) vengono copiati sul PC
  (`~/.bm/sd-backup`) e rimessi;
- kernel dalla rete (`1`), senza togliere la SD (`tools/bm_net.py --kernel`): sceglie una console
  salvata o ne aggiunge una (nome, IP e "Console password" di 6 cifre, che Settings > WiFi and
  network mostra; la scheda: Pi Zero / Zero W / Pi 1 o Pi Zero 2 W); se l'IP o il codice sono
  cambiati li chiede e aggiorna il profilo; alla fine `kernel (nome): vecchio -> nuovo`. I
  profili stanno in `.easy_install.conf` (leggibile solo da te); anche `./easy_install.sh net nome`;
- `b` cambia branch o aggiornalo (`git pull`); `p` cambia cartella o lettera della SD.

Monta la SD (`drvfs`, con `sudo`), fa `sync`, la smonta e la espelle; come ultima riga scrive
la versione del kernel che c'era e quella nuova (`kernel: 9603de5 -> d6eebee`). Anche
direttamente: `./easy_install.sh kernel`, `install`, `image`.

## Build e test

```sh
make                  # build/kernel.img + build/chainloader.img (ZERO2=1: anche build/kernel7.img)
make test             # test end-to-end in QEMU: boot, console, schermo, eccezioni, chainloader
make test-zero2       # gli stessi test con kernel7.img in QEMU (-M raspi2b)
make qemu             # esegue in QEMU (-M raspi0), seriale sul terminale
make qemu7            # kernel7.img in QEMU (-M raspi2b)
make qemu-screenshot  # esecuzione headless, salva build/screen.png
make studio           # bm Studio e bm Animator su http://localhost:8765 (sdk/README.md)
```

`make test` comprende anche `make test-studio` (bm Studio e bm Animator in Node: i file
che scrivono, letti anche dal Python della build e dal parser del kernel; saltato senza
Node). `make test-studio-ui` prova le due applicazioni in un browser vero (Playwright +
Chromium).

La CI GitHub Actions (`.github/workflows/ci.yml`) esegue build e `make test` a ogni push.
Se modifichi di proposito il test pattern: `python3 tests/qemu_test.py --update-ref`.
I test leggono il testo mostrato sullo schermo confrontando ogni cella 8×16
con i glifi del font, quindi verificano anche ciò che appare sull'HDMI.

Test della rete, sul PC:

```sh
make test-net        # console di rete e stream su lwIP (interfaccia di loopback, sul PC)
make test-http       # client HTTP contro un server Python locale
make test-https      # HTTPS (mbedTLS) contro server TLS locali con una CA di prova
```

Assistente (M30), sul PC:

```sh
make test-ai         # rete C contro Python, domande di prova, esempi di codice, pannello
make ai-model        # riaddestra la rete dopo aver cambiato src/ai/kb/ (serve numpy)
```

## nano8: cartucce `.p8` e `.p8.png` (M23)

**nano8**, nella scheda Games, gioca le cartucce nei formati di PICO-8: `.p8` (testo) e
`.p8.png` (l'immagine della cartuccia). Non è PICO-8: nome e font sono nostri; la macchina
(memoria, grafica, testo, suono) è scritta da zero in C nel kernel (`src/bm/n8*.c`,
`src/audio/n8snd.c`), il resto è una cartuccia `.bm` in Lua (`carts/nano8`): traduce il
dialetto Lua delle cartucce in Lua 5.4, mostra la lista e i menu.

- **Le cartucce** vanno nella cartella `carts/nano8/` della SD (si leggono anche `nano8/`,
  `carts/` e la radice). `make sdcard` / `make image` / `make install` ci mettono quelle
  incluse: sette giochi di altri autori con licenza libera e la demo **Comet Catcher**
  (`carts/nano8/roms`, crediti e licenze in `CREDITS.md`). Per le altre basta copiare i file
  dal PC: i `.p8.png` scaricati dal forum funzionano così come sono.
- **Lista**: le etichette delle cartucce in una griglia; frecce o croce per scegliere, A /
  Invio / spazio per giocare, X / Tab per i **controlli**.
- **Durante il gioco**: Start (o Invio / P) apre la **pausa**: continua, le voci della
  cartuccia (`menuitem`), ricomincia, controlli, schermo (nitido 2× o a tutta altezza),
  volume, torna alla lista. Esc (o Start+Select, o PS) torna al menu di bm lasciando il
  gioco sospeso, come gli altri giochi.
- **Controlli** (mappabili): per ognuno dei 7 tasti delle cartucce (⬅️ ➡️ ⬆️ ⬇️ 🅾️ ❎ e
  pausa) i tasti della tastiera del giocatore 1, del giocatore 2 e i pulsanti del
  controller. A cambia il tasto (premi quello nuovo), X / Tab ne aggiunge un altro, "Reset to
  defaults" torna a quelli iniziali: giocatore 1 frecce, Z C N (🅾️), X V M (❎), Invio P
  (pausa); giocatore 2 S F E D, Shift Tab, A Q; controller croce, A / X (🅾️), B / Y (❎),
  Start. I controller Bluetooth sono i giocatori 1–4 delle cartucce.
- **Salvataggi**: i dati che le cartucce tengono (`cartdata`, record e progressi) e la
  mappatura stanno in `bm/save` sulla SD.
- **Mouse e tastiera delle cartucce**: per le cartucce che usano il mouse il cursore si muove
  con la levetta, la croce o le frecce, e 🅾️ / ❎ sono i tasti del mouse; quelle che leggono la
  tastiera come testo la ricevono così com'è.
- **Limiti**: i `.p8` con `#include` vanno esportati prima (in `.p8.png` o in un `.p8` unico);
  le cartucce più pesanti possono andare sotto i 60 fps (rallentano, non si fermano). Dettagli in
  [ROADMAP M23](docs/ROADMAP.md#m23--emulatore-di-cartucce-p8--p8png-stile-pico-8-lxl).

Sul PC, `make test-nano8` prova caricatore, traduttore, una cartuccia di 151 controlli
(`tests/nano8/carts/api.p8`) e le cartucce incluse; `build/host/n8host build/nano8/main.lua
--root CARTELLA --exec "NANO8.Ui.play(1)" --shot 120:out.ppm` gioca nano8 sul PC (screenshot,
`--wav`, `--perf`).

## Suoni e musica

L'audio esce dall'**HDMI** (gli altoparlanti del monitor) a 48 kHz: un sintetizzatore a
8 voci (quadra, triangolo, dente di sega, seno, due rumori, ADSR) e un **sequencer** che
suona il banco di suoni della cartuccia, la sezione **AUDIO** del `.bm`: strumenti,
effetti sonori, pattern a 8 tracce e brani, con effetti sui passi (glide, bend,
vibrato, tremolo, accordi, arpeggi, fade, retrigger). Gira nell'interrupt audio, a tempo
anche se il gioco rallenta. Dai giochi: `sfx(n)`, `music(n)`, `volume()` e le note
singole (`note`, `slide`, `vibrato`, `arp`), vedi [docs/API-IT.md](docs/API-IT.md#suono).

Il banco si fa con il **Sound editor** (scheda Dev, o `A` nel monitor), col solo pad o
con la tastiera:

- **SOUNDS**: gli strumenti, con i grafici di forma d'onda, inviluppo e altezza;
- **SFX**: gli effetti per i giochi (fino a 32 passi);
- **PATTERN**: un sequencer a 8 tracce, una per voce, a pad come una drum machine;
- **SONG**: l'ordine dei pattern, tempo, swing e loop.

Apre un gioco della SD e ne modifica i suoni direttamente ("Save" li riscrive nel
gioco), salva pacchetti di suoni in `bm/sounds/`, importa da un'altra cartuccia un
suono, un effetto, un pattern o un brano (con i suoni che usa) ed esporta in un gioco.
Parte da un progetto dimostrativo: START lo suona.

Sul PC: `scripts/bmaudio.py` converte un banco in JSON e ritorno, `mkbm.py --audio`
lo mette in una cartuccia, `make wav BANK=carts/sound/demo.json SONG=0` lo suona in
`build/song0.wav` con lo stesso sintetizzatore della console.

## Cartucce native `.bm`

Cartucce solo per bm che sfruttano il Pi Zero: **640×360, colore diretto a 16 bit
(RGB565), 60 fps**, logica in Lua 5.4, tutto il disegno in C. Formato in
`src/bm/bm.h` (header + sezioni: copertina, codice Lua, sprite sheet RGBA, mappa, suoni,
modelli 3D e scheletri di bm Studio e bm Animator); la grafica è salvata in un formato
indipendente dallo schermo, pronta per un futuro 32 bit. I modelli 3D
(`--models modelli.glb`, o un `.bm` fatto con [bm Studio](sdk/README.md)) si caricano con
`model("nome")`; con uno scheletro di bm Animator, `animate()` li muove.

```sh
python3 scripts/mkbm.py -o gioco.bm --lua main.lua --sheet sheet.png --map map.csv \
        --title "Il mio gioco" --cover copertina.png
```

La cartuccia definisce `_init()`, `_update()` e `_draw()` (60 volte al secondo) e usa
un'API in stile PICO-8: forme, sprite e mappa, testo, input (`btn`/`btnp`), tempo,
3D software. **Riferimento completo e guida alla prima cartuccia: [docs/API-IT.md](docs/API-IT.md).**
Giochi di esempio: `carts/pong`, `carts/snake`, `carts/shooter`, `carts/astrowing` (3D), `carts/hunt` (mappa 2048×2048, luci; grafica e mappa da `mkassets.py`), `carts/texroom` (stanza 3D con texture a 320×180, ms e fps sullo schermo; texture da `mkassets.py`), `carts/village` (Studio Village: modelli 3D fatti con bm Studio e un paesano animato con bm Animator, in `models.bm`) (solo Lua, sprite
disegnati nel codice con `sset`), `carts/demo` (sprite sheet PNG e mappa CSV),
`carts/kitchen` (Chaos Kitchen: gioco grande in più file Lua uniti da `build.py`, 3D,
fino a 4 giocatori, simulatore host in `tests/kitchen/`), `carts/titan` (Titan Clash:
picchiaduro con sprite grandi pre-renderizzati da un modello 3D, sheet 2048×3376 con
palette, simulatore host in `tests/titan/`), `carts/yharnam` (Yharnam: città gotica infinita a
256×256 fatta mentre si cammina, luce a livelli come in Dank Tomb, 12 nemici e 4 boss;
grafica da `mkassets.py`, simulatore host in `tests/yharnam/`).

Sandbox: niente `io`, `os`, `load`, `dofile`, `require`. Un errore o un ciclo infinito
(oltre 20 milioni di istruzioni in un frame) ferma la cartuccia e mostra l'errore
sulla console, senza bloccare il kernel. Il disegno va direttamente nella pagina
nascosta del framebuffer (in alternativa, comando `V`, in un buffer in RAM copiato
una volta per frame: `p` confronta i due modi).

![demo bm](docs/m7-bm-demo.png)

## Lua

Lua 5.4.7 completo (numeri double, interi a 64 bit, coroutine, string, table,
math, utf8, os, io su stdout/stdin). `print` scrive su seriale e schermo.
Gli errori non bloccano il kernel: vengono stampati in rosso con il traceback.
Lua ha un limite di 64 MiB di memoria; oltre, `not enough memory` (recuperabile).

Prestazioni misurate su Pi Zero W (1 GHz, MMU e cache attive): `fib(25)` 83 ms,
1 milione di addizioni in un ciclo 104 ms, `table.sort` di 100k interi 657 ms,
20k `tostring` + `table.concat` 104 ms. In un frame a 60 fps (16,7 ms, di cui
~2,7 ms per disegnare) restano circa 150k operazioni Lua semplici.

Modulo `bm`:

| Funzione | Descrizione |
|----------|-------------|
| `bm.micros()` | contatore a 1 MHz (intero) |
| `bm.millis()` | millisecondi dal tick di sistema |
| `bm.sleep(ms)` | attesa |
| `bm.mem()` | byte usati da Lua, picco, byte in uso nell'heap C |
| `bm.color(fg [, bg])` | colori della console 0–15 (ordine ANSI) |
| `bm.cls()` | pulisce lo schermo |
| `bm.reboot()` | riavvio (watchdog) |
| `bm.version` | versione del kernel |

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
secondo; il PC risponde `BM` + dimensione + CRC-32; il loader verifica, copia il
kernel a `0x8000` e ci salta. Il chainloader si ricopia prima a `0x02000000`, quindi
il kernel può essere grande fino a ~31 MiB. A 115200 baud la velocità è ~11 KB/s:
con Lua il kernel è ~340 KB, cioè ~30 s per caricarlo. Conviene `BAUD=921600` (deve essere uguale per build e `run-serial`,
e va rifatto anche il chainloader sulla SD).

## Aggiornare solo il kernel sulla SD (senza seriale)

Dalla cartella del progetto, con la SD montata (in WSL: `sudo mount -t drvfs D: /mnt/d`):

```sh
make install            # = make sdcard, poi copia tutto sulla SD in /mnt/d
make install SD=/mnt/e  # se la SD è montata altrove
```

`make install` copia il kernel (`kernel.img`; con `ZERO2=1` anche `kernel7.img` del Pi Zero 2 W), file di
avvio, `config.txt`, cartucce e il firmware dei chip in `bm/` (Bluetooth e WiFi del Zero W
e del Zero 2 W); non tocca mai impostazioni e salvataggi
(`bm/CONFIG.TXT`, `bm/SAVE`). Alla fine elenca cosa c'è in `bm/` sulla SD.

## Release (M19)

A ogni tag `v*` il CI (`.github/workflows/ci.yml`), dopo i test, costruisce i kernel
(`kernel.img` e `kernel7.img`, con il tag come versione: `bm v0.1.0` nella barra) e i giochi e
li pubblica in una release di GitHub
con `bm/ca.pem` e `manifest.txt`: per ogni file il nome nella release, dove va sulla SD, la
dimensione e lo SHA-256. `manifest.sig` è la firma del manifesto (ECDSA P-256), fatta con la
chiave privata nel secret `BM_RELEASE_KEY` del repository; la chiave pubblica
(`keys/release-pub.pem`) è dentro il kernel, che controlla firma e SHA-256 prima di installare
(`src/net/release.c`; l'aggiornamento dal Pi è il passo 4 di M19).

Tutto in un comando, da WSL (con la SD nel lettore): `scripts/release.sh v0.1.0` installa se
serve la GitHub CLI (`gh`) e fa il login, crea la chiave (o tiene quella che c'è), mette il
secret, fa commit e push della chiave pubblica, mette sulla SD un kernel con la chiave (prima
del tag: un kernel compilato sul tag sarebbe la release stessa), crea e manda il tag e aspetta
la CI fino alla release. Chiede prima di ogni passo che cambia qualcosa; `--sd E` per un'altra
lettera, `--no-sd` per le release dopo la prima (il Pi si aggiorna da solo), `--dry-run` per
vedere cosa farebbe.

La prima volta a mano, dal PC (la chiave privata resta lì, in `~/.bm/release-key.pem`: tienine una copia):

```sh
scripts/release-key.sh                          # la coppia di chiavi
gh secret set BM_RELEASE_KEY < ~/.bm/release-key.pem   # o dal sito: Settings > Secrets and variables > Actions
git add keys/release-pub.pem && git commit -m "Release key" && git push
```

La stessa release ha anche il manifesto della RGB30, `manifest-rgb30.txt` (con `.sig`):
`kernel8.img` e `bm/ca.pem`, per *System > Updates* della RGB30 (`make release
RGB30_KERNEL=build/rgb30/kernel8.img`; la CI lo fa sui tag).

Poi una release: `git tag v0.1.0 && git push origin v0.1.0`. In locale, per provare:
`BM_RELEASE_KEY="$(cat ~/.bm/release-key.pem)" make release VERSION=v0.1.0` (file in `dist/release/`).


**Aggiornare dal Pi** (M19.4): Settings > System > *Check for updates* legge l'ultima
release (`manifest.txt` e la sua firma), dice se è più nuova del kernel che gira (un kernel
compilato dai sorgenti, `make install`, conta come "build of the sources": la release lo può
sostituire) e quali file cambierebbero; *Install the update* chiede conferma, scarica e
controlla tutto (dimensione, SHA-256, il marchio `bmK6`/`bmK7` dei kernel) prima di
scrivere qualcosa, tiene i kernel di prima in `/bm/backup` (per tornare indietro dal PC),
aggiorna i giochi che sono sulla SD (quelli tolti restano tolti: li ha il Market),
`bm/ca.pem`, poi `kernel7.img` e `kernel.img` (quello della scheda per ultimo) e riavvia.
Dal monitor: `u`. Impostazioni: `update_url=` in `bm/config.txt` (predefinito
`https://github.com/f-accomando/bm/releases`; `sd:/cartella/` per una release copiata sulla
SD), `bm/release.pem` aggiunge una chiave. Test: `test_update` in QEMU, `make test-release`.
## Market (M25)

La prima scheda del menu (**Market | Games | Dev | Settings**; il menu si apre su Games):
giochi gratuiti da scaricare, dal repository pubblico
[f-accomando/bm-market](https://github.com/f-accomando/bm-market) pubblicato con GitHub Pages.

- **Sul Pi**: scheda Market (L1 da Games, o `1` dalla tastiera). Compaiono subito dei
  segnaposto, poi il catalogo e le copertine, una alla volta: il menu resta a 60 fps
  perché il lavoro gira in una "fibra" (`src/kernel/fiber.c`) nel tempo che avanza in ogni
  frame, e solo mentre la scheda è aperta (un'altra scheda, le impostazioni o un gioco lo
  interrompono). A su un gioco: domanda (dimensione, licenza), download con la percentuale
  sulla copertina, controllo, installazione in `/carts`; poi A lo avvia. X: autore,
  versione, licenza, file, "Download again", cancellazione. Un gioco già sulla SD con lo
  stesso titolo e autore risulta installato.
- **Sicurezza**: il catalogo (`index.txt`) è firmato con la chiave del market (ECDSA P-256,
  `keys/market-pub.pem` nel kernel, non quella delle release); ogni file deve avere la
  dimensione e lo SHA-256 del catalogo prima di toccare la SD; il market scrive solo in
  `/carts` e in `/bm/market` (cache del catalogo e delle copertine, `GAMES.TXT` con i giochi
  installati). Le cartucce della SD scrivono solo file `.bm` in `/carts` e i loro
  salvataggi (gli strumenti incorporati, SDK, Code, Sound, bm Studio, bm Animator, bm Mesh e bm Pixel,
  dove vogliono).
- **Impostazioni** in `bm/config.txt`: `market_url=` (predefinito
  `https://f-accomando.github.io/bm-market/`; anche una cartella della SD con gli stessi
  file, `market_url=sd:/market/`, per un catalogo senza rete); `bm/market.pem` sulla SD
  aggiunge una chiave (un market proprio); `market_delay=` (ms) rallenta la sorgente, per i
  test.
- **Pubblicare un gioco**: una pull request a bm-market con `games/<id>/` (il `.bm` e
  `info.txt` con versione, licenza obbligatoria e descrizione); vedi il README del market
  (il modello è in `market/`). Il CI del market controlla le pull request
  (`scripts/mkmarket.py --check`) e, dopo il merge, costruisce, firma e pubblica il catalogo.
- **Pubblicare dal Pi**: X su un gioco della SD, *Publish to the Market*: cartella (quella
  del catalogo se il gioco c'è già, se no dal nome del file), versione (la data), licenza
  (sinistra/destra), poi *Send the pull request*. Serve un token personale di GitHub in
  `bm/config.txt` (`github_token=...`, un token "fine-grained" che può scrivere i
  repository pubblici del proprio account). Il proprietario del market apre un ramo nel
  market; chiunque altro un fork. L'esito (e l'indirizzo della pull request) compare sulla
  console di testo (`src/net/github.c`, `src/kernel/publish.c`; test: `make test-github`
  contro un finto server delle API, `test_publish` in QEMU).
- **Tra console sulla rete di casa** (M24): con la scheda Market aperta una console si
  annuncia (UDP, porta 3335) e ascolta (TCP 3336). Da un'altra: X su un gioco della SD,
  *Send to a nearby console*, si sceglie la console; chi riceve vede una domanda ("bm-108
  sends Snake", con "the Market's own: checked" se quei byte sono un gioco del catalogo,
  altrimenti "from friends only") e il gioco va in `/carts` solo se ha lo SHA-256
  annunciato. Il nome della console è `name=` in `bm/config.txt` (se no `bm-` e l'ultimo
  numero dell'IP). Niente P2P via internet. Test: `make test-lan` (sul PC, lwIP in loopback).
- **I giochi del progetto** sono tutti nel market (e per ora anche nell'immagine della SD):
  `make market-seed MARKET=../bm-market` li costruisce e aggiorna le loro cartelle.

La prima volta, dal PC: `./easy_install.sh market` (o `m` nel suo menu; è
`scripts/market.sh`) fa tutto, chiedendo prima di ogni passo che cambia GitHub: la chiave,
il secret, la chiave pubblica su `bm-core`, GitHub Pages, poi i giochi del progetto nel
clone `../bm-market`, commit, push, il workflow aspettato e il catalogo riletto da
<https://f-accomando.github.io/bm-market/>. Le volte dopo aggiorna solo i giochi. A mano:

```sh
scripts/market-key.sh                                   # la coppia di chiavi del market
gh secret set BM_MARKET_KEY -R f-accomando/bm-market < ~/.bm/market-key.pem
git add keys/market-pub.pem && git commit -m "Market key" && git push
```

poi, nel repository del market: Settings > Pages > Source: **GitHub Actions**. Finché la
chiave non c'è, la scheda Market dice "The Market needs a key". Test: `make test-catalog`
(catalogo, firma e copertine sul PC) e `test_market` in QEMU (il market da una cartella
della SD, rallentato).

## Scheda SD senza chainloader

Il modo più semplice è l'immagine completa: `make firmware && make image`, poi scrivi
`dist/bm.img` con Raspberry Pi Imager ("Use custom"), balenaEtcher o `dd`
(serve `sudo apt install dosfstools mtools`). In alternativa, a mano:

1. Formatta la SD con una partizione **FAT32** (tabella MBR).
2. `make firmware && make sdcard` (per fissare una versione del firmware: `FW_REF=<tag> make firmware`).
3. Copia il contenuto di `dist/` nella root della SD (`cp -r dist/* /mnt/d/`):
   `bootcode.bin  start.elf  fixup.dat  config.txt  kernel.img  kernel7.img  carts/  bm/`
4. Collega l'HDMI (mini-HDMI) *prima* di alimentare il Pi.

## Raspberry Pi 1 (B e B+, M29)

Stesso kernel del Zero W: all'avvio riconosce la scheda (il banner dice per esempio
`Raspberry Pi 1 B rev 2.0`). L'immagine per il Pi 1 è la stessa senza il firmware del
chip WiFi/Bluetooth, che il Pi 1 non ha:

```sh
make firmware && make image-pi1   # dist/bm-pi1.img: scrivila sulla SD come bm.img
```

- **Rete**: col cavo Ethernet la rete parte da sola (DHCP appena il cavo ha il link);
  l'IP compare sullo schermo e nella barra di stato, poi console di rete e invio di file
  e kernel funzionano come col WiFi (`tools/bm_net.py IP`). `E` nel monitor mostra link,
  contatori e registri del chip.
- **USB**: le porte del Pi 1 B stanno dietro l'hub del LAN9512; tastiera o gamepad
  vanno su una porta qualsiasi (uno alla volta in uso, la tastiera ha la precedenza).
- **Non ci sono**: WiFi e Bluetooth (`W` e `T` lo dicono).
- **LED**: sul Pi 1 B il LED ACT ("OK") è il GPIO 16; il kernel lo sceglie da solo.
- **Prestazioni**: ARM a 700 MHz invece di 1 GHz; i giochi più pesanti possono scendere
  sotto i 60 fps.
- Il firmware di avvio (`make firmware`) e gli aggiornamenti di bm (`--kernel`, M19)
  sono gli stessi per Zero W e Pi 1.

## Raspberry Pi Zero 2 W (M31)

Il Zero 2 W ha un altro processore (BCM2710A1: quattro Cortex-A53) e quindi un suo kernel,
**`kernel7.img`**: gli stessi sorgenti compilati per ARMv7 a 32 bit. `make`, `make sdcard`,
`make install` e `make image` mettono sulla SD **tutti e due i kernel**, e `config.txt` fa
partire quello giusto (`[pi02]` → `kernel7.img`): la stessa scheda va nel Zero W e nel
Zero 2 W.

**Per ora la build del Zero 2 W è spenta**: `make`, `make test`, `make sdcard`, `make install`,
`make image`, `make release` e la CI la lasciano fuori. Si riaccende con `ZERO2=1` (`make
ZERO2=1 install`, oppure `ZERO2: "1"` in `.github/workflows/ci.yml`); `make test-zero2` la
compila comunque.

```sh
make firmware && make ZERO2=1 image   # dist/bm.img per Zero, Zero W e Zero 2 W
make install                  # oppure: aggiorna la SD in /mnt/d (tutti e due i kernel)
```

- **Schermo**: la prima riga dice `Raspberry Pi Zero 2 W (BCM2710A1, revision 902120)`, la
  seconda `Cortex-A53 from HYP` (il processore e il modo in cui il firmware l'ha avviato).
  Nel menu, scheda System, la riga CPU dice `Cortex-A53, 1000 MHz, ...`.
- **Un core su quattro**: gli altri tre restano fermi nello stub del firmware.
- **LED**: il LED ACT del Zero 2 W è il GPIO 29.
- **WiFi e Bluetooth**: il chip è un CYW43436, con un firmware diverso da quello del Zero W;
  `make firmware` scarica anche i suoi file (`brcmfmac43436-sdio.*`, `brcmfmac43436s-sdio.*`,
  `SYN43430A1.hcd`, `SYN43430B0.hcd`) e `make sdcard` li mette in `bm/`. Il kernel sceglie
  quelli della versione del chip e lo scrive sullo schermo (`wifi: chip ... CYW43436`,
  `bt: chip LMP subversion ...`).
- **Kernel dalla rete**: `tools/bm_net.py IP --kernel build/kernel7.img` (sul Zero 2 W
  viene scritto come `kernel7.img`); un kernel per l'altra scheda viene rifiutato senza
  scrivere niente (all'offset 4 di ogni immagine c'è `bmK6` o `bmK7`).
- **Chainloader seriale**: solo per le schede BCM2835 (`kernel.img`).
- **Test**: QEMU non ha il Zero 2 W; `make test-zero2` prova `kernel7.img` in `raspi2b`
  (un Pi 2 B: le stesse periferiche, un Cortex-A7, niente radio). Dettagli in
  [`docs/HARDWARE.md`](docs/HARDWARE.md), sezione 8.

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
src/usb/                 host USB DWC2 (DMA, polling, split transactions), enumerazione anche
                         dietro un hub, HID tastiera/gamepad/Xbox 360, Ethernet LAN951x (smsc95xx.c)
src/drivers/sd.c         SD: controller SDHOST (sdhost.c), ripiego sull'EMMC/Arasan (sd_emmc.c); PIO, lettura e scrittura
src/wifi/                WiFi (M18): SDIO sul controller Arasan (GPIO34-39), comando W del monitor
src/fs/fat.c             FAT16/FAT32: lettura con nomi lunghi, scrittura 8.3, cancellazione
src/kernel/carts.c       elenco delle cartucce (incorporate + SD), menu e opzioni delle cartucce
src/kernel/menu_ui.c     BareMetal UI: griglia, schede, pannelli, copertine degli strumenti
src/kernel/home.c        strumenti della scheda Dev e pannelli delle impostazioni
src/kernel/market.c      scheda Market (M25): catalogo, copertine, download, installazione
src/kernel/fiber.c       fibre: lavoro su uno stack proprio che cede il controllo al menu
src/kernel/input.c       input unificato: seriale + tastiera/gamepad USB
src/bm/                 cartucce native: formato, grafica RGB565 (gfx16), 3D software (r3d),
                         runtime Lua, stress test
src/ai/                  assistente (M30): rete INT8 (nn.c), testo (text.c), domande (assist.c),
                         ricette di sprite (sprite.c), tabella Lua ai (lua_ai.c), pannello
                         (assist.lua, require "assist"); base di conoscenza in src/ai/kb/
carts/assistant/         lo strumento Assistant della scheda Dev
carts/code/              bm Code, l'editor del codice (tab, due pagine, font 6x12, #entry:)
scripts/mkassist.py      base di conoscenza + rete -> build/assist.bin (nel kernel)
scripts/trainassist.py   addestramento della rete (make ai-model, numpy)
tests/ai/                test dell'assistente (C, Lua, esempi di codice)
carts/demo/              cartuccia nativa demo: main.lua, sheet.png, map.csv
carts/pong|snake|shooter|astrowing|hunt giochi demo (solo Lua)
carts/texroom/           Texture Room: il benchmark 3D della scheda Dev (nel kernel)
carts/village/           Studio Village: main.lua, models.bm (modelli, scheletro e sheet),
                         mkmodels.js (li costruisce con gli strumenti di Studio e Animator)
sdk/studio/              bm Studio: modelli 3D e pixel art per i .bm, sul PC (sdk/README.md)
sdk/animator/            bm Animator: scheletri, animazioni, sprite pre-renderizzati
carts/studio3d/          lo studio 3D della console (scheda Dev): player, blocchi, ossa, keyframe
carts/mesh/              bm Mesh (scheda Dev): le mesh di un .bm, anche quelle del codice; mesh <-> modello
carts/pixel/             bm Pixel (scheda Dev): la pixel art dello sprite sheet, tavolozza, animazione
tests/studio/            test di bm Studio, bm Animator e dello studio 3D: Node, Playwright, Lua sul PC
carts/kitchen/           Chaos Kitchen (M17): src/*.lua, build.py, mkassets.py,
                         models/*.glb e import_chefs.py (modelli 3D degli chef)
tests/kitchen/           simulatore host di Chaos Kitchen (luahost + sim.lua)
carts/titan/             Titan Clash (M20): src/*.lua, build.py; mkrobot.py (il robot
                         pre-renderizzato), art.py e mkassets.py (sheet.png)
tests/titan/             simulatore host di Titan Clash (sim.lua)
carts/yharnam/           Yharnam: main.lua, mkassets.py e art/ (sdf.py: modelli 3D in pixel art;
                         hunter.py, anims.py, rig.py, foeparts.py, foe_*.py: le creature;
                         props.py, tiles.py, buildings.py), sheet.png, cover.png
tests/yharnam/           simulatore host di Yharnam (sim.lua)
carts/nano8/             nano8 (M23): src/*.lua (traduttore, API, input, ui), build.py,
                         roms/ (le cartucce .p8 incluse, CREDITS.md), mkdemo.py (Comet Catcher)
src/bm/n8*.c             la macchina di nano8: memoria e disegno (n8.c), font (n8font.c),
                         cartucce .p8 / .p8.png (n8cart.c), la libreria Lua n8 (n8lua.c)
src/audio/n8snd.c        il suono di nano8: 4 canali, effetti e musica delle cartucce
tests/nano8/             prove di nano8 sul PC: n8host (nano8 senza Pi), api.p8, run.py
docs/API-IT.md           API delle cartucce .bm e guida alla prima cartuccia (API.md in inglese)
scripts/mkbm.py         packer .bm (PNG e CSV, solo libreria standard Python)
scripts/bmmesh.py        sezione MESH (modelli 3D) e file .glb di bm Studio, per mkbm.py --models
                         (con un .bm: modelli, scheletri e sheet)
scripts/mksd.py          immagine SD (MBR + FAT32): make image e test in QEMU
scripts/mkmarket.py      catalogo del Market: controllo, firma, copertine, --add
scripts/mkrelease.py     file di una release e manifest.txt firmato (make release, CI sui tag v*)
scripts/release-key.sh   coppia di chiavi delle release; la pubblica in keys/release-pub.pem
scripts/release.sh       una release da WSL: gh, chiave, secret, kernel sulla SD, tag, attesa della CI
src/net/release.c        verifica delle release: firma del manifesto, righe, SHA-256 dei file
src/net/catalog.c        catalogo del Market (M25): firma con la chiave del market, record, SHA-256
tests/bm/               test host della grafica e del formato
src/script/luavm.c       stato Lua, allocatore con limite (64 MiB), esecuzione protetta
src/script/repl.c        REPL: espressioni, righe di continuazione, traceback
src/script/lib_bm.c    modulo Lua `bm`
src/script/boot.lua      script di avvio (incluso nell'immagine con .incbin)
third_party/lua/         Lua 5.4.7 non modificato (licenza MIT)
third_party/lwip/        lwIP 2.2.0, sottoinsieme non modificato (licenza BSD)
third_party/mbedtls/     mbedTLS 3.6.2, sottoinsieme non modificato (licenza Apache 2.0)
src/kernel/selftest.c    self-test di newlib
src/arch/mmu.c           tabella delle sezioni da 1 MiB, attivazione MMU e cache
src/arch/cache.c         clean/invalidate della D-cache per range (mailbox)
src/gfx/console.c        console testuale: celle, scroll, cursore, ANSI, barra di stato
src/gfx/font8x16.c       font 8×16 CP437 (derivato da Terminus, OFL: docs/LICENSE.font);
                         font6x12.c e font8x14.c per font() delle cartucce (bm Code)
src/drivers/             mmio, mailbox, prop tags, framebuffer, gpio, uart (PL011),
                         timer, LED, watchdog, scheda (board.c: Zero, Zero W, Pi 1)
src/lib/                 kprintf, crc32, syscalls newlib (_sbrk, _write, ...)
chainloader/             bootloader seriale (si riloca a 0x02000000)
tools/bm_load.py       invio del kernel + terminale seriale (solo stdlib Python)
tools/bm_net.py        via rete (WiFi o Ethernet): console (monitor), invio di cartucce e kernel (solo stdlib Python)
tests/qemu_test.py       test end-to-end in QEMU (anche tastiera USB, hub, gamepad HID, SD, Pi 1 A+)
tests/usb/, tests/net/   test sul PC: HID, scheda, Ethernet su un LAN9512 simulato, lwIP con DHCP
tests/mksd.py            crea un'immagine SD (MBR + FAT32) per i test in QEMU
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

## Licenza

BM is a project by F. Accomando.

bm è distribuito con la **BM Community License 1.0** (file [`LICENSE`](LICENSE)):
uso, modifica e redistribuzione libera per le persone fisiche (anche commerciale,
se in proprio), con obbligo di attribuzione e di pubblicare il sorgente delle
versioni modificate sotto la stessa licenza. Le organizzazioni hanno bisogno di
una licenza commerciale separata.

Componenti di terze parti, con la loro licenza (sezione 7 della licenza):

- `third_party/lua/` — Lua 5.4.7, licenza MIT (`third_party/lua/LICENSE`);
- `third_party/lwip/` — lwIP 2.2.0, licenza BSD a 3 clausole (`third_party/lwip/COPYING`);
- `third_party/mbedtls/` — mbedTLS 3.6.2, licenza Apache 2.0 (`third_party/mbedtls/LICENSE`);
- `boot/ca.pem` — certificati radice dalla lista Mozilla (`scripts/make-ca.sh`);
- `src/gfx/font8x16.c` — font derivato da Terminus, SIL OFL (`docs/LICENSE.font`);
- `carts/nano8/roms/` — cartucce di altri autori (CC0 e MIT), con la loro licenza
  (`carts/nano8/roms/CREDITS.md` e `licenses/`);
- firmware del Raspberry Pi (scaricato da `scripts/`, non incluso nel repository),
  con la licenza di Raspberry Pi Ltd.
