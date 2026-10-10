# riff: la musica scritta in pattern

**riff** è il linguaggio di bm per scrivere musica in poche righe di Lua, sullo stile di
[TidalCycles](https://tidalcycles.org) e [Strudel](https://strudel.cc): ritmi e melodie in
una stringa (la *mini-notazione*), cambiati da funzioni che si concatenano.

```lua
local R = require "riff"

R.code [[
setcpm(30)
drums = s "kick*4, ~ snare, hat*8" :gain(.9)
bass  = note "<c2 a1 f1 g1>" :s "acid" :lpf(sine:range(300, 1800):slow(4))
keys  = chord "<Cm7 Ab Eb Bb>" :s "epiano" :arp("updown") :fast(2) :room(.4)
]]

function _update()
  R.update()                    -- le note dei prossimi istanti in coda
end
```

È una libreria del kernel (`src/script/riff.lua`, come bmlib): gira nei `.bm`, sul Pi e
sulla RGB30, e in bmhost sul PC. Usa il sintetizzatore e gli strumenti di bm (`play()`,
`instruments()`) e i suoni del **banco** della cartuccia per nome; la scrivi e la ascolti
dal vivo in **bm Code** (Ctrl+Invio), e in **bm Sound** (F7, *Riff...*) diventa un brano del
banco.

## Come funziona

- Il tempo si conta in **cicli**: un ciclo è una battuta di quattro tempi, 2 secondi a
  120 BPM (`setcps(0.5)`, il valore iniziale). `setcpm(30)` = 30 cicli al minuto;
  `bpm(120)` = 120 battiti, quattro per ciclo.
- Un **pattern** è una funzione da un intervallo di cicli agli eventi che ci cadono: ogni
  evento ha il suo intero (inizio e fine della nota), la parte nell'intervallo e un
  valore (`{s = "kick", note = 36, gain = .9, ...}`).
- `R.update()`, chiamata a ogni fotogramma, chiede ai pattern gli eventi di un pezzo di
  ciclo un po' più avanti dell'orologio del suono (circa 0,2 s) e mette in coda le note
  che cominciano con **`play_at()`**: l'interrupt del suono le fa partire in tempo, entro
  un blocco di 1,3 ms, qualunque sia il frame rate del gioco. Un fotogramma lento non fa
  perdere il tempo; dopo una pausa (il gioco sospeso) le note già passate si saltano.
- Costo: ~700 istruzioni Lua a fotogramma in media con quattro pattern, ~9000 nel
  fotogramma che prepara un pezzo (ogni ¼ di secondo): meno di un millisecondo sul Pi.
- Le note aspettano in una **coda** (fino a 160) e la voce si sceglie quando la nota
  parte: una libera, la coda di un suono, o la nota più vecchia di riff.
  Non prende mai le voci di un brano del banco né quelle di un effetto sonoro;
  `R.voices(4, 5, 6, 7)` limita le voci che può usare.

## La mini-notazione

Una stringa è un ciclo diviso in parti uguali:

| Scritto | Cosa fa | Esempio |
|---|---|---|
| `a b c d` | una sequenza: quattro parti del ciclo | `"kick hat snare hat"` |
| `~` o `-` | una pausa | `"kick ~ kick ~"` |
| `[a b]` | un gruppo: due parti nello spazio di una | `"kick [hat hat]"` |
| `a, b` | insieme (anche dentro `[ ]`) | `"kick*4, ~ snare"`, `"[c,e,g]"` |
| `<a b c>` | una per ciclo, a turno | `"<c2 a1 f1 g1>"` |
| `a*3` | tre volte più veloce (anche `a*<2 4>`) | `"hat*8"` |
| `a/2` | due volte più lenta | `"pad/2"` |
| `a!3`, `a !` | ripetuta (tre volte, una in più) | `"kick!3 snare"` |
| `a@3`, `a _ _` | più lunga: pesa tre parti | `"c@3 e"`, `"c _ _ e"` |
| `a?`, `a?0.3` | suona a caso, il 50% (o il 70%) delle volte | `"hat*16?"` |
| `a(3,8)`, `a(3,8,2)` | ritmo euclideo: 3 colpi distribuiti su 8 passi (girato di 2) | `"kick(3,8)"` |
| `{a b c}%4` | polimetro: 4 passi a ciclo dalla sequenza di 3 | `"{kick hat snare}%4"` |
| `a \| b` | una delle due a caso, ogni ciclo | `"[snare \| clap]"` |
| `a . b c` | gruppi separati da un punto (`[a] [b c]`) | `"kick . hat hat hat"` |
| `kick:2` | una variante: due semitoni sopra (per la batteria) | `"kick:2 kick"` |

Le parole sono note (`c`, `eb4`, `f#2`: l'ottava 3 se manca, `c3` = 48, `c4` = 60 il do
centrale, come in Strudel), numeri (`0 2 -1 0.5`) o nomi di strumenti. Un errore dice dove:
`riff: missing ] at 6 in "a [b c"`.

## Costruire i pattern

| Funzione | Cosa fa |
|---|---|
| `s(p)`, `sound(p)` | gli strumenti: un nome del banco della cartuccia, un preset (`"kick"`, `"epiano"`, `instruments()`), un numero (il suono del banco) |
| `note(p)` | le note: nomi o numeri MIDI |
| `n(p)` | gradi di una scala (`:scale`); senza scala do maggiore dal C3 |
| `chord(p)` | accordi: `"<C Am F G7>"`, `"Fmaj7"`, `"Dm7"`, `"Bb"`, `"F#m"`, `"Csus4"`, `"Edim"`... in una posizione vicina al do centrale |
| `seq(a, b, ...)`, `cat(a, b, ...)`, `stack(a, b, ...)` | in sequenza nel ciclo, un ciclo ciascuno, insieme |
| `timecat({3, a}, {1, b})` | in sequenza con i pesi |
| `arrange({4, strofa}, {4, ritornello})` | sezioni di più cicli, una dopo l'altra |
| `run(n)` | `0 1 2 ... n-1` |
| `pure(v)`, `silence`, `mini(s)` | un valore per ciclo, niente, una stringa di mini-notazione |
| `piece(t)` | un pezzo di `ai.music` (o di `R.bake`) come pattern: un ciclo ogni 16 passi |

Un argomento che è una stringa è sempre mini-notazione, quindi si può fare un pattern di
quasi tutto: `:lpf("<400 2000>")`, `:fast("<1 2>")`, `:scale("<C:major A:minor>")`.

## Cambiarli

I metodi si concatenano con i due punti (in Lua `p:fast(2)`; con una stringa le parentesi
si possono omettere: `note "c e g" :s "pluck"`):

| Metodo | Cosa fa |
|---|---|
| `:fast(n)`, `:slow(n)` | più veloce, più lento |
| `:early(t)`, `:late(t)` | prima, dopo (in cicli) |
| `:rev()`, `:palindrome()` | al contrario, un ciclo sì e uno no |
| `:ply(n)` | ogni evento ripetuto n volte |
| `:iter(n)`, `:iterBack(n)` | ogni ciclo parte un n-esimo più avanti |
| `:every(n, f)`, `:lastOf(n, f)` | f sul primo (o sull'ultimo) di ogni n cicli |
| `:sometimes(f)`, `:often(f)`, `:rarely(f)`, `:almostNever(f)`, `:almostAlways(f)`, `:sometimesBy(x, f)`, `:someCycles(f)` | f su una parte degli eventi (o dei cicli) a caso |
| `:degrade()`, `:degradeBy(x)` | toglie a caso la metà (o una parte x) degli eventi |
| `:euclid(k, n, [rot])`, `:struct("x ~ x x")`, `:mask("1 0 1 1")` | la struttura di un ritmo, o i soli eventi dove la maschera è vera |
| `:off(t, f)`, `:superimpose(f)`, `:layer(f, g, ...)` | insieme a una copia cambiata (spostata di t) |
| `:jux(f)`, `:juxBy(w, f)` | il pattern a sinistra, f del pattern a destra |
| `:chunk(n, f)` | f su un n-esimo del ciclo, ogni volta un altro |
| `:linger(x)` | ripete la prima parte x del ciclo |
| `:inside(n, f)`, `:outside(n, f)` | f su un pezzo di 1/n (o n) cicli |
| `:swing(n)`, `:swingBy(x, n)` | i passi pari di ogni 1/n un po' in ritardo |
| `:segment(n)` | n eventi a ciclo da un segnale |
| `:range(a, b)`, `:rangex(a, b)` | da 0..1 ad a..b (esponenziale) |
| `:add(x)`, `:sub(x)`, `:mul(x)`, `:div(x)`, `+`, `-`, `*` | sui numeri, sulla nota (o su `n`) |
| `:transpose(x)`, `:scale("C:minor")` | semitoni; i gradi `n` su una scala |
| `:arp("up")` | le note di un accordo una dopo l'altra: `up`, `down`, `updown`, `downup`, `converge`, `diverge`, `thumbup` |
| `:fmap(f)` | una funzione sui valori |

Le funzioni da passare a `every`, `sometimes`, `off`... sono funzioni Lua
(`function(p) return p:fast(2) end`), ma ci sono anche le scorciatoie di Strudel:
`rev`, `fast(2)`, `slow(2)`, `ply(2)`, `late(0.25)`, `add(12)`, `degrade`:
`:every(4, fast(2))`, `:sometimes(add(12))`.

**Scale**: `"C:major"`, `"A:minor"`, `"D4:dorian"`, e `phrygian`, `lydian`, `mixolydian`,
`locrian`, `harmonic`, `melodic`, `pentatonic`, `minor_pentatonic`, `blues`, `chromatic`,
`wholetone` (la radice all'ottava 3 se manca).

**Segnali** (0..1 su ogni ciclo, continui: si usano come valori o con `:segment`): `sine`,
`cosine`, `saw`, `isaw`, `tri`, `square`, `rand`, `perlin`, `irand(n)`, `choose(a, b, ...)`.

## Il suono di ogni nota

| Controllo | Cosa fa |
|---|---|
| `:s(x)` | lo strumento (anche un pattern: `:s("<pluck epiano>")`) |
| `:note(x)`, `:n(x)` | nota, grado |
| `:gain(x)`, `:velocity(x)` | il volume (0..1) |
| `:legato(x)`, `:clip(x)` | quanto dura la nota rispetto al suo spazio (1 = tutto) |
| `:lpf(hz)`, `:hpf(hz)`, `:bpf(hz)`, `:cutoff(hz)`, `:res(x)` | il filtro (passa-basso, alto, banda) e la risonanza 0..1 |
| `:pan(x)` | −1 sinistra, 1 destra |
| `:room(x)`, `:delay(x)` | quanto va nell'ambiente e nell'eco (`reverb()`, `echo()` li regolano) |
| `:attack(ms)`, `:decay(ms)`, `:sustain(x)`, `:release(ms)` | l'inviluppo |
| `:shape(x)`, `:drive(x)`, `:noise(x)` | saturazione, rumore |
| `:fenv(ottave)`, `:fdecay(ms)`, `:lfo(hz)`, `:wah(ottave)`, `:pwm(x)`, `:duty(x)` | inviluppo del filtro, oscillazione lenta |
| `:wave(nome)` | la forma d'onda (`"saw"`, `"fm"`, `"pluck"`...) |
| `:vib(cent)`, `:vibhz(hz)`, `:detune(cent)`, `:pitch(semitoni)`, `:ptime(ms)` | vibrato e inviluppo dell'altezza |
| `:fm(x)`, `:fmh(x)` | profondità e rapporto della FM |
| `:raw(true)` | la voce 8-bit |
| `:tone{...}` | qualunque chiave di `tone()` |

Le unità sono quelle di `tone()` (millisecondi, pan −1..1, risonanza 0..1): non sono quelle
di Strudel, dove l'attacco è in secondi e il pan va da 0 a 1. Uno strumento senza nome è
`"triangle"`; la batteria senza nota suona alla sua (kick 36, snare 50, hat 72), il resto
al C3.

## Suonare

| Funzione | Cosa fa |
|---|---|
| `R.play(nome, p)`, `p:play(nome)` | suona p con quel nome (al posto di quello che c'era) |
| `R.stop(nome)`, `R.hush()` | ferma uno, tutti |
| `R.update()` | da chiamare in `_update`: le note dei prossimi istanti in coda |
| `R.setcps(x)`, `R.setcpm(x)`, `R.bpm(x, [tempi])` | la velocità (cambia senza saltare) |
| `R.playing()`, `R.get([nome])` | i nomi che suonano; il pattern di un nome (senza nome: tutti insieme) |
| `R.now()` | il ciclo adesso |
| `R.voices(v, ...)` | le voci che può usare (niente: tutte e 8) |
| `R.errors()` | gli errori dei pattern (un pattern con un errore si ferma) |
| `R.code(testo)` | il codice dal vivo (sotto) |

### Il codice dal vivo: `R.code`

`R.code(testo)` esegue un pezzo di Lua dove le funzioni di riff sono globali (`note`, `s`,
`stack`, `sine`, `setcpm`, `hush`...) e poi quelle del gioco. **Ogni globale a cui si dà un
pattern suona con quel nome** (`drums = s "kick*4"`); i nomi che cominciano con `_` no
(`_drums = ...` lo zittisce, come `_$:` in Strudel); i pattern di un `R.code` di prima che
non compaiono più si fermano; i `local` sono solo pezzi. Restituisce `true`, oppure `nil` e
l'errore: la musica di prima continua. Prima di suonare prova ogni pattern sul suo primo
ciclo, così un errore (una nota che non esiste, una scala sbagliata) arriva subito.

```lua
R.code [[
setcpm(28)
local verse  = chord "<Am F C G>"
pad   = verse :s "pad" :room(.6) :gain(.7)
arp   = verse :s "pluck" :arp("updown") :fast(4) :pan(sine:range(-.5, .5))
drums = s "kick ~ ~ kick, ~ snare, hat*8?" :every(4, fast(2))
]]
```

Il codice è testo Lua compilato dal kernel (mai bytecode): non può fare niente che il
codice della cartuccia non possa già fare.

## In bm Code

**Ctrl+Invio** suona il codice: in una scheda di soli pattern tutta la scheda, in un gioco
il blocco `riff.code [[ ... ]]` sotto il cursore (o il primo). Le parole delle note che
suonano **si accendono** (finché il testo è quello suonato); un errore segna la sua riga e
lo dice nella riga di stato, la musica continua; la riga di stato dice `MUSIC n`.
**Ctrl+.** ferma tutto. Le stesse voci nel menu (*Play the music*, *Stop the music*). Dalla
seriale: `ESC [ 28 ~` e `ESC [ 29 ~`.

## In bm Sound

**F7** (o *Riff...* nel menu) apre una riga dove scrivere un pattern (`s "kick*4, ~ snare"`,
o un'assegnazione `drums = ...`), con otto esempi (su/giù). **Invio** lo suona con i suoni
del banco aperto (per nome) e gli strumenti; **Ctrl+Invio** ne mette 1, 2, 4, 8 o 16
battute (Tab) nel banco come un **brano nuovo** con i suoi pattern, al tempo del riff (16
passi a ciclo). Gli strumenti che il banco non ha diventano suoni del banco. Una nota per
passo e per traccia (fino a 8 tracce): filtro, posto e ambiente di ogni nota restano quelli
dello strumento. Col pad: su/giù gli esempi, sinistra/destra le battute, A suona, X nel
banco, B chiude.

`R.bake(p, {cycles = 4, steps = 16, name = "RIFF"})` fa lo stesso dal codice: restituisce
il pezzo nella forma di `ai.music` (`bpm`, `instruments`, `patterns`). Al contrario
`R.piece(ai.music("ritmo rock"))` suona un pezzo dell'assistente come pattern, da cambiare
con tutto quello che c'è sopra.

## Differenze da Strudel

- Lua: i metodi con i due punti (`:fast(2)`), le funzioni anonime con `function(p) ... end`;
  niente `$:` (si danno nomi ai pattern).
- I suoni sono quelli del sintetizzatore di bm, nessun campione: `s "bd"` non esiste,
  `s "kick"` sì (`instruments("drum")`).
- Unità di `tone()`; `pan` −1..1; la nota predefinita è il C3.
- Il ciclo comincia da 0 quando il primo pattern parte e torna a 0 quando tutto si ferma.

## Prove

- `make test-riff`: la mini-notazione, le funzioni, i controlli, lo scheduler sull'orologio
  del suono, il codice dal vivo, `bake` e `piece` in bmhost (`tests/riff/cart.lua`; il
  suono in `build/riff/riff.wav`).
- `make test-audio`: la coda delle note a tempo (`player_at` in `src/audio/player.c`).
- QEMU: `test_riff_code` (bm Code), `test_sound_riff` (bm Sound), `test_audio` (`play_at`).
