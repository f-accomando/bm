title: bm Write: documenti
accent: Write
lang: it
kicker: Un editor di bm
subtitle: Scrivi lettere e relazioni sulla console, salvale sulla scheda SD ed esportale in PDF, HTML, Markdown o testo.
footer: manuali bm - editor-write

::: readme
**Cos'è.** Il manuale di bm Write, il programma di videoscrittura della suite bm.

**Come si usa.** Leggi una volta i primi tre capitoli, poi segui in ordine i passi numerati dei tutorial. Tutto ciò che sta in un riquadro con bordo e ombra va copiato così com'è.

**Cosa manca.** Ogni lacuna è segnata con un'etichetta evidenziata DA COMPLETARE; non viene inventato nulla.
:::

# Cos'è bm Write

bm Write è un'app per scrivere documenti. Scrivi con una tastiera, o con il pad, e vedi il testo su pagine A4 esattamente come verranno stampate.

| Formato | Estensione | Cos'è |
|---|---|---|
| Documento bm Write | `.BMD` | il formato che salvi e modifichi; dentro è testo semplice |
| PDF | `.PDF` | esportazione, stesse righe sulle stesse pagine dello schermo |
| HTML | `.HTM` | esportazione |
| Markdown | `.MD` | esportazione; si può anche aprire |
| Testo | `.TXT` | esportazione; si può anche aprire |

## Cosa fa

- Otto stili di paragrafo: titolo, due titoletti, corpo, citazione, elenco puntato, elenco numerato e tabella.
- Grassetto, corsivo e sottolineato sulle singole lettere.
- Quattro allineamenti: a sinistra, centrato, a destra e giustificato.
- Pagine in formato A4 con margini di 2 cm e il numero di pagina in fondo. La riga di stato mostra `Page 1 of 3` e il numero di parole.
- Completamento delle parole come in bm Code, in italiano o in inglese.
- Annulla e ripeti, copia, taglia e incolla.

## Perché conta

Il PDF lo crea la console stessa. Ha le stesse righe sulle stesse pagine dello schermo, quindi ciò che vedi è ciò che stampi. Il carattere è Courier, le cui lettere sono tutte della stessa larghezza, come quelle dello schermo.

## Da sapere

- I tuoi documenti stanno nella cartella `/docs` della scheda SD. Sono condivisi dalle app a cui lo permetti.
- I nomi sono nella forma 8.3, senza cartelle: fino a 8 lettere o cifre, come `letter.bmd`.
- bm Write è un'app del Market: non fa parte del kernel.

# Il primo avvio

## La domanda sui documenti

La prima volta che un'app usa i tuoi documenti, la console ti chiede se lo permetti, come fa per la rete. La tua risposta viene conservata nel file `bm/config.txt` della scheda SD.

- Se rispondi sì, bm Write può elencare, leggere, scrivere e cancellare i file in `/docs`.
- Se rispondi no, salvare, aprire ed esportare non funzionano: ogni chiamata fallisce e la riga di stato mostra il motivo.

La domanda è "This document has changes. Save them?" e i pulsanti sono Save, Discard e Cancel.

## La prima schermata

Vedi una pagina vuota su una scrivania scura. Il cursore è una barra lampeggiante. La barra in alto dice `bm Write`, poi `Untitled`. Comincia a scrivere: il testo compare sulla pagina.

Se l'ultima volta hai lasciato un documento con modifiche, bm Write lo riporta con il messaggio `the document with its changes is back (Ctrl+S saves it)`. Se hai lasciato un documento salvato, lo riapre.

::: note
Premi **F1** in qualsiasi momento per vedere i tasti sullo schermo. Qualsiasi tasto chiude l'elenco.
:::

