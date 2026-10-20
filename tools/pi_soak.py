#!/usr/bin/env python3
"""A long session on the Pi, driven from the PC through the bridge (tools/pi_bridge.py); it never sees the password.

  python3 tools/pi_soak.py --cart build/carts/stages.bm --minutes 30 --reboot

Sequence: status (the Pi must be up), play FILE, a status every --every seconds for --minutes, keys monitor (the game
ends and prints its fps line), with --reboot also line reboot, wait for the Pi, previous run and logs lastrun.
Three lines 'soak: ...' on stdout. Exit: 0 verdict OK, 1 verdict FAIL, 2 usage or no bridge, 3 the Pi does not answer.
WSL: needs Unix sockets (the bridge socket is ~/.pi_bridge.sock)."""
import argparse
import os
import re
import socket
import sys
import time

POLL_REBOOT = 10        # seconds between two status asks while the Pi restarts
WAIT_REBOOT = 180      # seconds after the reboot before the Pi counts as lost
ASK_TIMEOUT = 320      # the bridge answers within 300 s (pi_bridge.py)
# the line bm_print_stats prints when a game ends (src/bm/runtime.c)
FPS_LINE = re.compile(r'bm: "([^"]*)" (\d+) frames, (\d+\.\d) fps, (\d+) dropped')


def first_line(text):
    return text.strip().splitlines()[0] if text.strip() else "(empty answer)"


def ask(sock_path, request):
    """One request to the bridge, its whole answer. None when the bridge is not there."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(ASK_TIMEOUT)
    try:
        s.connect(sock_path)
    except OSError:
        s.close()
        return None
    try:
        s.sendall((request + "\n").encode())
        data = b""
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
    finally:
        s.close()
    return data.decode("utf-8", "replace")


def bad_answer(text):
    t = text.lstrip()
    return t.startswith("refused") or "refused:" in text or "timeout:" in text or "no answer" in text


def main(argv=None, sleep=time.sleep, now=time.monotonic):
    ap = argparse.ArgumentParser(prog="pi_soak.py", description="a long session on the Pi through the bridge")
    ap.add_argument("--cart", required=True, help="the .bm to play (inside the repo or ~/bmk*, as the bridge wants)")
    ap.add_argument("--minutes", required=True, type=float)
    ap.add_argument("--every", type=float, default=60.0, help="seconds between two checks")
    ap.add_argument("--reboot", action="store_true", help="reboot at the end and read the run before (needs the user's ok)")
    ap.add_argument("--min-fps", type=float, default=55.0)
    ap.add_argument("--sock", default="~/.pi_bridge.sock")
    try:
        a = ap.parse_args(argv)
    except SystemExit:
        return 2
    if a.minutes <= 0 or a.every <= 0:
        print("minutes and every must be above 0")
        return 2
    if not hasattr(socket, "AF_UNIX"):
        print("needs Unix sockets: run it in WSL")
        return 2
    sock = os.path.expanduser(a.sock)
    n = int(a.minutes * 60 // a.every)
    if n < 1:
        print("minutes must be at least one --every")
        return 2

    def q(request):
        r = ask(sock, request)
        if r is None:
            print("the bridge is not running (python3 tools/pi_bridge.py <ip>)")
            raise ConnectionError
        return r

    why = []
    up = down = 0
    fps_line = None
    prev = None
    try:
        st = q("status")
        if not st.startswith("status: up"):
            print("\n".join(st.strip().splitlines()[:3]))
            return 3
        p = q("play " + os.path.abspath(a.cart))
        if bad_answer(p):
            why.append("play failed: " + first_line(p))
        else:
            for i in range(1, n + 1):
                sleep(a.every)
                alive = q("status").startswith("status: up")
                up += alive
                down += not alive
                print("soak: check %d/%d %s" % (i, n, "up" if alive else "down"), file=sys.stderr)
            q("keys game-exit")             # a game takes the keys: Ctrl-\ ends it, then the monitor has them
            k = q("keys monitor")
            if "back to the menu first" in k or k.lstrip().startswith("refused"):
                why.append("cannot end the game from the PC: " + first_line(k))
            else:
                found = FPS_LINE.findall(k)
                if found:
                    fps_line = found[-1]
                if a.reboot:
                    r = q("line reboot")
                    if r.lstrip().startswith("refused"):
                        why.append("reboot refused")
                    else:
                        back = None
                        for _ in range(WAIT_REBOOT // POLL_REBOOT):
                            sleep(POLL_REBOOT)
                            s2 = q("status")
                            if s2.startswith("status: up"):
                                back = s2
                                break
                        if back is None:
                            why.append("no answer %d s after the reboot" % WAIT_REBOOT)
                        else:
                            m = re.search(r"previous run: (.*)", back)
                            prev = m.group(1).strip() if m else None
                            lg = q("logs lastrun")
                            if fps_line is None:
                                found = FPS_LINE.findall(lg)
                                if found:
                                    fps_line = found[-1]
    except ConnectionError:
        return 2

    if down > 0:
        why.append("%d status down" % down)
    if fps_line is None:
        if not any(w.startswith(("play failed", "cannot end")) for w in why):
            why.append("fps line missing")
    elif float(fps_line[2]) < a.min_fps:
        why.append("fps %s below %g" % (fps_line[2], a.min_fps))
    if a.reboot and not any(w.startswith(("play failed", "cannot end", "reboot refused", "no answer")) for w in why):
        if prev is None:
            why.append("previous run unknown")
        elif not prev.startswith("ended cleanly"):
            why.append("previous run: " + prev)

    print("soak: %s %g min, %d checks, up %d down %d" % (os.path.basename(a.cart), a.minutes, n, up, down))
    if fps_line:
        print('soak: "%s" %s frames, %s fps (min %g), %s dropped' % (fps_line[0], fps_line[1], fps_line[2], a.min_fps, fps_line[3]))
    else:
        print("soak: no fps line")
    shown = (prev if prev else "unknown") if a.reboot else "not checked"
    print("soak: previous run %s; verdict %s" % (shown, "OK" if not why else "FAIL (" + "; ".join(why) + ")"))
    return 0 if not why else 1


if __name__ == "__main__":
    sys.exit(main())
