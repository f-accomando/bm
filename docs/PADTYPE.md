# Scrittura col pad (assistive typing)

Scrivere **italiano, inglese e Lua col solo controller**, in due modi:

- **Composizione rapida** (*compose*): la croce scrive le consonanti e la predizione finisce
  la sillaba (↑ = "t", e il dizionario propone "te", "ta", "tr"...); □ e △ girano la
  sillaba sulle altre; tre parole intere sono sempre a portata di R2.
- **Tastiera su schermo** (*keyboard*): una griglia di tasti, un carattere alla volta con
  la croce, come sulle console.

**Share** passa dall'una all'altra (in bm Code accende e spegne la scrittura). Mentre la
composizione è accesa, un **overlay del controller** mostra cosa scriverebbero le quattro
frecce e i quattro tasti; tenendo L2, R2 o tutti e due mostra le loro proposte.

- **Dove**: in **bm Code** (scheda Dev), Share; nella cartuccia **Pad Typing** (scheda
  Games), che serve per esercitarsi e misurarsi; in ogni cartuccia con
  `require "padtype"` (`src/ai/padtype.lua`, nel kernel).
- **Numeri** (sezione 6): sui testi dell'esercizio, con i suggerimenti di parola, **1,09
  pressioni per carattere** in italiano, 1,06 in inglese, 1,33 in Lua; con la tastiera su
  schermo 3,8–4,0.

Riprende l'idea del branch `archive/pad-typing` (accordi di croce e tasti), con regole
nuove decise dall'utente il 2026-10-04: solo quattro direzioni, la doppia pressione, la
rotazione delle sillabe, L1/R1 per muoversi. Le regole qui sotto sono quelle di partenza:
le tabelle stanno in cima a `padtype.lua` (`CROSS`, `FACE`, `KB`) e si cambiano lì.

## 1. La composizione rapida

### Le regole

- **Solo su, giù, destra, sinistra.** Le diagonali non scrivono: una pressione conta quando
  la croce si ferma su una direzione sola (passare da ↑ a → scrive la →).
- **La pressione aspetta la sua doppia.** Una freccia scrive dopo **X ms** (250 di
  default; nella cartuccia da 150 a 600): se nel frattempo la stessa freccia torna giù,
  è la **doppia**, che dà la consonante *alternativa*, mai la stessa due volte (↑ = t,
  ↑↑ veloce = d). Qualunque altro tasto non aspetta: fa partire subito la prima. Per due
  "t" di fila si aspetta un attimo tra l'una e l'altra (ma "tt" di solito lo propone già
  la sillaba: "tu" + ↑ = "tutto" → "tto").
- **La predizione completa la sillaba.** Una consonante scrive anche quello che il
  dizionario si aspetta dopo: la sua doppia, una h, le vocali fino alla consonante
  successiva ("c" in "ciao" diventa "ciao", in "che" "che", "t" dopo "tu" "tto"). Se
  il dizionario si aspetta un'altra consonante, resta la consonante sola ("t" di "tre").
