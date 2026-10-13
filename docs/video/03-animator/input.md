# Video 3 — automazione dell'input (descrizione, non implementazione)

Registrazione riproducibile con **`bmhost --tool`** (orologio virtuale: il frame *n* è a *n*/60 s; stesso copione = stesso video).
Fonti: skill `claude/skills/bm-video-tutorial/SKILL.md` ("Cose imparate"), `video/lib/bmvideo.py` (`Script`, `run_bmhost`),
`video/01-pixel/record.py` e `verify.py` (modello da seguire). I file vanno in `video/3d/03-animator/` (cartella separata dal 2D).

## 1. Prerequisiti e comando

- Build: `make bmhost build/animator.bm` (regola `$(BUILD)/animator.bm` nel `Makefile`, da `carts/animator/main.lua`).
- Comando (come `run_bmhost`, **più `--tool`**, che `run_bmhost` oggi non passa: va aggiunto come opzione):

  `build/host/bmhost-bin build/animator.bm --tool --sd video/3d/03-animator/out/sd --input video/3d/03-animator/input.txt --seconds 210 --video <fifo-o-file raw> --quiet [--shots DIR] [--wav FILE]`

  `--tool` serve perché l'editor possa salvare su un file già presente; senza, l'SDK dice "a cartridge cannot change a .bm that is there already".
  `--video` scrive raw rgb24 **640×360** (~700 KB a frame): usare una fifo, non il disco. Pagina finale 1920×1080 a 60 fps con `bmvideo.encode`.
- Non c'è la scheda Dev in `bmhost`: le righe 2 e 3 della shot-list (Dev ▸ bm Animator) sono un cartello in sovrimpressione. Opzionale: una breve
  ripresa QEMU `-M raspi0` del solo menu (PIANO.md §6, punto 2).

## 2. Stato iniziale (SD pulita)

1. `out/sd` cancellata a ogni registrazione (`shutil.rmtree`): l'editor ricorda sulla SD pagina e file aperti.
2. Cartuccia di partenza **`video/3d/03-animator/start.bm`** = copia byte per byte di `video/3d/chain/02-pixel.bm`, messa in `out/sd/carts/`
   (nome 8.3 maiuscolo, es. `TROPIC.BM`; `[DA VERIFICARE: nome e se l'elenco file la mostra per prima]`).
   Contiene MESH con `PICO`, `APPLE`, `CRATE` dipinti e **nessuno** scheletro (pagina F2: `NO SKELETON`).
3. Prima di registrare: `python3 .claude/skills/bm-forge/scripts/bm_forge.py check start.bm --budget 300 --draw PICO=1` (PICO ≤ 300 triangoli, PIANO.md §2).

## 3. File dei tasti: `video/3d/03-animator/input.txt`

Formato `FRAME type CODICI`, **righe in ordine di frame** (una riga fuori posto blocca le altre). Si genera con `Script` di `bmvideo.py`
(`s.key`, `s.chord`, `s.type_text`, `s.wait`), non a mano. Esempi (frame = secondi × 60):

```
1800 type n               # 0:30  F2, n: nasce root (se il video parte da 0:00, frame relativi alla registrazione)
1830 type w               # w ×N, una riga ogni 3 frame (tasti ripetuti a 1-3 frame vanno bene)
2640 type \x0a            # Invio: rinomina (poi \x7f ×5, il nome, \x0a)
```

Codici (da `bmvideo.KEYS` e dalla skill): frecce `\xf0` su, `\xf1` giù, `\xf2` sinistra, `\xf3` destra; `\xf4` Home, `\xf5` End; `F1`…`F4` = `\xf9\xfa\xfb\xfc`;
Esc `\x1b`; Invio `\n`; Backspace `\x7f`; **Spazio `\x20`** (uno spazio nudo si perde); `Ctrl+Z` `\x1a`; `Ctrl+S` `\x13`; le maiuscole (`K`, `W`) si scrivono come
caratteri maiuscoli. I nomi (`spine`, `arm.L`, `idle`...) si scrivono con `type_text` (un carattere ogni 4 frame). Backspace nel campo del nome: il nome
proposto è già scritto e il testo si **accoda** (`bm3d.lua` `input_key`), quindi 5 Backspace prima di scrivere (come "Save as" di bm Pixel).
`[DA VERIFICARE: che 0x7f arrivi come "\b" all'editor; è così in bm Pixel, qui da provare]`.

