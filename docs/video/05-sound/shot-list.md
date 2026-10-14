# Video 5 — shot list (bm Sound)

Una riga = un'azione. Tempi del **video finale** (hook + titolo = 0:12 di scarto: frame 0 della registrazione = 0:12), coerenti con
[`script.md`](script.md). Didascalie in inglese, max 70 caratteri (spec in `claude/skills/bm-video-series/overlay.md`). Nomi dei tasti dal manuale
`manuals/content/en/editor-sound.md` (tabella "Controls"); posizioni e passi dei parametri da `carts/sound/main.lua`.

## Convenzioni dei tasti

- `=` / `-`: +1 / -1 sul valore scelto. `+` / `_`: + / - il **passo del parametro** (tabella sotto). Nota: il manuale dice "-10 / +10", ma nel codice
  (`key` di `P[1]`, riga ~969) il passo dipende dal parametro (BEND FROM 12, ATTACK 10, CUTOFF 12, VOLUME 16...). Valgono i passi del codice.
- `xN` = N pressioni a 2-3 frame l'una (time-lapse; il valore si vede salire). Piano: `z s x d c v g b h n j m` = C..B dell'ottava, `q 2 w 3 e r 5 t 6 y 7 u` = ottava sopra; `,` / `.` ottava -1 / +1 (si parte da 4, barra in alto `OCT`).
- `goto X` = frecce `↑ ↓ ← →` che `record.py` calcola dalla posizione corrente alla riga X con le regole di `move` (`carts/sound/main.lua:916`): `↓` oltre l'ultima riga passa al gruppo
  seguente (stessa colonna), `↑` oltre la prima al gruppo precedente, `←`/`→` cambiano colonna solo sulle righe dei valori, riga 0 = il nome. Il percorso e' deterministico.
- Testi digitati su 8 caselle fisse: si **sovrascrive**, quindi si scrive il nome con spazi (`APPLE   `).
- Tasti per bmhost: `F1..F5` = `\xf9..\xfd`, frecce `\xf0..\xf3`, `PgUp \xf6`, `PgDn \xf7`, `Enter \n`, `Space \x20`, `Ctrl+O \x0f`, `Ctrl+C \x03`, `Ctrl+V \x16`, `Ctrl+S \x13`.
- Volume: every new sound starts at VOLUME 200; `_` is -16. Final quiet values (peak <= -6 dBFS): APPLE 56 (x9), CRATE 72 (x8), JUMP 24 (x11), STEP 120 (x5), SPIN 104 (x6), HIT 104 (x6); music sounds 8 (x12).

### Griglia dei parametri (pagina F1, 3 gruppi)

| Gruppo | Colonna 1 | Colonna 2 | Colonna 3 |
|---|---|---|---|
| SOUND | WAVE, DUTY, VOLUME, DETUNE | ATTACK, DECAY, SUSTAIN, RELEASE | BEND FROM, BEND TIME, VIBRATO, VIB SPEED |
| FILTER | CUTOFF, RESONANCE, MODE, KEY TRACK | FILT ENV, ENV DECAY, DRIVE, NOISE | LFO SPEED, LFO CUT, LFO DUTY, 8-BIT |
| WAVE & SPACE | wave set 1, wave set 2, FM FADE, FM FEEDBACK | PAN, ROOM, ECHO | (nomi dei gruppi) |

Passi di `+` / `_`: WAVE 1; DUTY, VOLUME, SUSTAIN, RESONANCE, DRIVE, NOISE, FILT ENV, wave set 1/2, FM FEEDBACK, PAN, ROOM, ECHO 16; ATTACK, DECAY, RELEASE, ENV DECAY, FM FADE, BEND TIME 10; BEND FROM, CUTOFF 12.
"Wave set 1" si chiama FM RATIO con FM, BRIGHT con PLUCK. Intervallo di BEND FROM -48..48.

## Aprire e orientarsi (0:12-0:34)

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 1 | 0:12 | Dev > bm Sound | `Ctrl+O` | l'elenco delle cartucce della SD | Ctrl+O: open a cartridge |
| 2 | 0:15 | Dev > bm Sound | `Enter` | si apre TROPIC.BM; la barra dice "Tropic Trail assets has no sounds yet: make them, then Save puts them in it" | Enter: open TROPIC |
| 3 | 0:20 | bm Sound > F1 SOUNDS | `F1` | pagina SOUNDS, suono 00 | F1: SOUNDS, the instruments |
| 4 | 0:22 | bm Sound > F2 SFX | `F2` | pagina SFX, 32 passi | F2: SFX, the effects |
| 5 | 0:24 | bm Sound > F3 PATTERN | `F3` | otto tracce, 16 passi | F3: PATTERN, eight tracks |
| 6 | 0:26 | bm Sound > F4 SONG | `F4` | l'elenco delle canzoni e gli slot | F4: SONG, the order of patterns |
| 7 | 0:30 | bm Sound > F1 SOUNDS | `F1` | di nuovo SOUNDS | An effect = an instrument + notes |

