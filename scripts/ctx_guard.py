#!/usr/bin/env python3
"""Hook UserPromptSubmit: avvisa quando il contesto supera una soglia e
chiede a Claude di chiudere la risposta con un `/compact ...` pronto.

Env: CTX_GUARD_WINDOW (token della finestra, default 200000),
     CTX_GUARD_PCT (soglia %, default 30), CTX_GUARD_STEP (riavviso ogni +N %, default 10).
Stato in .claude/state/ctx_guard_<session>.json (ignorato da git).
"""
import json, os, sys

WINDOW = int(os.environ.get("CTX_GUARD_WINDOW", "200000"))
PCT = float(os.environ.get("CTX_GUARD_PCT", "30"))
STEP = float(os.environ.get("CTX_GUARD_STEP", "10"))

MSG = (
    "[ctx_guard] Contesto al {pct:.0f}% ({tok}k/{win}k token), oltre la soglia {thr:.0f}%. "
    "Dopo aver risposto, chiudi con UN solo blocco di codice contenente il comando da incollare: "
    "`/compact tieni: <obiettivo>; <decisioni prese e perché>; <file toccati e branch>; "
    "<stato docs/progress/ da aggiornare>; <prossimo passo>. scarta: output di comandi e letture di file già usate`. "
    "Compila ogni campo dal contesto reale, max 12 righe, nessun altro testo sul compact."
)


def context_tokens(path):
    """Token dell'ultimo messaggio assistant (input + cache), 0 se non trovati."""
    last = 0
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                try:
                    e = json.loads(line)
                except ValueError:
                    continue
                u = (e.get("message") or {}).get("usage") if isinstance(e.get("message"), dict) else None
                if u and not e.get("isSidechain"):
                    last = (u.get("input_tokens", 0) + u.get("cache_read_input_tokens", 0)
                            + u.get("cache_creation_input_tokens", 0)) or last
    except OSError:
        pass
    return last


def main():
    try:
        d = json.load(sys.stdin)
    except ValueError:
        return
    tok = context_tokens(d.get("transcript_path", ""))
    pct = 100.0 * tok / WINDOW
    sdir = os.path.join(d.get("cwd") or ".", ".claude", "state")
    sfile = os.path.join(sdir, "ctx_guard_%s.json" % d.get("session_id", "x"))
    try:
        with open(sfile) as f:
            seen = json.load(f).get("pct", 0)
    except (OSError, ValueError):
        seen = 0
    if pct < seen - 5:  # contesto sceso: compact avvenuto, riarma
        seen = 0
    if pct < PCT or (seen and pct < seen + STEP):
        if pct < PCT and seen:
            _save(sdir, sfile, 0)
        return
    _save(sdir, sfile, pct)
    print(json.dumps({"hookSpecificOutput": {
        "hookEventName": "UserPromptSubmit",
        "additionalContext": MSG.format(pct=pct, tok=tok // 1000, win=WINDOW // 1000, thr=PCT)}}))


def _save(sdir, sfile, pct):
    try:
        os.makedirs(sdir, exist_ok=True)
        with open(sfile, "w") as f:
            json.dump({"pct": pct}, f)
    except OSError:
        pass


if __name__ == "__main__":
    main()
