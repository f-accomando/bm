# CLD — Claude cloud (CLD)

> Scrive solo Claude cloud (CLD). Aggiornato a mano.

**Ultimo aggiornamento:** 2026-10-14 09:20 CEST (ora di Roma)
**Stato:** `WORKING video 4: ripresa QEMU in corso` (bozza AGY-08 non ancora validata da SPV; finisce circa 10:15 ora di Roma)

## Task
- Video 1, 2, 5 registrati in QEMU 1080p60 e verificati (`video/3d/0N-*/out/qemu/epNN_qemu.mp4`); consegne in `video/3d/chain-delivered/`.
- Video 4: merge di `feat/antigravity-map3d` fatto (fix 3D dell'editor applicato da AGY, 6d2299c); manca il `record.py` di SPV.
- Video 3: manca la ricalibrazione di SPV su `chain/02-pixel.bm` (ora consegnato).

## Progressi
- Video 3 registrato in QEMU 1080p60 (0 tasti persi), verify 46/46, TROPIC.BME identico a bmhost; `chain-delivered/03-animator.bm/.bme` su `feat/claude-video3d-rec`. Nota: nella SD QEMU compare anche `TROPIC1.BME` (copia identica dal secondo Ctrl+S), assente in bmhost.
Branch `feat/claude-video3d-rec`. Videos 6-8 dipendono dal 4.

## Problemi
- Video 4: registro la bozza di `04-map/record.py` (AGY-08, tasti `[DA PROVARE]`) come prova dal vivo: bmhost la esegue senza errori e salva TROPIC.BME (547 KB). Se SPV la corregge, la ripresa va rifatta.
Nessuno bloccante dal lato CLD.

## Richieste
- SPV: `video/3d/04-map/record.py` + input/overlay; video 3 con `--start chain`.
