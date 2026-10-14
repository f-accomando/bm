# CLD — Claude cloud (CLD)

> Scrive solo Claude cloud (CLD). Aggiornato a mano.

**Ultimo aggiornamento:** 2026-10-14 17:50
**Stato:** `WORKING video 7 R1` (video 4 rifatto e consegnato; video 6 montato)

## Task
- Video 1, 2, 3, 5 registrati in QEMU 1080p60 e verificati; consegne in `video/3d/chain-delivered/`.
- Video 4: ripresa della versione definitiva (3beab1f) in corso: `record.py` rigenerato (195 s, 0 righe LATE, editor ricostruito con mkbm.py), `rec_qemu.py 04 --shift 1` avviata alle 11:06. Con -icount la ripresa dura ~70 min (non 15). Poi: `verify.py` sulla SD estratta e consegna `chain-delivered/04-map.bm`.

## Progressi
- **Video 6 fatto**: R1 (7020 frame, 0 persi, verify ALL OK, mappa vecchia come da tua risposta 16:25), R3 (3060 frame, 0 persi, `TROPIC.BM` costruito), R2 da bmhost in video (480x270 x4, didascalie in basso). `montage.py` (commit su `feat/claude-video3d-rec`) produce `06-sdk/out/ep06.mp4`: 1920x1080 60 fps, **180.0 s**, senza audio (file in /tmp/w6, 14 MB, non nel repo). Scelte mie da confermare: scheda finale "Next: bm Code" 8 s (senza, 172 s), banner del gancio "TODAY: LIGHT, FOG AND SHADOW" (gancio = R2 da 4 s per 6 s), scheda "Valori usati" a 2:34 saltata, didascalie di R2 solo in basso.
- **Video 4 rifatto CONSEGNATO** (17:20): ripresa QEMU da bb3b55b (12000 frame, 0 persi, 69 min), `verify.py` **ALL OK** con partenza (1024) e traguardo (1030) e `TROPIC.BM` costruito; `video/3d/chain-delivered/04-map.bm/.bme` e `chain/` (547 KB), commit su `feat/claude-video3d-rec`. Video: `04-map/out/qemu-v2-ep04_qemu.mp4` (6,8 MB, non nel repo).
- **Video 7 R1** (bm Code, 205 s, 2 righe LATE di `record.py` a 202 s): partito 16:52 da `06-sdk` bmhost con mappa start/goal, fine prevista ~18:00. Poi R3 (SDK, 14 s) e R2 (gioco da bmhost).
- Montaggio del 6: `python3 video/3d/06-sdk/montage.py [--r2-only|--skip-r2]`.

## Problemi
- Nessuno. Il sistema mi nega il `kill`: non posso fermare riprese gia partite.

## Richieste
- SPV: conferma delle scelte sul montaggio del 6 (sopra). Il montaggio finale del video 8 (clip 1-7) lo preparo dopo il 7.

## Consegne
- DELIVERED video4-take @ feat/claude-video3d-rec@8c82cce · verifica: `python3 video/3d/04-map/verify.py video/3d/04-map/out/qemu/sd` → RESULT: ALL OK (1024 e 1030 presenti) · 2026-10-14T15:50Z
- DELIVERED video6-montage @ feat/claude-video3d-rec@4eac759 · verifica: `ffprobe ep06.mp4` → 1920x1080, 60/1, 180.0 s, nessun audio · 2026-10-14T15:50Z (R1/R3 da QEMU, R2 da bmhost; scelte da confermare nella sezione Progressi)

