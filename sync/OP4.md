---
schema: 1
id: OP4
state: WORKING
task: OP4-01 passo 2 (fix tool SDK in rec_qemu.py e riesecuzione r3)
waiting_on: 
progress: 75
updated: 2026-10-14T18:42+02:00
---
# OP4 — Operator 4 (Second Windows Machine)

> **Scrive solo OP4** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e aggiorna `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 15 min** (o a ogni task conclusa); senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 18:42 +02:00
**Stato:** `WORKING OP4-01 passo 2 (fix tool SDK in rec_qemu.py e riesecuzione r3)`
**Heartbeat:** Aggiornamento garantito ogni <=15 min o a fine task; subagent/background scheduler attivi.

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

## Risposta a SPV: Lavori (A, B, D)
- **(A) Montaggio video 8 con ffmpeg** (clip mp4 1-7): **`posso`** (stima ~30-40 min). FFmpeg 9.0.1 è presente e verificato. Posso comporre clip, audio e card non appena le clip grezze/partita sono disponibili.
- **(B) Montaggio video 7**: **`posso`** (stima ~25-35 min), purché siano fornite o disponibili le tracce video/audio registrate.
- **(D) Ripresa QEMU con `qemurec.py` / `rec_qemu.py`**: **`posso con installazione pacchetti in WSL`** (stima setup 10 min, poi ~15-20 min per prova di ripresa). In WSL2 Ubuntu ho già Linux, `mtools`, `mkfs.vfat`, `python3` e `arm-none-eabi-gcc`. Mancano solo `Xvfb` e `qemu-system-arm` Linux, che posso installare immediatamente senza password con `wsl -u root apt update && apt install -y qemu-system-arm xvfb`.

## Task
- **OP4-01**: Ripresa QEMU di R3 del video 7 (in corso, passo 2).

## Progressi
- **Passo 1 (Setup WSL & build)**: COMPLETATO. Pacchetti WSL `qemu-system-arm`, `xvfb`, `ffmpeg` installati; repo in `/home/francesco_accomando/bm-op4` creato; branch `op4/rec` con merge di `claude-video3d-rec`, `claude-video3d` e `bm-core`; `build/editor.bm` e `build/kernel.img` compilati (verificati `map3d_build` e `tool_start`).
- **Passo 2 (Ripresa QEMU R3)**: `record.py --build-only` eseguito (input-r3.txt 840 frame, 14 s); prima ripresa QEMU completata (1260 frame 1920x1080 @ 60 fps, 0 late keys, serial.log pulito senza errori Lua). Riscontrato che `rec_qemu.py` per ep 07 apriva di default l'icona Dev `CODE` anziché `SDK` (quindi `Ctrl+B` era una selezione testo anziché build cartuccia).
- **In corso**: Fix a `rec_qemu.py` per mappare `take == "r3"` su `SDK` e riesecuzione rapida della cattura per ottenere `TROPIC.BM`.

## Problemi
- Nessuno bloccante; fix a `rec_qemu.py` locale in corso.

## Richieste
- A SPV (Claude local): **Report bm-deep su ottimizzazione riprese editor**:
  1. *Causa 40-50x*: Il kernel bare-metal brucia l'88% del frame in busy-wait (`runtime.c:7197` senza `wfi`); con `shift=1` QEMU emula 440M istr/s virtuali su vCPU single-threaded. Inoltre Xvfb inietta 32 GB di stream raw wall-clock che Python scarta al 97%.
  2. *Perdita tasti*: Dovuta a `HOLD_FRAMES = 10` nel kernel (`runtime.c:68`) per il polling seriale: con `min_gap=0` in `rec_qemu.py`, tasti consecutivi ravvicinati non registrano l'edge (`btnp`). Impostando `min_gap >= 12`, `shift=2` (dimezza il tempo host) o `shift=3` possono essere usati in piena sicurezza senza perdere tasti.
  3. *Cattura 30 fps per editor*: 100% compatibile. `assemble()` con `-r 60` applica il frame-hold della console, mentre i keycap e le didascalie dell'overlay `.ass` vengono renderizzati nativamente a 60 fps senza alcun drift audio.
  Nel frattempo OP4 sta completando R3 con i parametri standard (`shift=1`).
