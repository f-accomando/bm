title: bm Studio: modellazione 3D
accent: modellazione 3D
lang: it
kicker: Un manuale degli editor bm
subtitle: Costruisci i modelli 3D del tuo gioco sulla console, con tessere e blocchi, e usali nel tuo codice.
footer: manuali bm - bm Studio

::: readme
**Cos'è.** Il manuale di bm Studio, il programma della console che crea i modelli 3D di un gioco bm. È scritto a partire dal codice e dai documenti del programma stesso.

**Come usarlo.** Leggi una volta i primi tre capitoli, poi segui i tutorial con la console in mano. Tutto ciò che sta in un riquadro con il bordo e l'ombra va copiato così com'è.

**Cosa manca.** Ogni lacuna è segnata con un'etichetta DA COMPLETARE evidenziata; niente è inventato.
:::

# Cos'è bm Studio

bm Studio costruisce modelli 3D con blocchi e tessere, direttamente sulla console. Metti un cubo, incolli un pezzo del tuo sprite sheet sulle sue pareti, sposti un angolo per fare un tetto, e il modello è pronto per essere disegnato dal tuo gioco con `draw3d()`.

## A cosa si collega

- **Il progetto.** bm Studio legge e scrive la sezione MESH del tuo progetto (un file `.bme`). Non tocca il codice del gioco, la mappa né le sezioni che non conosce.
- **bm Animator.** Scheletri e animazioni si fanno lì. Nel menu di bm Studio, *Open in bm Animator* apre lo stesso file in quel programma.
- **bm Mesh.** L'editor analitico di vertici e triangoli, comprese le mesh che costruisce il tuo codice. Lavora sullo stesso file.
- **bm Pixel e lo sprite sheet.** Le texture delle facce sono pezzi dello sprite sheet del progetto. Lo disegna bm Pixel.
- **bm SDK.** Il centro del progetto. Nella sua pagina F1 l'elenco *OPEN IN* ha bm Studio come numero 3, e il menu di bm Studio ha *Back to bm SDK* quando è stato aperto dall'SDK.

## Quale editor per cosa

- **bm Studio**: modelli statici. Oggetti, props ed edifici, fatti di blocchi e tile. La mappa 3D di un gioco si fa nella pagina mappa di bm SDK.
- **bm Mesh**: modelli organici. Personaggi ed eroi, e tutta la modifica fine di vertici e triangoli.
- **bm Animator**: rig e animazioni. Mette uno scheletro in un modello e lo muove. Non costruisce modelli.
- bm Studio e bm Mesh scrivono la stessa sezione MESH, quindi un modello passa da un editor all'altro.

## Da sapere

- bm Studio è una cartuccia inclusa nel kernel. La trovi nella scheda **Dev** del menu.
- Lo usi con la tastiera o con il gamepad (Bluetooth o USB). Anche il mouse funziona, quando ce n'è uno collegato.
- Tieni premuto **F12**, oppure premi **?**, e sullo schermo compaiono i tasti della pagina in cui sei.

# Aprire un progetto o un gioco

bm Studio modifica **progetti** (`.bme`). Un **gioco** (`.bm` o `.b16`) è solo letto: puoi aprirlo per guardare i suoi modelli, e il primo salvataggio crea una copia modificabile.

## Modi per iniziare

| Da | Cosa fai |
|---|---|
| Il menu | Apri bm Studio nella scheda **Dev** |
| Un gioco nel menu | **X** sulla sua copertina, poi *Open in bm Studio* |
| bm SDK | Pagina F1, *OPEN IN*, numero 3 |
| bm Animator | Nel suo menu, *Open in bm Studio* |

## Dentro bm Studio

- Premi **Esc** per arrivare al menu. *Open...* elenca i progetti `.bme` di `/bme` e i file `.bm` e `.bme` di `/carts` e della radice della scheda SD. Scorciatoia: **Ctrl+O**.
- *New project* (**Ctrl+N**) avvia un progetto vuoto. Porta con sé le tessere iniziali di bm Studio nello sprite sheet.
- Un nuovo progetto non è ancora salvato: il menu mostra `(not saved yet)` accanto al nome. Salvalo con **Ctrl+S**.

