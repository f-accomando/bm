---
name: bm-ci-triage
description: Diagnose a red CI run by reproducing every failing target locally (make -k in WSL) and classifying each failure. Use when CI is red, when each push reveals one new failure, or before opening an integration PR.
---

# bm CI triage: all failures at once

The CI host job stops at the first failing target, so each push reveals one failure and costs 9-12 minutes. Reproduce all of them locally in one pass, classify them, then fix each in its own perimeter.

## 1. Read the runs first (no push)
- `gh run list --branch <branch> --limit 10` and `gh run view <id> --log-failed` for the last completed run; note queued and cancelled runs apart from the failures.
- A job that fails early hides the targets after it: `qemu` 1-4 and `rgb30` can be green on one commit and red on another; check each on the commit under test.

## 2. Reproduce locally (WSL, worktree of the commit under test)
```bash
wsl bash -lc 'cd /mnt/c/Users/Utente/Documents/GitHub/<worktree> && export TMPDIR=/mnt/c/Users/Utente/Documents/GitHub/<tmpdir> && make -j4 && timeout 1500 make -k -j2 test-host 2>&1 | tee host.log; grep -E "make: \*\*\*|^FAIL" host.log'
```
- `make -k` runs every target even after a failure; `-j2` or `-j3` keeps the disk of `/mnt/c` from distorting timings.
- Set `TMPDIR` on disk: `/tmp` in WSL is a 3.9 GB tmpfs that other operators' `qemu_test.py` runs can fill ("No space left on device" then is the environment).
- Checkout with `core.autocrlf=false` (or a Linux checkout): with CRLF, `test-qpu` (`cmp build/shaders.h src/gpu/shaders.h`) and `test-nano8` fail only locally.
- A target killed by the 25-minute timeout is a speed problem; list the targets after it as not confirmed and run them alone.

## 3. Classify every failure (one row each)
| Class | Meaning | Action |
|---|---|---|
| a | the test is older than the data or format it checks | update the test, one agent per failing target |
| b | a real regression in code | fix in code; name the commit that introduced it (`git bisect` or `git log -S`) |
| c | only the local environment (CRLF, disk, load, timeout) | no fix; write it in the table so nobody chases it again |
| d | pre-existing on `bm-core` | say so, and say whether the branch fixes it |

## 4. The answer: a table
`target | cause | class | minimal fix | pre-existing on bm-core | command to reproduce`, plus the targets not confirmed and the caveats (what ran partially). Keep to facts the run showed.

## 5. After the table
- One agent per failure with disjoint perimeters; fixes only on the repair branch (`claude/ci-repair`); each verified locally (`make <target>`) before the push.
- Rebase or merge the repair branch on current `bm-core` and rerun `make -k test-host` before trusting it: new tests on `bm-core` can turn red.
- Merge the repair branch into the integration only with CI green.
- Ways to cut the cycle: `make -k test-host` in the workflow with a closing summary of the red targets, the host job split in 2-3 jobs with `HOST_SKIP`, `concurrency: cancel-in-progress` on work branches, a cache for apt and ccache.
- Cancelling queued runs (`gh run cancel`) needs the user's consent.
