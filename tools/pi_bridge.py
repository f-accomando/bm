#!/usr/bin/env python3
"""A bridge to the network console of a Pi: the owner types the password once, then a local client (tools/pi.py) can send a few
fixed operations without ever seeing the password.

  python3 tools/pi_bridge.py 192.168.1.108          # asks for the password, then waits (Ctrl-C to stop)
  python3 tools/pi_bridge.py --detach 192.168.1.108  # the same, then in the background (the shell may be closed)
  python3 tools/pi.py line gpu                        # the client (another shell, or Claude)
  python3 tools/pi.py status                          # is the Pi up, which kernel, how did the run before end

Operations: status, logs NAME (bootprev, bootlog, lastrun: saved in out/pi_*.TXT and shown), keys NAME (monitor, game-exit: Ctrl-backslash ends the game, crash-*, freeze, reboot: the last ones only when the monitor has the keys), line TEXT (monitor commands separated by ';', only gpu, b3d tests=.. profiles=.., render, room, log, err, send, reboot; set/save/play and anything else refused), send FILE, play FILE, kernel FILE, shot OUT.png, config KEY=VALUE.. (words of letters, digits and _-. only).
One bridge per device: --dev NAME gives the socket ~/.pi_bridge.NAME.sock and the files out/pi_NAME_*.TXT.
FILE must be inside the repo or in ~/bmk*/ (the build folders); OUT of shot is a .png in a folder there. The socket is ~/.pi_bridge.sock, mode 0600."""
import datetime
import getpass
import os
import re
import signal
import socket
import string
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
SOCK = os.path.expanduser("~/.pi_bridge.sock")
DEV = ""            # --dev NAME: one bridge per device
sys.path.insert(0, HERE)
import bm_net  # noqa: E402  (status asks the greeting of the console)
# keys the Pi can be told to press, by name (the monitor; the voluntary crashes: X then a/u/b). No free keys.
KEYS = {"monitor": "q", "crash-dabt": "Xa", "crash-undef": "Xu", "crash-prefetch": "Xb", "freeze": "Xf", "reboot": "r"}
# these keys mean something else in the menu (X opens the options, r is an action): only when the monitor has them
MONITOR_ONLY = {"crash-dabt", "crash-undef", "crash-prefetch", "freeze", "reboot"}
# the logs of the SD card the Pi writes at a crash or a boot (netxfer.c G allows these three names only)
LOGS = {"bootprev": "BOOTPREV.TXT", "bootlog": "BOOTLOG.TXT", "lastrun": "LASTRUN.TXT"}
# the monitor commands the line operation passes (src/kernel/monitor.c:291 lists them all; set, save and play are left out:
# set writes any config key, wifi_psk and net_password included; play is the bridge's play FILE)
LINE_CMDS = ("gpu", "b3d", "render", "room", "log", "err", "send", "reboot")
# letters, digits, space and _ , = ; only (a control character would end the line early: bm_net.py closes it with a carriage return)
LINE_CHARS = set(string.ascii_letters + string.digits + " _,=;")
# b3d reads tests= and profiles= only (monitor.c:225-232); their names are [A-Za-z0-9_] (src/bm/b3d.c prof_short, test names)
B3D_ARG = re.compile(r"(tests|profiles)=[A-Za-z0-9_,]+")


def check_line(text):
    """(normalised line, None) or (None, why): the commands of LINE_CMDS, ';' between them, b3d with tests=/profiles= only."""
    if not text or len(text) >= 200:
        return None, "empty or longer than 199 characters"
    for c in text:
        if c not in LINE_CHARS:
            return None, "character %r not allowed" % c
    segments = []
    for seg in text.split(";"):
        words = seg.split()
        if not words:
            return None, "empty command between ';'"
        if words[0] not in LINE_CMDS:
            return None, "'%s' is not allowed (allowed: gpu, b3d, render, room, log, err, send, reboot)" % words[0]
        if words[0] != "b3d" and len(words) > 1:
            return None, "'%s' takes no arguments" % words[0]
        for w in words[1:]:
            if not B3D_ARG.fullmatch(w):
                return None, "b3d: '%s' is not tests=... or profiles=..." % w
        segments.append(" ".join(words))
    return "; ".join(segments), None


