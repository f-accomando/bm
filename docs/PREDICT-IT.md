# Completamento delle parole (M30)

Mentre scrivi una parola con la tastiera, bm propone la più probabile e ne mostra **il resto
in blu-grigio**, dopo il cursore; **Tab** la scrive, e la parte scritta resta **verde** fino
al tasto successivo. Se non serve, si continua a scrivere: il suggerimento cambia a ogni
lettera e sparisce da solo.

- **Dove**: in **bm Code** (scheda Dev) nel codice, nei commenti, nelle stringhe e nelle
  righe `#entry:`; in **Trova** e **Sostituisci**; nella domanda del **pannello
  dell'assistente** (F6).
- **Riga di stato** di bm Code: `Tab: parola  (also: altre, due)`, la parola che Tab
  scriverebbe e le altre che il dizionario aveva in mente (si ottengono scrivendo un'altra
  lettera).
- **Tab prima di una parola** (inizio riga, dopo uno spazio) indenta come prima; nel
  pannello dell'assistente, senza suggerimento, cambia modo (code, sprite, any).
- **Menu** di bm Code (Esc): **Word completion** on/off, **Words in comments** it/en (le
  parole dei commenti e delle stringhe). Le due scelte restano nella sessione.
- **Modulo**: `require "predict"` (`src/ai/predict.lua`), qualsiasi cartuccia può usarlo;
  i dizionari sono `require "words"`, fatti da `scripts/mkwords.py`.

Su un testo italiano di 100 caratteri (100 tasti, una maiuscola con Shift conta uno) il
completamento ne fa risparmiare **17**, sul codice Lua **19%**, sulle domande
all'assistente **26%** (i numeri completi più sotto).

## 1. Dove scrivi, quali parole

Il dizionario cambia da solo con il punto in cui si trova il cursore:

| Dove | Parole | Esempi |
|---|---|---|
| codice in bm Code | Lua: parole chiave, API della base di conoscenza dell'assistente, nomi dei giochi di bm, nomi della scheda aperta | `f` → `function` su una riga non indentata, `for` su una indentata; `if bt` → `btnp`; `x = ma` → `math.random`; `string.fo` → `string.format` |
| dopo `--` (commento) | italiano (o inglese, dal menu), un po' dei nomi della scheda | `-- muovi il gioc` → gioco |
| in una stringa | italiano (o inglese) | |
| riga `#entry:` | italiano + le domande della base di conoscenza | `#entry: come faccio a sal` → salvare |
| pannello dell'assistente | italiano + le domande della base di conoscenza | `co` → come |
| Trova, Sostituisci | Lua e nomi della scheda | `_up` → `_update` |
| Vai alla riga, nomi dei file | nessuna | |

Nel commento e nella stringa il contesto parte dal segno (`--`, `"`): la prima parola non
dipende dal codice che c'è prima. Le **domande all'assistente** hanno un dizionario loro:
le righe `ask:`, i titoli e le parole chiave della base di conoscenza (`src/ai/kb`, non
`tests.txt`), mescolati con l'italiano con peso 2 a 1.

Regole della parola scritta:

- **Accenti**: non servono per trovarla, Tab li mette: `perch` → perché, `citt` → città.
- **Maiuscole**: una maiuscola all'inizio resta (`Con` → Console); tutta maiuscola resta
  tutta maiuscola (`CA` → CASA). I nomi propri del dizionario hanno la loro maiuscola
  (`marc` → Marco). Nel codice conta il dizionario (Lua distingue le maiuscole).
- **Tab scrive solo la parola**: niente spazio né parentesi dopo, così il tasto
  successivo è sempre quello che ci si aspetta.
- `end`, `else`, `until` scritti con Tab tornano all'indentazione del loro blocco, come
  scritti a mano; Tab e la parola scritta prima sono un passo solo di Ctrl+Z.

## 2. Il modello

Un **n-gramma**: quante volte ogni parola è scritta e quali parole la seguono, contate da
`scripts/mkwords.py` (`make` lo rifà quando cambiano i testi) su:

- **it**: i testi di `src/ai/words` scritti per bm (racconti, messaggi, giochi, tecnica,
  lettere, informativi: 23 000 parole) e il lessico `it_lessico.txt` (6 500 forme che ai
  testi mancano): 8 450 parole;
- **en**: i testi inglesi di `src/ai/words` (4 500 parole): 1 140 parole;
- **lua**: il Lua dei giochi di bm (senza commenti e stringhe), il codice della base di
  conoscenza e tutti i nomi delle sue API con la firma: 3 350 parole;
