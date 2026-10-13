---
description: Build a bm MESH models file (.bm or .bmm) from box JSON or an untextured .glb
argument-hint: INPUT.json|.glb [...] -o OUT.bm|OUT.bmm [--max-tris N] [--kernel-limits]
allowed-tools: Bash(python:*), Bash(python3:*), Read
disable-model-invocation: true
---
Run `python .claude/skills/bm-forge/scripts/bm_forge.py mesh $ARGUMENTS` from the repo root (`python3` on WSL).
Report the exit code (0 OK, 1 rule broken, 2 usage/I-O, 3 similarity below --min-iou, 4 drift) and the output lines.
Then run `check` on the output and `preview` into the scratchpad directory, and Read the PNG. Do not fix anything unless asked.
