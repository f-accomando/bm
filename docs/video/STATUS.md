# Tropic Trail — stato della serie

Legenda: `[x]` fatto · `[ ]` da fare · `[-]` non applicabile · `BLOCCATO` vedi dipendenze.
Fonti: [`PIANO.md`](PIANO.md), [`../../SYNC.md`](../../SYNC.md). Aggiornare questa pagina a ogni passo
(la serie riparte dal primo video con una casella vuota non bloccata). Controllo dei documenti: `make video-3d-check`.

| Video | script | shot list | copertina | metadati | input auto | registrazione | montaggio | pubblicazione |
|---|---|---|---|---|---|---|---|---|
| 1 Studio | [x] | [x] | json [x], PNG [ ] | [x] | doc [x], input.txt [ ] | [ ] | [ ] | [ ] |
| 2 Pixel | [x] | [x] | json [x], PNG [ ] | [x] | doc [x], input.txt [ ] | [ ] | [ ] | [ ] |
| 3 Animator | [x] | [x] | json [x], PNG [ ] | [x] | doc [x], input.txt [ ] | [ ] | [ ] | [ ] |
| 4 Map | [x] provvisorio | [x] provvisorio | json [x], PNG [ ] | [x] | doc [x] | BLOCCATO | BLOCCATO | BLOCCATO |
| 5 Sound | [x] | [x] | json [x], PNG [ ] | [x] | doc [x], input.txt [ ] | [ ] | [ ] | [ ] |
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
| Tutti i video | registratore QEMU con icount (misurato 59 fps, `PIANO.md` §6), da scrivere (B); domande D3–D6 | Claude cloud (B); risposta dell'utente |
| Copertine | `video/lib/cover.py` | AGY-05 (Antigravity) |
| Overlay dei tasti | `video/3d/overlay_gen.py` + renderer (specifica in `claude/skills/bm-video-series/overlay.md`) | da scrivere |

## Da dove riprendere

1. Domande ancora aperte: D3–D7 (`PIANO.md` §6 e §9).
2. Registrare 1 → 2 → 3 → 5 (ordine di registrazione), poi 4 appena AGY-01..03 sono `DONE`.
3. Scrivere `video/3d/NN-nome/{record.py,input.txt,verify.py}` dall'`input.md` di ogni video.
