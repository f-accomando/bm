---
name: bm-wsl
description: Runs bm builds, tests and host tools in WSL from Windows with a single fixed command (scripts/wsl-run.ps1), without rebuilding `wsl -e bash -c "cd /mnt/c/... && ..."` every time. Use it every time you need make, a test, build/host/*, python3 on repo files or a temporary script in WSL; from Windows it is the normal way.
---

# bm-wsl: build and test in WSL, always the same way

The repo is built and tested in WSL (Ubuntu, 8 cores). From Windows use **only**
`scripts/wsl-run.ps1`: it enters the repo folder, converts the paths, keeps the last
lines of output and returns the exit code of the command (not that of `tail`).

```powershell
scripts/wsl-run.ps1 "make test-audio"                       # ultime 40 righe
scripts/wsl-run.ps1 "make -j`$(nproc) build/kernel.img" -Tail 15
scripts/wsl-run.ps1 "git status --short" -Tail 0            # tutto l'output
scripts/wsl-run.ps1 "make test-riff" -Dir tests             # da una sottocartella
scripts/wsl-run.ps1 -Script C:\percorso\scratchpad\t.sh test-riff   # script temporaneo + argomenti
```

From the Bash tool: `powershell -NoProfile -File scripts/wsl-run.ps1 "<comando>"`.

## Rules (from CLAUDE.md)

- Test only what the change touches (`make test-<nome>`, QEMU `tests/qemu_test.py -k <nome>`).
  Never the whole `make test` during development; GPU only `make test-b3d`. `kernel7` is ignored.
- Build with `-j$(nproc)`; a `make` that has already built is not rerun idly: first
  `make -n <target> | head` if you do not know what it would rebuild.
- Long output: `-Tail 15` for builds, `-Tail 40` (default) for tests. If a test fails,
  rerun only that test with `-Tail 120`, not the suite.

## Most used targets

Build: `make -j$(nproc) build/kernel.img`, `make ai-model`, `make market-seed MARKET=<clone>`.
Tests: `test-audio`, `test-sound`, `test-riff`, `test-impostor`, `test-b3d`, `test-bm`,
`test-gameapi`, `test-studio`, `test-sdk-b16`, `test-bmnet`, `test-qpu`, `test-host`.
Host tools: `build/host/luahost <script.lua> …`, `build/host/bmhost-bin`.
The full list: `Grep -n '^test-' Makefile`.

## Temporary scripts

Test scripts live in the session scratchpad folder and are launched with
`-Script <Windows path>` (the wrapper converts the path with `wslpath`). Do not write
`wsl bash /mnt/c/Users/...` by hand.

## If something goes wrong

- `wslpath failed`: WSL not installed or distro stopped; `wsl -l -v`.
- Paths with spaces: they are already quoted in the wrapper; in the command use inner double quotes.
- Shell variables in the PowerShell command: protect the `$` with a backtick (`` `$(nproc) ``) or
  use single quotes around the whole command.
