---
name: bm-video-tutorial
description: Genera video tutorial YouTube (storyboard, copione, registrazione in QEMU, montaggio) che mostrano come usare gli strumenti della console bm (SDK, bm Code, Pixel, Sound, Mesh, Studio, Animator, assistente AI) per creare giochi e app .bm e .b16. Usa questa skill ogni volta che l'utente parla di video, tutorial, YouTube, showreel, registrazione di QEMU, screenshot della console o di "mostrare come si usa" un editor di bm, anche senza nominare la skill. Mostra sempre quali tab, tasti e pulsanti vengono premuti.
---

# bm: video tutorial con QEMU

Questa skill produce video YouTube che insegnano a usare la piattaforma bm
(console bare-metal su Raspberry Pi Zero W) registrando la console reale in QEMU,
non mockup. Ogni video mostra **cosa succede sullo schermo e quale tasto, tab o
pulsante lo provoca**.

## Regole fisse

1. **Solo editor dentro la console** (tab Dev): SDK, bm Code, bm Pixel, Sound editor,
   bm Mesh, bm Studio e bm Animator *della console*, assistente AI.
2. **Un video = uno strumento principale**, circa 3 minuti (confermare con l'utente
   la durata se non è già stata fissata). Si può passare da un editor all'altro
   e tornare, nell'ordine in cui serve.
3. **Niente dati inventati.** Ogni nome di tasto, tab, menu e pulsante va verificato
   nella repo (vedi "Fonti da leggere"). Se un dato non si trova, scrivi nel
   storyboard `[DA VERIFICARE: ...]` e chiedi all'utente: non indovinare.
4. **Formati**: `.bm` è la cartuccia (codice Lua, sprite sheet, tile map, modelli 3D,
   animazioni, banco suoni, copertina). Il formato `.b16` va descritto solo dopo
   aver trovato in repo cosa è: se non è documentato, `[DA VERIFICARE: .b16]`.
5. **Lingua del video**: inglese, salvo diversa richiesta. Parte della documentazione
   della repo è in italiano, quindi i nomi dei tasti vanno copiati da lì, se è disponibile usa l'inglese.
6. **Codice sempre come testo** (nel copione e nelle schede), mai solo come immagine.
7. L'assistente AI della console ha base di conoscenza in italiano: se lo mostri,
   usa domande in italiano, se la versione inglese è disponibile usa l'inglese.

## Fonti da leggere (in quest'ordine, prima di scrivere qualsiasi cosa)

Branch `bm-core`. Leggi con gli strumenti a disposizione; non scrivere da memoria.

- `CLAUDE.md` e `AI.md`: regole del progetto e convenzioni per gli agenti.
- `README.md`: panoramica, strumenti sulla console, tab Dev.
- `docs/GAME-GUIDE.md` e `docs/API.md`: come si scrive un gioco, API Lua.
- `docs/PREDICT.md`: autocompletamento in bm Code (Tab accetta il suggerimento).
- `Makefile`: target che registrano video o schermate (es. `showreel`,
  `overbit-reel*`, `test-studio-ui`). Leggi cosa fanno prima di riusarli.
- `tests/qemu_test.py`: harness QEMU esistente (opzione `--shots DIR` per le
  schermate). Capisci come avvia QEMU (`-M raspi0`), come invia i tasti e come
  cattura i frame **leggendo il codice**, poi riusalo invece di scriverne uno nuovo.
- `carts/` e `sdk/`: cartucce di esempio e README degli strumenti.

## Flusso di lavoro

### 1. Scegli lo strumento e l'obiettivo

Chiedi (o ricava dal contesto) quale strumento è il protagonista e cosa lo
spettatore saprà fare a fine video. Se la serie ha un gioco di esempio (platform 2D
in stile Super Mario World), l'obiettivo del video è un passo concreto verso quel
gioco (es. "disegniamo gli sprite con bm Pixel").

### 2. Scrivi lo storyboard in tabella

Crea `video/<NN>-<strumento>/storyboard.md` con una riga per passaggio:

| # | Tempo | Dove (menu → tab) | Tasto / pulsante premuto | Cosa si vede | Frase del narratore |
|---|-------|-------------------|--------------------------|--------------|---------------------|

