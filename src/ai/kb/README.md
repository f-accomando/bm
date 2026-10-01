# Base di conoscenza dell'assistente (M30)

Quello che l'assistente sa: un file `.txt` per argomento, compilato nel kernel da
`scripts/mkassist.py` insieme alla rete addestrata (`src/ai/assist.weights`).
`tests.txt` contiene le domande di prova (mai usate per l'addestramento).

## Una voce

```
== move_arrows
kind: howto
title: Muovere un personaggio con le frecce
ask: come muovo il personaggio con le frecce | muovere uno sprite con i tasti
ask: move the player with the arrow keys
keys: muovere, frecce, move
see: spr, move_8dir
text:
btn(0..3) sono sinistra, destra, su, giù: finché il tasto è premuto si
cambia la posizione.
code:
if btn(0) then x = x - 2 end
if btn(1) then x = x + 2 end
```

| Campo | Cosa |
|---|---|
| `== id` | inizio della voce: lettere minuscole, cifre, `_` e `.`, unico |
| `kind:` | `api` (una funzione), `howto` (un esempio con il codice), `error` (un messaggio d'errore di Lua), `tip` (un consiglio), `sprite` (una ricetta di sprite), `none` (domande fuori tema: non si mostrano, servono a dire "non so") |
| `name:` | per le `api`: il nome della funzione. Se è scritto nella domanda o è la parola sotto il cursore, la voce sale in cima (un nome che è anche una parola comune, come `tempo`, conta solo sotto il cursore o scritto come chiamata, `tempo(`: elenco in `src/ai/assist.c`) |
| `title:` | la riga nell'elenco delle risposte |
| `ask:` | domande che portano qui, separate da `\|`, in italiano e in inglese: sono gli esempi su cui si addestra la rete. Più sono e più varie, meglio è (6-12) |
| `keys:` | parole che, se ci sono nella domanda, alzano un po' il punteggio |
| `see:` | altre voci collegate (devono esistere): compaiono sotto la risposta |
| `text:` | la spiegazione, su più righe, fino al campo successivo; le lettere accentate vanno bene (il font è CP437; `È` diventa `E'`) |
| `code:` | il codice che "Invio" inserisce nell'editor: ASCII, righe di al massimo 72 caratteri, deve compilare con Lua 5.4 (`make test-ai` lo controlla) |
| `gen:` | per gli `sprite`: la ricetta di `src/ai/sprite.c` (`ship`, `slime`, `coin`...) |

## Dopo una modifica

- `make`: ricostruisce `build/assist.bin`. Una voce nuova si trova subito con le
  sue parole (`name`, `keys`), non ancora con la rete.
- `make ai-model` (serve numpy: `pip install numpy`): riaddestra la rete su tutte le
  voci, in circa un minuto, e stampa quante domande di `tests.txt` hanno la
  risposta giusta al primo posto. Poi `make` e commit di `src/ai/assist.weights`.
- `make test-ai`: rete in C uguale a quella in Python bit per bit, almeno il 90%
  delle domande di prova con la risposta giusta tra le prime tre, ogni esempio
  di codice che compila, il pannello.
