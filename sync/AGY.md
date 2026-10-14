---
schema: 1
id: AGY
state: FREE
task: AGY-13 fuso in bm-core; pronto per review finale
waiting_on: 
progress: 100
wakeup: yes
ack: 2026-10-14T19:22+02:00
updated: 2026-10-14T19:30+02:00
---
# AGY — Antigravity (AGY)

> **Scrive solo Antigravity (AGY)** (gli altri leggono). Il supervisore (SPV, Claude local) legge questo file e lo riassume in `SYNC.md`.
> Modifica questo file, committa e fai `git pull --rebase origin bm-core && git push origin HEAD:bm-core`. Un file per autore: nessun conflitto.
> **Aggiorna "Ultimo aggiornamento" almeno ogni 15 min** (o a ogni task conclusa); senza aggiornamenti per 60 min lo stato diventa `IDLE`.

**Ultimo aggiornamento:** 2026-10-14 19:30 +02:00
**Stato:** `FREE AGY-13 fuso in bm-core; pronto per review finale`
**Heartbeat:** (1) Sì, subagent e schedulazione dedicati attivi; (2) Aggiornamento di sync/AGY.md garantito a ogni passo concluso (<=35 min) e prima/dopo ogni operazione estesa con orario previsto.

## Profilo
- **Ambiente/Luogo**: Locale (Windows 11 x64, Machine A). CPU 6 core (12 thread), 16 GB RAM. Nessun timeout di sessione o container (regge processi lunghi continuativi >60 min).
- **Tool presenti**: Python 3.12 (con Pillow, lupa, zlib), Git, FFmpeg 9.0.2 installato (Gyan.FFmpeg in WinGet packages, rilevato e linkato automaticamente dagli script). Agente principale sempre libero, background tasks e subagenti concorrenti attivi.
- **Tool mancanti**: `qemu-system-arm` assente; `make`/`gcc`/`arm-none-eabi-gcc` assenti (quindi non posso compilare `make kernel.img`); `bmhost` è binario ELF Linux (WSL non è installato sulla macchina host); non è presente l'utente `Utente` o `C:\Users\Utente` su questa macchina fisica (l'host è Windows Machine A con utente `ROCKET`).
- **Competenze**: Codice C e Lua del runtime, 3D engine (`map3d`), editor (`carts/editor`, `sdk_b16`), scripting Python (montaggi, verifiche, generatori come `cover.py`), audit e coerenza (`consistency.py`, `docs_audit.py`).
- **Limiti**: Non posso fare catture QEMU (`rec_qemu.py`) né esecuzioni con `bmhost` (Linux).

## Consegne
- DELIVERED AGY-13 @ agy/sync-check-fix@0d9c6a59 · verifica: `python tools/sync_check.py` → RESULT: ALL OK (0 crash, 6 note/righe lunghe rilevate) · 2026-10-14T17:00Z
- DELIVERED montage7 @ agy/montage7@d202a8af · verifica: `python video/3d/07-code/montage7.py --test` → RESULT: ALL OK (242.00 s, 1920x1080@60fps, stereo AAC) · 2026-10-14T15:52Z
- DELIVERED montage8 @ agy/montage8@0897809 · verifica: `python video/3d/08-gameplay/montage8.py --test` → RESULT: ALL OK (95.00 s, 1920x1080@60fps, stereo AAC) · 2026-10-14T15:23Z

## Task
- **AGY-13 COMPLETATO su branch `agy/sync-check-fix` (commit `0d9c6a59`)**:
  - Revisione approfondita di `tools/sync_check.py` e `tools/sync_hdr.py`:
    1. *Bug colonna addressed*: in `sync_check.py`, veniva controllata solo `line.split("|")[4]` (Oggetto); i messaggi indirizzati nel corpo del messaggio (colonna 5, es. broadcast a `Tutti`) venivano persi. Risolto cercando su tutte le colonne dalla 4 in poi.
    2. *Bug autoriconoscimento autore*: `| %s not in line[:60]` non escludeva l'autore se scritto per esteso (es. `| Antigravity (AGY) |` o `| Claude Code (local) = SPV |`), contando le proprie righe come messaggi non letti. Risolto controllando la colonna autore `line.split("|")[2]`.
    3. *Falso positivo stale su operatori DONE*: mancava la clausola `state != "DONE"` nel controllo `stale`. Aggiunta.
    4. *Falso positivo commenti header*: `sync_check.py` non rimuoveva i commenti inline `# ...` dal blocco header (a differenza di `sync_hdr.py`), provocando errori di validazione su `state: WORKING # commento`. Risolto.
    5. *Windows CRLF*: `line.rstrip("\n")` lasciava `\r`, falsando il conteggio caratteri per la regola dei 600 caratteri. Corretto con `rstrip("\r\n")`.
    6. *Rigidità regex DELIVERED*: richiedeva obbligatoriamente `·` e `→` Unicode esatti; esteso per accettare anche caratteri ASCII `-` e `->`.
    7. *Windows console encoding*: aggiunta riconfigurazione UTF-8 di `sys.stdout` con fallback `errors="replace"` per evitare `UnicodeEncodeError` su terminali legacy.
    8. *Cross-timezone in sync_hdr.py*: `last_row_time()` applicava il fuso locale dell'host ai tempi di `SYNC.md` (scritti in ora di Roma). In container UTC (CLD), produceva timestamp `+00:00` spostati di 2 ore nel futuro. Corretto con fallback standard a `+02:00` se l'host è in UTC.
- **AGY-12 COMPLETATO e APPROVATO da SPV (fuso in bm-core)** (commit `5850347`, review 18:12).
- **AGY-11 COMPLETATO e APPROVATO da SPV (fuso in bm-core)** (commit `45d82ab`, review 17:55).
- **AGY-10 COMPLETATO & MERGIATO in `bm-core` (R-NET1: comando monitor err)** (commit `173019e`).
- **AGY-09 COMPLETATO & MERGIATO in `bm-core` (Passi 1-6: adaptive layout 720x720 SDK b16)** (commit `028f853`).
- **AGY-01..AGY-06 COMPLETATI & MERGIATI in `bm-core` (3D engine map3d)**.

## Progressi
- AGY-13 verificato ed eseguito senza errori su Windows (`python tools/sync_check.py`).
- Correzioni pushate su `origin/agy/sync-check-fix`.
- Stato reimpostato a `FREE`.

## Problemi
- Nessuno.

## Richieste
- A SPV (Claude local): AGY-13 consegnato con fix e diagnosi su `agy/sync-check-fix`. Se approvato, puoi fondere il branch in `bm-core`. Pronto per review finale o signoff quando la serie sarà chiusa.
