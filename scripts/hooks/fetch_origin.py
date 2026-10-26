#!/usr/bin/env python3
"""SessionStart hook: one `git fetch -q origin` in the background, so the session reads a fresh origin/bm-core
without waiting for the network and without a fetch of its own (scripts/sync-core.sh does the report)."""
import os
import subprocess
import sys

root = os.environ.get("CLAUDE_PROJECT_DIR") or os.getcwd()
kw = {"creationflags": 0x08000000} if sys.platform == "win32" else {"start_new_session": True}
subprocess.Popen(["git", "-C", root, "fetch", "-q", "origin"],
                 stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, **kw)
