title: bm Sound
accent: Sound
lang: it
kicker: Un editor di bm
subtitle: Suoni, effetti, pattern e canzoni per il tuo gioco, fatti sulla console.
footer: bm manuali - bm Sound

::: readme
**Che cos'è.** Il manuale di bm Sound, l'editor che crea gli effetti sonori e la musica di un gioco bm: un sintetizzatore a 8 voci, uno step sequencer e un creatore di canzoni.

**Come usarlo.** Leggi una volta i capitoli "Che cos'è bm Sound" e "Le pagine", poi segui i tutorial in ordine: effetto, pattern, canzone, importazione, riproduzione.

**Che cosa manca.** Ogni dato non ancora confermato è segnato da un'etichetta evidenziata DA COMPLETARE; nulla è inventato.
:::

# Che cos'è bm Sound

bm Sound è l'editor del **banco sonoro** di un gioco: i suoni, gli effetti, i pattern e le canzoni che il gioco riproduce con `sfx()` e `music()`.

| Pagina | Tasto | Che cosa ci fai |
|---|---|---|
| SOUNDS | F1 | uno strumento: onda, inviluppo, filtro, stanza ed eco |
| SFX | F2 | un breve effetto sonoro, passo per passo |
| PATTERN | F3 | un ciclo fino a 64 passi su 8 tracce |
| SONG | F4 | l'ordine in cui suonano i pattern |

## Che cosa fa

- Un **suono** è uno strumento. Il banco ne contiene 32.
- Un **effetto sonoro** (SFX) è una breve riga di al massimo 32 note, ognuna con il suo suono, volume ed effetto. Il banco ne contiene 64.
- Un **pattern** è una griglia di 8 tracce (una per voce) e fino a 64 passi. Il banco ne contiene 64.
- Una **canzone** è un elenco di al massimo 64 pattern, con tempo, swing, punto di ripetizione, eco e stanza. Il banco ne contiene 8.

## Perché conta

I suoni viaggiano dentro il file `.bm` del gioco, nella sua sezione AUDIO. Il gioco li chiede per numero: `sfx(1)` suona l'effetto 1, `music(0)` suona la canzone 0. Nessun file audio da portarsi dietro.

## Da sapere

- bm Sound ha 8 voci. Le tracce sono numerate da 1 a 8 e le voci da 0 a 7: la traccia `t` di un pattern suona sulla voce `t - 1` (lo schermo dice `TRACK 1 (voice 0)`).
- Il tasto F6 apre un assistente musicale che crea per te ritmi, melodie ed effetti sonori.
- Il tasto F7 apre Riff, dove scrivi un pattern come una riga di testo.
- Tutto quello che cambi si può annullare con Ctrl+Z.

# Aprire bm Sound

bm Sound modifica solo **progetti** (`.bme`). Un gioco (`.bm`) lo legge soltanto. È la regola di tutta la suite.

## Dal bm SDK

1. Avvia il **bm SDK** e apri il tuo progetto.
2. Nella pagina F1, nell'elenco *OPEN IN*, scegli **6 bm Sound**.
3. L'SDK salva il progetto e bm Sound si apre sullo stesso file.
4. Quando hai finito, premi Esc e scegli **Back to bm SDK**. Prima salva.

## Dal menu

[DA COMPLETARE: la voce esatta del menu che avvia bm Sound sulla console]

## Giochi e sound pack

Premi Esc, poi scegli **Open a game or a sound pack...** (oppure premi Ctrl+O). L'elenco mostra quello che c'è in `/carts`, nella radice della scheda, in `/bme` (progetti, anche i sound pack) e in `/bm/sounds` (sound pack più vecchi), con l'etichetta "game" o "sound pack".

- Se il file non ha ancora suoni, bm Sound lo dice e parti da banchi vuoti. **Save** ci scrive i suoni.
- Se apri un gioco (`.bm`), il primo salvataggio chiede *Save an editable copy: name.bme?* e da lì in poi lavori sulla copia.
- **New project** (Ctrl+N) parte da banchi vuoti: il suono 0 si chiama `SOUND` e la canzone 0 si chiama `SONG`, con l'ordine `{0}` (pattern 0). **The demo project** carica un banco con suoni e canzoni: premi START per ascoltare la canzone.

