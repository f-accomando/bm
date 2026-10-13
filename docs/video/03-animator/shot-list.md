# Video 3 — shot list (bm Animator)

Una riga = un'azione. Tempi nel video finale, coerenti con [`script.md`](script.md). Tasti dal manuale `manuals/content/en/editor-animator.md`
e da `carts/animator/main.lua`. `×N` = numero di pressioni da tarare con una prova sul modello vero (non si inventa).
Ordine delle ossa nella lista di F2/F3 (per i tasti su/giù): 1 `root`, 2 `spine`, 3 `head`, 4 `arm.L`, 5 `arm.R`, 6 `leg.L`, 7 `leg.R`,
8 `tail1`, 9 `tail2` `[DA VERIFICARE: dove `m` mette la copia nella lista e quale osso resta scelto dopo `m`]`.
Rinominare: Invio, 5× Backspace (il nome proposto `bone2`… è già scritto e il testo si accoda: `src/script/bm3d.lua` `input_key`), il nome, Invio.

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 1 | 0:00 | bm Animator ▸ F1 play | (ripresa finale, nessun tasto) | Pico corre in loop con lo scheletro | Tropic Trail · Episode 3 · bm Animator |
| 2 | 0:08 | Dev ▸ bm Animator | (cartello; `bmhost` non ha la scheda Dev) | titolo dello strumento | Dev tab ▸ bm Animator |
| 3 | 0:11 | bm Animator ▸ elenco file | `↓` fino a `tropic`, `Invio` `[DA VERIFICARE: nome e posizione del file nell'elenco]` | l'elenco dei `.bm`/`.bme`, poi il progetto aperto | Open the cartridge |
| 4 | 0:14 | F1 play | (nessuno) | Pico gira da fermo; a sinistra MODELS e ANIMATIONS (vuota) | Pico: a model with no skeleton |
| 5 | 0:17 | F1 play | `]` poi `[` | passa al modello seguente e torna a `PICO` | ] [ : next / previous model |
| 6 | 0:22 | F2 rig | `F2` | pannello `NO SKELETON` | F2 · rig: bones and skin |
| 7 | 0:24 | F3 animate | `F3` | linea del tempo | F3 · animate: keyframes |
| 8 | 0:26 | F4 sprites | `F4` | impostazioni degli sprite | F4 · sprites (not used today) |
| 9 | 0:28 | F2 rig | `F2` | di nuovo la pagina rig | Back to F2 |
| 10 | 0:30 | F2 rig | `n` | nasce `root`; messaggio "a skeleton: one bone, root" | N · make the skeleton |
| 11 | 0:33 | F2 rig | `w` ×N | la coda di `root` sale ai fianchi (1/8 per pressione) | W · move the tail (1/8 unit per press) |
| 12 | 0:42 | F2 rig | `n` | nuovo osso `bone2` sotto `root` | N · a bone under the chosen one |
| 13 | 0:44 | F2 rig | `Invio`, `Backspace` ×5, scrivere `spine`, `Invio` | il nome diventa `spine` | Enter · rename the bone |
| 14 | 0:47 | F2 rig | `w` ×N | la coda di `spine` sale alle spalle | W · tail to the shoulders |
| 15 | 0:50 | F2 rig | `n`, poi `Invio`, `Backspace` ×5, `head`, `Invio`, `w` ×N | osso `head` sopra le spalle | N · Enter · the head |
| 16 | 0:54 | F2 rig | `↑` ×1 (sceglie `spine`) | `spine` in giallo nella lista | ↑ ↓ · choose the bone |
| 17 | 0:57 | F2 rig | `n`, `Invio`, `Backspace` ×5, `arm.L`, `Invio` | nuovo osso `arm.L` | N · arm.L |
| 18 | 1:00 | F2 rig | `d` ×N, `s` ×N `[DA VERIFICARE: asse di d e s nella pagina F2]` | la coda va sulla mano | D S · place the hand |
| 19 | 1:03 | F2 rig | `m` | "2 bones mirrored" e compare `arm.R` `[DA VERIFICARE: testo esatto con 1 osso]` | M · mirror to the other side |
| 20 | 1:05 | F2 rig | `↑` ×N fino a `root`, `n`, `Invio`, `Backspace` ×5, `leg.L`, `Invio` | osso `leg.L` dal fianco | Choose root · N · leg.L |
| 21 | 1:08 | F2 rig | `s` ×N, `d` ×N | la coda va al piede | S D · place the foot |
| 22 | 1:10 | F2 rig | `m` | compare `leg.R` | M · leg.R |
| 23 | 1:12 | F2 rig | `↑`/`↓` fino a `root`, `n`, `Invio`, `Backspace` ×5, `tail1`, `Invio` | osso `tail1` | N · tail1 |
| 24 | 1:15 | F2 rig | `r` ×N (o `f` ×N) e `s` ×N `[DA VERIFICARE: asse di r e f]` | la coda punta indietro | R F · tail backwards |
| 25 | 1:17 | F2 rig | `n`, `Invio`, `Backspace` ×5, `tail2`, `Invio`, `r` ×N | osso `tail2` sulla punta | N · tail2 |
| 26 | 1:19 | F2 rig | (nessuno) | lista BONES: 9 | 9 bones (the limit is 64) |
| 27 | 1:20 | F2 rig | `k` | "every face follows its nearest bone" | K · each face follows its nearest bone |
| 28 | 1:24 | F1 play | `F1` | Pico di nuovo in F1 | F1 · play |
| 29 | 1:26 | F1 play | `k` | scheletro disegnato sul modello | K · show the skeleton |
| 30 | 1:30 | F2 rig | `F2`, `K` (maiuscolo) | "every corner follows its nearest bone (smooth)" | Shift+K · each corner follows one bone |
| 31 | 1:34 | F2 rig | `Ctrl+Z` | torna la skin a facce | Ctrl+Z · undo |
| 32 | 1:37 | F2 rig ▸ skin mode | `v` | si passa alla scelta manuale delle facce | V · choose faces by hand |
| 33 | 1:40 | F2 skin mode | `→` ×1, `Spazio` | una faccia marcata | Arrows · Space: mark a face |
| 34 | 1:43 | F2 skin mode | `Backspace`, `v` | marcature cancellate, di nuovo le ossa | Backspace · clear · V back |
| 35 | 1:45 | F3 animate | `F3`, `n` | clip `anim1`, 12 frame, loop | N · new clip |
| 36 | 1:47 | F3 animate | `Invio`, `Backspace` ×5, `idle`, `Invio` | la clip si chiama `idle` | Enter · name: idle |
| 37 | 1:50 | F3 animate | `>` ×12 | la clip passa a 24 frame | > · longer by one frame |
| 38 | 1:53 | F3 animate | `↓` ×1 (`spine`), `→` ×12 | cursore al frame 12 | → · next frame |
| 39 | 1:55 | F3 animate | `W` (maiuscolo, 5°) | rombo (keyframe) al frame 12; la spina si inclina | Shift+W · turn 5 degrees |
| 40 | 1:57 | F3 animate | `Spazio` | l'animazione gira in loop: respiro | Space · play |
| 41 | 2:00 | F3 animate | `n`, `Invio`, `Backspace` ×5, `run`, `Invio` | clip `run` | N · Enter · name: run |
| 42 | 2:04 | F3 animate | `<` ×4 | 8 frame | < · shorter |
| 43 | 2:06 | F3 animate | `Home`, `↓` fino a `leg.L`, `w` ×2 | gamba sinistra avanti 30° | W · 30 degrees |
| 44 | 2:08 | F3 animate | `↓`, `s` ×2 (`leg.R`) | gamba destra indietro | S · the other way |
| 45 | 2:10 | F3 animate | `↑` fino a `arm.L`, `s` ×2; `↓`, `w` ×2 (`arm.R`) | braccia opposte alle gambe | arms against legs |
| 46 | 2:12 | F3 animate | `→` ×4 | cursore al frame 4 | → ×4 · frame 4 |
| 47 | 2:14 | F3 animate | `m` | posa specchiata: secondo keyframe | M · mirror the pose |
| 48 | 2:15 | F3 animate | `Spazio` | corsa in loop | Space · play |
| 49 | 2:17 | F3 animate | `i` fino a "smooth" `[DA VERIFICARE: valore iniziale di i]` | interpolazione morbida | I · linear / smooth / step |
| 50 | 2:18 | F3 animate | `o` | ossa del keyframe precedente e seguente in due colori | O · onion skin |
| 51 | 2:20 | F3 animate | `n`, `Invio`, `Backspace` ×5, `jump`, `Invio` | clip `jump` | N · name: jump |
| 52 | 2:23 | F3 animate | `l` | loop spento | L · loop off |
| 53 | 2:25 | F3 animate | `Home`, `w` su `leg.L` e `leg.R`, su `spine` ×1 | accovacciata `[DA VERIFICARE: segno della rotazione]` | crouch |
| 54 | 2:27 | F3 animate | `→` ×4, `r` su gambe e `s` su braccia (bracci in alto) | slancio | stretch |
| 55 | 2:30 | F3 animate | `→` ×4, `r` su gambe e braccia, `k` | atterraggio a riposo | land |
| 56 | 2:32 | F3 animate | `Spazio` | il salto gira una volta e si ferma | Space · play |
| 57 | 2:33 | F3 animate | `n`, `Invio`, `Backspace` ×5, `spin`, `Invio` | clip `spin` | N · name: spin |
| 58 | 2:36 | F3 animate | `l`, `<` ×4 | loop spento, 8 frame | L off · < ×4 |
| 59 | 2:38 | F3 animate | `↑` fino a `root`, `Home`, `→` ×4, `a` ×12 | `root` gira di 180° al frame 4 | A · turn around y (15° per press) |
| 60 | 2:41 | F3 animate | `End`, `a` ×12 `[DA VERIFICARE: l'angolo oltre 180° si accumula]` | altri 180°: giro completo | End · another half turn |
| 61 | 2:43 | F3 animate | `Spazio` | Pico gira su se stesso | Space · play |
| 62 | 2:45 | F3 animate | `n`, `Invio`, `Backspace` ×5, `hit`, `Invio` | clip `hit` | N · name: hit |
| 63 | 2:48 | F3 animate | `l`, `<` ×6 | loop spento, 6 frame | L off · < ×6 |
| 64 | 2:50 | F3 animate | `Home`, `→` ×2, `s` ×2 su `spine`, `s` ×1 su `head` | Pico si inclina indietro | S · lean back |
| 65 | 2:53 | F3 animate | `End`, `r` su `spine` e `head`, `k` | torna a riposo | R · rest · K · keyframe |
| 66 | 2:54 | F3 animate | `Spazio` | colpo e ritorno | Space · play |
| 67 | 2:55 | F1 play | `F1` | la pagina di anteprima | F1 · play |
| 68 | 2:57 | F1 play | `→` (clip successiva), `←` | cambia clip; la lista ANIMATIONS è in giallo | ← → · previous / next clip |
| 69 | 3:00 | F1 play | `Spazio` | la clip parte e si ferma | Space · play, stop |
| 70 | 3:02 | F1 play | `k` | scheletro sopra il modello | K · show the skeleton |
| 71 | 3:04 | F1 play | `b` ×2 | fusione col clip seguente al 25 %, 50 % | B · blend: 0, 25, 50, 75 % |
| 72 | 3:07 | F1 play | `>` ×2 poi `<` ×2 | più veloce, poi più lento | < > · slower / faster |
| 73 | 3:10 | F1 play | `Ctrl+S` | domanda "Save an editable copy: …BME?" `[DA VERIFICARE: tasto di risposta]` | Ctrl+S · save |
| 74 | 3:14 | F1 play | risposta alla domanda (vedi riga 73) | messaggio `saved /carts/…BME` | An editable copy, next to the game |
| 75 | 3:17 | menu | `Esc` | menù: Continue, Open, Save, Save as, Try the game, Open in bm Studio, Exit | Esc · the menu |
| 76 | 3:19 | menu | `Esc` | si torna a F1 | Esc · back |
| 77 | 3:20 | F1 play | `Spazio` su `run` | Pico corre con lo scheletro | Pico is ready |
| 78 | 3:25 | cartello finale | (nessun tasto) | "Next: the trail" | Next: the trail (map editor) |

## Tasti nuovi del video

Mai usati nei video 1-2 (Studio, Pixel); da mostrare in sovrimpressione la prima volta:

- Pagine: `F2` rig, `F3` animate, `F4` sprites (pagina `F1` play già nota come "vista"). `Esc` menu.
- F1: `←` `→` clip, `Spazio` play/stop, `k` scheletro, `b` fusione, `<` `>` velocità, `]` `[` modello.
- F2: `n` osso, `↑` `↓` scelta osso, `w a s d r f` sposta l'estremo (1/8), `m` specchio (`.L`/`.R`), `Invio` rinomina,
  `k` / `K` skin per faccia / per angolo, `v` scelta manuale delle facce, `Spazio` marca, `Backspace` cancella marcature.
- F3: `n` clip, `Invio` rinomina, `w s a d q e` ruota l'osso (15°, maiuscolo 5°), `→` `←` frame, `Home` `End`, `m` specchia la posa,
  `r` osso a riposo, `k` keyframe, `l` loop, `i` interpolazione, `o` pelle di cipolla, `<` `>` accorcia/allunga di un frame, `Spazio` play.
- Globali: `Ctrl+Z` annulla, `Ctrl+S` salva.
- Tasti dell'Animator usati col gamepad: nessuno (il video usa la tastiera).

Gli `[DA VERIFICARE]` di questa tabella sono raccolti in [`input.md`](input.md) §Da verificare.
