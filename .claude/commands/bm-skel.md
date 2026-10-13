---
description: Bind a skeleton description to a model of a bm models file
argument-hint: SKELETON.json --in FILE -o OUT [--strategy nearest_bone_part|nearest_bone]
allowed-tools: Bash(python:*), Bash(python3:*), Read
disable-model-invocation: true
---
Run `python .claude/skills/bm-forge/scripts/bm_forge.py skel $ARGUMENTS` from the repo root (`python3` on WSL).
Report the exit code (0 OK, 1 rule broken, 2 usage/I-O, 3 similarity below --min-iou, 4 drift) and the output lines.
Do not fix anything unless asked.
