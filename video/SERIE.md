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
| 2 | bm Studio | tessere (terra, nuvole, tubi, acqua), flag (solido, piattaforma, scala, acqua, fa male), il primo livello | da fare |
| 3 | Sound | effetti (salto, moneta, rimbalzo) e musica del livello | da fare |
| 4 | bm Code | movimento, salto a pressione variabile, collisioni con la mappa (`bmlib`) | da fare |
| 5 | bm Code + assistente | nemici, monete, blocchi, HUD; domande all'assistente | da fare |
| 6 | bm Studio | layer a scorrimento parallasse, scatole di collisione, bandiera, mappa dei livelli | da fare |
| 7 | il gioco | Skyvale World giocato dall'inizio alla bandiera | da fare |

Note per chi continua:

- Registrazione sul PC con `bmhost` (QEMU non c'è nelle sessioni cloud): non ha il menu del
  kernel né l'assistente AI; per quelle parti serve QEMU o la console vera.
- Lo sheet di Kip è creato in bm Pixel e salvato come `.bm`; gli episodi 2–6 riprendono
  quella cartuccia (`out/sd/carts/`) e ci aggiungono mappa, suoni e codice. Per ripetere gli
  episodi in ordine, `record.py` di ogni puntata deve ripartire dal file lasciato dalla
  puntata prima (da tenere nel repo quando sarà pronto: `carts/skyvale/`).
- Non si usano personaggi, sprite o musiche di giochi esistenti.
