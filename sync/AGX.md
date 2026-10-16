---
schema: 1
id: AGX
state: WAITING
task: handshake: attesa requisiti WSL/toolchain/QEMU
waiting_on: utente
wakeup: no
ack: 2026-10-16T11:56+02:00
updated: 2026-10-16T12:38+02:00
---
# AGX — Antigravity (AGX)

> **Scrive solo AGX** (gli altri leggono). Il supervisore (SPV) legge questo file e aggiorna la tabella §1b di `SYNC.md`.
> Aggiornare il blocco con `python tools/sync_hdr.py AGX --state ... --task ... [--waiting-on] [--progress] [--eta]`, poi commit e
> `git pull --rebase origin sync/protocollo-ottobre && git push origin HEAD:sync/protocollo-ottobre`. Almeno ogni 15 min e a ogni task conclusa.

## Profilo
- Dove gira: Google Antigravity IDE (sessione locale Windows 11 su host `ROCKET`, PowerShell). Worktree dedicati: `bm33-agx-sync` (`C:\Users\ROCKET\Documents\GitHub\bm33-agx-sync`) per `sync/protocollo-ottobre` e `bm33-agx` (`C:\Users\ROCKET\Documents\GitHub\bm33-agx`) per sviluppo. Punti di rottura: chiusura sessione / timeout contesto / rate limit.
- Si risveglia da solo: NO (`wakeup no`); lavora a turni con tool esecutivi e subagent (Gemini Pro/Flash/Argon), dichiara `eta` nel blocco di stato.
- Modelli: Gemini 3.8 Flash (coordinamento ed esecuzione); subagent nativi / routing secondo AGENTS.md (`pro`, `flash`, `flash_lite`, `research`).
- Competenze: programmazione C bare-metal (kernel, driver, fat32), Lua, Python, Makefile. Limiti: nessun display/input hardware diretto su console; Pi 192.168.1.208 raggiungibile su rete locale (kernel precedente; monitor di rete via `tools/bm_net.py`).
- Strumenti: Python 3.12.10 (Windows), GNU Make 4.4.1 (ezwinports, Windows), git 2.53.0.windows.4 con push verificato su origin. QEMU arm/aarch64 e toolchain cross ARM non disponibili in ambiente utente Windows (installazione bloccata da privilegi UAC); WSL non installato.

## Requisiti
- `python` (3.x) nel PATH: SÌ (`Python 3.12.10` Windows).
- `git` con push su `origin`: SÌ (`git version 2.53.0.windows.4`, push dry-run su `origin/sync/protocollo-ottobre` con esito positivo `Everything up-to-date`).
- `make` nel PATH: SÌ (`GNU Make 4.4.1` installato via winget).
- `qemu-system-arm --version` e `qemu-system-aarch64 --version`: NO (mancanti in locale Windows; l'installer winget richiede elevazione amministratore/UAC interattiva).
- `arm-none-eabi-gcc --version` e `aarch64-linux-gnu-gcc --version`: NO (mancanti in locale Windows; l'installer toolchain ARM richiede elevazione amministratore/UAC).
- WSL con toolchain linux (`scripts/wsl-run.ps1`): NO (sottosistema Linux non installato; `wsl --install` richiede privilegi di amministratore e riavvio del sistema).
- Nessun Pi/RGB30 raggiungibile dagli operatori: PARZIALE (Pi all'IP `192.168.1.208` raggiungibile via ping e `tools/bm_net.py` con kernel vecchio; nessun dispositivo fisico accessibile per controlli visivi o tasti, prove hardware delegate a UTE).

## Task
- Handshake iniziale con supervisore SPV su `sync/protocollo-ottobre`.
- Letti ordini in arrivo in `sync/inbox/AGX.md` (WO AGX-1 e AGX-2). Come da protocollo ("Se un requisito resta mancante, scrivilo nel file e non iniziare WO. Poi pusha e attendi l'assegnazione"), il WO AGX-1 (`make test-sdk-b16`) non viene iniziato in attesa della disponibilità dell'ambiente di compilazione host C/WSL.

## Consegne

## Progressi
- Eseguito handshake e configurato worktree `bm33-agx-sync` su `sync/protocollo-ottobre`.
- Installati e configurati nel PATH utente `git` (2.53.0) e `make` (4.4.1).
- Verificato accesso di rete locale a Raspberry Pi (`192.168.1.208`).
- Letta la inbox `sync/inbox/AGX.md` con assegnazione WO AGX-1 e AGX-2.
- Rilevata impossibilità di completare in autonomia l'installazione di QEMU, Toolchain ARM e WSL per mancanza di privilegi amministrativi (richiesta elevazione UAC).

## Problemi
- Mancano `qemu-system-arm`, `qemu-system-aarch64`, `arm-none-eabi-gcc`, `aarch64-linux-gnu-gcc` e WSL.
- La suite di test host di AGX-1 (`make test-sdk-b16`) richiede un compilatore C host (`HOSTCC`) per generare `build/host/luahost`, attualmente assente su Windows e WSL non installato.

## Richieste
- A UTE / SPV: installazione di WSL (`wsl --install -d Ubuntu` da PowerShell Amministratore con successivo riavvio) oppure concessione/installazione manuale delle toolchain per consentire l'esecuzione delle build e delle verifiche di test.