ALLOWED_DIRS = [ROOT] + [os.path.expanduser("~/" + d) for d in os.listdir(os.path.expanduser("~")) if d.startswith("bmk")]


def allowed(path):
    p = os.path.realpath(path)
    return os.path.isfile(p) and any(p.startswith(os.path.realpath(d) + os.sep) for d in ALLOWED_DIRS)


def allowed_dir(path):
    p = os.path.realpath(path)
    return os.path.isdir(p) and any(p == os.path.realpath(d) or p.startswith(os.path.realpath(d) + os.sep) for d in ALLOWED_DIRS)


def status(host, pw):
    """up or down (the greeting of the console), the version, the state of the run before (BOOTPREV.TXT)"""
    v = bm_net.console_version(host)
    if v is None:
        return "status: down - no answer from %s:%d (off, restarting, frozen, or another client connected)" % (host, bm_net.PORT)
    out_file = os.path.join(ROOT, "out", "pi_" + (DEV + "_" if DEV else "") + "status_BOOTPREV.TXT")
    os.makedirs(os.path.dirname(out_file), exist_ok=True)
    cmd = [sys.executable, os.path.join(HERE, "bm_net.py"), host, "--get", "bm/BOOTPREV.TXT", out_file]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=300, env=dict(os.environ, BM_PASSWORD=pw))
    prev, first = None, (r.stdout + r.stderr).strip().split(chr(10))[0]
    if r.returncode == 0 and os.path.isfile(out_file):
        with open(out_file, encoding="utf-8", errors="replace") as f:
            body = f.read().replace(pw, "***")
        with open(out_file, "w", encoding="utf-8") as f:
            f.write(body)
        found = re.findall(r"--- the run before (.*?); the last lines", body)
        prev = found[-1] if found else None
    lines = ["status: up - bm %s at %s" % (v, host),
             "previous run: %s" % (prev if prev else "unknown" + (" (" + first + ")" if first else "")),
             "checked: " + datetime.datetime.now().isoformat(timespec="seconds")]
    return chr(10).join(lines).replace(pw, "***")


def is_secret_key(word):
    k = word.partition("=")[0].lower()
    return k == "net_password" or bool(bm_net.SECRET.match(k))


def masked(req):
    """The request as it is logged: a config value is never written"""
    op, _, arg = req.partition(" ")
    if op != "config":
        return req
    return "config " + " ".join(w.partition("=")[0] + "=***" if "=" in w else w for w in arg.split())


