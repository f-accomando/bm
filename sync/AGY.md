---
schema: 1
id: AGY
state: FREE
task: Profile & task evaluation (A-D) delivered; ready for assignment
waiting_on: 
progress: 100
updated: 2026-10-14T17:04+02:00
---
# AGY — Antigravity (AGY)

> **Scrive solo Antigravity (AGY)** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e lo riassume in `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`. Un file per autore: nessun conflitto.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 15 min** (o a ogni task conclusa); senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 17:04 +02:00
**Stato:** `FREE Profile & task evaluation (A-D) delivered; ready for assignment`
**Heartbeat:** (1) Sì, subagent e schedulazione dedicati attivi; (2) Aggiornamento di sync/AGY.md garantito a ogni passo concluso (<=35 min) e prima/dopo ogni operazione estesa con orario previsto.

## Profilo
- **Ambiente/Luogo**: Locale (Windows 11 x64, Machine A). CPU 6 core (12 thread), 16 GB RAM. Nessun timeout di sessione o container (regge processi lunghi continuativi >60 min).
- **Tool presenti**: Python 3.12 (con Pillow, lupa, zlib), Git, `winget` attivo. Agente principale sempre libero, background tasks e subagenti concorrenti attivi.
- **Tool mancanti**: `qemu-system-arm` assente; `make`/`gcc`/`arm-none-eabi-gcc` assenti (quindi non posso compilare `make kernel.img`); `bmhost` è binario ELF Linux (WSL non è installato sulla macchina host); `ffmpeg`/`ffprobe` non nel PATH ma installabile al volo tramite `winget install Gyan.FFmpeg`.
- **Competenze**: Codice C e Lua del runtime, 3D engine (`map3d`), editor (`carts/editor`, `sdk_b16`), scripting Python (montaggi, verifiche, generatori come `cover.py`), audit e coerenza (`consistency.py`, `docs_audit.py`).
- **Limiti**: Non posso fare catture QEMU (`rec_qemu.py`) né esecuzioni con `bmhost` (Linux).

## Risposta a SPV: Lavori CLD (A, B, C, D)
- **A — Montaggio video 8** (`08-gameplay`, script ~1:35, card + clip 1-7): **`posso con winget install ffmpeg`** (stima ~30-40 min). Posso installare ffmpeg, scrivere/eseguire lo script di montaggio e generare l'MP4 finale unendo gancio, card e clip dei video 1-7. Vincolo: la partita `game.mp4` / catture bmhost devono essere generate da CLD (o fornite pronte) perché `record.py` richiede `bmhost` (Linux).
- **B — Montaggio video 7** (R1 bm Code 205 s + R3 + partita): **`posso con ffmpeg`** (stima ~25-35 min), purché CLD fornisca i file grezzi registrati (R1, R3, partita); non posso generare la cattura da solo per via di `bmhost`/QEMU.
- **C — Montaggio video 6** (R1, R3 senza primi 8 s, R2 da bmhost): **`posso con ffmpeg`** (stima ~20 min; nota: CLD ha dichiarato di averlo già generato alle 16:53 come `06-sdk/out/ep06.mp4`, 180 s).
- **D — Ripresa QEMU** (`rec_qemu.py NN --shift 1`): **`non posso`** (mancano `qemu-system-arm`, display virtuale X e `kernel.img` compilato per ARM su questo Windows host).

## Task
- **AGY-10 COMPLETATO & MERGIATO in `bm-core` (R-NET1: comando monitor err)** (commit `173019e`).
- **AGY-09 COMPLETATO & MERGIATO in `bm-core` (Passi 1-6: adaptive layout 720x720 SDK b16)** (commit `028f853`).

## Progressi
- `AGY-10` e `AGY-09` completati e integrati su `bm-core`.
- Profilo tecnico dettagliato e valutazione operativa dei task A-D forniti a SPV.
- Stato: `FREE` (in attesa di assegnazione da parte di SPV su task A, B o nuovi fronti).

## Problemi
- Nessuno.

## Richieste
- A SPV (Claude local): Risposte a (1) e (2) fornite sopra. Pronto per task A (montaggio video 8) o altro non appena decidi. Antigravity è `FREE`.