Regole della tabella:
- Una riga = un'azione. Mai "e poi configura": spezza finché ogni riga è un tasto
  o un gesto.
- La colonna "Tasto / pulsante" usa i nomi **esatti** della documentazione
  (tastiera e gamepad, se la console supporta entrambi: indica quale usi).
- Ogni riga ha un risultato visibile verificabile in un frame.
- Struttura tipica di ~3 minuti: 0:00 gancio (risultato finale) → 0:15 dove si
  trova lo strumento (tab Dev) → corpo (3-6 azioni chiave) → ultimi 20 s:
  provare in gioco e punto d'arrivo del prossimo video.

### 3. Prepara lo script di registrazione

Riusa l'harness esistente (`tests/qemu_test.py` o gli script dei target `showreel`).
Crea `video/<NN>-<strumento>/record.py` (o un target Makefile `video-<NN>`) che:

1. avvia QEMU con lo stesso kernel che gira sulla Pi;
2. invia i tasti dello storyboard con pause fisse tra le azioni (lasciare 0,5-1 s
   dopo ogni azione perché lo spettatore legga lo schermo);
3. salva i frame e il video alla risoluzione che l'harness già produce (la repo
   produce MP4 1280×720 per lo showreel: verifica e mantieni lo stesso formato);
4. è **deterministico**: stesso storyboard, stesso video. Niente attese a occhio.

Se QEMU non emula una funzione (es. GPU V3D, Bluetooth, WiFi), non mostrarla in
quel video e dillo all'utente: nel README la rasterizzazione 3D in QEMU avviene
con il rasterizzatore ARM, non con la GPU.

### 4. Sovrimpressioni: tasti e tab premuti

Il video deve far vedere **quale tasto è stato premuto**. Per ogni riga dello
storyboard genera una scheda in sovrimpressione (in basso) con:
- il nome del tasto o pulsante (es. `F6`, `Tab`, `A`), in stile "tasto";
- la tab o il menu in cui ci si trova (es. `Dev ▸ bm Pixel`).

Usa `ffmpeg` (`drawtext`/`overlay`) a partire dai tempi dello storyboard, oppure
genera i sottotitoli `.ass`. Se `ffmpeg` non è installato, dillo e passa all'utente
il comando da eseguire.

### 5. Copione del narratore

Crea `video/<NN>-<strumento>/copione.md`: una frase per riga dello storyboard,
tono diretto, in inglese, che spiega *cosa fa* lo strumento, *perché* serve e
*cosa si può fare di più* (le potenzialità). Per ogni frase indica il tempo di
inizio. Dopo il copione, aggiungi:

- **Titolo** (max 70 caratteri, con il nome dello strumento);
- **Descrizione YouTube** con capitoli (`0:00 ...`) generati dallo storyboard e
  link alla repo `github.com/f-accomando/bm`;
- **Miniatura**: indica quale frame usare.

### 6. Controllo prima di consegnare

- Ogni tasto nello storyboard compare in una fonte della repo (cita il file).
- Nessun riferimento a bm Studio/Animator PC o web.
- Il video dura circa quanto concordato.
- I punti `[DA VERIFICARE]` sono elencati in fondo alla risposta.
- Non eseguire comandi che scrivono su `dist/`, sull'SD o sulla repo senza
  dirlo all'utente; i file nuovi vanno solo in `video/`.

## Cosa consegnare

1. `storyboard.md`, `copione.md`, `record.py` (o target Makefile), eventuale script
   di montaggio.
2. Se l'ambiente lo permette, anche il video renderizzato; altrimenti i comandi
   esatti per generarlo (`make video-<NN>`).
3. Elenco dei dubbi aperti (tasti non trovati, `.b16`, funzioni non emulate).

## Proporre la struttura della serie

Se l'utente chiede la struttura dei video, proponi una lista ordinata di
puntate (una per strumento, ~3 minuti) verso il gioco di esempio, indicando per
ognuna lo strumento, l'obiettivo e il risultato che si vede a fine puntata.
Parti dagli sprite (bm Pixel) se non indicato diversamente, e fai approvare la
struttura prima di scrivere gli storyboard.
