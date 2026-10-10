# Picchiaduro a robot giganti — concept (M20)

Brief originale dell'autore (2026-09-28), riportato senza modifiche di contenuto.
La progettazione tecnica (punto 25) sarà in `docs/giochi/mecha-fighter-design.md`,
da scrivere all'avvio di M20.

---

Voglio sviluppare un picchiaduro 2D a incontri con robot giganti, fortemente ispirato
alla struttura e al feeling di Street Fighter II Turbo su SNES, ma evoluto dove
tecnicamente e visivamente possibile prendendo ispirazione anche dai picchiaduro 2D
successivi, in particolare dalla filosofia di Marvel Super Heroes vs. Street Fighter:
combattimento molto dinamico, combo più elaborate, verticalità, dash, air combat,
juggle, tag team e maggiore libertà di movimento.

Il progetto deve però mantenere una forte identità 16-bit / SNES-like, evitando che
"retro" venga interpretato come "minimalista".

## 1. Scala visiva — vincolo fondamentale

Questo è un punto fondamentale: i robot NON devono essere rappresentati con piccoli
sprite da gioco indie minimalista. La dimensione e la presenza dei personaggi sullo
schermo devono essere comparabili, come ordine di grandezza e importanza visiva, agli
sprite dei personaggi di Street Fighter II Turbo su SNES.

Voglio quindi:

- sprite grandi e chiaramente leggibili;
- personaggi che occupino una parte consistente dello schermo;
- animazioni composte da molti frame quando possibile;
- silhouette molto leggibili;
- dettagli visibili nell'armatura;
- effetti di impatto ben evidenti;
- deformazioni, danni e distacchi di parti chiaramente visibili;
- fondali con grande profondità;
- nessuna rappresentazione "micro-sprite" o eccessivamente semplificata.

Il riferimento deve essere: "Street Fighter II Turbo / Marvel Super Heroes vs. Street
Fighter, ma con robot giganti e tecnologia più avanzata" e NON: "un piccolo gioco
pixel-art con robot minuscoli". Quando devi fare compromessi tecnici, privilegia la
leggibilità e la presenza dei robot sullo schermo.

## 2. Concept

Il gioco è un fighting game 2D nel quale combattono robot da combattimento
giganteschi, alti come grattacieli. Non sono semplicemente robot umanoidi standard.
Sono vere e proprie macchine da guerra modulari.

Ogni robot può essere personalizzato sia:

1. visivamente;
2. nelle statistiche;
3. nelle armi;
4. nei movimenti;
5. nelle abilità;
6. nelle mosse speciali.

Il giocatore costruisce il proprio robot prima del combattimento. L'obiettivo è creare
un sistema nel quale la configurazione del robot influenzi realmente il gameplay, non
sia soltanto cosmetica.

## 3. Modalità di gioco

Il gioco deve supportare almeno:

**Single player** — modalità arcade/campagna nella quale il giocatore affronta una
serie di avversari. Possibili elementi: incontri normali; boss; robot particolarmente
configurati; combattimenti con regole speciali; progressione attraverso diverse
ambientazioni.

**Versus 1v1** — due robot combattono direttamente.

**Tag team** — due robot per squadra. Il giocatore può passare da un robot all'altro
durante il combattimento.

**2v2 tag** — due robot contro due robot.

Il sistema deve essere progettato fin dall'inizio per supportare il combattimento
tag, evitando di costruirlo come aggiunta successiva.

## 4. Sistema tag

Il sistema tag deve essere importante e tattico. All'inizio della partita la squadra
dispone di **2 slot di cambio**. Ogni cambio consuma uno slot. Gli slot non sono
semplicemente consumabili per tutta la partita: si rigenerano lentamente nel tempo.
Quindi il giocatore deve decidere quando utilizzare il tag.

Questo permette di creare: tag offensivi; tag difensivi; salvataggi quando il robot è
in difficoltà; combo che terminano con cambio; strategie di alternanza; pressione
continua.

La barra/risorsa del tag deve essere chiaramente visibile nell'interfaccia.

## 5. Robot personalizzabili

La personalizzazione è una delle caratteristiche principali del gioco. Durante la
selezione del robot il giocatore non deve scegliere semplicemente un personaggio.
Deve costruire/configurare una macchina da combattimento. La selezione dovrebbe
permettere di scegliere elementi come:

**Struttura base** — corpo; peso; altezza; telaio; distribuzione della massa.

**Armatura** — differenti configurazioni di armatura. L'armatura deve avere
conseguenze sia estetiche che gameplay.

- Più armatura: maggiore resistenza; maggiore peso; minore accelerazione; minore
  agilità.