## Sei effetti (0:34-1:52)

Schema di ogni effetto: F1 (scelta del suono con `PgDn`, nome, parametri per gruppo, `I` per ascoltare), F2 (`PgDn` all'effetto, `←`x8 per tornare al passo 1, note del pianoforte, intestazione:
nome e SPEED, `Space`). Il suono dei passi nuovi e' quello scelto in F1 (`cur.sound`). Gli effetti hanno 8 passi di partenza: non si cambia `STEPS`.

### APPLE (0:34)

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 8 | 0:34 | F1 SOUNDS | `F1` | suono 00 senza nome | Sound 00: APPLE |
| 9 | 0:35 | F1 SOUNDS | goto NAME, `Enter`, `APPLE   `, `Enter` | il nome compare nella lista a sinistra | Name: 8 characters |
| 10 | 0:36 | F1 SOUNDS > SOUND | goto WAVE, `=` x6; goto VOLUME, `_` x9 | WAVE passa da SQUARE a FM, VOLUME 56 (22%) | WAVE: FM, VOLUME 22% |
| 11 | 0:36 | F1 SOUNDS > SOUND | goto DECAY, `+` x4 | DECAY 314 ms | DECAY 314 ms |
| 12 | 0:37 | F1 SOUNDS > SOUND | goto SUSTAIN, `_` x16 | SUSTAIN 0% | SUSTAIN 0% |
| 13 | 0:37 | F1 SOUNDS > SOUND | goto RELEASE, `+` x2 | RELEASE 235 ms | RELEASE 235 ms |
| 14 | 0:38 | F1 SOUNDS > WAVE & SPACE | goto FM RATIO, `+` x2; goto FM FADE, `+` x2 | RATIO 2, FADE 157 ms | FM RATIO 2, FM FADE 157 ms |
| 15 | 0:39 | F1 SOUNDS > WAVE & SPACE | goto ROOM, `+` x4; goto ECHO, `+` x4 | ROOM 41%, ECHO 25% | ROOM 41%, ECHO 25% |
| 16 | 0:40 | F1 SOUNDS | `I` | si sente la campana | I: listen |
| 17 | 0:41 | F2 SFX | `F2` | effetto 00, cursore al passo 1 | F2: sound effect 00 |
| 18 | 0:41 | F2 SFX | `e`, `u` | due passi: E5 e B5 | E5 then B5 |
| 19 | 0:42 | F2 SFX > intestazione | `↑`, `Enter`, `APPLE   `, `Enter` | NAME dell'effetto | Name the effect |
| 20 | 0:43 | F2 SFX > intestazione | `→`, `+` x2, `↓` | SPEED 80 ms | SPEED 80 ms |
| 21 | 0:44 | F2 SFX | `Space` | l'effetto suona; il passo acceso | Space: play |

### CRATE (0:47)

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 22 | 0:47 | F1 SOUNDS | `F1`, `PgDn` | suono 01 | PgDn: next sound |
| 23 | 0:48 | F1 SOUNDS | goto NAME, `Enter`, `CRATE   `, `Enter` | nome | Name: CRATE |
| 24 | 0:49 | F1 SOUNDS > SOUND | goto WAVE, `=` x3; goto VOLUME, `_` x8 | NOISE, VOLUME 72 | WAVE: NOISE, VOLUME 72 |
| 25 | 0:50 | F1 SOUNDS > SOUND | goto DECAY, `+` x2; goto SUSTAIN, `_` x16 | DECAY 157 ms, SUSTAIN 0% | Short decay, no sustain |
| 26 | 0:51 | F1 SOUNDS > FILTER | goto CUTOFF, `+` x11; goto RESONANCE, `+` x6; goto MODE, `=` | 706 Hz, 38%, BAND PASS | Band-pass at 706 Hz |
| 27 | 0:52 | F1 SOUNDS > FILTER | goto FILT ENV, `_` x3; goto ENV DECAY, `+` x2; goto DRIVE, `+` x2 | -3.0 oct, 157 ms, 13% | The filter closes fast |
| 28 | 0:53 | F1 SOUNDS > WAVE & SPACE | goto ROOM, `+` x2 | ROOM 28% | ROOM 28% |
| 29 | 0:54 | F1 SOUNDS | `I` | si sente il colpo | I: listen |
| 30 | 0:55 | F2 SFX | `F2`, `PgDn` | effetto 01 | PgDn: next effect |
| 31 | 0:55 | F2 SFX | `←` x8, `z`, `c`, `v` | tre passi: C4 E4 F4 | Three hits |
| 32 | 0:57 | F2 SFX > intestazione | `↑`, `Enter`, `CRATE   `, `Enter`, `→`, `_` x2, `↓` | NAME, SPEED 40 ms | SPEED 40 ms |
| 33 | 0:59 | F2 SFX | `Space` | suona | Space: play |

