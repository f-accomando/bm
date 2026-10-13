# Tropic Trail · Video 2 (bm Pixel + pittura in Studio) — automazione dell'input

Solo descrizione: niente è implementato qui. Codice futuro in `video/3d/02-pixel/` (`record.py`, `art.py`, `input-pixel.txt`, `input-studio.txt`, `verify.py`), sul modello di `video/01-pixel/` e della skill `bm-video-tutorial` (sezione "Più editor nella stessa puntata").

## Runtime

- `bmhost --tool`, orologio virtuale 60 fps. Due registrazioni in sequenza sulla **stessa SD**: prima `build/pixel.bm` con `input-pixel.txt`, poi `build/studio.bm` con `input-studio.txt`. I raw rgb24 640x360 si accodano (come nel 2D: `cat`, o fifo) e l'audio è assente.
- Frame (60 fps): Pixel da 0:08 (480) a 2:22 (8520); Studio da 2:24 (8640) a 3:30 (12600). Nei file i frame ripartono da 0 a ogni registrazione (`split_input` per frame, come nella skill); i tempi sopra sono quelli del video montato.
- Comando tipo: `bmhost-bin build/pixel.bm --tool --sd video/3d/02-pixel/out/sd --input input-pixel.txt --seconds 134 --video <fifo> --quiet`, poi lo stesso con `build/studio.bm` e `input-studio.txt`, `--seconds 66`.
- La tab Dev (shot list righe 2) è una ripresa QEMU opzionale o una scheda grafica (decisione D1, PIANO.md §6).

## File dei tasti

Formato `FRAME type CODICI`, righe **in ordine di frame**. Codici come in `video/lib/bmvideo.py`: frecce `\xf0` su, `\xf1` giù, `\xf2` sinistra, `\xf3` destra; `\xf6` PgUp, `\xf7` PgDn; `\xf9`/`\xfa`/`\xfb` = F1/F2/F3; Esc `\x1b`; Tab `\x09`; Space `\x20`; Backspace `\x7f`; Enter `\n`; Ctrl+C `\x03`, Ctrl+V `\x16`, Ctrl+S `\x13`, Ctrl+O `\x0f`. Maiuscole come caratteri (`O` ovale pieno). Si generano da uno `Script` di `bmvideo.py` (una chiamata per riga della shot list). Le serie di frecce+Space (pixel per pixel) escono da `art.py`, che dichiara i 16x16 pixel di ogni tessera e produce i tasti per tracciarli; come nel 2D, un'indicazione di colore è `1`-`9`, `0` e `,` `.`. Il puntatore deve stare in (0,0) prima di `Ctrl+V`. I tasti ripetuti a 1 frame vanno bene (timelapse). Un `shot` consuma un frame.

## Stato di partenza

- SD pulita ad ogni registrazione (`record.py` cancella `out/sd`).
- Cartuccia di partenza (`start`): `out/sd/carts/TROPIC.BME`, copia del file prodotto dal video 1 (`video/3d/01-studio/out/sd/carts/TROPIC.BME`). Non si parte da `chain/01-studio.bm`: i `.bm` sono in sola lettura per gli editor e Pixel lo salverebbe in una copia `.BME` (manuale Pixel, "Projects and games"). `[DA VERIFICARE: nome e cartella esatti del file nella SD e se Ctrl+O lo elenca]`.

## File prodotto e dove va

1. Pixel salva la sheet nel progetto (Ctrl+S), Studio salva i modelli con le tessere e la sheet dipinta (Ctrl+S): `out/sd/carts/TROPIC.BME`.
2. Per la catena: `python .claude/skills/bm-forge/scripts/bm_forge.py export models out/sd/carts/TROPIC.BME -o video/3d/chain/02-pixel.bm --title tropic` (stesso passo e stessi `[DA VERIFICARE]` di 01-studio/input.md). Il file `video/3d/chain/02-pixel.bm` contiene la sheet con le 8 tessere e i tre modelli dipinti, ed è l'input del video 3.

## Verifica (`verify.py`, senza guardare immagini)

1. Sheet pixel per pixel: `scripts/bmres.py` (`load`, `sheet_get`, `sheet8_palette`) legge `TROPIC.BME`; per ognuna delle 8 tessere 16x16 confronta ogni pixel con `art.py` (come `video/01-pixel/verify.py` per gli sprite del 2D), con la palette di Studio. Controlla anche `art.check()`: le tessere sono tutte diverse tra loro, `face` è uguale a `skin` fuori dai pixel degli occhi e `apple_icon` ha lo sfondo trasparente.
2. Modelli: `mesh_parse` della sezione MESH conserva i nomi `PICO`, `APPLE`, `CRATE`, i conteggi di triangoli del video 1 (116, 80, 12) e almeno una faccia con tessera su ognuno (le facce a colore piatto restano solo sulle gambe, pancia e punta della coda di PICO). `[DA VERIFICARE: come mesh_parse espone tessera/colore per faccia]`.
3. Budget: `python .claude/skills/bm-forge/scripts/bm_forge.py check out/sd/carts/TROPIC.BME --no-sync` e `check ... --json`: `PICO <= 300`, `APPLE <= 80`, `CRATE <= 40` (dipingere non cambia i triangoli).
4. Occhi: dopo il video, i pixel bianchi/neri della tessera 5 sono dove dice `art.py` e la tessera 4 (skin) è invariata.
5. Durata e formato con `ffprobe`: 3:30 +/- 5 s, 1920x1080, 60 fps.
