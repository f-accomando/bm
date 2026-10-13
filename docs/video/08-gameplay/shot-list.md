# Tropic Trail · Episode 8: The game, played — shot list

Tempi del video finale. Una riga = un'azione; le righe del gioco sono le azioni del bot (copione `replay.txt`, righe
`FRAME pad 1 bottoni`), i tempi sono stime da confermare con il log (`z` stimati: vedi `script.md`). Nessun editor: la colonna
«Tab» indica la scena. Tasti di gioco da `docs/api/input.md`: A = Space/Z/J, B = X/K, croce = frecce (o WASD); azioni `jump`/`spin`
da `keymap`. Nessuna sovrimpressione di tasti salvo il riquadro fisso dei comandi (riga 3).

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 1 | 0:00 | Gancio (clip della partita) | nessuno | salto sopra la buca 2 con le mele in arco, spin su una cassa; barra in alto | TROPIC TRAIL: THE GAME |
| 2 | 0:08 | Card titolo | nessuno | "Episode 8 · The game" (4 s, muta) | Episode 8 · The game |
| 3 | 0:12 | Gioco ▸ titolo | nessuno (`replay.txt` frame 0–330 vuoti) | "TROPIC TRAIL / PRESS A", suona `JUNGLE`; in basso a sinistra il riquadro fisso `← ↑ → ↓ run · A jump · B spin` | Title screen · song: JUNGLE |
| 4 | 0:18 | Gioco ▸ partita | `pad 1 a` (4 frame, frame ~330) | Pico di spalle sul sentiero, camera dietro | A: start |
| 5 | 0:19 | Gioco | `pad 1 up` tenuto; `left`/`right` a tratti | corsa; la prima fila di mele sparisce, il contatore sale | Run: the apples count at the top left |
| 6 | 0:25 | Gioco | `pad 1 b` (1 frame, `spin` 24 frame) | spin sulla prima cassa (z ≈ 16): si rompe, esce una mela e viene presa | B: spin breaks a crate |
| 7 | 0:29 | Gioco | `up` con `left`/`right` | zig-zag tra le mele; altre casse con `B` | Apples by distance |
| 8 | 0:34 | Gioco | `pad 1 up a` (A tenuto ~40 frame) | salto pieno sopra la buca 1 (z ≈ 32) | A: jump (hold = higher) |
| 9 | 0:41 | Gioco | `pad 1 up a` (A, atterra sulla cassa) | Pico atterra sulla cassa (z ≈ 40): si rompe, rimbalza | Land on a crate: it breaks |
| 10 | 0:46 | Gioco | `pad 1 up` | sale il gradino camminando | The step |
| 11 | 0:49 | Gioco | `pad 1 up` (nessun salto) | urta il muro basso (z ≈ 58): clip `hit`, suono HIT, LIVES 3 → 2, arretra | The hit: one life less |
| 12 | 0:54 | Gioco | `pad 1 up a` a ~1.2 dal muro | secondo tentativo: salta il muro | Second try: jump |
| 13 | 0:57 | Gioco | `pad 1 up a` (buca 2, z ≈ 72) | salto pieno, mele in arco | Second hole |
| 14 | 1:00 | Gioco | `pad 1 b` (cassa z ≈ 84) | spin sull'ultima cassa | B: spin |
| 15 | 1:03 | Gioco ▸ fine | `pad 1 none` | superato il traguardo: «COURSE CLEAR», contatore mele (9 s) | Course clear! |
| 16 | 1:12 | Card riepilogo | nessuno | "HOW IT WAS MADE" (3 s, muta) | How it was made |
| 17 | 1:15 | Clip ep 1 | nessuno | primi 6 s del video 1 (bm Studio) + striscia | EPISODE 1 · bm Studio |
| 18 | 1:21 | Clip ep 2 | nessuno | primi 6 s del video 2 (bm Pixel) | EPISODE 2 · bm Pixel |
| 19 | 1:27 | Clip ep 3 | nessuno | primi 6 s del video 3 (bm Animator) | EPISODE 3 · bm Animator |
| 20 | 1:33 | Clip ep 4 | nessuno | primi 6 s del video 4 (mappa 3D) `[DA VERIFICARE: video 4 bloccato]` | EPISODE 4 · the map |
| 21 | 1:39 | Clip ep 5 | nessuno | primi 6 s del video 5 (bm Sound) | EPISODE 5 · bm Sound |
| 22 | 1:45 | Clip ep 6 | nessuno | primi 6 s del video 6 (bm SDK 3D) | EPISODE 6 · bm SDK |
| 23 | 1:51 | Clip ep 7 | nessuno | primi 6 s del video 7 (bm Code + assistente) | EPISODE 7 · bm Code + AI |
| 24 | 1:57 | Chiusura | nessuno | fermo immagine del traguardo sotto la frase finale (18 s) | Seven tools, one game |
| 25 | 2:15 | Scheda finale | nessuno | "TROPIC TRAIL · bm · github.com/f-accomando/bm" (15 s, muta) | github.com/f-accomando/bm |

## Tasti nuovi

Nessun tasto nuovo dell'utente (nessun editor). Nel gioco compaiono, già visti nel video 7: frecce (croce), A = `Space`, B = `X`.
Il riquadro dei comandi è fisso: non segue i tasti del bot.
