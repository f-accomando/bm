---
description: Validate bm files (.bm .b16 .bme .bmm ...), report limits, budget, drift and optional silhouette similarity
argument-hint: FILE [...] [--budget N] [--kernel-limits] [--reference REF] [--no-sync] [--json]
allowed-tools: Bash(python:*), Bash(python3:*), Read
disable-model-invocation: true
---
Run `python .claude/skills/bm-forge/scripts/bm_forge.py check $ARGUMENTS` from the repo root (`python3` on WSL).
Report the exit code (0 OK, 1 rule broken, 2 usage/I-O, 3 similarity below --min-iou, 4 drift) and the output lines.
Do not fix anything unless asked.
