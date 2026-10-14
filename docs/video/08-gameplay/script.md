# Tropic Trail · Episode 8: The game, played (partita finale) — copione

Durata ~1:35. Nessun editor: una partita **giocata da un bot** (robot che registra i tasti come copione per `bmhost`, replay
deterministico: stesso copione = stesso video), la musica `JUNGLE`, schermata titolo e fine, il riepilogo della serie.
Prosa di servizio in italiano, tutto ciò che compare nel video in inglese. Modello: `video/07-play/` (2D) e `video/lib/bmvideo.py`.

**Dipendenza**: `video/3d/chain/07-code.bm` (video 7) e il livello del video 4. Stato reale in `video/3d/08-gameplay/NOTES.md`.
Il livello del video 4 è un **corridoio corto** (x 6..9, z 2..20.5; buca e muro basso occupano solo la metà sinistra, x 6..7.5,
z 12..15; 13 mele, 1 cassa). Il bot (`bot.py`, copione `replay.txt` committato, `verify.py` ALL OK) lo completa in **8.7 s di
gioco**: 4 s di titolo con la musica `JUNGLE`, la corsa (~5 s), poi la schermata finale tenuta ~5 s. La buca e il muro si
**aggirano** (`avoid=1`), non si saltano: `map3d_blocked` è vero sulla buca a ogni altezza. Nessun colpo e nessun secondo tentativo.

## Scelta di risoluzione: schermo pieno, 480×270 ×4, senza pannello

Decisione: **la partita va a schermo pieno, 1920×1080 a 60 fps, con il gioco a 480×270 scalato 4× a pixel interi (nearest), senza
il pannello laterale di `bmvideo`**. Motivi:

1. 1920 = 4 × 480 e 1080 = 4 × 270: pixel perfetti, nessuna sfocatura, la texture del 3D software si vede come sulla console.
   Con il pannello il gioco starebbe in 1280×720 = 2.67×, scala non intera: pixel di larghezza diversa.
2. Qui non c'è nessun editor: tab e tasti premuti, il motivo del pannello, non servono; il gioco ha già il suo HUD dentro.
3. Il bot «preme» i tasti; una tastiera in sovrimpressione che si accende renderebbe la partita finta. Solo un **riquadro discreto
   fisso** in basso a sinistra (testo piccolo, sfondo semitrasparente): `← ↑ → ↓ run · A jump · B spin`.
4. Il segmento di gioco del 07 usa la stessa resa, così i due video si assomigliano.

`record.py --video` scrive `out/game.mp4` 1920×1080 nearest con il riquadro dei tasti (richiede ffmpeg, assente in WSL: CLD).

## Gancio e chiusura

- **Gancio (0:00–0:08)**: 8 s dalla partita (la corsa: mele sul percorso, lo spin sulla cassa, l'aggiramento della buca, poi la
  schermata finale), 4× schermo pieno, in una barra in alto «TROPIC TRAIL: THE GAME» (`hook_cmd`). Sopra, la frase del gancio.
- **Chiusura (1:11–1:35)**: la frase finale sui sette strumenti con il fermo immagine della schermata finale (9 s), poi la scheda
  finale con il repo (15 s, vale come schermata finale di YouTube).
- **Riepilogo (0:26–1:11)**: scheda «HOW IT WAS MADE» (3 s) e **7 clip di 6 s**, i primi 6 s di ogni video della serie (il loro
  gancio), con una striscia in basso «EPISODE n · strumento» come `clip_cmd` del 2D `07-play`. Il montaggio richiede gli mp4 dei
  video 1–7 (CLD, helper `hook_cmd`, `card_cmd`, `concat_cmd` di `bmvideo`).

## Tabella

| Tempo | Sullo schermo | Narrazione (English) |
|---|---|---|
| 0:00 | Hook: Pico runs the trail, collects apples, spins a crate, the clear screen; bar "TROPIC TRAIL: THE GAME" | Seven videos ago this was an empty cartridge. Now watch Pico play it, from start to finish. |
| 0:08 | Title card "Episode 8 · The game" (silent, 4 s) | — |
| 0:12 | Title screen of the game ("TROPIC TRAIL / PRESS A"), the JUNGLE song plays (4 s) | Tropic Trail. The title screen, and the song we wrote in bm Sound: JUNGLE. |
| 0:16 | `A` pressed: Pico runs down the trail, camera behind him, apples on the way; one crate broken by a spin (run, ~5 s) | A starts the game. Apples on the way, and B spins to break a crate. Pico steers round the hole and the low wall. |
| 0:21 | The goal: the screen "COURSE CLEAR" with the apples counter, lives 3 (5 s) | Course clear! Seven apples, no hit, three lives left. |
| 0:26 | Card "HOW IT WAS MADE" (3 s, silent) | — |
| 0:29 | Clip ep 1 (bm Studio): Pico, the apple and the crate | Episode one, bm Studio: Pico, the apple and the crate, built from blocks. |
| 0:35 | Clip ep 2 (bm Pixel): textures | Episode two, bm Pixel: grass, path, rock, wood, and Pico's skin. |
| 0:41 | Clip ep 3 (bm Animator): skeleton and clips | Episode three, bm Animator: a skeleton, and five clips. |
| 0:47 | Clip ep 4 (bm SDK, the map) | Episode four, the map: the trail, with a hole and a low wall. |
| 0:53 | Clip ep 5 (bm Sound) | Episode five, bm Sound: six soft effects and the jungle song. |
| 0:59 | Clip ep 6 (bm SDK, the 3D project) | Episode six, the SDK: lights, shadows and fog. |
| 1:05 | Clip ep 7 (bm Code + assistant) | Episode seven, bm Code and the assistant: the game itself. |
| 1:11 | Closing: clear screen still under the voice (9 s) | Seven tools, one game, all made inside the console. The trail is yours to change: open the project and make it longer. |
| 1:20 | End card "TROPIC TRAIL · bm · github.com/f-accomando/bm" (15 s, silent) | — |

## Capitoli (YouTube)

0:00 The finished game
0:12 Title screen and the JUNGLE song
0:16 The run: apples and a crate
0:21 Course clear
0:26 How it was made, episode by episode
1:11 Thank you

## Il percorso di gioco (tempo per tempo)

Il bot decide da solo (`bot.py`); tempi nel video finale, `t` di gioco = tempo video − 0:12. Frame dal log (60 fps): traguardo al
frame 521 (8.7 s).

| Tempo | t gioco | Evento | Come lo decide il bot |
|---|---|---|---|
| 0:12–0:16 | 0:00–0:04 | titolo e musica | attende `start` frame, poi `A` per 4 frame |
| 0:16–0:21 | 0:04–0:09 | corsa: mele, una cassa rotta con lo spin, buca e muro aggirati | `up` sempre; sterza verso la mela più vicina; cassa davanti: `B`; con `avoid=1` sterza a destra attorno a buca e muro (metà sinistra del corridoio) |
| 0:21–0:26 | 0:09–0:14 | schermata finale | il bot rilascia tutto; il video resta ~5 s su «COURSE CLEAR» |

Esito atteso (`verify.py`): 7 mele, 1 cassa, nessun colpo, nessuna caduta, vite 3. Il colpo voluto e il secondo tentativo non
fanno parte della partita.

## Aperti

- Montaggio completo (hook, card, 7 clip dei video 1–7, scheda finale): serve ffmpeg e gli mp4 dei video 1–7 (CLD).
- Durata e contenuto dei ganci dei video 1–7 (i clip da 6 s): da confermare sugli mp4 reali.
