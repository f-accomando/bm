#!/usr/bin/env python3
"""A bridge to the network console of a Pi: the owner types the password once, then a local client (tools/pi.py) can send a few
fixed operations without ever seeing the password.

  python3 tools/pi_bridge.py 192.168.1.108          # asks for the password, then waits (Ctrl-C to stop)
  python3 tools/pi_bridge.py --detach 192.168.1.108  # the same, then in the background (the shell may be closed)
  python3 tools/pi_bridge.py --board rgb30 IP        # the board: pi (default), zero2, rgb30 (easy_install.sh bridge [profile] passes it)
  python3 tools/pi.py line render                       # the client (another shell, or Claude)
  python3 tools/pi.py status                          # is the Pi up, which kernel, how did the run before end

Operations: status, logs NAME (bootprev, bootlog, lastrun: saved in out/pi_*.txt and shown), keys NAME (monitor, game-exit: Ctrl-backslash ends the game, crash-*, freeze, freeze-irq (IRQs on: the guard restarts the Pi in about 23 s), reboot: the last ones only when the monitor has the keys), line TEXT (monitor commands separated by ';', only b3d tests=.. profiles=.., render, log, err, send, reboot; set/save/play and anything else refused), send FILE, play FILE, kernel FILE, shot OUT.png, config KEY=VALUE.. (words of letters, digits and _-. only).
One bridge per device: --dev NAME gives the socket ~/.pi_bridge.NAME.sock and the files out/pi_NAME_*.txt.
The board decides the kernel file kernel FILE takes (pi: any name, as before; zero2: kernel7.img, rgb30: kernel8.img); the RGB30 has no monitor, so its crash, freeze and reboot keys answer "not for this board".
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
KEYS = {"monitor": "q", "crash-dabt": "Xa", "crash-undef": "Xu", "crash-prefetch": "Xb", "freeze": "Xf", "freeze-irq": "Xg", "reboot": "r"}
# these keys mean something else in the menu (X opens the options, r is an action): only when the monitor has them
MONITOR_ONLY = {"crash-dabt", "crash-undef", "crash-prefetch", "freeze", "freeze-irq", "reboot"}
# the logs of the SD card the Pi writes at a crash or a boot (netxfer.c G allows these three names only, in any case)
LOGS = {"bootprev": "bootprev.txt", "bootlog": "bootlog.txt", "lastrun": "lastrun.txt"}
# the monitor commands the line operation passes (the old words of the table `commands` in src/kernel/monitor.c; set, save and play are left out:
# set writes any config key, wifi_psk and net_password included; play is the bridge's play FILE)
LINE_CMDS = ("b3d", "render", "log", "err", "crc", "send", "reboot")
# letters, digits, space and _ , = ; only (a control character would end the line early: bm_net.py closes it with a carriage return)
LINE_CHARS = set(string.ascii_letters + string.digits + " _,=;")
# b3d (bench 3d) reads tests= and profiles= only (c_bench_3d in monitor.c); their names are [A-Za-z0-9_] (src/bm/b3d.c prof_short, test names)
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
            return None, "'%s' is not allowed (allowed: b3d, render, log, err, crc, send, reboot)" % words[0]
        if words[0] != "b3d" and len(words) > 1:
            return None, "'%s' takes no arguments" % words[0]
        for w in words[1:]:
            if not B3D_ARG.fullmatch(w):
                return None, "b3d: '%s' is not tests=... or profiles=..." % w
        segments.append(" ".join(words))
    return "; ".join(segments), None


# the kernel file name each board takes (bm_net.py --kernel answers KA for another one; refused here before it is sent)
KERNEL_FILE = {"pi": "kernel.img", "zero2": "kernel7.img", "rgb30": "kernel8.img"}
ALLOWED_DIRS = [ROOT] + [os.path.expanduser("~/" + d) for d in os.listdir(os.path.expanduser("~")) if d.startswith("bmk")]


def allowed(path):
    p = os.path.realpath(path)
    return os.path.isfile(p) and any(p.startswith(os.path.realpath(d) + os.sep) for d in ALLOWED_DIRS)


def allowed_dir(path):
    p = os.path.realpath(path)
    return os.path.isdir(p) and any(p == os.path.realpath(d) or p.startswith(os.path.realpath(d) + os.sep) for d in ALLOWED_DIRS)


def status(host, pw):
    """up or down (the greeting of the console), the version, the state of the run before (bootprev.txt)"""
    v = bm_net.console_version(host)
    if v is None:
        return "status: down - no answer from %s:%d (off, restarting, frozen, or another client connected)" % (host, bm_net.PORT)
    out_file = os.path.join(ROOT, "out", "pi_" + (DEV + "_" if DEV else "") + "status_bootprev.txt")
    os.makedirs(os.path.dirname(out_file), exist_ok=True)
    cmd = [sys.executable, os.path.join(HERE, "bm_net.py"), host, "--get", "bm/bootprev.txt", out_file]
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


def board_refusal(req, board):
    """why the bridge of this board refuses req, or None (the board decides only: kernel names and monitor-only keys)"""
    op, _, arg = req.partition(" ")
    if board not in KERNEL_FILE:
        return "refused: unknown board %r" % board
    if op == "kernel" and board != "pi" and os.path.basename(arg) != KERNEL_FILE[board]:   # pi: any name, as before (kernelA.img)
        return "refused: the %s board takes %s only, not %s" % (board, KERNEL_FILE[board], os.path.basename(arg))
    if op == "keys" and board == "rgb30" and arg in MONITOR_ONLY:
        return "refused: keys %s: not for this board (the RGB30 has no monitor)" % arg
    return None


def run(host, pw, req, board="pi"):
    op, _, arg = req.partition(" ")
    why = board_refusal(req, board)
    if why:
        return why
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
    elif op == "logs" and (arg in LOGS or re.fullmatch(r"bat[0-9]{2}", arg)):
        if arg not in LOGS:
            LOGS[arg] = arg + ".csv"        # the RGB30's battery log (battery.c): bm/bat00.csv ... bm/bat99.csv
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


def get_password(host, environ=os.environ, ask=getpass.getpass):
    """(password, where it came from, None) or (None, None, why): BM_PASSWORD, then the file BM_PASSWORD_FILE or
    ~/.pi_bridge.pw (the owner writes it, mode 0600, one line), then the prompt. The value is never shown."""
    if environ.get("BM_PASSWORD"):
        return environ["BM_PASSWORD"], "BM_PASSWORD", None
    path = os.path.expanduser(environ.get("BM_PASSWORD_FILE") or "~/.pi_bridge.pw")
    try:
        fd = os.open(path, os.O_RDONLY)
    except OSError:
        fd = None
    if fd is not None:
        try:
            st = os.fstat(fd)
            if hasattr(os, "getuid") and (st.st_mode & 0o077 or st.st_uid != os.getuid()):
                return None, None, "refused: password file mode (%s must be yours and mode 0600: chmod 600 it)" % path
            line = os.read(fd, 4096).decode("utf-8", "replace").splitlines()
        finally:
            os.close(fd)
        pw = line[0].strip() if line else ""
        if not pw:
            return None, None, "refused: password file %s is empty (one line, the password)" % path
        return pw, "file " + path, None
    return ask("console password of %s (stays in this process): " % host), "the prompt", None


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
    board = "pi"
    if "--board" in args:
        i = args.index("--board")
        board = args[i + 1] if i + 1 < len(args) else ""
        del args[i:i + 2]
    if board not in KERNEL_FILE:
        sys.exit("--board: pi, zero2 or rgb30")
    names = [a for a in args if not a.startswith("--")]
    host = names[0] if names else "192.168.1.108"
    s = claim_socket(SOCK)
    pw, where, why = get_password(host)
    if why:
        print(why)
        s.close()
        os.unlink(SOCK)
        sys.exit(2)
    print("password from", where, flush=True)
    signal.signal(signal.SIGTERM, signal.SIG_IGN)   # the socket is removed below
    if background:
        detach()
    print("bridge ready on", SOCK, "(board %s) - Ctrl-C to stop" % board, flush=True)
    try:
        while True:
            c, _ = s.accept()
            try:
                req = c.makefile().readline().strip()
                print("request:", masked(req), flush=True)
                try:
                    out = run(host, pw, req, board)
                except Exception as e:
                    out = "error: %s" % e
                c.sendall(out.encode("utf-8", "replace"))
            except Exception as e:
                print("client connection error:", e, flush=True)
            finally:
                c.close()
    finally:
        s.close()
        if os.path.exists(SOCK):
            os.unlink(SOCK)


if __name__ == "__main__":
    main()