::: note
La barra in alto mostra le quattro pagine, `OCT` (l'ottava del pianoforte), il player (`STOP`, `SFX`, `PATTERN` o `SONG n`) e il nome del file. Un punto giallo prima del nome indica modifiche non ancora salvate.
:::

![La pagina SOUNDS: il nome, i tre grafici e le barre di un gruppo di parametri](shots/editor-sound-sounds.png)

# Le pagine

Cambi pagina con F1-F4, con Tab (pagina successiva), oppure sul pad con SELECT + sinistra/destra.

## SOUNDS

Qui costruisci uno strumento. I 32 slot sono nell'elenco a sinistra. Ne scegli uno con PgUp / PgDn (pad: SELECT + su/giù).

I parametri sono in tre gruppi. Scendi oltre l'ultima riga per passare al gruppo successivo; la terza colonna dell'ultimo gruppo mostra i nomi dei gruppi.

| Gruppo | Parametri |
|---|---|
| SOUND | WAVE, DUTY, VOLUME, DETUNE; ATTACK, DECAY, SUSTAIN, RELEASE; BEND FROM, BEND TIME, VIBRATO, VIB SPEED |
| FILTER | CUTOFF, RESONANCE, MODE, KEY TRACK; FILT ENV, ENV DECAY, DRIVE, NOISE; LFO SPEED, LFO CUT, LFO DUTY, 8-BIT |
| WAVE & SPACE | due impostazioni dell'onda (i nomi cambiano con l'onda), FM FADE, FM FEEDBACK; PAN, ROOM, ECHO |

Le onde sono: SQUARE, TRIANGLE, SAW, NOISE, SINE, METAL, FM, PLUCK, SUPERSAW, ORGAN, SAMPLE, PINK, BROWN, CRACKLE. Le modalità del filtro sono LOW PASS, BAND PASS, HIGH PASS e NOTCH.

