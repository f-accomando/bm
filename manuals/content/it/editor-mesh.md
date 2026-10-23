title: bm Mesh: modificare le mesh
accent: Mesh
lang: it
kicker: L'editor della suite
subtitle: Sposta i vertici e le facce di un modello 3D, sulla console, e guardalo nel tuo gioco.
footer: manuali bm - editor-mesh

::: readme
**Che cos'è.** Il manuale di **bm Mesh**, l'editor di vertici e facce. Ogni tasto citato viene dal codice dell'editor.

**Come usarlo.** Leggi una volta "Aprire" e "Lo schermo e i comandi", poi segui i tre tutorial con una cartuccia tua.

**Cosa manca.** Ogni lacuna è segnata con un'etichetta DA COMPLETARE evidenziata; nulla è inventato.
:::

# Che cos'è bm Mesh

**bm Mesh** è l'editor che lavora sui punti (vertici) e sui triangoli (facce) di un modello 3D. È dentro la console: lo trovi nella scheda **Dev**.

## Cosa fa

- Mostra i modelli di una cartuccia che girano in un'anteprima, con vertici, triangoli e scheletro.
- Ti lascia scegliere vertici, spigoli o facce e spostarli, ruotarli, scalarli, estrudere, specchiare, fondere, cancellare e colorare.
- Legge anche le mesh che costruisce il codice del gioco, quindi puoi partire da un'astronave o da un anello fatti in Lua.

## Come si collega a Studio e Animator

- **bm Studio** crea i modelli. bm Mesh modifica gli stessi modelli: stanno nella sezione MESH della cartuccia.
- **bm Animator** aggiunge scheletri e animazioni (la sezione ANIM). bm Mesh conserva lo scheletro di un modello: l'osso di ogni vertice nuovo è quello del vertice da cui viene, e ossa e animazioni restano quelle di bm Animator. In bm Mesh puoi anche dare dei vertici a un osso (vedi Ossa).
- Nel gioco usi un modello con `model("name")`.

## Quale editor per cosa

- **bm Studio**: modelli statici. Oggetti, props ed edifici, fatti di blocchi e tile. La mappa 3D di un gioco si fa nella pagina mappa di bm SDK.
- **bm Mesh**: modelli organici. Personaggi ed eroi, e tutta la modifica fine di vertici e triangoli.
- **bm Animator**: rig e animazioni. Mette uno scheletro in un modello e lo muove. Non costruisce modelli.
- bm Studio e bm Mesh scrivono la stessa sezione MESH, quindi un modello passa da un editor all'altro.

## I tre tipi di mesh

L'elenco segna ogni mesh con una lettera.

| Lettera | Che cos'è | Nel gioco |
|---|---|---|
| M | un modello della sezione MESH (quelli di bm Studio) | `model("name")` |
| C | una mesh nel codice, scritta da bm Mesh come `mesh_name()` tra le righe `-- [bm Mesh begin]` e `-- [bm Mesh end]` in fondo a `main.lua` | `local m = mesh_name()`, poi `draw3d(m, ...)` |
| G | una mesh che il codice del gioco costruisce con `mesh()`, `mesh_sphere()` o `mesh_cube()` | il codice stesso del gioco |

## Da sapere

- L'editor si usa con la tastiera, con il mouse o con il gamepad.
- I passi di modifica si possono annullare (30 passi): Ctrl+Z annulla, Ctrl+Y ripete (gamepad: Y + A annulla). Rinomina e cancella azzerano i passi di annullamento e ripetizione, e lo fa anche l'apertura di un file; copia come modello, copia come codice, duplica e nuovo non creano un passo di annullamento.

# Aprire

## Progetti e giochi

Tutti gli strumenti della suite modificano solo **progetti** (`.bme`). Un **gioco** (`.bm` o `.b16`) lo possono solo leggere.

- Se apri un gioco, bm Mesh lo legge e puoi modificare. Il primo salvataggio crea una copia modificabile, con il nome del gioco e l'estensione `.BME`, e da lì lavori sulla copia.
- La barra di stato ti dice quando hai aperto un gioco: `a game: saving makes its copy`.

## Modi per aprire

- Dalla scheda **Dev** del menu della console.
- Da un gioco: premi **X** sulla sua copertina, poi **Open in bm Mesh**.
- Dal monitor: il tasto `4`.
- Dall'interno dell'editor: Esc, poi **Open...** (Ctrl+O). L'elenco mostra i progetti `.bme` di `/bme` e i file `.bm` e `.bme` di `/carts` e della radice della SD.
- Se hai avviato bm Mesh senza file, il selettore di file si apre da solo.

## Cosa vedi dopo l'apertura

La barra di stato dice quanti modelli, mesh nel codice e mesh dal codice del gioco sono state trovate, e l'editor mostra la pagina **list**.

## Attenzione

- Se il codice del gioco si ferma mentre viene letto, ricevi il messaggio `the game's code stopped while read`: le mesh trovate prima dell'errore ci sono ancora.
- Un file che non si riesce ad aprire dà `cannot open ...` con il motivo.

# Lo schermo e i comandi

## Le pagine

La barra in alto inizia con il nome `bm Mesh`. Poi ha tre pagine. Un clic su una scheda, o il suo tasto, cambia pagina.

| Pagina | Tasto | A cosa serve |
|---|---|---|
| list | F1 | scegliere una mesh; copiare, rinominare, duplicare, cancellare, crearne una nuova |
| edit | F2 | scegliere e cambiare vertici e facce |
| menu | Esc | aprire, salvare, provare il gioco, uscire |