- Meno armatura: maggiore velocità; maggiore agilità; maggiore capacità di movimento;
  minore protezione.

**Armi** — possibili equipaggiamenti: spadoni; martelli; lame; cannoni montati sulle
braccia; mitragliatori; lanciarazzi; micromissili; armi sulle spalle; armi integrate
nel corpo; eventuali armi energetiche.

**Booster** — installati sotto i piedi; sulla schiena; sulle gambe; sulle spalle.
Possono influenzare: dash; salto; velocità; movimento aereo; recupero; possibilità di
effettuare scatti aerei. I booster consumano energia.

## 6. Peso ed equipaggiamento

Questo deve essere uno dei principi fondamentali del gioco. Più equipaggiamento = più
peso.

- Più peso significa: minore velocità; minore accelerazione; salto più basso; dash
  più lento; maggiore inerzia.
- Meno equipaggiamento significa: maggiore agilità; maggiore velocità; salto
  migliore; dash più rapido; maggiore mobilità aerea.

Quindi non deve esistere una configurazione semplicemente "migliore". Il giocatore
deve scegliere tra: potenza / protezione / mobilità / energia / capacità offensiva.

## 7. Combattimento base

Il combattimento deve partire dalla filosofia di Street Fighter II Turbo. I robot
devono poter effettuare: pugni; calci; attacchi pesanti; attacchi leggeri; attacchi
accovacciati; attacchi in salto; attacchi aerei; prese; proiezioni; parate; dash;
salto; doppio salto quando consentito dalla configurazione; attacchi speciali; super;
combo.

Ma il sistema deve poter evolvere verso la maggiore spettacolarità dei fighting game
successivi. Prendere quindi ispirazione anche da: air combos; juggle; launch; dash
aereo; combo estese; cancellazioni; chain combo; special cancel; super; tag combo.

Non copiare direttamente mosse o personaggi esistenti: utilizzare queste idee come
riferimento per il tipo di profondità del combattimento.

## 8. Verticalità

La verticalità deve essere molto importante. I robot sono enormi e devono poter
sfruttare lo spazio verticale. Possibili meccaniche: salti normali; salti potenziati;
boost verticali; dash aerei; attacchi dall'alto; launcher; juggle; combattimenti
temporaneamente sospesi in aria; atterraggio pesante; attacchi verso il basso.

La configurazione del robot deve influenzare queste possibilità. Un robot molto
pesante potrebbe avere un salto corto ma un enorme impatto all'atterraggio. Un robot
leggero potrebbe avere un salto molto più alto e maggiore controllo aereo.

## 9. Sistema armatura

Questa è una delle caratteristiche più importanti del gioco. Ogni robot deve avere una
**barra vita** e sopra di essa una **barra armatura**.

L'armatura non è semplicemente una seconda barra vita astratta. Deve essere
rappresentata fisicamente sul robot. Quando l'armatura subisce danni: la barra
armatura diminuisce; la parte corrispondente dello sprite si danneggia; l'armatura può
deformarsi; pannelli possono rompersi; pezzi possono staccarsi; componenti interni
possono diventare visibili.

E soprattutto: la distruzione dell'armatura deve essere sincronizzata tra barra e
sprite.

Esempio: un robot ha una grossa corazza sulla spalla destra. Quando quella parte
subisce abbastanza danni:

1. la porzione corrispondente della barra armatura diminuisce;
2. la corazza mostra danni;
3. eventualmente si stacca;
4. lo sprite cambia mostrando il componente sottostante;
5. il robot rimane visivamente danneggiato per il resto dello scontro.

Questo deve creare un senso di progressiva distruzione.

## 10. Danni localizzati

Quando possibile, considera un sistema di danno localizzato. Possibili sezioni: testa;
torso; braccio destro; braccio sinistro; gamba destra; gamba sinistra; spalle;
componenti esterni.

Non è necessario simulare fisicamente ogni componente. L'obiettivo è ottenere feedback
visivo e gameplay leggibile.

Esempio: se viene distrutto un cannone sul braccio: il cannone scompare dallo sprite;
quell'arma non è più utilizzabile; il robot deve continuare il combattimento con le
altre capacità.

## 11. Scudi olografici

Alcune configurazioni possono utilizzare scudi energetici/olografici. Gli scudi devono
avere una propria condizione. Quando subiscono danni: diventano instabili;
lampeggiano; mostrano distorsioni; presentano "glitch"; possono perdere sezioni;
infine collassano.

