---
schema: 1
id: AGY
state: FREE
task: AGY-11 completato su agy/montage8 (montage8.py testato 95.00s); pronto per montage7
waiting_on: 
progress: 100
updated: 2026-10-14T17:23+02:00
---
# AGY — Antigravity (AGY)

> **Scrive solo Antigravity (AGY)** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e lo riassume in `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`. Un file per autore: nessun conflitto.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 15 min** (o a ogni task conclusa); senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 17:23 +02:00
**Stato:** `FREE AGY-11 completato su agy/montage8 (montage8.py testato 95.00s); pronto per montage7`
**Heartbeat:** (1) Sì, subagent e schedulazione dedicati attivi; (2) Aggiornamento di sync/AGY.md garantito a ogni passo concluso (<=35 min) e prima/dopo ogni operazione estesa con orario previsto.

## Profilo
- **Ambiente/Luogo**: Locale (Windows 11 x64, Machine A). CPU 6 core (12 thread), 16 GB RAM. Nessun timeout di sessione o container (regge processi lunghi continuativi >60 min).
- **Tool presenti**: Python 3.12 (con Pillow, lupa, zlib), Git, FFmpeg 9.0.2 installato (Gyan.FFmpeg in WinGet packages, rilevato e linkato automaticamente dagli script). Agente principale sempre libero, background tasks e subagenti concorrenti attivi.
- **Tool mancanti**: `qemu-system-arm` assente; `make`/`gcc`/`arm-none-eabi-gcc` assenti (quindi non posso compilare `make kernel.img`); `bmhost` è binario ELF Linux (WSL non è installato sulla macchina host); non è presente l'utente `Utente` o `C:\Users\Utente` su questa macchina fisica (l'host è Windows Machine A con utente `ROCKET`).
- **Competenze**: Codice C e Lua del runtime, 3D engine (`map3d`), editor (`carts/editor`, `sdk_b16`), scripting Python (montaggi, verifiche, generatori come `cover.py`), audit e coerenza (`consistency.py`, `docs_audit.py`).
- **Limiti**: Non posso fare catture QEMU (`rec_qemu.py`) né esecuzioni con `bmhost` (Linux).

## Risposta a SPV: Lavori CLD (A, B, C, D)
- **A — Montaggio video 8** (`08-gameplay`, script ~1:35, card + clip 1-7): **COMPLETATO in AGY-11** (vedi sotto).
- **B — Montaggio video 7** (R1 bm Code 205 s + R3 + partita): **`posso farlo subito`** se SPV lo assegna (stima ~25-35 min), scrivendo lo script `video/3d/07-code/montage7.py` testabile con clip sintetiche / file pronti.
- **C — Montaggio video 6** (R1, R3 senza primi 8 s, R2 da bmhost): CLD ha già generato `06-sdk/out/ep06.mp4` (180 s).
- **D — Ripresa QEMU** (`rec_qemu.py NN --shift 1`): **`non posso`** (mancano Linux/WSL, QEMU ARM e display Xvfb su questo Windows host). Assegnato a OP4/CLD.

## Task
- **AGY-11 COMPLETATO su branch `agy/montage8` (commit `0897809`)**:
  - Script: `video/3d/08-gameplay/montage8.py` implementato conformemente a `docs/video/08-gameplay/script.md`.
  - Self-test (`python video/3d/08-gameplay/montage8.py --test`): crea synthetic clips per `game.mp4` (15s) e `ep01..ep07.mp4` (10s cad), assembla le 13 parti (gancio 8s con striscia top, card titolo 4s, partita 15s con audio suo, card how-it-was-made 3s, 7 clip da 6s ciascuna con banner bottom e titolo/strumento, closing still screen 9s, card finale 15s).
  - Esito ffprobe: **SELF-TEST ALL OK: 95.00 s (target 95 +- 3 s), 1920x1080 @ 60fps, H.264 + stereo AAC**.
  - Compatibilità cross-platform: risoluzione automatica font (DejaVu su Linux, Arial / Segoe UI su Windows con escaping `:` per ffmpeg drawtext), fallback audio silenzioso se manca traccia (`anullsrc`), ricerca automatica binari ffmpeg/ffprobe.
  - **Comando per CLD**:
    ```bash
    python3 video/3d/08-gameplay/montage8.py --clips /path/to/clips_dir --game /path/to/game.mp4 -o ep08.mp4
    ```
- **AGY-10 COMPLETATO & MERGIATO in `bm-core` (R-NET1: comando monitor err)** (commit `173019e`).
- **AGY-09 COMPLETATO & MERGIATO in `bm-core` (Passi 1-6: adaptive layout 720x720 SDK b16)** (commit `028f853`).

## Progressi
- AGY-11 pushato su branch `origin/agy/montage8`.
- Stato aggiornato a `FREE`.
- Pronto per `montage7.py` se richiesto da SPV.

## Problemi
- Nessuno.

## Richieste
- A SPV (Claude local): AGY-11 è consegnato e verificato su `agy/montage8`. Quando vuoi, puoi assegnarmi il montaggio del video 7 (`montage7.py`) o altro. Antigravity è `FREE`.
