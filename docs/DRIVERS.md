# I driver 3D di bm: le versioni

Il 3D di bm passa da due driver: **r3d** (`src/bm/r3d.c`: scena, trasformazioni, luce,
il rasterizzatore dell'ARM) e **gpu3d** (`src/gpu/gpu3d.c` con gli shader di
`tools/qpuasm.py`: il backend della V3D). Hanno una versione sola, **bm3d X.Y**: X è il
blocco di sviluppo (una milestone), Y il passo dentro il blocco. La versione è in
`src/gpu/version3d.h` (`BM3D_VERSION`, la tabella `bm3d_versions()`).

Dove si vede: la riga di stato della GPU (test `g`, log), *Impostazioni > System > 3D
driver*, il log all'avvio di un gioco ("the 3D is drawn by the GPU as bm3d 3.0"), lo
stress test, il 3D Bench, il benchmark di Overbit e il quarto valore di `gpu3d()`.

## Le versioni

- **0.1** (fino a settembre 2026, M7–M32): solo ARM, il primo rasterizzatore di r3d (lo
  stress test di settembre: 31 sfere a 60 fps).
- **0.2** (2026-10-01, M33): solo ARM, bordi in virgola fissa, cicli delle texture
  specializzati, z azzerato con il DMA (69 sfere a 60 fps sul Pi).
- **1.0** (2026-10-01, M33): la GPU disegna i triangoli (shader NV), l'ARM li mette e li
  illumina; lo z conservato tra un lavoro e l'altro.
- **2.0** (2026-10-01, M34): pagine pulite senza load, texture in T-format, MSAA 4×, meno
  istruzioni dell'ARM per triangolo (182 sfere a 60 fps sul Pi).
- **2.1** (2026-10-03, M34): Overbit sulla GPU: facce a retino, luce RGB precalcolata e
  nebbia sugli angoli, ombre, effetti 3D.
- **3.0** (2026-10-03, M36): il vertex shader della GPU mette lo scenario (modelli spenti
  o con la luce agli angoli); la GPU taglia sul piano vicino e sulla guard band.
- **3.1** (2026-10-03, M36): il vertex shader anche per i modelli illuminati dal sole, con
  le ossa (gli eroi).
- **3.2** (2026-10-03, M36): il vertex shader anche per le ombre e il primo piano.
- **3.3** (2026-10-03, M36): i modelli con texture illuminati dal sole (gli eroi di Meshy):
  la luce di ogni angolo (Gouraud, grigia) anche sulle texture, sull'ARM e sulla GPU; il
  vertex shader `vs_lit_tex` li mette con le ossa.
- **3.4** (2026-10-03, M36): le pelli sul vertex shader: le facce a cavallo di due ossa (i
  modelli Meshy sono una superficie unica) in gruppi per coppia di ossa, ogni angolo messo
  dalla matrice del suo osso come fa r3d (`vs_lit_tex2`, `cs_colour2`, le ombre
  `vs_shadow2`/`cs_shadow2`).
- **4.0** (2026-10-03, M35): il fotogramma in coda: alla fine del fotogramma il lavoro della
  GPU parte e non si aspetta (il binning incrementa un semaforo che il rendering aspetta,
  come il driver vc4 di Linux: i due thread partono insieme); intanto gira il `_update`
  del fotogramma dopo, e si aspetta la GPU solo prima di toccare la pagina o la memoria
  che il lavoro legge. Una prova all'avvio (`probe_queue`) la spegne se la V3D non finisce
  il lavoro come deve. Opzione `gpu3d_queue`.
