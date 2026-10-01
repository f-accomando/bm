# Scrittura col pad (M30)

Scrivere testo e codice col solo controller, senza tastiera a schermo: **un accordo**
della croce e dei quattro tasti scrive **una sillaba intera** (giù + △ = "ca"), i
grilletti scelgono il banco delle consonanti, e il dizionario **finisce la parola**
(R2 la scrive). L'idea è quella del T9 dei Nokia (pochi tasti, il dizionario fa il resto),
ma per sillabe invece che per lettere; la modalità **steno** aggiunge gruppi di consonanti e
dittonghi, come la stenotipia dei tribunali.

Su un testo italiano di 100 caratteri: tastiera **100** pressioni, tastiera a schermo delle
console **416**, **sillabe 50**, **sillabe + predizione 41**, **steno + predizione 39**
(i numeri completi più sotto).

- **Dove**: in **bm Code** (scheda Dev), **Share** (Select) accende e spegne la scrittura;
  anche nelle richieste di una riga (Trova, Vai alla riga, Salva come). Nel codice il
  dizionario è quello di Lua (parole chiave, API, nomi della scheda), nei commenti e nelle
  stringhe l'italiano (o l'inglese: menu, "Pad: comments it / en").
- **Esercizio**: menu di bm Code (Start), **Pad practice...**: un testo da 100 caratteri
  da ricopiare, le pressioni contate, il migliore possibile, e il **prossimo accordo**
  consigliato, illuminato anche nel pannello.
- **Modulo**: `require "padtype"` (`src/ai/padtype.lua`), qualsiasi cartuccia può usarlo.

## 1. Quante sillabe ha l'italiano

`make pad-stats` (`scripts/padsyll.py`) le conta, accenti esclusi, sui testi di
`src/ai/words` (23 118 parole scritte per bm: racconti, messaggi, giochi, tecnica,
lettere) e sul loro lessico (8 523 parole diverse):

- **In teoria**: 16 consonanti italiane (b c d f g h l m n p q r s t v z) per 5 vocali
  fanno **80 sillabe consonante + vocale**, **85** con le vocali da sole; più **31** che si
  scrivono con due o tre lettere per un suono (che chi ghe ghi; gna gne gni gno gnu;
  glia glie gli glio gliu; sce sci scia scio sciu sche schi; cia cio ciu gia gio giu;
  qua que qui quo). La q va solo con la u, la h quasi solo in ha, ho, hanno, ch e gh.
- **Nei testi**: 46 251 sillabe (2 per parola), **1 078 diverse** (1 277 con il lessico,
  per i gruppi come stra, glie, scrit e le sillabe chiuse come con, per, par).
- **Ma poche fanno quasi tutto**: le **35** più frequenti sono **metà** delle sillabe
  scritte, le 130 l'80%, le 247 il 90%, le 382 il 95%.
- **Consonante + vocale** è il **52,8%** delle sillabe, una **vocale sola** il 6,4%:
  insieme il **59%**, una pressione ciascuna.
- Inizio della sillaba: vocale 12,6%, t 9,5%, n 7,8%, l 7,2%, s 7,2%, r 6,8%, d 6,7%,
  c 6,4%, m 5,8%, p 5,2%, v 4,2%, ch 2,1%, f 2,0%, b 1,7%, st 1,4%, z 1,3%, pr 1,3%...
  Vocali: a 25%, e 24%, o 22%, i 18%, u 5%, poi i dittonghi ia, io, ie, uo.
  Fine della sillaba: aperta 75%, n 8%, l 5%, r 4,5%, poi le doppie.

Da qui la disposizione: **16 consonanti = 4 direzioni × 4 banchi** (nessun grilletto, L2,
R2, L2+R2), **4 vocali sui 4 tasti** (la u, la meno usata, su due tasti), e la sillaba
consonante + vocale in **un accordo solo**. Le sillabe più lunghe si fanno in 2 o 3 accordi
(stra = s, t, ra; in steno è un accordo solo, str + a), e la predizione toglie il resto.