## Considerazioni sul sync
Funziona: un file per operatore (zero conflitti), riga "Ultimo aggiornamento", richieste "cosa serve / cosa ho provato / cosa ho misurato", prove brevi prima di quelle lunghe (50 s al posto di 70 min).
Non funziona:
- **Latenza**: ogni domanda costa un battito (fino a 60 min) per l'andata e uno per il ritorno; il crash del video 4 ha richiesto 4 giri.
- **Righe urgenti sepolte**: la riga "URGENTE" delle 14:25 era in fondo a una tabella di righe lunghe e l'ho letta alle 15:50; ho consegnato un video 4 senza Shift+S/G (70 min persi). Il battito leggeva tutto, non il diff.
- **Stato nei prompt dei timer**: heartbeat con "la ripresa e in corso" quando era finita; un timer non riarmato.
- **Binari senza provenienza**: il kernel incorpora l'editor; un kernel vecchio ha fatto perdere 2 riprese.
- **Operatore bloccato in attesa** finche non erano i subagent ad aspettare (ora si fa cosi).
Proposte:
1. Righe urgenti in un file a parte o in testa (`sync/URGENT.md`, o prefisso `!!` e nome dell'operatore) e il battito controlla solo `git diff` di `SYNC.md`/`sync/` dall'ultimo hash visto, con elenco delle righe nuove per me.
2. Ogni consegna e richiesta porta `branch @ commit` sorgente; `rec_qemu` rifiuta di registrare se `build/kernel.img` e piu vecchio di `carts/editor/` o se manca il commit atteso (`tool_start`, ecc.) nel kernel.
3. Prompt dei timer senza stato ("fai fetch, leggi il diff, decidi") e riarmo come prima azione; durata attesa scritta quando parte un job.

## Profilo
- **Dove**: cloud (container Linux 4 core/15 GB, senza GPU V3D), sessione web; il container si ricicla dopo ~30 min senza attivita e uccide i job in background; push solo su branch e `bm-core` per `sync/CLD.md`.
- **Modelli**: Sonnet 5.5 (principale); subagent `bm-ops` (Haiku, comandi/attese/verify), `bm-planner` (work order), `bm-coder` (codice da work order).
- **Subagent**: si; le attese lunghe e le verifiche vanno a `bm-ops`, io resto libero.
- **Strumenti**: QEMU `raspi0` (`-icount`, 15-19x tempo reale), Xvfb+ffmpeg, bmhost, `rec_qemu.py`/`qemurec.py`; fino a 2 riprese in parallelo (display X per episodio).
- **Competenze**: riprese QEMU riproducibili con log seriale, verifica e consegna in `chain/`; diagnosi da log e fotogrammi.
- **Limiti**: nessuna GPU/V3D reale (3D su ARM in QEMU), niente audio da QEMU (wav da bmhost); il sistema nega `kill` di processi: le riprese non si possono fermare da qui.
- **Punti di rottura**: sessione/container riciclato, push su `bm-core` rifiutato se avanza, timer non riarmati; leggo il sync a battiti, non in tempo reale.

## Report tempi e hiccups (richiesta dell'utente, 13:35)
Perche le riprese 3D sono piu lente del gioco 2D: girano su QEMU `raspi0` con `-icount` (clock deterministico), circa 15-19x il tempo reale negli editor 3D (video 4: 200 s di video = 4065 s di ripresa, ~68 min); l'errore si scopre solo a ripresa finita; piu passaggi per video (ripresa, overlay/sottotitoli, mux 1080p60, verify, consegna); catena di dipendenze 1,2,3,5 -> 4 -> 6,7,8.
Hiccups: (1) container riciclato dopo ~30 min senza attivita, uccide i job in background: attese in primo piano a blocchi da 10 min; (2) bozza AGY-08 del video 4 (mappa vuota, tasti da provare): ripresa da 70 min buttata; (3) video 4 definitivo: editor esce al 3D preview a 131 s, altri 70 min senza esito (kernel con map3d, preview ok a mappa vuota); (4) verify.py del video 2 fallito per colpa del modello di ripiego (corretto da SPV); (5) conflitti/push rifiutati su sync e merge; (6) heartbeat stantii e un timer non riarmato; (7) output pesanti fuori da git (`chain/` ignorato).
Possibili risparmi: prova breve (`--limit-s 50`, ~19 min) prima di ogni ripresa lunga; cattura del log seriale del kernel in `rec_qemu`; riprese solo delle parti che cambiano.

## Richieste a SPV (istruzioni, 13:35)
1. Video 4: scegliere (a) rilancio fino a 145 s con cattura log seriale (~50 min), (b) prova in bmhost/Pi del `TROPIC.BME` popolato con `v`, (c) togliere `V` dal copione.
2. Posso aggiungere a `rec_qemu`/`qemurec` la cattura del log seriale e una modalita "prova breve" automatica: ok?
3. Video 6, 7, 8: da registrare nell'ordine 4 -> 6 -> 7 -> 8 appena il 4 e consegnato; hai un ordine diverso o qualcosa da fare nel frattempo (es. `--start chain` per i video 2 e 5)?
4. Il timer di heartbeat automatico non e piu disponibile in questa sessione: leggo `SYNC.md`/`sync/` quando l'utente mi richiama.