- **ask**: le domande della base di conoscenza: 1 770 parole.

Il punteggio di una parola che inizia come quella scritta è `0,4 × frequenza + quante volte
segue la parola prima`; nel codice "la parola prima" di una riga che comincia è `^` se la
riga è indentata, `^0` se non lo è (lì si scrivono `function` e `local`, dentro `for`,
`if`, `local`). Nel codice si aggiungono i nomi della scheda aperta, contati ogni 30
suggerimenti.

Sul Pi: il dizionario si legge **un pezzo per fotogramma** dall'apertura di bm Code
(`predict.preload`), così la prima parola lo trova pronto; un suggerimento è una ricerca
binaria nella tabella ordinata, e per i prefissi di una o due lettere (che valgono mille
parole) si tengono le otto più scritte più quelle che seguono la parola prima. Sul PC:
0,02 ms per suggerimento, 14 ms per leggere il dizionario Lua (sul Pi Zero una ventina di
volte di più, distribuiti sui fotogrammi).

Il formato di `build/words.lua` (testo in code page 437, come la console):

- `uni`: `parola conteggio[ f]` per riga, ordinate per chiave (minuscola, senza accenti);
  ` f` = una funzione;
- `big`: `prima totale dopo n dopo n ...` per riga, le 24 parole più frequenti dopo `prima`;
- `api` (solo lua): `nome firma` per riga.

### Da Lua

```lua
local predict = require "predict"
local c = predict.complete(testo_prima_del_cursore, { lang = "lua", words = predict.count_words(righe) })
if c then
  print(c.rest, x, y, predict.C_GHOST)      -- il resto, in blu-grigio
  -- Tab: c.prefix (quello che è scritto) diventa c.word
end
```

`lang` è un dizionario (`"it"`, `"en"`, `"lua"`, `"ask"`), una miscela con i pesi
(`{it = 1, ask = 2}`) o `"none"`; `words` e `words_weight` i nomi del codice; `min` il
prefisso più corto (1). Il risultato ha anche `list` (le prime tre) ed `ending` (`(` dopo
una funzione, uno spazio dopo una parola chiave o una parola di testo), per chi li vuole.
Altre funzioni: `candidates`, `word_at`, `count_words`, `preload`, `plain`, `from_utf8`.

## 3. Benchmark

`make predict-bench` (`tests/predict/bench.lua`): i tasti per scrivere i testi di
`tests/predict/texts.lua` con la tastiera di bm Code, prima senza e poi con Tab (premuto
quando il suggerimento è la parola del testo, o il suo inizio, e scrive almeno due
lettere). Nel codice Invio tiene l'indentazione.

| Testo | Caratteri | Tasti | Tasti con Tab | Tab | Risparmio |
|---|---|---|---|---|---|
| italiano | 100 | 100 | 83 | 7 | 17% |
| english | 100 | 100 | 88 | 7 | 12% |
| lua | 100 | 94 | 76 | 7 | 19% |

- Le **155 domande all'assistente mai viste** (`src/ai/kb/tests.txt`, 5 332 caratteri):
  1 tasto per carattere senza completamento, **0,885** con il dizionario italiano,
  **0,738** con italiano + domande (il 26% in meno).
- I **testi di `src/ai/words` che il dizionario non ha visto** (ogni gruppo tolto a turno
  dal dizionario, 136 245 caratteri): **0,868** tasti per carattere, il 13% in meno.
  Sui testi visti il risparmio è più alto, ma non conta.

Le prime 10 parole del testo italiano (57 caratteri, 47 tasti, 4 Tab):

| Parola | Tasti | Quanti |
|---|---|---|
| Ciao | C i a o | 4 su 4 |
| Marco, | M a r c o , | 6 su 6 |
| domani | d o m Tab (ani) | 4 su 6 |
| sera | s e Tab (ra) | 3 su 4 |
| giochiamo | g i o c h i Tab (amo) | 7 su 9 |
| da | d a | 2 su 2 |
| me? | m e ? | 3 su 3 |
| La | L a | 2 su 2 |
| console | c Tab (onsole) | 2 su 7 |
| nuova | n u o v a | 5 su 5 |

Lo spazio dopo ogni parola è un tasto.

## 4. Le sillabe dell'italiano

