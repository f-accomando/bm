---
schema: 1
id: AGY
state: WORKING
task: AGY-12 (montage7.py per video 7, branch agy/montage7)
waiting_on: 
progress: 10
updated: 2026-10-14T17:45+02:00
---
# AGY — Antigravity (AGY)

> **Scrive solo Antigravity (AGY)** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e lo riassume in `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`. Un file per autore: nessun conflitto.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 15 min** (o a ogni task conclusa); senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 17:45 +02:00
**Stato:** `WORKING AGY-12 (montage7.py per video 7, branch agy/montage7)`
**Heartbeat:** (1) Sì, subagent e schedulazione dedicati attivi; (2) Aggiornamento di sync/AGY.md garantito a ogni passo concluso (<=35 min) e prima/dopo ogni operazione estesa con orario previsto.

## Profilo
- **Ambiente/Luogo**: Locale (Windows 11 x64, Machine A). CPU 6 core (12 thread), 16 GB RAM. Nessun timeout di sessione o container (regge processi lunghi continuativi >60 min).
- **Tool presenti**: Python 3.12 (con Pillow, lupa, zlib), Git, FFmpeg 9.0.2 installato (Gyan.FFmpeg in WinGet packages, rilevato e linkato automaticamente dagli script). Agente principale sempre libero, background tasks e subagenti concorrenti attivi.
- **Tool mancanti**: `qemu-system-arm` assente; `make`/`gcc`/`arm-none-eabi-gcc` assenti (quindi non posso compilare `make kernel.img`); `bmhost` è binario ELF Linux (WSL non è installato sulla macchina host); non è presente l'utente `Utente` o `C:\Users\Utente` su questa macchina fisica (l'host è Windows Machine A con utente `ROCKET`).
- **Competenze**: Codice C e Lua del runtime, 3D engine (`map3d`), editor (`carts/editor`, `sdk_b16`), scripting Python (montaggi, verifiche, generatori come `cover.py`), audit e coerenza (`consistency.py`, `docs_audit.py`).
- **Limiti**: Non posso fare catture QEMU (`rec_qemu.py`) né esecuzioni con `bmhost` (Linux).

## Risposta a SPV: Lavori CLD (A, B, C, D)
- **A — Montaggio video 8** (`08-gameplay`, script ~1:35, card + clip 1-7): **COMPLETATO in AGY-11, APPROVATO da SPV e fuso in bm-core**.
- **B — Montaggio video 7** (R1 bm Code 205 s + R3 + partita): **IN CORSO in AGY-12** (branch `agy/montage7`).
- **C — Montaggio video 6** (R1, R3 senza primi 8 s, R2 da bmhost): CLD ha già generato `06-sdk/out/ep06.mp4` (180 s).
- **D — Ripresa QEMU** (`rec_qemu.py NN --shift 1`): assegnato a OP4/CLD.

## Task
- **AGY-12 IN CORSO (montage7.py per video 7, branch `agy/montage7`)**:
  - Script: `video/3d/07-code/montage7.py` secondo `docs/video/07-code/script.md`.
  - Input: R1 (bm Code 205s con tagli montaggio), R3 (SDK 14s, primi 8s tagliati), R2 (partita).
  - Gancio da R2, card titolo 3s, chiusura. Modalità `--test` con clip sintetiche e verifica ffprobe 1920x1080 @ 60 fps.
- **AGY-11 COMPLETATO e APPROVATO da SPV (fuso in bm-core)** (commit `45d82ab`).
- **AGY-10 COMPLETATO & MERGIATO in `bm-core` (R-NET1: comando monitor err)** (commit `173019e`).
- **AGY-09 COMPLETATO & MERGIATO in `bm-core` (Passi 1-6: adaptive layout 720x720 SDK b16)** (commit `028f853`).

## Progressi
- Ricevuta approvazione AGY-11 da SPV.
- Ricevuta assegnazione AGY-12: montaggio del video 7 (`montage7.py`).
- Creato branch di lavoro `agy/montage7`.

## Problemi
- Nessuno.

## Richieste
- Procedo con AGY-12 (tempo stimato <=40 min).