![La pagina list: le mesh a sinistra, quella scelta che gira nell'anteprima](shots/editor-mesh-list.png)

Altri tasti. F3 funziona solo nelle pagine list ed edit, e `[` e `]` sono ignorati nella pagina menu; gli altri valgono in ogni pagina:

| Tasto | Cosa fa |
|---|---|
| F3 | i comandi della pagina, come menu |
| `[` e `]` | la mesh precedente e la successiva |
| F12 tenuto, oppure `?` | mostra i tasti della pagina |
| Ctrl+S | salva |
| Ctrl+Shift+S | salva con nome (un nome di 8 lettere, in `/carts`) |
| Ctrl+O | apre un altro file |
| Ctrl+N | un nuovo modello, un cubo |
| Ctrl+I | importa un modello da `/bmm` o `/glb` |
| Ctrl+E | esporta la mesh in `/bmm` |
| F5 o Ctrl+R | prova il gioco |
| F6 | l'assistente |
| Ctrl+Z, Ctrl+Y | annulla, ripeti |
| Ctrl+Esc | torna al menu di bm (se ci sono modifiche non salvate chiede prima) |

## La pagina list (F1)

| Tasto | Cosa fa |
|---|---|
| su, giù, home, end | sceglie la mesh |
| Enter | la modifica |
| m | copia come modello (da mesh a modello) |
| c | copia come codice (da modello a `mesh_name()`) |
| r | rinomina |
| d | duplica (un modello conserva lo scheletro) |
| Del | cancella: premilo due volte |
| n | un nuovo modello, un cubo; con F3 anche un piano, una sfera, un cilindro o la base umanoide |
| i | importa un modello da `/bmm` o `/glb` |
| x | esporta la mesh in `/bmm` |
| q, e | ruota la vista |
| w, s | inclina la vista |
| + e - | zoom |
| spazio o o | avvia o ferma la rotazione |
| f | inquadra di nuovo la mesh |

Una mesh G non si può rinominare né cancellare: la crea il codice del gioco. Copiala con **m** o **c** per averne una a cui dare un nome.

## La pagina edit (F2)

La pagina edit disegna il modello con i suoi spigoli, un puntatore e una riga in alto con il nome, il numero di vertici e di triangoli, la modalità (`VERTICES` o `FACES`) e quanti sono scelti. Sotto, un riquadro del colore corrente e il passo. La barra dei suggerimenti in fondo mostra i tasti principali. Per le tre modalità (vertici, spigoli, facce) mostra `mode`, cioè il tasto **Tab**. I tasti che la barra non mostra, come **a** (sceglie tutto), sono nell'aiuto di **F12**.

| Tasto | Cosa fa |
|---|---|
| frecce | muovono il puntatore (più veloce se tenute) |
| home | il puntatore al centro |
| n, b | il puntatore sul vertice o sulla faccia successivi o precedenti, anche dietro |
| spazio | aggiunge alla scelta, o toglie |
| Enter | sceglie solo quello |
| a | sceglie tutto; se qualcosa è scelto, non sceglie niente |
| l | sceglie tutto ciò che è collegato |
| Tab | passa da vertici a spigoli e a facce |
| g, r, t | sposta, ruota, scala |
| x | estrude le facce scelte |
| d | duplica le facce scelte |
| A (Maiusc+A) | aggiunge una parte: un cubo, un cilindro, una sfera o un piano |
| m | specchia la scelta, da sinistra a destra sullo schermo |
| M | copia la scelta dall'altra parte dello 0 |
| X (Maiusc+X) | specchio dal vivo acceso o spento |
| B (Maiusc+B) | dai i vertici scelti a un osso dello scheletro |
| v | vista ossa: i punti dei vertici prendono il colore del loro osso |
| h | mostra o nascondi il fantasma (il modello di riferimento) |
| H (Maiusc+H) | carica un fantasma da `/bmm` o `/glb`, o toglilo |
| [ e ] | il fantasma più piccolo o più grande (5 per cento a ogni pressione) |
| { e } (Maiusc+[ e Maiusc+]) | il fantasma in basso o in alto (0,05 a ogni pressione) |
| S (Maiusc+S) | aggancio: spento, griglia, vertice, griglia e vertice |
| G (Maiusc+G) | passo della griglia: 0,05, 0,1, 0,25, 0,5 |
| z | scelta a riquadro: il puntatore è un angolo; spostalo sull'altro angolo, Enter sceglie solo ciò che è dentro, Space lo aggiunge, Esc annulla |
| y | nasconde i vertici scelti; senza scelta mostra di nuovo tutto |
| Y (Maiusc+Y) | isola: nasconde tutto tranne la scelta; senza scelta mostra di nuovo tutto |
| P (Maiusc+P) | spostamento morbido: raggio spento, 0.25, 0.5, 1, 2 (anche dentro uno spostamento) |
| O (Maiusc+O) | perno di rotazione e scala: centro, origine, base (anche dentro r e t) |
| V (Maiusc+V) | leviga: rilassa i vertici scelti verso i vicini |
| R (Maiusc+R) | immagini di riferimento: una frontale e una laterale da `/pics` |
| Ctrl+R | taglio ad anello attraverso lo spigolo scelto |
| Maiusc+J | coltello: taglia un triangolo tra i punti di mezzo di due spigoli scelti |
| Maiusc+E | smusso: ogni spigolo scelto diventa una striscia stretta (scrivi la larghezza) |
| Maiusc+U | proietta le facce sullo sprite sheet (scrivi dimensione della cella, x e y) |
| j | crea una faccia su 3 o 4 vertici scelti |
| k | fonde i vertici scelti in uno |
| K | salda i vertici nello stesso punto |
| u | divide ogni faccia scelta in 4 |
| i | rovescia le facce scelte |
| Del | cancella la scelta |
| p | colora le facce scelte con il colore corrente |
| o | prende il colore della faccia sotto il puntatore |
| c | apre la tavolozza |
| q, e | ruota la vista |
| w, s | inclina la vista |
| + e - | zoom |
| f | inquadra i vertici scelti |
| 1, 3, 7, 0 | vista frontale, laterale, dall'alto, vista libera |
| `,` e `.` | un passo più fine o più grosso |

![La pagina edit: un modello con i suoi spigoli, il puntatore e un vertice scelto](shots/editor-mesh-edit.png)

## Il gamepad

| Comando | Cosa fa |
|---|---|
| Y + sinistra o destra | cambia pagina |
| Y + su o giù | la mesh precedente o la successiva |
| Y + B | il menu |
| Y + A | annulla |
| X, premuto una volta | i comandi della pagina |
| X + croce direzionale | ruota e inclina la vista |
| X + A, X + B | zoom |
| A | nell'elenco: modifica la mesh; nella pagina edit: sceglie il vertice o la faccia sotto il puntatore |
| B | nella pagina edit: non sceglie niente |
| croce direzionale | il puntatore; dentro uno strumento, cambia il valore |

[DA COMPLETARE: i comandi del pad sono scritti dal codice; non provati sul PowKiddy RGB30]

# Scegliere e spostare

## Vertici, spigoli o facce

La pagina edit ha tre modalità. **Tab** passa dall'una alla successiva: vertici, spigoli, facce, e di nuovo vertici. La riga in alto dice `VERTICES`, `EDGES` o `FACES`. In ogni modalità una scelta è un insieme di vertici, di spigoli o di facce.

- Metti il puntatore su un vertice, su uno spigolo o su una faccia: si illumina. Uno spigolo è la linea tra due vertici di un triangolo; il puntatore deve stare entro 6 pixel.
- **spazio** lo aggiunge alla scelta, o lo toglie se c'era. **Enter** lo rende l'unico.
- Con il mouse, un clic sceglie solo ciò che è sotto il puntatore; Shift + clic aggiunge o toglie.
- In una riga di dialogo (per esempio il nome del file), un clic mette il cursore tra le lettere.
- **a** sceglie tutto; premilo ancora per non sceglierne nessuno. **l** aggiunge tutto ciò che è unito alla scelta da facce.
- **n** e **b** scorrono tutti i vertici, gli spigoli o le facce in ordine: così raggiungi anche quelli nascosti dietro il modello.

Quando cambi modalità la scelta ti segue: da vertici a spigoli, gli spigoli di cui i due estremi sono scelti; da spigoli a facce, le facce di cui tutti e tre gli angoli sono scelti; da facce a vertici, gli angoli delle facce scelte.

## Sposta, ruota, scala

Premi **g** per spostare, **r** per ruotare, **t** per scalare la scelta (si muovono anche gli angoli delle facce scelte). Lo strumento si apre e la riga in alto mostra il valore, per esempio `MOVE x 0 y 0.25 z 0`.

- Le frecce e PgUp/PgDn cambiano il valore. Per uno spostamento: sinistra e destra vanno lungo l'asse orizzontale della vista, su e giù cambiano l'altezza, PgUp e PgDn avvicinano o allontanano.
- **x**, **y**, **z** limitano il cambiamento a quell'asse. **n** agisce lungo la normale. Premi lo stesso tasto ancora per liberarlo.
- `,` e `.` cambiano il passo. I passi di spostamento sono 0.01, 0.05, 0.1, 0.25, 0.5 e 1; quelli di rotazione 1, 5, 15, 30, 45 e 90 gradi; quelli di scala 1.01, 1.05, 1.1, 1.25, 1.5 e 2.
- Scrivi **0**-**9** per dare il valore esatto (vedi *Scrivere un valore*). **Maiusc+P** accende lo spostamento morbido (vedi *Spostamento morbido*).
- **Enter** conferma. **Esc** annulla e rimette tutto a posto.
- Con il mouse, trascina il tasto sinistro per cambiare il valore e fai clic per metterlo. Con il gamepad, A conferma, B annulla e un tocco di X cambia asse.

### Il perno (Maiusc+O)

Rotazione e scala girano e ingrandiscono la scelta intorno a un punto, il perno. Premi **Maiusc+O** per cambiare il perno. Puoi premerlo prima dello strumento, o mentre lo strumento è aperto (dentro **r** e **t**). Gira tra tre valori:

| Perno | Il punto |
|---|---|
| centre | il centro della scelta (è il valore di partenza) |
| origin | il punto (0, 0, 0) del modello |
| base | il centro della scelta in x e z, e la y più bassa della scelta |

Usa *base* per scalare una figura e tenerne i piedi a terra. Usa *origin* per girare una parte intorno al centro del modello. Quando il perno non è il centro, l'intestazione mostra `PIVOT origin` o `PIVOT base`. Con lo specchio dal vivo acceso, il lato speculare gira intorno al perno speculare. Lo spostamento non ha perno. Il perno non è salvato nel file: una nuova sessione parte con *centre*. Il menu ha la voce *Pivot of rotate and scale: centre, origin, base (O)*.

## Altre modifiche

- **Maiusc+V** leviga. Un passaggio sposta ogni vertice scelto a metà strada verso la media dei vertici che condividono uno spigolo con lui. Un vertice su un bordo aperto (uno spigolo con una sola faccia), un vertice nascosto e un vertice senza spigoli restano dove sono. Con lo specchio dal vivo acceso, il vertice speculare riceve l'immagine speculare del risultato. Se niente può muoversi, un messaggio lo dice. Un passaggio è un passo di annullamento. Premi di nuovo **Maiusc+V** per levigare di più.
- **x** estrude le facce scelte; poi si muovono lungo la normale.
- **j** crea una faccia su 3 o 4 vertici scelti, nell'ordine in cui li hai scelti, rivolta verso la camera. Funziona solo nella modalità vertici.
- **k** fonde i vertici scelti nel loro punto medio; **K** salda i vertici che stanno nello stesso punto (con il flag 4 di `draw3d` le facce intorno sembrano lisce).
- **Del** cancella: nella modalità vertici i vertici e le facce che li usano, nella modalità facce le facce.
- **p** colora, **o** prende un colore, **c** apre la tavolozza (frecce per scegliere, Enter per prenderlo, Esc per chiuderla). Una faccia con una texture della sheet non ha colore.

## Aggiungi una parte

**Maiusc+A** apre un elenco di quattro forme: cubo, cilindro, sfera e piano. Quella che scegli si aggiunge al modello come parte nuova, di larghezza 1 e del colore della pagina. Va al centro dei vertici scelti, oppure allo 0 se non c'è nulla di scelto. I vertici della parte nuova diventano la scelta, quindi puoi spostarli, ruotarli o scalarli subito. Con lo specchio dal vivo acceso (Maiusc+X), si aggiunge anche l'immagine speculare rispetto a X = 0, e tutti i vertici nuovi sono la scelta. Un solo annulla toglie entrambe le parti. Se il modello ha uno scheletro, i vertici nuovi seguono l'osso del primo vertice scelto (o il primo osso). Con il gamepad tocca X e scegli **Aggiungi una parte**.

## Specchio dal vivo

**Maiusc+X** accende e spegne lo specchio dal vivo. Quando è acceso, la pagina mostra **MIRROR X**. Sposta, ruota, scala, dipingi, cancella, aggiungi una parte e il taglio ad anello agiscono allora sui vertici o sulle facce scelti e sui loro corrispondenti dall'altra parte del piano X = 0. L'editor trova il corrispondente dalla posizione: il vertice in (-x, y, z), entro 0,001. Un vertice sul piano è il corrispondente di se stesso. Un vertice senza corrispondente cambia da solo. Se scegli entrambi i lati, guida il vertice con il numero più basso e l'altro lo segue. Due strumenti che cambiano la topologia lavorano su entrambi i lati. Maiusc+A aggiunge la parte e la sua immagine speculare: la seconda parte è l'esatta copia speculare, con le facce sempre rivolte all'esterno, e prende l'osso speculare. Se il centro della parte è sul piano (x entro 0,001 da 0), la parte si aggiunge una volta sola. Ctrl+R taglia l'anello dello spigolo scelto e l'anello del suo spigolo corrispondente. Se lo spigolo corrispondente non esiste, si taglia un solo anello e un messaggio lo dice. Se l'anello attraversa il piano e contiene lo spigolo corrispondente, si taglia una volta sola. In modalità spigoli, scegliere uno spigolo sceglie anche il suo corrispondente, e Ctrl+R accetta questa coppia. Gli altri strumenti di topologia (estrudi, duplica, suddividi e il coltello) non agiscono sull'altro lato. La sfera e il cilindro hanno triangoli che sono immagini speculari rispetto a X = 0, quindi ogni vertice che Suddividi aggiunge ha un corrispondente. Un solo annulla riporta indietro entrambi i lati. Con il gamepad tocca X e scegli **Live mirror X on / off**.

## Ossa

Un modello con uno scheletro dà ogni vertice a un solo osso. Le ossa le fa bm Animator. In bm Mesh scegli quali vertici seguono quale osso.

1. Scegli vertici o facce. Per scegliere una parte intera, scegli un suo vertice e premi **l**.
2. Premi **Maiusc+B**. Un elenco mostra le ossa con numero e nome.
3. Scegli un osso e premi Invio. I vertici scelti (gli angoli delle facce scelte) ora seguono quell'osso. Un messaggio dice quanti sono.

L'intestazione mostra l'osso della scelta a destra della seconda riga, per esempio **BONE 3 arm.L**. Se la scelta ha vertici di più ossa, mostra **BONES 3: arm.L+** (il numero di ossa e la prima). Se non c'è nulla di scelto, mostra l'osso del vertice sotto il puntatore. **v** accende e spegne la vista ossa: i punti dei vertici prendono il colore del loro osso (12 colori, poi si ripetono). Un solo annulla riporta indietro tutta la modifica.

Con lo specchio dal vivo acceso, seguono anche i corrispondenti dall'altra parte di X = 0. Se lo scheletro ha una coppia speculare per nome, i corrispondenti prendono l'altro osso della coppia. I nomi che differiscono solo per il lato sono una coppia: `arm_l` e `arm_r`, `hand.L` e `hand.R`, `l_foot` e `r_foot`, `armLeft` e `armRight`. Un osso senza coppia nello scheletro dà lo stesso osso ai corrispondenti. Se scegli entrambi i lati, i vertici dal lato della testa dell'osso tengono quell'osso e l'altro lato prende la coppia. Un vertice su X = 0 tiene l'osso che hai scelto.

Se uno scheletro non ha ancora vertici, tutti i vertici sono sull'osso 0 finché non li dai a un altro osso. Se lo scheletro è stato fatto per un altro numero di vertici, bm Mesh non cambia le ossa: associa di nuovo il modello in bm Animator. Un modello senza scheletro risponde "no skeleton". Con il gamepad tocca X e scegli **Give the choice to a bone** o **Bone view**.

## Aggancio

**Maiusc+S** cambia l'aggancio. L'ordine è: spento, griglia, vertice, griglia e vertice, poi di nuovo spento. La pagina mostra il modo in alto a destra, per esempio **SNAP grid 0.1**, e un messaggio dice il modo nuovo. L'aggancio funziona mentre sposti i vertici (**g**, e lo spostamento dopo **d**). **Maiusc+G** cambia il passo della griglia: 0,05, 0,1, 0,25 o 0,5 unità. Puoi premere i due tasti anche mentre uno spostamento è aperto.

L'aggancio agisce sul vertice guida. È il vertice scelto con il numero più basso. L'editor aggancia la posizione di questo vertice, e ogni altro vertice scelto si sposta della stessa distanza. Il modo griglia arrotonda ogni asse del vertice guida al passo. Se blocchi un asse (x, y o z), si arrotonda solo quell'asse. Il modo vertice porta il vertice guida sul vertice più vicino del modello quando quel vertice è entro 6 pixel sullo schermo. Un piccolo anello verde mostra il vertice che l'aggancio ha preso. I vertici scelti non sono mai un bersaglio. Nel modo griglia e vertice vince un vertice a portata, e la griglia agisce quando nessun vertice è a portata. Con un asse bloccato, l'aggancio al vertice cambia solo quell'asse.

Con lo specchio dal vivo acceso, i corrispondenti seguono lo stesso spostamento, specchiato. Un solo annulla riporta indietro tutto lo spostamento. La riga dello strumento mostra **SNAP** e lo spostamento dopo l'aggancio. L'aggancio non cambia una rotazione o una scala, e non posiziona una parte nuova. Con il gamepad tocca X e scegli **Snap: off, grid, vertex, both** o **Snap grid step**.

## Scelta a riquadro, nascondi e isola

**Scelta a riquadro.** Premi **z**. Il puntatore è ora un angolo di un riquadro. Con le frecce porta il puntatore sull'altro angolo: il riquadro è disegnato. **Enter** sceglie solo ciò che è dentro il riquadro, **Space** lo aggiunge alla scelta, **Esc** annulla. Con il mouse, trascina il tasto sinistro sul modello: quando lo lasci, ciò che è dentro è scelto; tieni premuto Maiusc per aggiungere. Il riquadro prende tutto ciò che è dentro, anche i vertici dietro il modello. In modalità vertici sceglie i vertici dentro. In modalità spigoli sceglie gli spigoli con i due estremi dentro. In modalità facce sceglie le facce con i tre angoli dentro. Con il gamepad tocca X e scegli **Box select**: il tasto A sceglie, il tasto B annulla.

**Nascondi e isola.** Premi **y** per nascondere i vertici scelti e le facce che li usano. Premi **Maiusc+Y** per isolare: tutto tranne la scelta è nascosto. In alto a destra si vede quanti vertici sono nascosti, per esempio `12 HIDDEN`. Un vertice nascosto non è disegnato, non è sotto il puntatore, non è scelto da **a**, **l** o da un riquadro, e nessuno strumento lo sposta. Con lo specchio dal vivo acceso, sono nascosti anche i corrispondenti. Premi **y** (o **Maiusc+Y**) senza niente di scelto per mostrare di nuovo tutto. I vertici nascosti restano nel modello: il file li tiene tutti e i contatori li contano tutti. Nascondere fa parte dei passi di annullamento, quindi **Ctrl+Z** li mostra di nuovo. L'insieme dei nascosti si dimentica quando passi a un'altra mesh.

## Spostamento morbido

**Maiusc+P** accende lo spostamento morbido. Fa girare il raggio: spento, 0.25, 0.5, 1 e 2 unità. In alto a destra si vede, per esempio `SOFT 0.5`. Con lo spostamento morbido acceso, uno spostamento (**g**) tira anche i vertici vicini a quelli scelti: un vertice a distanza uguale al raggio non si muove, un vertice accanto alla scelta segue quasi del tutto, e la discesa in mezzo è dolce. La distanza è quella nello spazio dal vertice scelto più vicino. La riga dello strumento mostra il raggio e quanti vertici seguono, per esempio `soft 0.5 (34 near)`. Puoi premere **Maiusc+P** dentro lo spostamento per cambiare il raggio. Lo spostamento morbido agisce solo sullo spostamento, non su una rotazione o una scala. I vertici nascosti non seguono. Con lo specchio dal vivo acceso, ogni corrispondente tira i propri vicini, specchiati. L'aggancio e il valore scritto funzionano insieme. Un solo annulla riporta indietro lo spostamento e tutti i vertici che ha tirato.

## Scrivere un valore

In uno spostamento, una rotazione o una scala puoi scrivere il valore esatto. Premi **x**, **y**, **z** o **n** per scegliere l'asse, poi scrivi il numero con i tasti da **0** a **9**, **.** e **-** (il meno per primo). La riga in alto mostra `typed 1.25_` e il modello segue subito. **Backspace** toglie l'ultimo carattere. Per uno spostamento il numero è la distanza in unità su quell'asse. Per una rotazione è l'angolo in gradi. Per una scala è il fattore. Se non hai scelto un asse, lo spostamento va lungo x, la rotazione gira intorno a y e la scala cambia tutti e tre gli assi. Premi un altro tasto d'asse per scrivere un valore anche per quell'asse: `x 3`, poi `y 4` dà uno spostamento di 3 lungo x e di 4 lungo y. Il valore scritto sostituisce i passi delle frecce di quell'asse. **Enter** conferma ed **Esc** annulla, come sempre. Comincia un numero minore di 1 con lo zero (`0.5`): il punto da solo è il tasto del passo. Per scrivere serve una tastiera.

## Immagini di riferimento

Un'immagine di riferimento è un'immagine frontale o laterale che sta dietro il modello mentre lo modifichi. Metti le immagini nella cartella `/pics` della scheda SD (PNG, JPEG baseline o WebP).

1. Premi **Maiusc+R**. Un elenco mostra **Front picture** e **Side picture**. Con il gamepad tocca X e scegli **Reference pictures**.
2. Scegline una. Un secondo elenco mostra le immagini di `/pics`. Scegline una.
3. Premi **1** per la vista frontale, o **3** per la vista laterale. L'immagine è disegnata dietro il modello, piatta sul piano che passa per il centro del modello, con i piedi del modello sul suo bordo basso.

L'immagine si vede quando la vista la guarda dritta, entro circa 17 gradi. L'immagine frontale si vede da davanti e da dietro (specchiata), quella laterale da entrambi i lati. Nelle altre viste non è disegnato niente. L'immagine è adattata a 256 per 256 pixel ed è disegnata chiara e sbiadita, così il modello resta facile da vedere. L'elenco di **Maiusc+R** ha anche **Hide the pictures**, **Bigger**, **Smaller**, **Up**, **Down** e **Clear the pictures**. All'inizio l'immagine è alta quanto il modello. Usa Bigger e Smaller finché l'immagine combacia, e Up e Down per mettere i piedi a terra.

Le immagini non sono mai salvate nel modello o nel progetto. Stanno nel foglio degli sprite, sotto le righe del progetto: due posti di 256 righe ciascuno (512 righe). Clear riporta il foglio alla sua misura. L'esportazione in `/bmm` scrive solo le righe del progetto. Aprire un altro progetto fa dimenticare le immagini. Il Raspberry Pi Zero W ha poca memoria: due immagini piccole costano circa 260 KB nel foglio. Usa immagini non più grandi di 1000 pixel: la console legge prima tutto il file, poi lo rimpicciolisce.

## Spigoli e taglio ad anello

In modalità spigoli una scelta è un insieme di spigoli. Gli spigoli scelti si vedono come linee spesse. Sposta, ruota e scala agiscono sui due estremi di ogni spigolo scelto. **x** e **d** agiscono sulle facce di cui tutti gli angoli sono scelti. **Canc** toglie i triangoli accanto agli spigoli scelti, e i vertici che nessun triangolo usa più. Con lo specchio dal vivo acceso, scegliere uno spigolo sceglie anche il suo corrispondente dall'altra parte di X = 0, e sposta, ruota, scala e cancella agiscono su entrambi. +'+altro lato.'altro lato.

**Ctrl+R** fa un taglio ad anello. Scegli prima uno spigolo (in modalità vertici bastano due vertici che formano uno spigolo). L'editor parte dallo spigolo e attraversa i quadrati intorno al modello. Un quadrato è fatto di due triangoli che guardano dalla stessa parte (l'angolo tra le loro normali è minore di 60 gradi) e che hanno una diagonale in comune. In ogni quadrato attraversa lo spigolo e quello opposto, e continua da quello opposto. Mette un vertice nuovo a metà di ogni spigolo attraversato e taglia ogni quadrato in quattro triangoli. Un vertice nuovo prende l'osso dell'estremo più basso del suo spigolo e la media dei punti della texture (UV). Un triangolo nuovo prende il colore della vecchia faccia che contiene più dei suoi angoli.

