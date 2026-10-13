# Tropic Trail · Episode 7: Code the game — shot list

Tempi del video finale (gancio e titolo inclusi: corpo da 0:11). Una riga = un'azione. I tempi di battitura sono indicativi
(1 frame per carattere, `Enter` 2 frame); `record.py` li ricalcola dal copione di tasti e il narratore segue i capitoli, non il
secondo. I blocchi `1a…4e` sono in [`script.md`](script.md). Tasti copiati da `manuals/content/en/editor-code.md`,
`editor-sdk.md` e `docs/api/input.md`; i codici `type` da `video/lib/bmvideo.py`.

| # | Tempo | Tab / pagina / menu | Tasti esatti | Cosa si vede | Didascalia a schermo (English) |
|---|---|---|---|---|---|
| 1 | 0:00 | Gancio (clip del gioco finito) | nessuno | Pico corre, salta una buca, spin su una cassa, il contatore delle mele sale (8 s, 4× schermo pieno) | TROPIC TRAIL · EP 7 · bm Code |
| 2 | 0:08 | Card titolo | nessuno | "Episode 7 · Code the game" su fondo giungla (3 s, muta) | Episode 7 · Code the game |
| 3 | 0:11 | Dev ▸ bm Code | `Ctrl+O` | lista: `+ New project...` e i file `.bm`/`.bme` | Ctrl+O: open a cartridge |
| 4 | 0:14 | bm Code ▸ lista Open | `Enter` su `TROPIC.BM` `[DA VERIFICARE: riga della lista]` | si apre il tab con il codice di anteprima del video 6 | Enter: open Tropic Trail |
| 5 | 0:26 | bm Code ▸ editor | `Ctrl+K` ×`[N]` (3 frame di distanza) | le righe del codice di anteprima spariscono una a una; cursore in cima | Ctrl+K: cut a line |
| 6 | 0:36 | bm Code ▸ battitura | testo del blocco 1a (5 righe) + `Enter` dopo ogni riga | le variabili di stato | Typing: the state |
| 7 | 0:42 | bm Code ▸ battitura | `Enter`, blocco 1b (4 righe) + `Enter` | la funzione `near` | Typing: near() |
| 8 | 0:50 | bm Code ▸ battitura | `Enter`, blocco 1c (17 righe) + `Enter` | `_init`: keymap, mondo, Pico, mele, casse, musica | Typing: _init |
| 9 | 1:08 | bm Code ▸ battitura | `Enter`, blocco 2a (5 righe) + `Enter` | la funzione `hit` | Typing: hit() |
| 10 | 1:10 | bm Code ▸ battitura | blocco 2b (7 righe) | inizio di `_update`, titolo e fine | Typing: _update |
| 11 | 1:14 | bm Code ▸ battitura | blocco 2c (13 righe) | stick, muro, altezza del suolo | Typing: move and walls |
| 12 | 1:30 | bm Code ▸ battitura | blocco 2d (17 righe) | salto, gravità, caduta nella buca | Typing: jump, gravity, holes |
| 13 | 1:54 | bm Code ▸ battitura | blocco 3a (5 righe) | lo spin con il suo suono | Typing: spin |
| 14 | 1:58 | bm Code ▸ battitura | blocco 3b (7 righe) | la scelta della clip (`idle`, `run`, `jump`, `spin`, `hit`) | Typing: animation clips |
| 15 | 2:05 | bm Code ▸ battitura | blocco 3c (2 righe) | il suono dei passi | Typing: footsteps |
| 16 | 2:14 | bm Code ▸ battitura | blocco 3d (8 righe) | raccolta delle mele per distanza | Typing: apples |
| 17 | 2:19 | bm Code ▸ battitura | blocco 3e (16 righe) | rottura delle casse (spin o salto sopra) | Typing: crates |
| 18 | 2:31 | bm Code ▸ battitura | blocco 3f (3 righe), chiude `_update` | traguardo e vite | Typing: goal and lives |
| 19 | 2:38 | bm Code ▸ battitura | `Enter`, blocco 4a (15 righe) + `Enter` | la funzione `lights` | Typing: lights |
| 20 | 2:48 | bm Code ▸ battitura | blocco 4b (6 righe) | inizio di `_draw`, variabili `px, py, pz, a` | Typing: _draw |
| 21 | 2:56 | bm Code ▸ assistente | `F6` (in bmvideo: `\x1b[17~`) | si apre il pannello dell'assistente in modo codice | F6: the assistant |
| 22 | 3:00 | assistente | testo `third person camera` (4 frame per tasto) | la domanda scritta; le risposte compaiono mentre si scrive | Ask in English |
| 23 | 3:03 | assistente | nessuno (attesa 8 s) | risposta "Guide: the third-person camera" con il codice | It finds the guide |
| 24 | 3:12 | assistente | `Enter` | il blocco 4c entra nel codice, il pannello si chiude | Enter: insert the code |
| 25 | 3:15 | bm Code ▸ battitura | `Enter` (il cursore resta in fondo al blocco) | riga vuota sotto la camera | Enter: a new line |
| 26 | 3:18 | bm Code ▸ battitura | blocco 4d (9 righe) | terreno, ombra, casse, mele, Pico | Typing: draw the models |
| 27 | 3:24 | bm Code ▸ battitura | blocco 4e (10 righe), chiude `_draw` | HUD, schermate di titolo e fine | Typing: HUD and screens |
| 28 | 3:33 | bm Code ▸ salva | `Ctrl+S` | barra di stato: copia modificabile `TROPIC.BME` `[DA VERIFICARE: testo e conferma]` | Ctrl+S: save |
| 29 | 3:36 | bm SDK (registrazione separata) | `Ctrl+O` | lista dei progetti | Ctrl+O: open |
| 30 | 3:38 | bm SDK ▸ lista Open | `↓` ×`[N]`, `Enter` su `TROPIC.BME` `[DA VERIFICARE]` | pagina progetto F1 del gioco | Enter: open the project |
| 31 | 3:40 | bm SDK | `Ctrl+B` | "built TROPIC: it is in Games" | Ctrl+B: build the game |
| 32 | 3:44 | Gioco (registrazione separata, 480×270, 4×) | `A` (pad 1) | schermata titolo "TROPIC TRAIL / PRESS A"; la partita parte | A / Space: start |
| 33 | 3:46 | Gioco | `↑` tenuto (con `→`/`←` per spostarsi) | Pico corre, camera da dietro, mele che spariscono, contatore che sale | ← ↑ → ↓: run |
| 34 | 3:50 | Gioco | `B` | spin su una cassa: si rompe, esce una mela | B / X: spin |
| 35 | 3:55 | Gioco | `A` tenuto davanti a una buca | salto pieno sopra la buca | A / Space: jump (hold: higher) |
| 36 | 4:00 | Gioco | nessuno | Pico atterra, il contatore mele, fine del clip | Next: the whole trail |

## Tasti nuovi in questo video

Rispetto ai video 1–6 (editor grafici/audio); fonte `manuals/content/en/editor-code.md`, `editor-sdk.md`, `docs/api/input.md`.

| Tasto | Dove | Cosa fa |
|---|---|---|
| `Ctrl+O` | bm Code, bm SDK | apre la lista delle cartucce/progetti |
| `Ctrl+K` | bm Code | taglia la riga intera |
| `Ctrl+S` | bm Code | salva (un `.bm` diventa copia modificabile `.BME`) |
| `F6` | bm Code | assistente in modo codice; `Enter` inserisce il codice, `Esc` chiude |
| `Ctrl+B` | bm SDK | costruisce il `.bm` del progetto |
| frecce / `Space` (A) / `X` (B) | gioco | correre / saltare / spin (`keymap({ jump = "a", spin = "b" })`; tastiera da `docs/api/input.md`) |

Nota: la skill `bm-video-tutorial` dà F6 = `\xe6`, ma `video/lib/bmvideo.py` usa la sequenza `\x1b[17~` (i byte `0xE5-0xEF` grezzi
vengono scartati): vale il codice della libreria.
