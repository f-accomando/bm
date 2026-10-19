# Proposte: ottimizzazioni grafica e CPU per la console bm

Stato: proposta, nessuna implementazione. Le decisioni D1-D7 sono aperte.

Base: `origin/bm-core` @ `30b5489`.
Hardware: Raspberry Pi Zero W 1.1, ARM1176JZF-S (ARMv6, single core, VFPv2, niente NEON), VideoCore IV.
Caso guida: platform 3D in stile PS1 (clone di Crash Bandicoot), bassa risoluzione, texture piccole, 16 bit.

Il linguaggio non è in discussione: il C produce codice equivalente a Rust o Zig. I guadagni vengono dall'uso dell'hardware.

## Stato attuale (verificato sul codice)

- **MMU e cache** non sono nel bootloader (`src/boot/start.S`, che imposta solo vettori, VFP e FPSCR). Le attiva il kernel C: `memory_init()` in `src/kernel/main.c` chiama `mmu_init()` in `src/arch/mmu.c`, prima di `kernel_main`.
- **Rasterizzazione**: il backend di default è il V3D (`src/gpu/gpu3d.c`, control list di binning e rendering). Il rasterizzatore ARM `src/bm/r3d.c` serve solo con `gpu3d=0` o come fallback. Il vertex shader gira già sulla QPU (`gpu3d_vs=2`, `runtime.c:1868`). L'header `src/gpu/gpu3d.h` dice il contrario: è obsoleto.
- **Memoria dei job V3D**: `JOB_SECTIONS` e `JOB2_SECTIONS` (`gpu3d.c:32,37`), arrotondati a 1 MiB, circa 18.8 MiB con una coda, circa 38 MiB con `gpu3d_queue=2`.
- **V3D in polling**, senza IRQ (`docs/progress/v3d.md`).
- **Strumento di misura esistente**: 3D Bench (`src/bm/b3d.c`, `src/kernel/b3dpi.c`, `docs/BENCH3D.md`). Il report finisce in `bm/bench/3D000N.TXT` e riporta ms medi e peggiori, ms GPU, istruzioni ARM per frame, `clean_kcyc` e `inval_kcyc`. L'unica baseline Pi nel repo è `docs/bench/20261005-082943_bench3d_pi-zero-w_v0.2.0.txt`, vecchia.
- **Nessuna misura reale** in questo sandbox: non ci sono toolchain ARM, `qemu-system-arm` né un Pi. Le cifre di `docs/PERFORMANCE.md` e `docs/progress/v3d.md` sono dichiarate dagli autori su Pi, non riprodotte.

### Stato per punto

| # | Punto | Stato |
|---|---|---|
| 0 | Cambio di linguaggio | Non valutato, fuori scope |
| 1 | MMU, cache, branch prediction | Fatto. Il commento su TEX/S in `mmu.c` non è verificato contro il TRM |
| 2 | Flag di build | Parziale: `-O2` in `Makefile:53`, `-mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard` in `Makefile:38`. Niente `-O3`, LTO, `restrict` |
| 3 | Fixed-point | Parziale: span e UV già in 16.16 (`gfx16.c:332,459`), edge 32.32 (`r3d.c:18`), attributi ancora in float |
| 4 | SIMD in registro (UADD8, SMLAD, ...) | Non fatto nel rendering. Usato solo in `ai/nn.c` |
| 5 | Divisioni nei loop | Fatto in gran parte. Nessuna divisione per pixel (`TEX_RUN=16`, tabella 1/n) |
| 6 | Trucchi di rendering | Parziale: 640x360, RGB565, texture 64x32, ETC1, dither. Manca `PLD` (0 occorrenze) e manca la generazione di tabelle in build |
| 7 | DMA 2D con stride | Non fatto: DMA solo lineare, `cb.stride=0` (`dma.c:139`). `dma_frames` mai attivato (`runtime.c:9653`) |
| 8 | HVS | Non fatto lato bm. Scaling dal firmware (`boot/config.txt:21`) |
| 9 | QPU | Parziale: vertex e fragment shader già su QPU. Non usato il lancio di programmi utente (SRQPC, SRQUA) |
| 10 | Control list hardware | Fatto (`gpu3d.c` JOB_BCL/JOB_RCL, `v3d.c` CT0/CT1). Non fatto: concatenare due job |