L'anello si ferma in due casi. A un bordo aperto (uno spigolo con un solo triangolo) non c'è nulla da attraversare. A un triangolo che non fa parte di un quadrato, il triangolo viene tagliato in due, dal vertice nuovo all'angolo opposto, così non resta nessun buco. L'anello è chiuso se torna al primo spigolo. Se l'anello si attraversasse da solo, o uno spigolo avesse più di due triangoli, non cambia nulla e un messaggio dice il perché.

Alla fine i vertici nuovi sono la scelta (in modalità spigoli, i nuovi spigoli attraverso i quadrati), quindi puoi spostare o scalare l'anello subito. Con lo specchio dal vivo acceso, si taglia anche l'anello dello spigolo corrispondente, e i vertici nuovi di entrambi gli anelli sono la scelta. Un solo annulla riporta indietro tutto il taglio, entrambi gli anelli. Con il gamepad tocca X e scegli **Loop cut on the chosen edge**.

**Maiusc+J** è il coltello. Usalo in modalità spigoli. Scegli due spigoli di un triangolo (due spigoli che si incontrano in un angolo del triangolo). L'editor mette un vertice nuovo a metà di ogni spigolo e taglia il triangolo in modo che un nuovo spigolo unisca i due vertici nuovi. Anche il triangolo dall'altra parte di ognuno dei due spigoli viene tagliato in due, dal vertice nuovo all'angolo opposto, così non compare nessuna fessura. Un vertice nuovo prende l'osso dell'estremo più basso del suo spigolo e la media dei punti della texture (UV). Un triangolo nuovo tiene il colore della sua vecchia faccia. I due vertici nuovi sono la scelta (in modalità spigoli, il nuovo spigolo), e un solo annulla riporta indietro tutto il taglio.

