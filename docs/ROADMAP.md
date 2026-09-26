# Roadmap bm33

Obiettivo MVP: una *fantasy console* bare metal su Raspberry Pi Zero W che avvia
da SD, mostra un menu, carica giochi scritti in Lua ("cart") e li esegue a 60 fps
con grafica, input e (opzionale) audio.

Ogni milestone ha un **criterio di completamento** verificabile e, quando
possibile, un test automatico in QEMU (`-M raspi0`).
Dimensione: **S** = pochi giorni, **M** = 1–2 settimane, **L** = più di 2 settimane.

```
M0 ─ M1 ─ M2 ─ M3 ─ M4 ─ M5 ─ M6 ─┬─ M7 ─┬─ M9 (MVP)
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

## M2 — Console testuale su schermo (S)
- Font bitmap 8×8 (o 8×16) in `rodata`, rendering carattere, cursore, a capo, scroll.
- `kprintf` scrive sia su UART sia su schermo; il panic appare anche su HDMI.
- **Fatto quando:** i log di boot sono leggibili sul monitor senza cavo seriale.

## M3 — Memoria, cache e libc (M)
- MMU con mappa identità a sezioni da 1 MB: RAM cacheable, periferiche
  device/strongly-ordered, framebuffer write-through (o cache + flush esplicito).
- Attivazione di I-cache, D-cache e branch prediction (senza cache l'ARM1176 è
  10–20× più lento).
- Integrazione di **newlib**: `_sbrk` (heap), `_write` → console, stub per gli altri syscall.
- Benchmark: fill a schermo intero e `memcpy`, prima e dopo le cache.
- **Fatto quando:** `malloc`/`printf` di newlib funzionano; il clear 320×240 richiede meno di 1 ms.

## M4 — Interrupt e temporizzazione (M)
- Controller IRQ BCM2835, IRQ del system timer, contatore di tick a 1 kHz.
- Doppio buffer: framebuffer virtuale alto 2×, scambio tramite il tag *set virtual offset*.
- Frame loop a 60 Hz stabile (sync al vblank se il firmware lo espone, altrimenti timer).
- **Fatto quando:** un rettangolo in movimento scorre fluido, senza tearing visibile, a 60 fps.

## M5 — Lua embedded (M)
- Lua 5.4 in `third_party/lua`, compilato con newlib e VFP hard-float.
- Allocatore dedicato per `lua_State`, `print` → console, errori con traceback su schermo.
- REPL su UART; script di avvio incluso nell'immagine (`.incbin`).
- **Fatto quando:** dalla seriale `> print(2^10)` risponde `1024.0`; un errore Lua
  non blocca il kernel.

## M6 — API grafica e ciclo di gioco (M)
- Risoluzione logica bassa (es. 320×240, 8 bpp con palette oppure 16 bpp):
  il firmware la scala in hardware sull'uscita HDMI, quindi il disegno è veloce.
- API Lua: `cls pset pget line rect rectfill circ spr print pal camera clip`.
- Callback della cart: `_init()`, `_update()`, `_draw()` a 60 fps.
- Sprite sheet e font integrati; formato cart = sorgente `.lua` + asset.
- **Fatto quando:** una demo con 100 sprite in movimento gira a 60 fps.

## M7 — Input (L, rischio alto)
- **Fase A (S):** pulsanti su GPIO oppure pad SNES/NES via GPIO (latch/clock/data):
  semplice, deterministico, pronto per giocare subito.
- **Fase B (L):** controller USB DWC OTG → HID tastiera/gamepad.
  È il pezzo più complesso: valutare il porting di **USPi** (C, compatibile con il Pi 1,
  licenza GPLv3) rispetto a uno stack scritto da zero.
- API Lua: `btn(i)`, `btnp(i)`, più tastiera per il REPL/editor.
- **Fatto quando:** una cart di esempio si gioca con il pad; (fase B) una tastiera USB
  scrive nel REPL.

## M8 — Storage e caricamento delle cart (M)
- Driver EMMC/SDHCI per la SD (lettura, poi scrittura), **FatFs** sopra.
- Lettura di `/carts/*.lua` (+ asset), salvataggio di high score e config.
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
- DMA + PWM, mixer a 4 canali (quadra/triangolo/rumore/sample), IRQ di refill.
- API Lua: `sfx(n)`, `music(n)`.
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
