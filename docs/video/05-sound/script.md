# Video 5 — bm Sound: sei effetti morbidi e la canzone JUNGLE

Serie **Tropic Trail** ([`../PIANO.md`](../PIANO.md)). Editor: bm Sound. Durata: **~3:30 (210 s)**. Ritmo calmo (~130 parole/minuto
quando si parla; ci sono pause per ascoltare). Narrazione e didascalie in inglese, prosa di servizio in italiano.
Fonti dei valori: `manuals/content/en/editor-sound.md` (tasti, pagine, limiti) e `carts/sound/main.lua` (intervalli e unita' dei
parametri, `SGROUPS` righe 527-558; passi `+`/`_` per parametro). Altri file del video: [`shot-list.md`](shot-list.md),
[`youtube.md`](youtube.md), [`input.md`](input.md), [`cover.json`](cover.json).

## Struttura del video finale

| Parte | Tempo | Note |
|---|---|---|
| Gancio | 0:00-0:08 | 8 s della canzone JUNGLE che suona sulla pagina F4 (si ritaglia dalla registrazione, come `hook_cmd` del 2D) |
| Titolo | 0:08-0:12 | scheda "Tropic Trail - Episode 5 - bm Sound", senza voce |
| Registrazione principale | 0:12-3:22 | frame 0 della registrazione = 0:12 del video (190 s) |
| Chiusura | 3:22-3:30 | scheda "Next: bm SDK", voce fuori campo |

Riceve: una cartuccia della catena (`video/3d/chain/03-animator.bm`; il suono non dipende dalla mappa, quindi basta una qualunque).
Produce: `video/3d/chain/05-sound.bm` (sezione AUDIO: 6 effetti + canzone `JUNGLE`). Vedi [`input.md`](input.md).

## Gancio (5-8 s)

0:00-0:08. Sullo schermo la pagina F4 con la canzone JUNGLE che suona (barra bianca sullo slot in riproduzione), le celle del
pattern accese. Voce: *"Every sound of Tropic Trail, and the jungle song, made inside the console."*

## Script

| Tempo | Sullo schermo | Narrazione (English) |
|---|---|---|
| 0:00 | Gancio: F4, JUNGLE suona, la barra bianca scorre sugli slot | Every sound of Tropic Trail, and the jungle song, made inside the console. |
| 0:08 | Scheda titolo | (nessuna voce) |
| 0:12 | Dev > bm Sound, `Ctrl+O`, `Enter`: si apre la cartuccia della catena | bm Sound edits the sound bank of a cartridge. Control O opens the one we saved before. |
| 0:20 | `F1`, `F2`, `F3`, `F4`: le quattro pagine, una ogni due secondi | Four pages: F1 sounds, F2 effects, F3 patterns, F4 songs. |
| 0:30 | `F1`: pagina SOUNDS | An effect is an instrument plus a few notes. Instruments first. |
| 0:34 | APPLE: onda FM, decay breve, room ed echo; poi F2 con due note | APPLE: a soft bell. Wave FM, a short decay, no sustain, a little room and echo. Two notes, a fifth apart. |
| 0:47 | CRATE: rumore con filtro passa-banda che si chiude; tre colpi | CRATE: filtered noise. A band-pass filter that closes fast makes the wooden crack. Three quick hits. |
| 1:00 | JUMP: sine con BEND FROM -12; due note | JUMP: a pure sine that glides up. Bend from minus twelve semitones, over a tenth of a second. |
| 1:13 | STEP: rumore marrone con passa-basso; una nota | STEP: brown noise through a low-pass. Very short, very quiet. You will hear it a lot, so keep it soft. |
| 1:26 | SPIN: rumore rosa con passa-banda che spazza; quattro impulsi | SPIN: pink noise through a sweeping band-pass, four soft pulses: a whoosh. |
| 1:39 | HIT: triangolo che cade con un po' di rumore | HIT: a falling triangle with a little noise. Gentle, because Pico should never feel punished. |
| 1:52 | F1: cinque strumenti nuovi in time-lapse (KICK, SHAKER, BASS, MARIMBA, PAD) | Now the song. Five new instruments: kick, shaker, bass, marimba and a pad. The bell is the apple sound again. Each one is a wave, an envelope, a filter and a little room. |
| 2:08 | `F3`: pattern 00, tracce 1, 2, 3 e 5 si riempiono; `Space` lo fa girare | A pattern is one bar: sixteen steps, eight tracks. We use six, one per voice. Kick, shaker, bass and pad first. |
| 2:22 | `Ctrl+C`, `PgDn`, `Ctrl+V`: pattern 01; traccia 4 (marimba) si riempie | Copy it with Control C and Control V, then add the marimba melody. |
| 2:34 | Pattern 02: pad cambia nota, traccia 6 (campana) scintilla | A third bar changes the pad and adds the apple bell as a sparkle. |
| 2:44 | `F4`: 24 slot con `i`; intestazione: nome, tempo, swing, echo, room | On F4 the song is a list of patterns. I copies a slot. Name it JUNGLE, a hundred beats a minute, a little swing, echo and room. |
| 2:56 | `Space`: la canzone suona, la barra scorre | (musica sola, 9 s) |
| 3:05 | La canzone continua; in sovrimpressione "8 voices" | Honest limits: eight voices only. An effect borrows one and mutes the music on it, so the song uses six tracks. No WAV import here. |
| 3:17 | `Space` (stop), `Ctrl+S`, conferma della copia modificabile | Control S saves it into an editable copy. |
| 3:22 | Scheda di chiusura | Next: bm SDK puts models, map and sound together, and lights the jungle. |