Quindi uno scudo perfettamente integro deve avere un aspetto diverso da uno quasi
distrutto. Gli scudi possono eventualmente rigenerarsi lentamente, ma questo deve
dipendere dalla configurazione scelta.

## 12. Surriscaldamento delle armi

Le armi da fuoco non devono essere infinite. I cannoni, mitragliatori e sistemi
missilistici devono avere una risorsa di **heat / surriscaldamento**. Dopo un certo
numero di colpi: l'arma si surriscalda; aumenta il rischio di malfunzionamento; l'arma
diventa temporaneamente inutilizzabile; il giocatore deve aspettare il raffreddamento.

Il surriscaldamento deve essere chiaramente visibile attraverso: barra; effetti sul
modello/sprite; fumo; bagliori; animazioni; eventuali segnali sonori.

Questo evita che il combattimento a distanza diventi semplicemente "spam di
proiettili".

## 13. Energia

Il robot deve avere anche una riserva energetica. L'energia può essere utilizzata da:
booster; scudi; alcune armi; armi energetiche; abilità speciali.

Quindi il giocatore deve gestire più risorse contemporaneamente: **vita, armatura,
energia, calore, tag**. Ma l'interfaccia deve rimanere leggibile. Non voglio un HUD
complicato da simulatore. Deve sembrare un fighting game arcade.

## 14. Effetto della distruzione

Quando un robot subisce danni importanti deve apparire progressivamente distrutto.
Possibili effetti: pannelli che saltano; scintille; fumo; componenti esposti; cavi;
parti incandescenti; parti meccaniche danneggiate; armi distrutte; vetri/sensori
rotti.

Questo deve essere soprattutto leggibile durante il combattimento, non soltanto una
sequenza cinematica.

## 15. Selezione del robot — hangar

La schermata di selezione deve essere una delle caratteristiche estetiche distintive
del gioco. Non voglio una normale schermata "SELECT YOUR FIGHTER" con semplici
riquadri.

Il giocatore deve trovarsi dentro un enorme hangar industriale. Il robot selezionato è
al centro. Intorno al robot ci sono: ingegneri; operai; piattaforme; gru; bracci
robotici; impalcature; ascensori; strumenti; saldatrici; cavi; container; macchinari.

Gli esseri umani devono essere minuscoli rispetto al robot, in modo da rendere
evidente la scala gigantesca.

Gli operai possono: saldare; trasportare materiali; lavorare sui piedi; lavorare sulle
gambe; controllare pannelli; utilizzare piattaforme mobili.

I bracci meccanici possono: montare armatura; spostare componenti; installare armi;
sollevare pannelli.

Questi elementi sono principalmente estetici, ma devono rendere l'hangar vivo.

## 16. Animazione dell'hangar

L'hangar non deve essere completamente statico. Devono esserci piccoli eventi
ambientali continui: saldature; scintille; bracci meccanici che si muovono;
piattaforme che salgono; operai che camminano; luci che lampeggiano; pannelli che si
aprono; componenti trasportati.

Questi elementi possono essere in loop e non devono necessariamente avere una funzione
gameplay. Lo scopo è dare la sensazione che il robot venga realmente preparato prima
della battaglia.

## 17. Selezione dell'equipaggiamento

Mentre il giocatore è nell'hangar deve poter modificare il robot. La schermata può
permettere di selezionare: chassis; armatura; braccia; armi; spalle; booster; scudo;
sistema energetico; sistema di movimento.

Quando il giocatore cambia un equipaggiamento il robot visualizzato nell'hangar deve
cambiare realmente. Esempio:

- se equipaggio un enorme martello, il martello compare nella mano del robot;
- se cambio armatura, il modello/sprite cambia;
- se aggiungo booster ai piedi, compaiono fisicamente;
- se rimuovo un cannone, il cannone scompare.

Questo deve rendere la personalizzazione visivamente soddisfacente.

## 18. Arene

I combattimenti devono avvenire in ambientazioni dove la scala dei robot sia evidente.
Esempi:

- **Città abbandonata**: grattacieli distrutti; strade; automobili; palazzi; ponti;
  detriti.
- **Zona industriale**: fabbriche; serbatoi; condutture; gru; strutture metalliche.
- **Foresta**: alberi giganteschi; terreno; rovine; vegetazione.
- **Terre aride**: deserto; canyon; rocce; strutture abbandonate.
- **Zona costiera / porto**: navi; container; gru; edifici industriali.

Le ambientazioni devono aiutare a comunicare: "questi robot sono giganteschi".

## 19. Profondità dei fondali