- **□ e △ girano la sillaba** appena scritta (□ la prossima, △ la precedente): prima le
  proposte del dizionario, dalla più probabile, poi la consonante da sola e le vocali in
  ordine (a e i o u), in italiano per ultime le accentate non proposte (ù ì ò é à è: △
  dall'inizio dà è). La sillaba che gira è azzurra; l'overlay ne mostra le vicine.
- **Una vocale a inizio parola**: □ (o △) quando non c'è una sillaba aperta; R1 chiude
  quella aperta se serve una vocale nuova.
- **○ cancella l'ultima cosa scritta**: una sillaba, una parola presa da R2, lo spazio, la
  punteggiatura (e rimette quello che c'era prima); se non sa cosa, un carattere. Tenuto,
  continua un carattere alla volta.
- **✕ è lo spazio, ✕✕ il punto** (". " con la maiuscola dopo; nel codice "." attaccato:
  "math" ✕✕ "floor").

### La croce

| Croce | sola | doppia | L2 | L2 doppia | R2 | R2 doppia | L2+R2 | L2+R2 doppia |
|---|---|---|---|---|---|---|---|---|
| ↑ | **t** | d | p | b | k | q | 1 | 5 |
| → | **n** | m | f | v | w | y | 2 | 6 |
| ↓ | **r** | l | g | h | j | _ | 3 | 7 |
| ← | **s** | c | z | x | [ | ] | 4 | 8 |

### I quattro tasti

| Tasto | solo | doppio | L2 | L2 doppio | R2 | L2+R2 | L2+R2 doppio |
|---|---|---|---|---|---|---|---|
| ✕ | spazio | punto | , | ; | 1ª parola | ( | ) |
| ○ | cancella | | ? | ! | cancella la parola | 0 | |
| □ | sillaba dopo | | ' | " | 2ª parola | = | + |
| △ | sillaba prima | | : | - | 3ª parola | { | } |

Lo 0 è ○ con L2+R2 (si ripete: 100 = 1, ○, ○); il 9 è l'8 girato con □ (o lo 0 con △):
anche le cifre girano.

### Gli altri tasti

| Tasto | Fa |
|---|---|
| L1 | indietro di un carattere (tenuto: continua) |
| R1 | avanti; con una sillaba aperta la accetta così com'è |
| L1 o R1 tenuto + croce | il cursore nelle quattro direzioni (su e giù le righe) |
| L1 + R1 tenuti | a capo (nel codice con il rientro: due spazi in più dopo `then`, `do`, `function(...)`, `{`; `end`, `else`, `until` tornano indietro da soli) |
| L3 | maiuscola per la prossima lettera (su una sillaba aperta la cambia); a inizio frase è automatica |
| R3 | la tastiera su schermo per **un** carattere (simboli rari: `* / < > # % ~`...), poi torna la composizione |
| Share | la tastiera su schermo (in bm Code: spegne) |
| Start | resta all'applicazione (pausa, menu) |

### Perché queste consonanti

Contate come inizio di sillaba (consonante + doppia + h + vocali, la regola della
composizione) sui testi di `src/ai/words` e sul Lua dei giochi, con peso ½ italiano, ¼
inglese, ¼ Lua:

n 12,1% · t 11,7 · r 10,1 · s 8,7 · l 8,4 · c 7,0 · d 5,9 · p 4,8 · m 4,6 · f 3,0 · g 2,8 ·
v 2,5 · b 2,1 · w 1,2 · k 0,9 · x 0,9 · h 0,8 · z 0,6 · y 0,5 · q 0,5 · j 0,1 (una vocale
a inizio sillaba il 10,6%).

- **Frequenza**: le otto più scritte (n t r s l c d p: il 69% delle sillabe) non hanno
  grilletto; le quattro prime con una pressione sola, quattro con la doppia.
- **Intuitività**: la doppia è la *gemella* della singola: t/d (la stessa con la voce),
  n/m (nasali), r/l (liquide), s/c (il suono "s": *city*, "sc"). Ogni direzione tiene la
  sua famiglia nei livelli: ↑ le occlusive (t d · p b · k q), → le nasali e i suoni
  morbidi (n m · f v · w y), ↓ r l · g h · j, ← le sibilanti (s c · z x). L2 sono le
  otto seguenti, R2 le lettere rare dell'inglese e del codice, L2+R2 i numeri in senso
  orario da ↑ (1 2 3 4, doppi 5 6 7 8).
- **Comodità**: L2 con la croce si fa con una mano sola (indice e pollice sinistri); le
  parole (R2 + ✕ □ △) con la destra. La h quasi non serve da sola: viene con la sillaba
  (che, chi, the, she). Le doppie dell'italiano vengono con la sillaba (tto, zza, lla).

### Le parole (R2)

Tenendo R2, ✕ □ △ scrivono la **1ª, 2ª, 3ª parola proposta** (le stesse sono sempre al
centro dell'overlay, e il resto della prima si vede in blu-grigio dopo il cursore):
quelle che finiscono la parola scritta o, dopo uno spazio, quelle che la seguono più
spesso ("Ciao Marco, doma" → domani). La parola ha il suo accento e la sua maiuscola, e
uno **spazio "morbido"**: la punteggiatura che segue si attacca alla parola ("domani" +
L2 ✕ = "domani, "), ✕ dopo non ne aggiunge un altro. Nel codice le funzioni portano la
loro parentesi (`cl` → `cls(`), le parole chiave lo spazio. R2 + ○ cancella la parola
prima del cursore.

Le parole vengono dal completamento di bm Code (`require "predict"`, guida in
[PREDICT.md](PREDICT.md)) e seguono il posto del cursore: nel codice Lua, dopo `--` e
nelle stringhe l'italiano o l'inglese, nelle righe `#entry:` le domande all'assistente.

## 2. La tastiera su schermo

```
1 2 3 4 5 6 7 8 9 0          ! " # $ % & / ( ) =
q w e r t y u i o p          + - * < > [ ] { } _
a s d f g h j k l '          à è é ì ò ù ; : @ \
z x c v b n m , . ?          | ^ ~ ` « » ° £ ± ß
shift  #+  space  del enter
```

La croce sposta il tasto acceso (tenuta, continua; ai bordi si gira), **✕** lo scrive,
**□** spazio, **○** cancella, **△** cambia pagina (lettere / simboli), **L2** (o L3)
maiuscola, R2 + ✕ □ △ le parole come nella composizione, L1 / R1 e L1 + R1 come sopra.
La maiuscola a inizio frase è automatica. Share torna alla composizione (R3 anche).

## 3. L'overlay

Largo 340 pixel e alto 132 (`pt.size()`), disegnato dall'applicazione dove vuole
(`pt.draw(x, y)`):

- in alto L2, L1, R1, R2 (accesi quando sono tenuti) e il livello (abc, L2, R2 words,
  123 code);
- a sinistra la **croce**: per ogni freccia la sillaba che scriverebbe adesso ("to", "sa":
  la predizione) e, piccola, la doppia; la freccia che aspetta la doppia è gialla;
- a destra i **quattro tasti** nei colori dei simboli del DS4 (△ verde, ○ rosso, ✕ blu,
  □ rosa), con quello che fanno nel livello tenuto e, piccolo, il doppio;
- al centro le **tre parole** di R2;
- in basso la **sillaba che gira** con le vicine (□ avanti, △ indietro), oppure i tasti
  L3, R3, L1+R1, Share.

Con la tastiera su schermo l'overlay è la tastiera, con le parole sotto.

## 4. bm Code

**Share** accende la scrittura col pad (o il menu, *Pad typing*): l'overlay compare sotto
il codice, la riga di stato dice `PAD compose lua` (o `it`, `en`, `ask` secondo il posto),
al cursore si vedono la pressione che aspetta (gialla), il resto della parola (blu-grigio),
la sillaba che gira (azzurra) e quello che una parola ha appena scritto (verde). Start
apre il menu; Share spegne e la croce torna a muovere il cursore. La tastiera del PC
continua a funzionare insieme.

## 5. Pad Typing (la cartuccia)

`carts/typing`, nella scheda Games: esercizio e prova di scrittura.

- **Menu**: lingua (Italiano, English, Lua), modo iniziale (compose o keyboard), attesa
  della doppia (150–600 ms), testo (otto per lingua, scritti per bm, o *free writing*),
  il prossimo tasto mostrato sì / no; sotto i record di ogni lingua. Start comincia.
- **Esercizio**: il testo da ricopiare; quello scritto bene diventa bianco, un errore
  rosso su fondo scuro, la sillaba che gira azzurra, il resto della parola proposta
  blu-grigio. Sotto, **next:** il prossimo tasto (le icone del pad, ×2 per la doppia),
  calcolato da `pt.coach`. A sinistra modo, tempo, caratteri al minuto, pressioni (e per
  carattere); a destra i tasti principali.
- **Fine**: tempo, caratteri al minuto, pressioni, pressioni per carattere, quante volte ○
  ha cancellato, con "best!" sui record (salvati con `save()`). ✕ testo dopo, △ di nuovo,
  ○ menu. Start durante l'esercizio è la pausa (Continua, Ricomincia, Testo dopo, Menu).

## 6. Numeri

`make test-padtype`: un dattilografo simulato preme quello che dice `pt.coach`
fotogramma per fotogramma (pressione di due fotogrammi, grilletto lasciato tra un tasto
e l'altro: ogni grilletto conta), su tutti gli otto testi di ogni lingua:

| Lingua | Caratteri | Composizione | Tastiera su schermo |
|---|---|---|---|
| italiano | 837 | 909 pressioni, **1,09** a carattere | 3 202, 3,83 |
| inglese | 806 | 851, **1,06** | 2 968, 3,68 |
| Lua | 717 | 955, **1,33** | 2 887, 4,03 |

Senza le parole di R2 la composizione fa 1,27 in italiano: le parole tolgono il 15%.
Dove vanno le pressioni in italiano: un terzo a girare le sillabe (□ △), un terzo alle
consonanti (doppie e grilletti compresi), il resto a parole di R2, spazi e punteggiatura.
Pad Typing in bmhost, col primo testo di ogni lingua: 1,04 (it), 1,18 (en), 1,08 (Lua)
pressioni a carattere.

## 7. Da Lua

```lua
local pt = require "padtype"
pt.set({ delay = 0.25, fallback = "keyboard" })   -- o "off": Share spegne
pt.on("compose")                                   -- "keyboard", nil: spenta
local host = pt.text_host("it")                    -- un testo; o un host proprio:
--   host.before()  il testo prima del cursore, sulla sua riga
--   host.insert(s), host.erase(n), host.newline(), host.move(dir)
--   host.lang      "it", "en", "lua", "ask", {it = 1, ask = 2} o "none"
--   host.prose     maiuscole a inizio frase (di default: non per "lua")
--   host.words     i nomi del codice (predict.count_words)
function _update()
  if not pt.update(host) then
    -- il pad è dell'applicazione (spenta)
  end
end
function _draw()
  -- il testo; al cursore pt.pending() (P.C_PEND), pt.ghost() (C_GHOST),
  -- le pt.open_len() lettere prima (C_OPEN), le pt.flash() (C_PRED)
  pt.draw(150, 226)
end
```

Altre funzioni: `pt.mode()`, `pt.presses()` / `pt.reset_count()`, `pt.clear()` (un testo
nuovo), `pt.idle(host)` (i fotogrammi in cui un menu ha il pad), `pt.commit()` (la
pressione che aspetta, subito), `pt.suggestions()`, `pt.syllables(host, prima, consonante)`,
`pt.coach(host, testo)` (il prossimo tasto: `{hold, tap, double, both, wait, label}`),
`pt.align(scritto, testo)` (il Lua confrontato come se `end` fosse già rientrato),
`pt.preload(lingue)`, `pt.TEXTS` (i testi dell'esercizio), `pt.CROSS`, `pt.FACE`, `pt.KB`.

## 8. File e test

- `src/ai/padtype.lua` (`require "padtype"`, nel kernel, in bmhost e nell'immagine della
  RGB30): regole, sillabe, tastiera, overlay, `coach`, `text_host`, i testi.
- `carts/typing/main.lua` (Pad Typing, copertina da `scripts/mkcovers.py`);
  `carts/code/main.lua` (bm Code: `pad_host`, Share).
- `tests/padtype/pad_test.lua` (`make test-padtype`, in `make test`): le regole una per
  una (attesa e doppia, livelli, sillabe e rotazione, ○, parole, punteggiatura, L1/R1,
  L1+R1, L3, Share, R3, diagonali) e tutti i testi scritti fino in fondo nei due modi,
  con i limiti di pressioni a carattere.
- `tests/padtype/script.lua`: lo stesso dattilografo scrive lo script d'ingresso di
  bmhost; `make test-padtype` fa copiare a Pad Typing il primo testo di ogni lingua fino
  al risultato (fotogrammi in `build/padtype/`).
- `tests/qemu_test.py`, `test_pad_typing`: un DS4 simulato in QEMU, Pad Typing dalla
  scheda Games (sillaba, doppia, ○, numeri, punto, tastiera su schermo, parola, a capo,
  pausa, esercizio) e Share in bm Code.

## 9. Limiti e passi successivi

- Da provare sul Pi con un DS4: l'attesa giusta per la doppia (250 ms è un'ipotesi), il
  peso delle rotazioni, se le doppie s/c e r/l vengono naturali.
- La RGB30: i bit di `pad()` sono le posizioni dei tasti, l'overlay usa i colori del DS4;
  non è ancora provata lì.
- Un servizio del kernel per scrivere da qualunque schermo (password del WiFi, nomi dei
  file: lo spunto R2 della roadmap) potrebbe usare questa tastiera e la composizione.
- Le parole nuove dell'utente nel dizionario; il pannello dell'assistente e i prompt di
  bm Code (Trova, Vai alla riga) ancora senza la scrittura col pad.