## Punti 1-10: piano

Il piano segue un principio: prima si misura, poi si ottimizza. Con V3D attivo di default, i punti 3-5 dentro `r3d.c` contano solo per il fallback `gpu3d=0`. Se il frame PS1 è lento, la causa probabile è sull'ARM: logica e Lua, layer 2D di `map()`, copia del frame, operazioni di cache, attesa in polling.

### Valutazione per punto

| # | Utile? | Motivo | Guadagno atteso | Sforzo | Rischio |
|---|---|---|---|---|---|
| 1 | Sì, da verificare | Se la RAM fosse mappata Shared, su ARM11 i dati in cache potrebbero comportarsi da non cacheable. L'impatto sarebbe enorme | Da 0 a molto alto, se c'è il difetto | S (TRM e confronto bit) | Basso |
| 2 | Sì, per file | Provare `-O3` solo su `gfx16.c` e `r3d.c`, misurando con il bench. LTO dopo, perché interagisce con linker script e `gc-sections` | 0-15% sul codice ARM caldo (stima) | S | Basso con `-O3` per file, medio con LTO |
| 3 | No, salvo misura | VFPv2 è hardware. `cosf`/`sinf` girano poche volte per frame, una tabella seno non serve | Trascurabile | M | Medio (precisione) |
| 4 | Dopo | Candidati: `g16_fade_*`, `g16_light_apply`, `dither565`. Ha senso solo se il profilo mostra il 2D ARM caldo. Meglio spostare fade e filtri sulla GPU (punto 9) | 1.5-2x su quei loop, se sono caldi | M | Basso |
| 5 | Già fatto | Nessuna divisione per pixel. `__aeabi_idiv` resta solo nel setup | Trascurabile | – | – |
| 6 | Dopo | `PLD` sui blit e sui layer di `map()` solo se caldi. ETC1 in build riduce il caricamento, non il frame | PLD 5-20% sui blit (stima) | S / M | Basso |
| 7 | Dopo, con prova | Le misure dichiarate sono contrastanti: il DMA vince sulle strip (0.26 contro 0.87 ms per 48 righe) ma perde sul frame intero (14.06 contro 11.85 ms). Impostazioni aggressive hanno bloccato il Pi | Circa 0.6 ms a frame sulle strip, se confermato | M | Alto (blocchi) |
| 8 | Dopo, ultimo | Conflitta col firmware, non si testa in QEMU. HUD e parallasse si possono già fare sul V3D | Alto sull'ARM, ma ottenibile più a buon mercato col V3D | L | Alto |
| 9 | Sì, dentro la pipeline V3D | Fade, CRT e layer come fragment shader su un quad a tutto schermo, senza lanciare programmi utente | Toglie dall'ARM fade e layer (ms da misurare) | M | Medio |
| 10 | Dopo | Concatenare due job serve solo se il bench mostra la GPU ferma mentre l'ARM lavora | Da misurare | M | Medio |

Punti aggiunti:

- **Da: memoria dei job**, circa 38 MiB su 512 MB con `queue=2`. Sostenibile. Va documentato nel budget memoria. Non serve cambiarlo ora.
- **Db: V3D in polling**. Con `queue=2` l'ARM lavora già mentre la GPU disegna. Le istruzioni di attesa nel bench dicono se vale la pena un'IRQ.
- **Dc: 8 layer di `map()` a 640x360 sull'ARM**. È il candidato più probabile come collo di bottiglia ARM nel caso PS1 con parallasse. Da misurare per primo.
- **Dd: asm del build reale**. Finora l'asm viene da clang, non dal gcc del build. Senza l'asm reale i punti 2, 4 e 6 si decidono alla cieca.

### Passi

