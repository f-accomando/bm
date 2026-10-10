# Roadmap bm

Le milestone chiuse e il testo storico sono nella storia git: prima di questa versione, commit 0e969b5.

**bm**, BareMetal: una *fantasy console* bare metal per Raspberry Pi Zero W (e PowKiddy
RGB30) che avvia da SD, mostra un menu, carica giochi in Lua (cartucce `.bm`, intestazione
`BMCART`, cartella `bm/` sulla SD) e li esegue a 60 fps con grafica, input e audio. Strumenti
`tools/bm_net.py`, `tools/bm_load.py`, `scripts/mkbm.py`, runtime in `src/bm/`. Il kernel
legge ancora la cartella e le cartucce col nome di prima.

Ogni milestone ha un **criterio di completamento** verificabile ("Fatto quando") e, quando
possibile, un test automatico sul PC o in QEMU (`-M raspi0`). Dimensione: **S** = pochi
giorni, **M** = 1–2 settimane, **L** = più di 2 settimane (XL: più di una L).

Dipendenze tra le milestone aperte:
- M35 (coda) e M37 (2D nel lavoro della GPU) si misurano insieme; i profili con la coda
  vogliono anche il vertex shader di M36.
- M39 parte dalle misure sul Pi dei profili di M35–M36.
- M44 usa l'interprete QPU dell'emulatore di M36; sulla RGB30 aspetta il driver Mali di M41.
- M42, passo 5 → Overbit in `.b16` (M41, passo 1).
- M43, M45, M46, M47, M48: indipendenti.

---

## Da fare (aggiornato 2026-10-10)

Controlli su Yharnam, senza milestone propria. Sono solo da verificare: nessuna correzione
in questa passata, nessuna cosa già decisa.

- [ ] **Prestazioni di Yharnam**: misurare il costo di un fotogramma sul Pi Zero W e sulla
  RGB30 (generazione dei chunk in sottofondo, `stream()`, buio a livelli, particelle) con il
  report di sessione del dev kit e le righe `devinfo()` a fine partita. Il confronto è con il
  budget di 60 fps di M42.
- [ ] **Mappa disegnata di Yharnam sul Pi**: aprire `yharnam.bm` nell'SDK, F3 F3, `z`, piazzare
  qualche tile, Ctrl+S (la copia `YHARNAM.BME`, circa 6 MB: lo sheet resta SHEET8), F5: il
  terreno cambiato si vede nel gioco. Poi `python3 carts/yharnam/mkmap.py --from` sul file
  copiato dalla SD e commit dei CSV.
- [ ] **Glitch grafico all'avvio di Yharnam**: un piccolo difetto visibile nei primi fotogrammi.
  Da descrivere sul Pi: quando compare, quanto dura, in quale schermata (titolo o gioco dopo
  i primi chunk). Punti da guardare, come ipotesi: `_init` (le tabelle di fade prima del primo
  disegno) e `visible_chunks` (i chunk non ancora pronti non si disegnano).

---

## Decisioni in vigore

**Video delle cartucce `.bm` (2026-09-26).** 640×360, 16:9, scala intera ×2 su 720p e ×3
su 1080p (la risoluzione della console, nessun cambio di modo); 320×180 facoltativa
(header). RGB565 (colore diretto, metà banda del 32 bit); l'API riceve i colori come RGB888 e
la grafica è salvata indipendente dal framebuffer, così il 32 bit resta possibile (il 24 bit
impacchettato no). Budget: disegno completo (mappa piena + 256 sprite) sotto il 25% del
fotogramma; tutto il disegno in C, Lua solo logica. Il profilo `.b16` ha le sue regole
(M42, `docs/B16.md` §0).

**GPU e 3D (2026-10-03).**
- Ogni novità della GPU che cambia aspetto, latenza o memoria è **un'opzione**: una chiave
  in `bm/config.txt`, una riga in *Settings > Graphics* (o *Screen and sound*) e un argomento
  di `gpu3d()` per cartucce e benchmark. Il default resta quello verificato sul Pi; le
  ottimizzazioni a pixel identici non hanno interruttore. Dove la V3D fa qualcosa che il Pi
  non ha ancora mostrato, una prova all'avvio la spegne da sola se non torna.
- Ogni passo si misura su Overbit prima e dopo: il suo benchmark (bot, qualità, renderer,
  anello di eroi), `tests/overbit/frames.py`, `tests/bm/herobench.c`, `tests/bm/mapbench.c`,
  `tools/armprof.py`, `make bmhost-gpu`; sul Pi con il 3D Bench, il test `g` del monitor e le
  foto del report.