## Ricette dei suoni

Valori **grezzi** dell'editor (0-255 salvo dove indicato) e come li mostra la schermata. Conversioni dal codice (`carts/sound/main.lua`):
ATTACK/DECAY/RELEASE `v * 2000 / 255` ms (circa 7.8 ms per unita'); SUSTAIN, DUTY, VOLUME, RESONANCE, ROOM, ECHO, DRIVE, NOISE `v * 100 / 255` %;
BEND TIME `v * 10` ms; BEND FROM in semitoni; FILT ENV `v / 16` ottave; CUTOFF `20 * 2^((v-1) * 9.9658 / 254)` Hz; PAN `-127..127`.
Partenza di ogni suono nuovo (`B.sound()`): SQUARE, DUTY 128, VOLUME 200, ATTACK 1, DECAY 0, SUSTAIN 255, RELEASE 10, ROOM 40, il resto 0.
I tasti per arrivarci sono in [`shot-list.md`](shot-list.md) (`=`/`-` +-1; `+`/`_` +- il passo del parametro).

### Sei effetti (banco: suono N = effetto N)

| # | Nome | Onda | VOLUME | ATTACK / DECAY / SUSTAIN / RELEASE | BEND FROM / TIME | Filtro | Wave set e spazio | Effetto (F2) |
|---|---|---|---|---|---|---|---|---|
| 0 | `APPLE` | FM | 200 (78%) | 1 (8 ms) / 40 (314 ms) / 0 (0%) / 30 (235 ms) | - | nessuno | FM RATIO 32 (2.0), FM DEPTH AUTO, FM FADE 20 (157 ms), ROOM 104 (41%), ECHO 64 (25%) | E5, B5; SPEED 80 ms |
| 1 | `CRATE` | NOISE | 168 (66%) | 1 / 20 (157 ms) / 0 / 10 (78 ms) | - | BAND PASS, CUTOFF 132 (706 Hz), RESONANCE 96 (38%), FILT ENV -48 (-3.0 oct), ENV DECAY 20 (157 ms), DRIVE 32 (13%) | ROOM 72 (28%) | C4, E4, F4; SPEED 40 ms |
| 2 | `JUMP` | SINE | 200 (78%) | 1 / 0 / 255 (100%) / 20 (157 ms) | -12 st / 10 (100 ms) | nessuno | ROOM 56 (22%) | G4, C5; SPEED 50 ms |
| 3 | `STEP` | BROWN | 136 (53%) | 1 / 10 (78 ms) / 0 / 10 | - | LOW PASS, CUTOFF 96 (265 Hz) | ROOM 40 (16%, di partenza) | C4; SPEED 60 ms |
| 4 | `SPIN` | PINK | 168 (66%) | 21 (165 ms) / 0 / 255 / 30 (235 ms) | - | BAND PASS, CUTOFF 144 (978 Hz), RESONANCE 80 (31%), FILT ENV +64 (+4.0 oct), ENV DECAY 50 (392 ms) | ROOM 72 (28%), ECHO 48 (19%) | A4 x4 (retrigger); SPEED 80 ms |
| 5 | `HIT` | TRIANGLE | 200 (78%) | 1 / 30 (235 ms) / 0 / 20 (157 ms) | +12 st / 20 (200 ms) | LOW PASS, CUTOFF 132 (706 Hz), NOISE 32 (13%) | ROOM 72 (28%) | E3, C3; SPEED 90 ms |

Le note degli effetti sono le note del pianoforte della tastiera, ottava 4 all'avvio (`z s x d c v g b h n j m` = C4..B4, `q 2 w 3 e r 5 t 6 y 7 u` = C5..B5;
`,` e `.` cambiano ottava). Gli effetti sono lunghi 8 passi (valore di partenza, `STEPS`) e i passi vuoti in coda lasciano ringhiare la coda di room/echo.
Perche' "morbidi": nessuna SQUARE (il suono tipico a 8 bit), attacchi di 1-21 unita', sustain a zero sui colpi, filtri passa-basso e room su tutti.
Il modo `sound=8bit` va lasciato spento (toglie room, echo e le onde senza alias).

