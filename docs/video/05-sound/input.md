# Video 5 — automazione dell'input

Descrive, non implementa. Da realizzare in CLD-03: `video/3d/05-sound/{record.py,input.txt,verify.py,start.bm}`. Segue lo schema del 2D
(`video/03-sound/record.py`, `verify.py`) con `video/lib/bmvideo.py` (`Script`, `encode`, `hook_cmd`, `card_cmd`, `concat_cmd`).

## Cosa serve

| Voce | Valore |
|---|---|
| Runtime | `build/host/bmhost-bin` (`make bmhost`), orologio virtuale: frame n = n/60 s, 60 fps esatti |
| Cartuccia strumento | `build/sound.bm` (`make build/sound.bm`) |
| Opzioni | `--tool --sd video/3d/05-sound/out/sd --seconds 191 --input video/3d/05-sound/input.txt --video <raw/fifo> --wav video/3d/05-sound/out/sound.wav --quiet` |
| Schermo registrato | 640x360 (console degli editor) nella pagina 1920x1080 (`bmvideo`: console 2x, pannello laterale, barra dei tasti) |
| Cartuccia di partenza | `video/3d/05-sound/start.bm` = copia di `video/3d/chain/03-animator.bm` (qualunque file della catena va bene: bm Sound tocca solo la sezione AUDIO) |
| SD | **pulita a ogni registrazione**: `record.py` cancella `out/sd`, crea `out/sd/carts/` e copia `start.bm` come `TROPIC.BM` (l'editor ricorda sulla SD pagina e file aperti) |
| Audio | `--wav` scrive un file **STEREO** a 48 kHz (pan udibile); si unisce al video con `encode(raw, ass, out, audio=wav)` |
| Output | `video/3d/chain/05-sound.bm` (vedi sotto) |

## File dei tasti `input.txt`

Formato di `bmhost --input`: una riga per evento, `FRAME type CODICI`, righe in **ordine di frame** (una riga fuori posto blocca le altre). Frame 0 della
registrazione = 0:12 del video finale; `frame = (t_video - 12 s) x 60`. Codici: `\xNN` per i codici di `keyp()`; frecce `\xf0 su, \xf1 giu, \xf2 sin, \xf3 des`; `\xf6` PgUp, `\xf7` PgDn;
`F1..F4 = \xf9 \xfa \xfb \xfc`; Invio `\n`; spazio **`\x20`** (uno spazio nudo si perde); Backspace `\x7f`; Ctrl+lettera = codice 1-26 (`\x0f` Ctrl+O, `\x03` Ctrl+C,
`\x16` Ctrl+V, `\x13` Ctrl+S). I caratteri `= - + _ , . i I z s x ...` si scrivono come sono. Esempio dei primi eventi:

```
0 type \x0f
180 type \n
480 type \xf9
600 type \xfa
```

(`Ctrl+O` al frame 0 = 0:12; `Enter` al frame 180 = 0:15; `F1` al frame 480 = 0:20; `F2` al frame 600 = 0:22). Tempi tra i tasti: 2-3 frame nei time-lapse (`Script.fast = 3`),
`hold=6..8` per i tasti del pianoforte, 50 frame dopo `I` e `Space` per lasciare finire il suono. Il testo dei nomi si digita con `type_text(..., per_key=6)`, spazi come `\x20`.

### Frame di ancoraggio (da `shot-list.md`)

| Blocco | Video | Frame | | Blocco | Video | Frame |
|---|---|---|---|---|---|---|
| Apri (`Ctrl+O`) | 0:12 | 0 | | Strumenti canzone | 1:52 | 6000 |
| Pagine F1-F4 | 0:20 | 480 | | Pattern 00 | 2:08 | 6960 |
| APPLE | 0:34 | 1320 | | Pattern 01 | 2:22 | 7800 |
| CRATE | 0:47 | 2100 | | Pattern 02 | 2:34 | 8520 |
| JUMP | 1:00 | 2880 | | Canzone (`i` slot) | 2:44 | 9120 |
| STEP | 1:13 | 3660 | | `Space` play JUNGLE | 2:56 | 9840 |
| SPIN | 1:26 | 4440 | | `Space` stop, `Ctrl+S` | 3:17 | 11100 |
| HIT | 1:39 | 5220 | | fine registrazione | 3:22 | 11400 |

`record.py` costruisce l'input con `Script`, non a mano: l'orologio avanza con `s.sec()`/`wait()` e **si calcola** da solo, poi un controllo confronta i frame con la tabella (tolleranza 60 frame per blocco; se un blocco
sfora, si accorciano le pause, non la narrazione). Dopo `Space` di play, `s.sec(...)` fino al frame 11100 per far suonare la canzone.

### Aiuti che `record.py` deve avere

- `goto(group, col, row)`: da posizione corrente a bersaglio, con le regole di `move` di `carts/sound/main.lua` (vedi `shot-list.md`); tiene lo stato (gruppo, colonna, riga). La posizione parte da gruppo 1, colonna 1, riga 1.
- `piano(note, octave)` come `Piano` del 2D (`video/03-sound/record.py`): tiene l'ottava corrente (4 all'avvio), emette `,`/`.` quando serve.
- `set(param, delta_key, n)`: `n` pressioni di `+`/`_`/`=`/`-` e aggiornamento dello stato atteso, usato anche da `verify.py` come "risposta corretta".
- Il suono delle tracce (F3): `=` +1, track n starts on sound n-1. Verificato.

