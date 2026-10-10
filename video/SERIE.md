# Skyvale World — la serie di video

Un platform 2D a scorrimento, originale, nello stile dei giochi a 16 bit dei primi anni '90,
fatto **solo con gli strumenti della console bm** (2D: niente bm Mesh, Animator 3D,
`picture3d`, GPU). Lo scopo dei video è mostrare cosa sa fare bm, gli editor uno per uno,
e alla fine il gioco giocabile in una `.bm`. Eroe: **Kip**, una volpe (personaggio
originale: nessun personaggio, sprite o musica di giochi esistenti).

Ogni episodio: ~3 minuti, un editor protagonista, tasti sempre in sovrimpressione
(pannello a destra: dove siamo, tasto premuto, tasti della scena), sottotitoli del
narratore, gancio col risultato in apertura. Skill: `claude/skills/bm-video-tutorial`.
Libreria: `video/lib/bmvideo.py`. Un episodio = `video/NN-nome/` (`record.py`, `art.py`,
`storyboard.md`, `copione.md`); i file generati vanno in `out/` (non nel repo).

| # | Strumento | Cosa si fa | Stato |
|---|-----------|------------|-------|
| 1 | bm Pixel | Kip di profilo: ciclo di corsa a 6 fotogrammi, salto, moneta; matita, riempimento, annulla, animazione, ovali e linee, specchio, tavolozza, salva `skyvale.bm` | fatto |
| 2 | SDK (2D) | tessere 8×8 nello sheet, flag (solido, piattaforma, scala, acqua, fa male), mappa a layer, il primo livello; codice di cinque righe e prova (F5) | fatto |
| 3 | Sound | suoni (salto con bend, moneta, rimbalzo), tre effetti con il piano, musica del livello scritta con riff (F7) e messa nel banco come canzone, pagine pattern e song | fatto |
| 4 | bm Code | Kip corre e salta: `lib.tiles`, `lib.step`, salto a pressione variabile, spuntoni (flag 4), suoni e musica dell'episodio 3; la partita giocata da uno script di tasti | fatto |
| 5 | bm Code + assistente | nemici, monete, blocchi, HUD; domande all'assistente | da fare |
| 6 | SDK + bm Pixel | zone con nome e scatole di collisione (SPRITES/BOXES), layer a parallasse, bandiera di fine livello, più livelli | da fare |
| 7 | il gioco | Skyvale World giocato dall'inizio alla bandiera | da fare |

Note per chi continua:

- Registrazione sul PC con `bmhost` (QEMU non c'è nelle sessioni cloud): non ha il menu del
  kernel né l'assistente AI; per quelle parti serve QEMU o la console vera.
- Lo sheet di Kip è creato in bm Pixel e salvato come `.bm`; gli episodi 2–6 riprendono
  quella cartuccia (`out/sd/carts/`) e ci aggiungono mappa, suoni e codice. Per ripetere gli
  episodi in ordine, `record.py` di ogni puntata deve ripartire dal file lasciato dalla
  puntata prima (da tenere nel repo quando sarà pronto: `carts/skyvale/`).
- Non si usano personaggi, sprite o musiche di giochi esistenti.
- **bm Studio e bm Animator della console sono 3D**: non si usano. Le tessere e la mappa 2D stanno nell'SDK (pagina F3, di nuovo F3 la mappa).
- Gli strumenti si registrano con `bmhost --tool` (come gli strumenti incorporati nel kernel: possono salvare su un `.bm` già presente); F5 dentro `bmhost` chiude la registrazione, quindi la prova del gioco è una seconda registrazione accodata.
- Ogni episodio riparte dal `.bm` lasciato dal precedente: l'episodio 2 parte da `video/02-sdk/start.bm` (la cartuccia salvata nell'episodio 1).
- Audio: `bmhost --wav` e `encode(..., audio=wav)` mettono il suono nel video; le schede e il gancio hanno una traccia muta. Verifica: `video/03-sound/verify.py` legge il banco salvato (`scripts/bmaudio.py`) e controlla che il wav non sia silenzio.
- Tasti: F6–F10 e Ctrl+Invio arrivano a `bmhost` solo come sequenze ESC (`\x1b[18~` è F7, `\x1b[28~` Ctrl+Invio): i byte grezzi 0xE5–0xEF vengono scartati.
- bm Code (e l'assistente F6, episodio 5) hanno bisogno della tabella `ai`: si registrano con `build/host/bmhost-ai` (`make bmhost-ai`: collega `src/ai/lua_ai.c` e carica `build/assist.bin`); `bmhost` normale non ce l'ha.
- La cartuccia di ogni episodio parte da quella lasciata dal precedente (`video/NN-nome/start.bm`); il codice nuovo si scrive **senza indentazione**: bm Code rientra da solo con Invio e `end`.