## 2. Come si scrive

**Un accordo** è tutto quello che si preme finché i tasti tornano tutti su: si scrive al
rilascio, quindi i tasti non devono scendere nello stesso istante (giù, poi △, poi si
lascia). Mentre lo tieni, il pannello mostra cosa scriverà (`> ca`) e i tasti cambiano
etichetta: tenendo giù, i quattro tasti dicono ca ce ci co (e cu); tenendo L2, la croce dice
d l g b e i tasti la punteggiatura.

### Consonanti: la croce, i grilletti per il banco

| Croce | — | L2 | R2 | L2+R2 |
|---|---|---|---|---|
| ↑ | t | d | s | z |
| → | n | l | r | m |
| ↓ | c | g | h | qu |
| ← | p | b | v | f |

Ogni direzione va dalla lettera più usata alla meno usata: più grilletti, lettera più rara.
Con L2, t c p diventano d g b (le "sonore"). **Le diagonali** da sole sono le lettere
dell'inglese e del codice: ↗ x, ↘ w, ↙ k, ↖ y (la j è Share + ✕ + ○).
La q scrive "qu": L2+R2 + ↓ + △ = qua.

### Vocali: i quattro tasti

| △ | □ | ✕ | ○ | ✕ + ○ |
|---|---|---|---|---|
| a (la forma della A) | e | i | o (la forma della O) | u |

Insieme: **croce + tasto = sillaba** (↓ + △ = ca, L2 + ↑ + ○ = do). Croce da sola = la
consonante, tasto da solo = la vocale.

### Nello stesso accordo

- **L1**: raddoppia la consonante (L1 + ↑ + △ = tta, pi + zza = pizza).
- **R1**: lo spazio dopo (→ + ○ + R1 = "no "): la parola finisce con la sua ultima sillaba.
- La **maiuscola** a inizio frase è automatica (nel testo; nel codice no).

### Tasti da soli

| Tasto | Fa |
|---|---|
| R1 | spazio |
| L1 | cancella (tenuto: continua); subito dopo un suggerimento lo toglie |
| R2 / L2 / L2+R2 | scrive il 1°, 2° o 3° suggerimento (con lo spazio dopo, o la "(" di un'API) |
| L1 + L2 | accento sulla vocale appena scritta: e → è → é, a → à, o → ò... (con R1 anche lo spazio) |
| L1 + R1 | maiuscola (o minuscola) per la lettera che segue |
| Start | a capo (in bm Code con il rientro) |
| Start + croce | sposta il cursore (tenuto: continua) |
| Start + L1 | annulla |
| Share | spegne la scrittura |

### Punteggiatura, numeri, simboli

Un tasto con un grilletto (e R1 per lo spazio dopo):

| | △ | □ | ✕ | ○ |
|---|---|---|---|---|
| L2 | . | , | ' | ? |
| R2 | ! | : | - | " |
| L2+R2 | ( | ) | = | ; |

Share tenuto + un tasto (pollice sinistro su Share, destro sui tasti):

| Share + | △ | ○ | ✕ | □ |
|---|---|---|---|---|
| — | 1 | 2 | 3 | 4 |
| L2 | 5 | 6 | 7 | 8 |
| R2 | 9 | 0 | + | * |
| L2+R2 | / | < | > | # |
| L1 | [ | ] | { | } |
| R1 | _ | ~ | % | & |
| L1+R1 | \| | ^ | @ | \ |

Dopo un suggerimento (che mette lo spazio) la punteggiatura `, . ; : ! ? '` si attacca
alla parola: "casa" + "," = "casa, ".

## 3. La predizione

Dopo ogni accordo il dizionario propone **tre parole** che iniziano come quella che stai
scrivendo, tenendo conto della **parola prima** ("do" dopo "ciao" → domani, dopo, dove).
Nell'editor la parte che manca si vede **dopo il cursore in blu-grigio**; R2 (o L2, L2+R2
per la seconda e la terza) la scrive, e la parte scritta dalla predizione resta **verde**
fino all'accordo successivo. Anche prima di una lettera, dopo uno spazio, propone la parola
che segue più spesso ("La" → console).

