# Tropic Trail · Episode 8 — automazione

Modello: `video/07-play/` della serie 2D (`bot.py`, `replay.py`, `record.py`, `verify.py`) e la skill `bm-video-tutorial`
(«Partita giocata»). Implementata in `video/3d/08-gameplay/`; stato in `video/3d/08-gameplay/NOTES.md`.

## Ingressi e uscite

| Voce | Valore |
|---|---|
| Cartella | `video/3d/08-gameplay/` (`bot.py`, `replay.txt` committato, `record.py`, `verify.py`, `NOTES.md`) |
| Ingresso | `video/3d/chain/07-code.bm` (cartuccia del video 7) |
| Uscita | `replay.txt` (copione dei tasti del bot), `out/shots/p*.png`, `out/play.wav`; con `--video` `out/game.mp4` |
| Runtime | `build/host/bmhost-bin` (senza `--tool`, senza assistente: nessun editor) |
| SD | `out/sd`, ricreata a ogni registrazione; `carts/TROPIC.BM` = copia di `07-code.bm` |
| Formato | cartuccia 480×270, pagina 1920×1080 a 60 fps, H.264; gioco 4× a pixel interi (nearest), senza pannello (motivi in `script.md`) |

## Il bot (`bot.py`)

Un chunk Lua aggiunto in fondo al codice di una **copia** di `07-code.bm` (`bmres.read`, `SEC_LUA`, `bmres.write`): essendo in
fondo allo stesso file, vede le `local` del gioco. Sostituisce `stick`, `btn` e `btnp` con funzioni che rispondono al volere del bot
(anche per le azioni `"jump"` e `"spin"` di `keymap`), decide a ogni `_update` e scrive nel log righe `PAD`. La croce è normalizzata
sulla diagonale (0.7071) come quella vera: senza, il replay diverge. Il gioco non usa `math.random` e il clock di `bmhost` è virtuale:
lo stesso copione dà sempre la stessa partita.

```
python3 video/3d/08-gameplay/record.py [--rebot] [--video]      (WSL/Linux)
```

Il livello del video 4 è un corridoio corto (18 unità): il bot lo completa in 8.7 s (traguardo al frame 521). Decisione:

| Stato | Mossa |
|---|---|
| titolo | attende `start` frame (240 in `record.py`), poi `a` per 4 frame |
| clear | rilascia tutto |
| corsa | `up` sempre; sterza su `left`/`right` verso la mela più vicina davanti |
| cassa davanti | `b` (spin) |
| buca o muro davanti (`map3d_blocked`) | con `avoid=1` sterza attorno (occupano solo la metà sinistra del corridoio); un salto non può funzionare, `map3d_blocked` è vero sulla buca a ogni altezza |

I parametri (`chiave=valore`, come nel 2D) si tarano su `bmhost` senza video.

## Registrazione (`record.py`)

1. Ricrea `out/sd`, copia `07-code.bm` in `out/sd/carts/TROPIC.BM`.
2. `bot.py` → `replay.txt` (solo se manca o con `--rebot`).
3. `build/host/bmhost-bin` con `--input replay.txt`, senza `--tool`: scatti `out/shots/p*.png` e `out/play.wav` (15 s: titolo 4 s,
   corsa ~5 s, schermata finale il resto).
4. Con `--video` (serve ffmpeg, assente in WSL: CLD): `out/game.mp4` a 1920×1080, `scale` nearest, riquadro dei comandi fisso in
   basso a sinistra, audio del gioco.
5. Montaggio dell'episodio (CLD, helper `hook_cmd`, `card_cmd`, `concat_cmd` di `bmvideo`, serve ffmpeg e gli mp4 dei video 1–7):
   hook 8 s dalla partita, card titolo 4 s, partita (14 s), scheda «HOW IT WAS MADE» 3 s, 7 clip da 6 s (striscia «EPISODE n ·
   strumento»), chiusura 9 s (fermo immagine), scheda finale 15 s; `concat_cmd(..., audio=True)` in quest'ordine.

## verify.py (cosa controlla)

Rigioca `replay.txt` (senza bot) con un log e controlla sul livello del video 4:

| Controllo | Atteso |
|---|---|
| La partita gira | nessun errore Lua |
| Traguardo | lo stato raggiunge `clear`; frame stampato (521 = 8.7 s) |
| Mele | ≥ 6 delle 13 della mappa (il bot ne prende 7) |
| Casse | ≥ 1 rotta |
| Vite | `lives == 3` |
| Cadute e colpi | nessun `RFALL`, nessun `RHIT` |
| Scatti | ≥ 5 scatti di `record.py`, diversi tra loro (il gioco si muove), 480×270 |
| Audio | `play.wav` non silenzioso (rms > 50) |

Esito atteso: `RESULT: ALL OK`. Sul video finale (CLD, `ffprobe`): 1920×1080, 60 fps, video e audio, durata ≈ 95 s (hook 8 + titolo 4 +
partita 14 + scheda 3 + 7 clip 42 + chiusura 9 + scheda finale 15).