## Avvertenze

- Aprendo un gioco leggi `a game: saving makes its copy`. Il primo salvataggio chiede `Save an editable copy: NAME.BME?`; da lì in poi lavori sulla copia.
- Un gioco senza modelli può riceverne: costruiscili e salva.
- Se il menu mostra `*modified*`, ci sono modifiche non salvate. *Open...*, *New project* ed *Exit* chiedono conferma prima di buttarle via.

# Lo schermo e la vista 3D

bm Studio ha due pagine, scelte con i tasti F (o **Y + sinistra/destra** sul gamepad): **F1 build** e **F2 models**. **Esc** (sul gamepad **Y + B**) apre il menu.

## La pagina build

- A sinistra, un pannello: i cinque strumenti, il pennello (una tessera o un colore, con lo sheet intorno) e il modello (facce, triangoli, vertici).
- Sopra la vista, due righe dicono cosa c'è e cosa si sta facendo.
- In basso, i tasti del momento.
- Il cursore è un riquadro a forma di cella. La griglia sul pavimento è di una unità per quadretto.

![La pagina build: gli strumenti a sinistra, il modello al centro](shots/editor-studio-build.png)

## I comandi della vista 3D

| Tasto | Gamepad | Cosa fa |
|---|---|---|
| `q` / `e` | X + sinistra / destra | Gira la vista di 45 gradi |
| `w` / `s` | - | Inclina la vista |
| `Alt` + sinistra / destra | - | Gira la vista di 45 gradi (pagina build) |
| `Alt` + su / giù | - | Inclina la vista |
| `+` / `-` | X + A / X + B nella pagina models; nessuno nella pagina build | Ingrandisce / rimpicciolisce |
| `z`, `0` o `Home` | - | Inquadra tutto il modello |
| `v` | - | Cambia l'aspetto: light, flat, wireframe |
| `b` | - | Mostra le facce viste da dietro, più scure |
| `g` | - | Mostra o nasconde la griglia (strumenti block e tile) |
| `F12` o `?` tenuti premuti | - | Mostrano i tasti della pagina |

Con il mouse: nella vista, il tasto destro trascinato gira e inclina, il tasto centrale trascinato sposta, la rotella ingrandisce. Il puntatore è il cursore: il tasto sinistro fa quello che fa lo spazio.

## Da sapere

- Il movimento del cursore è sempre relativo alla vista. Dopo aver girato la vista, su significa ancora "lontano da te".
- La riga di stato mostra `model 1/3: house`: il modello in cui sei, su quanti.
- Premi **[** o **]** per passare al modello precedente o successivo da qualsiasi pagina.
- **Ctrl+Z** annulla, **Ctrl+Y** ripete.

# Costruire: tessere e blocchi

I primi due strumenti mettono qualcosa in una cella. Si scelgono con **1** (block) e **2** (tile).

## Lo strumento block

Un blocco è un cubo di una unità. Due blocchi affiancati non hanno una parete in mezzo, e togliendone uno ricompare la parete del vicino.

| Tasto | Cosa fa |
|---|---|
| frecce | Muovono il cursore sul piano del pavimento |
| `PgUp` / `PgDn` | Muovono il cursore di un livello su / giù |
| `Space` | Mette un blocco |
| `Backspace` o `Del` | Toglie il blocco |

Sul gamepad: **A** mette, **B** toglie, **X + su/giù** cambia livello.

## Lo strumento tile

Una tessera va su un lato della cella, non su un cubo intero. Usala per pavimenti, pareti singole, porte e finestre.

- Premi **f** per scegliere il lato a rotazione: pavimento, parete in fondo, parete destra, parete vicina, parete sinistra, soffitto. Sul gamepad, tocca **X**.
- **Space** mette la tessera, **Backspace** toglie ciò che sta su quel lato.

## Il pennello: tessere e colori

Premi **Tab** (sul gamepad **Y**) per aprire lo sheet.

