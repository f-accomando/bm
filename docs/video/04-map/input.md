STATO: BLOCCATO finché AGY-01..03 non sono DONE in bm-core. **Non registrabile oggi**: questo file descrive cosa servirà.

# Video 4 — automazione dell'input (descrizione, non implementazione)

Registrazione futura riproducibile con **`bmhost --tool`** (orologio virtuale, 60 fps esatti). Fonti: skill `bm-video-tutorial`, `video/lib/bmvideo.py`,
`video/01-pixel/record.py` e `verify.py`. File in `video/3d/04-map/`.

## 1. Perché non si può registrare ora

- Mancano il costruttore `map3d` (AGY-01), la vista 3D `v` (AGY-02) e gli strumenti `Shift+P/A/C/O` (AGY-03): tutti `PENDING` in `SYNC.md`.
- Manca la rinomina dei layer (`floor`, `height`, `walls`, `objects`); manca la regola per i segnaposto con il pennello 16×16
  (vedi `script.md`, "Problemi trovati" 1 e 2, e `editor-requirements.md` §2).
- Prove "per sbloccare 6-8" con una mappa fatta a script sono permesse solo fuori dal video finale (PIANO.md §4).

## 2. Cosa servirà

- Build dell'editor: `make bmhost build/editor.bm` (regola `$(BUILD)/editor.bm` nel `Makefile`, da `carts/editor/main.lua`), con le modifiche di AGY-02/03.
- Comando: `build/host/bmhost-bin build/editor.bm --tool --sd video/3d/04-map/out/sd --input video/3d/04-map/input.txt --seconds 210 --video <fifo> --quiet [--shots DIR]`
  (`--tool` da aggiungere a `run_bmhost`; raw rgb24 640×360, fifo; pagina finale 1920×1080 a 60 fps con `bmvideo.encode`).
- Risoluzione: l'SDK ha il tasto `r` (schermo `640x360`, `480x270`, …) nella pagina progetto: per l'anteprima 3D del gioco serve il progetto a **480×270**
  (PIANO.md §2); la pagina video resta 640×360 per la console dell'editor `[DA VERIFICARE: la vista 3D dell'editor usa 480×270 o la risoluzione dell'editor]`.
- SD pulita a ogni registrazione (`out/sd` cancellata). Cartuccia di partenza **`video/3d/04-map/start.bm`** = copia di `video/3d/chain/03-animator.bm`
  (o del `.bme` corrispondente, vedi il video 3 `input.md` §4), messa in `out/sd/carts/`. Contiene MESH (con `PICO`, `APPLE`, `CRATE`), ANIM, sheet con le tessere
  del terreno, e una mappa vuota. Se `start.bm` è un `.bm`, il primo `Ctrl+S` chiede di creare la copia `.BME` `[DA VERIFICARE: tasto di risposta]`.

## 3. File dei tasti: `video/3d/04-map/input.txt`

Formato `FRAME type CODICI`, righe **in ordine di frame**, generato da `Script` di `bmvideo.py`. Codici: frecce `\xf0 \xf1 \xf2 \xf3`; `F3` = `\xfb`; Esc `\x1b`;
Invio `\n`; Backspace `\x7f`; Spazio `\x20`; Tab `\x09`; `Ctrl+S` `\x13`; `Ctrl+B` `\x02`; `Ctrl+Z` `\x1a`; `Shift+L` = `L` maiuscola;
`Shift+P/A/C/O` = `P`, `A`, `C`, `O` maiuscole `[DA VERIFICARE: che le maiuscole arrivino così all'editor; il tasto `L` lo fa già nella pagina Map, riga 2722 di carts/editor/main.lua]`.
I tasti marcati `PROVVISORIO` in `shot-list.md` sono quelli da riscrivere quando AGY-02/03 avranno scelto i tasti definitivi.
Mappa tempi → frame (frame = secondi × 60): vedi la colonna "Tempo" di `shot-list.md`.

## 4. File prodotto

`video/3d/chain/04-map.bm`: il progetto dell'editor costruito con `Ctrl+B`, con i layer `floor`, `height`, `walls`, `objects` (PIANO.md §4); tocca solo
le sezioni MAP, LAYERS e FLAGS (e il `.bme`). Consumato dai video 6 e 7 (il video 6 lo fonde con `05-sound.bm`).

## 5. Verifica (`video/3d/04-map/verify.py`, da scrivere)

1. `scripts/bmres.py`: `f = bmres.read(path)`; `bmres.check(f)` senza errori; `bmres.layers_get(f)` elenca esattamente `floor`, `height`, `walls`, `objects`.
2. Conteggi sul layer `objects` (ids del contratto `SYNC.md` §2): 1 × `1024` PLAYER_START, 1 × `1030` GOAL, ~60 × `1026`, 14 × `1028`, ≥ 1 × `1032`
   (con la regola dei segnaposto scelta per il pennello 16×16); `floor`: area percorribile 9 × 96 tessere meno 2 buche; `height`: un gradino da 0,5
   (id 2); `walls`: un muro basso. Le sezioni MESH e ANIM identiche byte per byte a `chain/03-animator.bm`.
3. `python3 .claude/skills/bm-forge/scripts/bm_forge.py check chain/04-map.bm` senza errori (modelli invariati).
4. Test del costruttore: `make test-map3d` → riga `map3d OK` (AGY-01); la mappa del video deve passare `map3d_height`, `map3d_blocked`, `map3d_objects` con i valori attesi.
5. Durata del video finale con `ffprobe` ≈ 3:30; nessun frame oltre 16,7 ms con la mappa vera (misura da definire, PIANO.md §6 punto 5).

## 6. Da verificare quando l'editor esisterà

Tutti i `PROVVISORIO` e i `[DA VERIFICARE]` di [`shot-list.md`](shot-list.md); i punti 1-6 di "Problemi trovati" in [`script.md`](script.md).
