# Tropic Trail · Episode 7 — automazione (da descrivere, non implementata)

Modello: `video/05-assistant/record.py` (stessa struttura: più registrazioni sulla stessa SD, poi la partita) e la skill
`bm-video-tutorial` ("Cose imparate"). Qui solo il piano; `record.py`/`verify.py` si scrivono in CLD-03.

## Ingressi e uscite

| Voce | Valore |
|---|---|
| Cartella | `video/3d/07-code/` (`record.py`, `verify.py`, `input-code.txt`, `input-sdk.txt`, `game.txt`) |
| `start.bm` | `video/3d/chain/06-sdk.bm` |
| Uscita | `video/3d/chain/07-code.bm` (il `.bm` costruito dal progetto) + `ep07.mp4` |
| SD | pulita a ogni registrazione (`record.py` la cancella); `carts/TROPIC.BM` = copia di `start.bm` |
| Dipendenze | `make bmhost bmhost-ai`, `build/code.bm`, `build/editor.bm`, `ffmpeg`; **AGY-01 in `bm-core`** (`map3d_*`) |

L'assistente (F6) esiste solo in **`build/host/bmhost-ai`** (carica `build/assist.bin`; lanciarlo dalla radice del repo o con
`BMHOST_AI_WEIGHTS`): con `bmhost` normale bm Code non parte (`attempt to index a nil value (global 'ai')`). Quindi le due
registrazioni degli editor usano `bmhost-ai --tool`; la partita usa `bmhost-bin` (senza `--tool`).

## Le tre registrazioni (stessa SD)

1. **bm Code** — `build/host/bmhost-ai build/code.bm --tool --sd SD --seconds S --input input-code.txt --video raw-code.rgb --wav code.wav --quiet`
   (cwd = radice del repo). `input-code.txt`: righe `FRAME type CODICI`, in **ordine di frame**.
2. **bm SDK** — stesso comando con `build/editor.bm` e `input-sdk.txt` (pochi secondi: aprire il progetto, `Ctrl+B`).
3. **La partita** — `build/host/bmhost-bin SD/carts/TROPIC.BM --sd SD2 --seconds 20 --input game.txt --video raw-game.rgb --wav game.wav --quiet`
   (480×270, righe `FRAME pad 1 bottoni`). Raw e wav delle tre parti accodati (`join_wavs`) come nel 05.

F5 dentro `bmhost --tool` termina la corsa: per questo la partita è una registrazione a parte e non si preme F5 nel video.

## Codici dei tasti (righe `FRAME type CODICI`)

Da `video/lib/bmvideo.py` (`Script.key/raw/type_text/chord`): `Ctrl+O` `\x0f`, `Ctrl+K` `\x0b`, `Ctrl+S` `\x13`, `Ctrl+B` `\x02`,
`Enter` `\n`, `↓` `\xf1`, `Esc` `\x1b`, spazio `\x20` (mai nudo), **F6 = `\x1b[17~`** (F6–F10 solo come sequenze del terminale seriale).
Battitura del codice: 1 frame per carattere, `Enter` 2 frame (time-lapse); domanda all'assistente: 4 frame per carattere.
Scritto **senza indentazione** (bm Code rientra da solo); `end`, `else`, `elseif` da soli in riga tornano al rientro del blocco.
Dopo l'inserimento dell'assistente (`Enter`) il cursore resta in fondo al blocco: un `Enter` prima di scrivere la riga dopo.

Esempio (forma, non i frame veri):

```
660 type \x0f            # Ctrl+O
840 type \n              # Enter su TROPIC.BM        [DA VERIFICARE: riga]
1560 type \x0b           # Ctrl+K  (x N, ogni 3 frame) [DA VERIFICARE: N]
2160 type l              # ... il codice, un carattere per frame
3360 type \x1b[17~       # F6
3600 type t              # "third person camera"
4320 type \n              # Enter: inserisce la risposta
7980 type \x13           # Ctrl+S
```

## Taratura (prima di registrare)

- **Il codice deve girare**: appena `map3d_*` c'è, copia della cartuccia con `log(...)` in `_update` e `bmhost` senza `--quiet`
  (come il 05): controllare che `state` passi `title → play`, che Pico atterri (`pico.ground`), che una mela e una cassa spariscano.
- **Buche**: salto pieno = 0.22 di velocità, gravità 0.012, passo 0.07: ~2.57 unità in aria. Se una buca è più larga di 2 celle,
  alzare la velocità del salto o ridurre il passo (e riscrivere il copione).
- **Assistente**: provare `third person camera` sul build; se la prima risposta non è `guide.3d_camera`, scegliere la voce giusta
  con `↓` (nel 05 «Up and down choose among the answers») e aggiungere il tasto alla shot list.
- **`game.txt` (20 s)**: non si scrive a mano. Si fa girare `video/3d/08-gameplay/bot.py` sulla `07-code.bm` appena costruita e si
  tagliano i primi 1200 frame di `replay.txt` (titolo, corsa, qualche mela, una cassa con lo spin, la prima buca). Ordine: registrare
  1–2, costruire `07-code.bm`, far girare il bot, poi registrare 3 e montare.

## Montaggio (descritto)

`hook` (8 s dal clip della partita, testo "TROPIC TRAIL · EP 7") → card titolo 3 s → corpo (pagina 1920×1080 di `bmvideo`: console
640×360 ×2 con pannello laterale e tasti) → il segmento della partita va a **schermo pieno 480×270 ×4** senza pannello (scala
intera, vedi 08) con un riquadro discreto dei comandi. `bmvideo.encode` oggi fissa 640×360: serve un parametro per la dimensione
del raw `[DA VERIFICARE: modifica di video/lib/bmvideo.py o filtro dedicato in video/3d/]`.

## verify.py (cosa controlla)

Come `video/05-assistant/verify.py` e `video/07-play/verify.py`: legge la cartuccia salvata (`scripts/bmres.py`, `bmres.read`,
`SEC_LUA`) e rigioca `game.txt` con un log (copia con `_update` avvolto, `replay.py`).

| Controllo | Atteso |
|---|---|
| Codice | `SEC_LUA` contiene `map3d_build`, `map3d_objects`, `keymap`, `stick`, `animate`, `camera3d`, `lamp3d`, `shadow3d`; nessun `[DA VERIFICARE]` |
| Camera dall'assistente | le quattro righe del blocco 4c coincidono con il `code:` di `guide.3d_camera` in `src/ai/kb/guide_sdk.txt` |
| La partita gira | nessun errore Lua, `state` passa `title → play`, la musica parte |
| Mele | almeno 5 raccolte nei 20 s (`score` ≥ 5) |
| Casse | almeno 1 rotta (con lo spin) e la mela che rilascia |
| Buche | la prima buca attraversata senza caduta (`y < -6` mai raggiunto) |
| Vite | nessun `hit` nei 20 s: `lives == 3` |
| Traguardo | non raggiunto nei 20 s (è il compito del video 8) |
| Video | `ffprobe`: stream video e audio, 1920×1080, 60 fps, durata 244 ± 3 s |
| Audio | RMS a finestre di 5 s (come `video/03-sound/verify.py`): niente silenzio nei segmenti con musica |