| Tasto | Cosa fa |
|---|---|
| frecce | Si muovono sullo sheet |
| `z` | Dimensione della tessera: 8, 16 o 32 pixel |
| `a` / `d` | Selezione più stretta / più larga |
| `w` / `s` | Selezione più bassa / più alta |
| `c` | Passa tra le tessere e la tavolozza dei colori |
| `Enter` | La prende come pennello |
| `Tab` | Chiude senza cambiare |

- Una selezione di più tessere si mette in un colpo solo: una porta alta due tessere è un solo inserimento.
- Fuori dallo sheet, **r** ruota il pennello di un quarto di giro solo con gli strumenti 1-2 (con lo strumento 3, selezione, **r** ruota le facce scelte), e **h** specchia il pennello con qualsiasi strumento. **x** prende la tessera o il colore dalla faccia sotto il cursore.
- Con un colore come pennello, le facce prendono un colore piatto e nessuna texture.

![Il selettore di tessere: lo sheet, la selezione e la tessera ingrandita](shots/editor-studio-tiles.png)

## Selezionare e spostare

### Select (strumento 3)

Le frecce portano il puntatore sulla faccia più vicina in quella direzione sullo schermo.

| Tasto | Cosa fa |
|---|---|
| `Space` | Sceglie o rilascia la faccia |
| `a` | Tutte le facce, oppure nessuna se alcune sono scelte |
| `c` | Tutte le facce unite a quelle scelte: un intero oggetto |
| `g` | Sposta (vedi sotto) |
| `r` / `t` / `m` | Ruota / capovolge / specchia |
| `n` | Mostra l'altro lato della faccia |
| `Enter` | Mette sulle facce la tessera del pennello |
| `u` | Ruota la texture |
| `,` / `.` | Grande la metà / grande il doppio |
| `d` | Copia e sposta la copia |
| `Del` | Cancella le facce |
| `o` | Sposta le facce in un nuovo modello |

### Move (g)

Le frecce spostano, `PgUp` / `PgDn` salgono e scendono. **Tab** cambia il passo, da 1 fino a 1/16. **Enter** lascia le facce dove sono; **Esc** le rimette dov'erano.

### Vertex (strumento 4)

Il puntatore va sugli angoli. **Space** sceglie, **a** tutti o nessuno, **g** li sposta (tetti, rampe, forme libere), **m** unisce più angoli in uno.

# Dipingere un modello

bm Studio dipinge **i pixel della tessera di una faccia**, non i colori agli angoli. Non esiste il colore per vertice: una faccia è o una tessera dello sheet o un solo colore piatto.

## Cosa fa

Con lo strumento **5** (paint) scelto, **Enter** o **Spazio** apre la tessera della faccia sotto il puntatore, grande nel pannello. La dipingi pixel per pixel mentre il modello nella vista cambia. Lo sheet dipinto viene salvato con il modello.

## Da sapere

- I pixel dipinti stanno nello sprite sheet del progetto. Una tessera usata da più facce cambia su tutte.
- Una faccia con un colore piatto non si dipinge pixel per pixel: **Enter** le dà il colore di pittura in un colpo solo.
- Lo sheet è lo stesso che usano bm Pixel e il tuo gioco. Se il tuo gioco lo cambia con `sset`, cambiano anche i modelli.

| Tasto | Cosa fa |
|---|---|
| frecce | Muovono il cursore del pixel |
| `Space` | Dipinge il pixel |
| `e` | Cancella il pixel (trasparente) |
| `i` | Prende il colore del pixel sotto il cursore |
| `c` | Sceglie un colore: la tavolozza, poi i colori già presenti nella tessera |
| `Esc` o `Enter` | Chiude il pannello di pittura |

Con il mouse, tieni premuto il tasto sinistro sopra i pixel e trascina.

![Il pannello paint: la tessera di una faccia, pixel per pixel](shots/editor-studio-paint.png)

# La pagina models e la riduzione delle mesh

Premi **F2**. L'elenco a sinistra contiene i modelli del file. Sopra la vista, tre righe mostrano il nome, i conteggi (facce, triangoli, vertici) e lo scheletro (`N bones, N animations`) oppure `no skeleton (bm Animator makes one)`. Un puntino blu segna un modello con scheletro.

## Tasti