### JUMP (1:00)

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 34 | 1:00 | F1 SOUNDS | `F1`, `PgDn` | suono 02 | Sound 02: JUMP |
| 35 | 1:01 | F1 SOUNDS | goto NAME, `Enter`, `JUMP    `, `Enter` | nome | Name: JUMP |
| 36 | 1:02 | F1 SOUNDS > SOUND | goto WAVE, `=` x4; goto VOLUME, `_` x11; goto RELEASE, `+` x1 | SINE, VOLUME 24, RELEASE 157 ms | WAVE: SINE, VOLUME 24 |
| 37 | 1:03 | F1 SOUNDS > SOUND | goto BEND FROM, `_` x1; goto BEND TIME, `+` x1 | -12 st, 100 ms | BEND FROM -12, TIME 100 ms |
| 38 | 1:04 | F1 SOUNDS > WAVE & SPACE | goto ROOM, `+` x1 | ROOM 22% | ROOM 22% |
| 39 | 1:05 | F1 SOUNDS | `I` | si sente la scivolata | I: listen to the glide |
| 40 | 1:07 | F2 SFX | `F2`, `PgDn` | effetto 02 | PgDn: next effect |
| 41 | 1:07 | F2 SFX | `←` x8, `b`, `q` | due passi: G4 e C5 | G4 then C5 |
| 42 | 1:09 | F2 SFX > intestazione | `↑`, `Enter`, `JUMP    `, `Enter`, `→`, `_` x1, `↓` | NAME, SPEED 50 ms | SPEED 50 ms |
| 43 | 1:11 | F2 SFX | `Space` | suona | Space: play |

### STEP (1:13)

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 44 | 1:13 | F1 SOUNDS | `F1`, `PgDn` | suono 03 | Sound 03: STEP |
| 45 | 1:14 | F1 SOUNDS | goto NAME, `Enter`, `STEP    `, `Enter` | nome | Name: STEP |
| 46 | 1:15 | F1 SOUNDS > SOUND | goto WAVE, `=` x12; goto VOLUME, `_` x5 | BROWN, VOLUME 120 | WAVE: BROWN, VOLUME 120 |
| 47 | 1:16 | F1 SOUNDS > SOUND | goto DECAY, `+` x1; goto SUSTAIN, `_` x16 | DECAY 78 ms, SUSTAIN 0% | Very short |
| 48 | 1:17 | F1 SOUNDS > FILTER | goto CUTOFF, `+` x8 | CUTOFF 265 Hz (LOW PASS) | Low-pass at 265 Hz |
| 49 | 1:18 | F1 SOUNDS | `I` | si sente il passo | I: listen |
| 50 | 1:19 | F2 SFX | `F2`, `PgDn` | effetto 03 | PgDn: next effect |
| 51 | 1:20 | F2 SFX | `←` x8, `z` | un passo: C4 | One step |
| 52 | 1:21 | F2 SFX > intestazione | `↑`, `Enter`, `STEP    `, `Enter`, `↓` | NAME (SPEED resta 60 ms) | Name: STEP |
| 53 | 1:23 | F2 SFX | `Space` | suona | Space: play |

