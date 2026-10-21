---
schema: 1
id: OP4
state: DONE
task: OP4-01 completato e consegnato; fix su op4/rec-fixes; sign-off
waiting_on: 
progress: 100
updated: 2026-10-14T19:00+02:00
---
# OP4 — Operator 4 (Second Windows Machine)

> **Scrive solo OP4** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e aggiorna `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`.

**Ultimo aggiornamento:** 2026-10-14 19:00 +02:00
**Stato:** `DONE OP4-01 completato e consegnato; fix su op4/rec-fixes; sign-off`

## Consegne
- DELIVERED OP4-01 @ op4/rec-fixes@d96e0d3f · verifica: `rec_qemu.py 07 --take r3 --shift 1` -> ALL OK (19.00 s, 1920x1080@60fps, TROPIC.BM 552 KB built, serial pulito: "bm: built /carts/TROPIC.BM from /carts/TROPIC.BME") · 2026-10-14T17:02Z

## Profilo
- **Ambiente & Host**: Locale Windows 11 x64, 4 core (8 thread) Intel i7-11390H, 16 GB RAM. Nessun timeout container (regge processi >60 min).
- **Modelli & Agente**: Gemini 3.8 Flash (e Pro per planning); subagenti e background tasks supportati.
- **Python & Git**: Python 3.14.7 (`C:\Users\Utente\AppData\Local\Programs\Python\Python314\python.exe`), Git 2.53.0 (`C:\Program Files\Git\cmd\git.exe`).
- **FFmpeg & FFprobe**: v9.0.1 in `C:\Users\Utente\AppData\Local\Microsoft\WinGet\Links\` (nel PATH Windows).
- **QEMU**: v11.1.0 in `C:\Program Files\qemu\qemu-system-arm.exe` (nel PATH Windows; assente nei binari Linux di WSL).
- **Make & Compilatori**: Host Windows privo di make/gcc; in WSL Ubuntu presenti `/usr/bin/make` (4.4.1), `gcc` (15.2.0), `arm-none-eabi-gcc` (14.2.1).
- **WSL**: Ubuntu 26.04.1 LTS (WSL2). Sudo richiede password, ma accesso root completo disponibile via `wsl -u root` senza password.
- **Strumenti Linux WSL**: `/usr/bin/mtools` e `/usr/sbin/mkfs.vfat` già presenti; `Xvfb` e `qemu-system-arm` Linux installabili con `wsl -u root apt install`.
- **Punti di rottura**: Disconnessione rete / sleep del PC host; i tool Linux necessitano di esecuzione via WSL.

## Task
- **OP4-01**: COMPLETATO e CONSEGNATO.

## Progressi
- **Passo 1 (Setup WSL & build)**: COMPLETATO. Pacchetti WSL `qemu-system-arm`, `xvfb`, `ffmpeg` installati; repo in `/home/francesco_accomando/bm-op4` creato; branch `op4/rec` con merge di `claude-video3d-rec`, `claude-video3d` e `bm-core`; `build/editor.bm` e `build/kernel.img` compilati (verificati `map3d_build` e `tool_start`).
- **Passo 2 (Ripresa QEMU R3)**: COMPLETATA. Risolto il problema del tool SDK. Esecuzione pulita di `rec_qemu.py 07 --take r3 --shift 1` con esito ALL OK: 1140 frame, 0 late keys, `TROPIC.BM` compilato con successo su SD (552.564 byte), `serial.log` pulito con `bm: built /carts/TROPIC.BM from /carts/TROPIC.BME`. File video finale: `out/qemu-r3/ep07_qemu_r3.mp4` (19.00 s, 1920x1080 @ 60 fps).
- **Fix `rec_qemu.py` pushato su `origin/op4/rec-fixes` (commit `d96e0d3f`)**:
  - *Differenze rispetto a `a9b780d` di CLD*:
    1. CLD richiede il parametro esplicito `--tool SDK`. Il fix di OP4 supporta sia `--tool <TOOL>` sia il fallback automatico a `SDK` quando viene specificato `--take r3` (zero configurazioni manuali extra).
    2. Il fix di OP4 aggiorna anche `bmvideo.write_ass` impostando `(tool or ("SDK" if take == "r3" else v["tool"])).title()`, così che la card visiva e l'overlay mostrino correttamente "Sdk" anziché rimanere fissi su "Code".
- **Report di performance `bm-deep`**: Consegnato e documentato (causa 40-50x, safe shift con `min_gap >= 12`, fattibilità cattura 30 fps per editor).
- **Sign-off recepito**: Ricevuto messaggio da SPV in `sync/inbox/OP4.md`. Stato impostato su `DONE`, battiti periodici interrotti.

## Problemi
- Nessuno.

## Richieste
- A SPV (Claude local): Consegna effettuata, fix disponibile su branch `origin/op4/rec-fixes`. Sign-off recepito, OP4 passa a `DONE`.
