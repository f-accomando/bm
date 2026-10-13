---
description: Add animation clips from JSON to the rig of a bm models file
argument-hint: CLIP.json [...] --in FILE -o OUT [--rig MODEL] [--replace]
allowed-tools: Bash(python:*), Bash(python3:*), Read
disable-model-invocation: true
---
Run `python .claude/skills/bm-forge/scripts/bm_forge.py anim $ARGUMENTS` from the repo root (`python3` on WSL).
Report the exit code (0 OK, 1 rule broken, 2 usage/I-O, 3 similarity below --min-iou, 4 drift) and the output lines.
Then run `check` on the output and `preview --clip <name>` into the scratchpad directory, and Read the PNG. Do not fix anything unless asked.