### Cinque strumenti per la canzone (banco: suoni 6-10)

| # | Nome | Onda | VOLUME | A / D / S / R | Bend | Filtro | Spazio |
|---|---|---|---|---|---|---|---|
| 6 | `KICK` | SINE | 232 (91%) | 1 / 30 (235 ms) / 0 / 10 | +24 st / 4 (40 ms) | - | ROOM 40 |
| 7 | `SHAKER` | NOISE | 104 (41%) | 1 / 6 (47 ms) / 0 / 10 | - | HIGH PASS, CUTOFF 192 (3.6 kHz) | ROOM 24 (9%) |
| 8 | `BASS` | SINE | 216 (85%) | 2 (16 ms) / 0 / 255 / 30 (235 ms) | - | LOW PASS, CUTOFF 144 (978 Hz), DRIVE 32 (13%) | ROOM 24 (9%) |
| 9 | `MARIMBA` | PLUCK | 200 | 1 / 0 / 255 / 30 (235 ms) | - | - | BRIGHT 96 (38%), PAN -32 (sinistra 25%), ROOM 72 (28%), ECHO 48 (19%) |
| 10 | `PAD` | SUPERSAW | 120 (47%) | 41 (321 ms) / 0 / 255 / 60 (470 ms) | - | LOW PASS, CUTOFF 120 (508 Hz), RESONANCE 32 (13%) | PAN +32 (destra 25%), ROOM 136 (53%) |

La traccia 6 della canzone usa il suono 0 (`APPLE`): una campana FM che scintilla.

## Canzone JUNGLE (tracce 1-6; voci 6 e 7 libere)

Traccia `t` suona sulla voce `t - 1`: tracce 1-6 = voci 0-5, **tracce 7 e 8 restano vuote** cosi' gli effetti hanno sempre due voci libere
e non zittiscono la musica. Un passo e' una sedicesima; tempo **100 BPM** (un pattern di 16 passi = 2.4 s), swing **20%**, ECHO **3/16**, ROOM **50%**
(valore 128), LOOP `FROM 1` (e' gia' il valore di partenza). Tonalita': do maggiore pentatonica (do re mi sol la).

