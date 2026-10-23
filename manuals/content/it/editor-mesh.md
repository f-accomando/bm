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
- **bm Animator** aggiunge scheletri e animazioni (la sezione ANIM). bm Mesh conserva lo scheletro di un modello: l'osso di ogni vertice nuovo è quello del vertice da cui viene, e ossa e animazioni restano quelle di bm Animator.
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

La barra in alto ha tre pagine. Un clic su una scheda, o il suo tasto, cambia pagina.

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
| n | un nuovo modello, un cubo; con F3 anche un piano o una sfera |
| q, e | ruota la vista |
| w, s | inclina la vista |
| + e - | zoom |
| spazio o o | avvia o ferma la rotazione |
| f | inquadra di nuovo la mesh |

Una mesh G non si può rinominare né cancellare: la crea il codice del gioco. Copiala con **m** o **c** per averne una a cui dare un nome.

## La pagina edit (F2)

La pagina edit disegna il modello con i suoi spigoli, un puntatore e una riga in alto con il nome, il numero di vertici e di triangoli, la modalità (`VERTICES` o `FACES`) e quanti sono scelti. Sotto, un riquadro del colore corrente e il passo.

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
| Ctrl+R | taglio ad anello attraverso lo spigolo scelto |
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
- **a** sceglie tutto; premilo ancora per non sceglierne nessuno. **l** aggiunge tutto ciò che è unito alla scelta da facce.
- **n** e **b** scorrono tutti i vertici, gli spigoli o le facce in ordine: così raggiungi anche quelli nascosti dietro il modello.

Quando cambi modalità la scelta ti segue: da vertici a spigoli, gli spigoli di cui i due estremi sono scelti; da spigoli a facce, le facce di cui tutti e tre gli angoli sono scelti; da facce a vertici, gli angoli delle facce scelte.

## Sposta, ruota, scala

Premi **g** per spostare, **r** per ruotare, **t** per scalare la scelta (si muovono anche gli angoli delle facce scelte). Lo strumento si apre e la riga in alto mostra il valore, per esempio `MOVE x 0 y 0.25 z 0`.

- Le frecce e PgUp/PgDn cambiano il valore. Per uno spostamento: sinistra e destra vanno lungo l'asse orizzontale della vista, su e giù cambiano l'altezza, PgUp e PgDn avvicinano o allontanano.
- **x**, **y**, **z** limitano il cambiamento a quell'asse. **n** agisce lungo la normale. Premi lo stesso tasto ancora per liberarlo.
- `,` e `.` cambiano il passo. I passi di spostamento sono 0.01, 0.05, 0.1, 0.25, 0.5 e 1; quelli di rotazione 1, 5, 15, 30, 45 e 90 gradi; quelli di scala 1.01, 1.05, 1.1, 1.25, 1.5 e 2.
- **Enter** conferma. **Esc** annulla e rimette tutto a posto.
- Con il mouse, trascina il tasto sinistro per cambiare il valore e fai clic per metterlo. Con il gamepad, A conferma, B annulla e un tocco di X cambia asse.

## Altre modifiche

- **x** estrude le facce scelte; poi si muovono lungo la normale.
- **j** crea una faccia su 3 o 4 vertici scelti, nell'ordine in cui li hai scelti, rivolta verso la camera. Funziona solo nella modalità vertici.
- **k** fonde i vertici scelti nel loro punto medio; **K** salda i vertici che stanno nello stesso punto (con il flag 4 di `draw3d` le facce intorno sembrano lisce).
- **Del** cancella: nella modalità vertici i vertici e le facce che li usano, nella modalità facce le facce.
- **p** colora, **o** prende un colore, **c** apre la tavolozza (frecce per scegliere, Enter per prenderlo, Esc per chiuderla). Una faccia con una texture della sheet non ha colore.

## Aggiungi una parte

**Maiusc+A** apre un elenco di quattro forme: cubo, cilindro, sfera e piano. Quella che scegli si aggiunge al modello come parte nuova, di larghezza 1 e del colore della pagina. Va al centro dei vertici scelti, oppure allo 0 se non c'è nulla di scelto. I vertici della parte nuova diventano la scelta, quindi puoi spostarli, ruotarli o scalarli subito. Un solo annulla toglie la parte. Se il modello ha uno scheletro, i vertici nuovi seguono l'osso del primo vertice scelto (o il primo osso). Con il gamepad tocca X e scegli **Aggiungi una parte**.

## Specchio dal vivo