### SPIN (1:26)

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 54 | 1:26 | F1 SOUNDS | `F1`, `PgDn` | suono 04 | Sound 04: SPIN |
| 55 | 1:27 | F1 SOUNDS | goto NAME, `Enter`, `SPIN    `, `Enter` | nome | Name: SPIN |
| 56 | 1:28 | F1 SOUNDS > SOUND | goto WAVE, `=` x11; goto VOLUME, `_` x6 | PINK, VOLUME 104 | WAVE: PINK, VOLUME 104 |
| 57 | 1:29 | F1 SOUNDS > SOUND | goto ATTACK, `+` x2; goto RELEASE, `+` x2 | ATTACK 165 ms, RELEASE 235 ms | A soft swell |
| 58 | 1:30 | F1 SOUNDS > FILTER | goto CUTOFF, `+` x12; goto RESONANCE, `+` x5; goto MODE, `=` | 978 Hz, 31%, BAND PASS | Band-pass at 978 Hz |
| 59 | 1:31 | F1 SOUNDS > FILTER | goto FILT ENV, `+` x4; goto ENV DECAY, `+` x5 | +4.0 oct, 392 ms | The filter sweeps |
| 60 | 1:32 | F1 SOUNDS > WAVE & SPACE | goto ROOM, `+` x2; goto ECHO, `+` x3 | ROOM 28%, ECHO 19% | ROOM 28%, ECHO 19% |
| 61 | 1:33 | F1 SOUNDS | `I` | si sente il fruscio | I: listen |
| 62 | 1:34 | F2 SFX | `F2`, `PgDn` | effetto 04 | PgDn: next effect |
| 63 | 1:35 | F2 SFX | `←` x8, `n` x4 | quattro passi uguali: A4 | Four pulses |
| 64 | 1:36 | F2 SFX > intestazione | `↑`, `Enter`, `SPIN    `, `Enter`, `→`, `+` x2, `↓` | NAME, SPEED 80 ms | SPEED 80 ms |
| 65 | 1:38 | F2 SFX | `Space` | suona | Space: play |

### HIT (1:39)

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 66 | 1:39 | F1 SOUNDS | `F1`, `PgDn` | suono 05 | Sound 05: HIT |
| 67 | 1:40 | F1 SOUNDS | goto NAME, `Enter`, `HIT     `, `Enter` | nome | Name: HIT |
| 68 | 1:41 | F1 SOUNDS > SOUND | goto WAVE, `=` x1; goto VOLUME, `_` x6; goto DECAY, `+` x3; goto SUSTAIN, `_` x16; goto RELEASE, `+` x1 | TRIANGLE, VOLUME 104, 235 ms, 0%, 157 ms | WAVE: TRIANGLE, VOLUME 104 |
| 69 | 1:42 | F1 SOUNDS > SOUND | goto BEND FROM, `+` x1; goto BEND TIME, `+` x2 | +12 st, 200 ms | It falls: BEND FROM +12 |
| 70 | 1:43 | F1 SOUNDS > FILTER | goto CUTOFF, `+` x11; goto NOISE, `+` x2 | 706 Hz, NOISE 13% | A little noise |
| 71 | 1:44 | F1 SOUNDS > WAVE & SPACE | goto ROOM, `+` x2 | ROOM 28% | ROOM 28% |
| 72 | 1:45 | F1 SOUNDS | `I` | si sente il colpo morbido | I: listen |
| 73 | 1:46 | F2 SFX | `F2`, `PgDn` | effetto 05 | PgDn: next effect |
| 74 | 1:47 | F2 SFX | `←` x8, `,`, `c`, `z` | ottava 3 (barra `OCT 3`), passi E3 e C3 | E3 then C3 |
| 75 | 1:49 | F2 SFX > intestazione | `↑`, `Enter`, `HIT     `, `Enter`, `→`, `+` x3, `↓` | NAME, SPEED 90 ms | SPEED 90 ms |
| 76 | 1:51 | F2 SFX | `Space` | suona | Space: play |

## Strumenti della canzone (1:52-2:08, time-lapse a 2 frame per tasto)