![bm Write all'avvio: una pagina vuota, la barra sopra e la riga di stato sotto](shots/editor-write-start.png)

# Lo schermo e i tasti

## Lo schermo

| Parte | Dove | Cosa mostra |
|---|---|---|
| Barra | riga in alto | `bm Write`, il nome del documento (`Untitled` se nuovo, con `*` se ha modifiche), lo stile del paragrafo, le lettere `B` `I` `U` e `L` `C` `R` `J` |
| Pagine | al centro | il documento su pagine A4, il cursore, la selezione e la parola grigio-blu che Tab scriverebbe |
| Riga di stato | riga in basso | `Page N of M`, il numero di parole, `PAD` e la modalità del pad quando la scrittura col pad è attiva, la lingua |

Sulla barra, le lettere accese mostrano lo stato sul cursore: grassetto, corsivo, sottolineato e l'allineamento del paragrafo. Messaggi come `saved /docs/letter.bmd` prendono il posto della riga di stato per qualche secondo.

## Scrivere e muoversi

| Tasto | Cosa fa |
|---|---|
| lettere, cifre, simboli | scrive sul cursore; una selezione viene sostituita |
| Enter | nuovo paragrafo |
| Backspace, Del | cancella indietro, avanti |
| Tab | scrive la parola grigio-blu; se non c'è, quattro spazi |
| Frecce, Home, End, PgUp, PgDn | muovono |
| F3 | inizia o termina una selezione |
| Shift + frecce | selezionano |
| Esc | con una selezione: la annulla; altrimenti apre il menu |

## Aspetto e modifica

| Tasto | Cosa fa |
|---|---|
| F2 | stile successivo del paragrafo |
| F4 | allineamento successivo del paragrafo |
| Ctrl+B, Ctrl+I, Ctrl+U | grassetto, corsivo, sottolineato |
| Ctrl+L, Ctrl+E, Ctrl+R, Ctrl+J | allinea a sinistra, centra, a destra, giustifica |
| Ctrl+Z, Ctrl+Y | annulla, ripeti |
| Ctrl+C, Ctrl+X, Ctrl+V | copia, taglia, incolla |
| F1 | l'elenco dei tasti |

## File

| Tasto | Cosa fa |
|---|---|
| Ctrl+S | salva; la prima volta chiede un nome |
| Ctrl+Shift+S | salva con nome, con un nome nuovo |
| Ctrl+O | apre un documento da `/docs` |
| Ctrl+N | nuovo documento |
| Esc | il menu, con `Export...` |

## Il menu

Premi **Esc** (oppure **Start** sul pad). Usa le frecce su e giù e Enter. Le voci sono: New document, Open..., Save, Save as..., Export..., Style, Alignment, Bold, Italic, Underline, Select, Undo, Redo, Word completion, Language, Pad typing, Keys, Exit. `Word completion` accende e spegne le parole grigio-blu. `Language` cambia la lingua delle parole tra Italiano e English.

## Il mouse

Un mouse USB o Bluetooth fa ciò che fanno i tasti.

Nella richiesta del nome del file, un clic mette il cursore tra le lettere.

| Azione | Cosa fa |
|---|---|
| Clic sulla pagina | mette il cursore lì |
| Trascinamento | seleziona il testo; al bordo alto o basso la pagina scorre |
| Doppio clic | seleziona la parola |
| Maiusc + clic | allarga la selezione |
| Rotella | fa scorrere le pagine |
| Tasto destro | apre il menu di modifica: Cut, Copy, Paste, Bold, Italic, Underline, Style, Alignment, Undo, Redo, Save, Menu |
| Clic su `bm Write` nella barra | apre il menu |
| Clic su stile, **B**, **I**, **U**, **L**, **C**, **R**, **J** nella barra | premono lo stesso tasto |

Nel menu e negli elenchi un clic sceglie una riga e un secondo clic la prende. La rotella sposta la scelta. Un clic fuori dal riquadro, o il tasto destro, torna indietro.

## Il pad

| Pulsante | Cosa fa |
|---|---|
| DPAD | muove (si ripete se lo tieni premuto) |
| pulsante di conferma | nuovo paragrafo |
| pulsante indietro | cancella |
| Start | il menu |
| Share | scrittura col pad accesa o spenta |

La schermata dei tasti (F1) dice "Share writes with the pad" e il menu dice Share per la scrittura col pad; solo l'elenco mostrato tenendo premuto F12 dice SELECT.

Con il pad, A scrive un nuovo paragrafo e B cancella; Select attiva e disattiva la scrittura col pad (il menu la chiama Share). [DA COMPLETARE: verificare questi pulsanti sul PowKiddy RGB30]

Con la scrittura col pad accesa, la riga di stato mostra `PAD` e sopra di essa compare un piccolo pannello. [DA COMPLETARE: come comporre le lettere con il pad]

# Lo stile

## Stili di paragrafo

Premi **F2** per passare allo stile successivo: Body, Title, Heading 1, Heading 2, Quote, Bullet list, Numbered list, Table, poi di nuovo Body. La barra mostra il nome dello stile.

| Stile | Aspetto |
|---|---|
| Body | testo normale, 72 lettere per riga |
| Title | grande, grassetto, centrato |
| Heading 1 | grande, grassetto |
| Heading 2 | medio, grassetto |
| Quote | rientrato, corsivo |
| Bullet list | rientrato; un paragrafo per voce |
| Numbered list | rientrato; i numeri seguono i paragrafi |
| Table | una riga di tabella; la barra `\|` separa le celle |

### Tabelle

Scegli lo stile Table (l'ultimo nell'elenco di **F2**) e scrivi una riga con `|` tra le celle, per esempio `Nome | Prezzo`. La barra diventa una linea sottile. La riga dopo è un altro paragrafo nello stile Table. Premi **Invio** a fine riga per cominciare una nuova riga.

- Le righe di tabella consecutive condividono le larghezze delle colonne. Ogni colonna è larga quanto la sua cella più lunga. Le celle sono allineate.
- Una riga più larga di 72 lettere non sta nella pagina. bm Write la mostra come testo normale e la riga di stato dice `table too wide`. Accorcia le celle.
- Premi **Invio** su una riga vuota, o **Backspace** all'inizio di una riga, per cambiare la riga in testo normale.
- Mentre il cursore è in una riga di tabella, la riga di stato ti ricorda: `Table: | between the cells`.

## Le lettere

Premi **Ctrl+B**, **Ctrl+I** o **Ctrl+U** per attivare o togliere grassetto, corsivo o sottolineato.

- Con una selezione: lo stile si applica alle lettere selezionate.
- Senza selezione: vale per ciò che scrivi dopo.

## Allineamento

Premi **F4** per scorrere sinistra, centro, destra e giustificato, oppure premi **Ctrl+L**, **Ctrl+E**, **Ctrl+R**, **Ctrl+J**. Un paragrafo giustificato allarga le parole fino al margine destro; la sua ultima riga resta a sinistra.

## Da sapere

- Stile e allineamento appartengono a tutto il paragrafo. Grassetto, corsivo e sottolineato appartengono alle singole lettere.
- Dopo un titolo o un titoletto, il paragrafo successivo è Body. Dopo una citazione o una voce di elenco, il nuovo paragrafo mantiene lo stesso stile; una voce di elenco vuota diventa Body.

![Un documento con un titolo, un titoletto, un elenco e un paragrafo giustificato](shots/editor-write-typed.png)

# Salvare, aprire, esportare

## Dove vanno i file

Tutto va nella cartella `/docs` della scheda SD. La cartella viene creata dal primo salvataggio. Estrai la scheda e mettila nel computer per trovarvi i file.

## Salvare

1. Premi **Ctrl+S**.
2. La prima volta, una riga chiede `Name of the document (8 letters, in /docs):`. Scrivi fino a 8 lettere o cifre (vanno bene `-` e `_`), per esempio `letter`. Il nome viene scritto in minuscolo e viene aggiunto `.bmd`.
3. Premi **Enter**. La riga di stato dice `saved /docs/letter.bmd` e l'`*` accanto al nome sparisce.

I **Ctrl+S** successivi salvano con lo stesso nome. **Ctrl+Shift+S** chiede un nome nuovo.

## Aprire

1. Premi **Ctrl+O**. Se il documento ha modifiche, un riquadro chiede `This document has changes. Save them?` con Save, Discard, Cancel.
2. Scegli un file dall'elenco `Open a document (/docs)` con le frecce su e giù. L'elenco mostra i file `.BMD`, `.MD` e `.TXT`, con la loro dimensione.
3. Premi **Enter**.

Un file `.MD` o `.TXT` si apre come documento nuovo senza nome: **Ctrl+S** chiede un nome e lo salva come `.BMD`. I segni del Markdown sono capiti: `#` titolo, `##` e `###` titoletti, `>` citazione, `-` elenchi puntati, `1.` numeri, `**grassetto**`, `*corsivo*`, `<u>sottolineato</u>`. Le righe con barre, come `| a | b |`, tornano come righe di tabella; una riga come `|---|---|` è saltata.

Un kernel più vecchio non conosce lo stile Table. Apre una tabella salvata come testo normale, e le celle sono separate da barre di disegno a riquadro.

## Esportare

1. Premi **Esc**, scendi su `Export...` e premi **Enter**.
2. Nel riquadro `Export to /docs as`, scegli `PDF`, `HTML`, `Markdown` o `Text`, e premi **Enter**.
3. Se il documento non ha ancora un nome, bm Write lo chiede. La riga di stato dice `exported /docs/letter.pdf` con la dimensione in byte.

| Formato | File |
|---|---|
| PDF | `letter.pdf` |
| HTML | `letter.htm` |
| Markdown | `letter.md` |
| Testo | `letter.txt` |

Un'esportazione è una copia da leggere o condividere. Salva anche il `.BMD`: è quello che puoi modificare con tutti gli stili.

Le tabelle vanno in ogni formato così:

| Formato | Una tabella |
|---|---|
| PDF | come sullo schermo |
| HTML | un `<table>` |
| Markdown | righe `\| a \| b \|`, con una riga `\|---\|` sotto la prima riga della tabella |
| Testo | le colonne riempite con spazi |

::: warn
Un'esportazione con lo stesso nome sostituisce la precedente in `/docs`.
:::

## Uscire

Scegli `Exit` nel menu. Se ci sono modifiche, un riquadro chiede di salvare, scartare o annullare. Se esci con Ctrl+Esc, vengono chieste anche in quel caso; un secondo Ctrl+Esc le conserva per la volta dopo.

# Tutorial

## Scrivi e salva il tuo primo documento

::: try
Parti dal menu della console, con la scheda SD inserita.
:::

1. Avvia **bm Write** dal menu. Vedi una pagina vuota e la barra `bm Write` `Untitled`.
2. Premi **F2** una volta. La barra mostra `Title`. Scrivi `A letter` e premi **Enter**. Il titolo è grande e centrato.
3. Il nuovo paragrafo è Body. Scrivi `This text has a ` poi premi **Ctrl+B**, scrivi `bold`, premi di nuovo **Ctrl+B** e scrivi ` word.` La parola `bold` è più spessa.
4. Premi **Enter**. Premi **F2** cinque volte per arrivare a `Bullet list`. Scrivi `one`, **Enter**, `two`, **Enter**, `three`. Le tre righe hanno il punto elenco.
5. Premi **Ctrl+S**. Scrivi `LETTER` e premi **Enter**.
6. Guarda la riga di stato: dice `saved /docs/letter.bmd` e la barra mostra `letter.bmd` senza `*`.

Se la console non riesce a scrivere, la riga di stato è rossa e dice `cannot save letter.bmd:` con il motivo. Vedi l'ultimo capitolo.

![Un primo documento appena scritto](shots/editor-write-pages.png)

## Esportalo in PDF e leggilo sul computer

::: try
Parti con `letter.bmd` aperto e salvato, come nel tutorial qui sopra.
:::

1. Premi **Esc**. Si apre il menu.
2. Scendi su `Export...` e premi **Enter**.
3. Con `PDF` selezionato, premi **Enter**.
4. Aspetta `exported /docs/letter.pdf` sulla riga di stato.
5. Spegni la console e metti la scheda SD nel computer.
6. Apri la cartella `docs`: `letter.pdf` è lì. Aprilo con un qualsiasi lettore PDF. Le pagine hanno le stesse righe dello schermo, in Courier.

## Aprilo di nuovo

::: try
Parti con il documento chiuso, oppure dopo aver riavviato bm Write.
:::

1. Premi **Ctrl+O**. Se bm Write chiede di salvare le modifiche, scegli una risposta.
2. L'elenco `Open a document (/docs)` mostra `letter.bmd` con la sua dimensione, e il `.PDF` non è nell'elenco.
3. Seleziona `letter.bmd` con le frecce e premi **Enter**.
4. La riga di stato dice `opened /docs/letter.bmd`. Il testo è come l'avevi lasciato.

Puoi aprire anche `letter.md`, se l'hai esportato: ritorna con gli stessi paragrafi.

# Consigli, limiti e problemi

## Consigli

- Salva presto con **Ctrl+S**, e spesso.
- Usa **F1** quando dimentichi un tasto.
- Usa **Tab** per prendere la parola grigio-blu invece di scriverla tutta. Cambia la lingua nel menu se le parole non sono le tue.
- Con una selezione, **Esc** toglie prima la selezione; premilo ancora per il menu.

## Limiti

- Il nome di un documento è di al massimo 8 lettere o cifre, `-` e `_` ammessi, senza cartelle.
- Un file scritto in `/docs` è al massimo di 4 MiB.
- Il corpo di una pagina contiene 72 lettere per riga, in Courier.
- Il testo non salvato viene conservato per il prossimo avvio solo se è sotto i 24000 byte.
- Il titolo del file `.BMD` conserva le prime 60 lettere del primo paragrafo.
- [DA COMPLETARE: il massimo numero di pagine o di paragrafi che un documento può avere sulla console]
- Il carattere dello schermo è la tabella di caratteri 437: lettere latine con gli accenti, alcune lettere greche, linee per riquadri e segni di matematica. Il PDF usa l'insieme WinAnsi (lettere latine con gli accenti, virgolette curve, il pallino elenco e il segno dell'euro). Un segno che non è in WinAnsi, come una linea per riquadri, nel PDF è scritto come `?`.

