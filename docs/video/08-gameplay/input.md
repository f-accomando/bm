# Tropic Trail · Episode 8 — automazione (da descrivere, non implementata)

Modello: `video/07-play/` della serie 2D (`bot.py`, `replay.py`, `record.py`, `verify.py`) e la skill `bm-video-tutorial`
(«Partita giocata»). Qui solo il piano; i file si scrivono in CLD-03.

## Ingressi e uscite

| Voce | Valore |
|---|---|
| Cartella | `video/3d/08-gameplay/` (`bot.py`, `replay.py`, `replay.txt`, `record.py`, `verify.py`) |
| Ingresso | `video/3d/chain/07-code.bm` (cartuccia del video 7) |
| Uscita | `video/3d/08-gameplay/replay.txt` (copione dei tasti del bot) + `ep08.mp4` |
| Runtime | `build/host/bmhost-bin` (senza `--tool`, senza assistente: nessun editor) |
| SD | pulita a ogni registrazione; `carts/TROPIC.BM` = copia di `07-code.bm` |
| Formato | cartuccia 480×270, pagina 1920×1080 a 60 fps, H.264; gioco 4× a pixel interi, senza pannello (motivi in `script.md`) |

## Il bot (`bot.py`, derivato da `video/07-play/bot.py`)

Un chunk Lua aggiunto in fondo al codice di una **copia** di `07-code.bm` (`bmres.read`, `SEC_LUA`, `bmres.write`): essendo in
fondo allo stesso file, vede le `local` del gioco (`pico`, `apples`, `crates`, `goal`, `world`, `state`, `lives`, `score`), come il bot 2D
vedeva `kip` e `slimes`. Sostituisce `stick`, `btn` e `btnp` con funzioni che rispondono al volere del bot (anche per i nomi dell'azione
`"jump"` e `"spin"` di `keymap`), decide a ogni `_update` e scrive nel log `PAD FRAME pad 1 bottoni`. Girato con `bmhost-bin
--seconds`, il log dà il copione:

```
python3 video/3d/08-gameplay/bot.py video/3d/chain/07-code.bm video/3d/08-gameplay/replay.txt --seconds 70 [chiave=valore ...]
```

Decisione a ogni frame (stato → mossa):

| Stato | Mossa |
|---|---|
| `state == "title"` | attende `start` frame (330), poi `a` per 4 frame |
| `state == "clear"` | rilascia tutto |
| corsa | `up` sempre; sceglie la mela più vicina in avanti (scarto z < 6, laterale ≤ 3) e preme `left`/`right` verso di essa |
| cassa davanti a < 1.4 (e `spin == 0`) | `b` per 1 frame (spin) |
| cassa davanti a 1.6 con salto (`crate_jump=1` per la cassa n. 3) | `a` tenuto: atterra sopra |
| buca davanti: `map3d_height(world, pico.x, pico.z + lead) == nil` | `a` tenuto `hole_hold` (40) frame a `lead` ≈ 0.25 dal bordo |
| muro davanti: `map3d_blocked(...)` a un passo | la prima volta niente (`wall_hit=1`), poi `a` tenuto `wall_hold` frame a 1.2 |
| gradino | niente (si sale camminando) |