## Gancio, titolo e chiusura

- Gancio: 8 s ritagliati dal file principale a 2:56 + 2 s (dentro la canzone), con la scritta "TODAY: SOUND FOR TROPIC TRAIL" (`hook_cmd`).
- Scheda titolo (4 s) e chiusura (8 s): `card_cmd` con testi "Tropic Trail - Episode 5 - bm Sound" e "Next: bm SDK - light, fog and shadow", `silent=True`; la voce della chiusura va sopra.
- Concatenazione: `hook.mp4`, `intro.mp4`, `main.mp4`, `outro.mp4` -> `ep05.mp4` con `concat_cmd(..., audio=True)`.

## Il file prodotto: `video/3d/chain/05-sound.bm`

1. Alla fine di `Ctrl+S` la SD contiene la cartuccia con la sezione AUDIO scritta da bm Sound. Una cartuccia `.bm` e' in sola lettura (regola M47): il primo salvataggio chiede
   "Save an editable copy: NAME.BME?" (modal del kernel, Enter held) e da li' in poi si lavora sulla copia (`write_to` di `carts/sound/main.lua`). Quindi la SD finale ha `carts/TROPIC.BM` (invariato) e `carts/TROPIC.BME` (con l'AUDIO nuovo).
2. `record.py` ricava `chain/05-sound.bm` dal salvataggio. Soluzione di ripiego, deterministica e senza editor: `scripts/mkbm.py --audio <banco.json>` sul `start.bm`, dove il JSON viene da `scripts/bmaudio.py unpack` del `.BME`.
3. Contratto con il video 6: `05-sound.bm` ha le stesse sezioni MESH, ANIM, SHEET di `03-animator.bm` (byte per byte) e in piu' (o al posto) la sezione AUDIO.

## `verify.py` (nessuna immagine)

Lancia `python video/3d/05-sound/verify.py [OUT_DIR]`; exit 0 se tutto e' a posto. Legge il banco con `scripts/bmaudio.py` (`extract` + `unpack`, come `video/03-sound/verify.py`) e il `.wav` con `struct`.

**Banco salvato** (campi di `bmaudio.unpack`; ROOM = chiave `reverb`, ECHO = `echo`):

| Controllo | Atteso |
|---|---|
| nomi dei suoni 0-10 | `APPLE CRATE JUMP STEP SPIN HIT KICK SHAKER BASS MARIMBA PAD` |
| `APPLE` | onda `fm`; adsr = [1, 40, 0, 30]; `reverb` 104; `echo` 64; VOLUME 56 |
| `CRATE` | `noise`; `filter` `bp`; `cutoff` 132; `fenv` -48; VOLUME 72 |
| `JUMP` | `sine`; `pitch` -12; `pitch_time` > 0 (10); VOLUME 24 |
| `STEP` | `brown`; `cutoff` 96; adsr sustain 0; VOLUME 120 |
| `SPIN` | `pink`; `filter` `bp`; `fenv` +64; VOLUME 104 |
| `HIT` | `triangle`; `pitch` +12; `noise` 32; VOLUME 104 |
| effetti 0-5 | stessi nomi dei suoni; numero di note 2, 3, 2, 1, 4, 2; `ms` 80, 40, 50, 60, 80, 90 |
| canzone 0 | nome `JUNGLE`, `bpm` 100, `swing` 20, `echo` 3, `room` 128, `loop` 0, `order` di 24 slot = `0 0 1 1 1 1 2 2 1 1 1 1 2 2 2 2 1 1 1 1 0 0 0 0` |
| pattern 0, 1, 2 | 16 passi; tracce 0-5 con le note delle tabelle di `script.md`; **tracce 6 e 7 vuote in tutti i pattern** (voci libere per gli effetti) |
| altro | le sezioni MESH, ANIM, SHEET sono identiche a `03-animator.bm` (hash) |

**Audio** (`out/sound.wav`, 48 kHz STEREO, finestre di RMS come `video/03-sound/verify.py`):

- finestre di 0.5 s: silenzio (RMS < 100) nei primi 5 s (nulla ha ancora suonato); un picco > 300 in ciascuna finestra che contiene un `I`/`Space` di un effetto (sei istanti dai frame 1320-4500);
- RMS in finestre di 5 s durante la canzone (frame 9840-11100, ~21 s): tutte > 300 (non e' silenzio) e senza buchi > 1 s di RMS < 50 (nessuna interruzione);
- il picco assoluto resta sotto -6 dBFS (nessun clipping; compressore e limitatore della console). Music RMS < -18 dBFS.
- Pan verificabile (stereo).

Dopo: `ffprobe` per la durata della registrazione (~190 s) e del video finale (~210 s, 3:30).

## Cosa NON si fa

Niente WAV importati (non c'e' modo dalla console), niente QEMU per le riprese (solo eventuale prova), nessun suono fatto fuori dall'editor.