| Tasto | Cosa fa |
|---|---|
| su / giù | Sceglie il modello |
| `Enter` | Lo costruisce (va alla pagina build) |
| `n` | Nuovo modello |
| `r` | Rinomina (fino a 16 lettere) |
| `d` | Duplica |
| `Del` | Cancella (premi due volte) |
| `PgUp` / `PgDn` | Cambia l'ordine |
| `-` | Riduce i triangoli |
| `m` | Crea un modello da un'immagine |
| `i` | Margine della texture: 0, 0.125, 0.25, 0.5 pixel |

![La pagina models: l'elenco, i conteggi e il modello che gira](shots/editor-studio-models.png)

## Ridurre i triangoli

Premi `-`. La finestra chiede i triangoli voluti e propone la metà. Il riduttore mantiene bordi, linee di colore e cuciture delle texture, e lo scheletro segue i vertici. **Ctrl+Z** annulla.

## Perché conta

La console disegna circa 1200 triangoli in una scena a 60 fps. Oltre, i conteggi diventano un avviso: `heavy for 60 fps (1200 triangles a scene)`.

## Avvertenze

- Se chiedi più triangoli di quelli che ha il modello, leggi `it has N triangles already`.
- A volte il modello resta sopra il numero chiesto: il messaggio aggiunge `(no further without turning faces over)`.
- Nella pagina models il tasto `-` riduce, quindi per ingrandire usa `+` o la rotella del mouse.

## Un modello da un'immagine

`m` (o *Model from picture...* nel menu) crea un modello da un'immagine nella cartella `pics/` della SD o nella radice della SD (`.png` o `.jpg`). Ci sono tre modi: **cutout** (una figura di carta con spessore), **lathe** (mezzo contorno fatto girare attorno all'asse verticale: vasi, torri) e **meshy.ai** (un servizio online che richiede la rete e un account).

# Salvare ed esportare

| Tasto | Cosa fa |
|---|---|
| `Ctrl+S` | Salva sul posto |
| `Ctrl+Shift+S` | Salva con nome: un nome in forma 8.3, in `/bme`, che finisce in `.BME` |
| `F5` | Salva, poi prova il gioco |

## Cosa viene salvato

- I modelli vanno nella sezione MESH del file, insieme allo sprite sheet se l'hai dipinto. Gli scheletri dei modelli vengono salvati con essi.
- Il menu permette di impostare titolo e autore della cartuccia.
- Un nuovo progetto nasce con un piccolo programma di visualizzazione: mostra i modelli, così F5 funziona subito.

## File di risorsa

Il formato di risorsa per i modelli è `.bmm`: MESH, ANIM se animato e uno sheet con solo le celle usate dalle texture. Sul PC, `scripts/bmres.py extract GAME.bm --model villager -o VILLAGER.bmm` ne ricava uno da un gioco.

[DA COMPLETARE: salvare e aprire file `.bmm` dal bm Studio della console; i documenti sulle risorse dicono che è previsto, il programma salva solo il progetto]

## Per un gioco costruito dal repository

Un `.bm` con i modelli e gli scheletri, salvato da bm Studio, può essere messo come `models.bm` in `carts/<game>/`. `make` lo inserisce nella cartuccia con `mkbm.py --models`. Se il gioco non ha un `sheet.png`, lo sheet è quello di `models.bm`.

# Usare il modello in un gioco

Le funzioni qui sotto sono documentate nell'API 3D.

| Funzione | Cosa dà |
|---|---|
| `models()` | I nomi dei modelli della cartuccia, in ordine |
| `model(name)` | Il modello come mesh (`nil` se manca). Chiamala in `_init` |
| `bounds3d(m)` | `x0, y0, z0, x1, y1, z1`, il riquadro attorno alla mesh |
| `draw3d(m, x, y, z, rx, ry, rz, scale, flags)` | Disegna la mesh |
| `camera3d`, `light3d`, `zclear` | Camera, luce, azzera la profondità |

::: copy
local casa
function _init()
  casa = model("house")
end
function _draw()
  cls(0x1c2030)
  zclear()
  camera3d(0, 4, -8, 0, -0.4)
  light3d(-0.4, 0.8, -0.5, 0.4)
  draw3d(casa, 0, 0, 0, 0, time() * 0.5)
