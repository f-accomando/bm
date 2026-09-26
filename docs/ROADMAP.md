# Roadmap bm33

Obiettivo MVP: una *fantasy console* bare metal su Raspberry Pi Zero W che avvia
da SD, mostra un menu, carica giochi scritti in Lua ("cart") e li esegue a 60 fps
con grafica, input e (opzionale) audio.

Ogni milestone ha un **criterio di completamento** verificabile e, quando
possibile, un test automatico in QEMU (`-M raspi0`).
Dimensione: **S** = pochi giorni, **M** = 1–2 settimane, **L** = più di 2 settimane.

```
M0 ─ M1 ─ M2 ─ M3 ─ M4 ─ M5 ─ M6 (s32) ─┬─ M7 ─┬─ M9 (MVP)
                                  └─ M8 ─┘
                                     M10 audio (opzionale per l'MVP)
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

## M6 — Core s32 (compatibilità con lua32) ✅ (M)
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

## M7 — Cartucce native `.b33` ✅ (M)
- Formato `.b33` (header 128 byte + sezioni Lua / sheet RGBA / mappa, CRC), packer
  `scripts/mkb33.py` con PNG e CSV.
- Grafica C in RGB565 (`src/b33/gfx16.c`): forme, sprite con flip e trasparenza,
  scorciatoia per celle opache, mappa, testo, camera, clip.
- Runtime: stato Lua isolato per cartuccia, `_init/_update/_draw` a 60 fps, input
  seriale, limite di istruzioni per frame, errori mostrati sulla console, GC
  generazionale; caricamento dalla seriale (`U`).
- Benchmark C e demo nativa all'avvio.
- **Fatto quando:** mappa piena + 256 sprite sotto il 25% del frame sul Pi reale.

## M7b — Input (L, rischio alto)
- **Fase A (S):** pulsanti su GPIO oppure pad SNES/NES via GPIO (latch/clock/data):
  semplice, deterministico, pronto per giocare subito.
- **Fase B (L):** controller USB DWC OTG → HID tastiera/gamepad.
  È il pezzo più complesso: valutare il porting di **USPi** (C, compatibile con il Pi 1,
  licenza GPLv3) rispetto a uno stack scritto da zero.
- Mappatura sull'`input_byte` di s32 (bit 0–4: su, giù, sinistra, destra, azione;
  fino a 8 giocatori), più tastiera per il REPL.
- **Fatto quando:** una cart di esempio si gioca con il pad; (fase B) una tastiera USB
  scrive nel REPL.

## M8 — Storage e caricamento delle cart (M)
- Driver EMMC/SDHCI per la SD (lettura, poi scrittura), **FatFs** sopra.
- Lettura di `/carts/*.cart` (s32, codice macchina o Lua), picker delle cartucce come
  `s32_os` di lua32, salvataggio di config.
- **Fatto quando:** copiando una nuova cart sulla SD da PC, questa compare nel menu.

## M9 — MVP (M)
- Launcher: menu con elenco delle cart, anteprima, ritorno al menu (combinazione di tasti).
- 2–3 giochi demo (es. pong, snake, shooter) che coprono tutta l'API.
- `make image` → `bm33.img` pronto da scrivere con Raspberry Pi Imager / `dd`.
- Documentazione dell'API Lua e guida "scrivi la tua prima cart".
- **Fatto quando:** da una SD appena scritta si accende, si sceglie un gioco e si gioca
  senza PC collegato.

## M10 — Audio (M, opzionale per l'MVP)
- Il Pi Zero non ha jack audio: PWM su GPIO18/13 con filtro RC esterno
  (semplice) oppure audio via HDMI (complesso, poco documentato).
- APU di s32: 8 canali (quadra/triangolo/dente di sega/rumore) con ADSR, registri
  memory-mapped; DMA + PWM, IRQ di refill.
- **Fatto quando:** i giochi demo hanno effetti sonori senza cali di frame rate.

---

## Rischi principali
| Rischio | Mitigazione |
|---------|-------------|
| Stack USB (M7B) molto complesso | Fase A con pad via GPIO per sbloccare l'MVP; USPi come base |
| Prestazioni Lua su ARM1176 a 1 GHz | Cache attive (M3), bassa risoluzione, API di blit in C |
| Firmware closed-source che cambia comportamento | Fissare la versione con `FW_REF` |
| Test solo su hardware | QEMU raspi0 in CI + chainloader via seriale |

## Hardware consigliato per lo sviluppo
- Adattatore USB-seriale 3.3 V (**non 5 V**) su GPIO14/15 + GND
- Cavo mini-HDMI, alimentatore 5 V 2 A stabile
- Pulsanti o pad SNES + qualche resistenza per M7A
- Filtro RC (270 Ω + 33 nF) e jack per M10
