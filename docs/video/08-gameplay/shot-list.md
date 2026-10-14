# Tropic Trail · Episode 8: The game, played — shot list

Tempi del video finale. Una riga = un'azione; le righe del gioco sono le azioni del bot (copione `replay.txt`, righe
`FRAME pad 1 bottoni`, committato), i tempi seguono il log (traguardo al frame 521 = 8.7 s di gioco, vedi `script.md`). Nessun
editor: la colonna «Tab» indica la scena. Tasti di gioco da `docs/api/input.md`: A = Space/Z/J, B = X/K, croce = frecce (o WASD);
azioni `jump`/`spin` da `keymap`. Nessuna sovrimpressione di tasti salvo il riquadro fisso dei comandi (riga 3).

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 1 | 0:00 | Gancio (clip della partita) | nessuno | 8 s dalla partita: corsa con le mele, spin su una cassa, schermata finale; barra in alto | TROPIC TRAIL: THE GAME |
| 2 | 0:08 | Card titolo | nessuno | "Episode 8 · The game" (4 s, muta) | Episode 8 · The game |
| 3 | 0:12 | Gioco ▸ titolo | nessuno (`replay.txt` frame iniziali vuoti) | "TROPIC TRAIL / PRESS A", suona `JUNGLE` (4 s); in basso a sinistra il riquadro fisso `← ↑ → ↓ run · A jump · B spin` | Title screen · song: JUNGLE |
| 4 | 0:16 | Gioco ▸ partita | `pad 1 a` (4 frame) | Pico di spalle sul corridoio, camera dietro | A: start |
| 5 | 0:17 | Gioco | `pad 1 up` tenuto; `left`/`right` a tratti | corsa; le mele sul percorso spariscono, il contatore sale | Run: the apples count at the top left |
| 6 | 0:18 | Gioco | `pad 1 b` (spin) | spin sulla cassa: si rompe, esce una mela e viene presa | B: spin breaks a crate |
| 7 | 0:19 | Gioco | `up` con `right` | Pico sterza attorno alla buca e al muro basso (metà sinistra del corridoio), nessun colpo | Round the hole and the wall |
| 8 | 0:21 | Gioco ▸ fine | `pad 1 none` | superato il traguardo: «COURSE CLEAR», contatore mele (7), vite 3 (5 s) | Course clear! |
| 9 | 0:26 | Card riepilogo | nessuno | "HOW IT WAS MADE" (3 s, muta) | How it was made |
| 10 | 0:29 | Clip ep 1 | nessuno | primi 6 s del video 1 (bm Studio) + striscia | EPISODE 1 · bm Studio |
| 11 | 0:35 | Clip ep 2 | nessuno | primi 6 s del video 2 (bm Pixel) | EPISODE 2 · bm Pixel |
| 12 | 0:41 | Clip ep 3 | nessuno | primi 6 s del video 3 (bm Animator) | EPISODE 3 · bm Animator |
| 13 | 0:47 | Clip ep 4 | nessuno | primi 6 s del video 4 (mappa 3D) | EPISODE 4 · the map |
| 14 | 0:53 | Clip ep 5 | nessuno | primi 6 s del video 5 (bm Sound) | EPISODE 5 · bm Sound |
| 15 | 0:59 | Clip ep 6 | nessuno | primi 6 s del video 6 (bm SDK 3D) | EPISODE 6 · bm SDK |
| 16 | 1:05 | Clip ep 7 | nessuno | primi 6 s del video 7 (bm Code + assistente) | EPISODE 7 · bm Code + AI |
| 17 | 1:11 | Chiusura | nessuno | fermo immagine del traguardo sotto la frase finale (9 s) | Seven tools, one game |
| 18 | 1:20 | Scheda finale | nessuno | "TROPIC TRAIL · bm · github.com/f-accomando/bm" (15 s, muta) | github.com/f-accomando/bm |

Totale ~1:35. I tempi 4–7 sono indicativi (la corsa dura ~5 s); i riferimenti fissi sono titolo 4 s, traguardo al frame 521.

## Tasti nuovi

Nessun tasto nuovo dell'utente (nessun editor). Nel gioco compaiono, già visti nel video 7: frecce (croce), A = `Space`, B = `X`.
Il riquadro dei comandi è fisso: non segue i tasti del bot.
