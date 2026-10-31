title: bm Animator: rig e animazioni
accent: Animator
lang: it
kicker: L'editor della suite
subtitle: Dai uno scheletro a un modello 3D, fallo camminare e usalo nel tuo gioco o come sprite pre-renderizzati.
footer: manuali bm - editor-animator

::: readme
**Cos'è.** Il manuale di **bm Animator**, l'editor di scheletri (rig) e animazioni. Ogni tasto citato viene dal codice dell'editor.

**Come si usa.** Leggi una volta "Apertura" e "Lo schermo e i comandi", poi segui i tre tutorial con un modello tuo.

**Cosa manca.** Ogni lacuna è segnata con un'etichetta DA COMPLETARE evidenziata; nulla è inventato.
:::

# Cos'è bm Animator

**bm Animator** mette uno scheletro in un modello 3D, muove le ossa su una timeline e riproduce il risultato. È integrato nella console: lo trovi nella scheda **Dev**.

## Cosa fa

- Riproduce le animazioni di un modello e te le fa controllare (pagina F1).
- Costruisce lo scheletro: ossa, genitori, lati specchiati e la pelle che lega le facce alle ossa (pagina F2).
- Registra le animazioni come keyframe su una timeline a 12 fotogrammi al secondo (pagina F3).
- Disegna un'animazione nello sprite sheet come sprite 2D pre-renderizzati, con hitbox ricavate dalle ossa (pagina F4).

## Come si collega a Studio e Mesh

- **bm Studio** crea i modelli. bm Animator non costruisce modelli: apre quelli del file. Nel suo menu, **Open in bm Studio** salva e passa allo stesso file.
- **bm Mesh** modifica vertici e facce degli stessi modelli. Conserva lo scheletro di un modello, quindi ossa e animazioni restano quelle di bm Animator.
- I modelli stanno nella sezione MESH della cartuccia, scheletri e animazioni nella sezione ANIM. bm Animator cambia solo queste due sezioni (e lo sprite sheet, quando ci metti gli sprite).
- Il gioco usa il risultato con `model()` e `animate()`.

## Quale editor per cosa

- **bm Studio**: modelli statici. Oggetti, props ed edifici, fatti di blocchi e tile. La mappa 3D di un gioco si fa nella pagina mappa di bm SDK.
- **bm Mesh**: modelli organici. Personaggi ed eroi, e tutta la modifica fine di vertici e triangoli.
- **bm Animator**: rig e animazioni. Mette uno scheletro in un modello e lo muove. Non costruisce modelli.
- bm Studio e bm Mesh scrivono la stessa sezione MESH, quindi un modello passa da un editor all'altro.

## Da sapere

- Il Pi Zero W ha bm Animator. Il kernel dell'RGB30 no: non offre i programmi della suite.
- Lavori su un **progetto** (`.bme`), mai su un gioco: vedi "Apertura".

# Apertura

## Tre modi per entrare