Ogni riga: `F1`/`PgDn` al suono, `goto NAME`, nome, poi i valori. Si legge a sovrimpressione il solo elenco dei valori. VOLUME 8 for all music sounds.

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 77 | 1:52 | F1 SOUNDS | `F1`, `PgDn`, goto NAME, `Enter`, `KICK    `, `Enter` | suono 06 | KICK: sine + fast bend down |
| 78 | 1:53 | F1 SOUNDS | WAVE `=` x4; VOLUME `_` x12; DECAY `+` x3; SUSTAIN `_` x16; BEND FROM `+` x2; BEND TIME `=` x4 | SINE, VOLUME 8, 235 ms, 0%, +24 st, 40 ms | BEND FROM +24, TIME 40 ms |
| 79 | 1:56 | F1 SOUNDS | `PgDn`, nome `SHAKER  `; WAVE `=` x3; VOLUME `_` x12; DECAY `=` x6; SUSTAIN `_` x16; CUTOFF `+` x16; MODE `=` x2; ROOM `_` x1 | suono 07: NOISE, VOLUME 8, HIGH PASS 3.6 kHz | SHAKER: high-passed noise |
| 80 | 1:59 | F1 SOUNDS | `PgDn`, nome `BASS    `; WAVE `=` x4; VOLUME `_` x12; ATTACK `=` x1; RELEASE `+` x2; CUTOFF `+` x12; DRIVE `+` x2; ROOM `_` x1 | suono 08: SINE, VOLUME 8, LOW PASS 978 Hz, DRIVE 13% | BASS: sine + a little drive |
| 81 | 2:02 | F1 SOUNDS | `PgDn`, nome `MARIMBA `; WAVE `=` x7; BRIGHT `+` x6; RELEASE `+` x2; PAN `_` x2; ROOM `+` x2; ECHO `+` x3 | suono 09: PLUCK, VOLUME 8, PAN LEFT 25% | MARIMBA: pluck, a bit left |
| 82 | 2:05 | F1 SOUNDS | `PgDn`, nome `PAD     `; WAVE `=` x8; VOLUME `_` x12; ATTACK `+` x4; RELEASE `+` x5; CUTOFF `+` x10; RESONANCE `+` x2; PAN `+` x2; ROOM `+` x6 | suono 10: SUPERSAW, VOLUME 8, LOW PASS 508 Hz, ROOM 53% | PAD: soft supersaw |

## Patterns (2:08-2:44)

Pagina PATTERN: cursore `step -1` = intestazione di traccia (a sinistra), `←` dal passo 1 ci arriva; `Enter` su una cella mette/toglie la nota con l'ultima nota usata
(`last_note`); un tasto del pianoforte mette la nota e **avanza** di un passo; sull'intestazione della traccia il tasto del pianoforte solo suona e fissa la nota.
La traccia prende il suono con `-`/`=` sull'intestazione (si legge `TRACK n (voice n-1)  new notes: NOME` in basso). Track n starts on sound n-1.

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 83 | 2:08 | F3 PATTERN | `F3` | pattern 00, 16 passi | One pattern = one bar |
| 84 | 2:09 | F3 PATTERN | `,` (OCT 3 -> 2) | barra `OCT 2` | , : piano octave down |
| 85 | 2:10 | F3 PATTERN > traccia 1 | goto intestazione T1, `=` x6 (to KICK), `z`, poi `→`+`Enter` ai passi 1, 7, 9 | tre celle sulla traccia 1 | Track 1: KICK on 1, 7, 9 |
| 86 | 2:12 | F3 PATTERN > traccia 2 | intestazione T2: `=` x5 (SHAKER), `z`; `Enter` ai passi 3, 7, 11, 15 | quattro celle | Track 2: SHAKER, off-beats |
| 87 | 2:14 | F3 PATTERN > traccia 3 | intestazione T3: `=` x4 (BASS); passi con il pianoforte: 1 `z`, 4 `z`, 7 `b`, 9 `n`, 12 `n`, 15 `b` | sei celle | Track 3: BASS, C C G A A G |
| 88 | 2:17 | F3 PATTERN > traccia 5 | `.` x2 (OCT 4); intestazione T5: `=` x2 (PAD); passo 1 `c` | una cella lunga | Track 5: PAD holds E4 |
| 89 | 2:19 | F3 PATTERN | `Space` | il pattern gira; passo acceso | Space: play the bar |
| 90 | 2:21 | F3 PATTERN | `Space` | stop | Space: stop |
| 91 | 2:22 | F3 PATTERN | `Ctrl+C` | copia del pattern 00 | Ctrl+C: copy the pattern |
| 92 | 2:23 | F3 PATTERN | `PgDn` | pattern 01 vuoto | PgDn: next pattern |
| 93 | 2:24 | F3 PATTERN | `Ctrl+V` | il pattern 01 e' uguale al 00 | Ctrl+V: paste over |
| 94 | 2:25 | F3 PATTERN > traccia 4 | intestazione T4: `=` x1 (MARIMBA); passi con il pianoforte: 1 `e`, 4 `t`, 6 `y`, 8 `t`, 9 `e`, 12 `w`, 14 `q`, 15 `w` | otto celle | Track 4: MARIMBA melody |
| 95 | 2:30 | F3 PATTERN | `Space`, 3 s, `Space` | suona e si ferma | Space: play, Space: stop |
| 96 | 2:34 | F3 PATTERN | `Ctrl+C`, `PgDn`, `Ctrl+V` | pattern 02 = copia del 01 | Copy, next, paste |
| 97 | 2:36 | F3 PATTERN > traccia 5 | passo 1 della T5: `b` (G4) | la nota del pad cambia | Pad moves to G4 |
| 98 | 2:38 | F3 PATTERN > traccia 6 | intestazione T6: `-` x5 (APPLE); passi 8 `u`, 16 `e` | due celle sulla traccia 6 | Track 6: APPLE sparkle |
| 99 | 2:41 | F3 PATTERN | `Space`, 2 s, `Space` | suona e si ferma | Space: listen |