**P1. Verifica TEX/C/B/S della RAM (E4)**
- Cosa fare: confronto in sola lettura dei bit dei descrittori in `src/arch/mmu.c` (RAM, GPU share, periferiche) con il TRM ARM1176JZF-S.
- Successo: tabella bit per bit senza discrepanze. Se c'è una discrepanza, Work Order dedicato, misurato su Pi con P3.
- Agente: `bm-research`, poi `bm-light` se c'è da correggere.

**P2. Doc obsolete (E5)**
- Cosa fare: allineare il commento di `src/gpu/gpu3d.h` al default `gpu3d_vs=2`. In `docs/progress/v3d.md` annotare il budget memoria dei job (`JOB_SECTIONS` più `JOB2_SECTIONS` con `queue=2`).
- Successo: `python scripts/docs_audit.py score` senza errori.
- Agente: `bm-mechanic`.

**P3. Baseline Pi del caso PS1 (serve misura reale)**
- Cosa fare: build, poi `./easy_install.sh`, poi `./easy_install.sh line "gpu; b3d tests=mix,heroes_tex,tiny,gpu2d,queue,big_logic,quad_tex; send"`. Copiare il report in `docs/bench/`.
- Cosa misurare:
  - ms medi e peggiori a 60 fps;
  - `gpu_ms` contro i ms del frame (dice chi detta il ritmo, ARM o GPU);
  - istruzioni ARM per frame, con e senza attesa;
  - `clean_kcyc` e `inval_kcyc`;
  - `ns_px_frame` di `gpu2d`.
- Successo: esiste un report Pi a questo commit, e da lì si stabilisce chi detta il ritmo in `mix` e `big_logic`.
- Agente: `bm-ops`, poi il proprietario sul Pi.

**P4. (Opzionale, se D1 lo approva) Bench dedicato al caso PS1**
- Contenuto: un eroe da circa 1.5k triangoli con texture 64x32, mappa da circa 5k triangoli, 2 layer di parallasse con `map()`, HUD, logica fissa come in `queue`.
- Successo: `make test-b3d` verde, e il nuovo test compare nel report.
- Agente: `bm-coder`, con Work Order da scrivere dopo la decisione.

**P5. Asm del build reale (E6)**
- Cosa fare: con `arm-none-eabi-gcc`, `objdump -d` dei loop caldi di `gfx16.c`, `r3d.c` e del codice dei layer di `map()`. Elencare spill, `__aeabi_*` e assenza di `ldm`/`stm`.
- Successo: elenco dei loop chiaramente subottimali. Se non ce ne sono, i punti 2, 4 e 6 si chiudono come "no".
- Agente: `bm-research`.

**P6. `-O3` per file**
- Cosa fare: override in `Makefile` limitato a `gfx16.c` e `r3d.c` (ed eventualmente al file dei layer). Rimisurare con il comando di P3.
- Successo:
  - `python tests/qemu_test.py -k gfx` e `make test-b3d` verdi;
  - su Pi, ms medi di `gpu2d` e `mix` ridotti di almeno 3% rispetto a P3;
  - `kernel.img` cresciuto al massimo del 5%.
- Se fallisce: si torna a `-O2`.
- Agente: `bm-light`.

**P7. Layer, fade e luci sulla GPU (punti 9 e Dc)**
- Condizione: solo se P3 mostra l'ARM a dettare il ritmo, con 2D e layer pesanti.
- Cosa fare: portare i layer di `map()` e il fade a tutto schermo nel job V3D, come fragment shader generati da `tools/qpuasm.py`.
- Successo: su Pi, `ns_px_frame` del 2D di almeno il 50% più basso, ms del frame in `mix` ridotti, `make test-b3d` verde.
- Agente: `bm-coder` su Work Order dettagliato. Se la diagnosi è incerta, prima `bm-deep` (necessità 4).

