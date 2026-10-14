# Video 3 — bm Animator: a skeleton and five moves for Pico

Serie: Tropic Trail (episodio 3 di 8). Durata prevista: **3:30**. Ritmo calmo: ~130 parole/minuto (circa 2 parole al secondo),
ogni azione resta a schermo il tempo di leggerla. Narrazione e didascalie in **inglese**; la prosa di servizio è in italiano.

- **Strumento**: bm Animator (tab Dev). Edizione Pi (l'Animator non esiste su RGB30: manuale `editor-animator.md`, "Good to know").
- **Parte da**: `chain/02-pixel.bm` (modelli `PICO`, `APPLE`, `CRATE` dipinti; nessuno scheletro). **Produce**: `chain/03-animator.bm`
  (scheletro `PICO` + 5 clip: `idle`, `run`, `jump`, `spin`, `hit`). Dettagli in [`input.md`](input.md).
- **Fonti dei tasti**: `manuals/content/en/editor-animator.md` (tabelle F1/F2/F3), verificato con `carts/animator/main.lua`.
- Le code delle ossa sono in unità della griglia di Pico (x -1..4, centro 1,5; y 0..4; z -3..1; asse x = 1,5, z = 0,5). Le quantità di
  pressioni (`×N`) dei tasti `w a s d r f` (1/8 di unità l'una) si ricavano da `record.py` e non sono inventate qui. Vedi [`shot-list.md`](shot-list.md).

## Copione

| Tempo | Sullo schermo | Narrazione (English) |
|---|---|---|
| 0:00 | Gancio: Pico corre in loop su F1 con lo scheletro visibile (ripresa finale); titolo "Tropic Trail · Ep 3 · bm Animator" | Meet Pico, alive. One skeleton, five moves, all built inside the console. |
| 0:08 | Tab Dev, bm Animator, elenco file; si apre il progetto; F1: Pico gira da fermo | bm Animator lives in the Dev tab. It does not build models: it opens the ones you made in bm Studio. Here is Pico, still a statue. |
| 0:22 | Le pagine F2, F3, F4 una dopo l'altra, poi di nuovo F2 | Four pages. F1 plays, F2 builds the skeleton, F3 records clips, and F4 bakes sprites. We need the first three. |
| 0:30 | F2: N crea `root`, S/R alza la coda fino ai fianchi | On F2, press N. The first bone is called root. Press W to lift its tail to Pico's hips: each press moves it an eighth of a unit. |
| 0:42 | N aggiunge `spine`, poi `head`; Invio rinomina | N again adds a bone under the chosen one. The spine rises from the hips to the shoulders, and the head sits on top. Enter renames a bone. |
| 0:54 | Si sceglie `spine`, N crea `arm.L`; di nuovo `spine`, N crea `arm.R` a mano; poi `root`, `leg.L`, `leg.R` a mano | Choose the spine and add an arm. The names end in dot L and dot R. M copies a bone across the middle of the world, but Pico stands off to one side, so each right bone is made by hand. Choose the root and do the same for the legs. |
| 1:08 | `tail1`, `tail2` sul `root`; la lista mostra 9 ossa | The tail is two bones on the root. Nine bones in all. The limit is sixty-four, so there is plenty of room. |
| 1:20 | F2: K minuscolo; F1: K mostra lo scheletro sopra il modello | Now the skin. Press K, and every face follows its nearest bone. That is right for a blocky gecko. |
| 1:30 | F2: K maiuscolo, annulla; modo `v`, un poligono marcato e liberato | Capital K splits the faces by corner, so joints stretch, but a corner always follows one bone, never a mix. If a part follows the wrong bone, V lets you choose its faces by hand. |
| 1:45 | F3: N, nome `idle`, 24 frame, la spina dorsale respira (loop) | F3 is the timeline, twelve frames a second. N makes a clip, Enter names it. Idle is a quiet breath: turn the spine a little and a keyframe appears. |
| 2:00 | `run`: 8 frame, posa a frame 0, M a frame 4, Spazio, fluido (`i`), pelle di cipolla (`o`) | Run is eight frames and it loops. Pose one: arms and legs opposed. Go to frame four and press M to mirror the pose. Space plays it, and the loop closes by itself. |
| 2:20 | `jump`: L spegne il loop; accovacciata, slancio, atterraggio | Jump does not loop, so press L. A crouch, a stretch, and a landing. |
| 2:33 | `spin`: 6 frame, root gira di 120 gradi tre volte | Spin turns the root one hundred and twenty degrees, three times. Three keyframes make a full circle. |
| 2:45 | `hit`: 6 frame, Pico si inclina indietro e torna | Hit is a short lean back, and back to rest. It does not loop either. |
| 2:55 | F1: frecce sinistra/destra cambiano clip; Spazio; K; B (fusione); `<` `>` velocità | Back on F1, left and right change the clip, Space plays it. K shows the skeleton. B blends one clip into the next. |
| 3:10 | Ctrl+S; domanda "Save an editable copy"; risposta Enter/Space; messaggio `saved` | Control S saves. A cartridge is only read, so the first save makes an editable copy beside it. |
| 3:20 | F1: Pico corre con lo scheletro; cartello finale "Next: the trail" | Pico moves. The next video receives this file, with the skeleton and five clips, and we build the trail he runs along. |

Parole di narrazione: ~455 (a 130 parole/minuto, ~3:30 con le pause delle azioni).

## Capitoli (identici a youtube.md)

0:00 Meet Pico · 0:08 What bm Animator does · 0:22 The four pages · 0:30 Build the skeleton · 1:20 Skin the model ·
1:45 Idle · 2:00 Run (loop) · 2:20 Jump · 2:33 Spin · 2:45 Hit · 2:55 Preview the clips · 3:10 Save

## Problemi trovati

1. **Nomi delle ossa**: PIANO.md §2 propone `arm_l, arm_r, leg_l, leg_r` e "nomi ≤ 15 byte". Nel manuale e nel codice (`carts/animator/main.lua`
   righe 46, 615: `name:sub(1, 16)`) il limite è **16 lettere**, e il tasto `m` (specchio) usa il suffisso **`.L` / `.R`**
   (`arm.L` → `arm.R`). Questo video usa `arm.L, arm.R, leg.L, leg.R`; PIANO.md §2 va aggiornato (`root, spine, head, tail1, tail2,
   arm.L, arm.R, leg.L, leg.R`). I nomi con `hit`, `weapon`, `fist`... creano hitbox "hit" nella pagina F4 (non usata): nessuno dei 9 nomi li contiene,
   `root` è "body".
2. **Limiti verificati**: 64 ossa per scheletro (9 usate, ok), 255 animazioni per modello (5 usate, ok), nomi di osso e di animazione 16 lettere.
3. **Nessun peso misto**: la skin è *una faccia = un osso* (`k`) o *un angolo = un osso* (`K`); il manuale lo dice ("each corner follows one bone,
   no mixed weights"). PIANO.md non parla di pesi: nel video non si promettono pesi sfumati. Le giunture di Pico (modello a blocchi) vanno bene con `k`.
4. **Timeline a 12 fps** (non 60): `animate(m, anim, t, ...)` prende il tempo in secondi, quindi il gioco a 60 fps è comunque fluido
   (interpolazione `i`), ma le durate delle clip sono multipli di 1/12 s. Durate scelte: `idle` 24 frame (2 s), `run` 8 (0,67 s), `jump` 12 (1 s),
   `spin` 6, `hit` 6. Sono scelte editoriali, modificabili.
5. **Catena `.bm` e progetti `.bme`**: l'Animator modifica solo progetti `.bme`; un `.bm` aperto è in sola lettura e il primo salvataggio crea
   una copia `.BME`. PIANO.md §4 dice che l'output è `chain/03-animator.bm`: quel `.bm` va prodotto dal `.bme` (SDK `Ctrl+B`, oppure
   copia diretta dal salvataggio). Da decidere (vedi `input.md`).
6. **`bmhost` non ha la scheda Dev**: la sequenza "Dev ▸ bm Animator" è un cartello/overlay (o una ripresa QEMU breve), non un'azione ripresa.
7. **Console a 640×360 nel video**: `video/lib/bmvideo.py` registra `GAME_W, GAME_H = 640, 360`, mentre PIANO.md fissa il *gioco* a 480×270.
   L'Animator è uno strumento (non il gioco), quindi 640×360 va bene qui; la risoluzione 480×270 vale per i video 6-8.
8. **Il segno delle rotazioni** (`w`/`s` attorno a x, `a`/`d` attorno a y) e l'asse di `a d w s r f` nella pagina F2 sono verificati (camera yaw 0.5): le pose di `jump`, `spin`, `hit` sono tarate sul modello vero.