- Escluso (decisione dell'utente): impostor per gli oggetti lontani, risoluzione che cambia
  da sola per guadagnare fotogrammi, overclock della GPU e dell'ARM.

---

## M35 — ARM e GPU insieme (M) — sul PC, da provare sul Pi
Obiettivo: il tempo in cui l'ARM aspetta la V3D a fine fotogramma (2,1 ms su 9,1 nella scena
di prova) fuori dal fotogramma, e meno lavori per fotogramma.
- Fatti sul PC (branch `bm3d-driver`): la coda del fotogramma (bm3d 4.0, `gpu3d_queue`,
  *Graphics > 3D frame queue*, quarto argomento di `gpu3d()`), il 2D dopo il 3D registrato e
  messo sulla pagina dopo (4.1, `d2` in `runtime.c`), lo `zclear()` nel lavoro senza early z
  dopo di lui (4.4, `fs_zclear`), la coda senza vertex shader (profilo GPU+Q nel 3D Bench,
  renderer GPU+Q in Overbit), la memoria dei lavori senza cache (4.5, `gpu3d_wc=1`, *3D job
  memory*, `mmu_set_cached`, profilo GPU+WC), 8 texture in un lavoro e il 2D nel lavoro della
  GPU (4.6, 4.8, `gpu3d_2d=1`, M37). Prove sul PC: `make test-queue2d`, test `queue` del 3D
  Bench.
- Sul Pi (v0.2.0): `queue yes`, il passo 15 del test `g` disegna come l'ARM; lo `zclear()`
  nel lavoro allora dava `no` (corretto in 4.4, da riprovare).
- **Da provare sul Pi:** test `g` (passo 15: `zclear in job yes`; passo 16: il 2D nel
  lavoro); 3D Bench con i profili GPU+Q, GPU+WC, GPU+2D e il test `texswap`; il benchmark di
  Overbit con GPU+Q e GPU+VS+Q.
- **Fatto quando:** il benchmark di Overbit e lo stress test mostrano il guadagno sul Pi,
  con l'opzione accesa e spenta.

## M36 — Vertici sulla GPU (L/XL) — sul Pi manca solo il benchmark di Overbit
Obiettivo: la mesh va alla GPU una volta sola e a ogni `draw3d` l'ARM manda solo matrice,
luci e ossa; le QPU trasformano e illuminano (shader `vs_baked`, `vs_tex_rgb`, `vs_lit`,
`vs_lit_tex`, `vs_lit_tex2`, ombre `vs_shadow*`/`cs_shadow*`), la V3D scarta e taglia.
Opzione `gpu3d_vs` (0 ARM, 1 lo scenario, 2 tutti i modelli), *3D vertices*, terzo
argomento di `gpu3d()`.
- Stato (2026-10-06, bm3d 6.5–6.6, kernel `v0.2.3-51`): sul Pi il vertex shader disegna con
  il clipper sempre acceso (`gl_clip_all`; riga di stato `vertex shader yes (clipper always
  on), clipping yes, lit models yes`), il passo 14 del test `g` dà lo 0,0% di pixel diversi
  dall'ARM. Nel 3D Bench, a 60 fps, GPU → GPU+VS: sfere 205 → 833, eroi 6,1 → 67,5 (score
  circa 3280 contro 986). Il vertex shader è acceso di default (livello 2); il profilo
  GPU+VS+FS2 è quello dello score.
- **Da fare sul Pi:** il benchmark di Overbit con GPU+VS (e GPU+VS1) per chiudere.
- **Fatto quando:** sul Pi l'anello di eroi di Overbit e le sfere dello stress test crescono
  di almeno 2× rispetto alla GPU senza vertex shader a 60 fps (nel 3D Bench passato: 11× e
  4×; manca Overbit).

## M37 — 2D e qualità sulla GPU (M, se serve) — fatta sul PC (2026-10-05), da provare sul Pi
Fatto sul PC (branch `bm3d-driver`, bm3d 4.8–4.9):
- **2D sulla GPU** (`gpu3d_2d=1`, *2D over the 3D*): `rectfill`, `rect`, `pset`, righe
  dritte, `spr`, `sspr` (girati, zoom interi), `map()` e `print` come quad nel lavoro del 3D,
  pixel per pixel come gfx16; il resto (cerchi, righe oblique, triangoli, `prompt`, zoom non
  interi) chiude il lavoro come prima. Prove: `make test-gpu3d` scena `2D on GPU`, `make
  test-queue2d`, 3D Bench profilo GPU+2D e test `gpu2d`, test `g` passo 16.