- Gli accenti non servono: "perche" trova "perché", "e" propone "è".
- I nomi tengono la maiuscola ("ma" → Marco, se è nel dizionario).
- **Nel codice**: le parole chiave di Lua (con lo spazio dopo: `function `, `local `),
  le funzioni delle API **dalla base di conoscenza dell'assistente** (M30), con la
  parentesi (`cl` → `cls(`) e la **firma** nel pannello (`cls([c])`, `circfill(x, y, r,
  c)`), i nomi dei giochi di bm e quelli della scheda aperta. "f" a
  inizio riga propone `for` e `function` (R2, L2), "if b" propone `btn`.

Il modello è un **n-gramma**: quante volte ogni parola è scritta e quali parole la seguono,
contate sui testi di `src/ai/words` (italiano: 23 000 parole + un lessico di 6 500 forme;
inglese: 4 500 parole) e sul Lua dei giochi di bm (`scripts/mkpadwords.py`, `make` lo
rifà). Sul Pi è una ricerca binaria in una tabella, fatta una volta per accordo: la
prima volta il dizionario si legge in qualche decimo di secondo, poi non si vede. Una
rete per la parola successiva costerebbe molto di più sul Pi Zero (vedi [AI.md](../AI.md))
e su testi così brevi non è detto che indovini meglio; il bigramma si misura (sotto) e si
rifà in un secondo quando cambiano i testi.

## 4. Steno: come la stenotipia

