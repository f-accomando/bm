# Video 4 — automazione dell'input

Registrazione riproducibile con **`bmhost --tool`** (orologio virtuale, 60 fps esatti). Fonti: skill `bm-video-tutorial`, `video/lib/bmvideo.py`,
`video/3d/04-map/record.py` e `verify.py`. File in `video/3d/04-map/`.

## 1. Stato

L'editor mappa 3D esiste. Tutti i tasti usati sono stati provati dal vivo con `bmhost` e screenshot (`video/3d/04-map/proto.py`): ogni tasto resta premuto
**8 frame** (`hold=8`) e poi si aspetta **20 frame** (`wait 20`, più lungo nei punti segnati in `record.py`). Le frecce con Shift (inclinazione 3D) non si possono
digitare via seriale e non sono usate.

## 2. Cosa serve

- Build dell'editor: `record.py` lo costruisce con `scripts/mkbm.py` in `out/bin/editor.bm` (da `carts/editor/main.lua`).
- Comando: `build/host/bmhost-bin out/bin/editor.bm --tool --sd video/3d/04-map/out/sd --input video/3d/04-map/out/input.txt --seconds 210 --video <fifo> --quiet [--shots DIR]`
  (raw rgb24 640×360, fifo; pagina finale 1920×1080 a 60 fps con `bmvideo.encode`).
- SD pulita a ogni registrazione (`out/sd` cancellata e rifatta da `record.py`). Il progetto di partenza è copiato come **`TROPIC.BME`** in `out/sd/carts/`
  (da `chain/03-animator.bme`, in alternativa `chain-delivered/03-animator.bme` o `chain-real/02-pixel.bme`). Contiene MESH (`PICO`, `APPLE`, `CRATE`), ANIM e lo sheet con le tessere del terreno.
- Si apre con `Ctrl+O` poi `Invio` (primo file dell'elenco: `TROPIC.BME`).

## 3. File dei tasti: `video/3d/04-map/out/input.txt`

Formato `FRAME type CODICI`, righe **in ordine di frame**, generato da `Script` di `bmvideo.py` (`record.py`, anche `--build-only`). Codici: frecce `\xf0 \xf1 \xf2 \xf3`;
`F3` = `\xfb`; Esc `\x1b`; Invio `\n`; Backspace `\x7f`; Spazio `\x20`; Tab `\x09`; `Ctrl+S` `\x13`; `Ctrl+B` `\x02`; `Ctrl+O` `\x0f`; `Ctrl+Z` `\x1a`.
`Shift+lettera` = la **lettera maiuscola** digitata (`T`, `W`, `P`, `S`, `G`, `A`, `C`, `O`; `Shift+S` e `Shift+G` = `S` e `G` maiuscole): così arriva all'editor, provato con `bmhost`.
Mappa tempi → frame (frame = secondi × 60): vedi la colonna "Tempo" di [`shot-list.md`](shot-list.md). Capitoli a 0:00, 0:08, 0:20, 0:38, 1:05, 1:28, 1:55, 2:10, 2:40, 2:55; totale 3:15.

## 4. File prodotto

`TROPIC.BME` salvato con `Ctrl+S` e `TROPIC.BM` costruito con `Ctrl+B` (in `out/sd/carts/`), con i layer `floor`, `height`, `walls`, `objects`; tocca solo
le sezioni MAP, LAYERS e FLAGS. Consumato dai video 6 e 7 (il video 6 lo fonde con `05-sound.bm`).

## 5. Verifica (`video/3d/04-map/verify.py`)

`python3 video/3d/04-map/verify.py [SD_DIR]` (predefinito `out/sd`), esce 0 solo se tutto passa:

1. mappa 256×256 e layer esattamente `floor`, `height`, `walls`, `objects`;
2. `floor`: corridoio di almeno 3 celle di larghezza e 10 righe, con una buca;
3. `height` e `walls`: almeno un gradino e un blocco;
4. `objects`: partenza `1024` (`Shift+S`) e traguardo `1030` (`Shift+G`), mele `1026`, una cassa `1028`, una lanterna `1032` (rimessa dopo `Ctrl+Z`);
5. MESH, ANIM e SHEET del video 3 mantenute; `TROPIC.BM` ha gli stessi layer e mappa; `bm_forge check` senza errori.
