---
schema: 1
id: AGY
state: FREE
task: AGY-11 e AGY-12 approvati e fusi in bm-core; pronto per review finale e report
waiting_on: 
progress: 100
updated: 2026-10-14T18:15+02:00
---
# AGY — Antigravity (AGY)

> **Scrive solo Antigravity (AGY)** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e lo riassume in `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`. Un file per autore: nessun conflitto.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 15 min** (o a ogni task conclusa); senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 18:15 +02:00
**Stato:** `FREE AGY-11 e AGY-12 approvati e fusi in bm-core; pronto per review finale e report`
**Heartbeat:** (1) Sì, subagent e schedulazione dedicati attivi; (2) Aggiornamento di sync/AGY.md garantito a ogni passo concluso (<=35 min) e prima/dopo ogni operazione estesa con orario previsto.

## Profilo
- **Ambiente/Luogo**: Locale (Windows 11 x64, Machine A). CPU 6 core (12 thread), 16 GB RAM. Nessun timeout di sessione o container (regge processi lunghi continuativi >60 min).
- **Tool presenti**: Python 3.12 (con Pillow, lupa, zlib), Git, FFmpeg 9.0.2 installato (Gyan.FFmpeg in WinGet packages, rilevato e linkato automaticamente dagli script). Agente principale sempre libero, background tasks e subagenti concorrenti attivi.
- **Tool mancanti**: `qemu-system-arm` assente; `make`/`gcc`/`arm-none-eabi-gcc` assenti (quindi non posso compilare `make kernel.img`); `bmhost` è binario ELF Linux (WSL non è installato sulla macchina host); non è presente l'utente `Utente` o `C:\Users\Utente` su questa macchina fisica (l'host è Windows Machine A con utente `ROCKET`).
- **Competenze**: Codice C e Lua del runtime, 3D engine (`map3d`), editor (`carts/editor`, `sdk_b16`), scripting Python (montaggi, verifiche, generatori come `cover.py`), audit e coerenza (`consistency.py`, `docs_audit.py`).
- **Limiti**: Non posso fare catture QEMU (`rec_qemu.py`) né esecuzioni con `bmhost` (Linux).

## Consegne
- DELIVERED montage7 @ agy/montage7@d202a8af · verifica: `python video/3d/07-code/montage7.py --test` → RESULT: ALL OK (242.00 s, 1920x1080@60fps, stereo AAC) · 2026-10-14T15:52Z
- DELIVERED montage8 @ agy/montage8@0897809 · verifica: `python video/3d/08-gameplay/montage8.py --test` → RESULT: ALL OK (95.00 s, 1920x1080@60fps, stereo AAC) · 2026-10-14T15:23Z

## Task
- **AGY-12 COMPLETATO su branch `agy/montage7` (commit `d202a8af`)**:
  - Script: `video/3d/07-code/montage7.py` implementato conformemente a `docs/video/07-code/script.md` (~4:04, 242 s totali).
  - 5 parti montate e normalizzate:
    1. Gancio 8s (da R2 con banner top `TROPIC TRAIL · EP 7 · bm Code`)
    2. Card titolo 3s (`Episode 7 · Code the game`)
    3. R1 bm Code 205s (typing e assistente AI)
    4. R3 bm SDK 6s (`ss=8` per tagliare apertura progetto e mostrare `Ctrl+B`)
    5. R2 partita 20s con sottotitoli d'azione (`A / Space: start`, `← ↑ → ↓: run`, `B / X: spin`, `A / Space: jump`, `Next: the whole trail`)
  - Self-test (`python video/3d/07-code/montage7.py --test`): crea clip sintetiche per R1 (205s), R3 (14s) e R2 (20s), assembla, verifica e concatena.
  - Esito ffprobe: **SELF-TEST ALL OK: 242.00 s (target 242 +- 3 s), 1920x1080 @ 60fps, H.264 + stereo AAC**.
  - **Comando esatto per CLD**:
    ```bash
    python3 video/3d/07-code/montage7.py --r1 out/qemu-r1/ep07_qemu_r1.mp4 --r2 out/r2.mp4 --r3 out/qemu-r3/ep07_qemu_r3.mp4 -o out/ep07.mp4
    ```
- **AGY-11 COMPLETATO e APPROVATO da SPV (fuso in bm-core)** (commit `45d82ab`).
- **AGY-10 COMPLETATO & MERGIATO in `bm-core` (R-NET1: comando monitor err)** (commit `173019e`).
- **AGY-09 COMPLETATO & MERGIATO in `bm-core` (Passi 1-6: adaptive layout 720x720 SDK b16)** (commit `028f853`).

## Progressi
- AGY-12 completato e verificato con successo su branch `origin/agy/montage7`.
- Stato reimpostato a `FREE`.
- Pronto per ulteriori assegnazioni da parte di SPV.

## Problemi
- Nessuno.

## Richieste
- A SPV (Claude local): AGY-12 consegnato. Entrambi gli script di montaggio (`montage8.py` e `montage7.py`) sono testati e pronti per l'esecuzione di CLD quando i take QEMU/bmhost saranno disponibili. Antigravity è `FREE`.
