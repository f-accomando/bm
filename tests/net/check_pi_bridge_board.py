#!/usr/bin/env python3
"""tools/pi_bridge.py run() per board, no hardware: subprocess.run is replaced and records the bm_net.py command it would start."""
import os
import shutil
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import pi_bridge  # noqa: E402

fails = 0
started = []


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        fails += 1


def fake_run(cmd, **kw):
    started.append(cmd)
    return types.SimpleNamespace(returncode=0, stdout="done", stderr="")


pi_bridge.subprocess.run = fake_run
work = os.path.join(pi_bridge.ROOT, "out", "board_check")
os.makedirs(work, exist_ok=True)
try:
    for name in ("kernel.img", "kernel7.img", "kernel8.img"):
        with open(os.path.join(work, name), "wb") as f:
            f.write(b"x")

    def ask(board, req):
        del started[:]
        out = pi_bridge.run("127.0.0.1", "123456", req, board)
        return out, bool(started)

    for board, good in (("pi", "kernel.img"), ("zero2", "kernel7.img"), ("rgb30", "kernel8.img")):
        out, ran = ask(board, "kernel " + os.path.join(work, good))
        check(ran and not out.startswith("refused"), "%s accepts %s" % (board, good))
        for bad in sorted({"kernel.img", "kernel7.img", "kernel8.img"} - {good}) if board != "pi" else []:
            out, ran = ask(board, "kernel " + os.path.join(work, bad))
            check(not ran and out.startswith("refused"), "%s refuses %s" % (board, bad))
    out, ran = ask("pi", "kernel " + os.path.join(work, "kernel8.img"))
    check(ran, "pi takes any kernel file name, as before (bm_net answers KA)")
    out, ran = ask("rgb30", "keys reboot")
    check(not ran and "not for this board" in out, "rgb30 answers 'not for this board' to keys reboot")
    out, ran = ask("rgb30", "keys crash-dabt")
    check(not ran and "not for this board" in out, "rgb30 refuses keys crash-dabt")
    out, ran = ask("pi", "keys reboot")
    check(ran, "pi still takes keys reboot")
    out, ran = ask("rgb30", "keys game-exit")
    check(ran, "rgb30 takes keys game-exit")
    out, ran = ask("rgb30", "keys nosuchkey")
    check(not ran and out.startswith("refused"), "rgb30 refuses an unknown key name")
    out, ran = ask("pi", "keys nosuchkey")
    check(not ran and out.startswith("refused"), "pi refuses an unknown key name")
    out, ran = ask("c64", "kernel " + os.path.join(work, "kernel.img"))
    check(not ran and "unknown board" in out, "an unknown board is refused")
    out, ran = ask("rgb30", "config net_password=x")
    check(not ran and "secret" in out, "a secret config key stays refused")
finally:
    shutil.rmtree(work, ignore_errors=True)
print("%d failed" % fails)
sys.exit(1 if fails else 0)