**Maiusc+X** accende e spegne lo specchio dal vivo. Quando è acceso, la pagina mostra **MIRROR X**. Sposta, ruota, scala, dipingi e cancella agiscono allora sui vertici o sulle facce scelti e sui loro corrispondenti dall'altra parte del piano X = 0. L'editor trova il corrispondente dalla posizione: il vertice in (-x, y, z), entro 0,001. Un vertice sul piano è il corrispondente di se stesso. Un vertice senza corrispondente cambia da solo. Se scegli entrambi i lati, guida il vertice con il numero più basso e l'altro lo segue. Gli strumenti che aggiungono parti o cambiano la topologia (aggiungi una parte, estrudi, duplica, suddividi) non agiscono sull'altro lato. Un solo annulla riporta indietro entrambi i lati. Con il gamepad tocca X e scegli **Live mirror X on / off**.

## Spigoli e taglio ad anello

In modalità spigoli una scelta è un insieme di spigoli. Gli spigoli scelti si vedono come linee spesse. Sposta, ruota e scala agiscono sui due estremi di ogni spigolo scelto. **x** e **d** agiscono sulle facce di cui tutti gli angoli sono scelti. **Canc** toglie i triangoli accanto agli spigoli scelti, e i vertici che nessun triangolo usa più. Con lo specchio dal vivo acceso, scegliere uno spigolo sceglie anche il suo corrispondente dall'altra parte di X = 0, e sposta, ruota, scala e cancella agiscono su entrambi. Il taglio ad anello non agisce sull'altro lato.

**Ctrl+R** fa un taglio ad anello. Scegli prima uno spigolo (in modalità vertici bastano due vertici che formano uno spigolo). L'editor parte dallo spigolo e attraversa i quadrati intorno al modello. Un quadrato è fatto di due triangoli che guardano dalla stessa parte (l'angolo tra le loro normali è minore di 60 gradi) e che hanno una diagonale in comune. In ogni quadrato attraversa lo spigolo e quello opposto, e continua da quello opposto. Mette un vertice nuovo a metà di ogni spigolo attraversato e taglia ogni quadrato in quattro triangoli. Un vertice nuovo prende l'osso dell'estremo più basso del suo spigolo e la media dei punti della texture (UV). Un triangolo nuovo prende il colore della vecchia faccia che contiene più dei suoi angoli.

L'anello si ferma in due casi. A un bordo aperto (uno spigolo con un solo triangolo) non c'è nulla da attraversare. A un triangolo che non fa parte di un quadrato, il triangolo viene tagliato in due, dal vertice nuovo all'angolo opposto, così non resta nessun buco. L'anello è chiuso se torna al primo spigolo. Se l'anello si attraversasse da solo, o uno spigolo avesse più di due triangoli, non cambia nulla e un messaggio dice il perché.

Alla fine i vertici nuovi sono la scelta (in modalità spigoli, i nuovi spigoli attraverso i quadrati), quindi puoi spostare o scalare l'anello subito. Un solo annulla riporta indietro tutto il taglio. Con il gamepad tocca X e scegli **Loop cut on the chosen edge**.

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
- Il menu (Esc) ha **Save**, **Save as...**, **Try the game (F5)** e **Exit bm Mesh**. Un segno `*` dopo il nome del file in alto significa modifiche non salvate.
- Cambiano solo le sezioni MESH e ANIM e il blocco di bm Mesh nel codice. Sprite sheet, mappa, copertina, suono e il resto del codice restano esattamente come erano.
- Un modello senza vertici o senza facce resta fuori dal file: il messaggio dice quale.
- Una cartuccia contiene al massimo 256 modelli.
- Se hai aperto un gioco, il primo salvataggio chiede la copia modificabile e da lì il file è il `.BME`. Per riavere un gioco, costruiscilo dal progetto nell'SDK.
- Exit e Open ti chiedono di scegliere la stessa voce due volte quando ci sono modifiche non salvate.

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
- Salva spesso: l'editor ricorda 30 passi di annullamento, e aprire o rinominare li azzera.

## Limiti

- Il limite del formato di una mesh è 65535 vertici e 65535 triangoli. La pagina di modifica mostra il conteggio dei vertici rispetto a esso.
- Al massimo 256 modelli in una cartuccia.
- Una mesh G non si può rinominare né cancellare: cambia il codice con bm Code.
- Una copia come codice non conserva lo scheletro.
- L'assistente (F6) aggiunge un modello senza scheletro.
- [DA COMPLETARE: la dimensione di un modello che il Pi Zero W disegna in modo fluido]
