# Tropic Trail · Video 1 (bm Studio) — automazione dell'input

Solo descrizione: niente è implementato qui. Costruire poi `video/3d/01-studio/` con `record.py`, `input.txt`, `verify.py`, sul modello di `video/01-pixel/` (skill `bm-video-tutorial`, libreria `video/lib/bmvideo.py`).

## Runtime

- `bmhost --tool` (orologio virtuale: il frame n è a n/60 s, stesso copione = stesso video). Binario `build/host/bmhost-bin`. `record.py` costruisce la propria cartuccia dell'editor in `out/studio.bm` con `scripts/mkbm.py` (il `build/studio.bm` del checkout è rotto).
- Comando (come in `video/01-pixel/record.py`): `bmhost-bin out/studio.bm --tool --sd video/3d/01-studio/out/sd --input video/3d/01-studio/input.txt --seconds 215 --video <fifo> --quiet`, con audio assente.
- Dev tab: bmhost non ha il menu del kernel. Il frammento 0:08-0:11 della shot list (tab Dev) è una ripresa QEMU a parte, opzionale, o una scheda grafica (decisione D1 di PIANO.md §6).

## File dei tasti

`video/3d/01-studio/input.txt`: una riga per evento, formato `FRAME type CODICI`, **in ordine di frame** (una riga fuori posto blocca le altre). Codici come nella skill: frecce `\xf0` su, `\xf1` giù, `\xf2` sinistra, `\xf3` destra; `\xf6` PgUp, `\xf7` PgDn, `\xf8` Canc; `\xf9`/`\xfa` = F1/F2; Esc `\x1b`; Tab `\x09`; Space `\x20` (uno spazio nudo si perde); Backspace `\x7f`; Enter `\n`; Ctrl+lettera = codice 1-26 (`\x13` Ctrl+S, `\x0e` Ctrl+N, `\x1a` Ctrl+Z, `\x19` Ctrl+Y). Ogni tasto dura ~3 frame, 40 frame fra i `q` della rotazione. I `shot NOME` consumano un frame.
Il file si genera da uno `Script` di `bmvideo.py` (`s.key`, `s.chord`, `s.type_text`, `s.sec`), una chiamata per riga della shot list, con le stesse didascalie.

Ancoraggi di frame (60 fps): 0:08 = 480, 0:32 = 1920, 1:05 = 3900, 1:35 = 5700, 2:00 = 7200, 2:15 = 8100, 2:30 = 9000, 3:04 = 11040, 3:16 = 11760, fine 3:30 = 12600.

## Stato di partenza

- SD pulita ad ogni registrazione: `record.py` cancella `out/sd` e la ricrea (l'editor ricorda sull'SD pagina e file aperti).
- Cartuccia di partenza: `start.bm` = nessuna; si avvia direttamente e si crea il progetto con `Ctrl+N` (riga 4 della shot list). Per il video 1 non serve nulla in `chain/`.

## File prodotto e dove va

1. Studio salva `out/sd/carts/TROPIC.BME` (Ctrl+S, riga 7 e riga 68).
2. Per la catena il file diventa `video/3d/chain/01-studio.bm` con `python .claude/skills/bm-forge/scripts/bm_forge.py export models out/sd/carts/TROPIC.BME -o video/3d/chain/01-studio.bm --title tropic`. Non si scrive `models.bm` altrove nel resto della serie.
3. Il video 2 parte da `TROPIC.BME` (stesso file, copiato nella SD pulita) e non dal `.bm`, perché i `.bm` sono in sola lettura e Pixel li salverebbe in una copia `.BME` (vedi 02-pixel/input.md).

## Verifica (`verify.py`, senza guardare immagini)

1. Esiste `out/sd/carts/TROPIC.BME` e `scripts/bmres.py list` lo apre senza errori (`bmres.load`/`check`).
2. `scripts/bmres.py`: `mesh_parse(f.get(MESH))` restituisce tre modelli con nomi esatti `CRATE`, `APPLE`, `PICO`, in questo ordine.
3. Triangoli: `python .claude/skills/bm-forge/scripts/bm_forge.py check out/sd/carts/TROPIC.BME --no-sync --draw PICO=1 --draw APPLE=60 --draw CRATE=14 --budget 100000 --json` (uso `--draw` solo per leggere i conteggi per modello); si controlla `PICO <= 300`, `APPLE <= 80`, `CRATE <= 40` dai numeri riportati, non dal budget di scena.
4. Totale di scena della cartuccia sotto 1200 (soglia di Studio).
5. Silhouette (opzionale): `bm_forge.py check ... --reference chain-ref/PICO.bm --min-iou 0.75` contro un riferimento fatto con bm-forge (solo come "risposta corretta", mai mostrato). Note: IoU informativo solo.
6. Durata del video con `ffprobe`: 3:30 +/- 5 s; risoluzione 1920x1080 a 60 fps.