| Traccia (voce) | Suono | Pattern 00 (base) | Pattern 01 (+ melodia) | Pattern 02 (+ campana) |
|---|---|---|---|---|
| 1 (0) | `KICK` | passi 1, 7, 9: C2 | come 00 | come 00 |
| 2 (1) | `SHAKER` | passi 3, 7, 11, 15 (qualunque nota) | come 00 | come 00 |
| 3 (2) | `BASS` | 1 C2, 4 C2, 7 G2, 9 A2, 12 A2, 15 G2 | come 00 | come 00 |
| 4 (3) | `MARIMBA` | vuota | 1 E5, 4 G5, 6 A5, 8 G5, 9 E5, 12 D5, 14 C5, 15 D5 | come 01 |
| 5 (4) | `PAD` | passo 1: E4 (tenuta tutta la battuta) | come 00 | passo 1: G4 |
| 6 (5) | `APPLE` | vuota | vuota | 8 B5, 16 E5 |
| 7, 8 | - | vuote | vuote | vuote |

Ordine dei 24 slot (60 s a 100 BPM): `0 0 1 1 1 1 2 2 1 1 1 1 2 2 2 2 1 1 1 1 0 0 0 0`. Il pattern 00 vuoto di melodia apre e chiude: la ripartenza da `FROM 1` ricade sullo stesso pattern, senza salto.
Si costruisce con `Enter` sul primo slot e `i` (copia lo slot e si sposta sulla copia), cambiando il numero con `=`/`-` (vedi shot list). Pattern 01 e 02 nascono da `Ctrl+C`/`Ctrl+V` del precedente.

## Limiti onesti

Confrontati con `PIANO.md` §7 e con il manuale (`editor-sound.md`, capitolo "Tips and limits" e "Play it in your game").

1. **8 voci in tutto.** Un effetto prende una voce libera e **zittisce la traccia di musica su quella voce** finche' suona (manuale: "An effect uses a free voice and the music's track on that voice is silent while it plays"). Per questo la canzone usa solo le tracce 1-6.
2. **Una nota alla volta per voce.** L'effetto CHORD e' un arpeggio velocissimo (60 note al secondo, `src/audio/player.h`), non un accordo vero: gli accordi si fanno con tracce diverse (e il pad tiene una nota sola).
3. **Dimensioni.** 32 suoni, 64 effetti da 32 passi, 64 pattern da 64 passi su 8 tracce, 8 canzoni da 64 slot, nomi di 8 caratteri (tabella "Limits" del manuale).
4. **Niente WAV dall'editor.** Il manuale riporta ancora `[TO FILL: how to bring a WAV file into the bank on the console]`: nella console si usano solo i campioni gia' nel banco e il kit (`bd sd hh oh cp rim tom cb`). I WAV si importano solo con `scripts/bmaudio.py`, fuori dalla console, quindi **non** in questi video.
5. **Modo `sound=8bit` spento.** Toglie room, echo e le onde senza alias; il video usa il modo normale.
6. **Non e' uno strumento vero.** E' un synth morbido: i valori di volume reali dipendono dall'uscita (HDMI). Il mix si controlla anche con `verify.py` (RMS a finestre), non solo a orecchio.
7. **`bmhost --wav` e' mono** (`docs/progress/audio.md`): il pan (MARIMBA a sinistra, PAD a destra) si sente solo sulla console o in QEMU, non nel `.wav` di verifica.

## Capitoli (identici a youtube.md)

```
0:00 Hear it first
0:12 Open the sound bank
0:30 Six soft sound effects
1:52 Instruments for the song
2:08 Patterns: one bar of jungle
2:44 The song JUNGLE
3:05 Honest limits
3:17 Save
```

## [DA VERIFICARE] di questo video

- Se l'ECHO e il ROOM di un suono si sentono anche negli **effetti** fuori da una canzone: il manuale dice che sono "l'invio" verso l'eco e il room della canzone. Se non si sentono, APPLE e SPIN perdono l'echo (la ricetta resta valida, la narrazione "room and echo" va sistemata).
- Direzione di `=`/`-` sull'intestazione di traccia (pagina F3) e suono di partenza di ogni traccia: `learn_tracks()` e `track_sound` di `carts/sound/main.lua`; nel video si guarda la scritta "new notes: NOME" in basso.
- Ottava di partenza del pianoforte = 4 (la serie 2D lo assumeva); la barra in alto mostra `OCT`.
- Conferma della domanda "Save an editable copy: NAME.BME?" con `Enter` e comportamento in `bmhost --tool` (vedi `input.md`).