Anche se il gioco è 2D, voglio una forte sensazione di profondità. Utilizzare, quando
possibile: parallasse; più livelli di fondale; elementi davanti ai robot; elementi
dietro; particelle; fumo; detriti; oggetti ambientali animati.

Il fondale deve sembrare un vero luogo, non una semplice immagine statica.

## 20. Stile grafico

Direzione artistica: SNES / 16-bit evoluto. Il riferimento estetico è la generazione
SNES, ma il progetto deve cercare di spingersi oltre i limiti classici dove possibile.
Quindi: pixel art dettagliata; sprite grandi; animazioni fluide; palette ricche ma
coerenti; effetti particellari; esplosioni; scie; lampi; distorsioni energetiche;
grandi effetti di impatto.

Non trasformare il gioco in 3D semplicemente perché sarebbe più facile rappresentare i
robot. Il combattimento deve rimanere principalmente: 2D side-view fighting game.

## 21. Filosofia del design

Il gioco deve essere facile da capire ma difficile da padroneggiare.

Un nuovo giocatore deve poter: muoversi; colpire; saltare; parare; usare un'arma; fare
un semplice combo.

Un giocatore esperto deve poter sfruttare: combo; juggle; air combo; dash; gestione
dell'energia; gestione del calore; gestione dell'armatura; gestione degli scudi; tag;
configurazione del robot; timing.

La complessità deve emergere progressivamente, senza rendere il gioco immediatamente
simile a un simulatore.

## 22. Priorità

Quando devi prendere decisioni progettuali, usa questa gerarchia:

1. Feeling da fighting game arcade
2. Robot grandi e visivamente imponenti
3. Combattimento fluido
4. Leggibilità
5. Personalizzazione significativa
6. Sistema di danno/armatura
7. Tag team
8. Verticalità
9. Effetti spettacolari
10. Dettagli estetici

Non sacrificare la giocabilità per aggiungere sistemi inutilmente complessi.

## 23. Importante: evitare il minimalismo

Non interpretare il progetto come: "SNES = pochi pixel = pochi elementi". L'obiettivo
è invece: "Come sarebbe un fighting game SNES estremamente ambizioso, progettato con
alcune idee evolute dei fighting game successivi?"

Quindi voglio vedere: sprite grandi; robot dettagliati; molte animazioni; fondali
ricchi; effetti; distruzione progressiva; personalizzazione; combattimento verticale;
combo; tag; armi; gestione delle risorse.

Quando una funzionalità non è possibile nella stessa misura dell'hardware SNES reale,
implementa una versione compatibile con lo stile retro ma non ridurre automaticamente
la scala o la complessità del concept.

## 24. Obiettivo finale

Il risultato dovrebbe dare questa impressione: "Street Fighter II Turbo incontra
Marvel Super Heroes vs. Street Fighter, ma tutti i combattenti sono giganteschi robot
modulari costruiti in un enorme hangar."

Il giocatore deve percepire immediatamente: peso; scala; potenza; distruzione;
personalizzazione; velocità; tecnologia; spettacolarità. E contemporaneamente deve
rimanere chiaramente riconoscibile come: un fighting game 2D arcade.

## 25. Cosa voglio da te

Prima di implementare qualsiasi cosa:

1. analizza il concept;
2. individua eventuali conflitti tra le meccaniche;
3. proponi una struttura tecnica realizzabile;
4. definisci il core gameplay loop;
5. definisci le risorse del combattimento;
6. definisci il sistema di movimento;
7. definisci il sistema combo;
8. definisci il sistema tag;
9. definisci il sistema armatura/danni;
10. definisci il sistema di equipaggiamento;
11. definisci come la configurazione influenza statistiche e mosse;
12. definisci la struttura delle arene;
13. definisci l'HUD;
14. definisci la schermata hangar;
15. definisci un primo vertical slice giocabile.

Non partire immediatamente costruendo tutto il gioco. Prima voglio una progettazione
tecnica concreta e modulare. Dopo la progettazione, procedi per incrementi, mantenendo
sempre una build giocabile. Ogni nuova funzionalità deve poter essere testata senza
rompere quelle precedenti.

La priorità iniziale è creare un vertical slice 1v1 completo e giocabile, con:

- 2 robot;
- movimento;
- salto;
- dash;
- pugni;
- calci;
- parata;
- combo;
- almeno un'arma;
- energia;
- calore;
- barra vita;
- barra armatura;
- almeno un pezzo di armatura distruttibile;
- una piccola arena;
- sprite sufficientemente grandi da comunicare immediatamente la scala dei robot.

Solo dopo che questo core funziona bene, espandere il progetto con personalizzazione
completa, tag team, ulteriori armi, altre arene e contenuti.
