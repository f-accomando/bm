# Tropic Trail — stato della serie

Legenda: `[x]` fatto · `[ ]` da fare · `[-]` non applicabile · `BLOCCATO` vedi dipendenze.
Fonti: [`PIANO.md`](PIANO.md), [`../../SYNC.md`](../../SYNC.md). Aggiornare questa pagina a ogni passo
(la serie riparte dal primo video con una casella vuota non bloccata). Controllo dei documenti: `make video-3d-check`.

| Video | script | shot list | copertina | metadati | input auto | registrazione | montaggio | pubblicazione |
|---|---|---|---|---|---|---|---|---|
| 1 Studio | [x] | [x] | json [x], PNG [ ] | [x] | doc [x], record+verify [x] (bmhost) | [x] QEMU 1920x1080 60 fps, verify PASS | [ ] | [ ] |
| 2 Pixel | [x] | [x] | json [x], PNG [ ] | [x] | doc [x], record+verify [x] (bmhost) | [x] QEMU 1080p60, verify ALL OK | [ ] | [ ] |
| 3 Animator | [x] | [x] | json [x], PNG [ ] | [x] | doc [x], record+verify [x] (bmhost) | [x] QEMU 1080p60, verify 46/46 | [ ] | [ ] |
| 4 Map | [x] provvisorio | [x] provvisorio | json [x], PNG [ ] | [x] | doc [x] | BLOCCATO | BLOCCATO | BLOCCATO |
| 5 Sound | [x] | [x] | json [x], PNG [ ] | [x] | doc [x], record+verify [x] (bmhost) | [ ] | [ ] | [ ] |
| 6 SDK | [x] provvisorio (import da risolvere) | [x] provvisorio | json [x], PNG [ ] | [x] | doc [x] | BLOCCATO (4) | BLOCCATO | BLOCCATO |
| 7 Code | [x] (map3d_* non testate) | [x] | json [x], PNG [ ] | [x] | doc [x] | BLOCCATO (6) | BLOCCATO | BLOCCATO |
| 8 Gameplay | [x] | [x] | json [x], PNG [ ] | [x] | doc [x] | BLOCCATO (7) | BLOCCATO | BLOCCATO |

Documenti scritti il 2026-10-13 (`make video-3d-check`: 0 problemi). I `[DA VERIFICARE]` rimasti stanno in fondo a ogni `script.md`: vanno chiusi in prova, sull'editor vero, prima di registrare.

## Dipendenze bloccanti

| Bloccato | Da cosa | Come si sblocca |
|---|---|---|
| Video 4 (registrazione) | editor mappa 3D | `SYNC.md` AGY-01, 02, 03 `DONE` e fusi in `bm-core` (Antigravity, branch `feat/antigravity-map3d`) |
| Video 6 | output del video 4 e 5 (`chain/04-map.bm`, `chain/05-sound.bm`) | video 4 registrato (in prova si può usare una mappa fatta a script) |
| Video 7 | `chain/06-sdk.bm` e API `map3d_*` implementate | video 6; AGY-01 |
| Video 8 | `chain/07-code.bm` | video 7 |
| Tutti i video | registratore QEMU `video/lib/qemurec.py` fatto e provato (B); audio da risolvere (video 5/8); domande D3–D6 | risposta dell'utente |
| Copertine | `video/lib/cover.py` | AGY-05 (Antigravity) |
| Overlay dei tasti | oggi `bmvideo.make_ass` (luogo, tasto, tasti della scena, didascalia); chip di tab (`overlay.md`) | da scrivere (Claude cloud) |

## Da dove riprendere