end
:::

## Da sapere

- L'origine della griglia di bm Studio va nel punto `x, y, z` di `draw3d`. Un quadretto è una unità.
- Le facce con texture usano lo sprite sheet della cartuccia.
- I modelli con scheletro si animano con `animate()`; vedi il manuale di bm Animator.

# Tutorial guidati

## Il tuo primo blocco

1. Apri bm Studio dalla scheda **Dev**.
2. Premi **Esc**, scegli *New project*. Vedi la pagina build con lo strumento **1 block** scelto.
3. Premi **Space**. Nella vista compare un cubo.
4. Premi **PgUp**, poi **Space**. Un secondo cubo sta sopra il primo.
5. Premi **q** alcune volte. La vista gira di 45 gradi ogni volta.
6. Premi **Backspace**. Il blocco sotto il cursore sparisce.
7. Se hai sbagliato, premi **Ctrl+Z**.

::: try
Metti tre blocchi in fila. Controlla nel pannello che `faces` e `tri` contino solo le pareti visibili.
:::

## Dipingi un modello

1. Metti un blocco con lo strumento **1**.
2. Premi **5** per scegliere paint, poi **Enter**. La tessera della faccia sotto il puntatore compare grande nel pannello.
3. Premi **c** e scegli un colore dall'elenco.
4. Muoviti con le frecce e premi **Space**: il pixel prende il colore, e anche la faccia nella vista.
5. Premi **e** su un pixel per cancellarlo.
6. Premi **Esc** per chiudere il pannello e **Ctrl+S** per salvare.

Se la faccia ha un colore piatto invece di una tessera, **Enter** dipinge tutta la faccia. Per dipingere i pixel, dai prima una tessera alla faccia: strumento **3**, scegli la faccia con **Space**, premi **Tab**, scegli una tessera e premi **Enter**, poi **Enter** ancora per metterla sulla faccia.

## Riduci una mesh e guarda il numero di triangoli

1. Premi **F2**. Scegli un modello con molti triangoli.
2. Leggi la seconda riga sopra la vista: le facce, i `tri` e i vertici.
3. Premi `-`. La finestra propone la metà dei triangoli. Premi **Enter**.
4. Leggi `reduced to N triangles  (Ctrl+Z undoes)` e il conteggio cambia.
5. Premi **Ctrl+Z** per tornare indietro.

::: warn
Se il conteggio supera 1200, la riga dice `heavy for 60 fps`. Riduci finché l'avviso sparisce.
:::

## Metti il tuo modello in un gioco

1. Costruisci e dai un nome a un modello, per esempio `house`. Nella pagina models, **r** lo rinomina.
2. Premi **Ctrl+S**. Un nuovo progetto chiede un nome: scrivine uno in forma 8.3.
3. Premi **F5**. Il gioco parte con il programma di visualizzazione che bm Studio ha messo nel progetto; sinistra e destra cambiano il modello.
4. Apri lo stesso progetto in bm Code (bm SDK, pagina F1, *OPEN IN*, numero 1) e sostituisci il codice del visualizzatore con l'esempio del capitolo precedente, usando il nome del tuo modello in `model("house")`.
5. Premi di nuovo **F5**. Vedi il tuo modello che gira.

# Consigli e limiti

- Circa 1200 triangoli in una scena è ciò che la console disegna a 60 fps. Un modello ridotto è il modo più economico per restare sotto.
- Le celle del cursore vanno da -64 a 63 su ogni asse.
- Il pannello conta vertici e triangoli. Il salvataggio non ha un tetto di dimensione; resta solo il limite del formato di 65535 vertici e 65535 triangoli per mesh. Il contatore dei vertici lo mostra ("of 65535").
- Le facce con texture richiedono lo sprite sheet: le texture ne sono dei pezzi.
- Usa spesso **F5**: salva e mostra il modello nel gioco.
- Un gioco e un progetto non sono la stessa cosa: il menu mostra il percorso del file su cui lavori, quindi controlla che sia la copia `.BME` prima di proseguire.
- [DA COMPLETARE: i tasti del gamepad di bm Studio provati sulla PowKiddy RGB30]