- **Menu a 1080p** (`menu_scale=3`): il layout 640×360 ingrandito ×3 dalla GPU
  (`gpu3d_enlarge`), senza GPU dall'ARM. Quad nativi a 1080p (copertine più nitide) sono un
  passo dopo, se serve (`docs/RISOLUZIONI.md`, sezione 6).
- **Texture filtrate** (`gpu3d_filter=1`, *3D textures: Bilinear*), test `bilinear`; il 2D
  resta al texel più vicino.
- **Matrice unica e luce nel modello sull'ARM** (`r3d_fast=1`, *3D on the ARM: Fast*), anche
  sulla RGB30; spento, i checksum di `bench3d` restano quelli di prima.
- **Da provare sul Pi:** il passo 16 del test `g` (lo stesso HUD dall'ARM e dalla GPU, pixel
  uguali), il 3D Bench (GPU+2D, `gpu2d`, `bilinear`), il menu con `menu_scale=3` (riga `menu:
  1920x1080, the layout enlarged by the GPU` nel log, e l'overlay).

## M39 — GPU 3: verso il limite della V3D (L/XL) — passi bm3d 5.0–5.6 sul PC
Obiettivo: i triangoli della GPU da ~3 milioni al secondo verso i 16 visti in un demo reale
sulla stessa GPU (25 dichiarati), lavorando su come i vertici arrivano alla V3D e su come
ARM e GPU si passano il lavoro. Driver **bm3d 5.x**; ogni passo è un'opzione, spenta finché
il Pi non la verifica, confrontata dai renderer di Overbit e dai profili del 3D Bench.
Misure di partenza sul Pi (v0.2.0, ARM 0.2, GPU 2.1, GPU+AA) in `docs/bench/`.
Passi aperti o da provare (branch `bm3d-driver`):
1. **Misure sul Pi** con i profili del vertex shader e il test `big`/`big_logic` del 3D Bench
   (modelli da 10 080 triangoli su una mappa da 14 112: "regge 34 000 triangoli?").
2. **Mesh indicizzate e attributi compatti**: fatti angoli condivisi, primitive indicizzate a
   16 bit con la prova all'avvio, ordine della cache dei vertici (Forsyth, `gpu3d_sort`);
   **restano** gli attributi a 16 e 8 bit (posizioni scalate, normali e colori a 8, uv a 16;
   posizioni separate per lo shader di coordinate).
3. Fatto: 65535 vertici e facce in `.bm`, r3d, `mesh()`, bm Studio, bm Mesh.
4. **Due lavori in volo** (`gpu3d_queue=2`) fatto nel driver; **resta** il runtime: mostrare
   il fotogramma uno dopo, perché anche il `_draw` vada insieme alla GPU.
5. **Texture più leggere**: fatto RGB565 per gli sheet opachi (`gpu3d_tex16`); **restano**
   RGBA5551/4444 per i ritagli, ETC1 (al build) e le mipmap; la prova all'avvio controlla come
   la TMU legge ogni formato.
6. Fatto: fragment shader a due thread (`gpu3d_fs2`, `fs_tex_lit_t`, `fs_tex_rgb_t`, regole in
   `qpuasm.py`).
7. Fatto: opachi dal più vicino dentro il lavoro (`gpu3d_sort`).
8. Fatto: `visible3d`, `pvs3d`, usati da Overbit per gli eroi.
9. Memoria senza cache: in M35 (`gpu3d_wc`).
10. **Il Lua dei giochi** (fuori dal driver): in Overbit circa il 60% del tempo dell'ARM;
    parti calde in C (raggi e collisioni, strade dei bot, particelle), meno allocazioni per
    fotogramma, `frames.py` per misurare; uno studio a parte su LuaJIT (ARMv6) sul bare metal.
- **Da provare sul Pi:** test `g` (la riga di stato: `indexed`, `16-bit textures`,
  `two-thread shaders`), 3D Bench con i profili GPU+VS+Q2, GPU+T16, GPU+FS2, GPU+VS+S, il
  benchmark di Overbit con le opzioni accese.
- **Fatto quando:** sul Pi il test `big` regge almeno il doppio dei triangoli a 60 fps di
  bm3d 4.1, il test `spheres` del 3D Bench almeno 3× quelli di 3.4, e Overbit (GPU+VS+Q) sta
  nei 60 fps a HIGH nello scontro di 10 bot.

## M41 — RGB30: la GPU Mali e Overbit in `.b16` (XL) — in corso (driver)
Richiesta dell'utente (2026-10-05): lo stesso banco di prova della GPU del Pi sulla RGB30
(Mali-G52, Bifrost), con Overbit come gioco di misura. Oggi lì il 3D lo fa l'ARM e il 3D
Bench ha le colonne GPU vuote.
1. **Overbit in `.b16`** (per ora un `.bm` con l'estensione diversa, `docs/B16.md`)
   nell'immagine della RGB30, a **360×360** (×2 dal controller video) e **720×720**, scelte
   nel menu RESOLUTION: `screen()` con i modi quadrati (`fb_init_mode`: righe a 64 byte e
   tessere da 16), in Overbit HUD, menu, cielo e campo visivo per l'1:1 (`SW`, `SH`, `LW`×`LH`
   e `UI` di `00_core.lua`).
2. **Le misure con l'ARM**: il benchmark di Overbit (bot e anello di eroi) alle due
   risoluzioni, con il report come sul Pi.
3. **Il driver Mali**, a passi: triangoli preparati dall'ARM e disegnati dalla GPU, poi
   vertex shader, poi la coda; per ogni passo una prova all'avvio, un profilo nel 3D Bench e
   un renderer in Overbit. Fatto (bm3d 6.0–6.1, `make TARGET=rgb30 test-mali`, report `gpu`
   della console del 2026-10-05): accensione della Mali, MMU LPAE, lavori del job manager, un
   lavoro di frammenti che pulisce una superficie (descrittori Bifrost v7 da Mesa, MIT).
   **Prossimo:** i triangoli preparati dall'ARM (lavoro del tiler con contesto e heap) con un
   fragment shader Bifrost minimo (`ISA.xml` di Mesa), poi il vertex shader.
- **Fatto quando:** sulla RGB30 Overbit `.b16` gira a 360×360 e a 720×720 e il suo benchmark
  manda il report, prima con l'ARM e poi con la Mali; il 3D Bench della RGB30 ha le righe GPU.

## M42 — Il profilo `.b16` (L) — da fare
Decisioni dell'utente del 2026-10-05 in [B16.md](B16.md) §0: un ambiente limitato come
PICO-8, uguale sul Pi e sulla RGB30 (stesso contenitore con il campo del profilo; schermo
360×360 o 720×720 fisso; 256 colori; Lua 4 MiB; banchi grafici 1024×1024; 8 voci;
salvataggio 64 KiB; CPU a budget fisso a 60 fps che scende da sola a 30; 3D con un tetto di
triangoli; pad stile SNES; niente file; `rnd()` con seme; 8 MiB; nessun limite di token).
1. Il campo del profilo, `mkbm.py --b16`, i controlli (lettore e impacchettatore) e il menu:
   il Pi mostra `.bm` e `.b16`, la RGB30 solo `.b16`.
2. Il runtime nel profilo: schermo fisso, tavolozza, memoria, sandbox, banchi grafici.
3. La CPU a budget: costi dei disegni, misura sul Pi, 60 → 30 fps da soli, la percentuale nel
   dev kit.
4. L'SDK: il target `.b16` salva un `.b16` vero (oggi solo i promemoria del dev kit, §8.5).
5. Yharnam in `.b16` (360×360, sheet a banchi), poi Overbit (M41).
- **Fatto quando:** Yharnam `.b16` gira uguale sul Pi e sulla RGB30 nel profilo, con la CPU
  che scende a 30 fps negli stessi punti sulle due console.

## M43 — Sprite stacking (L) — da fare
Richiesta dell'utente (2026-10-06). Un oggetto è una pila di fette 2D disegnate una sopra
l'altra con un piccolo scarto e ruotate: un volume 3D al costo del 2D. Due costi: solo la
rotazione attorno a z (classico) e x, y, z libere (volume di voxel).
1. **Classico nel runtime**: `stack(sx, sy, w, h, n, x, y, [rz, scala, passo])`, le `n`
   fette in fila nello sheet da `(sx, sy)` (a destra, poi sotto), in C (`gfx16.c`: una fetta
   ruotata è un `sspr` campionato all'indietro, colore 0 trasparente), anche in `draw2d()` e
   in bmhost. Misura sul Pi: quante pile da 16 fette 16×16 a 60 fps (test del dev kit).
2. **Pile con il nome**: zona di SPRITES con il tipo `stack` e il numero di fette
   (`sprites.txt`, `mkbm.py --sprites`, `bmres.py`), `zstack(nome, x, y, [rz, scala])`; nella
   scheda Lib (anteprima che gira) e nei `.bmi`.
3. **Editor in bm Pixel**: pagina *Stack* (fette in griglia e sovrapposte, onion skin,
   anteprima che gira, copia nella fetta dopo, nuova pila da una zona).
4. **x, y, z libere**: `stackv(...)` con `rx`, `ry`, `rz` come volume di voxel (fette rifatte
   lungo l'asse più vicino alla camera, da dietro in avanti); misura come al passo 1.
5. **Verso gli altri strumenti**: una pila diventa MESH (facce visibili dei voxel, colori
   dello sheet) per bm Studio e bm Mesh, e fotogrammi in 8 direzioni (bm Animator).
6. **Documentazione ed esempi**: i quattro doc API, la kb dell'assistente, un modello
   dell'SDK *Top-down stack*.
- Prove: bmhost (fotogrammi contro un riferimento in Python), `make test-res` per le zone
  `stack`, QEMU con un gioco di prova e bm Pixel.
- **Fatto quando:** un gioco top-down con decine di pile che girano va a 60 fps sul Pi, le
  pile si disegnano e si modificano in bm Pixel e diventano modelli 3D e fotogrammi.

## M44 — La GPU come coprocessore: programmi sulle QPU (L/XL) — da fare
Richiesta dell'utente (2026-10-06). Le 12 QPU fanno la stessa operazione su 16 numeri alla
volta: calcoli uguali su tanti dati, quando il 3D non le usa (giochi 2D, menu) o con alcune
QPU riservate nei giochi 3D.
1. **Il driver**: il lancio di un programma QPU "utente" fuori dal disegno (richieste di
   programma della V3D: codice, uniform, numero di QPU, contatore dei completati; oggi `v3d.c`
   dà tutta la VPM ai vertici), una coda, l'attesa con il timeout, la prova all'avvio che lo
   spegne e l'ARM come riserva per ogni programma; l'emulatore `tests/gpu/v3d_emu.c` che li
   esegue, un passo del test `g` e un profilo del 3D Bench.
2. **Particelle sulla GPU**: `particles()` (posizione, velocità, gravità, durata, colori),
   mosse e disegnate senza l'ARM: migliaia invece di centinaia; poi `lib.particles` in bmlib
   sopra di lei.
3. **Effetti a schermo intero dei giochi 2D**: buio a livelli e bagliori di Yharnam (oggi
   `g16_fade_*` sull'ARM), dissolvenze, sfocature, filtro CRT.
4. **Effetti audio** a blocchi (riverbero, eco, filtri), anche per nano8.
5. **Tanti raggi insieme** (`hit3d`, visibilità dei bot): solo se servirà.
Non conviene per il Lua, CRC, SHA-256, decompressione, il riduttore di poligoni, la rete dei
bot, il menu a 1080p. Programmi con `tools/qpuasm.py`, provati sul PC con l'emulatore.
Riferimenti esterni: GPU_FFT, QPULib, py-videocore, VC4CL. Sulla RGB30 l'ARM finché non c'è
il driver Mali (M41).
- **Fatto quando:** sul Pi le particelle di Yharnam e il suo buio a livelli li fa la GPU, con
  le misure del 3D Bench e di Yharnam prima e dopo nel report, e senza GPU (QEMU, `gpu3d=0`)
  tutto va come oggi sull'ARM.

## M45 — bm Write: i documenti, come Word e Pages (M) — in corso
Richiesta dell'utente (2026-10-06): app del Market (`carts/write`, scheda Games), formato
`.BMD` più esportazioni (`.TXT`, `.MD`, `.HTM`, `.PDF`), stili e impaginazione A4,
completamento (`predict`) e scrittura col pad (`padtype`); `/docs` comune alle app con il
permesso `docs`. Fatto sul PC: API `doc_*`, l'app, il formato e le esportazioni (prove `make
test-write`, QEMU `test_bm_write`).
- **Da fare:** provarla sul Pi (tastiera e pad) e aprire il PDF sul PC; dopo l'unione a
  `bm-core` pubblicarla nel Market (`market/about.txt`).
- Poi, se servono: immagini nel testo, tabelle, ricerca e conteggi, più font.
- **Fatto quando:** sul Pi si scrive una lettera con titolo, stili e una lista, si salva, si
  esporta in PDF e il PDF aperto sul PC ha la stessa pagina dello schermo.

## M46 — Audio 2: suono hi-fi, strumenti e musica dall'assistente (L) — in corso
Richiesta dell'utente (2026-10-06, branch `claude/audio-synth`). Fatto sul PC: sintetizzatore
stereo (`src/audio/synth.c`; il suono di prima resta con `retro`, *Sound style*,
`sound=8bit`), 50 strumenti (`src/audio/presets.c`) e banco versione 2 (3 con i campioni) in
C, Lua e Python, bm Sound (FILTER, WAVE & SPACE, ECHO, ROOM, brano HIFI, *Samples...*),
l'assistente della musica (`src/ai/music.c`), riff (`docs/RIFF.md`), il jingle dello splash
nuovo; poi (branch `claude/audio-hifi`) profondità 16/24/32 bit col dither, campioni e kit
della console, gli effetti di superdough, `chorus()`, `audio_depth()`, `audio_samples()`, WAV a
24/32 bit, doc e kb (`docs/progress/audio.md`).
- **Da verificare sul Pi e sulla RGB30:** Dev → *Audio test*, bm Sound → brano HIFI, F6 →
  "ritmo rock", Settings → *Sound style* 8-bit e ritorno, *Bit depth* 16/24/32; bm Code →
  Ctrl+T, un riff, Ctrl+Invio (tempo stabile, parole accese), bm Sound → F7 e *Samples...*.
- **Fatto quando:** i giochi suonano puliti sulle due console, quelli vecchi possono restare
  8 bit, e il linguaggio di pattern suona da un gioco e da bm Code.

## M47 — Progetti e giochi: `.bme` modificabili, `.bm` in sola lettura (M) — in corso
Richiesta dell'utente (2026-10-06, branch `claude/bm-projects`). Decisioni: `.bme` per il
progetto; un `.bm` si legge ma non si cambia, se ne fanno copie; gli strumenti aprono un gioco
in sola lettura e al primo salvataggio chiedono la copia; *Make an editable copy* nelle
opzioni; i progetti in Dev, provabili; *Build .bm* dall'SDK e dal menu sostituisce il gioco
fatto prima dallo stesso progetto; Market e `mkbm.py` restano sui `.bm`. Fatto sul PC: formato
(`src/bm/project.c`), runtime (`write_target`, `copy_question`, `cart_build`), strumenti,
menu (prova QEMU `test_projects`).
- **Da fare sul Pi:** aprire un gioco in bm Code e salvare (la domanda), fare la copia dal
  menu, provarla, costruirne il gioco due volte (lo stesso `PONG1.BM`).
- **Fatto quando:** sul Pi nessuno strumento cambia un `.bm`, le copie si fanno dal menu e
  dagli strumenti e il build di un progetto finisce in Games.

## M48 — Il mouse nella bm Suite (M) — in corso
Richiesta dell'utente (2026-10-06, branch `claude/bm-projects`): tutta la suite di editor
usabile col mouse. Regola: il mouse fa quello che fanno i tasti. Fatto sul PC: `bmui`
(`src/script/bmui.lua`), tutti gli strumenti e il pannello dell'assistente, prove
(`bmui_host.lua`, controfigure col mouse, QEMU `test_editor_mouse`).
- **Da fare sul Pi:** provare ogni strumento con un mouse USB o Bluetooth; poi, se serve, il
  cursore a I nel testo e il trascinamento dei pannelli.
- **Fatto quando:** sul Pi ogni strumento della suite si usa col solo mouse per le cose che
  si indicano (scegliere, disegnare, girare la vista) e i tasti per il resto.

---

## Rischi principali
- **Prestazioni del Lua sull'ARM1176**: disegno in C, cache attive, risoluzione bassa; parti
  calde dei giochi in C (M39, passo 10).
- **Firmware closed-source che cambia comportamento**: versione fissata con `FW_REF`.
- **Prove solo sull'hardware, senza seriale**: QEMU raspi0 in CI, test sul PC, diagnostica a
  schermo e report nel branch `reports`.
- **Bluetooth senza emulatore**: tracce HCI registrate sul Pi per i test.
- **Scrittura su SD che corrompe la scheda**: test in QEMU con `fsck.vfat`, file di bm in una
  cartella dedicata; niente si scrive sul Pi prima che i file siano scaricati e controllati.
- **GPU (V3D, Mali) senza la scheda vera nei test**: prova passo per passo a schermo (test
  `g`, *GPU test* sulla RGB30), timeout su ogni attesa, emulatori sul PC
  (`tests/gpu/v3d_emu.c`, `make TARGET=rgb30 test-mali`), prove all'avvio che spengono ciò che
  non torna, rasterizzatore software come riserva.

## Hardware consigliato per lo sviluppo
- Adattatore USB-seriale 3.3 V (**non 5 V**) su GPIO14/15 + GND, per il debug a banco.
- Cavo mini-HDMI, alimentatore 5 V 2 A stabile.
- Un DualShock 4 (controller Bluetooth di riferimento) e un mouse USB o Bluetooth (M48).
- Filtro RC (270 Ω + 33 nF) e jack, solo per l'audio su PWM (R21).

---

## Spunti R1, R2, … (da riprendere)
Cose utili che a bm mancano. Nessuno è deciso: l'utente li richiama per nome ("facciamo
R7"), e allora si chiede il branch come per ogni sviluppo. Gli spunti diventati milestone o
già fatti sono tolti (R5 in M19; R10–R14, R18, R28–R30, hitbox, più giocatori fatti; R23 → M44, R24 →
M43): quello che ne resta da fare è sotto. Suggeriti per primi: R3, R1 con R2, R7 (sul Pi si
prova senza seriale e spesso senza tastiera).

### Usare la console senza PC né seriale
- **R1 — WiFi dal menu.** Settings > WiFi and network elenca le reti e la password si scrive
  con la tastiera USB; manca la scelta col pad e la password con la tastiera a schermo (R2).
- **R2 — Tastiera a schermo.** C'è come libreria Lua (`require "padtype"`); manca il servizio
  del kernel per gli schermi in C (password del WiFi, nomi dei file, Market), chiamabile anche
  dalle cartucce (es. `textinput(titolo, testo)`).
- **R3 — Log su SD.** Dev ha la pagina *Log* (quello stampato dall'avvio) e il report del log;
  sul Pi manca il file sulla SD (`bm/log.txt`, la RGB30 ha `bm/bootlog.txt`) con il traceback
  dell'ultimo errore di una cartuccia.
- **R4 — Screenshot sulla console.** Una combinazione di tasti salva un PNG in `bm/shots/`
  (segnalare problemi, copertine del Market). `src/bm/png.c` oggi legge soltanto: per
  scrivere basta il deflate senza compressione.
- **R6 — Pagina web della console.** Un piccolo server HTTP e il nome `bm.local` (mDNS): dal
  browser del telefono si carica un `.bm`, si scaricano salvataggi e screenshot, si modifica
  `bm/config.txt`, senza `bm_net.py`.

### API dei giochi
- **R7 — Lettere accentate in `print()`.** Il testo è disegnato byte per byte in CP437
  (`g16_text` in `gfx16.c`): "città" in UTF-8 esce sbagliato. Conversione da UTF-8 a CP437;
  poi, se serve, font dallo sheet.
- **R8 — Vibrazione e luce del DS4.** `rumble(p, forte, debole, ms)` e `padlight(p, colore)`:
  il report d'uscita del DS4 (0x11, `bt.c`) parte già, oggi solo per il colore del giocatore.
- **R9 — Suoni campionati (PCM/WAV).** Nel banco ci sono (versione 3, M46; WAV da
  `scripts/bmaudio.py`); resta l'import di un WAV dalla SD in bm Sound.
- **Resti di R11 (mappe).** Hunter's Night dai numeri 32–63 ai flag delle tile; import delle
  mappe di Tiled (`.tmj`) con livelli e proprietà; più nomi per una voce dell'assistente.
- **Resti delle hitbox (BOXES).** bm Animator (Sprites) e bm Pixel che disegnano i
  riquadri di BOXES sui fotogrammi; i riquadri dalle ossa quando un modello diventa sprite.
- **Resti di più giocatori e bmnet.** Provare sul Pi due console in rete con il modello *Online
  2D* (anche col relay) e due pad con *Versus 2D*; Overbit sopra bmnet (oggi ha la sua copia
  del protocollo).
- **R27 — La batteria nei giochi** (2026-10-10). Con `battery()` / `battery_low()` (RGB30) i
  giochi lunghi (Yharnam, Overbit in solitario) potrebbero salvare da soli a batteria
  scarica, e bmlib offrire `lib.autosave` al primo `battery_low()`; l'assistente potrebbe
  suggerirlo (kb `battery`). In rete resta solo informazione (lockstep).

### Strumenti di sviluppo
- **R15 — Ricarica dal PC.** `bm_net.py --watch`: a ogni salvataggio di `main.lua` sul PC la
  cartuccia torna sulla console e riparte.
- **R16 — Modelli di gioco in bm Code.** L'SDK li ha già (Ctrl+N); "New game" di bm Code
  parte ancora da uno scheletro vuoto (`TEMPLATE`).
- **R17 — Import MIDI in bm Sound.** Un file MIDI diventa i pattern del banco (dentro R25).
- **R25 — Suoni da e verso il PC.** In bm Sound WAV (campioni brevi) e MIDI importati ed
  esportati, l'uscita stereo e un editor delle forme d'onda.
- **R26 — Il mouse vero in nano8.** Le cartucce `.p8` che chiedono il mouse (`poke(0x5f2d,
  1)`) oggi hanno un cursore mosso da levetta, croce o frecce: con `mouse(true)` il mouse USB
  o Bluetooth, i tasti e la rotella (`stat(32)`–`stat(36)`).
- **R36 — Yharnam: gli oggetti dalla mappa disegnata** (2026-10-10). Oggi a mano si disegna solo
  il terreno (`map_ground.csv`, `map_overlay.csv`); case, alberi, lampioni, collisioni, porte,
  lampade del cacciatore e boss li fa ancora il codice dal piano delle strade (una casa
  ridipinta come strada resta una casa). Un livello in più di segnaposti (tile "casa", "albero",
  "lampione"...) letto da `gen()`, o le case come tile di un livello davanti, li renderebbe
  disegnabili; con `MAP.areas` e i cancelli come dati, anche la forma della caccia.
- **R37 — Panoramica e tavolozza delle mappe grandi nell'SDK.** Sulla pagina mappa una vista
  rimpicciolita (256×256 celle in uno schermo) per spostarsi, e con Tab le sole tile 16×16 già
  usate nella mappa (lo sheet di Yharnam è largo 4096 px: le sue tile sono le prime due righe).
- **R38 — L'assistente e il pennello 16×16.** Una voce della kb ("come disegno una mappa grande
  con tile 16x16?", `z` sulla pagina mappa, `mkmap.py` come esempio) e la guida del platform che
  lo cita.

### Audio
- **R31 — Import di un suono SAMPLE col suo campione.** Oggi un suono SAMPLE preso da un altro
  banco non porta il campione (`MOD1` punta nel banco corrente): copiarlo e rinumerarlo.
- **R32 — Editor e registrazione di campioni.** In bm Sound tagliare, normalizzare, mettere i
  loop di un campione; registrare dal microfono USB o dal mix (`audio_samples()` già dà il meter).
- **R33 — Campioni nel Market.** Banchi di campioni (`.bmau` versione 3) da scaricare e
  mettere in un gioco, con licenza come le app.
- **R34 — Kit di più campioni in riff.** `s "mykit:3"` su un banco di campioni proprio, come
  `kit:N`, e più kit per cartuccia (`sound_of` in `riff.lua`).
- **R35 — L'assistente musicale e gli effetti nuovi.** `ai.music` / Predict / kb che scelgono i
  preset nuovi (`kit`, `choir`, `lush`, `bitbass`...) e `crush`, `vowel`, `duck` dalle parole
  ("lofi", "coro", "sidechain").

### Hardware
- **R19 — Altri controller Bluetooth.** Oggi via Bluetooth solo il DS4 (più tastiere e
  mouse): DualSense, Switch Pro, 8BitDo, i pad Xbox (Bluetooth LE, come `ble.c`).
- **R20 — Telecomando della TV (HDMI-CEC).** Frecce e OK per muoversi nel menu senza pad.
- **R21 — Audio senza HDMI.** PWM su GPIO con il filtro RC o un DAC I2S, per i monitor senza
  altoparlanti.
- **R22 — Pulsanti su GPIO e schermo piccolo.** Il Pi Zero dentro un guscio portatile (pad
  sui GPIO, LCD DPI o SPI): una strada diversa dalla RGB30.
