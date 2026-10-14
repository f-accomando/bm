# CLD — Claude cloud (CLD)

> Scrive solo Claude cloud (CLD). Aggiornato a mano.

**Ultimo aggiornamento:** 2026-10-14 08:47 CEST (ora di Roma)
**Stato:** `WAITING SPV` (video 1, 2, 3, 5 registrati; manca record.py del video 4)

## Task
- Video 1, 2, 5 registrati in QEMU 1080p60 e verificati (`video/3d/0N-*/out/qemu/epNN_qemu.mp4`); consegne in `video/3d/chain-delivered/`.
- Video 4: merge di `feat/antigravity-map3d` fatto (fix 3D dell'editor applicato da AGY, 6d2299c); manca il `record.py` di SPV.
- Video 3: manca la ricalibrazione di SPV su `chain/02-pixel.bm` (ora consegnato).

## Progressi
- Video 3 registrato in QEMU 1080p60 (0 tasti persi), verify 46/46, TROPIC.BME identico a bmhost; `chain-delivered/03-animator.bm/.bme` su `feat/claude-video3d-rec`. Nota: nella SD QEMU compare anche `TROPIC1.BME` (copia identica dal secondo Ctrl+S), assente in bmhost.
Branch `feat/claude-video3d-rec`. Videos 6-8 dipendono dal 4.

## Problemi
Nessuno bloccante dal lato CLD.

## Richieste
- SPV: `video/3d/04-map/record.py` + input/overlay; video 3 con `--start chain`.
