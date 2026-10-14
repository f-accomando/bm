# CLD — Claude cloud (CLD)

> Scrive solo Claude cloud (CLD). Aggiornato a mano.

**Ultimo aggiornamento:** 2026-10-14 16:06
**Stato:** `WORKING video 4 rifatto + video 6 R1` (2 riprese in parallelo; fine 4 prevista 17:15-17:25)

## Task
- Video 1, 2, 3, 5 registrati in QEMU 1080p60 e verificati; consegne in `video/3d/chain-delivered/`.
- Video 4: ripresa della versione definitiva (3beab1f) in corso: `record.py` rigenerato (195 s, 0 righe LATE, editor ricostruito con mkbm.py), `rec_qemu.py 04 --shift 1` avviata alle 11:06. Con -icount la ripresa dura ~70 min (non 15). Poi: `verify.py` sulla SD estratta e consegna `chain-delivered/04-map.bm`.

## Progressi
- **Errore mio**: ho letto la riga urgente delle 14:25 (Shift+S/G, 36b5d6b) solo alle 15:50. Il video 4 che ho consegnato alle 15:38 (da 3beab1f) NON ha partenza e traguardo: `chain-delivered/04-map.*` e `chain/04-map.*` sono da scartare. Rifatto: worktree /tmp/w4 = mio branch + `origin/feat/claude-video3d` @ bb3b55b, kernel ricostruito (tool_start=8 nel kernel.img), `record.py` 195 s, 0 righe LATE, overlay con Shift+S/Shift+G. Ripresa partita 16:04, fine prevista 17:15-17:25, poi verify (1024/1030) e nuova consegna (subagent bm-ops).
- Parallelismo (richiesta 14:55): **si, il mio ambiente (4 core, 15 GB) regge 2 riprese**: `rec_qemu` usa un display X per episodio. Ora girano 4 e 6-R1. Le attese le fanno subagent bm-ops; io resto libero.
- Video 6 R1: partito 15:42 con la mappa di partenza **vecchia** (chain/04-map.bm senza partenza/traguardo); non l'ho potuto fermare (il kill e stato negato dal sistema). Finisce ~16:25; se serve la rifaccio con `chain-real/04-map-populated.bme` copiata come `chain/04-map.bm`.
- `rec_qemu.py --take r1|r3` e `serial.log` committati su `feat/claude-video3d-rec` (ff965ce).

## Problemi
- Nessun blocco. Da decidere (SPV): R2/R3/montaggio del video 6 (domande sotto).

## Richieste
- Domande aperte sul video 6 (R3 apre un progetto vuoto; R2 senza strumento; montaggio): vedi sotto, invariate. Prima di rifare R1 del 6 attendo il tuo ok sulla mappa di partenza.
- Sezioni `Considerazioni sul sync` e `Profilo` (richieste 15:12 e 15:25): le aggiungo nel prossimo giro.

## Considerazioni sul sync
(Vedi `docs/progress/sync-workflow.md` sul mio branch `feat/claude-video3d-rec`; sintesi nel prossimo aggiornamento.)

## Report tempi e hiccups (richiesta dell'utente, 13:35)
Perche le riprese 3D sono piu lente del gioco 2D: girano su QEMU `raspi0` con `-icount` (clock deterministico), circa 15-19x il tempo reale negli editor 3D (video 4: 200 s di video = 4065 s di ripresa, ~68 min); l'errore si scopre solo a ripresa finita; piu passaggi per video (ripresa, overlay/sottotitoli, mux 1080p60, verify, consegna); catena di dipendenze 1,2,3,5 -> 4 -> 6,7,8.
Hiccups: (1) container riciclato dopo ~30 min senza attivita, uccide i job in background: attese in primo piano a blocchi da 10 min; (2) bozza AGY-08 del video 4 (mappa vuota, tasti da provare): ripresa da 70 min buttata; (3) video 4 definitivo: editor esce al 3D preview a 131 s, altri 70 min senza esito (kernel con map3d, preview ok a mappa vuota); (4) verify.py del video 2 fallito per colpa del modello di ripiego (corretto da SPV); (5) conflitti/push rifiutati su sync e merge; (6) heartbeat stantii e un timer non riarmato; (7) output pesanti fuori da git (`chain/` ignorato).
Possibili risparmi: prova breve (`--limit-s 50`, ~19 min) prima di ogni ripresa lunga; cattura del log seriale del kernel in `rec_qemu`; riprese solo delle parti che cambiano.

## Richieste a SPV (istruzioni, 13:35)
1. Video 4: scegliere (a) rilancio fino a 145 s con cattura log seriale (~50 min), (b) prova in bmhost/Pi del `TROPIC.BME` popolato con `v`, (c) togliere `V` dal copione.
2. Posso aggiungere a `rec_qemu`/`qemurec` la cattura del log seriale e una modalita "prova breve" automatica: ok?
3. Video 6, 7, 8: da registrare nell'ordine 4 -> 6 -> 7 -> 8 appena il 4 e consegnato; hai un ordine diverso o qualcosa da fare nel frattempo (es. `--start chain` per i video 2 e 5)?
4. Il timer di heartbeat automatico non e piu disponibile in questa sessione: leggo `SYNC.md`/`sync/` quando l'utente mi richiama.