Mappa tempi → frame di inizio (dal video finale; il gancio 0:00-0:08 è un taglio della ripresa finale, `bmvideo.hook_cmd`, quindi la registrazione
vera parte al 0:08 = frame 0): rig 0:30 → 22 s di registrazione; skin 1:20; clip 1:45; preview 2:55; save 3:10. Tarare con la prova.

Calibrazione dei `×N` e delle pose (rotazioni): copia di `carts/animator` con `log(...)` in `_update` (stato di pagina, `S.msg`), compilata con
`scripts/mkbm.py`, e `bmhost` senza `--quiet`; poi si fissano i numeri in `video/3d/03-animator/record.py` (come per `video/01-pixel`).

## 4. File prodotto

- Salvataggio: `Ctrl+S` su un `.bm` crea la copia modificabile **`/carts/NOME.BME`** (SD di registrazione: `out/sd/carts/`).
- Per la catena PIANO.md §4: **`video/3d/chain/03-animator.bm`** = quel progetto fuso in `.bm`. Come ottenerlo è una decisione aperta
  (`[DA VERIFICARE]`): (a) `Ctrl+B` di bm SDK sul `.bme` dopo la registrazione (fuori dal video); (b) usare il `.bme` come input del video 4 e
  tenere `.bm` solo per le prove. Finché non è deciso, `verify.py` accetta entrambi.
- Contenuto atteso: MESH invariata; ANIM con lo scheletro di `PICO` (9 ossa) e 5 clip.

## 5. Verifica (`video/3d/03-animator/verify.py`, da scrivere sul modello di `video/01-pixel/verify.py`)

Nessuna immagine: si legge il file che l'editor ha davvero scritto.

1. `scripts/bmres.py`: `f = bmres.read(path)`; `bmres.check(f)` senza errori; `anim_parse(f.get(SEC_ANIM))` dà i blocchi per modello
   (nome, `nb` ossa 1…64). Decodifica dei dati interni secondo `docs/BM-FORMAT.md` (sezione 9 ANIM).
2. Controlli (valori fissi di PIANO.md §2):
   - il modello `PICO` ha **9 ossa** con i nomi `root, spine, head, arm.L, arm.R, leg.L, leg.R, tail1, tail2` (nomi ≤ 16 lettere);
   - **5 clip** `idle, run, jump, spin, hit`; loop attivo solo in `idle` e `run`; durate in frame 24, 8, 12, 8, 6;
   - ogni clip ha ≥ 2 keyframe e almeno una posa diversa dal riposo (nessuna clip vuota); la sezione MESH è identica all'ingresso (byte per byte);
   - il modello ha facce legate a più di un osso (skin applicata), cioè non tutte a `root`.
3. `python3 .claude/skills/bm-forge/scripts/bm_forge.py check chain/03-animator.bm --budget 300 --draw PICO=1` senza errori (triangoli di Pico invariati).
4. Riferimento visivo (facoltativo): `bm_forge.py preview chain/03-animator.bm --model PICO --clip run -o run.gif` per confrontare con la ripresa.
5. Durata del video finale con `ffprobe` ≈ 3:30 (210 s ±5).

## 6. Da verificare prima di registrare

- Nome e posizione del file nell'elenco di apertura; tasto per rispondere a "Save an editable copy"; dove `m` mette l'osso copiato e quale osso resta scelto;
  asse e segno di `w a s d r f` (F2) e di `w s a d q e` (F3); valore iniziale di `i`; se `r` crea un keyframe (altrimenti aggiungere `k`);
  se l'angolo oltre 180° si accumula (`spin`); `0x7f` come Backspace; testo esatto di `m` con un solo osso.
- Ogni voce è marcata `[DA VERIFICARE]` in [`shot-list.md`](shot-list.md).