Nei tribunali e al Senato italiano si scrive con la **stenotipia** (la macchina Michela,
1863): pochi tasti premuti insieme danno una sillaba intera, inizio, vocale e fine, e
il resocontista scrive alla velocità della voce. La modalità **steno** (menu: "Pad:
syllables / steno") porta l'idea sul pad, sopra le sillabe (tutto il resto è uguale):

- **diagonali con un grilletto**: i gruppi di consonanti più frequenti, scelti sui testi
  (ricerca che aggiunge ogni volta il gruppo che fa risparmiare di più):

  | Diagonale | L2 | R2 | L2+R2 |
  |---|---|---|---|
  | ↗ | tr | st | str |
  | ↘ | ch | gn | gl |
  | ↙ | sc | sp | pi |
  | ↖ | pr | br | gr |

- **dittonghi su due tasti**: △+□ ia, △+○ io, ✕+□ ie (✕+○ resta la u).

Così "chia" (in giochiamo) è un accordo solo: L2 + ↘ + △ + □. Costa imparare le
diagonali (il pannello le mostra) e premere 4-5 tasti insieme; rende il 10% in meno di
pressioni. Le fini di sillaba (con, per, il) restano due accordi: il pad non ha abbastanza
dita libere per un terzo gruppo di tasti come la macchina Michela.

## 5. Benchmark

Il **testo**: `Ciao Marco, domani sera giochiamo da me? La console nuova è arrivata: tu
porta la pizza e le bibite!` (100 caratteri, spazi e punteggiatura compresi: 100
pressioni su una tastiera, una maiuscola con Shift conta una). Anche in inglese e in Lua,
100 caratteri ciascuno (`padtype.TEXTS`).

`make pad-bench` (`tests/pad/bench.lua`) cerca **il minimo di pressioni** con le stesse
tabelle e gli stessi suggerimenti della console (un utente esperto; `padtype.encode`), e
`make test-pad` riscrive ogni testo premendo quegli accordi nel motore vero.

| Testo (100 caratteri) | Tastiera | Tastiera a schermo | Multitap sillabe | Sillabe | Steno | Sillabe + predizione | Steno + predizione |
|---|---|---|---|---|---|---|---|
| italiano | 100 | 416 | 163 | 50 | 46 | 41 | 39 |
| english | 100 | 382 | 201 | 57 | 56 | 49 | 48 |
| lua | 100 | 372 | 177 | 71 | 70 | 46 | 46 |

- **Tastiera a schermo**: la griglia delle console (QWERTY, la croce si muove, ✕ scrive,
  △ spazio): 4 pressioni per carattere.
- **Multitap sillabe**: la prima idea, come i vecchi cellulari: su, su, su = ba, ca, da;
  △ e ○ scorrono le vocali; ✕ conferma. 1,6–2 pressioni per carattere: scorrere costa.
- **Sillabe**: mezza pressione per carattere; **con la predizione** 0,41.
- **Lua**: la predizione vale di più (71 → 46), perché le parole del codice sono poche e
  si ripetono (function, then, end, btn, x).

Sui testi del corpus **che il dizionario non ha visto** (ogni gruppo tolto a turno,
136 245 caratteri): sillabe **0,518** pressioni per carattere, steno **0,464**, sillabe +
predizione **0,456**, steno + predizione **0,422**.

## 6. Le prime 10 parole

`Ciao Marco, domani sera giochiamo da me? La console nuova` (57 caratteri, 10 parole):
**23 pressioni** con le sillabe e la predizione, **21** in steno (una tastiera ne vuole
57). `_` è lo spazio; "suggerimento" è la parte scritta dalla predizione (R2 il primo, L2
il secondo, L2 + R2 il terzo). Generate da `make pad-bench`.

| # | Sillabe + predizione | Scrive | | Steno + predizione | Scrive |
|---|---|---|---|---|---|
| 1 | ↓ + ✕ | Ci | | ↓ + △ + □ | Cia |
| 2 | L2 + R2 | ao_ (suggerimento) | | R1 + ○ | o_ |
| 3 | L1 + R1 | (maiuscola) | | L1 + R1 | (maiuscola) |
| 4 | L2 + R2 + → + △ | Ma | | L2 + R2 + → + △ | Ma |
| 5 | R2 + → | r | | R2 + → | r |
| 6 | ↓ + ○ | co | | ↓ + ○ | co |
| 7 | L2 + R1 + □ | ,_ | | L2 + R1 + □ | ,_ |
| 8 | L2 + ↑ + ○ | do | | L2 + ↑ + ○ | do |
| 9 | L2 | mani_ (suggerimento) | | L2 | mani_ (suggerimento) |
| 10 | R2 + ↑ + □ | se | | R2 + ↑ + □ | se |
| 11 | R1 + R2 + → + △ | ra_ | | R1 + R2 + → + △ | ra_ |
| 12 | L2 + ↓ + ✕ | gi | | L2 + ↓ + △ + ○ | gio |
| 13 | ○ | o | | L2 + ↓ + → + △ + □ | chia |
| 14 | ↓ | c | | L2 + R1 + R2 + → + ○ | mo_ |
| 15 | R2 + ↓ + ✕ | hi | | L2 + R1 + ↑ + △ | da_ |
| 16 | R2 | amo_ (suggerimento) | | L2 + R2 + → + □ | me |
| 17 | L2 + R1 + ↑ + △ | da_ | | L2 + R1 + ○ | ?_ |
| 18 | L2 + R2 + → + □ | me | | L2 + R1 + → + △ | La_ |
| 19 | L2 + R1 + ○ | ?_ | | R2 | console_ (suggerimento) |
| 20 | L2 + R1 + → + △ | La_ | | → + ✕ + ○ | nu |
| 21 | R2 | console_ (suggerimento) | | L2 + R2 | ova_ (suggerimento) |
| 22 | → + ✕ + ○ | nu | | | |
| 23 | L2 + R2 | ova_ (suggerimento) | | | |

Si legge così: "Ciao" = giù + ✕ (ci), poi L2 + R2 prende il terzo suggerimento, "Ciao ".
"Marco" a metà frase vuole la maiuscola (L1 + R1), poi ma, r, co. "giochiamo" con le
sillabe è gi, o, c, hi e la predizione finisce "amo "; in steno gio e chia sono un
accordo ciascuno. "La" ha la maiuscola da sé (dopo il punto di domanda), e "console"
arriva prima ancora di una lettera, perché dopo "la" è la parola più frequente.

## 7. Da provare sul Pi

Con un DS4 (o un altro pad con L2/R2), nella scheda **Dev > bm Code**:

1. **Share**: in basso a destra compare il pannello "PAD sillabe"; la barra in basso dice
   `PAD sillabe lua`.
2. Scrivi una riga di commento: R2 + ✕ due volte (con R1 la seconda) fa `-- `; poi ↓ + ✕,
   △, ○ + R1 fanno "ciao ". La barra dice `PAD sillabe it`: nel commento le parole sono
   italiane.
3. L2 + ↑ + ○ (do): il pannello propone tre parole, la prima in blu-grigio anche nella
   riga; R2 la scrive, e la parte scritta resta verde.
4. □ poi L1 + L2: "è". L1 cancella, tenuto continua.
5. Start (a capo), ↓ (c), L2 + → (l): il pannello propone `cls`; il suo tasto scrive
   `cls(`.
6. Tieni L2, poi R2, poi Share: il pannello cambia etichette (banchi, punteggiatura,
   numeri). Start + croce sposta il cursore.
7. Share spegne; Start apre il menu: **Pad practice...**, "italiano": ricopia il testo
   seguendo il prossimo accordo (scritto sotto e illuminato nel pannello). Alla fine:
   pressioni fatte contro le 41 del migliore e le 100 della tastiera.
8. Menu, **Pad: syllables / steno**, e di nuovo l'esercizio: le diagonali con un grilletto
   scrivono tr, ch, sc, pr...

Il test `test_code_pad_typing` in QEMU fa gli stessi passi con un DS4 simulato.

## 8. File e test

- `src/ai/padtype.lua` (`require "padtype"`): accordi, tabelle, predizione, pannello,
  esercizio, `encode` (le pressioni minime per un testo: benchmark e guida). Le tabelle
  sono una sola copia: il motore, il pannello e `encode` le leggono da lì.
- `src/ai/words/`: i testi italiani e inglesi scritti per bm e il lessico italiano
  (`README.md`); `scripts/mkpadwords.py` → `build/padwords.lua` (`require "padwords"`,
  nel kernel, ~320 KB di testo).
- `scripts/padsyll.py` (`make pad-stats`): il conteggio delle sillabe.
- `tests/pad/pad_test.lua` (`make test-pad`, dentro `make test`): accordi, regole di
  scrittura (maiuscole, accenti, doppie, punteggiatura dopo un suggerimento, L1 che toglie
  il suggerimento), suggerimenti nel codice, pannello, i tre testi riscritti dalle pressioni
  di `encode` in tutte e quattro le modalità, l'esercizio e la sua guida seguita passo per
  passo.
- `tests/pad/bench.lua` (`make pad-bench`): la tabella, le prime 10 parole e il corpus
  con i gruppi tolti a turno dal dizionario.
- `tests/qemu_test.py`, `test_code_pad_typing`: bm Code con un DS4 simulato.

**L2 e R2**: prima il kernel non li leggeva. Ora `pad()` li dà come 4096 e 8192: DS4 (USB
e Bluetooth), Xbox 360 (grilletti oltre un quarto della corsa), pad generici (pulsanti 7 e
8, che prima facevano da A e B come era stato per L1/R1).

## 9. Limiti e passi successivi

- Gli accordi con 4-5 tasti (due grilletti, croce, due tasti) si imparano ma sono lenti
  all'inizio; le lettere su L2 + R2 sono le meno usate per questo.
- Le diagonali di un D-pad DS4 si sbagliano facilmente: con le sillabe una diagonale da
  sola scrive x w k y; se capita per errore, L1.
- Il dizionario viene da ~28 000 parole: va bene per i messaggi e i giochi, meno per testi
  tecnici lunghi. Si arricchisce aggiungendo testi in `src/ai/words` (poi `make`).
- Dopo: le parole nuove scritte dall'utente nel dizionario (salvate), lo stesso modulo nel
  pannello dell'assistente, nel Sound editor e nello studio 3D (nomi dei file, testi).