**P8. DMA per le strip (E2, punto 7)**
- Cosa fare: riprodurre con `dmatest.c` su Pi la misura 48 righe 0.26 ms contro 0.87 ms. Solo se si riproduce e il Pi non si blocca in 3 run da 10 minuti: attivare il DMA per le strip a `runtime.c:7179`, con modalità 2D (TDMODE e stride) se serve.
- Successo: tempo della strip ridotto di almeno il 50%, nessun blocco, `python tests/qemu_test.py -k dma` verde.
- Agente: `bm-light`.

**P9. PLD e SIMD in registro**
- Condizione: solo sui loop che P3 e P5 indicano come caldi.
- Successo: almeno 10% sul loop specifico, misurato su Pi.
- Agente: `bm-light`.

**P10. Più avanti, se le misure lo indicano**
- IRQ del V3D, se P3 mostra un'attesa ARM significativa con `queue=2`.
- Concatenazione su CT0/CT1, se la GPU risulta ferma.
- HVS per ultimo.

Ad ogni passo che chiude qualcosa: aggiornare `## Open work` in `docs/progress/{graphics,v3d,system}.md` ed eseguire `python scripts/docs_audit.py score`. Se cambiano chiavi di config o il comportamento di un solo kernel, eseguire anche `python scripts/consistency.py check`.

Regole da aggiungere in `## Rules (do not break)`, in forma positiva:
- in `graphics`: prima di attivare un percorso DMA, riprodurlo con `dmatest.c` su Pi e registrare la misura in `docs/bench/`;
- in `v3d`: misurare con 3D Bench su Pi ogni modifica di flag o di loop caldi, allegando il report.

## Decisioni aperte

**D1. Priorità tra QPU/V3D e HVS.**
Consiglio: prima V3D e fragment shader (P7), cioè layer e fade dentro il job esistente. Poi i programmi utente SRQPC per particelle e audio. HVS per ultimo. Collegato: un bench dedicato al caso PS1 (P4)? Consiglio sì: senza un caso misurabile le priorità restano opinioni.

**D2. DMA per le strip.**
Consiglio sì, ma riprodurre prima con `dmatest.c` (P8). Le cifre attuali sono dichiarate e non riprodotte, e ci sono precedenti di blocco del Pi.

**D3. `dma_frames`.**
Consiglio di rimuoverlo dopo P8, se il DMA 2D non cambia il risultato misurato sul frame intero. Il codice resta nella storia di git.

**D4. TEX/S in `mmu.c`.**
Consiglio di verificarlo subito (P1): costa poco e ha il rischio più alto di tutto l'elenco.

**D5. Doc di `gpu3d.h`.**
Consiglio sì, è una modifica senza rischio (P2).

**D6. Toolchain.**
- Installare `arm-none-eabi-gcc` nel sandbox per P5: consiglio sì. Serve autorizzazione, è un'installazione.
- QEMU `raspi1ap`: utile per contare le istruzioni, non i cicli (QEMU non modella i tempi dell'ARM11). I tempi arrivano solo dal Pi via 3D Bench. Consiglio sì, a priorità bassa.

**D7. Accesso al Pi per P3.**
Il proprietario esegue la baseline sul Pi (`./easy_install.sh`, poi la riga `b3d` del bench)? Senza quella baseline i passi P6-P10 restano bloccati.

## Domande aperte dell'investigazione

1. Priorità: lancio QPU utente (M44) prima di HVS per HUD e parallasse, o il contrario? (vedi D1)
2. Il DMA per il percorso box/strip (`runtime.c:7179`) va riprodotto con `dmatest.c` su Pi prima di attivarlo? (vedi D2)
3. `dma_frames` è codice morto da rimuovere o da tenere per esperimenti? (vedi D3)
4. Il commento su TEX/S in `mmu.c` va verificato contro il TRM ARM1176JZF-S? (vedi D4)
5. `gpu3d.h` dice che i vertici sono ombreggiati sull'ARM: aggiornare la doc, dato che il default è `gpu3d_vs=2` su QPU? (vedi D5)
6. Autorizzi l'installazione di `arm-none-eabi-gcc` per l'asm del build reale e le misure QEMU (`raspi1ap`) sui loop caldi? (vedi D6)