def run(host, pw, req):
    op, _, arg = req.partition(" ")
    base = [sys.executable, os.path.join(HERE, "bm_net.py"), host]
    if req == "status":
        return status(host, pw)
    if op == "config" and any(is_secret_key(w) for w in arg.split()):
        return "config: secret keys are not set through the bridge"
    if op == "line":
        line, why = check_line(arg)
        if why:
            return "refused: line: " + why
        cmd = base + ["--line", line]
    elif op == "config" and arg and len(arg) < 200 and all(c.isalnum() or c in " _-.=" for c in arg) \
            and all(w.count("=") == 1 and not w.startswith("=") for w in arg.split()):
        cmd = base + ["--config"] + arg.split()
    elif op in ("send", "play", "kernel") and allowed(arg):
        cmd = base + ["--" + op, os.path.realpath(arg)]
    elif op == "shot" and arg.lower().endswith(".png") and allowed_dir(os.path.dirname(arg) or "."):
        cmd = base + ["--shot", os.path.realpath(arg)]
    elif op == "keys" and arg == "game-exit":
        cmd = base + ["--game-exit"]
    elif op == "keys" and arg in KEYS:
        cmd = base + (["--in-monitor"] if arg in MONITOR_ONLY else []) + ["--keys", KEYS[arg]]
    elif op == "logs" and arg in LOGS:
        out_file = os.path.join(ROOT, "out", "pi_" + (DEV + "_" if DEV else "") + LOGS[arg])
        os.makedirs(os.path.dirname(out_file), exist_ok=True)
        cmd = base + ["--get", "bm/" + LOGS[arg], out_file]
    else:
        return "refused: %r" % masked(req)
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=300, env=dict(os.environ, BM_PASSWORD=pw))
    except subprocess.TimeoutExpired:
        return "timeout: no end after 300 s (the Pi may be frozen or restarting: python3 tools/pi.py status)"
    text = r.stdout + r.stderr
    if op == "logs" and r.returncode == 0 and os.path.isfile(out_file):
        with open(out_file, encoding="utf-8", errors="replace") as f:
            body = f.read().replace(pw, "***")
        with open(out_file, "w", encoding="utf-8") as f:    # what is kept on the PC is masked too
            f.write(body)
        text += chr(10) + body
    return text.replace(pw, "***")


def claim_socket(path):
    """The listening socket, or an exit if a bridge already listens there (a stale file is removed)"""
    if os.path.exists(path):
        probe = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        probe.settimeout(1)
        try:
            probe.connect(path)
        except (ConnectionRefusedError, FileNotFoundError):
            os.unlink(path)
        except OSError:
            pass
        else:
            probe.close()
            sys.exit("a bridge is already running on %s (stop it: pkill -INT -f pi_bridge.py)" % path)
        finally:
            probe.close()
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.bind(path)
    os.chmod(path, 0o600)
    s.listen(1)
    return s


def detach():
    """Into the background: the shell may be closed. The password is already in this process's memory only."""
    pid = os.fork()
    if pid:
        print("bridge started in the background (pid %d), log out/pi_bridge.log; stop: pkill -INT -f pi_bridge.py" % pid)
        os._exit(0)
    os.setsid()
    signal.signal(signal.SIGHUP, signal.SIG_IGN)
    os.makedirs(os.path.join(ROOT, "out"), exist_ok=True)
    log = os.open(os.path.join(ROOT, "out", "pi_bridge.log"), os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    null = os.open(os.devnull, os.O_RDONLY)
    os.dup2(null, 0)
    os.dup2(log, 1)
    os.dup2(log, 2)


def main():
    global SOCK, DEV
    args = sys.argv[1:]
    if "--dev" in args:
        i = args.index("--dev")
        DEV = args[i + 1] if i + 1 < len(args) else ""
        if not re.fullmatch(r"[a-z0-9]{1,12}", DEV):
            sys.exit("--dev needs a name of 1-12 letters or digits, for example pizero, pi1, rgb30")
        del args[i:i + 2]
        SOCK = os.path.expanduser("~/.pi_bridge.%s.sock" % DEV)
    background = "--detach" in args
    names = [a for a in args if not a.startswith("--")]
    host = names[0] if names else "192.168.1.108"
    s = claim_socket(SOCK)
    pw = os.environ.get("BM_PASSWORD") or getpass.getpass("console password of %s (stays in this process): " % host)
    signal.signal(signal.SIGTERM, signal.default_int_handler)   # the socket is removed below
    if background:
        detach()
    print("bridge ready on", SOCK, "- Ctrl-C to stop", flush=True)
    try:
        while True:
            c, _ = s.accept()
            req = c.makefile().readline().strip()
            print("request:", masked(req), flush=True)
            try:
                out = run(host, pw, req)
            except Exception as e:
                out = "error: %s" % e
            c.sendall(out.encode("utf-8", "replace"))
            c.close()
    finally:
        s.close()
        if os.path.exists(SOCK):
            os.unlink(SOCK)


if __name__ == "__main__":
    main()