- **4.1** (2026-10-03, M35): un solo lavoro della GPU a fotogramma anche con l'HUD e le
  braccia in prima persona. Il 2D disegnato dopo il 3D mentre la GPU disegna (l'HUD) non
  la aspetta più: il lavoro parte e il 2D si registra (`d2` in `runtime.c`: rettangoli,
  testo, sprite, mappa, prompt...), poi va sulla pagina nello stesso ordine quando la GPU ha
  finito (prima di altro 3D, di leggere la pagina, di mostrarla). Lo `zclear()` tra il
  mondo e le braccia (`R3D_FRONT`) resta nello stesso lavoro: un quadrato su tutta la
  pagina con `fs_zclear` scrive la profondità più lontana e rimette a ogni pixel il suo
  colore (il segnale *colour load* legge il tile buffer, come fa Mesa per il blending);
  la prova all'avvio `probe_zclear` lo spegne se la V3D non lo fa. Il 2D che il `_update`
  disegna mentre la GPU lavora va sulla pagina del fotogramma dopo, come senza la coda; un
  `_update` che disegna 3D o legge la pagina torna a girare dopo il fotogramma (una riga
  nel log). Tutto con `gpu3d_queue=1`.
- **4.2** (2026-10-04, M38): schermi fino a 1920×1080 (`screen()`, la risoluzione cambiata
  dalla cartuccia tra un fotogramma e l'altro): stato dei tile, lista di rendering e
  profondità tra i lavori dimensionati per 1080p anche con l'MSAA; la guard band degli
  angoli in 12.4 (±2048 pixel) si stringe sugli schermi larghi (120 pixel a 1920, prima
  1000 fissi), i triangoli che la passano li taglia l'ARM. Un `cls()` con la GPU non lo
  disegna più l'ARM (4 MB a 1080p): il lavoro della GPU pulisce i suoi tile a quel colore;
  l'ARM riempie la pagina solo se prima del 3D arriva del 2D, una lettura o la pagina va
  mostrata senza 3D. Vale per tutte le modalità della GPU.
- **4.3** (2026-10-05, M36): il vertex shader che sul Pi non disegnava niente (prova
  all'avvio `0000 0000 0000 0000`). Nel record GL la dimensione degli attributi e la loro
  posizione nella VPM sono in **byte**, come le scrive Mesa ("byte offsets for the start of
  the vertex attributes 0-7, and the total size", `vc4_context.h`); 3.0–4.2 le scrivevano in
  parole da 32 bit, e anche l'emulatore le leggeva così. Ora l'emulatore le legge in byte e
  controlla, come il simulatore di Broadcom, che lo shader legga ogni parola caricata; la
  prova all'avvio prova i byte e poi le parole (un lavoro che non finisce esclude solo quella
  strada) e dice quale ha disegnato. Quello che le prove facoltative vedono finisce nel
  report del test `g` e del 3D Bench (`gpu3d_probe_log()`).
- **4.4** (2026-10-05, M35): lo `zclear()` nel lavoro, che sul Pi lasciava il colore ma
  perdeva il 3D dopo (`07e0 07e0`). Lo z anticipato (*early z*) della V3D tiene una sua idea
  della profondità, scritta solo dalle primitive con *early z updates*: il quadrato di
  `fs_zclear` riporta la profondità lontana dallo shader, l'early z non lo sa e scarta quello
  che viene dopo. Dopo uno `zclear()` nel lavoro le primitive vanno senza early z (la prova
  dello z resta, nel tile buffer, in ordine); l'emulatore ora fa l'early z così e ripete
  esattamente i pixel del Pi. Niente early z neanche con l'MSAA (Mesa, HW-2905: dopo un load
  il tracciamento dell'early z può tenere i valori del tile prima). Il quadrato scrive una
  profondità appena sotto 1 (0xFFFFF0). La coda anche senza vertex shader: profilo GPU+Q
  del 3D Bench, renderer GPU+Q di Overbit.
- **4.5** (2026-10-05, M35): la memoria dei lavori (liste, record, uniform, vertici) senza
  cache: l'ARM la scrive una volta e non la rilegge, e con la cache *write-allocate* ogni
  riga scritta veniva prima letta dalla memoria e spingeva fuori le righe del Lua e di r3d.
  Senza cache le scritture escono unite dal write buffer. Il blocco è allocato a sezioni
  intere da 1 MiB che l'MMU rimappa (`mmu_set_cached`, `v3d_uncached`). Opzione
  `gpu3d_wc=1` (*Settings > Screen and sound > 3D job memory*), spenta finché il Pi non
  mostra che conviene: il 3D Bench la confronta con il profilo GPU+WC.
- **4.6** (2026-10-05, M35): fino a 8 texture in un lavoro (prima 2: con la terza il lavoro
  si chiudeva, e il test `texswap` del 3D Bench andava più piano sulla GPU che sull'ARM);
  le copie delle texture si rimpiazzano dalla meno usata, mai una che il lavoro aperto
  legge se ce n'è un'altra.
- **4.7** (2026-10-05, M34): le facce con texture *e* retino sulla GPU (`fs_tex_lit_screen`,
  `fs_tex_rgb_screen`: il texel sui pixel con x + y pari, dove non è trasparente), anche
  nelle mesh del vertex shader: era l'ultimo caso che passava il fotogramma all'ARM
  (`r3d_t.arm_hook`, il backend ora dichiara `tex_screen`). Test `quad_texscreen` del 3D
  Bench, scena `tex screen` di `make test-gpu3d`.
- **4.8** (2026-10-05, M37): il **2D sopra il 3D nel lavoro della GPU** (`gpu3d_2d=1`,
  *Settings > Screen and sound > 2D over the 3D*): rettangoli, `rect`, `pset`, righe
  orizzontali e verticali, sprite (`spr`, `sspr`, anche girati e ingranditi di un numero
  intero), le celle di `map()` e il testo di `print` (glifi da una texture del font,
  `fs_text`) diventano quad nello stesso lavoro, senza prova né scrittura dello z: il 3D
  dopo li copre dove è più vicino del 3D di prima, come sull'ARM. Pixel per pixel come
  gfx16 (il texel al centro del pixel, il colore RGB565 che torna uguale dal tile buffer;
  `make test-gpu3d` scena `2D on GPU`, `make test-queue2d` con l'opzione: gli stessi
  fotogrammi). Quello che la GPU non fa uguale (cerchi, righe oblique, triangoli,
  `prompt`, zoom non interi) chiude il lavoro e lo disegna l'ARM come prima. Un HUD tra il
  3D e il 3D non chiude più il lavoro: profilo GPU+2D del 3D Bench (`split`, `match`,
  `gpu2d`), passo 16 del test `g` (lo stesso HUD dall'ARM e dalla GPU, i pixel uguali).
  Poi le **texture filtrate** (bilineare, `gpu3d_filter=1`, *3D textures*): cambia
  l'aspetto rispetto all'ARM (e sui bordi delle zone di uno sheet il colore della zona
  accanto); il 2D resta al texel più vicino. Test `bilinear` del 3D Bench.
- **4.9** (2026-10-05, M37): il 3D dell'**ARM** con una matrice sola oggetto→camera e la
  luce nello spazio del modello (sole, alto, V e H girati una volta per modello senza
  ossa, le normali non girate): `r3d_fast=1`, *3D on the ARM: Fast*; anche sulla RGB30.
  I pixel non sono più identici al bit (in `bench3d` cambiano solo le scene con la
  mappa); `count_insns`: −2,2% sfere, −3,6% Gouraud, −2,6% stanza nel lavoro per
  triangolo dell'ARM. Spento, il percorso è quello di prima (stessi checksum).
- **5.0** (2026-10-05, M39): **mesh più grandi e indicizzate**. Un modello, una `mesh()` e
  r3d arrivano a 65535 vertici e 65535 facce (prima 4096 e 16384: un modello da 10 000
  triangoli andava spezzato; `bm.h`, `mkbm`/`bmmesh.py`, bm Studio, bm Mesh). Sulla GPU i
  gruppi di una mesh tengono una volta sola gli angoli uguali parola per parola e li
  disegnano con indici a 16 bit (`INDEXED_PRIMITIVE_LIST`): la V3D mette una volta un
  angolo che sta su più facce (le figure lisce, le mappe con la luce per vertice). Una
  prova all'avvio (`probe_index`, un quadrato di due triangoli con due angoli in comune)
  lo spegne se non torna (`indexed no` nella riga di stato). Test `big` e `big_logic` del
  3D Bench: modelli da 10 080 triangoli su una mappa da 14 112.
- **5.1** (2026-10-05, M39): **due lavori in volo** (`gpu3d_queue=2`, *3D frame queue: On, 2
  jobs*): la memoria di un lavoro in due blocchi, il lavoro dopo si riempie nell'altro
  mentre la GPU disegna quello di prima (una volta sola si aspetta, quando parte). La
  profondità tra i lavori, gli shader e la prova restano nel primo blocco. L'ARM aspetta la
  GPU prima di toccare una pagina solo se è quella del lavoro in volo (`gpu3d_sync_page`).
  L'emulatore ora esegue un lavoro avviato quando lo si aspetta, come la V3D: memoria
  riusata sotto un lavoro o una pagina toccata prima di aspettarlo darebbero un'immagine
  sbagliata. Profilo GPU+VS+Q2 del 3D Bench (`queue`, `split`).
- **5.2** (2026-10-05, M39): **texture a 16 bit**: uno sheet tutto opaco va alla GPU come
  RGB565 in T-format (`gpu3d_tex16=1`, *3D textures: ..., 16-bit*): metà memoria e metà
  letture della TMU, gli stessi colori. Il layout (tile da 4 KiB di 64×32 texel) lo impara
  la prova all'avvio come per le texture a 32 bit: una texture 128×64 dove la parola *i*
  vale *i* e torna sulla pagina come sé stessa (o con rosso e blu scambiati). Gli sheet con
  pixel trasparenti restano a 32 bit. Profilo GPU+T16 del 3D Bench.
- **5.3** (2026-10-05, M39): **shader dei pixel a due thread** (`gpu3d_fs2=1`, *3D pixel
  shaders: Two threads*): le facce con texture opache (`fs_tex_lit`, `fs_tex_rgb`) hanno una
  versione `_t` che chiede il texel, passa la QPU all'altro thread (`lthrsw`) e lo legge al
  ritorno: mentre la TMU legge, la QPU colora i pixel dell'altro thread invece di aspettare.
  `qpuasm.py` controlla le regole di Mesa (`vc4_qpu_validate`: nessun cambio col
  scoreboard preso, accumulatori persi dopo il cambio, texel letti dopo un cambio). La
  prova all'avvio (`probe_fs2`) disegna gli stessi quadrati con i due shader e li tiene
  solo se i pixel sono identici (`two-thread shaders yes` nella riga di stato); un lavoro
  che non finisce li spegne e il resto va avanti. Profilo GPU+FS2 del 3D Bench
  (`spheres_tex`, `heroes_tex`, `quad_tex`, `match`).
- **5.4** (2026-10-05, M39): **ordine di disegno** (`gpu3d_sort=1`, *3D draw order: Nearest
  first*): i disegni delle mesh del vertex shader che provano e scrivono lo z si mettono da
  parte (ognuno con tutto il suo stato) e vanno nel lavoro dal più vicino al più lontano
  (la profondità dell'origine dell'osso) al primo disegno di un altro tipo (triangoli di
  r3d, ombre, 2D, `zclear()`) o alla fine del lavoro: lo z anticipato della V3D scarta i
  pixel nascosti invece di colorarli. Stessa immagine (tranne dove due facce hanno la
  stessa profondità; i gruppi di una mesh restano nel loro ordine). Nell'emulatore otto
  scatole dal lontano al vicino: 99 960 pixel colorati invece di 416 546, 0 pixel diversi.
  Profilo GPU+VS+S del 3D Bench (`spheres`, `heroes`, `match`, `big`, `big_logic`).
- **5.5** (2026-10-05, M39): **cosa si vede, anche per gli attori**: `visible3d(x, y, z, r)`
  dice se una sfera può stare sullo schermo (la prova di r3d sui modelli, `r3d_visible`) e,
  con la visibilità precalcolata della mappa data da `pvs3d{...}` (celle sul terreno, i
  pezzi visti da ognuna, le scatole dei pezzi), se la cella della camera vede un pezzo su
  cui sta. Overbit la dà alla partenza della mappa e non disegna gli eroi dietro i muri
  (prima solo quelli fuori dall'inquadratura); le ombre restano (al tramonto escono dai
  muri). Vale anche sull'ARM e sulla RGB30. Prove in `make test-gameapi`.
- **5.6** (2026-10-05, M39): con la stessa opzione (*3D draw order: Nearest first*) i triangoli di
  ogni gruppo di una mesh indicizzata vanno nell'ordine che riusa di più la cache degli angoli
  già colorati della V3D (l'ottimizzazione lineare di Tom Forsyth per una cache di 32), e gli
  angoli si rinumerano nell'ordine del primo uso (la VCD li legge di fila). Nell'emulatore, che
  ora ha un modello della cache (16, FIFO): una griglia di 1800 triangoli in ordine sparso passa
  da 2,96 a 0,69 angoli colorati per triangolo, stessi pixel. Cambiare l'opzione rifà le copie
  delle mesh.
- **6.0** (2026-10-05, M41): **la GPU Mali-G52 della RGB30**, primo passo (`src/rgb30/mali.c`,
  *Dev > GPU test*): vdd_gpu, orologi e dominio di alimentazione PD_GPU, identità, reset,
  accensione dei core, MMU (spazio 0, tabelle Mali LPAE come panfrost sull'RK3568) e lavori del job
  manager (WRITE_VALUE e una catena di due). Non disegna ancora: il 3D della RGB30 resta dell'ARM
  e il 3D Bench lo dice nella riga della macchina. Prove sul PC con una GPU simulata
  (`make TARGET=rgb30 test-mali`).
- **6.1** (2026-10-05, M41): **la Mali scrive pixel**: un lavoro di frammenti sullo slot 0 senza
  disegni (niente tiler, niente shader) pulisce le tessere di 16×16 e le scrive attraverso il
  render target: il descrittore del framebuffer di Bifrost (v7) com'è in Mesa (parametri,
  posizioni dei campioni, un render target R8G8B8A8 scritto lineare con i canali girati per
  l'XRGB8888 dello schermo, i pixel "puliti" scritti). Prima una superficie di 64×64 controllata
  pixel per pixel, poi un quadrato verde in alto a destra dello schermo della pagina *GPU test*,
  ridisegnato per ultimo sopra il testo: se si vede, la GPU ha disegnato.
- **6.2** (2026-10-05, M39): sul Pi il 3D Bench si è fermato nel test `match` con GPU+FS2 (la
  GPU non ha finito il lavoro): gli shader che non cambiano thread erano detti "a più thread"
  nel record (bm3d 2.0–5.3, innocuo finché nessuno cambiava thread), mentre uno shader a più
  thread deve fare LTHRSW una volta prima di finire (Mesa, `vc4_program.c`). Ora il bit 0 del
  record ("single-threaded") è acceso per tutti tranne `fs_tex_lit_t` e `fs_tex_rgb_t`, come in
  Mesa; l'emulatore rifiuta un record che dica a più thread uno shader senza cambi, e la prova
  all'avvio degli shader a due thread mette nello stesso lavoro anche shader a un thread. Sul
  Pi GPU+FS2 aveva dato +22% in `quad_tex` (148 quad contro 121).
- **6.3** (2026-10-05, M39): i report del Pi hanno mostrato che la 6.2 costa: i test di
  riempimento senza texture hanno perso l'8–14% (`quad_smooth` 198 → 171, `quad_screen`
  184 → 160), e Overbit a 1920×1080 andava più piano che con v0.2.3 (3D 22,0 → 29,9 ms; con
  l'MSAA 22,9 → 42,4). Due cose: con **GPU+FS2** ora hanno due thread anche gli altri shader del
  3D (`fs_colour_t`, `fs_colour_screen_t`, `fs_tex_lit_alpha_t`, `fs_tex_lit_screen_t`,
  `fs_tex_rgb_alpha_t`, `fs_tex_rgb_screen_t`: il colore nel register file, poi `lthrsw` prima
  dello scoreboard), controllati da `qpuasm.py`, che li fa girare pixel per pixel accanto agli
  originali (stesse scritture di colore e profondità, qualunque cosa l'altro thread lasci) e
  dalla prova all'avvio, che li mette tutti in un lavoro; e con l'**MSAA** l'early z resta nei
  lavori che puliscono la pagina (HW-2905 riguarda solo un lavoro che la carica: la 6.2 lo
  toglieva sempre, v0.2.3 mai). Il resto è come la 6.2: senza FS2 gli shader restano a un
  thread.

## Le modalità: versioni vecchie sul codice di oggi

Le impostazioni riproducono le versioni precedenti, così si confrontano sullo stesso Pi:

- 3D sull'ARM (`gpu3d=0`): **0.2**;
- GPU senza vertex shader (`gpu3d_vs=0`): **2.1** (con `gpu3d_aa=1` anche l'MSAA);
- GPU con il vertex shader per lo scenario (`gpu3d_vs=1`): **3.0**;
- GPU con il vertex shader per tutto (`gpu3d_vs=2`): **3.4**;
- con il fotogramma in coda (`gpu3d_queue=1`): **4.1** (con o senza vertex shader);
- con la memoria dei lavori senza cache (`gpu3d_wc=1`): **4.5**;
- con il 2D sopra il 3D nel lavoro (`gpu3d_2d=1`): **4.8**;
- con due lavori in volo (`gpu3d_queue=2`): **5.1**;
- con le texture a 16 bit (`gpu3d_tex16=1`): **5.2**;
- con gli shader dei pixel a due thread (`gpu3d_fs2=1`): **5.3**;
- con le mesh dalla più vicina e i triangoli nell'ordine della cache (`gpu3d_sort=1`, con il
  vertex shader): **5.4** e **5.6**.

Quello che 4.2, 4.3, 4.4 e 4.6 hanno aggiunto (schermi fino a 1080p, `cls()` della GPU, il
record GL in byte, niente early z dopo uno `zclear()` nel lavoro, 8 texture in un lavoro)
vale in tutte le modalità della GPU: non si spegne.

0.1 e 1.0 non girano più: i loro numeri sono quelli misurati sul Pi allora
(`docs/M33-PRIMA-DOPO.md`).

## La regola

Ogni blocco nuovo (una milestone del 3D) alza X, ogni passo che cambia quello che il
driver sa fare alza Y: si cambia `BM3D_VERSION`, si aggiunge una riga alla tabella di
`version3d.h` e a questa pagina, e se il passo si può spegnere, una modalità.
