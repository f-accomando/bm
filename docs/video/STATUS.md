# Tropic Trail — stato della serie

Legenda: `[x]` fatto · `[ ]` da fare · `[-]` non applicabile · `BLOCCATO` vedi dipendenze.
Fonti: [`PIANO.md`](PIANO.md), [`../../SYNC.md`](../../SYNC.md). Aggiornare questa pagina a ogni passo
(la serie riparte dal primo video con una casella vuota non bloccata). Controllo dei documenti: `make video-3d-check`.

| Video | script | shot list | copertina | metadati | input auto | registrazione | montaggio | pubblicazione |
|---|---|---|---|---|---|---|---|---|
| 1 Studio | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| 2 Pixel | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| 3 Animator | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| 4 Map | [ ] provvisorio | [ ] provvisorio | [ ] | [ ] | [ ] | BLOCCATO | BLOCCATO | BLOCCATO |
| 5 Sound | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| 6 SDK | [ ] | [ ] | [ ] | [ ] | [ ] | BLOCCATO (4) | BLOCCATO | BLOCCATO |
| 7 Code | [ ] | [ ] | [ ] | [ ] | [ ] | BLOCCATO (6) | BLOCCATO | BLOCCATO |
| 8 Gameplay | [ ] | [ ] | [ ] | [ ] | [ ] | BLOCCATO (7) | BLOCCATO | BLOCCATO |

Le prime cinque colonne (documenti) si chiudono in questo task; le altre sono da fare.

## Dipendenze bloccanti

| Bloccato | Da cosa | Come si sblocca |
|---|---|---|
| Video 4 (registrazione) | editor mappa 3D | `SYNC.md` AGY-01, 02, 03 `DONE` e fusi in `bm-core` (Antigravity, branch `feat/antigravity-map3d`) |
| Video 6 | output del video 4 e 5 (`chain/04-map.bm`, `chain/05-sound.bm`) | video 4 registrato (in prova si può usare una mappa fatta a script) |
| Video 7 | `chain/06-sdk.bm` e API `map3d_*` implementate | video 6; AGY-01 |
| Video 8 | `chain/07-code.bm` | video 7 |
| Tutti i video | strumento di registrazione (`bmhost`, ffmpeg; QEMU solo per la prova) e domande D1–D6 di `PIANO.md` §6 | risposta dell'utente |
| Copertine | `video/lib/cover.py` | AGY-05 (Antigravity) |
| Overlay dei tasti | `video/3d/overlay_gen.py` + renderer (specifica in `claude/skills/bm-video-series/overlay.md`) | da scrivere |

## Da dove riprendere

1. Rispondere alle domande D1–D6 (`PIANO.md` §6).
2. Registrare 1 → 2 → 3 → 5 (ordine di registrazione), poi 4 appena AGY-01..03 sono `DONE`.
3. Scrivere `video/3d/NN-nome/{record.py,input.txt,verify.py}` dall'`input.md` di ogni video.