Il coltello non fa nulla, e un messaggio dice il perchè, in questi casi: non hai scelto esattamente due spigoli, i due spigoli non sono spigoli di uno stesso triangolo, oppure uno spigolo ha più di due triangoli. Il coltello lavora su un solo triangolo, quindi lo specchio dal vivo non lo copia sull'altro lato. Con lo specchio dal vivo acceso, scegliere uno spigolo sceglie anche il suo corrispondente, quindi spegni lo specchio (Maiusc+X) prima di scegliere i due spigoli. Taglia l'altro lato a mano. Con il gamepad tocca X e scegli **Knife on two edges of a triangle**.

## Smusso

**Maiusc+E** smussa gli spigoli. Usalo in modalità spigoli. Scegli gli spigoli, premi **Maiusc+E** e scrivi la larghezza (il primo valore è 0.1). In modalità vertici o facce si usano gli spigoli con entrambe le estremità scelte. Ogni spigolo scelto diventa una striscia stretta di due triangoli, e l'angolo duro prende una luce morbida.

Le due facce di uno spigolo si allontanano da esso della larghezza, e la striscia le unisce. All'estremità di uno spigolo il vertice d'angolo si divide in due (o in più, dove si incontrano più spigoli scelti; da tre spigoli in su c'è una faccia di chiusura), e una faccia si taglia in due, così non resta nessun buco. Le facce nuove tengono il colore, gli uv dello sheet e l'osso delle facce e dei vertici da cui vengono.

- Uno spigolo piatto (due facce che guardano nello stesso verso) viene lasciato fuori, e il messaggio lo conta.
- La larghezza si riduce a 0.45 dell'altezza minima delle facce su uno spigolo scelto, così la striscia non si ripiega. Il messaggio dice quando è stata ridotta.
- L'editor rifiuta uno spigolo su un bordo aperto o con più di due facce, e un vertice le cui facce non si chiudono attorno. Un messaggio dice quale, e nulla cambia.
- Finito lo smusso, le strisce sono la scelta (modalità facce). Un annulla riporta indietro tutto lo smusso.
- Con lo specchio dal vivo acceso si smussano anche i corrispondenti rispetto a X = 0. I due lati hanno la stessa larghezza.

## Proietta sullo sprite sheet

**Maiusc+U** dà un posto sullo sprite sheet a ogni faccia scelta (o a ogni faccia mostrata, se non ne hai scelta nessuna). È la proiezione a scatola. Scrivi la dimensione della cella, la x e la y dell'angolo in alto a sinistra, per esempio `32 0 0`. L'editor usa 6 celle di quella dimensione, 3 in orizzontale e 2 in verticale: +x, -x, +y sulla prima riga, e -y, +z, -z sulla seconda. Ogni faccia va nella cella del lato verso cui guarda, disegnata come si vede da quel lato. Tutte le celle hanno la stessa scala, quindi la dimensione del texel è la stessa su ogni lato. Un pixel al bordo di ogni cella resta libero.

Le facce diventano facce con una texture dello sheet. L'editor non dipinge lo sheet: dipingi le celle in bm Pixel, poi guarda il modello. Se lo sheet è troppo piccolo per le 6 celle, un messaggio lo dice e nulla cambia. Le facce che erano di colore piatto perdono il colore. Un annulla riporta indietro la proiezione.

## Modello di riferimento (fantasma)

Un fantasma è un modello che mostri come riferimento tenue mentre modifichi un altro modello. Ci disegni sopra. È disegnato come un sottile filo verde dietro il tuo lavoro. Non fa parte del tuo modello: non lo puoi scegliere, non è nei contatori dei vertici e dei triangoli, e non è mai salvato nel progetto o in un modello. Lo specchio dal vivo e l'aggancio non agiscono su di lui.

1. Apri un modello nella pagina di modifica. Premi **Maiusc+H**. Con il gamepad tocca X e scegli **Ghost: load a model, clear it**.
2. Scegli un modello da `/bmm` o `/glb`. Un `.glb` è solo letto: bm Mesh non lo converte e non scrive nulla in `/bmm`. La riga di stato dice il nome e i conteggi, per esempio `ghost robot.glb: 1200 vertices, 2000 triangles`. La pagina mostra **GHOST** e il nome in alto a destra. L'ultima riga dell'elenco è **Ghost detail**. Premi Invio su di essa per cambiare il dettaglio: 2000, 5000, 20000 o tutte le facce. Il dettaglio vale dal prossimo caricamento. Se un fantasma è caricato, bm Mesh lo legge di nuovo con il nuovo dettaglio e mantiene scala e altezza. Il dettaglio è un'impostazione dell'editor: non è salvato nel modello.
3. Premi **h** per nascondere il fantasma e ancora **h** per mostrarlo. Il fantasma resta quando cambi pagina o modello.
4. Adatta il fantasma al tuo lavoro. **[** e **]** lo fanno più piccolo e più grande. **{** e **}** (**Maiusc+[** e **Maiusc+]**) lo spostano in basso e in alto. La scala va da 0,1 a 10. Il fantasma usa la stessa camera del tuo modello, quindi resta allineato quando ruoti la vista.
5. Per togliere il fantasma, premi **Maiusc+H** e scegli **Clear the ghost**. La memoria del fantasma è liberata.

Con un fantasma caricato nella pagina di modifica, **[** e **]** cambiano la scala del fantasma e non passano più alla maglia precedente o successiva. Usa la pagina elenco (F1) per cambiare maglia.

Il fantasma ha un budget di facce. Il valore predefinito è 20000. Un modello con più facce ne mostra solo una parte: un `.glb` è ridotto dalla console, un `.bmm` mostra 1 triangolo su 2, 3 o più. La riga di stato lo dice quando succede. Il fantasma ha un secondo limite: un fotogramma disegna al massimo 4000 spigoli del fantasma. Quando il filo ha più spigoli, bm Mesh disegna 1 spigolo su 2, 3 o più, e l'insieme degli spigoli disegnati cambia a ogni fotogramma. Il filo appare più rado e può tremolare un poco. La riga di stato lo dice, per esempio `each frame draws 1 edge in 8 (the line budget)`. Scegli un dettaglio più basso per avere un filo più stabile. Questi limiti valgono solo per l'immagine: il file non cambia. Il fantasma non ha texture.

## La vista

**q** e **e** ruotano, **w** e **s** inclinano, **+** e **-** fanno zoom, **f** inquadra la scelta. **1**, **3**, **7** e **0** danno le viste dritte. Con il mouse: il tasto destro trascinato ruota, quello centrale sposta, la rotella fa zoom.

## Attenzione

- Modificare una mesh **G** prima ne crea una copia come modello; il codice del gioco non viene riscritto. La barra di stato dice il nome della copia.
- Le modifiche che supererebbero il limite del formato di 65535 vertici o 65535 triangoli vengono rifiutate e annullate.

# L'anteprima e cart_meshes

Il modello che modifichi è disegnato illuminato e con le sue texture dalla sheet della cartuccia, così lo vedi come lo mostrerà il gioco. Nella pagina edit sono disegnati solo gli spigoli delle facce rivolte verso la camera, e solo quando la mesh ha 3000 triangoli o meno.

## Come arrivano le mesh del gioco

Le mesh G vengono dalla funzione del kernel `cart_meshes(path)`. Esegue il codice del gioco **a parte**, in uno stato Lua tutto suo:

- esegue il corpo del file, poi `_init`, `_update` e `_draw` una volta;
- c'è un limite di istruzioni; file, schermo e suono non fanno nulla;
- raccoglie ciò che il codice costruisce con `mesh()`, `mesh_sphere()` e `mesh_cube()`;
- il nome di una mesh è il nome della variabile che la contiene: `M.ship` dà `ship`.

## Da sapere

- Una mesh costruita solo in un fotogramma successivo, o solo dopo un input, può non essere trovata.
- Se il codice si ferma con un errore, bm Mesh mostra comunque ciò che ha trovato e ti dice l'errore.
- Una mesh G uguale a una delle mesh C non compare due volte.

# Salvare

- **Ctrl+S** salva. **Ctrl+Shift+S** chiede un nome (8 lettere, salvato in `/bme` con l'estensione `.BME`).
- Il menu (Esc) ha **Save**, **Save as...**, **Try the game (F5)**, **Import a model...**, **Export to /bmm...** e **Exit bm Mesh**. Un segno `*` dopo il nome del file in alto significa modifiche non salvate.
- Cambiano solo le sezioni MESH e ANIM e il blocco di bm Mesh nel codice. Sprite sheet, mappa, copertina, suono e il resto del codice restano esattamente come erano.
- Un modello senza vertici o senza facce resta fuori dal file: il messaggio dice quale.
- Una cartuccia contiene al massimo 256 modelli.
- Se hai aperto un gioco, il primo salvataggio chiede la copia modificabile e da lì il file è il `.BME`. Per riavere un gioco, costruiscilo dal progetto nell'SDK.
- Exit e Open ti chiedono di scegliere la stessa voce due volte quando ci sono modifiche non salvate.

## Importare un .glb

La scheda SD ha due cartelle per i modelli. `/glb` contiene i file `.glb` che copi lì; bm Mesh li legge soltanto. `/bmm` contiene i modelli convertiti (`.bmm`); bm Mesh li scrive lì. Tutti i nomi sono in minuscolo.

1. Apri una cartuccia o un progetto. Premi **i** nella pagina list, oppure **Ctrl+I** in qualsiasi pagina, oppure scegli **Import a model...** nel menu. Sul pad, apri i comandi con **X** e scegli la voce.
2. L'elenco mostra `/bmm` e `/glb`. Un nome segnato `.glb: converted to /bmm` non ha ancora un `.bmm`.
3. Scegli un `.bmm` per leggerlo. Scegli un `.glb` per convertirlo: bm Mesh scrive `/bmm/NAME.bmm` e apre il modello nella pagina edit.
4. Se il nome ha più di 8 caratteri, o `/bmm` ha già quel nome, bm Mesh chiede un nuovo nome (da 1 a 8 tra a-z, 0-9, `_` e `-`).
5. La riga di stato mostra i vertici e i triangoli. Se il file non va bene, mostra il messaggio della console, per esempio il limite del formato di 65535 vertici o triangoli.

Un `.glb` dà colori piatti. La texture di un `.bmm` resta in quel file: le facce che la usavano diventano grigie, perché il progetto ha un suo sheet. Un `.bmm` con uno scheletro lo conserva. Un `.bmm` con più modelli dà il primo.

**Ctrl+S** salva il modello nel progetto, come ogni modello. **x** nella pagina list, **Ctrl+E**, o **Export to /bmm...** nel menu scrive la mesh in `/bmm/NAME.bmm`; se il file c'è già, bm Mesh chiede prima di sostituirlo.

# Tutorial

::: try
Fai i tre tutorial in ordine con una cartuccia tua che ha un modello, oppure con un gioco che costruisce le sue mesh nel codice.
:::

## Sposta un vertice

1. Avvia bm Mesh dalla scheda **Dev** (oppure **X** sulla copertina di un gioco, **Open in bm Mesh**). Scegli la cartuccia nel selettore di file con su, giù ed Enter.
2. Sei nella pagina list. Scegli una mesh con su e giù: gira nell'anteprima.
3. Premi **Enter** per modificarla. Se la mesh è una mesh G, la prima modifica ne crea una copia come modello.
4. Guarda la riga in alto. Se non dice `VERTICES`, premi **Tab** finché lo dice.
5. Muovi il puntatore con le frecce su un vertice finché ottiene un cerchio intorno.
6. Premi **Enter**. La riga in alto dice `VERTICES: 1 chosen`.
7. Premi **g**. La riga in alto mostra `MOVE`. Premi la freccia **su** una volta: il vertice sale del passo.
8. Premi **Enter**. La barra di stato dice `move ...: 1 vertices`.
9. Premi **Ctrl+S**. Vedi `saved` con il nome del file.

Se non va bene: **Esc** mentre lo strumento è aperto rimette a posto il vertice; **Ctrl+Z** dopo la conferma lo annulla.

::: try
Premi di nuovo **g**, poi **x**, poi la freccia destra qualche volta, poi **Enter**. Il vertice si muove solo lungo x.
:::

## Sistema una faccia

Nella pagina edit, quando la mesh ha 3000 triangoli o meno, hanno gli spigoli disegnati solo le facce rivolte verso la camera. Se una faccia non ha spigoli da nessun lato, o è rovesciata o manca.

### La faccia è rovesciata

1. Nella pagina edit premi **Tab** finché la riga in alto dice `FACES`.
2. Muovi il puntatore sulla faccia e premi **Enter**. La faccia è evidenziata e la riga in alto dice `FACES: 1 chosen`.
3. Premi **i**. Vedi `1 faces turned over`.
4. Premi **q** o **e** per ruotare la vista e controllare.

### La faccia manca

1. Premi **Tab** finché la riga in alto dice `VERTICES`.
2. Scegli tre (o quattro) vertici con **spazio**, uno dopo l'altro, nell'ordine intorno alla faccia.
3. Premi **j**. Vedi `a new face (1 triangles)`, o 2 per quattro vertici. La faccia prende il colore corrente; **o** e **p** cambiano i colori.
4. Se ti sembra ancora sbagliata, scegli la faccia e premi **i**.

Se vedi `choose 3 or 4 vertices (in order) for a new face`, il numero di vertici scelti è sbagliato.

## Controlla il modello in un gioco

1. Salva con **Ctrl+S**. Ricorda il nome che il modello ha nell'elenco (per la copia di una mesh G, te l'ha detto la barra di stato).
2. Nel codice del gioco usa il nome: per un modello, `model("name")`.
3. Premi **F5**. bm Mesh salva se serve e avvia il gioco.
4. Guarda il modello nel gioco. Poi torna all'editor: ritrovi la stessa pagina.
5. Se qualcosa non va, modifica ancora e premi di nuovo F5.

::: copy
local m = model("ship")
draw3d(m, 0, 0, 0, 0, 0, 0, 1, 0)
:::

Per uscire dal gioco e tornare a bm Mesh premi Start + Select sul pad o Ctrl+Esc sulla tastiera; l'editor torna alla stessa pagina.

# Consigli e limiti

## Consigli

- Usa **1**, **3** e **7** per controllare uno spostamento da davanti, di lato e dall'alto.
- Usa un passo piccolo (`,`) per allineare un vertice, uno più grande (`.`) per piazzare una parte intera.
- Usa **M** per costruire un modello simmetrico: fai metà, poi copiala dall'altra parte.
- Usa **K** dopo aver unito delle parti, perché le facce intorno sembrino lisce.
- Salva spesso: l'editor ricorda fino a 100 passi di annullamento (meno su un modello molto grande: i passi possono occupare 4 MB in tutto, e almeno 2 restano), e aprire o rinominare li azzera.

## Limiti

- Il limite del formato di una mesh è 65535 vertici e 65535 triangoli. La pagina di modifica mostra il conteggio dei vertici rispetto a esso.
- Al massimo 256 modelli in una cartuccia.
- Una mesh G non si può rinominare né cancellare: cambia il codice con bm Code.
- Una copia come codice non conserva lo scheletro.
- La base umanoide (F3 nella lista, **New model: humanoid with a skeleton**) ha 17 scatole, 136 vertici, 204 triangoli e 16 ossa. Non ha animazioni: falle in bm Animator.
- L'assistente (F6) aggiunge un modello senza scheletro.
- [DA COMPLETARE: la dimensione di un modello che il Pi Zero W disegna in modo fluido]
