# Roadmap bm

Il piano di sviluppo di **bm** (BareMetal), una *fantasy console* bare metal per Raspberry Pi Zero W e PowKiddy RGB30. Qui stanno **obiettivi, requisiti e priorità**; il come sta nei file di area di [`progress/`](progress.md), la storia nel `git log`. Milestone chiuse e testo storico: commit `0e969b5`. Il traguardo corrente è la [Beta](#beta-obiettivo-del-prossimo-traguardo).

## Come leggerla

- **Ordine dei livelli** (si sviluppa dall'alto verso il basso): 1 Sistema → 2 Funzioni e API → 3 Editor e app → 4 Giochi (facoltativo). Un livello non blocca il successivo, ma a parità di priorità vince il livello più alto.
- **Una riga = un obiettivo**, mai un compito. La riga rimanda al file di area (colonna *Dettagli*): lì c'è la sezione `Open work` / `To do` da cui si ricavano i work order. Non copiare qui il dettaglio.
- **ID**: restano `M##` (milestone) e `R##` (spunto, da idea a milestone se si decide). Le voci nuove usano `SYS-n`, `API-n`, `APP-n`, `GAME-n`. Un ID non si riusa né si rinumera.
- **Pri**: `P0` blocca altro lavoro o rompe una promessa del progetto; `P1` il prossimo; `P2` dopo; `P3` idea non pianificata.
- **Stato**: `in corso`, `da provare sul Pi` (fatto su PC/QEMU), `in specifica`, `da fare`, `idea`.
- **Requisito**: la condizione verificabile per chiuderla ("fatto quando"). Se manca (`da specificare`), la riga non è pronta per un work order: va al `bm-planner`.
- **Dopo**: dipendenze (ID). `-` = indipendente.

### Come la usa il sync

1. Il supervisore (SPV) o il `bm-planner` sceglie le righe di priorità più alta senza dipendenze aperte, partendo dal livello più alto.
2. Apre il file di area indicato e prende la voce in `Open work` per scrivere i work order in `sync/wo/<ID>.md` (formato in [`SYNC.md`](../SYNC.md)); un operatore = una funzione.
3. A chiusura aggiorna **questa riga** (stato, o rimozione) e la sezione `Open work` del file di area nello stesso commit. Le prove su hardware restano all'utente e portano le divergenze da `untested` a `resolved` in `docs/spec/divergences.toml`.

---

## Beta (obiettivo del prossimo traguardo)

**Che cos'è.** Una beta chiusa: uno sviluppatore indie, con la sola suite di bm e la documentazione, scrive un gioco 2D o 3D con menu iniziale, più stage e un finale, lo compila in `.bm` e lo gira su Pi Zero W e RGB30 con prestazioni accettabili. Poi lo sviluppatore usa bm, ne trova i limiti e il lavoro successivo parte dai suoi riscontri. I criteri qui sotto sono quelli per **aprirla**; ogni riga rimanda alle righe dei livelli (colonna *Dopo*) dove sta il lavoro.

**Prova di accettazione.** Una persona che non conosce il codice di bm parte dalla guida, crea con i soli strumenti della suite un gioco con titolo, 3 stage e finale (una versione 2D e una 3D), lo prova su entrambe le console e lo esporta. Se serve il PC solo per copiare file sulla SD, va bene; se serve leggere il codice del kernel, la prova è fallita.

| ID | Pri | Criterio per aprire la beta | Requisito verificabile | Dopo |
|---|---|---|---|---|
| BETA-1 | P0 | Base stabile | CI verde su `bm-core`; nessun freeze nei log di una sessione lunga su Pi e RGB30; il crash scrive un `bootprev.txt` giusto (prove su hardware fatte dall'utente) | SYS-1, SYS-2, SYS-5 |
| BETA-2 | P0 | Un gioco completo si costruisce con la suite | la prova di accettazione, versione 2D | APP-4, APP-1, APP-6 |
| BETA-3 | P0 | L'API è integrata nell'editor | aiuto sulla funzione e lint in bm Code; completamento con una sola fonte API | APP-5 |
| BETA-4 | P1 | Prestazioni accettabili in 2D e 3D | budget misurato e pubblicato per Pi e RGB30; il gioco della prova a 60 fps (o con il frame rate dichiarato nel budget) | API-1, M36, M35, M37, M41 |
| BETA-5 | P1 | La versione 3D della prova | un gioco 3D con più stage gira con la GPU (V3D sul Pi, Mali sull'RGB30) entro il budget | BETA-4, APP-2 |
| BETA-6 | P1 | Coerenza tra i due kernel | `python scripts/consistency.py gate` senza divergenze `open` o `untested` che toccano menu, input, Games, SDK e salvataggi (D01-D13, D28, D30-D36 e simili); le altre accettate per iscritto | SYS-5, APP-1 |
| BETA-7 | P1 | Guida per i tester | `GAME-GUIDE.md` e un avvio rapido di una pagina portano al primo gioco. I riscontri dei tester arrivano all'utente con contatto diretto, fuori da bm: nessun canale nel prodotto | APP-3 |

**Lua e LuaJIT (decisione di base, da confermare).** Per la beta si resta sull'interprete Lua 5.4: LuaJIT non serve e non conviene. Una operazione costa circa 100 ns (circa 140.000 per frame); il disegno e il 3D stanno in C e sulla GPU, e `docs/PERFORMANCE.md` (scelte scartate) ha già scartato il JIT perché richiede generare codice a runtime su bare metal; inoltre l'hook di `profile` e dei breakpoint non scatta nel codice compilato (`docs/B16.md`, sezione sull'hook di debug). Si riconsidera solo se i riscontri del tester mostrano giochi limitati dal Lua e non dal disegno: prima si misura con `profile(true)` e si sposta il lavoro pesante in C o sulle QPU.

**Fuori dalla beta** (restano nei livelli, senza bloccarla): multiplayer, Tiled, sinc/QPU, audio a 24 bit, video e giochi di esempio come Overbit e Yharnam.

---

## 1. Sistema (kernel, piattaforme, strumenti di sviluppo)

| ID | Pri | Stato | Obiettivo | Requisito | Dettagli | Dopo |
|---|---|---|---|---|---|---|
| SYS-1 | P0 | in corso | CI verde su `bm-core` | tutti i job di `ci.yml` (host, qemu 1-4, rgb30) verdi su `bm-core`; `scripts/ci_audit.py` exit 0 | [tools](progress/tools.md), [sync](progress/sync.md) | - |
| SYS-2 | P0 | da provare sul Pi | Freeze report e log su SD affidabili | un riavvio voluto non segnala un freeze; un crash scrive `bootprev.txt` giusto; la password è mascherata nei log su SD; il watchdog conta sul Pi Zero W | [system](progress/system.md) `Open work` | SYS-1 |
| SYS-3 | P1 | da provare sul Pi | Prove del Pi dal PC (rete, tasti, screenshot) | `bm_net.py --shot/--keys` e il bridge provati su un Pi; R-NET3 con un token di test limitato | [tools](progress/tools.md) `Pi tests from the PC`, [pi-test](design/pi-test.md) | - |
| SYS-4 | P1 | da fare | Coordinamento multi-agente misurabile | ledger e `agent_audit.py` con baseline; perimetro dei work order controllato da script | [sync](progress/sync.md), [sync-workflow](progress/sync-workflow.md) | - |
| M42 | P1 | in corso | Profilo `.b16` identico su Pi e RGB30 | *Yharnam* `.b16` gira indistinguibile su Pi Zero W e RGB30, stesso throttle CPU | [b16](progress/b16.md) §2-3 | - |
| SYS-5 | P1 | in corso | RGB30 stabile: pannello dopo riavvio a caldo, Start+Select, un solo core | riavvio a caldo senza schermo nero; Start+Select congela e riprende come sul Pi | [platforms](progress/platforms.md) `Open work`, [multitasking](progress/multitasking.md) `Open` | - |
| M41 | P2 | in corso | GPU Mali-G52 della RGB30 e Overbit in `.b16` | Overbit `.b16` a 60 fps con la Mali; 3D Bench riporta le metriche GPU | [graphics-mali](progress/graphics-mali.md) §M41, [platforms](progress/platforms.md) | M42 |
| SYS-6 | P2 | da fare | Multitasking: misure da rifare, fermi della CPU, download dentro un'app | `Open` di multitasking.md svuotata o ridotta a idee | [multitasking](progress/multitasking.md) | - |
| R3, R4 | P2 | da verificare | Log su SD e screenshot: chiudere dopo le prove su console | log SD (D19) e `shot` provati su console; voci tolte da `Open work` | [system](progress/system.md) | SYS-2, SYS-3 |
| R1, R2, R6, R8, R19, R20 | P3 | idea | Console standalone: WiFi da pad, tastiera a schermo, web UI, rumble DS4, pad BT, HDMI-CEC | da specificare | [system](progress/system.md) `Open work` | - |
| R22 | P3 | idea | Guscio portatile Pi Zero (GPIO, display DPI/SPI) | da specificare | [platforms](progress/platforms.md) | - |

## 2. Funzioni e API (GPU, runtime, formati, audio)

| ID | Pri | Stato | Obiettivo | Requisito | Dettagli | Dopo |
|---|---|---|---|---|---|---|
| M36 | P1 | in corso (quasi completa) | Vertici sulla GPU: mesh caricata una volta, le QPU trasformano e illuminano | anello eroi di Overbit e stress test al doppio delle prestazioni rispetto a solo ARM, a 60 fps | [v3d](progress/v3d.md) `To do` | - |
| M35 | P1 | da provare sul Pi | ARM e GPU insieme: code asincrone | benchmark Overbit e stress sul Pi con guadagno chiaro, opzione attiva e spenta | [v3d](progress/v3d.md) | M36 |
| M37 | P1 | da provare sul Pi | 2D e qualità sulla GPU (`gpu3d_2d`, `menu_scale=3`, bilineare) | passo 16 del test `g`: pixel identici ARM/GPU; menu a 1080p corretto | [v3d](progress/v3d.md) | M36 |
| M47 | P1 | in corso | Progetti `.bme` modificabili, `.bm` in sola lettura | gli strumenti aprono un `.bm` in sola lettura e chiedono la copia `.bme`; `Ctrl+B` genera il `.bm` in Games | [tools](progress/tools.md), [lua](progress/lua.md) | - |
| M39 | P2 | in corso (su PC) | GPU 3: verso il limite della V3D (attributi compatti, texture 16 bit, ETC1) | stress test al doppio dei triangoli a 60 fps; Overbit con 10 bot a 60 fps in HIGH | [v3d](progress/v3d.md) `To do`, [lua](progress/lua.md) | M35 |
| M43 | P2 | in specifica | Sprite stacking (`stack`, `zstack`, `stackv`) | decine di pile a 60 fps sul Pi; conversione in mesh | [graphics](progress/graphics.md), [suite-pixel](design/suite-pixel.md) | - |
| M46 | P2 | in corso | Audio 2: sintetizzatore stereo 24 bit, campioni PCM/WAV, assistente, `riff` | giochi e brani di bm Sound a 24 bit su Pi e RGB30 con campioni e pattern | [audio](progress/audio.md), [audio-samples](progress/audio-samples.md) | - |
| M44 | P3 | da fare | Programmi sulle QPU (particelle, buio di Yharnam, audio a blocchi) | particelle e buio di Yharnam calcolati dalla GPU con fallback trasparente | [v3d](progress/v3d.md) `To do` §5, [graphics](progress/graphics.md) | M36, M41 |
| R40-R42 | P3 | idea | Profiler, scaling sinc/QPU, impostor degli eroi | misure sul Pi che giustifichino ciascuno | [v3d](progress/v3d.md) | M36 |
| R7, R27, bmnet | P3 | idea | Accenti UTF-8 in `print()`, batteria e autosave, multiplayer in rete | da specificare | [runtime](progress/runtime.md) `Open work`, [system-net](progress/system-net.md) | - |
| R9, R17, R21, R25, R31-R35 | P3 | idea | WAV da SD, import MIDI, audio GPIO, audio PC, campioni e riff | da specificare | [audio-samples](progress/audio-samples.md), [audio-output](progress/audio-output.md) | M46 |
| API-1 | P1 | da fare | Budget di prestazioni 2D e 3D pubblicato e misurato su Pi e RGB30 | una tabella (sprite, tile, mesh, particelle a 60 fps) con misure vere su entrambe le piattaforme; l'RGB30 oggi non ha numeri; `profile(true)` spiegato nella guida | [PERFORMANCE](PERFORMANCE.md), [STRESS](STRESS.md), [lua](progress/lua.md) | M36, M41 |

## 3. Editor e app (la suite bm)

| ID | Pri | Stato | Obiettivo | Requisito | Dettagli | Dopo |
|---|---|---|---|---|---|---|
| APP-1 | P1 | in corso | bm SDK su RGB30 (Dev, pad, Games con `/carts`) | prove su console per ogni voce di `Open work`; divergenze D28 ecc. a `resolved` | [tools](progress/tools.md) `bm SDK (RGB30)` | M47 |
| M48 | P2 | in corso | Mouse in tutta la suite (USB e Bluetooth) | ogni programma seleziona, disegna e naviga col puntatore senza tastiera | [tools](progress/tools.md) | - |
| M45 | P2 | in corso | bm Write: documenti `.BMD`, export TXT/MD/HTM/PDF | un documento formattato esportato in PDF identico sul PC | [tools](progress/tools.md) | - |
| APP-2 | P2 | da fare | Mappe 3D in bm SDK; bm Pixel per le pile (M43 passo 3) | passi di `Open work` chiusi con una mappa 3D salvata e ricaricata | [tools](progress/tools.md) `Mappe 3D in bm SDK`, [suite-sdk](design/suite-sdk.md), [suite-pixel](design/suite-pixel.md) | M43 |
| APP-3 | P3 | da fare | Manuali PDF e struttura del progetto | manuali EN/IT allineati; layout di `docs/PROJECT-LAYOUT.md` rispettato | [tools](progress/tools.md) `PDF manuals`, `Project layout` | - |
| R11, R37, R38, R39 | P3 | idea | Tiled, palette delle mappe, assistente 16×16, SDK 720×720 | da specificare | [suite-sdk](design/suite-sdk.md), [sdk-720](design/sdk-720.md) | - |
| R15, R16, R26 | P3 | idea | Live reload dal PC, template bm Code, mouse in nano8 | da specificare | [tools](progress/tools.md), [suite-code](design/suite-code.md) | - |
| APP-4 | P0 | da fare | Template di gioco completo: menu iniziale, più stage, finale, salvataggio | dal bm SDK un nuovo progetto "Stages" compila in `.bm` e gira con titolo, 3 stage, schermata finale e record; usa la macchina a stati di `games-architecture.md` | [games-architecture](design/games-architecture.md), [tools](progress/tools.md) | APP-1 |
| APP-5 | P0 | da fare | API integrata nell'editor: aiuto sulla funzione sotto il cursore e lint | in bm Code un tasto mostra firma e descrizione della funzione; un controllo segnala nomi di API inesistenti; la lista API ha una sola fonte generata da `docs/api/` e usata anche da `predict` | [tools](progress/tools.md), [PREDICT](PREDICT.md), [API](API.md) | - |
| APP-6 | P1 | da fare | Sviluppare un gioco senza PC: risorse e progetti gestibili sul dispositivo | su RGB30 e Pi si crea, si salva e si compila un progetto con sprite, mappa, suoni e codice senza il PC (oggi i file di risorse sono solo da PC e l'RGB30 non ha la scheda Lib) | [tools](progress/tools.md) `bm SDK (RGB30)`, [CONSISTENCY](spec/CONSISTENCY.md) | APP-1 |

## 4. Giochi (facoltativo)

| ID | Pri | Stato | Obiettivo | Requisito | Dettagli | Dopo |
|---|---|---|---|---|---|---|
| GAME-1 | P3 | in corso | Yharnam: verifiche aperte, mappa e prestazioni sul Pi | le verifiche di §2 chiuse; mappa provata sul Pi | [yharnam](progress/yharnam.md) §2-3, [tools](progress/tools.md) | M42 |
| GAME-2 | P3 | in corso | Overbit: schermate, qualità, piloti dei mech dalle immagini | voci di `Open work` chiuse; benchmark sul Pi e sulla RGB30 | [overbit](progress/overbit.md), [tools](progress/tools.md) | M41 |
| GAME-3 | P3 | idea | Architettura di gioco e kit di gameplay | un gioco di prova usa il kit senza codice duplicato | [games-architecture](design/games-architecture.md) | - |

---

## Decisioni di base in vigore

- **Video delle cartucce `.bm` (2026-09-26)**: 640×360 (16:9; ×2 su 720p, ×3 su 1080p); 320×180 facoltativa; 360×360 su `.b16`. Colore RGB565. Disegno sotto il 25% del frame; rendering in C, Lua solo logica.
- **Opzioni GPU e 3D (2026-10-03)**: ogni novità GPU che cambia aspetto o latenza è un'opzione in `bm/config.txt` o `gpu3d()`; misure comparative su Overbit; overlay unico FPS / ms / CPU / GPU su F11.
- Altre decisioni aperte: [`open-decisions.md`](open-decisions.md).

## Vincoli di progetto

- Lua su ARM1176 è lento: disegno in C, cache attive, calcoli critici nel runtime C o sulle QPU.
- Firmware Broadcom closed-source: versione fissata (`FW_REF`) per evitare regressioni video o mailbox.
- Integrità della SD: hash dei file scaricati, catene FAT protette, cartelle di sistema isolate.
- Nessuna GPU reale in CI: emulatori QPU/V3D su PC, timeout di protezione, rasterizzatore software di riserva.
- Hardware di sviluppo (seriale, alimentazione, controller): [`HARDWARE.md`](HARDWARE.md) *Development setup*.
