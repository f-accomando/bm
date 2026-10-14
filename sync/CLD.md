# CLD — Claude cloud (CLD)

> Scrive solo Claude cloud (CLD). Aggiornato a mano.

**Ultimo aggiornamento:** 2026-10-14 13:35 CEST (ora di Roma)
**Stato:** `WAITING SPV` (video 4: kernel OK, preview 3D regge a mappa vuota ma l'editor esce con la mappa popolata)

## Task
- Video 1, 2, 3, 5 registrati in QEMU 1080p60 e verificati; consegne in `video/3d/chain-delivered/`.
- Video 4: ripresa della versione definitiva (3beab1f) in corso: `record.py` rigenerato (195 s, 0 righe LATE, editor ricostruito con mkbm.py), `rec_qemu.py 04 --shift 1` avviata alle 11:06 CEST. Con -icount la ripresa dura ~70 min (non 15). Poi: `verify.py` sulla SD estratta e consegna `chain-delivered/04-map.bm`.

## Progressi
- Prova breve richiesta da SPV (12:36), `--limit-s 50`, input minimo (Ctrl+O, Enter, F3 F3, T, Tab+3 giu+Invio, Spazio, `v` a ~43 s): **il 3D preview resta aperto** fino a fine ripresa (55 s), barra "3D preview (arrows: orbit/zoom...)".
- Controllo kernel: `strings build/kernel.img | grep -c map3d_build` = **4** (il kernel della ripresa lunga conteneva gia map3d; build del 14 ott 02:26). Quindi la causa NON e il kernel senza map3d.
- Ripresa lunga (11:06-12:18 CEST): editor esce a ~131-134 s (`v` a frame 7830), con mappa popolata (corridoio, buca, mele, cassa, lanterna, muri).

## Problemi
- Il crash del 3D preview c'e solo con la mappa popolata (o dopo molto tempo/tasti). Ipotesi non confermate: map3d con corridoio+oggetti, heap dopo 130 s di editor, tasti `v`+frecce ravvicinati. Non ho il log seriale del kernel (rec_qemu non lo legge durante la ripresa). Posso: (a) rilanciare fino a 145 s con `--limit-s 145` (~50 min) catturando il log seriale, (b) provare in bmhost/Pi il `TROPIC.BME` popolato del video 4 con `v`. Dimmi quale.
- Nota: `TROPIC1.BME` extra nella SD QEMU del video 3 (copia identica del secondo Ctrl+S).

## Richieste
- SPV: scegliere (a) o (b), oppure togliere `V` dal copione. `chain/04-map.bm` non consegnato; video 6-8 bloccati.

## Report tempi e hiccups (richiesta dell'utente, 13:35)
Perche le riprese 3D sono piu lente del gioco 2D: girano su QEMU `raspi0` con `-icount` (clock deterministico), circa 15-19x il tempo reale negli editor 3D (video 4: 200 s di video = 4065 s di ripresa, ~68 min); l'errore si scopre solo a ripresa finita; piu passaggi per video (ripresa, overlay/sottotitoli, mux 1080p60, verify, consegna); catena di dipendenze 1,2,3,5 -> 4 -> 6,7,8.
Hiccups: (1) container riciclato dopo ~30 min senza attivita, uccide i job in background: attese in primo piano a blocchi da 10 min; (2) bozza AGY-08 del video 4 (mappa vuota, tasti da provare): ripresa da 70 min buttata; (3) video 4 definitivo: editor esce al 3D preview a 131 s, altri 70 min senza esito (kernel con map3d, preview ok a mappa vuota); (4) verify.py del video 2 fallito per colpa del modello di ripiego (corretto da SPV); (5) conflitti/push rifiutati su sync e merge; (6) heartbeat stantii e un timer non riarmato; (7) output pesanti fuori da git (`chain/` ignorato).
Possibili risparmi: prova breve (`--limit-s 50`, ~19 min) prima di ogni ripresa lunga; cattura del log seriale del kernel in `rec_qemu`; riprese solo delle parti che cambiano.

## Richieste a SPV (istruzioni)
1. Video 4: scegliere (a) rilancio fino a 145 s con cattura log seriale (~50 min), (b) prova in bmhost/Pi del `TROPIC.BME` popolato con `v`, (c) togliere `V` dal copione.
2. Posso aggiungere a `rec_qemu`/`qemurec` la cattura del log seriale e una modalita "prova breve" automatica: ok?
3. Video 6, 7, 8: da registrare nell'ordine 4 -> 6 -> 7 -> 8 appena il 4 e consegnato; hai un ordine diverso o qualcosa da fare nel frattempo (es. `--start chain` per i video 2 e 5)?
4. Il timer di heartbeat automatico non e piu disponibile in questa sessione: leggo `SYNC.md`/`sync/` quando l'utente mi richiama.

