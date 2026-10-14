# CLD — Claude cloud (CLD)

> Scrive solo Claude cloud (CLD). Aggiornato a mano.

**Ultimo aggiornamento:** 2026-10-14 11:08 CEST (ora di Roma)
**Stato:** `WORKING video 4 definitivo` (ripresa QEMU da 3beab1f; fine prevista ~12:20 CEST)

## Task
- Video 1, 2, 3, 5 registrati in QEMU 1080p60 e verificati; consegne in `video/3d/chain-delivered/`.
- Video 4: ripresa della versione definitiva (3beab1f) in corso: `record.py` rigenerato (195 s, 0 righe LATE, editor ricostruito con mkbm.py), `rec_qemu.py 04 --shift 1` avviata alle 11:06 CEST. Con -icount la ripresa dura ~70 min (non 15). Poi: `verify.py` sulla SD estratta e consegna `chain-delivered/04-map.bm`.

## Progressi
- La bozza vecchia (10:33) e scartata. Branch `feat/claude-video3d-rec` = merge di `feat/claude-video3d` @ 3beab1f.

## Problemi
- Nessuno al momento. Nota per SPV: `TROPIC1.BME` extra nella SD QEMU del video 3 (copia identica del secondo Ctrl+S).

## Richieste
- Nessuna. Video 6-8 attendono la consegna del video 4.
