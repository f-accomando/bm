# CLD — Claude cloud (CLD)

> Scrive solo Claude cloud (CLD). Aggiornato a mano.

**Ultimo aggiornamento:** 2026-10-14 12:25 CEST (ora di Roma)
**Stato:** `WAITING SPV` (video 4 definitivo: l'editor esce al tasto V nella ripresa QEMU)

## Task
- Video 1, 2, 3, 5 registrati in QEMU 1080p60 e verificati; consegne in `video/3d/chain-delivered/`.
- Video 4: ripresa della versione definitiva (3beab1f) in corso: `record.py` rigenerato (195 s, 0 righe LATE, editor ricostruito con mkbm.py), `rec_qemu.py 04 --shift 1` avviata alle 11:06 CEST. Con -icount la ripresa dura ~70 min (non 15). Poi: `verify.py` sulla SD estratta e consegna `chain-delivered/04-map.bm`.

## Progressi
- Video 4 definitivo (3beab1f) registrato in QEMU: 12000 frame, 0 tasti persi (`out/qemu/ep04_qemu.mp4`, non in repo). `verify.py` NON passa: la SD estratta contiene TROPIC.BME identico alla partenza (nessun salvataggio).

## Problemi
- **Video 4 in QEMU (kernel Pi): all'inizio del "3D preview" (tasto `V` a 130 s, frame 7830) l'editor bm SDK esce al launcher entro ~3 s** (il frame a 134 s e il launcher con "last: SDK"); i tasti successivi (frecce, `V`, Ctrl+S, Ctrl+B) vanno al launcher e il progetto non viene salvato. Fino a 130 s la ripresa e corretta (mappa, 4 layer, pavimento, mele/cassa/lanterna visibili). In bmhost la stessa sequenza funziona. Sospetto: il 3D preview del Map editor (map3d) crasha o esce sul kernel Pi/QEMU (da controllare anche su hardware Pi). Non e un tasto perso. Serve da SPV/AGY: indagare map3d su raspi0, oppure togliere/accorciare il preview dal video.
- Nota: `TROPIC1.BME` extra nella SD QEMU del video 3 (copia identica del secondo Ctrl+S).

## Richieste
- SPV: decidere fra (a) correggere il preview 3D su Pi e poi ri-registrare (~70 min), (b) togliere `V` dal copione. Non consegno `chain/04-map.bm`. Video 6-8 bloccati dal 4.
