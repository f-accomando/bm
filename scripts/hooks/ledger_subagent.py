#!/usr/bin/env python3
"""Hook SubagentStop: append a measured ledger row (tokens, tools, secs) to
.claude/state/ledger_pending.txt (untracked). SPV then fixes area/outcome and moves
the row to docs/agents/ledger.txt with scripts/ledger_add.py. One row per agent id."""
import glob, json, os, sys
from datetime import datetime

root = os.environ.get("CLAUDE_PROJECT_DIR") or os.getcwd()
try:
    inp = json.load(sys.stdin)
except ValueError:
    sys.exit(0)

path = inp.get("agent_transcript_path")
aid = inp.get("agent_id") or ""
if not path or not os.path.exists(path):
    base = os.path.splitext(inp.get("transcript_path") or "")[0]
    hit = glob.glob(os.path.join(base, "subagents", "agent-%s.jsonl" % aid)) if aid else []
    path = hit[0] if hit else None
if not path:
    sys.exit(0)

rows = []
with open(path, encoding="utf-8") as f:
    for line in f:
        try:
            rows.append(json.loads(line))
        except ValueError:
            pass
asst = [r for r in rows if r.get("type") == "assistant" and isinstance(r.get("message"), dict)]
if not asst:
    sys.exit(0)
u = next((r["message"]["usage"] for r in reversed(asst) if r["message"].get("usage")), {})
tokens = sum(u.get(k, 0) or 0 for k in ("input_tokens", "cache_read_input_tokens", "cache_creation_input_tokens", "output_tokens"))
tools = sum(1 for r in asst for c in r["message"].get("content", []) if isinstance(c, dict) and c.get("type") == "tool_use")
model = (asst[0]["message"].get("model") or "?").replace("claude-", "").split("-")[0]
try:
    t0 = datetime.fromisoformat(rows[0]["timestamp"].replace("Z", "+00:00"))
    t1 = datetime.fromisoformat(rows[-1]["timestamp"].replace("Z", "+00:00"))
    secs = int((t1 - t0).total_seconds())
except (KeyError, ValueError):
    secs = 0
task = "?"
meta = os.path.splitext(path)[0] + ".meta.json"
try:
    task = json.load(open(meta, encoding="utf-8")).get("description") or task
except (OSError, ValueError):
    pass
atype = inp.get("agent_type") or "?"
date = (rows[0].get("timestamp") or "")[:10] or datetime.now().strftime("%Y-%m-%d")
row = "|".join([date, atype, model, task.replace("|", "/")[:80], "auto", "-", "-", "AUTO", str(tokens), str(tools), str(secs)])

state = os.path.join(root, ".claude", "state")
os.makedirs(state, exist_ok=True)
out = os.path.join(state, "ledger_pending.txt")
seen = open(out, encoding="utf-8").read() if os.path.exists(out) else ""
if row not in seen:
    with open(out, "a", encoding="utf-8") as f:
        f.write(row + "\n")