- La riga sopra le barre è il **nome** (8 caratteri). Premi Invio su di essa per rinominare il suono.
- Ogni barra cambia con `-` e `=`; `_` e `+` la cambiano di più.
- Premendo un tasto del pianoforte senti il suono a quella nota.
- START suona il suono come un breve accordo.
- Il tasto Backspace riporta il parametro scelto al valore predefinito.
- Nel menu Esc, **Instrument...** mette nel suono uno strumento già pronto (l'elenco suona ognuno mentre lo scegli), e **Samples...** ci mette un campione: quelli del banco e quelli del kit della console (`bd`, `sd`, `hh`, `oh`, `cp`, `rim`, `tom`, `cb`). L'onda diventa SAMPLE.

## SFX

Qui scrivi un effetto sonoro come una riga di passi. L'intestazione ha cinque campi: NAME, SPEED (millisecondi per passo, da 5 a 2000), STEPS (da 1 a 32), LOOP FROM e LOOP TO. Il loop ripete i passi tra i due punti mentre l'effetto suona; LOOP TO `OFF` significa nessun loop.

Sotto l'intestazione i 32 passi sono disegnati come corsie: l'altezza di una barra è la nota, la barra bianca sotto è il volume e una lettera indica l'effetto. Il riquadro in basso dice che cosa contiene il passo scelto. Modificare un passo oltre la fine dell'effetto allunga l'effetto.

Ogni passo ha una nota, un suono, un volume e un effetto. Gli effetti sono gli stessi in SFX e in PATTERN:

| Lettera | Effetto | Che cosa significa la quantità |
|---|---|---|
| G | GLIDE | quanti passi dura lo scivolamento |
| U | BEND UP | semitoni |
| D | BEND DOWN | semitoni |
| V | VIBRATO | ampiezza, in ottavi di semitono |
| T | TREMOLO | profondità, in percentuale |
| C | CHORD | l'accordo (OCTAVE, MAJOR, MINOR, SUS2, SUS4, MAJ7, MIN7, DOM7, DIM, AUG, POWER e altri) |
| A | ARPEGGIO | l'accordo che viene percorso |
| O | FADE OUT | quanti passi |
| I | FADE IN | quanti passi |
| R | RETRIGGER | quanti colpi |
| L | DELAY | l'inizio, in sedicesimi di passo |
| X | CUT | la fine, in sedicesimi di passo |

![La pagina SFX: i campi dell'intestazione, le corsie e il riquadro del passo](shots/editor-sound-sfx.png)

## PATTERN

Un pattern è una griglia: 8 righe (le tracce, numerate da 1 a 8) e fino a 64 passi, mostrati 16 alla volta. I riquadri in alto a destra mostrano le pagine da 16 passi.

L'intestazione ha tre campi: STEPS (da 1 a 64), TEMPO e SWING. Tempo e swing appartengono alla **canzone** che hai scelto, non al pattern: l'intestazione mostra quelli della canzone corrente.

- Ogni traccia ha a sinistra un'intestazione con il suo numero e il suono che ricevono le nuove note. Sull'intestazione, Invio **silenzia** la traccia (compare una `M`) e `-` / `=` cambiano il suono della traccia.
- Una nota è una cella colorata; il colore è più forte quando il volume è più alto. `--` in rosso è una nota OFF (la nota viene rilasciata). Un angolo bianco segna un passo con un effetto.
- Mentre suona una canzone o il pattern, il passo in esecuzione è illuminato.

![La pagina PATTERN: otto tracce, sedici passi](shots/editor-sound-pattern.png)

## SONG

Una canzone è una riga di al massimo 64 slot; ogni slot contiene il numero di un pattern. Le 8 canzoni sono nell'elenco a sinistra.

L'intestazione ha sei campi:

| Campo | Che cosa fa |
|---|---|
| NAME | il nome della canzone (8 caratteri) |
| TEMPO | battiti al minuto, da 40 a 255 |
| SWING | da 0 a 100%: il primo passo di ogni coppia è più lungo |
| LOOP | dove la canzone riprende dopo la fine (`OFF`: si ferma) |
| ECHO | il tempo dell'eco, in passi, da `AS IS` a 16 |
| ROOM | la grandezza della stanza, da `AS IS` a una sala grande |

L'ECHO e la ROOM di un suono dicono quanta parte di esso va all'eco e alla stanza della canzone.

Sotto gli slot un riquadro mostra il pattern scelto e una piccola immagine delle sue note.

![La pagina SONG: l'ordine dei pattern](shots/editor-sound-song.png)

## L'assistente (F6)

F6 (oppure **Compose with the assistant...** nel menu Esc) apre un pannello che crea ritmi, basi, linee di basso, arpeggi, melodie ed effetti sonori. Ognuno si ascolta mentre lo scegli, e A (Invio sulla tastiera) lo mette nel banco. Sulla tastiera la lettera `a` viene scritta nel testo di ricerca.

- Ritmi e basi vanno in nuovi pattern, con una canzone tutta loro.
- Linee di basso, arpeggi e melodie vanno sopra il pattern della pagina e ne seguono la tonalità e gli accordi.
- Un effetto sonoro va nel primo slot libero.
- Gli strumenti che usano diventano suoni del banco; un suono con lo stesso nome viene riusato.
- Ctrl+Z annulla.

![Il pannello dell'assistente sopra la pagina PATTERN](shots/editor-sound-assist.png)

Tasti nel pannello: Su e Giù scelgono, Sinistra e Destra cambiano la variante, Invio la mette nel banco, Esc chiude, Tab passa alla modalità successiva (pad: X), PgUp e PgDn scorrono, scrivendo si imposta la richiesta, Ctrl+U la cancella.

## Riff (F7)

F7 (oppure **Riff...** nel menu) apre un riquadro dove scrivi un pattern in una riga, per esempio `s "kick*4, ~ snare, hat*8"`. Invio lo suona con i suoni del banco e gli strumenti già pronti. Ctrl+Invio lo mette nel banco come canzone. Per il linguaggio vedi il manuale `riff`.

# Comandi

Il pad e la tastiera fanno le stesse cose. La riga in basso dello schermo mostra i tasti della pagina in cui sei. Tieni premuto F12 per vedere i tasti in qualsiasi momento.

| Azione | Pad | Tastiera |
|---|---|---|
| Muoversi | Croce direzionale | Tasti freccia |
| Aggiungere o togliere una nota, scegliere | A | Invio |
| Nota o valore -1 / +1 | A + su / giù | `-` / `=` |
| Ottava, o valore -10 / +10 | A + sinistra / destra | `_` / `+` |
| Suono del passo | Y + su / giù | `[` / `]` |
| Volume del passo | Y + sinistra / destra | `;` / `'` |
| Effetto | B + su / giù | `k` / `l` |
| Quantità dell'effetto | B + sinistra / destra | `o` / `p` |
| Cancellare il passo | X | Backspace |
| Ascoltare il passo, o inserire una copia (SONG) | Y | `i` |
| Suona / ferma | START | Spazio |
| Cambiare pagina | SELECT + sinistra / destra | F1-F4, Tab |
| Suono, effetto, pattern, canzone successivo o precedente | SELECT + su / giù | PgUp / PgDn |
| Menu | SELECT | Esc |
| Pianoforte, fila bassa | - | `z s x d c v g b h n j m` |
| Pianoforte, fila alta (un'ottava sopra) | - | `q 2 w 3 e r 5 t 6 y 7 u` |
| Ottava del pianoforte | - | `,` / `.` |
| Salvare | - | Ctrl+S |
| Salvare come nuovo sound pack | - | Ctrl+Shift+S |
| Aprire | - | Ctrl+O |
| Nuovo progetto | - | Ctrl+N |
| Annullare | - | Ctrl+Z |
| Copiare / incollare l'elemento | - | Ctrl+C / Ctrl+V |
| Esportare in un gioco | - | Ctrl+E |
| Provarlo nel gioco | - | F5 o Ctrl+R |
| Assistente | - | F6 |
| Riff | - | F7 |
| Uscire | START + SELECT | Esc, poi **Exit** |

Funziona anche il mouse: un clic sceglie, un doppio clic fa quello che fa A, la rotella cambia il valore sotto il puntatore e una barra trascinata a sinistra o a destra lo cambia.

::: note
Un breve tocco su SELECT apre il menu. Se premi SELECT con sinistra/destra o su/giù, cambia pagina o elemento e il menu non si apre.
:::

[DA COMPLETARE: verificare questi tasti del pad sul PowKiddy RGB30, dove nel resto di bm B conferma e A annulla; non ancora provato sulla console]

## Il menu Esc

| Voce | Che cosa fa |
|---|---|
| Play / Stop | avvia o ferma la musica o l'effetto (START) |
| Undo | annulla l'ultima modifica (Ctrl+Z) |
| Save | scrive nel file aperto; senza file, chiede il nome di un pack |
| Save as a new sound pack... | chiede 8 caratteri e scrive `/bme/name.bme` |
| Open a game or a sound pack... | apre un altro file (Ctrl+O) |
| Import from... | prende un suono, un effetto, un pattern o una canzone da un altro file |
| Export to a game... | scrive questo banco in un gioco |
| Instrument... | solo pagina SOUNDS: un suono già pronto |
| Samples... | un campione nel suono |
| Compose with the assistant... | apre l'assistente (F6) |
| Riff... | apre Riff (F7) |
| Try it in the game | con un gioco aperto: lo salva e lo avvia |
| Copy / Paste over / Clear this item | il suono, l'effetto, il pattern o la canzone della pagina |
| New project, The demo project | ricomincia; carica la demo |
| Controls | mostra l'elenco dei tasti |
| Back to bm SDK | solo se bm Sound è stato aperto dall'SDK; prima salva |
| Exit | chiede prima conferma se ci sono modifiche non salvate |

# Tutorial

## Creare un effetto sonoro

Creerai un breve suono da "moneta". Parti in bm Sound con un progetto nuovo (Esc, **New project**).

1. Premi F1 per andare a SOUNDS. Apri il menu Esc, scegli **Instrument...** e seleziona uno degli strumenti. L'elenco suona ognuno mentre ci passi sopra. Quello che scegli va nel suono 0.
2. Premi un tasto del pianoforte come `z`. Senti il suono 0 a quella nota.
3. Premi F2 per andare a SFX. Il cursore è sul passo 1 dell'effetto 0.
4. Premi Invio. Al passo 1 compare una nota e la senti. Usa il suono scelto nella pagina SOUNDS.
5. Premi la freccia destra e di nuovo Invio per aggiungere il passo 2. Premi `=` per alzarlo di un semitono; premi `_` o `+` per spostarlo di un'ottava sotto o sopra.
6. Premi Su sulla prima riga di passi per raggiungere l'intestazione. Spostati su SPEED con la freccia destra e premi `-` per rendere i passi più corti (meno ms) o `=` per renderli più lunghi (più ms). `_` e `+` lo cambiano di 10 ms.
7. Premi Spazio (START sul pad). Senti l'effetto. Premi di nuovo Spazio per fermarlo.
8. Per dare all'effetto uno scivolamento, torna a un passo con le frecce e premi `l` due volte: il riquadro del passo dice BEND UP. Poi premi `p` per una quantità maggiore.
9. Vai all'intestazione, scegli NAME, premi Invio e scrivi un nome di al massimo 8 caratteri. Premi Invio per confermare.

::: try
Fai del passo 1 una nota bassa e del passo 2 la stessa nota un'ottava sopra, con SPEED a 60 ms. Quella è una moneta.
:::

Se non senti nulla, controlla che il volume della console sia alto e che il passo abbia una nota (una corsia senza barra è vuota).

## Creare un pattern

1. Premi F3 per andare a PATTERN. Scegli un pattern libero con PgUp / PgDn.
2. Vai all'intestazione con Su (dalla traccia 1) e imposta STEPS a 16 con `-` / `=`. Imposta TEMPO allo stesso modo.
3. Torna all'intestazione della traccia 1 (il numero a sinistra): dall'intestazione STEPS / TEMPO / SWING premi prima Giù, poi premi la freccia sinistra. Premi `-` / `=` per scegliere il suono di quella traccia. Lo senti.
4. Vai alla prima cella e premi Invio. Viene messa una nota. Premi `=` per cambiarla. Premi Y o `i` per sentirla.
5. Riempi altri passi. Con i tasti del pianoforte (`z s x d c v`) viene messa una nota e il cursore passa al passo successivo.
6. Fai lo stesso sulla traccia 2 con un altro suono, per un basso o una batteria.
7. Premi Spazio. Il pattern suona e il passo in esecuzione è illuminato.
8. Per silenziare una traccia mentre ascolti, vai alla sua intestazione e premi Invio. Compare una `M`. Premi di nuovo Invio per riattivarla.

::: try
Metti un breve suono di rumore sulla traccia 1 ai passi 1, 5, 9 e 13, e un basso sulla traccia 2 ai passi 1 e 9.
:::

## Costruire una canzone

1. Premi F4 per andare a SONG. Scegli una canzone libera con PgUp / PgDn.
2. Vai al primo slot. Mostra un `+`. Premi Invio: lo slot 1 riceve un pattern (il pattern 0 per il primo; per i successivi, una copia dell'ultimo).
3. Premi `=` o `-` sullo slot per scegliere il numero del pattern. Premi `_` / `+` per saltare di 10.
4. Premi Invio su uno slot che ha già un pattern: bm Sound va alla pagina PATTERN su quel pattern. Modificalo, poi premi F4 per tornare.
5. Per ripetere un pattern, premi `i` sul suo slot: una copia viene inserita dopo di esso. Per togliere uno slot, premi Backspace.
6. Sali all'intestazione per impostare TEMPO, SWING e LOOP. Con LOOP impostato su `FROM 1` la canzone, alla fine, riparte dal primo slot.
7. Premi Spazio (START). La canzone suona dallo slot scelto, e una barra bianca mostra lo slot in esecuzione.

::: try
Fai una canzone di quattro slot: il pattern 0 due volte, poi il pattern 1, poi di nuovo il pattern 0. Imposta LOOP su `FROM 1`.
:::

## Importare un suono da un altro gioco

Puoi prendere un suono, un effetto, un pattern o una canzone da un altro gioco o sound pack.

1. Apri il menu Esc e scegli **Import from...**.
2. Nell'elenco scegli il file. L'elenco mostra giochi e sound pack. Mentre il cursore si ferma su un file, ne viene letto il contenuto: quanti suoni, effetti, pattern e canzoni ha.
3. Scegli che cosa prendere: *a sound*, *a sound effect*, *a pattern*, *a song* o *everything*.
4. Scegli l'elemento dall'elenco che segue.
5. Un messaggio dice dove è finito: per esempio `imported as sound effect 5`. I suoni usati da un effetto, un pattern o una canzone arrivano con esso, in slot liberi. Un suono già uguale nel tuo banco non viene copiato due volte. Un suono SAMPLE porta con sé anche il suo campione (un campione con gli stessi byte non viene copiato due volte).
6. Premi Ctrl+S per salvare.

::: warn
*everything* sostituisce l'intero banco su cui stai lavorando, dopo una domanda. Se non c'è uno slot libero, l'importazione si ferma con un messaggio come `no free sound slots`. Dopo un'importazione la cronologia dell'annulla viene cancellata.
:::

## Suonarlo nel tuo gioco

1. In bm Sound, con il tuo progetto aperto (vedi "Aprire bm Sound"), premi Ctrl+S. I suoni vengono scritti nel progetto.
2. Nel codice Lua del tuo gioco, richiama il banco per numero:

```lua
function _init()
  music(0)                         -- canzone 0, in loop come impostato in LOOP
end
function _update()
  if btnp(4) then sfx(1) end       -- effetto sonoro 1
  if btnp(8) then music(-1, 500) end
end
```

3. Per sentirlo subito nel gioco, premi F5 in bm Sound (Esc, **Try it in the game**). Prima salva, poi avvia il gioco.
4. Quando sei soddisfatto, compila il gioco dall'SDK con Ctrl+B. Questo scrive il `.bm` definitivo.

::: copy
sfx(n)            -- suona l'effetto sonoro n del banco
sfx(-1)           -- ferma gli effetti
music(n)          -- suona la canzone n
music(-1, 500)    -- ferma la musica, con dissolvenza in uscita di 500 ms
:::

Un effetto usa una voce libera e la traccia della musica su quella voce resta muta mentre l'effetto suona. Una canzone che usa solo le tracce da 1 a 6 lascia libere le voci 6 e 7 per gli effetti. Per tutte le chiamate vedi il manuale `api-sound`.

# Salvare ed esportare

- **Save** (Ctrl+S) scrive il banco nel file aperto. Con un gioco aperto, il primo salvataggio chiede la copia modificabile `name.bme`. Dopo un salvataggio la barra dice che il gioco suona i nuovi suoni.
- **Save as a new sound pack...** (Ctrl+Shift+S) chiede un nome di al massimo 8 caratteri e scrive `/bme/name.bme`. Un sound pack è un piccolo programma che suona le tue canzoni e i tuoi effetti; puoi riaprirlo dopo in bm Sound.
- **Export to a game...** (Ctrl+E) elenca i giochi e, dopo una domanda, scrive questo banco in quello che scegli. I suoi suoni e la sua musica vengono sostituiti da questi.

::: warn
Export sostituisce i suoni del gioco che scegli. La domanda nomina il file: leggila prima di confermare.
:::

# Consigli e limiti

## Limiti

| Che cosa | Limite |
|---|---|
| Suoni | 32 |
| Effetti sonori | 64, fino a 32 passi ciascuno |
| Pattern | 64, fino a 64 passi, 8 tracce |
| Canzoni | 8, fino a 64 slot ciascuna |
| Nomi | 8 caratteri |
| Velocità SFX | da 5 a 2000 ms per passo |
| Tempo | da 40 a 255 BPM |
| Note | da 1 a 127 (ottava del pianoforte da 0 a 8) |

## Consigli

- Tieni gli effetti brevi e metti i suoni che la musica non usa sulle tracce alte (voci 6 e 7), così gli effetti non tagliano la musica.
- Usa le onde NOISE, METAL e CRACKLE per colpi ed esplosioni; PLUCK e ORGAN per le melodie.
- Silenzia le tracce per sentire le altre mentre scrivi un basso o una melodia.
- Copia un pattern o un suono con Ctrl+C e incollalo con Ctrl+V per fare una variazione senza perdere l'originale.
- Le note delle file del pianoforte suonano sul suono della pagina; cambia l'ottava con `,` e `.`.
- Ctrl+Z annulla l'ultima modifica, anche un incolla, una cancellazione o un pezzo dell'assistente.
- Quando scegli un file in **Import from...**, una riga può dire `a sound bank this version cannot read`: quel file non si può importare.

[DA COMPLETARE: come portare un file WAV nel banco sulla console]