## Problemi

| Problema | Cosa fare |
|---|---|
| Salvare, aprire o esportare fallisce e la riga di stato è rossa | Leggi il motivo. Se hai risposto no alla domanda sui documenti, le chiamate falliscono: la risposta è conservata in `bm/config.txt` sulla scheda SD. Apri quel file su un computer e cerca la riga che inizia con `allow_` e finisce con `(bm Write)`. Cambia `docs=no` in `docs=yes`, oppure cancella la riga: la console chiede di nuovo al salvataggio successivo |
| `no documents in /docs yet: Ctrl+S saves this one` | La cartella è vuota. Salva prima un documento |
| `8 letters or digits, like LETTER` | Il nome ha uno spazio, o più di 8 lettere (un `.ext` finale viene tolto: `letter.txt` è accettato come `LETTER`). Scrivine uno più corto |
| `not a bm Write document` | Il file `.BMD` non è nel formato di bm Write |
| Il file salvato non è nell'elenco di apertura | Sono elencati solo `.BMD`, `.MD` e `.TXT`. Un PDF o un HTML non si può aprire |
| Hai chiuso l'app senza salvare | Riavvia bm Write: se le modifiche sono state conservate vedi `the document with its changes is back (Ctrl+S saves it)` |
| Le parole suggerite sono nella lingua sbagliata | Nel menu scegli `Language`; `Word completion` le spegne |

::: note
Controlla il PDF sulla console prima di affidarti a esso per qualcosa di importante: [DA COMPLETARE: provare il PDF sul Pi Zero W e aprirlo su un PC, come dice la roadmap]
:::
