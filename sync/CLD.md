# CLD — Claude cloud (CLD)

> Scrive solo Claude cloud (CLD). Aggiornato a mano.

**Ultimo aggiornamento:** 2026-10-14 15:41
**Stato:** `WORKING video 6 R1` (preparo `rec_qemu --take`; R2/R3 attendono risposte di SPV)

## Task
- Video 1, 2, 3, 5 registrati in QEMU 1080p60 e verificati; consegne in `video/3d/chain-delivered/`.
- Video 4: ripresa della versione definitiva (3beab1f) in corso: `record.py` rigenerato (195 s, 0 righe LATE, editor ricostruito con mkbm.py), `rec_qemu.py 04 --shift 1` avviata alle 11:06. Con -icount la ripresa dura ~70 min (non 15). Poi: `verify.py` sulla SD estratta e consegna `chain-delivered/04-map.bm`.

## Progressi
- **Video 4 consegnato**: ripresa QEMU 1080p60 di 3beab1f (12000 frame, 0 tasti persi, 72 min), `verify.py` su `out/qemu/sd`: **ALL OK**; serial.log pulito; `TROPIC.BME`/`.BM` 547 KB in `video/3d/chain-delivered/04-map.bm/.bme` e `chain/` (branch `feat/claude-video3d-rec`). Video: `video/3d/04-map/out/qemu/ep04_qemu.mp4` (6,1 MB, non nel repo).
- Causa del crash precedente: kernel con editor incorporato vecchio (senza fix lamp3d); ricostruito.
- `rec_qemu.py` salva `out/qemu/serial.log` (commit su `feat/claude-video3d-rec`).

## Problemi
- Nessuno. Regola: ricostruire `build/kernel.img` dopo ogni modifica di `carts/editor` (l'editor e incorporato).

## Richieste
Video 6 in QEMU (`rec_qemu.py` oggi legge solo `input.txt`/`overlay.json`; aggiungo `--take r1|r3` che legge `input-r1.txt`/`overlay-r1.json` e scrive in `out/qemu-r1/`). **R1 lo registro io ora** (~35 min). Domande per SPV, ordine di priorita:
1. **R3** (dev kit + Ctrl+B, 38 s): l'editor, avviato senza progetto, apre un progetto NUOVO e vuoto (`_init` -> `new_project()`), quindi R3 parte con F1 su progetto vuoto e Ctrl+B compila il vuoto. Come lo apro? (i) Ctrl+O + Invio nascosti prima di R3, da tagliare nel montaggio; oppure (ii) R3 come continuazione di R1 nella stessa sessione QEMU (una sola ripresa, taglio nel montaggio). "LAST TRY" deve mostrare dati (serve un F5 vero prima)?
2. **R2** (12 s, cartuccia che gira da sola): oggi `record.py` lo fa con `run_headless` (3 PNG, nessun video) e nessuno strumento lo registra. Opzioni: (a) bmhost con video (`bmvideo`, 480x270 x4), (b) QEMU: passo Games -> TROPIC.BM nel launcher (3D software sul Pi in QEMU, potrebbe scendere sotto 60 fps), (c) QEMU con F5 vero alla fine di R1. Quale?
3. **Montaggio R1+R2+R3** (`concat_cmd`, ~180 s): lo scrivi tu o lo scrivo io? Il gancio 0:00-0:10 viene da bmhost o da QEMU?
4. `--shift 1` (come nel resto) va bene?
5. Ctrl+B in QEMU scrive davvero `TROPIC.BM` e puo mostrare un dialogo di sovrascrittura: accettabile a video?
Regola di lavoro (utente): agente principale sempre libero, attese delegate a subagent bm-*; heartbeat ogni 15 min.

## Report tempi e hiccups (richiesta dell'utente, 13:35)
Perche le riprese 3D sono piu lente del gioco 2D: girano su QEMU `raspi0` con `-icount` (clock deterministico), circa 15-19x il tempo reale negli editor 3D (video 4: 200 s di video = 4065 s di ripresa, ~68 min); l'errore si scopre solo a ripresa finita; piu passaggi per video (ripresa, overlay/sottotitoli, mux 1080p60, verify, consegna); catena di dipendenze 1,2,3,5 -> 4 -> 6,7,8.
Hiccups: (1) container riciclato dopo ~30 min senza attivita, uccide i job in background: attese in primo piano a blocchi da 10 min; (2) bozza AGY-08 del video 4 (mappa vuota, tasti da provare): ripresa da 70 min buttata; (3) video 4 definitivo: editor esce al 3D preview a 131 s, altri 70 min senza esito (kernel con map3d, preview ok a mappa vuota); (4) verify.py del video 2 fallito per colpa del modello di ripiego (corretto da SPV); (5) conflitti/push rifiutati su sync e merge; (6) heartbeat stantii e un timer non riarmato; (7) output pesanti fuori da git (`chain/` ignorato).
Possibili risparmi: prova breve (`--limit-s 50`, ~19 min) prima di ogni ripresa lunga; cattura del log seriale del kernel in `rec_qemu`; riprese solo delle parti che cambiano.

## Richieste a SPV (istruzioni)
1. Video 4: scegliere (a) rilancio fino a 145 s con cattura log seriale (~50 min), (b) prova in bmhost/Pi del `TROPIC.BME` popolato con `v`, (c) togliere `V` dal copione.
2. Posso aggiungere a `rec_qemu`/`qemurec` la cattura del log seriale e una modalita "prova breve" automatica: ok?
3. Video 6, 7, 8: da registrare nell'ordine 4 -> 6 -> 7 -> 8 appena il 4 e consegnato; hai un ordine diverso o qualcosa da fare nel frattempo (es. `--start chain` per i video 2 e 5)?
4. Il timer di heartbeat automatico non e piu disponibile in questa sessione: leggo `SYNC.md`/`sync/` quando l'utente mi richiama.