1. Domande ancora aperte: D3–D7 (`PIANO.md` §6 e §9).
2. Registrare 1 → 2 → 3 → 5 (ordine di registrazione), poi 4 appena AGY-01..03 sono `DONE`.
3. `record.py`/`verify.py` dei video 1, 2, 3, 5: **fatti** (2026-10-13, `verify.py` esce 0 con stand-in `--start forge`; durate 210/210/202/190 s). Restano: prova `--start chain` a catena vera, ripresa QEMU (`video/3d/rec_qemu.py`, Claude cloud), correzioni delle shot list dalle `NOTES.md`, segnaposto `record.py` per 6-8 `[BLOCCATO]`.
4. Correzioni da riportare nei documenti (dettaglio in `video/3d/NN-nome/NOTES.md`): video 3 spin = 3 giri da 120 gradi su clip da 6 frame (non 180 x2); video 5 VOLUME finali APPLE 56, CRATE 72, JUMP 24, STEP 120, SPIN 104, HIT 104, musica 8 (`--wav` e stereo 48 kHz); video 1 nome modello "model" (5 backspace); video 2 tessere nella riga y=48.
5. Bug Makefile (`--lua $<` passa `bm.info`): corretto da Claude cloud sul suo branch; serve il via libera dell'utente per portarlo in `bm-core`.

## Avanzamento complessivo (2026-10-13)

9 task di peso uguale (11,1% ciascuna): 1 Piano e contratti 100 · 2 Documenti 8 video 100 · 3 Asset forge 100 · 4 record/verify video 1,2,3,5 95 · 5 Registratore QEMU e overlay 45 · 6 Copertine (AGY-05) 80 · 7 Editor mappa 3D (AGY-01..04) 5 · 8 Video 4,6,7,8 (script) 0 · 9 Riprese, montaggio, YouTube 3. **Totale ~59%.** Report finale delle feature/skill/editor: solo a completamento totale (richiesta dell'utente).

## Stato al 2026-10-14 pomeriggio

| Video | Pronto (record.py, verify.py, documenti) | Ripresa QEMU (Claude cloud) |
|---|---|---|
| 1, 2, 3, 5 | si, `verify.py` ALL OK | registrati e verificati (consegne in `chain-delivered/`) |
| 4 Map | si (`verify.py` ALL OK, con `Shift+S` / `Shift+G`) | in corso dalle 14:22, fine ~15:35 |
| 6 SDK | si (`verify.py` ALL OK; si apre `TROPIC.BME`) | da fare (parte con la mappa `chain-real/04-map-populated.bme`) |
| 7 Code | codice del gioco provato in bmhost; mancano `record.py` e `verify.py` di bm Code | da fare |
| 8 Gameplay | non iniziato | da fare |

Note delle riprese: `video/3d/NN-nome/NOTES.md` (1, 2, 3, 4, 5, 6, 7). Regole ricavate: `docs/progress/tools.md` (Rules).

## Branch da unire in `bm-core` (decide l'utente) e ordine consigliato

1. `feat/antigravity-map3d`: il motore `map3d` (kernel, runtime, editor mappa 3D). **Senza questo il kernel di `bm-core` non ha `map3d_build`** (provato sulla Pi vera, kernel `v0.2.5-358`: la cartuccia si ferma); i video 4-8 funzionano solo sui nostri branch.
2. `agy/sdk-720` (R39, layout SDK 720x720; non tocca `carts/sdk`) e `agy/net-err` (R-NET1, riga `err` del monitor).
3. `claude/pi-test` (ponte e test sulla Pi, documento `docs/design/pi-test.md`) e `claude/sync-report` (lezioni sul sync, Work Order di `bm-reviewer`, regole).
4. `feat/claude-video3d` (la serie: `video/3d/`, `docs/video/`, il fix e i tasti dell'editor `Shift+S` / `Shift+G`, `sync_hdr.py`): per ultimo, dopo il punto 1; contiene gia una copia dell'unione di `feat/antigravity-map3d`, quindi i conflitti vanno sui file dell'editor e di `runtime.c`/`map3d.c`.
`feat/claude-video3d-rec` (CLD: registratore QEMU, `rec_qemu.py`, consegne) segue `feat/claude-video3d`.
