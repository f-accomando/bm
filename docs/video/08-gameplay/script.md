# Tropic Trail · Episode 8: The game, played (partita finale) — copione

Durata ~2:30. Nessun editor: una partita **giocata da un bot** (robot che registra i tasti come copione per `bmhost`, replay
deterministico: stesso copione = stesso video), la musica `JUNGLE`, schermata titolo e fine, il riepilogo della serie.
Prosa di servizio in italiano, tutto ciò che compare nel video in inglese. Modello: `video/07-play/` (2D) e `video/lib/bmvideo.py`.

**Dipendenza**: `video/3d/chain/07-code.bm` (video 7). Il video 7 a sua volta richiede `map3d_*` (AGY-01 `PENDING`): finché non
esistono, anche questo video è bloccato. Le posizioni sulla mappa (`z ≈ …`) sotto sono stime sul bible (9 × 96 celle, 2 buche,
1 gradino, 1 muro basso) `[DA VERIFICARE: z reali dopo la mappa del video 4]`; i tempi finali sono quelli del log del bot.

## Scelta di risoluzione: schermo pieno, 480×270 ×4, senza pannello

Decisione: **la partita va a schermo pieno, 1920×1080 a 60 fps, con il gioco a 480×270 scalato 4× a pixel interi (nearest), senza
il pannello laterale di `bmvideo`**. Motivi:

1. 1920 = 4 × 480 e 1080 = 4 × 270: pixel perfetti, nessuna sfocatura, la texture del 3D software si vede come sulla console.
   Con il pannello il gioco starebbe in 1280×720 = 2.67×, scala non intera: pixel di larghezza diversa.
2. Qui non c'è nessun editor: tab e tasti premuti, il motivo del pannello, non servono; il gioco ha già il suo HUD dentro.
3. Il bot «preme» i tasti; una tastiera in sovrimpressione che si accende renderebbe la partita finta. Solo un **riquadro discreto
   fisso** in basso a sinistra (testo piccolo, sfondo semitrasparente): `← ↑ → ↓ run · A jump · B spin`.
4. Il segmento di gioco del 07 usa la stessa resa, così i due video si assomigliano.

Costo: `bmvideo.encode` oggi fissa il raw a 640×360 e il layout con pannello; serve una variante a 480×270 con `scale=1920:1080:flags=neighbor`
`[DA VERIFICARE: parametro di bmvideo o filtro dedicato in video/3d/]`. La cartuccia è 480×270 (`--res 480x270`, PIANO §2)
`[DA VERIFICARE: dove la fissa il progetto del video 6]`.

## Gancio e chiusura

- **Gancio (0:00–0:08)**: 8 s dalla partita, il salto sopra la seconda buca con le mele in arco e lo spin su una cassa, 4× schermo
  pieno, in una barra in alto «TROPIC TRAIL: THE GAME» (`hook_cmd`). Sopra, la frase del gancio.
- **Chiusura (1:57–2:30)**: la frase finale sui sette strumenti, poi la scheda finale con il repo (15 s, vale come schermata finale
  di YouTube).
- **Riepilogo (1:12–1:57)**: scheda «HOW IT WAS MADE» (3 s) e **7 clip di 6 s**, i primi 6 s di ogni video della serie (il loro
  gancio), con una striscia in basso «EPISODE n · strumento» come `clip_cmd` del 2D `07-play`
  `[DA VERIFICARE: durata e contenuto dei ganci dei video 1–7; 4 è bloccato]`.

## Tabella

