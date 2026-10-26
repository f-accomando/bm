# Local checklist — branch `pdf-manuals`

Checkpoint to pull the whole branch locally. Kernels were NOT built in the
cloud container (no ARM toolchain): RGB30/Pi C changes were only host
syntax-checked.

## Get it
    git fetch origin pdf-manuals bm-core
    git checkout pdf-manuals
    git merge origin/bm-core      # bm-core has 2 commits not in the branch

## Build
    make                          # Pi kernel
    make TARGET=rgb30             # RGB30 kernel

## Checks (selective)
    make test-consistency test-shot test-reserved test-spec test-bm
    python3 scripts/manuals.py check
    python3 scripts/docs_audit.py score

## Try on hardware (`./easy_install.sh`)
- RGB30: D01, D02, D06, D07, D08, D13, D16 (see `python3 scripts/consistency.py report`)
- Screenshot: Print Screen (host) / Select held 3 s (RGB30) -> `bm/shots/`
- Pi: a 720x720 `.bm` (boxed on 1280x720)

## Not done on purpose
No PR, no tag (`v0.3.0` baseline awaits a yes). D03/D05 suspended
(on-screen keyboard / padtyping in progress).