Parametri (`chiave=valore`, come `pit_hold`, `slime_min` nel 2D): `start`, `lead`, `hole_hold`, `wall_hit`, `wall_hold`,
`crate_spin_min/max`, `apple_ahead`, `apple_side`. Si **tarano per tentativi** su `bmhost` senza video (nel 2D la finestra di
un nemico era stretta; qui lo è l'atterraggio della buca: `lead` × `hole_hold`), finché il log dice: traguardo, nessuna caduta,
un solo `hit`. Il gioco non usa `math.random`: il clock di `bmhost` è virtuale, lo stesso copione dà sempre la stessa partita.

Log del bot (righe che `bot.py` cerca, come `STOMP`/`LIVES`/`STATE` nel 2D): `APPLE n at frame f`, `CRATE n at frame f (spin|stomp)`,
`HIT lives=L at frame f`, `FALL at frame f` (caduta in buca, `y < -6`), `STATE clear at frame f score S lives L`.
Stampa a fine corsa: mele, casse, cadute, vite, frame del traguardo.

## Replay e controllo (`replay.py`)

`replay.py CART.bm replay.txt [--seconds 70]`: stessa cartuccia con un `_update` avvolto che registra gli stessi eventi, ma con
`--input replay.txt` (nessun bot). Serve a verificare che il copione registrato giochi come il bot (stesso frame del traguardo, stessi
punti). Dentro `record.py` e `verify.py` si usa questa funzione.

## Registrazione (`record.py`)

1. Cancella e ricrea `SD`, copia `07-code.bm` in `SD/carts/TROPIC.BM`.
2. `bot.py` → `replay.txt` (solo se manca o con `--rebot`).
3. `build/host/bmhost-bin SD/carts/TROPIC.BM --sd SD --seconds 70 --input replay.txt --video raw-play.rgb --wav play.wav --quiet`
   (raw 480×270 rgb24 ≈ 390 KB a frame: ~1.6 GB per 70 s, usare una fifo come consiglia la skill).
4. Codifica a 1920×1080: `scale=1920:1080:flags=neighbor`, riquadro dei comandi con `drawbox` + `drawtext` fisso in basso a sinistra, nessun `.ass`
   dei tasti `[DA VERIFICARE: variante di bmvideo.encode per 480×270]`.
5. Clip del riepilogo: `ffmpeg -t 6` dei primi 6 s di `video/3d/NN-*/epNN.mp4` per N = 1…7, striscia «EPISODE n · strumento» (`clip_cmd` di
   `video/07-play/record.py`; con `anullsrc` dove manca l'audio), scheda «HOW IT WAS MADE» (`card_cmd`).
6. Gancio (`hook_cmd`, 8 s da ~0:46 di partita = buca 2 e spin), card titolo 4 s, chiusura 18 s (fotogramma fermo del traguardo, `-loop`), scheda
   finale 15 s; `concat_cmd(..., audio=True)` in ordine: hook, titolo, partita, riepilogo, 7 clip, chiusura, scheda finale.

## verify.py (cosa controlla)

Rigioca `replay.txt` con `replay.py` e legge il log (come `video/07-play/verify.py`).

| Controllo | Atteso |
|---|---|
| La partita gira | nessun errore Lua (`ok`), `state` `title → play → clear` |
| Traguardo | `STATE clear` entro 70 s (target 51 s di gioco) |
| Mele | ≥ 45 (circa il 75 % delle ~60 più quelle rilasciate dalle casse) `[DA VERIFICARE: soglia dopo il primo giro]` |
| Casse | ≥ 8 rotte, con almeno 1 spin e almeno 1 salto sopra (`stomp`) |
| Buche | entrambe attraversate: nessun evento `FALL` |
| Vite | esattamente 1 `HIT` (il muro voluto): `lives == 2` a fine partita |
| Replay = bot | stesso frame del traguardo e stesso punteggio del log del bot |
| Video | `ffprobe`: video e audio, 1920×1080, 60 fps, durata = somma delle parti, target 150 ± 2 s (hook 8 + titolo 4 + partita 60 [titolo 6 + corsa 45 + fine 9] + scheda 3 + 7 clip da 6 s = 42 + chiusura 18 + scheda finale 15); se la corsa vera dura diversamente, adattare la parte «partita» (gli altri tempi sono fissi) |
| Pixel interi | un fotogramma della partita ridotto a 480×270 (nearest) coincide con lo scatto `--shots` dello stesso frame |
| Audio | RMS a finestre di 5 s: la musica `JUNGLE` presente su titolo e corsa, silenzio nella card muta |
