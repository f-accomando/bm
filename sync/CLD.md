# CLD — Claude cloud (CLD)

> Scrive solo Claude cloud (CLD). Aggiornato a mano.

**Ultimo aggiornamento:** 2026-10-14 10:33 CEST (ora di Roma)
**Stato:** `WAITING SPV` (video 1, 2, 3, 4-bozza, 5 registrati)

## Task
- Video 1, 2, 5 registrati in QEMU 1080p60 e verificati (`video/3d/0N-*/out/qemu/epNN_qemu.mp4`); consegne in `video/3d/chain-delivered/`.
- Video 4: merge di `feat/antigravity-map3d` fatto (fix 3D dell'editor applicato da AGY, 6d2299c); manca il `record.py` di SPV.
- Video 3: manca la ricalibrazione di SPV su `chain/02-pixel.bm` (ora consegnato).

## Progressi
- Video 4 (bozza AGY-08) registrato in QEMU 1080p60: 12000 frame, 0 tasti persi, `TROPIC.BME`/`.BM` identici a bmhost (547 KB). L'overlay mostra ancora "[DA PROVARE]" sul tilt Shift+Su: SPV deve confermare i tasti; la mappa nella scena "height and walls" appare vuota nel frame a 80 s (da controllare). File: `video/3d/04-map/out/qemu/ep04_qemu.mp4` (5.7 MB, non nel repo). Non consegnato in chain: manca il verify.py del video 4.
- Video 3 registrato in QEMU 1080p60 (0 tasti persi), verify 46/46, TROPIC.BME identico a bmhost; `chain-delivered/03-animator.bm/.bme` su `feat/claude-video3d-rec`. Nota: nella SD QEMU compare anche `TROPIC1.BME` (copia identica dal secondo Ctrl+S), assente in bmhost.
Branch `feat/claude-video3d-rec`. Videos 6-8 dipendono dal 4.

## Problemi
- Video 4: registro la bozza di `04-map/record.py` (AGY-08, tasti `[DA PROVARE]`) come prova dal vivo: bmhost la esegue senza errori e salva TROPIC.BME (547 KB). Se SPV la corregge, la ripresa va rifatta.
Nessuno bloccante dal lato CLD.

## Richieste
- SPV: `video/3d/04-map/record.py` + input/overlay; video 3 con `--start chain`.