## Canzone (2:44-3:22)

Pagina SONG: `Enter` su una cella `+` aggiunge uno slot (copia dell'ultimo, o 00); `i` (Y) inserisce una copia dello slot e **sposta il cursore** sulla copia; `=`/`-` cambiano il numero del pattern dello slot.
L'intestazione si raggiunge con `↑`: da uno slot con indice >= 16 serve `↑` due volte (la prima salta alla riga sopra, la seconda alla intestazione): dallo slot 24 si arriva al campo SWING.

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 100 | 2:44 | F4 SONG | `F4` | canzone 00, nessuno slot | F4: the song |
| 101 | 2:45 | F4 SONG | `i` | slot 1 = pattern 00 (already present) | i: copy the slot |
| 102 | 2:46 | F4 SONG | `i`, `i`, `=`, `i` x3 | slot 2-6: 00, 01, 01, 01, 01 | i: copy the slot |
| 103 | 2:47 | F4 SONG | `i`, `=`, `i` | slot 7-8: pattern 02 | = : next pattern number |
| 104 | 2:48 | F4 SONG | `i`, `-`, `i` x3 | slot 9-12: pattern 01 | - : previous pattern number |
| 105 | 2:49 | F4 SONG | `i`, `=`, `i` x3 | slot 13-16: pattern 02 | 24 slots = one minute |
| 106 | 2:50 | F4 SONG | `i`, `-`, `i` x3 | slot 17-20: pattern 01 | Build it by copying |
| 107 | 2:51 | F4 SONG | `i`, `-`, `i` x3 | slot 21-24: pattern 00 | It ends where it starts |
| 108 | 2:52 | F4 SONG > intestazione | `↑`, `↑`, `+` x2 | SWING 20% | SWING 20% |
| 109 | 2:53 | F4 SONG > intestazione | `←`, `_` x2 | TEMPO 100 BPM | TEMPO 100 BPM |
| 110 | 2:54 | F4 SONG > intestazione | `←`, `Enter`, `JUNGLE  `, `Enter` | nome JUNGLE | Name: JUNGLE |
| 111 | 2:55 | F4 SONG > intestazione | `→` x4, `=` x3; `→`, `+` x2 | ECHO 3/16, ROOM 50% (LOOP gia' FROM 1) | ECHO 3/16, ROOM 50% |
| 112 | 2:56 | F4 SONG | `Space` | la canzone suona dal primo slot; la barra bianca avanza | Space: play the song |
| 113 | 3:17 | F4 SONG | `Space` | stop | Space: stop |
| 114 | 3:17 | bm Sound > salvataggio | `Ctrl+S` | domanda "Save an editable copy: TROPIC.BME?" | Ctrl+S: save |
| 115 | 3:19 | bm Sound > salvataggio | `keys 0x28` then `keys none` (Enter held) | messaggio "saved ..." /carts/TROPIC.BME | Saved into an editable copy |

## Tasti nuovi in questo video

Rispetto alla serie (fonte: tabella "Controls" del manuale): `F1`-`F4` (pagine di bm Sound), `PgUp`/`PgDn` (suono, effetto, pattern, canzone successivo), `=` `-` `+` `_` (valore), `I`
(ascolto del passo/suono), `z s x d c v g b h n j m` e `q 2 w 3 e r 5 t 6 y 7 u` con `,` `.` (pianoforte), `Enter` (aggiungere una nota o confermare il nome), `Space` (play/stop), `Ctrl+C` e `Ctrl+V`
(copia/incolla del pattern), `i` sulla pagina SONG (inserisce una copia dello slot), `Ctrl+O`, `Ctrl+S`. Non usati: F5, F6 (assistente), F7 (Riff), Ctrl+E.

## [DA VERIFICARE] di questa shot list

- Closed: row 115 (Enter held), rows 85-98 (track sounds), row 2 (status bar), VOLUME values, stereo 48 kHz.