`make syllables` (`scripts/syllables.py`) le conta, accenti esclusi, sugli stessi testi
(23 118 parole) e sul lessico (8 523 parole diverse), tagliate con le regole
dell'ortografia (ca-sa, piz-za, can-to, pa-dre, pa-e-se, pia-no):

- **In teoria**: 16 consonanti italiane (b c d f g h l m n p q r s t v z) per 5 vocali
  fanno **80 sillabe consonante + vocale**, **85** con le vocali da sole; più **31** che si
  scrivono con due o tre lettere per un suono (che chi ghe ghi; gna gne gni gno gnu;
  glia glie gli glio gliu; sce sci scia scio sciu sche schi; cia cio ciu gia gio giu;
  qua que qui quo).
- **Nei testi**: 46 251 sillabe (2 per parola), **1 078 diverse** (1 277 con il lessico).
- **Poche fanno quasi tutto**: le **35** più frequenti sono **metà** delle sillabe
  scritte, le 130 l'80%, le 247 il 90%, le 382 il 95%, le 727 il 99%.
- **Consonante + vocale** è il **52,8%** delle sillabe, una vocale sola il 6,4%.
- Vocali: a 25%, e 24%, o 22%, i 18%, u 5%, poi i dittonghi ia, io, ie, uo. Fine della
  sillaba: aperta 75%, n 8%, l 5%, r 4,5%.

A gruppi di quattro, per frequenza:

| Gruppo | Sillabe | Insieme | In tutto |
|---|---|---|---|
| 1 | e 2,8%, la 2,7%, to 2,6%, re 2,3% | 10,4% | 10,4% |
| 2 | di 2,3%, no 2,0%, ti 1,9%, ta 1,9% | 8,1% | 18,4% |
| 3 | na 1,7%, le 1,6%, co 1,5%, a 1,5% | 6,4% | 24,8% |
| 4 | che 1,5%, si 1,5%, te 1,3%, ra 1,3% | 5,6% | 30,4% |

| Gruppo | Consonante iniziale | Insieme | In tutto |
|---|---|---|---|
| 1 | s 12,4%, t 12,1%, c 10,3%, n 9,0% | 43,9% | 43,9% |
| 2 | l 8,3%, r 7,8%, d 7,8%, p 7,5% | 31,3% | 75,2% |
| 3 | m 6,7%, v 4,8%, g 4,5%, f 2,5% | 18,5% | 93,7% |
| 4 | b 2,3%, z 1,5%, q 1,2%, h 0,9% | 6,0% | 99,7% |

(lo script dà anche i gruppi delle sole sillabe consonante + vocale, e gli inizi e le fini
di sillaba più frequenti).

## 5. File e test

- `src/ai/predict.lua` (`require "predict"`): parole, dizionari, suggerimenti.
- `src/ai/words/`: i testi italiani e inglesi scritti per bm e il lessico italiano
  (`README.md`); `scripts/mkwords.py` → `build/words.lua` (`require "words"`, nel kernel,
  ~360 KB di testo).
- `carts/code/main.lua`: il completamento in bm Code (`place_at` sceglie codice, commento,
  stringa o `#entry:`); `src/ai/assist.lua`: quello della domanda.
- `tests/predict/predict_test.lua` (`make test-predict`, dentro `make test`): parole prima
  del cursore, dizionari, accenti e maiuscole, nomi della scheda, i tre testi riscritti con
  Tab (`tests/predict/typist.lua`) con meno tasti.
- `tests/predict/bench.lua` (`make predict-bench`): la tabella, le domande, le prime 10
  parole e il corpus con i gruppi tolti a turno.
- `tests/ai/panel_test.lua` (`make test-ai`): la domanda completata con Tab nel pannello.
- `tests/qemu_test.py`, `test_code_completion`: bm Code in QEMU (codice, commento,
  `#entry:`, Tab che indenta, Trova, pannello dell'assistente; i colori sullo schermo).

## 6. Limiti e passi successivi

- Il dizionario viene da ~28 000 parole: va bene per i messaggi, i commenti e i giochi,
  meno per testi tecnici lunghi. Si arricchisce aggiungendo testi in `src/ai/words` (poi
  `make`).
- Una parola che differisce solo per l'accento finale (`e` → è) non si completa: è della
  stessa lunghezza, non c'è un resto da mostrare.
- Dopo: le parole nuove scritte dall'utente nel dizionario (salvate), il completamento nel
  Sound editor e nello studio 3D (nomi, testi).