| Tempo | Sullo schermo | Narrazione (English) |
|---|---|---|
| 0:00 | Hook: Pico clears the second hole, spins a crate, apples rising; bar "TROPIC TRAIL: THE GAME" | Seven videos ago this was an empty cartridge. Now watch Pico play it, from start to finish. |
| 0:08 | Title card "Episode 8 · The game" (silent, 4 s) | — |
| 0:12 | Title screen of the game ("TROPIC TRAIL / PRESS A"), the JUNGLE song plays | Tropic Trail. The title screen, and the song we wrote in bm Sound: JUNGLE. |
| 0:18 | `A` pressed: Pico runs down the trail, camera behind him, first row of apples (z ≈ 0–14) | A starts the game. Pico runs down the trail, and the camera follows from behind. |
| 0:25 | First crate (z ≈ 16): `B`, the spin breaks it, an apple pops and is collected; counter top left | Apples to collect: the counter is at the top left. B spins, and a spun crate breaks and gives an apple. |
| 0:34 | First hole (z ≈ 32): `A` held, a full jump, apples in an arc | Holes in the trail. A jumps, and a longer press is a higher jump. |
| 0:41 | Crate (z ≈ 40): Pico lands on top, it breaks, he bounces | Land on top of a crate and it breaks too, and Pico bounces. |
| 0:46 | The step (z ≈ 50): he climbs it without jumping | A step: he just walks up. |
| 0:49 | Low wall (z ≈ 58): Pico runs into it, `hit` clip, HIT sound, LIVES 3 → 2 | A mistake on purpose: Pico runs into the low wall. That is the hit clip, the hit sound, and one life less. |
| 0:54 | Second try: `A`, he jumps over the wall | Second try: jump. |
| 0:57 | Second hole (z ≈ 72): full jump; last crate (z ≈ 84): spin | Second hole, one more crate, and the end of the trail is close. |
| 1:03 | The goal (z ≈ 96): the screen "COURSE CLEAR" with the apples counter (9 s) | Course clear! Every apple and every crate counted, and two lives left. |
| 1:12 | Card "HOW IT WAS MADE" (3 s, silent) | — |
| 1:15 | Clip ep 1 (bm Studio): Pico, the apple and the crate | Episode one, bm Studio: Pico, the apple and the crate, built from blocks. |
| 1:21 | Clip ep 2 (bm Pixel): textures | Episode two, bm Pixel: grass, path, rock, wood, and Pico's skin. |
| 1:27 | Clip ep 3 (bm Animator): skeleton and clips | Episode three, bm Animator: a skeleton, and five clips. |
| 1:33 | Clip ep 4 (bm SDK, the map) | Episode four, the map: a trail of nine by ninety-six tiles. |
| 1:39 | Clip ep 5 (bm Sound) | Episode five, bm Sound: six soft effects and the jungle song. |
| 1:45 | Clip ep 6 (bm SDK, the 3D project) | Episode six, the SDK: lights, shadows and fog. |
| 1:51 | Clip ep 7 (bm Code + assistant) | Episode seven, bm Code and the assistant: the game itself. |
| 1:57 | Closing: game still under the voice, then end card with the repo (15 s) | Seven tools, one game, and everything made inside the console. The trail is yours to change: open the project and make it longer. |
| 2:15 | End card "TROPIC TRAIL · bm · github.com/f-accomando/bm" (15 s, silent) | — |

## Capitoli (YouTube)

0:00 The finished game
0:12 Title screen and the JUNGLE song
0:18 Apples and crates
0:34 Holes, a step and a low wall
1:03 Course clear
1:12 How it was made, episode by episode
1:57 Thank you

## Il percorso di gioco (tempo per tempo)

Il bot decide da solo (come `video/07-play/bot.py`); la tabella è il comportamento atteso, da confermare con il log del primo
giro. Tempi nel video finale; `t` di gioco = tempo video − 0:12.

| Tempo | t gioco | Evento | Come lo decide il bot |
|---|---|---|---|
| 0:12–0:18 | 0:00–0:06 | titolo e musica | attende 330 frame, poi `A` per 4 frame |
| 0:18–0:25 | 0:06–0:13 | corsa, prima fila di mele | `up` sempre; sterza su `left`/`right` verso la mela più vicina davanti (distanza z < 6, scarto laterale ≤ 3) |
| 0:25–0:34 | 0:13–0:22 | prima cassa, mele a zig-zag | cassa davanti a < 1.4: `B` (spin); poi mele |
| 0:34–0:41 | 0:22–0:29 | buca 1 | `map3d_height(world, x, z + lead) == nil`: `A` tenuto per 40 frame a ~0.25 unità dal bordo |
| 0:41–0:46 | 0:29–0:34 | cassa saltata | cassa davanti a 1.6: `A` e atterra sopra |
| 0:46–0:49 | 0:34–0:37 | gradino | niente: il gradino (≤ 0.5) si sale camminando |
| 0:49–0:54 | 0:37–0:42 | muro basso: colpo | prima volta il bot **non** salta (parametro `wall_hit=1`): `hit`, vita 3 → 2, indietro di 0.6 |
| 0:54–0:57 | 0:42–0:45 | muro, secondo tentativo | `wall_hit` già usato: `A` tenuto a 1.2 dal muro |
| 0:57–1:03 | 0:45–0:51 | buca 2, ultime casse, traguardo | come buca 1; cassa: `B`; `pico.z >= goal.z` → `state == "clear"` |
| 1:03–1:12 | 0:51–1:00 | schermata finale | il bot rilascia tutto; il video resta 9 s su «COURSE CLEAR» |

Il colpo `hit` è voluto: la partita lo mostra una volta, e il verify lo conta (vite finali = 2).

## [DA VERIFICARE]

- Posizioni reali (z) di buche, gradino, muro, casse e arrivo, e la larghezza delle buche (≤ 2 celle).
- Durata effettiva della corsa (target 45–50 s, stima con passo 0.07 e zig-zag): se è più corta o lunga, spostare solo il
  riepilogo (i clip sono a durata fissa).
- Ganci dei video 1–7 (i clip da 6 s non esistono ancora; il 4 è bloccato).
- Che `pad 1 up` in `bmhost` muova `stick()` (la croce) nel 3D, e che `pad 1 a` / `pad 1 b` risultino `jump` / `spin` via `keymap`.
- Risoluzione 480×270 e dimensione del raw (vedi sopra).
