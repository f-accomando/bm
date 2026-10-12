---
name: bm-wsl
description: Esegue build, test e strumenti host di bm in WSL da Windows con un solo comando fisso (scripts/wsl-run.ps1), senza ricostruire ogni volta `wsl -e bash -c "cd /mnt/c/... && ..."`. Usala ogni volta che serve make, un test, build/host/*, python3 su file del repo o uno script temporaneo in WSL; da Windows è la via normale.
---

# bm-wsl: build e test in WSL, sempre nello stesso modo

Il repo si compila e si prova in WSL (Ubuntu, 8 core). Da Windows si usa **solo**
`scripts/wsl-run.ps1`: entra nella cartella del repo, converte i percorsi, tiene le ultime
righe dell'output e restituisce il codice di uscita del comando (non quello di `tail`).

```powershell
scripts/wsl-run.ps1 "make test-audio"                       # ultime 40 righe
scripts/wsl-run.ps1 "make -j`$(nproc) build/kernel.img" -Tail 15
scripts/wsl-run.ps1 "git status --short" -Tail 0            # tutto l'output
scripts/wsl-run.ps1 "make test-riff" -Dir tests             # da una sottocartella
scripts/wsl-run.ps1 -Script C:\percorso\scratchpad\t.sh test-riff   # script temporaneo + argomenti
```

Dalla Bash tool: `powershell -NoProfile -File scripts/wsl-run.ps1 "<comando>"`.

## Regole (da CLAUDE.md)

- Solo il test di ciò che la modifica tocca (`make test-<nome>`, QEMU `tests/qemu_test.py -k <nome>`).
  Mai `make test` intero in sviluppo; GPU solo `make test-b3d`. `kernel7` si ignora.
- Compilare con `-j$(nproc)`; un `make` che ha già costruito non si rilancia a vuoto: prima
  `make -n <target> | head` se non si sa cosa ricostruirebbe.
- Output lungo: `-Tail 15` per i build, `-Tail 40` (default) per i test. Se un test fallisce,
  rilancia solo quel test con `-Tail 120`, non la suite.

## Target usati più spesso

Build: `make -j$(nproc) build/kernel.img`, `make ai-model`, `make market-seed MARKET=<clone>`.
Test: `test-audio`, `test-sound`, `test-riff`, `test-impostor`, `test-b3d`, `test-bm`,
`test-gameapi`, `test-studio`, `test-sdk-b16`, `test-bmnet`, `test-qpu`, `test-host`.
Strumenti host: `build/host/luahost <script.lua> …`, `build/host/bmhost-bin`.
L'elenco completo: `Grep -n '^test-' Makefile`.

## Script temporanei

Gli script di prova stanno nella cartella scratchpad della sessione e si lanciano con
`-Script <percorso Windows>` (il wrapper converte il percorso con `wslpath`). Non scrivere
`wsl bash /mnt/c/Users/...` a mano.

## Se qualcosa non va

- `wslpath failed`: WSL non installato o distro spenta; `wsl -l -v`.
- Percorsi con spazi: sono già tra apici nel wrapper; nel comando usa apici doppi interni.
- Variabili della shell nel comando PowerShell: proteggi il `$` con un backtick (`` `$(nproc) ``) o
  usa apici singoli attorno a tutto il comando.