- Dalla scheda **Dev** del menu: avvia **bm Animator** e scegli un file.
- Da un gioco: premi **X** sulla sua copertina e scegli **Open in bm Animator**.
- Da un altro strumento della suite: la voce di menu **Open in bm Animator** (ce l'ha bm Studio) passa allo stesso file.

*Open...* elenca anche i file di modello `.bmm` di `/bmm`. Un `.bmm` si apre con tutti i suoi modelli e scheletri. **Save** riscrive lo stesso `.bmm`; **Save as** crea un progetto `.bme`; **Try the game** dice che un file di modello non ha un gioco.

Se l'elenco dei file è vuoto, lo schermo dice `no .bm files: make models in bm Studio`. Se apri un file senza modelli 3D, dice `no models: make them in bm Studio`. Il testo `open a .bm with 3D models (made in bm Studio)` compare quando l'app parte senza file e ci sono dei file `.bm`.

## Progetti e giochi

| File | Cos'è | Cosa ne fa bm Animator |
|---|---|---|
| `.bme` | un progetto | lo apre, lo modifica, lo salva al suo posto |
| `.bm` | un gioco | lo legge soltanto |

Con un gioco aperto, il primo salvataggio chiede `Save an editable copy: name.bme?`. Rispondi sì: da lì lavori sulla copia, in `/bme`. Il gioco non viene mai modificato.

## Da sapere

- L'elenco dei file mostra i progetti `.bme` di `/bme` e i `.bm` e `.bme` di `/carts` e della radice della scheda SD.
- La pagina del menu mostra il nome del file e `*modified*` quando c'è qualcosa da salvare.

## Provalo, passo per passo

1. Accendi la console e apri la scheda **Dev**.
2. Avvia **bm Animator**.
3. Scegli un `.bm` o un `.bme` con un modello dentro.
4. Vedi la pagina **F1** con il modello che gira. Gli elenchi a sinistra sono MODELS e ANIMATIONS, solo con i nomi; il nome del modello con vertici, triangoli e ossa sta nella striscia di informazioni a destra del pannello.

::: try
Premi `]` e `[` per passare al modello successivo e al precedente del file. La riga di stato dice `model` e il nome.
:::

# Lo schermo e i comandi

La disposizione è quella delle altre app della console: a sinistra un pannello con gli elenchi, sopra la vista 3D due righe che dicono cosa c'è e cosa si sta facendo, in basso i tasti della pagina. Le cose scelte sono in giallo. Tieni premuto **F12**, o premi **?**, per vedere i tasti della pagina.

![Il player (F1): un modello con lo scheletro visibile](shots/editor-animator-play.png)

## Pagine e tasti globali

| Tasto | Cosa fa |
|---|---|
| `F1` | play: il player |
| `F2` | rig: ossa e pelle |
| `F3` | animate: keyframe |
| `F4` | sprites: un'animazione disegnata nello sprite sheet |
| `Esc` | il menu (indietro dal menu) |
| `Ctrl+S` | salva |
| `Ctrl+Shift+S` | salva con nome (nome 8.3 in `/carts`) |
| `Ctrl+O` | apri un altro file |
| `Ctrl+I` | importa un modello da `/bmm` o `/glb` |
| `Ctrl+E` | esporta il modello in `/bmm` |
| `F5` o `Ctrl+R` | prova il gioco (salva prima, poi torna alla stessa pagina) |
| `F6` | l'assistente, dove il kernel ce l'ha |
| `Ctrl+Z`, `Ctrl+Y` | annulla, ripeti |
| `[`, `]` | modello precedente, successivo |
| `F12` tenuto, o `?` | mostra i tasti della pagina |
| `+`, `-` | zoom della vista 3D |
| `Alt` + frecce | ruota la camera |

## F1, play

| Tasto | Cosa fa |
|---|---|
| su, giù | modello precedente, successivo |
| sinistra, destra | animazione precedente, successiva |
| spazio o Invio | avvia, ferma |
| `a` `d` `w` `s` | ruota la camera a mano (`a` e `d` fermano la rotazione automatica; `w` e `s` inclinano soltanto) |
| `o` | la camera gira da sola sì, no |
| `k` | mostra lo scheletro |
| `l` | luce sì, no |
| `b` | fondi l'animazione con la successiva: 0, 25, 50, 75 % |
| `,` `.` | un fotogramma indietro, avanti (ferma la riproduzione) |
| `<` `>` | più lento, più veloce (da 1/8 a 4 volte) |
| `z` o `f` | inquadra il modello |

## F2, rig

| Tasto | Cosa fa |
|---|---|
| `n` | crea lo scheletro, o aggiunge un osso sotto quello scelto |
| su, giù | scegli l'osso |
| `Tab` | lavora sulla coda dell'osso o sulla testa |
| `j` | testa sola: **w a s d r f** spostano solo la testa dell'osso, lontano dalla coda del genitore (di nuovo: le giunture vanno insieme) |
| `w` `a` `s` `d` `r` `f` | sposta l'estremità scelta di 1/8 (maiuscolo: 1/32) |
| `m` | specchia l'osso e i suoi figli sull'altro lato |
| `p` | scegli il genitore |
| Invio | rinomina l'osso (fino a 16 lettere) |
| `x` o Canc | elimina l'osso |
| `k` | pelle: ogni faccia segue l'osso più vicino |
| `K` | pelle: ogni angolo segue l'osso più vicino (morbida) |
| `v` | modalità pelle: scegli le facce a mano |
| `h` | mostra solo le facce di questo osso; ancora: con le ossa sotto di lui; ancora: tutte |
| `o` `i` | asse di rotazione x, y, z / passo di rotazione 5, 15, 45, 90 gradi |
| `,` `.` | ruota questo osso e le ossa sotto di lui attorno alla sua testa, un passo indietro o avanti |
| `u` | scrivi i gradi da ruotare |
| `t` | posa di prova: piega ogni osso e segna in rosso le facce che si allungano o si strappano |
| `q` `e` | ruota la vista a scatti |
| `z` | inquadra il modello |

In modalità pelle: le frecce scelgono una faccia, spazio la segna, `c` segna quelle collegate, `b` segna tutte le facce dell'osso, `a` dà le facce segnate all'osso, PgSu e PgGiù cambiano osso, Backspace o Canc cancella i segni, `v` torna alle ossa.

### Parti e angolo di un'arma

Una parte è un osso con le sue facce. **h** mostra una parte da sola: le facce dell'osso scelto, nel colore dell'osso. Premi **h** di nuovo per aggiungere le ossa sotto di lui, e una terza volta per vedere tutto. Serve a controllare quali facce segue un osso.

**, e .** ruotano l'osso scelto e tutte le ossa sotto di lui attorno alla testa dell'osso. Le facce che seguono queste ossa ruotano con loro. Il passo è quello di **i** (5, 15, 45 o 90 gradi) e l'asse è quello di **o**. La pagina li mostra dopo `turn`, per esempio `turn z 15`. **u** chiede i gradi esatti. Un **Ctrl+Z** annulla una rotazione. La rotazione cambia la posa a riposo, quindi ogni animazione usa il nuovo angolo: giri un'arma una sola volta, non in ogni clip. I tasti **[** e **]** cambiano ancora il modello, per questo la rotazione usa la virgola e il punto.

Esempio: il Cacciatore di `dnhunter.bmm` ha la sega su un osso a parte, `grip.R`, figlio di `hand.R`. Scegli `grip.R` con su e giù, premi **o** per l'asse e **.** per ruotare la sega a passi di 15 gradi. Salva con Ctrl+S.

## F3, animate

| Tasto | Cosa fa |
|---|---|
| `n` | nuova animazione |
| PgSu, PgGiù | animazione precedente, successiva |
| su, giù | scegli l'osso |
| sinistra, destra | un fotogramma indietro, avanti |
| Home, Fine | inizio, fine dell'animazione |
| spazio | avvia, ferma |
| `w` `s` | ruota l'osso attorno a x di 15 gradi (maiuscolo: 5) |
| `a` `d` | ruota l'osso attorno a y |
| `q` `e` | ruota l'osso attorno a z |
| `g` | passa da ruotare a spostare l'osso |
| `k` | metti un keyframe qui |
| `x` o Canc | togli il keyframe qui |
| `,` `.` | vai al keyframe precedente, successivo |
| `(` `)` | sposta il keyframe di un fotogramma |
| `Ctrl+C`, `Ctrl+V` | copia, incolla la posa |
| `r` | rimetti l'osso a riposo |
| `m` | specchia la posa |
| `o` | onion skin: le ossa dei keyframe precedente e successivo |
| `l` | loop sì, no |
| `i` | cambia la curva: lineare, morbida, a scatti, entrata lenta, uscita lenta |
| `t` | maniglia IK accesa, spenta (vedi IK a due ossa) |
| `<` `>` | più corta, più lunga di un fotogramma |
| Invio | rinomina l'animazione (fino a 16 lettere) |
| `Ctrl+D` | duplica l'animazione |
| Backspace due volte | elimina l'animazione |
| `Ctrl+Shift+I` | prendi le animazioni di un altro modello |

La barra dei suggerimenti in fondo a F3 mostra solo i tasti principali. I tasti `,` `.` (keyframe prima e dopo), `m` (specchio) e `o` (onion skin) non sono sulla barra. Sono nell'aiuto: tieni premuto **F12**. La barra termina con il chip F12 keys. Fai clic su di esso, oppure tieni premuto F12, per vedere ogni tasto della pagina.

### Animazioni da un altro modello (Ctrl+Shift+I)

Puoi copiare le animazioni di un altro modello sullo scheletro che modifichi. Premi **Ctrl+Shift+I**, oppure apri il menu del tasto destro della pagina animate e scegli *Animations from another model...*. Prima serve uno scheletro (F2). Poi:

1. Un elenco mostra i modelli di `/bmm` e `/glb`. Scegline uno e premi **Invio**.
2. bm Animator abbina le ossa dei due scheletri per nome. Un osso si abbina quando i nomi sono uguali. Altrimenti si abbina quando i nomi sono uguali in minuscolo, senza i segni `.`, `_` e `-`.
3. Ogni animazione dell'altro modello è aggiunta al tuo scheletro, con i suoi tempi, la lunghezza e il loop. Le rotazioni delle ossa abbinate sono copiate. Le traslazioni sono scalate con il rapporto delle due altezze (l'altezza del tuo scheletro divisa per quella dell'altro). Un osso senza abbinamento resta nella posa di riposo.
4. La riga di stato dice, per esempio, `3 animations, 18 bones matched, missing: tail ear`. `missing` elenca le ossa del tuo scheletro che non hanno avuto un abbinamento.

Le nuove animazioni prendono il nome della sorgente. Se il nome è già usato, bm Animator lo rende diverso. La prima nuova animazione è quella scelta. È un solo passo di annullamento. Uno scheletro può avere al massimo 255 animazioni. Se l'altro modello non ha scheletro, o non ha animazioni, o nessun osso con un nome che si abbina, un messaggio lo dice e non si aggiunge niente.

## F4, sprites

| Tasto | Cosa fa |
|---|---|
| su, giù | scegli un'impostazione |
| sinistra, destra | cambiala |
| Invio | disegna gli sprite e mettili nello sheet |

## Gamepad

| Pulsanti | Cosa fanno |
|---|---|
| `Y` + sinistra, destra | cambia pagina |
| `Y` + `B` | il menu |
| `Y` + su, giù | cambia modello |
| `Y` + `A` | annulla |
| F1: `X` + croce direzionale | ruota la camera |
| F2: sinistra, destra | ruota la camera (`X` + sinistra, destra non fa nulla) |
| F3, F4 | nessuna camera sul pad |
| F1: `A` | avvia, ferma |
| F1: `B` | mostra lo scheletro |
| F2: `A` + croce | sposta l'estremità scelta (sinistra, destra, su, giù) |
| F2: `X` + su, giù | sposta l'estremità scelta più vicino, più lontano |
| F2: `B` | testa o coda |
| F3: `A` + croce | ruota l'osso attorno a y (sinistra, destra) e a x (su, giù) |
| F3: `X` + sinistra, destra | ruota l'osso attorno a z |
| F3: `X` + su, giù | animazione precedente, successiva |
| F3: `B` | avvia, ferma |
| `X` da solo | F1: rotazione della camera; F2: nuovo osso; F3: keyframe (o nuova animazione se non ce n'è) |
| F4: `A` | disegna gli sprite |

## Avvertenze

- [DA COMPLETARE: il mouse. La guida della suite dice che Animator non lo usa ancora, ma il codice ha gestori del mouse (clic su un osso, trascinamento per spostare o ruotare, clic sulla timeline); da verificare sulla console]
- In una riga di dialogo (per esempio il nome del file), un clic mette il cursore tra le lettere.

# Ossa e rigging

## Cosa fa

Uno scheletro è un elenco di ossa. Ogni osso ha una testa, una coda e un genitore: quando il genitore ruota, il figlio lo segue. Le facce sono legate alle ossa (la pelle), così quando un osso si muove, le sue facce si muovono con lui.

## Perché conta

Lo scheletro permette a un modello di avere tante animazioni a basso costo: il gioco muove solo le ossa.

## Da sapere

- Il modello deve avere delle facce: costruiscilo prima in bm Studio.
- **n** su un modello vuoto crea un osso, `root`, dal fondo del modello fino a metà della sua altezza. Ogni **n** successivo aggiunge un figlio dell'osso scelto, lungo circa mezza unità nella stessa direzione.
- Al massimo **64** ossa in uno scheletro.
- Le giunture nello stesso punto si muovono insieme: una catena resta unita quando sposti una giuntura.
- Una spalla o un'anca non è all'estremità del genitore. Crea l'osso con **n**, sposta la sua coda, poi scegli la sua testa con **Tab** e premi **j**. Ora **w a s d r f** spostano solo questa testa. Il genitore resta lo stesso, la coda resta ferma e le animazioni conservano le chiavi. Un osso non può essere più corto di 1/16. Premi **j** di nuovo per riunire le giunture. Ogni spostamento è un passo di **Ctrl+Z**.
- **x** elimina un osso: i suoi figli passano al suo genitore e le sue facce seguono il genitore (o il primo osso). Eliminando l'unico osso si toglie lo scheletro.
- **m** copia l'osso e i suoi figli sull'altro lato (x specchiato). I nomi finiscono in `.L` e `.R`; un osso senza lato ancora riceve `.L` se sta sul lato x positivo. Anche le pose delle animazioni vengono specchiate. Le facce sull'altro lato non si muovono da sole: rifai la pelle con **k** o assegnale con **a**.
- Il genitore di un osso si sceglie tra le ossa che lo precedono nell'elenco.

## La pelle

- **k** dà ogni faccia al suo osso più vicino: parti rigide, adatto a robot e personaggi squadrati.
- **K** dà ogni angolo al suo osso più vicino: il modello si allunga alle giunture.
- In modalità pelle (**v**), se hai segnato delle facce, **k** e **K** lavorano solo su quelle.
- **t** prova la pelle. Piega ogni osso a turno di 45 gradi (attorno a x e attorno a z) e segna in rosso le facce che si rompono: una faccia con un lato che cresce oltre 1,6 volte la sua lunghezza (si allunga), o un angolo condiviso con una faccia di un altro osso che si stacca di più del 2% dell'altezza del modello (la pelle si strappa). La riga di stato dice quante facce sono rosse e quale osso, piegato, ne segna di più, oppure che la pelle regge. Premi **t** di nuovo per spegnere la prova. Poi scegli le facce rosse con **v** e dalle a un osso con **a**.
- La pelle automatica (**k**, **K**) spegne la prova.

![La pagina rig (F2): le ossa di un modello e i loro nomi nell'elenco](shots/editor-animator-rig.png)

## Provalo, passo per passo

1. Apri un modello senza scheletro. Vai su **F2**. Il pannello dice `NO SKELETON`.
2. Premi **n**. La riga di stato dice `a skeleton: one bone, root`.
3. Premi di nuovo **n**. Un nuovo osso appare alla coda di `root` e la coda è l'estremità che sposti.
4. Premi **d** e **w** alcune volte per posizionare la sua coda. Ogni pressione la sposta di 1/8.
5. Premi **Invio**, scrivi un nome come `arm.L` e premi Invio.
6. Premi **k**. La riga di stato dice `every face follows its nearest bone`.

::: try
Scegli `arm.L` e premi **m**. La riga di stato dice `2 bones mirrored` se il braccio ha un figlio, e un osso `arm.R` appare sull'altro lato. Premi di nuovo **k** per dare le facce alle nuove ossa.
:::

# Keyframe e clip

## Cosa fa

Un'animazione (una clip) è un insieme di keyframe su una timeline: un keyframe memorizza la posa di tutte le ossa in un istante, e bm Animator riempie ciò che sta tra due keyframe.

## Perché conta

Un ciclo di camminata sono poche pose. Tu fai le pose, bm Animator fa il movimento.

## Da sapere

- La timeline va a **12 fotogrammi al secondo**.
- Una nuova animazione (**n**) si chiama `anim1`, `anim2`... Dura 1 secondo (12 fotogrammi), fa il loop, e ha un keyframe all'inizio con la posa a riposo.
- **Ogni rotazione di un osso è un keyframe** all'istante corrente: ruota l'osso e il keyframe c'è.
- **g** fa passare gli stessi tasti da ruotare a spostare l'osso (un piccolo passo; nient'altro cambia).
- **Loop** attivo: dopo l'ultimo keyframe l'animazione prosegue verso il primo, sul resto della durata. Un'animazione in loop non ha bisogno di un keyframe all'ultimo fotogramma.
- **i** sceglie come va il movimento tra due keyframe. Ogni pressione passa al successivo di cinque modi: lineare, morbido (lento a entrambe le estremità), a scatti (la posa salta), entrata lenta (ease-in: parte piano) e uscita lenta (ease-out: arriva piano).
- **Attenzione:** un kernel precedente a ease-in ed ease-out rifiuta una cartuccia che ha un'animazione con uno di questi due modi. Mostra il messaggio `bad skeletons (ANIM)` e la cartuccia non si carica. Aggiorna il kernel, oppure resta su lineare, morbido e a scatti.
- **<** e **>** accorciano e allungano l'animazione di un fotogramma. Non può diventare più corta del suo ultimo keyframe.
- `,` e `.` saltano tra i keyframe. `(` e `)` spostano di un fotogramma il keyframe su cui sei.
- L'unico keyframe rimasto di un'animazione non si può togliere (`the last keyframe stays`).
- Premere **Backspace** una volta chiede conferma; la seconda pressione elimina l'animazione.
- Al massimo **255** animazioni per modello.

## IK a due ossa

Con la maniglia IK non giri gli ossi uno per uno. Sposti la mano o il piede, e bm Animator gira gli ossi sopra di essa.

- Scegli un osso che ha un genitore e un nonno (una mano, un piede) e premi **t**. La maniglia sta sulla testa dell'osso scelto. Una croce mostra il bersaglio. La voce di menu è `IK handle`; con la maniglia accesa diventa `IK off`.
- Se l'osso non ha genitore e nonno, un messaggio lo dice e la maniglia resta spenta.
- **w s a d q e** ora spostano il bersaglio, non l'osso. I tasti maiuscoli fanno un passo di un terzo. Anche il gamepad e il trascinamento del mouse spostano il bersaglio allo stesso modo.
- Dopo ogni spostamento, bm Animator mette i keyframe sul genitore e sul nonno al tempo corrente. L'osso scelto mantiene la sua rotazione nel mondo. Il ginocchio o il gomito si piega verso la piega che ha la posa di riposo.
- Se il bersaglio è troppo lontano, l'arto va dritto verso di esso e non si allunga. Lo schermo mostra `OUT OF REACH`.
- Ogni spostamento è un passo di annullamento. Il risultato sono normali keyframe di rotazione, quindi l'animazione gira uguale ovunque.
- Il bersaglio non viene salvato. Premi di nuovo **t** per spegnere la maniglia.
- Il tasto **i** resta il tasto della curva. La maniglia IK sta su **t**.

## Onion skin

**o** mostra le ossa del keyframe precedente e di quello successivo accanto alla posa corrente, in due colori. Usala per vedere da dove viene il movimento e dove va.

![La pagina animate (F3): la timeline con i keyframe e l'elenco delle animazioni](shots/editor-animator-animate.png)

## Provalo, passo per passo

1. Vai su **F3** con un modello che ha uno scheletro. Se il pannello dice `NO SKELETON`, creane uno prima in F2.
2. Premi **n**. La riga di stato dice `new animation anim1`.
3. Scegli un osso con su e giù.
4. Premi **w** due volte. L'osso ruota e sulla timeline appare un rombo: è un keyframe al fotogramma 0.
5. Premi **destra** sei volte per andare al fotogramma 6, poi **s** quattro volte per ruotare l'osso nell'altro verso. Appare un secondo rombo.
6. Premi **spazio**. L'animazione parte e fa il loop.
7. Premi **Ctrl+S** per salvare.

::: try
Premi **o** e avanza con `,` e `.`: le ossa colorate mostrano le pose prima e dopo.
:::

# Hitbox e bake degli sprite (F4)

## Cosa fa

Per i giochi 2D, la pagina **sprites** disegna un'animazione con il motore 3D della console e mette i fotogrammi nello **sprite sheet**, pronti per `zspr()`. Può anche creare i **box** di ogni fotogramma dalle ossa.

![La pagina sprites (F4): le impostazioni e l'anteprima con i suoi box](shots/editor-animator-sprites.png)

## Le impostazioni

Su e giù scelgono una riga, sinistra e destra la cambiano.

| Riga | Valori |
|---|---|
| animation | una delle animazioni del modello (o la posa a riposo se non ne ha) |
| frames | da 1 a 16 |
| size | 16, 24, 32, 48, 64, 96 o 128 pixel |
| directions | 1, 2, 4 o 8 |
| from above | da 0 a 75 gradi, a passi di 15 |
| camera | piatta o in prospettiva |
| light | accesa, o spenta (colori piatti) |
| outline | scuro o nessuno |
| colours | tutti, 32, 16 o 8 |
| fill | dal 50 % al 160 %: quanta parte del fotogramma riempie il modello |
| hitboxes | dalle ossa, o nessuna |

L'anteprima a destra gira tra le direzioni mentre l'animazione procede, alla sua dimensione, e anche al doppio quando la dimensione è 64 o meno. Sotto le impostazioni lo schermo dice quanti pixel occuperanno gli sprite nello sheet.

## Le hitbox delle ossa

Con **hitboxes** attivo, ogni osso diventa un box in ogni fotogramma: un rettangolo attorno alla testa e alla coda dell'osso come disegnato in quel fotogramma, con un piccolo margine. Il tipo del box dipende dal **nome dell'osso**:

| Il nome contiene | Tipo | Contorno nell'anteprima |
|---|---|---|
| `hit`, `weapon`, `punch`, `fist`, `sword`, `blade`, `spear`, `attack` | hit: dove fa male | rosso |
| `root`, `body`, `hips`, `pelvis` | body: lo spazio che occupa il corpo | verde |
| qualsiasi altro | hurt: dove può essere colpito | blu |

Dai alle ossa nomi pensati per questo: un osso chiamato `sword` crea una hitbox, un osso chiamato `arm.L` crea una hurtbox.

## Mettere gli sprite nello sheet

Premi **Invio**. Lo schermo dice `drawing the sprites...` mentre lavora, a tappe: disegno, contorno, colori, poi inserimento nello sheet.

- Gli sprite vanno **sotto ciò che lo sheet ha già**, allargando lo sheet se serve (mai oltre 4096 pixel).
- Una riga di fotogrammi per direzione: il fotogramma `f` della direzione `d` sta a `x0 + f * size`, `y0 + d * size`.
- La pagina aggiunge allo sheet una **zona**, col nome del modello e dell'animazione in minuscolo (`model_animation`, fino a 16 caratteri), con il numero di fotogrammi e la velocità (fotogrammi divisi per la durata dell'animazione). Una zona con lo stesso nome viene sostituita. Se era una zona stack, conserva le sue fette quando la nuova zona sta ancora nello sheet.
- I box sono creati nella sessione dell'editor (sezione BOXES della cartuccia), per la zona, fotogramma per fotogramma (dal fotogramma 1), presi dalla prima direzione. Non è confermato che Ctrl+S li scriva nel file.
- Una riga in basso mostra la zona e la chiamata `sspr()` per disegnare i fotogrammi.

## Da sapere

- La dimensione totale deve restare sotto 512 x 512 pixel (fotogrammi per dimensione per direzioni per dimensione). Se non è così, lo schermo dice `too many, fewer frames or a smaller size`.
- Con un'animazione in loop i fotogrammi sono distribuiti sulla durata senza ripetere il primo alla fine; senza loop, l'ultimo fotogramma è l'ultima posa.
- Ctrl+Z annulla gli sprite messi nello sheet.

## Provalo, passo per passo

1. Vai su **F4** con un modello che ha un'animazione.
2. Scegli l'animazione nella prima riga, `frames` 8, `size` 48, `directions` 4.
3. Controlla che `hitboxes` dica `from bones`. L'anteprima mostra i box con i loro contorni colorati.
4. Premi **Invio**.
5. Aspetta il messaggio `N sprites[ and M boxes] put in sheet at x,y[ (the sheet is bigger now)]`; la posizione lo conclude solo se lo sheet non è cresciuto.
6. Premi **Ctrl+S** per salvare.

::: try
Rinomina un osso in `sword` in F2, rifai il bake e guarda l'anteprima: il suo box diventa rosso.
:::

# Salvataggio ed esportazione

## Cosa fa

**Ctrl+S** scrive il tuo lavoro nel file del progetto. Cambiano solo le sezioni MESH e ANIM (e lo sprite sheet, se ci hai messo degli sprite). Non è confermato se vengano scritte anche le sezioni SPRITES e BOXES (le zone e i box creati nella sessione). Codice del gioco, mappa, copertina e suoni restano byte per byte.

## Da sapere

- Un file aperto e salvato senza modifiche resta identico.
- **Salva con nome** (`Ctrl+Shift+S`) chiede un nome 8.3; il file va in `/bme` con l'estensione `.BME`.
- Un gioco (`.bm`) non viene mai sovrascritto: il primo salvataggio crea una copia `.BME`.
- **F5** salva prima e avvia il progetto. Start + Select (o Ctrl+Esc) esce dal gioco e ti riporta alla stessa pagina.
- Per creare il gioco finito usa **bm SDK** (Build, `Ctrl+B`): scrive il `.bm`. Il menu di bm Animator non ha una voce di build.
- Quando l'animazione è per un gioco tenuto nel repository, modelli e scheletri vengono salvati dalla console e messi in `carts/<game>/` come `models.bm`; `make` li porta nella cartuccia.

## Provalo, passo per passo

1. Premi **Esc**. Il menu elenca Continue, Open, Save, Save as, Try the game, Open in bm Studio ed Exit.
2. Scegli **Save as...** e scrivi un nome come `HERO`.
3. La riga di stato dice `saved` e il percorso. Il titolo del menu mostra il file senza `*modified*`.

## Importare un .glb

La scheda SD ha due cartelle per i modelli. `/glb` contiene i file `.glb` che copi lì; bm Animator li legge soltanto. `/bmm` contiene i modelli convertiti (`.bmm`); bm Animator li scrive lì. Tutti i nomi sono in minuscolo.

1. Premi **Ctrl+I**, oppure scegli **Import a model...** nel menu (Esc). Sul pad, premi Y + B per il menu.
2. L'elenco mostra `/bmm` e `/glb`. Un nome segnato `.glb: converted to /bmm` non ha ancora un `.bmm`.
3. Scegli un `.bmm` per leggerlo. Scegli un `.glb` per convertirlo: bm Animator scrive `/bmm/NAME.bmm` e aggiunge il modello al progetto.
4. Se il nome ha più di 8 caratteri, o `/bmm` ha già quel nome, bm Animator chiede un nuovo nome (da 1 a 8 tra a-z, 0-9, `_` e `-`).
5. La riga di stato mostra i vertici e i triangoli. Se il file non va bene, mostra il messaggio della console, per esempio il limite del formato di 65535 vertici o triangoli.

Un `.glb` dà colori piatti e nessuno scheletro: creane uno nella pagina rig (F2). Un `.bmm` con uno scheletro lo conserva, e le sue ossa compaiono nella riga di stato. La texture di un `.bmm` va sullo sheet del progetto solo quando lo sheet è vuoto; altrimenti le facce che la usavano diventano grigie. Un `.bmm` con più modelli dà il primo.

**Ctrl+E**, o **Export to /bmm...** nel menu, scrive il modello corrente in `/bmm/NAME.bmm` con scheletro e animazioni; se il file c'è già, bm Animator chiede prima di sostituirlo.

# Usare l'animazione in un gioco

Tutte queste chiamate sono nell'API 3D (`docs/api/light3d.md`). L'animazione fa parte della cartuccia: nessun file in più.

## Il modello 3D

| Chiamata | Cosa fa |
|---|---|
| `models()` | i nomi dei modelli della cartuccia |
| `model(name)` | il modello come mesh; fallo in `_init` |
| `clips(m)` | le animazioni di un modello: `{ {name=, length=, loop=}, ... }` |
| `animate(m, [anim, t, anim2, t2, k, bone])` | mette il modello nella posa dell'animazione `anim` (nome o numero) al tempo `t` in secondi; con `anim2, t2, k` fonde due animazioni; restituisce la durata |
| `draw3d(m, x, y, z, [rx, ry, rz, scale, flags])` | disegna il modello in posa |
| `bones3d(m)` | i nomi delle ossa |
| `bone3d(m, bone)` | dove stanno la testa e la coda di un osso nell'ultima posa: mani, la punta di una spada, una luce |
| `bone_turn(m, bone, [rx, ry, rz])` | ruota un osso sopra l'animazione: per mirare |
| `hit3d(m, x, y, z, ry, scale, ox, oy, oz, dx, dy, dz, [maxd])` | un raggio contro le ossa nell'ultima posa; restituisce la distanza e l'osso |

`animate` costa solo le ossa. Chiamala prima di `draw3d`, a ogni fotogramma.

## Gli sprite 2D

| Chiamata | Cosa fa |
|---|---|
| `zspr(name, x, y, [frame, flip_x, flip_y, zoom])` | disegna la zona creata da F4; senza `frame` va da sola alla velocità della zona |
| `zone(name)` | `x, y, w, h, frames, fps, slices` della zona; `slices` è 0 se non è una zona stack |
| `zboxes(name, [frame, kind])` | i box della zona: `{x, y, w, h, kind, frame}`, con `kind` `"hurt"`, `"hit"` o `"body"` |

`zspr` disegna la prima direzione. Per le altre direzioni usa `sspr()` con i numeri della riga in basso in F4.

## Codice da copiare

::: copy
local v, t = nil, 0
function _init()
  v = model("villager")
end

function _update()
  t = t + 1 / 60
end

function _draw()
  cls(1)
  zclear()
  camera3d(0, 1, -4)
  animate(v, "walk", t)
  draw3d(v, 0, 0, 0, 0, 0, 0, 1)
end
:::

È lo schema di `carts/overbit` del repository. Per gli sprite 2D:

::: copy
zspr("hero_walk", 100, 80)
for _, b in ipairs(zboxes("hero_walk", 3, "hit")) do
  rect(100 + b.x, 80 + b.y, b.w, b.h, 8)
end
:::

# Tutorial

## Fare il rig di un modello

1. Apri un `.bme` con un modello bipede (fatto in bm Studio) e vai su **F2**.
2. Premi **n**: appare `root`, dal fondo del modello fino a metà della sua altezza.
3. Sposta la coda di `root` fino al bacino con **w** o **s** (ogni pressione è 1/8).
4. Premi **n** per aggiungere un osso, chiamalo `leg.L` con Invio, e metti la sua coda sul piede sinistro con **s**, **a** e **d**.
5. Premi **m** per creare `leg.R` sull'altro lato.
6. Aggiungi braccia e testa allo stesso modo: `arm.L`, `arm.R`, `head`.
7. Premi **k**: ogni faccia segue l'osso più vicino. Premi invece **K** se vuoi giunture morbide.
8. Vai su **F1** e premi **k**: lo scheletro è disegnato sopra il modello.

::: warn
Se una parte del modello segue l'osso sbagliato, torna in F2, premi **v**, scegli le sue facce con spazio, scegli l'osso giusto con PgSu e PgGiù, e premi **a**.
:::

## Fare un ciclo di camminata

1. In **F3** premi **n**: `anim1` con 12 fotogrammi e loop attivo. Premi Invio e rinominala `walk`.
2. Scegli `leg.L` con su e giù. Premi **w** due volte: la gamba ruota attorno a x di 30 gradi.
3. Scegli `leg.R`. Premi **s** due volte: la gamba ruota nell'altro verso. Fai lo stesso con `arm.L` e `arm.R`, le braccia contro le gambe. È la prima posa, al fotogramma 0.
4. Premi **destra** sei volte per andare al fotogramma 6.
5. Premi **m**: la posa viene specchiata, con i lati sinistro e destro scambiati. È la seconda posa.
6. Premi **spazio**: l'animazione fa il loop, passando dalla seconda posa alla prima sugli altri sei fotogrammi.
7. Premi **i** finché la riga in alto dice `smooth`. Premi **o** per controllare le pose con l'onion skin.
8. Premi **Ctrl+S**.

::: try
Allunga l'animazione con `>` per rallentare la camminata, accorciala con `<` per accelerarla.
:::

## Riprodurla in un gioco

1. In bm SDK apri il progetto e vai alla pagina 3D (F4): il modello gira con le sue animazioni.
2. Premi **i** lì: l'SDK scrive nel codice le righe per caricare il modello e per disegnarlo con `animate` e `draw3d`.
3. Premi **F5** per provare il gioco. Il modello cammina.
4. Cambia l'animazione per nome: `animate(v, "walk", t)`, con `t` il tempo in secondi.
5. Per passare da un'animazione all'altra con dolcezza, indica due animazioni e una fusione: `animate(v, "walk", t, "wave", t, k)`, con `k` che va da 0 a 1.
6. Premi Start + Select per uscire dal gioco e tornare all'editor.

::: warn
`animate` dà errore se il modello non ha uno scheletro o se il nome dell'animazione non c'è. Controlla con `clips(v)`.
:::

# Consigli e limiti

## Consigli

- Dai alle ossa i nomi giusti per il compito: `.L` e `.R` per i due lati, `sword` o `fist` per le hitbox di un attacco, `root`, `body`, `hips` per il corpo.
- Fai la posa ai due estremi di un movimento e lascia che il loop riempia il resto: una camminata richiede solo due pose.
- Usa l'onion skin quando un movimento sembra sbagliato.
- Spegni la luce in F4 (colori piatti) e usa 16 o 8 colori per un aspetto disegnato a mano.
- Tieni il modello piccolo: la console disegna circa 1200 triangoli a 60 fotogrammi al secondo.

## Limiti

| Cosa | Limite |
|---|---|
| ossa in uno scheletro | 64 |
| animazioni in un modello | 255 |
| lunghezza del nome di un osso o di un'animazione | 16 lettere |
| timeline | 12 fotogrammi al secondo |
| fotogrammi in un'animazione di sprite | 16 |
| dimensione degli sprite | da 16 a 128 pixel |
| sprite in un bake | 512 x 512 pixel in tutto |
| lo sprite sheet | 4096 x 4096 pixel |
| skinning | ogni angolo segue un solo osso (niente pesi misti) |

## Cosa bm Animator non fa

- Non ci sono marcatori di evento sulla timeline (un fotogramma di colpo, un suono): il gioco deve contare i fotogrammi da sé.
- Non ci sono punti di aggancio (socket): la posizione 2D di una mano o di una volata non viene esportata con gli sprite. In 3D usa `bone3d()`.
- Gli sprite sono disegnati come un blocco unico: non ci sono livelli separati per armature o armi.
- I nomi degli sprite non hanno suffissi di direzione: una zona per animazione, le direzioni in righe.

## Avvertenze

- [DA COMPLETARE: quanti passi di annullamento vengono conservati]
- [DA COMPLETARE: quanto dura un bake sulla console con 8 direzioni a 128 pixel; non ancora misurato]
